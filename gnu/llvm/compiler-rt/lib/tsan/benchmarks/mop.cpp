// Synthetic benchmark for __tsan_read/write{1,2,4,8}.
// As compared to mini_bench_local/shared.cc this benchmark passes through
// deduplication logic (ContainsSameAccess).
// First argument is access size (1, 2, 4, 8). Second optional arg switches
// from writes to reads.

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>

template<typename T, bool write>
void* thread(void *arg) {
  const int kSize = 2 << 10;
  static volatile long data[kSize];
  static volatile long turn;
  const int kRepeat = 1 << 17;
  const int id = !!arg;
  for (int i = 0; i < kRepeat; i++) {
    for (;;) {
      int t = __atomic_load_n(&turn, __ATOMIC_ACQUIRE);
      if (t == id)
        break;
      syscall(SYS_futex, &turn, FUTEX_WAIT, t, 0, 0, 0);
    }
    for (int j = 0; j < kSize; j++) {
      if (write) {
        ((volatile T*)&data[j])[0] = 1;
        ((volatile T*)&data[j])[sizeof(T) == 8 ? 0 : 1] = 1;
      } else {
        T v0 = ((volatile T*)&data[j])[0];
        T v1 = ((volatile T*)&data[j])[sizeof(T) == 8 ? 0 : 1];
        (void)v0;
        (void)v1;
      }
    }
    __atomic_store_n(&turn, 1 - id, __ATOMIC_RELEASE);
    syscall(SYS_futex, &turn, FUTEX_WAKE, 0, 0, 0, 0);
  }
  return 0;
}

template<typename T, bool write>
void test() {
  pthread_t th;
  pthread_create(&th, 0, thread<T, write>, (void*)1);
  thread<T, write>(0);
  pthread_join(th, 0);  
}

template<bool write>
void testw(int size) {
  switch (size) {
  case 1: return test<char, write>();
  case 2: return test<short, write>();
  case 4: return test<int, write>();
  case 8: return test<long long, write>();
  }
}

int main(int argc, char** argv) {
  int size = 8;
  bool write = true;
  if (argc > 1) {
    size = atoi(argv[1]);
    if (size != 1 && size != 2 && size != 4 && size != 8)
      size = 8;
  }
  if (argc > 2)
    write = false;
  printf("%s%d\n", write ? "write" : "read", size);
  if (write)
    testw<true>(size);
  else
    testw<false>(size);
  return 0;
}
