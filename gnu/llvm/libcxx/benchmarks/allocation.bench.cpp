//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "benchmark/benchmark.h"

#include <cassert>
#include <new>
#include <vector>

struct PointerList {
  PointerList* Next = nullptr;
};

struct MallocWrapper {
  __attribute__((always_inline)) static void* Allocate(size_t N) { return std::malloc(N); }
  __attribute__((always_inline)) static void Deallocate(void* P, size_t) { std::free(P); }
};

struct NewWrapper {
  __attribute__((always_inline)) static void* Allocate(size_t N) { return ::operator new(N); }
  __attribute__((always_inline)) static void Deallocate(void* P, size_t) { ::operator delete(P); }
};

struct BuiltinNewWrapper {
  __attribute__((always_inline)) static void* Allocate(size_t N) { return __builtin_operator_new(N); }
  __attribute__((always_inline)) static void Deallocate(void* P, size_t) { __builtin_operator_delete(P); }
};

struct BuiltinSizedNewWrapper {
  __attribute__((always_inline)) static void* Allocate(size_t N) { return __builtin_operator_new(N); }
  __attribute__((always_inline)) static void Deallocate(void* P, size_t N) { __builtin_operator_delete(P, N); }
};

template <class AllocWrapper>
static void BM_AllocateAndDeallocate(benchmark::State& st) {
  const size_t alloc_size = st.range(0);
  while (st.KeepRunning()) {
    void* p = AllocWrapper::Allocate(alloc_size);
    benchmark::DoNotOptimize(p);
    AllocWrapper::Deallocate(p, alloc_size);
  }
}

template <class AllocWrapper>
static void BM_AllocateOnly(benchmark::State& st) {
  const size_t alloc_size = st.range(0);
  PointerList* Start      = nullptr;

  while (st.KeepRunning()) {
    PointerList* p = (PointerList*)AllocWrapper::Allocate(alloc_size);
    benchmark::DoNotOptimize(p);
    p->Next = Start;
    Start   = p;
  }

  PointerList* Next = Start;
  while (Next) {
    PointerList* Tmp = Next;
    Next             = Tmp->Next;
    AllocWrapper::Deallocate(Tmp, alloc_size);
  }
}

template <class AllocWrapper>
static void BM_DeallocateOnly(benchmark::State& st) {
  const size_t alloc_size = st.range(0);
  const auto NumAllocs    = st.max_iterations;

  std::vector<void*> Pointers(NumAllocs);
  for (auto& p : Pointers) {
    p = AllocWrapper::Allocate(alloc_size);
  }

  void** Data                       = Pointers.data();
  [[maybe_unused]] void** const End = Pointers.data() + Pointers.size();
  while (st.KeepRunning()) {
    AllocWrapper::Deallocate(*Data, alloc_size);
    Data += 1;
  }
  assert(Data == End);
}

static int RegisterAllocBenchmarks() {
  using FnType = void (*)(benchmark::State&);
  struct {
    const char* name;
    FnType func;
  } TestCases[] = {
      {"BM_Malloc", &BM_AllocateAndDeallocate<MallocWrapper>},
      {"BM_New", &BM_AllocateAndDeallocate<NewWrapper>},
      {"BM_BuiltinNewDelete", BM_AllocateAndDeallocate<BuiltinNewWrapper>},
      {"BM_BuiltinSizedNewDelete", BM_AllocateAndDeallocate<BuiltinSizedNewWrapper>},
      {"BM_BuiltinNewAllocateOnly", BM_AllocateOnly<BuiltinSizedNewWrapper>},
      {"BM_BuiltinNewSizedDeallocateOnly", BM_DeallocateOnly<BuiltinSizedNewWrapper>},

  };
  for (auto TC : TestCases) {
    benchmark::RegisterBenchmark(TC.name, TC.func)->Range(16, 4096 * 2);
  }
  return 0;
}
int Sink = RegisterAllocBenchmarks();

BENCHMARK_MAIN();
