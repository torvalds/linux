//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LIBCXX_ALGORITHMS_COMMON_H
#define LIBCXX_ALGORITHMS_COMMON_H

#include <algorithm>
#include <numeric>
#include <tuple>
#include <vector>

#include "../CartesianBenchmarks.h"
#include "../GenerateInput.h"

enum class ValueType { Uint32, Uint64, Pair, Tuple, String, Float };
struct AllValueTypes : EnumValuesAsTuple<AllValueTypes, ValueType, 6> {
  static constexpr const char* Names[] = {
      "uint32", "uint64", "pair<uint32, uint32>", "tuple<uint32, uint64, uint32>", "string", "float"};
};

using Types =
    std::tuple<uint32_t,
               uint64_t,
               std::pair<uint32_t, uint32_t>,
               std::tuple<uint32_t, uint64_t, uint32_t>,
               std::string,
               float>;

template <class V>
using Value = std::tuple_element_t<(int)V::value, Types>;

enum class Order {
  Random,
  Ascending,
  Descending,
  SingleElement,
  PipeOrgan,
  Heap,
  QuickSortAdversary,
};
struct AllOrders : EnumValuesAsTuple<AllOrders, Order, 7> {
  static constexpr const char* Names[] = {
      "Random", "Ascending", "Descending", "SingleElement", "PipeOrgan", "Heap", "QuickSortAdversary"};
};

// fillAdversarialQuickSortInput fills the input vector with N int-like values.
// These values are arranged in such a way that they would invoke O(N^2)
// behavior on any quick sort implementation that satisifies certain conditions.
// Details are available in the following paper:
// "A Killer Adversary for Quicksort", M. D. McIlroy, Software-Practice &
// Experience Volume 29 Issue 4 April 10, 1999 pp 341-344.
// https://dl.acm.org/doi/10.5555/311868.311871.
template <class T>
void fillAdversarialQuickSortInput(T& V, size_t N) {
  assert(N > 0);
  // If an element is equal to gas, it indicates that the value of the element
  // is still to be decided and may change over the course of time.
  const unsigned int gas = N - 1;
  V.resize(N);
  for (unsigned int i = 0; i < N; ++i) {
    V[i] = gas;
  }
  // Candidate for the pivot position.
  int candidate = 0;
  int nsolid    = 0;
  // Populate all positions in the generated input to gas.
  std::vector<int> ascVals(V.size());
  // Fill up with ascending values from 0 to V.size()-1.  These will act as
  // indices into V.
  std::iota(ascVals.begin(), ascVals.end(), 0);
  std::sort(ascVals.begin(), ascVals.end(), [&](int x, int y) {
    if (V[x] == gas && V[y] == gas) {
      // We are comparing two inputs whose value is still to be decided.
      if (x == candidate) {
        V[x] = nsolid++;
      } else {
        V[y] = nsolid++;
      }
    }
    if (V[x] == gas) {
      candidate = x;
    } else if (V[y] == gas) {
      candidate = y;
    }
    return V[x] < V[y];
  });
}

template <typename T>
void fillValues(std::vector<T>& V, size_t N, Order O) {
  if (O == Order::SingleElement) {
    V.resize(N, 0);
  } else if (O == Order::QuickSortAdversary) {
    fillAdversarialQuickSortInput(V, N);
  } else {
    while (V.size() < N)
      V.push_back(V.size());
  }
}

template <typename T>
void fillValues(std::vector<std::pair<T, T> >& V, size_t N, Order O) {
  if (O == Order::SingleElement) {
    V.resize(N, std::make_pair(0, 0));
  } else {
    while (V.size() < N)
      // Half of array will have the same first element.
      if (V.size() % 2) {
        V.push_back(std::make_pair(V.size(), V.size()));
      } else {
        V.push_back(std::make_pair(0, V.size()));
      }
  }
}

template <typename T1, typename T2, typename T3>
void fillValues(std::vector<std::tuple<T1, T2, T3> >& V, size_t N, Order O) {
  if (O == Order::SingleElement) {
    V.resize(N, std::make_tuple(0, 0, 0));
  } else {
    while (V.size() < N)
      // One third of array will have the same first element.
      // One third of array will have the same first element and the same second element.
      switch (V.size() % 3) {
      case 0:
        V.push_back(std::make_tuple(V.size(), V.size(), V.size()));
        break;
      case 1:
        V.push_back(std::make_tuple(0, V.size(), V.size()));
        break;
      case 2:
        V.push_back(std::make_tuple(0, 0, V.size()));
        break;
      }
  }
}

inline void fillValues(std::vector<std::string>& V, size_t N, Order O) {
  if (O == Order::SingleElement) {
    V.resize(N, getRandomString(64));
  } else {
    while (V.size() < N)
      V.push_back(getRandomString(64));
  }
}

template <class T>
void sortValues(T& V, Order O) {
  switch (O) {
  case Order::Random: {
    std::random_device R;
    std::mt19937 M(R());
    std::shuffle(V.begin(), V.end(), M);
    break;
  }
  case Order::Ascending:
    std::sort(V.begin(), V.end());
    break;
  case Order::Descending:
    std::sort(V.begin(), V.end(), std::greater<>());
    break;
  case Order::SingleElement:
    // Nothing to do
    break;
  case Order::PipeOrgan:
    std::sort(V.begin(), V.end());
    std::reverse(V.begin() + V.size() / 2, V.end());
    break;
  case Order::Heap:
    std::make_heap(V.begin(), V.end());
    break;
  case Order::QuickSortAdversary:
    // Nothing to do
    break;
  }
}

constexpr size_t TestSetElements =
#if !TEST_HAS_FEATURE(memory_sanitizer)
    1 << 18;
#else
    1 << 14;
#endif

template <class ValueType>
std::vector<std::vector<Value<ValueType> > > makeOrderedValues(size_t N, Order O) {
  std::vector<std::vector<Value<ValueType> > > Ret;
  const size_t NumCopies = std::max(size_t{1}, TestSetElements / N);
  Ret.resize(NumCopies);
  for (auto& V : Ret) {
    fillValues(V, N, O);
    sortValues(V, O);
  }
  return Ret;
}

template <class T, class U>
TEST_ALWAYS_INLINE void resetCopies(benchmark::State& state, T& Copies, U& Orig) {
  state.PauseTiming();
  for (auto& Copy : Copies)
    Copy = Orig;
  state.ResumeTiming();
}

enum class BatchSize {
  CountElements,
  CountBatch,
};

template <class ValueType, class F>
void runOpOnCopies(benchmark::State& state, size_t Quantity, Order O, BatchSize Count, F Body) {
  auto Copies = makeOrderedValues<ValueType>(Quantity, O);
  auto Orig   = Copies;

  const size_t Batch = Count == BatchSize::CountElements ? Copies.size() * Quantity : Copies.size();
  while (state.KeepRunningBatch(Batch)) {
    for (auto& Copy : Copies) {
      Body(Copy);
      benchmark::DoNotOptimize(Copy);
    }
    state.PauseTiming();
    Copies = Orig;
    state.ResumeTiming();
  }
}

const std::vector<size_t> Quantities = {
    1 << 0,
    1 << 2,
    1 << 4,
    1 << 6,
    1 << 8,
    1 << 10,
    1 << 14,
// Running each benchmark in parallel consumes too much memory with MSAN
// and can lead to the test process being killed.
#if !TEST_HAS_FEATURE(memory_sanitizer)
    1 << 18
#endif
};

#endif // LIBCXX_ALGORITHMS_COMMON_H
