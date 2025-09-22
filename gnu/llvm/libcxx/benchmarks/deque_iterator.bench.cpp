//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <deque>

#include "benchmark/benchmark.h"

namespace {
void run_sizes(auto benchmark) {
  benchmark->Arg(0)
      ->Arg(1)
      ->Arg(2)
      ->Arg(64)
      ->Arg(512)
      ->Arg(1024)
      ->Arg(4000)
      ->Arg(4096)
      ->Arg(5500)
      ->Arg(64000)
      ->Arg(65536)
      ->Arg(70000);
}

template <class FromContainer, class ToContainer, class Func>
void benchmark_containers(benchmark::State& state, FromContainer& d, ToContainer& v, Func&& func) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(d);
    func(d.begin(), d.end(), v.begin());
  }
}

template <class Func>
void benchmark_deque_vector(benchmark::State& state, Func&& func) {
  auto size = state.range(0);
  std::deque<int> d;
  d.resize(size);
  std::ranges::fill(d, 10);
  std::vector<int> v;
  v.resize(size);
  benchmark_containers(state, d, v, func);
}

template <class Func>
void benchmark_deque_deque(benchmark::State& state, Func&& func) {
  auto size = state.range(0);
  std::deque<int> d;
  d.resize(size);
  std::ranges::fill(d, 10);
  std::deque<int> v;
  v.resize(size);
  benchmark_containers(state, d, v, func);
}

template <class Func>
void benchmark_vector_deque(benchmark::State& state, Func&& func) {
  auto size = state.range(0);
  std::vector<int> d;
  d.resize(size);
  std::ranges::fill(d, 10);
  std::deque<int> v;
  v.resize(size);
  benchmark_containers(state, d, v, func);
}

template <class FromContainer, class ToContainer, class Func>
void benchmark_containers_backward(benchmark::State& state, FromContainer& d, ToContainer& v, Func&& func) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(v);
    benchmark::DoNotOptimize(d);
    func(d.begin(), d.end(), v.end());
  }
}

template <class Func>
void benchmark_deque_vector_backward(benchmark::State& state, Func&& func) {
  auto size = state.range(0);
  std::deque<int> d;
  d.resize(size);
  std::ranges::fill(d, 10);
  std::vector<int> v;
  v.resize(size);
  benchmark_containers_backward(state, d, v, func);
}

template <class Func>
void benchmark_deque_deque_backward(benchmark::State& state, Func&& func) {
  auto size = state.range(0);
  std::deque<int> d;
  d.resize(size);
  std::ranges::fill(d, 10);
  std::deque<int> v;
  v.resize(size);
  benchmark_containers_backward(state, d, v, func);
}

template <class Func>
void benchmark_vector_deque_backward(benchmark::State& state, Func&& func) {
  auto size = state.range(0);
  std::vector<int> d;
  d.resize(size);
  std::ranges::fill(d, 10);
  std::deque<int> v;
  v.resize(size);
  benchmark_containers_backward(state, d, v, func);
}

struct CopyFunctor {
  template <class... Args>
  auto operator()(Args... args) const {
    std::copy(std::forward<Args>(args)...);
  }
} copy;

struct MoveFunctor {
  template <class... Args>
  auto operator()(Args... args) const {
    std::move(std::forward<Args>(args)...);
  }
} move;

struct CopyBackwardFunctor {
  template <class... Args>
  auto operator()(Args... args) const {
    std::copy_backward(std::forward<Args>(args)...);
  }
} copy_backward;

struct MoveBackwardFunctor {
  template <class... Args>
  auto operator()(Args... args) const {
    std::move_backward(std::forward<Args>(args)...);
  }
} move_backward;

// copy
void BM_deque_vector_copy(benchmark::State& state) { benchmark_deque_vector(state, copy); }
BENCHMARK(BM_deque_vector_copy)->Apply(run_sizes);

void BM_deque_vector_ranges_copy(benchmark::State& state) { benchmark_deque_vector(state, std::ranges::copy); }
BENCHMARK(BM_deque_vector_ranges_copy)->Apply(run_sizes);

void BM_deque_deque_copy(benchmark::State& state) { benchmark_deque_deque(state, copy); }
BENCHMARK(BM_deque_deque_copy)->Apply(run_sizes);

void BM_deque_deque_ranges_copy(benchmark::State& state) { benchmark_deque_deque(state, std::ranges::copy); }
BENCHMARK(BM_deque_deque_ranges_copy)->Apply(run_sizes);

void BM_vector_deque_copy(benchmark::State& state) { benchmark_vector_deque(state, copy); }
BENCHMARK(BM_vector_deque_copy)->Apply(run_sizes);

void BM_vector_deque_ranges_copy(benchmark::State& state) { benchmark_vector_deque(state, std::ranges::copy); }
BENCHMARK(BM_vector_deque_ranges_copy)->Apply(run_sizes);

// move
void BM_deque_vector_move(benchmark::State& state) { benchmark_deque_vector(state, move); }
BENCHMARK(BM_deque_vector_move)->Apply(run_sizes);

void BM_deque_vector_ranges_move(benchmark::State& state) { benchmark_deque_vector(state, std::ranges::move); }
BENCHMARK(BM_deque_vector_ranges_move)->Apply(run_sizes);

void BM_deque_deque_move(benchmark::State& state) { benchmark_deque_deque(state, move); }
BENCHMARK(BM_deque_deque_move)->Apply(run_sizes);

void BM_deque_deque_ranges_move(benchmark::State& state) { benchmark_deque_deque(state, std::ranges::move); }
BENCHMARK(BM_deque_deque_ranges_move)->Apply(run_sizes);

void BM_vector_deque_move(benchmark::State& state) { benchmark_vector_deque(state, move); }
BENCHMARK(BM_vector_deque_move)->Apply(run_sizes);

void BM_vector_deque_ranges_move(benchmark::State& state) { benchmark_vector_deque(state, std::ranges::move); }
BENCHMARK(BM_vector_deque_ranges_move)->Apply(run_sizes);

// copy_backward
void BM_deque_vector_copy_backward(benchmark::State& state) { benchmark_deque_vector_backward(state, copy_backward); }
BENCHMARK(BM_deque_vector_copy_backward)->Apply(run_sizes);

void BM_deque_vector_ranges_copy_backward(benchmark::State& state) {
  benchmark_deque_vector_backward(state, std::ranges::copy_backward);
}
BENCHMARK(BM_deque_vector_ranges_copy_backward)->Apply(run_sizes);

void BM_deque_deque_copy_backward(benchmark::State& state) { benchmark_deque_deque_backward(state, copy_backward); }
BENCHMARK(BM_deque_deque_copy_backward)->Apply(run_sizes);

void BM_deque_deque_ranges_copy_backward(benchmark::State& state) {
  benchmark_deque_deque_backward(state, std::ranges::copy_backward);
}
BENCHMARK(BM_deque_deque_ranges_copy_backward)->Apply(run_sizes);

void BM_vector_deque_copy_backward(benchmark::State& state) { benchmark_vector_deque_backward(state, copy_backward); }
BENCHMARK(BM_vector_deque_copy_backward)->Apply(run_sizes);

void BM_vector_deque_ranges_copy_backward(benchmark::State& state) {
  benchmark_vector_deque_backward(state, std::ranges::copy_backward);
}
BENCHMARK(BM_vector_deque_ranges_copy_backward)->Apply(run_sizes);

// move_backward
void BM_deque_vector_move_backward(benchmark::State& state) { benchmark_deque_vector_backward(state, move_backward); }
BENCHMARK(BM_deque_vector_move_backward)->Apply(run_sizes);

void BM_deque_vector_ranges_move_backward(benchmark::State& state) {
  benchmark_deque_vector_backward(state, std::ranges::move_backward);
}
BENCHMARK(BM_deque_vector_ranges_move_backward)->Apply(run_sizes);

void BM_deque_deque_move_backward(benchmark::State& state) { benchmark_deque_deque_backward(state, move_backward); }
BENCHMARK(BM_deque_deque_move_backward)->Apply(run_sizes);

void BM_deque_deque_ranges_move_backward(benchmark::State& state) {
  benchmark_deque_deque_backward(state, std::ranges::move_backward);
}
BENCHMARK(BM_deque_deque_ranges_move_backward)->Apply(run_sizes);

void BM_vector_deque_move_backward(benchmark::State& state) { benchmark_vector_deque_backward(state, move_backward); }
BENCHMARK(BM_vector_deque_move_backward)->Apply(run_sizes);

void BM_vector_deque_ranges_move_backward(benchmark::State& state) {
  benchmark_vector_deque_backward(state, std::ranges::move_backward);
}
BENCHMARK(BM_vector_deque_ranges_move_backward)->Apply(run_sizes);

} // namespace

BENCHMARK_MAIN();
