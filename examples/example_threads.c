#include <pthread.h>

static void worker_inner(int x) {
  volatile int s = 0;
  for (int i = 0; i < x; i++) s += i;
  (void)s;
}

static void *worker(void *arg) {
  (void)arg;
  for (int i = 0; i < 3; i++) worker_inner(100000);
  return NULL;
}

int main(void) {
  pthread_t t[4];
  for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, worker, NULL);
  for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
  return 0;
}
