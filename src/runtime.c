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
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

typedef struct ct_filter_snapshot {
  char *include;
  char *exclude;
  struct ct_filter_snapshot *next;
} ct_filter_snapshot;

typedef struct ct_trace_targets {
  uintptr_t *addrs;
  size_t n;
  struct ct_trace_targets *next;
} ct_trace_targets;

#define CT_FCACHE_SIZE 256

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static pthread_key_t g_key;
static pthread_mutex_t g_retired_mu = PTHREAD_MUTEX_INITIALIZER;
static ct_buffer *g_retired_head = NULL;
static ct_filter_snapshot *g_filter_retired = NULL;
static ct_trace_targets *g_trace_retired = NULL;

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
static _Atomic(ct_trace_targets *) g_trace = NULL;
static _Atomic int g_ctl_started = 0;

static __thread ct_buffer *tls_buf = NULL;
static __thread int tls_in_hook = 0;
static __thread int tls_truncated = 0;
static __thread unsigned tls_depth = 0;
static __thread int tls_capture = 0;
static __thread unsigned tls_capture_depth = 0;
static __thread uint32_t tls_tid = 0;
static __thread int tls_tid_set = 0;
static __thread struct {
  uintptr_t addr;
  int decision;
  unsigned gen;
} tls_fcache[CT_FCACHE_SIZE];

CT_NOINSTR static ct_trace_targets *build_trace_targets(const char *pattern);
CT_NOINSTR static void start_control(void);

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

  ct_trace_targets *tt = g_trace_retired;
  while (tt) {
    ct_trace_targets *nx = tt->next;
    free(tt->addrs);
    free(tt);
    tt = nx;
  }
  g_trace_retired = NULL;

  ct_config_clear(&g_cfg);
  pthread_mutex_unlock(&g_retired_mu);
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
  atomic_store(&g_trace, build_trace_targets(g_cfg.trace_pattern));

  pthread_key_create(&g_key, retire_thread_buffer);
  atexit(ct_atexit);
  start_control();
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

CT_NOINSTR static int cmp_uptr(const void *a, const void *b) {
  uintptr_t x = *(const uintptr_t *)a, y = *(const uintptr_t *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

CT_NOINSTR static ct_trace_targets *build_trace_targets(const char *pattern) {
  ct_trace_targets *t = (ct_trace_targets *)calloc(1, sizeof(*t));
  if (!t) return NULL;
  if (pattern == NULL || pattern[0] == '\0') return t;
  size_t cap = 16;
  t->addrs = (uintptr_t *)malloc(cap * sizeof(uintptr_t));
  if (!t->addrs) { free(t); return NULL; }
  for (size_t i = 0; i < g_syms.n_symbols; i++) {
    if (!ct_filter_match(pattern, NULL, g_syms.syms[i].name)) continue;
    if (t->n == cap) {
      size_t nc = cap * 2;
      uintptr_t *na = (uintptr_t *)realloc(t->addrs, nc * sizeof(uintptr_t));
      if (!na) break;
      t->addrs = na;
      cap = nc;
    }
    t->addrs[t->n++] = (uintptr_t)g_syms.syms[i].addr;
  }
  qsort(t->addrs, t->n, sizeof(uintptr_t), cmp_uptr);
  return t;
}

CT_NOINSTR static ct_trace_targets *trace_snapshot(void) {
  return atomic_load(&g_trace);
}

CT_NOINSTR static int trace_configured(void) {
  ct_trace_targets *t = trace_snapshot();
  return t && t->n > 0;
}

CT_NOINSTR static int trigger_match(uintptr_t fn) {
  ct_trace_targets *t = trace_snapshot();
  if (!t) return 0;
  size_t lo = 0, hi = t->n;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    uintptr_t a = t->addrs[mid];
    if (a == fn) return 1;
    if (a < fn) lo = mid + 1;
    else hi = mid;
  }
  return 0;
}

CT_NOINSTR static int pass_exclusion(uintptr_t fn) {
  const char *name = ct_symbols_lookup(&g_syms, fn, NULL, NULL);
  if (!name) return g_cfg.drop_unknown ? 0 : 1;
  if (g_cfg.exclude_lib && ct_lib_name_match(name)) return 0;
  ct_filter_snapshot *s = atomic_load(&g_filter);
  return ct_filter_match(NULL, s ? s->exclude : NULL, name);
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

CT_NOINSTR static void record(uintptr_t fn, uintptr_t cs, ct_event_kind kind, unsigned depth, int forced) {
  if (tls_in_hook) return;
  tls_in_hook = 1;

  if (!atomic_load(&g_started)) pthread_once(&g_once, init_once);
  if (!atomic_load(&g_started) || !atomic_load(&g_enabled) || tls_truncated) {
    tls_in_hook = 0;
    return;
  }

  unsigned maxd = atomic_load(&g_max_depth);
  int ok = forced ? pass_exclusion(fn)
                  : (!trace_configured() && pass_filter(fn));
  if ((maxd == 0 || depth < maxd) && ok) {
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

CT_NOINSTR void __cyg_profile_func_enter(void *fnp, void *cs) {
  uintptr_t fn = (uintptr_t)fnp;
  unsigned d = tls_depth;
  int forced = 0;
  if (trace_configured()) {
    if (!tls_capture && trigger_match(fn)) { tls_capture = 1; tls_capture_depth = d; }
    forced = tls_capture;
  }
  record(fn, (uintptr_t)cs, CT_EV_ENTER, d, forced);
  tls_depth = d + 1;
}

CT_NOINSTR void __cyg_profile_func_exit(void *fnp, void *cs) {
  uintptr_t fn = (uintptr_t)fnp;
  if (tls_depth) tls_depth--;
  unsigned d = tls_depth;
  int forced = (trace_configured() && tls_capture);
  record(fn, (uintptr_t)cs, CT_EV_EXIT, d, forced);
  if (tls_capture && d == tls_capture_depth) tls_capture = 0;
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
  char *ni = include ? strdup(include) : NULL;
  char *ne = exclude ? strdup(exclude) : NULL;
  ct_filter_snapshot *s = make_snapshot(ni, ne);
  ct_filter_snapshot *old = atomic_exchange(&g_filter, s);
  atomic_fetch_add(&g_filter_gen, 1);
  pthread_mutex_lock(&g_retired_mu);
  if (old) {
    old->next = g_filter_retired;
    g_filter_retired = old;
  }
  if (g_cfg.include) free(g_cfg.include);
  if (g_cfg.exclude) free(g_cfg.exclude);
  g_cfg.include = ni;
  g_cfg.exclude = ne;
  pthread_mutex_unlock(&g_retired_mu);
}

CT_NOINSTR CTIMING_HIDDEN void ctiming_set_max_depth(unsigned depth) {
  pthread_once(&g_once, init_once);
  atomic_store(&g_max_depth, depth);
}

CT_NOINSTR CTIMING_HIDDEN int ctiming_set_trace_symbol(const char *pattern) {
  pthread_once(&g_once, init_once);
  ct_trace_targets *t = build_trace_targets(pattern);
  if (!t) return -1;
  ct_trace_targets *old = atomic_exchange(&g_trace, t);
  if (old) {
    pthread_mutex_lock(&g_retired_mu);
    old->next = g_trace_retired;
    g_trace_retired = old;
    pthread_mutex_unlock(&g_retired_mu);
  }
  return (int)t->n;
}

CT_NOINSTR static void ctl_reply(const char *msg) {
  fprintf(stderr, "ctiming: %s\n", msg);
}

CT_NOINSTR static void ctl_command(char *line) {
  char *save = NULL;
  char *cmd = strtok_r(line, " \t\r\n", &save);
  if (!cmd || !*cmd) return;
  if (strcmp(cmd, "start") == 0) { ctiming_start(); ctl_reply("recording on"); }
  else if (strcmp(cmd, "stop") == 0) { ctiming_stop(); ctl_reply("recording off"); }
  else if (strcmp(cmd, "toggle") == 0) {
    if (atomic_load(&g_enabled)) { ctiming_stop(); ctl_reply("recording off"); }
    else { ctiming_start(); ctl_reply("recording on"); }
  }
  else if (strcmp(cmd, "dump") == 0) {
    char *path = strtok_r(NULL, " \t\r\n", &save);
    int rc = ctiming_dump(path);
    ctl_reply(rc == 0 ? "dumped" : "dump failed");
  }
  else if (strcmp(cmd, "trace") == 0) {
    char *arg = strtok_r(NULL, "", &save);
    while (arg && (*arg == ' ' || *arg == '\t')) arg++;
    if (!arg || !*arg || strcmp(arg, "off") == 0) {
      ctiming_set_trace_symbol(NULL);
      ctl_reply("trace cleared");
    } else {
      int n = ctiming_set_trace_symbol(arg);
      char buf[128];
      snprintf(buf, sizeof(buf), "trace '%s' -> %d address(es)", arg, n);
      ctl_reply(buf);
    }
  }
  else if (strcmp(cmd, "untrace") == 0) { ctiming_set_trace_symbol(NULL); ctl_reply("trace cleared"); }
  else if (strcmp(cmd, "include") == 0) {
    char *arg = strtok_r(NULL, "", &save);
    while (arg && (*arg == ' ' || *arg == '\t')) arg++;
    ctiming_set_filter((arg && *arg) ? arg : NULL, g_cfg.exclude);
    ctl_reply("include set");
  }
  else if (strcmp(cmd, "exclude") == 0) {
    char *arg = strtok_r(NULL, "", &save);
    while (arg && (*arg == ' ' || *arg == '\t')) arg++;
    ctiming_set_filter(g_cfg.include, (arg && *arg) ? arg : NULL);
    ctl_reply("exclude set");
  }
  else if (strcmp(cmd, "status") == 0) {
    char buf[256];
    snprintf(buf, sizeof(buf), "enabled=%d trace=%s include=%s exclude=%s",
             atomic_load(&g_enabled), trace_configured() ? "on" : "off",
             g_cfg.include ? g_cfg.include : "-", g_cfg.exclude ? g_cfg.exclude : "-");
    ctl_reply(buf);
  }
  else ctl_reply("unknown command");
}

CT_NOINSTR static void *ctl_thread_main(void *arg) {
  (void)arg;
  const char *path = g_cfg.ctl_path;
  if (mkfifo(path, 0600) != 0 && errno != EEXIST) { ctl_reply("mkfifo failed"); return NULL; }
  int fd = open(path, O_RDWR);
  if (fd < 0) { ctl_reply("open ctl failed"); return NULL; }
  char line[512];
  size_t len = 0;
  for (;;) {
    char c;
    ssize_t r = read(fd, &c, 1);
    if (r <= 0) { if (r < 0 && errno == EINTR) continue; break; }
    if (c == '\n') {
      line[len] = '\0';
      ctl_command(line);
      len = 0;
    } else if (len + 1 < sizeof(line)) {
      line[len++] = c;
    }
  }
  close(fd);
  return NULL;
}

CT_NOINSTR static void start_control(void) {
  if (g_cfg.ctl_path == NULL || g_cfg.ctl_path[0] == '\0') return;
  if (atomic_exchange(&g_ctl_started, 1)) return;
  pthread_t th;
  if (pthread_create(&th, NULL, ctl_thread_main, NULL) == 0) pthread_detach(th);
}
