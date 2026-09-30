#include "ct_common.h"
#include "ctiming.h"
#include "buffer.h"
#include "config.h"
#include "glob.h"
#include "symbols.h"
#include "trace.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_key_t g_key;
static pthread_mutex_t g_retired_mu = PTHREAD_MUTEX_INITIALIZER;
static ct_buffer *g_retired_head = NULL;

static ct_config g_cfg;
static ct_symbol_table g_syms;
static char g_progname[512];
static _Atomic int g_started = 0;
static _Atomic int g_enabled = 1;

static __thread ct_buffer *tls_buf = NULL;
static __thread int tls_in_hook = 0;
static __thread unsigned tls_depth = 0;

CT_NOINSTR static uint64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

CT_NOINSTR static uint32_t current_tid(void) {
  return (uint32_t)syscall(SYS_gettid);
}

CT_NOINSTR static void retire_thread_buffer(void *p) {
  ct_buffer *b = (ct_buffer *)p;
  if (!b) return;
  pthread_mutex_lock(&g_retired_mu);
  b->next = g_retired_head;
  g_retired_head = b;
  pthread_mutex_unlock(&g_retired_mu);
}

CT_NOINSTR static int append_buffer(ct_buffer ***bufs, size_t *n, size_t *cap, ct_buffer *b) {
  if (*n == *cap) {
    size_t nc = *cap ? *cap * 2 : 16;
    ct_buffer **nb = (ct_buffer **)realloc(*bufs, nc * sizeof(**bufs));
    if (!nb) return -1;
    *bufs = nb;
    *cap = nc;
  }
  (*bufs)[(*n)++] = b;
  return 0;
}

CT_NOINSTR static void dump_locked(void) {
  ct_buffer **bufs = NULL;
  size_t n = 0, cap = 0;

  for (ct_buffer *b = g_retired_head; b; b = b->next) {
    if (b->count == 0) continue;
    if (append_buffer(&bufs, &n, &cap, b) != 0) break;
  }
  if (tls_buf && tls_buf->count > 0) {
    append_buffer(&bufs, &n, &cap, tls_buf);
  }

  ct_trace_module *mods = NULL;
  size_t nmods = 0;
  if (g_syms.n_modules > 0) {
    mods = (ct_trace_module *)malloc(g_syms.n_modules * sizeof(*mods));
    if (mods) {
      memcpy(mods, g_syms.modules, g_syms.n_modules * sizeof(*mods));
      nmods = g_syms.n_modules;
    }
  }

  ct_trace_symbol *syms = NULL;
  size_t nsyms = 0;
  if (g_syms.n_symbols > 0) {
    syms = (ct_trace_symbol *)malloc(g_syms.n_symbols * sizeof(*syms));
    if (syms) {
      for (size_t i = 0; i < g_syms.n_symbols; i++) {
        uint32_t m = g_syms.syms[i].module;
        syms[i].module = m;
        syms[i].offset = (m < g_syms.n_modules)
                             ? g_syms.syms[i].addr - g_syms.modules[m].base
                             : g_syms.syms[i].addr;
        snprintf(syms[i].name, sizeof(syms[i].name), "%s", g_syms.syms[i].name);
      }
      nsyms = g_syms.n_symbols;
    }
  }

  ct_trace_meta meta;
  memset(&meta, 0, sizeof(meta));
  meta.pid = (uint32_t)getpid();
  meta.start_ns = now_ns();
  meta.flags = 0;
  snprintf(meta.exe, CT_PATH_MAX, "%s", nmods > 0 ? mods[0].path : "");

  ct_trace_write(g_cfg.out_path, (const ct_buffer *const *)bufs, n,
                 mods, nmods, syms, nsyms, &meta);

  free(syms);
  free(mods);
  free(bufs);
}

CT_NOINSTR static void ct_atexit(void) {
  pthread_mutex_lock(&g_retired_mu);
  dump_locked();
  pthread_mutex_unlock(&g_retired_mu);
}

CT_NOINSTR static void init_once(void) {
  char exe[512];
  ssize_t k = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (k > 0) {
    exe[k] = '\0';
    const char *slash = strrchr(exe, '/');
    snprintf(g_progname, sizeof(g_progname), "%s", slash ? slash + 1 : exe);
  } else {
    snprintf(g_progname, sizeof(g_progname), "ctiming");
  }

  ct_config_load(&g_cfg, g_progname);
  ct_symbols_load(&g_syms);
  atomic_store(&g_enabled, g_cfg.enabled);

  pthread_key_create(&g_key, retire_thread_buffer);
  atexit(ct_atexit);
  atomic_store(&g_started, 1);
}

CT_NOINSTR static ct_buffer *current_buffer(void) {
  if (!tls_buf) {
    tls_buf = ct_buffer_new(8192);
    if (tls_buf) pthread_setspecific(g_key, tls_buf);
  }
  return tls_buf;
}

CT_NOINSTR static int pass_filter(uintptr_t fn) {
  if (!g_cfg.include && !g_cfg.exclude) return 1;
  const char *name = ct_symbols_lookup(&g_syms, fn, NULL, NULL);
  if (!name) return g_cfg.include ? 0 : 1;
  return ct_filter_match(g_cfg.include, g_cfg.exclude, name);
}

CT_NOINSTR static void record(uintptr_t fn, uintptr_t cs, ct_event_kind kind, unsigned depth) {
  if (!atomic_load(&g_enabled)) return;
  if (tls_in_hook) return;
  tls_in_hook = 1;

  pthread_once(&g_once, init_once);
  if (!atomic_load(&g_started)) {
    tls_in_hook = 0;
    return;
  }

  if ((g_cfg.max_depth == 0 || depth < g_cfg.max_depth) && pass_filter(fn)) {
    ct_buffer *b = current_buffer();
    if (b) {
      ct_event e;
      e.tid = current_tid();
      e.kind = (uint8_t)kind;
      e.ts = now_ns();
      e.fn = fn;
      e.call_site = cs;
      ct_buffer_push(b, e);
    }
  }
  tls_in_hook = 0;
}

CT_NOINSTR void __cyg_profile_func_enter(void *fn, void *cs) {
  record((uintptr_t)fn, (uintptr_t)cs, CT_EV_ENTER, tls_depth);
  tls_depth++;
}

CT_NOINSTR void __cyg_profile_func_exit(void *fn, void *cs) {
  if (tls_depth) tls_depth--;
  record((uintptr_t)fn, (uintptr_t)cs, CT_EV_EXIT, tls_depth);
}

CT_NOINSTR CTIMING_HIDDEN const char *ctiming_version(void) { return CT_VERSION_STRING; }

CT_NOINSTR CTIMING_HIDDEN void ctiming_start(void) {
  pthread_once(&g_once, init_once);
  atomic_store(&g_enabled, 1);
}

CT_NOINSTR CTIMING_HIDDEN void ctiming_stop(void) { atomic_store(&g_enabled, 0); }

CT_NOINSTR CTIMING_HIDDEN int ctiming_dump(const char *path) {
  pthread_once(&g_once, init_once);
  pthread_mutex_lock(&g_retired_mu);
  char prev[CT_PATH_MAX];
  snprintf(prev, CT_PATH_MAX, "%s", g_cfg.out_path);
  if (path) snprintf(g_cfg.out_path, CT_PATH_MAX, "%s", path);
  dump_locked();
  snprintf(g_cfg.out_path, CT_PATH_MAX, "%s", prev);
  pthread_mutex_unlock(&g_retired_mu);
  return 0;
}

CT_NOINSTR CTIMING_HIDDEN void ctiming_set_filter(const char *include, const char *exclude) {
  pthread_once(&g_once, init_once);
  free(g_cfg.include);
  free(g_cfg.exclude);
  g_cfg.include = include ? strdup(include) : NULL;
  g_cfg.exclude = exclude ? strdup(exclude) : NULL;
}

CT_NOINSTR CTIMING_HIDDEN void ctiming_set_max_depth(unsigned depth) {
  pthread_once(&g_once, init_once);
  g_cfg.max_depth = depth;
}
