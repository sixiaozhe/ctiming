#include <stdio.h>
#include <unistd.h>

static void leaf(int x) { volatile int s = 0; for (int i = 0; i < x; i++) s += i; (void)s; }
static int rec(int n) { return n <= 0 ? 0 : 1 + rec(n - 1); }
static int hot(int n) { int s = 0; for (int i = 0; i < n; i++) leaf(1000); s += rec(3); return s; }
static void unrelated(void) { volatile int s = 0; for (int i = 0; i < 1000000; i++) s += i; (void)s; }

int main(int argc, char **argv) {
  int once = (argc > 1 && argv[1][0] == 'o');
  if (once) { hot(3); unrelated(); printf("done\n"); return 0; }
  for (int i = 0; i < 60; i++) usleep(5000);
  for (int i = 0; i < 200; i++) { if (i % 5 == 0) unrelated(); else hot(2); usleep(2000); }
  printf("done\n");
  return 0;
}
