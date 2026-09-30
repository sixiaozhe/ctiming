#include "symbols.h"
#include "ct_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

extern char *__cxa_demangle(const char *mangled, char *buf, size_t *len, int *status)
    __attribute__((weak));

CT_NOINSTR static int add_module(ct_symbol_table *t, uint64_t base, const char *path) {
  if (t->n_modules == t->modules_cap) {
    size_t nc = t->modules_cap ? t->modules_cap * 2 : 16;
    if (nc < t->modules_cap) return -1;
    ct_trace_module *nm = (ct_trace_module *)realloc(t->modules, nc * sizeof(ct_trace_module));
    if (!nm) return -1;
    t->modules = nm;
    uint64_t *nh = (uint64_t *)realloc(t->module_hi, nc * sizeof(uint64_t));
    if (!nh) return -1;
    t->module_hi = nh;
    t->modules_cap = nc;
  }
  uint32_t idx = (uint32_t)t->n_modules;
  t->modules[idx].base = base;
  snprintf(t->modules[idx].path, CT_PATH_MAX, "%s", path);
  t->module_hi[idx] = 0;
  t->n_modules++;
  return (int)idx;
}

CT_NOINSTR static void add_symbol(ct_symbol_table *t, uint64_t addr, const char *name, uint32_t module) {
  if (t->n_symbols == t->syms_cap) {
    size_t nc = t->syms_cap ? t->syms_cap * 2 : 256;
    if (nc < t->syms_cap) return;
    ct_sym_entry *ns = (ct_sym_entry *)realloc(t->syms, nc * sizeof(ct_sym_entry));
    if (!ns) return;
    t->syms = ns;
    t->syms_cap = nc;
  }
  ct_sym_entry *e = &t->syms[t->n_symbols];
  e->addr = addr;
  e->module = module;
  snprintf(e->name, sizeof(e->name), "%s", name);
  t->n_symbols++;
}

CT_NOINSTR static void maybe_demangle(const char *raw, char *out, size_t cap) {
  if (__cxa_demangle) {
    int status = 0;
    char *d = __cxa_demangle(raw, NULL, NULL, &status);
    if (status == 0 && d) { snprintf(out, cap, "%s", d); free(d); return; }
  }
  snprintf(out, cap, "%s", raw);
}

CT_NOINSTR static int in_bounds(uint64_t off, uint64_t len, size_t size) {
  return off <= size && len <= (uint64_t)size - off;
}

CT_NOINSTR static void walk_elf(ct_symbol_table *t, const unsigned char *p, size_t size,
                                uint64_t base, uint32_t module) {
  if (!(p[0] == 0x7f && p[1] == 'E' && p[2] == 'L' && p[3] == 'F')) return;
  if (p[EI_CLASS] != ELFCLASS64 || p[EI_DATA] != ELFDATA2LSB) return;

  const Elf64_Ehdr *eh = (const Elf64_Ehdr *)p;
  if (eh->e_shoff == 0 || eh->e_shnum == 0 || eh->e_shentsize < sizeof(Elf64_Shdr)) return;
  if (!in_bounds(eh->e_shoff, (uint64_t)eh->e_shnum * eh->e_shentsize, size)) return;

  uint64_t bias = (eh->e_type == ET_EXEC) ? 0 : base;
  const Elf64_Shdr *sh = (const Elf64_Shdr *)(p + eh->e_shoff);

  for (unsigned i = 0; i < eh->e_shnum; i++) {
    if (sh[i].sh_type != SHT_SYMTAB && sh[i].sh_type != SHT_DYNSYM) continue;
    if (sh[i].sh_link >= eh->e_shnum) continue;
    if (!in_bounds(sh[i].sh_offset, sh[i].sh_size, size)) continue;
    const Elf64_Shdr *str = &sh[sh[i].sh_link];
    if (!in_bounds(str->sh_offset, str->sh_size, size)) continue;

    size_t n = (size_t)(sh[i].sh_size / sizeof(Elf64_Sym));
    const Elf64_Sym *syms = (const Elf64_Sym *)(p + sh[i].sh_offset);
    const char *strtab = (const char *)(p + str->sh_offset);
    size_t strsz = (size_t)str->sh_size;

    for (size_t j = 0; j < n; j++) {
      if (ELF64_ST_TYPE(syms[j].st_info) != STT_FUNC) continue;
      if (syms[j].st_name == 0 || syms[j].st_value == 0) continue;
      if (syms[j].st_name >= strsz) continue;
      char dem[256];
      maybe_demangle(strtab + syms[j].st_name, dem, sizeof(dem));
      add_symbol(t, bias + syms[j].st_value, dem, module);
    }
  }
}

CT_NOINSTR static void load_elf_symbols(ct_symbol_table *t, uint64_t base,
                                        const char *path, uint32_t module) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return;
  struct stat st;
  if (fstat(fd, &st) != 0 || st.st_size < (off_t)sizeof(Elf64_Ehdr)) { close(fd); return; }
  size_t size = (size_t)st.st_size;
  void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (map == MAP_FAILED) return;
  walk_elf(t, (const unsigned char *)map, size, base, module);
  munmap(map, size);
}

CT_NOINSTR static int cmp_sym(const void *a, const void *b) {
  const ct_sym_entry *x = (const ct_sym_entry *)a;
  const ct_sym_entry *y = (const ct_sym_entry *)b;
  if (x->addr < y->addr) return -1;
  if (x->addr > y->addr) return 1;
  return 0;
}

CT_NOINSTR CTIMING_HIDDEN int ct_symbols_load(ct_symbol_table *t) {
  if (t == NULL) return -1;
  memset(t, 0, sizeof(*t));
  FILE *f = fopen("/proc/self/maps", "r");
  if (f == NULL) return -1;

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    unsigned long long start = 0, end = 0, off = 0;
    char perms[8];
    int consumed = 0;
    perms[0] = '\0';
    if (sscanf(line, "%llx-%llx %7s %llx %*s %*s %n", &start, &end, perms, &off, &consumed) < 4) continue;
    const char *path = line + consumed;
    if (*path != '/') continue;

    size_t plen = strlen(path);
    while (plen && (path[plen - 1] == '\n' || path[plen - 1] == '\r' || path[plen - 1] == ' ')) plen--;
    if (plen == 0 || plen >= CT_PATH_MAX) continue;

    char file[CT_PATH_MAX];
    memcpy(file, path, plen);
    file[plen] = '\0';

    int mi = -1;
    for (size_t i = 0; i < t->n_modules; i++) {
      if (strcmp(t->modules[i].path, file) == 0) { mi = (int)i; break; }
    }
    if (mi < 0) {
      uint64_t base = (uint64_t)start - (uint64_t)off;
      mi = add_module(t, base, file);
      if (mi < 0) break;
      load_elf_symbols(t, base, file, (uint32_t)mi);
    }
    if ((uint64_t)end > t->module_hi[mi]) t->module_hi[mi] = (uint64_t)end;
  }

  fclose(f);
  if (t->n_symbols > 1) qsort(t->syms, t->n_symbols, sizeof(ct_sym_entry), cmp_sym);
  return 0;
}

CT_NOINSTR CTIMING_HIDDEN void ct_symbols_free(ct_symbol_table *t) {
  if (t == NULL) return;
  free(t->modules);
  free(t->module_hi);
  free(t->syms);
  memset(t, 0, sizeof(*t));
}

CT_NOINSTR CTIMING_HIDDEN const char *ct_symbols_lookup(const ct_symbol_table *t, uintptr_t addr,
                                                        uintptr_t *offset, uint32_t *module) {
  if (t == NULL || t->n_symbols == 0) return NULL;
  size_t lo = 0, hi = t->n_symbols;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (t->syms[mid].addr <= (uint64_t)addr) lo = mid + 1;
    else hi = mid;
  }
  if (lo == 0) return NULL;
  size_t i = lo - 1;
  uint32_t m = t->syms[i].module;
  if (m != 0xFFFFFFFFu && m < t->n_modules && t->module_hi &&
      (uint64_t)addr >= t->module_hi[m]) return NULL;
  if (offset) *offset = addr - (uintptr_t)t->syms[i].addr;
  if (module) *module = m;
  return t->syms[i].name;
}
