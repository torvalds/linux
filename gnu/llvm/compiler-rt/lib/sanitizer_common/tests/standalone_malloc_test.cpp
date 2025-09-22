#include <stdio.h>
#include <vector>
#include <pthread.h>
#include <malloc.h>
#include <algorithm>

using namespace std;

const size_t kNumThreds = 16;
const size_t kNumIters = 1 << 23;

inline void break_optimization(void *arg) {
  __asm__ __volatile__("" : : "r" (arg) : "memory");
}

__attribute__((noinline))
static void *MallocThread(void *t) {
  size_t total_malloced = 0, total_freed = 0;
  size_t max_in_use = 0;
  size_t tid = reinterpret_cast<size_t>(t);
  vector<pair<char *, size_t> > allocated;
  allocated.reserve(kNumIters);
  for (size_t i = 1; i < kNumIters; i++) {
    if ((i % (kNumIters / 4)) == 0 && tid == 0)
      fprintf(stderr, "   T[%ld] iter %ld\n", tid, i);
    bool allocate = (i % 5) <= 2;  // 60% malloc, 40% free
    if (i > kNumIters / 4)
      allocate = i % 2;  // then switch to 50% malloc, 50% free
    if (allocate) {
      size_t size = 1 + (i % 200);
      if ((i % 10001) == 0)
        size *= 4096;
      total_malloced += size;
      char *x = new char[size];
      x[0] = x[size - 1] = x[size / 2] = 0;
      allocated.push_back(make_pair(x, size));
      max_in_use = max(max_in_use, total_malloced - total_freed);
    } else {
      if (allocated.empty()) continue;
      size_t slot = i % allocated.size();
      char *p = allocated[slot].first;
      p[0] = 0;  // emulate last user touch of the block
      size_t size = allocated[slot].second;
      total_freed += size;
      swap(allocated[slot], allocated.back());
      allocated.pop_back();
      delete [] p;
    }
  }
  if (tid == 0)
    fprintf(stderr, "   T[%ld] total_malloced: %ldM in use %ldM max %ldM\n",
           tid, total_malloced >> 20, (total_malloced - total_freed) >> 20,
           max_in_use >> 20);
  for (size_t i = 0; i < allocated.size(); i++)
    delete [] allocated[i].first;
  return 0;
}

template <int depth>
struct DeepStack {
  __attribute__((noinline))
  static void *run(void *t) {
    break_optimization(0);
    DeepStack<depth - 1>::run(t);
    break_optimization(0);
    return 0;
  }
};

template<>
struct DeepStack<0> {
  static void *run(void *t) {
    MallocThread(t);
    return 0;
  }
};

// Build with -Dstandalone_malloc_test=main to make it a separate program.
int standalone_malloc_test() {
  pthread_t t[kNumThreds];
  for (size_t i = 0; i < kNumThreds; i++)
    pthread_create(&t[i], 0, DeepStack<200>::run, reinterpret_cast<void *>(i));
  for (size_t i = 0; i < kNumThreds; i++)
    pthread_join(t[i], 0);
  malloc_stats();
  return 0;
}
