#include <stdio.h>

static void leaf(int x) { volatile int s = 0; for (int i = 0; i < x; i++) s += i; (void)s; }
static void mid(void) { for (int i = 0; i < 3; i++) leaf(100000); }
int main(void) { for (int i = 0; i < 2; i++) mid(); printf("done\n"); return 0; }
