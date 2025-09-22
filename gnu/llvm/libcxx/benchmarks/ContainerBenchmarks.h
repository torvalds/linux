// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BENCHMARK_CONTAINER_BENCHMARKS_H
#define BENCHMARK_CONTAINER_BENCHMARKS_H

#include <cassert>

#include "Utilities.h"
#include "benchmark/benchmark.h"

namespace ContainerBenchmarks {

template <class Container>
void BM_ConstructSize(benchmark::State& st, Container) {
  auto size = st.range(0);
  for (auto _ : st) {
    Container c(size);
    DoNotOptimizeData(c);
  }
}

template <class Container>
void BM_CopyConstruct(benchmark::State& st, Container) {
  auto size = st.range(0);
  Container c(size);
  for (auto _ : st) {
    auto v = c;
    DoNotOptimizeData(v);
  }
}

template <class Container>
void BM_Assignment(benchmark::State& st, Container) {
  auto size = st.range(0);
  Container c1;
  Container c2(size);
  for (auto _ : st) {
    c1 = c2;
    DoNotOptimizeData(c1);
    DoNotOptimizeData(c2);
  }
}

template <class Container>
void BM_ConstructSizeValue(benchmark::State& st, Container, typename Container::value_type const& val) {
  const auto size = st.range(0);
  for (auto _ : st) {
    Container c(size, val);
    DoNotOptimizeData(c);
  }
}

template <class Container, class GenInputs>
void BM_ConstructIterIter(benchmark::State& st, Container, GenInputs gen) {
  auto in          = gen(st.range(0));
  const auto begin = in.begin();
  const auto end   = in.end();
  benchmark::DoNotOptimize(&in);
  while (st.KeepRunning()) {
    Container c(begin, end);
    DoNotOptimizeData(c);
  }
}

template <class Container, class GenInputs>
void BM_ConstructFromRange(benchmark::State& st, Container, GenInputs gen) {
  auto in = gen(st.range(0));
  benchmark::DoNotOptimize(&in);
  while (st.KeepRunning()) {
    Container c(std::from_range, in);
    DoNotOptimizeData(c);
  }
}

template <class Container>
void BM_Pushback_no_grow(benchmark::State& state, Container c) {
  int count = state.range(0);
  c.reserve(count);
  while (state.KeepRunningBatch(count)) {
    c.clear();
    for (int i = 0; i != count; ++i) {
      c.push_back(i);
    }
    benchmark::DoNotOptimize(c.data());
  }
}

template <class Container, class GenInputs>
void BM_InsertValue(benchmark::State& st, Container c, GenInputs gen) {
  auto in        = gen(st.range(0));
  const auto end = in.end();
  while (st.KeepRunning()) {
    c.clear();
    for (auto it = in.begin(); it != end; ++it) {
      benchmark::DoNotOptimize(&(*c.insert(*it).first));
    }
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
void BM_InsertValueRehash(benchmark::State& st, Container c, GenInputs gen) {
  auto in        = gen(st.range(0));
  const auto end = in.end();
  while (st.KeepRunning()) {
    c.clear();
    c.rehash(16);
    for (auto it = in.begin(); it != end; ++it) {
      benchmark::DoNotOptimize(&(*c.insert(*it).first));
    }
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
void BM_InsertDuplicate(benchmark::State& st, Container c, GenInputs gen) {
  auto in        = gen(st.range(0));
  const auto end = in.end();
  c.insert(in.begin(), in.end());
  benchmark::DoNotOptimize(&c);
  benchmark::DoNotOptimize(&in);
  while (st.KeepRunning()) {
    for (auto it = in.begin(); it != end; ++it) {
      benchmark::DoNotOptimize(&(*c.insert(*it).first));
    }
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
void BM_EmplaceDuplicate(benchmark::State& st, Container c, GenInputs gen) {
  auto in        = gen(st.range(0));
  const auto end = in.end();
  c.insert(in.begin(), in.end());
  benchmark::DoNotOptimize(&c);
  benchmark::DoNotOptimize(&in);
  while (st.KeepRunning()) {
    for (auto it = in.begin(); it != end; ++it) {
      benchmark::DoNotOptimize(&(*c.emplace(*it).first));
    }
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
static void BM_Find(benchmark::State& st, Container c, GenInputs gen) {
  auto in = gen(st.range(0));
  c.insert(in.begin(), in.end());
  benchmark::DoNotOptimize(&(*c.begin()));
  const auto end = in.data() + in.size();
  while (st.KeepRunning()) {
    for (auto it = in.data(); it != end; ++it) {
      benchmark::DoNotOptimize(&(*c.find(*it)));
    }
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
static void BM_FindRehash(benchmark::State& st, Container c, GenInputs gen) {
  c.rehash(8);
  auto in = gen(st.range(0));
  c.insert(in.begin(), in.end());
  benchmark::DoNotOptimize(&(*c.begin()));
  const auto end = in.data() + in.size();
  while (st.KeepRunning()) {
    for (auto it = in.data(); it != end; ++it) {
      benchmark::DoNotOptimize(&(*c.find(*it)));
    }
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
static void BM_Rehash(benchmark::State& st, Container c, GenInputs gen) {
  auto in = gen(st.range(0));
  c.max_load_factor(3.0);
  c.insert(in.begin(), in.end());
  benchmark::DoNotOptimize(c);
  const auto bucket_count = c.bucket_count();
  while (st.KeepRunning()) {
    c.rehash(bucket_count + 1);
    c.rehash(bucket_count);
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
static void BM_Compare_same_container(benchmark::State& st, Container, GenInputs gen) {
  auto in = gen(st.range(0));
  Container c1(in.begin(), in.end());
  Container c2 = c1;

  benchmark::DoNotOptimize(&(*c1.begin()));
  benchmark::DoNotOptimize(&(*c2.begin()));
  while (st.KeepRunning()) {
    bool res = c1 == c2;
    benchmark::DoNotOptimize(&res);
    benchmark::ClobberMemory();
  }
}

template <class Container, class GenInputs>
static void BM_Compare_different_containers(benchmark::State& st, Container, GenInputs gen) {
  auto in1 = gen(st.range(0));
  auto in2 = gen(st.range(0));
  Container c1(in1.begin(), in1.end());
  Container c2(in2.begin(), in2.end());

  benchmark::DoNotOptimize(&(*c1.begin()));
  benchmark::DoNotOptimize(&(*c2.begin()));
  while (st.KeepRunning()) {
    bool res = c1 == c2;
    benchmark::DoNotOptimize(&res);
    benchmark::ClobberMemory();
  }
}

} // end namespace ContainerBenchmarks

#endif // BENCHMARK_CONTAINER_BENCHMARKS_H
