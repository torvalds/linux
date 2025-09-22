#include <thread>

const size_t kAllocSize = 16;
const size_t kInitialNumAllocs = 1 << 10;
const size_t kPeriodicNumAllocs = 1 << 10;
const size_t kNumIterations = 1 << 7;
const size_t kNumThreads = 16;

void Thread() {
  char *InitialAllocations[kInitialNumAllocs];
  char *PeriodicaAllocations[kPeriodicNumAllocs];
  for (auto &p : InitialAllocations) p = new char[kAllocSize];
  for (size_t i = 0; i < kNumIterations; i++) {
    for (size_t j = 0; j < kPeriodicNumAllocs; j++) {
      for (auto &p : PeriodicaAllocations) {
        p = new char[kAllocSize];
        *p = 0;
      }
      for (auto p : PeriodicaAllocations) delete [] p;
    }
  }
  for (auto p : InitialAllocations) delete [] p;
}

int main() {
  std::thread *Threads[kNumThreads];
  for (auto &T : Threads) T = new std::thread(&Thread);
  for (auto T : Threads) {
    T->join();
    delete T;
  }
}
