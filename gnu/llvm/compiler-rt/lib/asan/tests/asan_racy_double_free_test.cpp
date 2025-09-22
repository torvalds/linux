#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

const int N = 1000;
void *x[N];

void *Thread1(void *unused) {
  for (int i = 0; i < N; i++) {
    fprintf(stderr, "%s %d\n", __func__, i);
    free(x[i]);
  }
  return NULL;
}

void *Thread2(void *unused) {
  for (int i = 0; i < N; i++) {
    fprintf(stderr, "%s %d\n", __func__, i);
    free(x[i]);
  }
  return NULL;
}

int main() {
  for (int i = 0; i < N; i++)
    x[i] = malloc(128);
  pthread_t t[2];
  pthread_create(&t[0], 0, Thread1, 0);
  pthread_create(&t[1], 0, Thread2, 0);
  pthread_join(t[0], 0);
  pthread_join(t[1], 0);
}
