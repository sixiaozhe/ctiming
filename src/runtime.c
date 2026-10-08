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

typedef struct ct_filter_snapshot {
  char *include;
  char *exclude;
  struct ct_filter_snapshot *next;
} ct_filter_snapshot;

#define CT_FCACHE_SIZE 256

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_key_t g_key;
static pthread_mutex_t g_retired_mu = PTHREAD_MUTEX_INITIALIZER;
static ct_buffer *g_retired_head = NULL;
static ct_filter_snapshot *g_filter_retired = NULL;

static ct_config g_cfg;
static ct_symbol_table g_syms;
static char g_progname[512];
static char g_exe_path[CT_PATH_MAX];
static uint64_t g_start_ns;
static _Atomic int g_started = 0;
static _Atomic int g_enabled = 1;
static _Atomic unsigned g_max_depth = 0;
static _Atomic(ct_filter_snapshot *) g_filter = NULL;
static _Atomic unsigned g_filter_gen = 0;

static __thread ct_buffer *tls_buf = NULL;
static __thread int tls_in_hook = 0;
static __thread int tls_truncated = 0;
static __thread unsigned tls_depth = 0;
static __thread uint32_t tls_tid = 0;
static __thread int tls_tid_set = 0;
static __thread struct {
  uintptr_t addr;
  int decision;
  unsigned gen;
} tls_fcache[CT_FCACHE_SIZE];

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

CT_NOINSTR static ct_filter_snapshot *make_snapshot(const char *include, const char *exclude) {
  ct_filter_snapshot *s = (ct_filter_snapshot *)calloc(1, sizeof(*s));
  if (!s) return NULL;
  if (include) {
    s->include = strdup(include);
    if (!s->include) { free(s); return NULL; }
  }
  if (exclude) {
    s->exclude = strdup(exclude);
    if (!s->exclude) { free(s->include); free(s); return NULL; }
  }
  return s;
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

CT_NOINSTR static int dump_locked(void) {
  ct_buffer **bufs = NULL;
  size_t n = 0, cap = 0;
  int any_truncated = 0;

  for (ct_buffer *b = g_retired_head; b; b = b->next) {
    if (b->truncated) any_truncated = 1;
    if (b->count == 0) continue;
    if (append_buffer(&bufs, &n, &cap, b) != 0) break;
  }
  if (tls_buf) {
    if (tls_buf->truncated) any_truncated = 1;
    if (tls_buf->count > 0) append_buffer(&bufs, &n, &cap, tls_buf);
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
  meta.start_ns = g_start_ns;
  meta.flags = any_truncated ? CT_META_FLAG_TRUNCATED : 0;
  snprintf(meta.exe, CT_PATH_MAX, "%s", g_exe_path);

  int rc = ct_trace_write(g_cfg.out_path, (const ct_buffer *const *)bufs, n,
                          mods, nmods, syms, nsyms, &meta);

  free(syms);
  free(mods);
  free(bufs);
  return rc;
}

CT_NOINSTR static void ct_atexit(void) {
  pthread_mutex_lock(&g_retired_mu);
  dump_locked();

  ct_filter_snapshot *s = g_filter_retired;
  while (s) {
    ct_filter_snapshot *nx = s->next;
    free(s->include);
    free(s->exclude);
    free(s);
    s = nx;
  }
  g_filter_retired = NULL;
  pthread_mutex_unlock(&g_retired_mu);

  ct_config_clear(&g_cfg);
}

CT_NOINSTR static void init_once(void) {
  char exe[512];
  ssize_t k = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (k > 0) {
    exe[k] = '\0';
    snprintf(g_exe_path, sizeof(g_exe_path), "%s", exe);
    const char *slash = strrchr(exe, '/');
    snprintf(g_progname, sizeof(g_progname), "%s", slash ? slash + 1 : exe);
  } else {
    g_exe_path[0] = '\0';
    snprintf(g_progname, sizeof(g_progname), "ctiming");
  }
  g_start_ns = now_ns();

  ct_config_load(&g_cfg, g_progname);
  ct_symbols_load(&g_syms);
  atomic_store(&g_enabled, g_cfg.enabled);
  atomic_store(&g_max_depth, g_cfg.max_depth);
  atomic_store(&g_filter, make_snapshot(g_cfg.include, g_cfg.exclude));

  pthread_key_create(&g_key, retire_thread_buffer);
  atexit(ct_atexit);
  atomic_store(&g_started, 1);
}

CT_NOINSTR static ct_buffer *current_buffer(void) {
  if (!tls_buf) {
    size_t esz = sizeof(ct_event);
    size_t init_cap = (size_t)g_cfg.buf_kb * 1024 / esz;
    if (init_cap < 1) init_cap = 1;
    tls_buf = ct_buffer_new(init_cap);
    if (tls_buf) {
      ct_buffer_set_max(tls_buf, (size_t)g_cfg.buf_max_kb * 1024 / esz);
      pthread_setspecific(g_key, tls_buf);
    }
  }
  if (!tls_tid_set) {
    tls_tid = current_tid();
    tls_tid_set = 1;
  }
  return tls_buf;
}

CT_NOINSTR static int pass_filter(uintptr_t fn) {
  unsigned gen = atomic_load(&g_filter_gen);
  ct_filter_snapshot *s = atomic_load(&g_filter);
  int have_filter = (s && (s->include || s->exclude));
  if (!have_filter && !g_cfg.drop_unknown && !g_cfg.exclude_lib) return 1;

  unsigned idx = (unsigned)((fn >> 4) & (CT_FCACHE_SIZE - 1));
  if (tls_fcache[idx].gen == gen && tls_fcache[idx].addr == fn)
    return tls_fcache[idx].decision;

  const char *name = ct_symbols_lookup(&g_syms, fn, NULL, NULL);
  int decision;
  if (!name) decision = g_cfg.drop_unknown ? 0 : 1;
  else if (g_cfg.exclude_lib && ct_lib_name_match(name)) decision = 0;
  else decision = have_filter ? ct_filter_match(s->include, s->exclude, name) : 1;

  tls_fcache[idx].addr = fn;
  tls_fcache[idx].decision = decision;
  tls_fcache[idx].gen = gen;
  return decision;
}

CT_NOINSTR static void record(uintptr_t fn, uintptr_t cs, ct_event_kind kind, unsigned depth) {
  if (tls_in_hook) return;
  tls_in_hook = 1;

  if (!atomic_load(&g_started)) pthread_once(&g_once, init_once);
  if (!atomic_load(&g_started) || !atomic_load(&g_enabled) || tls_truncated) {
    tls_in_hook = 0;
    return;
  }

  unsigned maxd = atomic_load(&g_max_depth);
  if ((maxd == 0 || depth < maxd) && pass_filter(fn)) {
    ct_buffer *b = current_buffer();
    if (b) {
      ct_event e;
      e.tid = tls_tid;
      e.kind = (uint8_t)kind;
      e.ts = now_ns();
      e.fn = fn;
      e.call_site = cs;
      ct_buffer_push(b, e);
      if (b->truncated) tls_truncated = 1;
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

CT_NOINSTR CTIMING_HIDDEN void ctiming_stop(void) {
  pthread_once(&g_once, init_once);
  atomic_store(&g_enabled, 0);
}

CT_NOINSTR CTIMING_HIDDEN int ctiming_dump(const char *path) {
  pthread_once(&g_once, init_once);
  pthread_mutex_lock(&g_retired_mu);
  char prev[CT_PATH_MAX];
  snprintf(prev, CT_PATH_MAX, "%s", g_cfg.out_path);
  if (path) snprintf(g_cfg.out_path, CT_PATH_MAX, "%s", path);
  int rc = dump_locked();
  snprintf(g_cfg.out_path, CT_PATH_MAX, "%s", prev);
  pthread_mutex_unlock(&g_retired_mu);
  return rc;
}

CT_NOINSTR CTIMING_HIDDEN void ctiming_set_filter(const char *include, const char *exclude) {
  pthread_once(&g_once, init_once);
  ct_filter_snapshot *s = make_snapshot(include, exclude);
  ct_filter_snapshot *old = atomic_exchange(&g_filter, s);
  atomic_fetch_add(&g_filter_gen, 1);
  if (old) {
    pthread_mutex_lock(&g_retired_mu);
    old->next = g_filter_retired;
    g_filter_retired = old;
    pthread_mutex_unlock(&g_retired_mu);
  }
}

CT_NOINSTR CTIMING_HIDDEN void ctiming_set_max_depth(unsigned depth) {
  pthread_once(&g_once, init_once);
  atomic_store(&g_max_depth, depth);
}
