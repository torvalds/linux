// Mini-benchmark for creating a lot of threads.
//
// Some facts:
// a) clang -O1 takes <15ms to start N=500 threads,
//    consuming ~4MB more RAM than N=1.
// b) clang -O1 -ftsan takes ~26s to start N=500 threads,
//    eats 5GB more RAM than N=1 (which is somewhat expected but still a lot)
//    but then it consumes ~4GB of extra memory when the threads shut down!
//        (definitely not in the barrier_wait interceptor)
//    Also, it takes 26s to run with N=500 vs just 1.1s to run with N=1.
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

pthread_barrier_t all_threads_ready;

void* Thread(void *unused) {
  pthread_barrier_wait(&all_threads_ready);
  return 0;
}

int main(int argc, char **argv) {
  int n_threads;
  if (argc == 1) {
    n_threads = 100;
  } else if (argc == 2) {
    n_threads = atoi(argv[1]);
  } else {
    printf("Usage: %s n_threads\n", argv[0]);
    return 1;
  }
  printf("%s: n_threads=%d\n", __FILE__, n_threads);

  pthread_barrier_init(&all_threads_ready, NULL, n_threads + 1);

  pthread_t *t = new pthread_t[n_threads];
  for (int i = 0; i < n_threads; i++) {
    int status = pthread_create(&t[i], 0, Thread, (void*)i);
    assert(status == 0);
  }
  // sleep(5);  // FIXME: simplify measuring the memory usage.
  pthread_barrier_wait(&all_threads_ready);
  for (int i = 0; i < n_threads; i++) {
    pthread_join(t[i], 0);
  }
  // sleep(5);  // FIXME: simplify measuring the memory usage.
  delete [] t;

  return 0;
}
