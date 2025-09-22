//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstddef>
#include <utility>

#include "benchmark/benchmark.h"

template <std::size_t Indx, std::size_t Depth>
struct C : public virtual C<Indx, Depth - 1>, public virtual C<Indx + 1, Depth - 1> {
  virtual ~C() {}
};

template <std::size_t Indx>
struct C<Indx, 0> {
  virtual ~C() {}
};

template <std::size_t Indx, std::size_t Depth>
struct B : public virtual C<Indx, Depth - 1>, public virtual C<Indx + 1, Depth - 1> {};

template <class Indx, std::size_t Depth>
struct makeB;

template <std::size_t... Indx, std::size_t Depth>
struct makeB<std::index_sequence<Indx...>, Depth> : public B<Indx, Depth>... {};

template <std::size_t Width, std::size_t Depth>
struct A : public makeB<std::make_index_sequence<Width>, Depth> {};

constexpr std::size_t Width = 10;
constexpr std::size_t Depth = 5;

template <typename Destination>
void CastTo(benchmark::State& state) {
  A<Width, Depth> a;
  auto base = static_cast<C<Width / 2, 0>*>(&a);

  Destination* b = nullptr;
  for (auto _ : state) {
    b = dynamic_cast<Destination*>(base);
    benchmark::DoNotOptimize(b);
  }

  assert(b != 0);
}

BENCHMARK(CastTo<B<Width / 2, Depth>>);
BENCHMARK(CastTo<A<Width, Depth>>);

BENCHMARK_MAIN();

/**
 * Benchmark results: (release builds)
 *
 * libcxxabi:
 * ----------------------------------------------------------------------
 * Benchmark                            Time             CPU   Iterations
 * ----------------------------------------------------------------------
 * CastTo<B<Width / 2, Depth>>       1997 ns         1997 ns       349247
 * CastTo<A<Width, Depth>>            256 ns          256 ns      2733871
 *
 * libsupc++:
 * ----------------------------------------------------------------------
 * Benchmark                            Time             CPU   Iterations
 * ----------------------------------------------------------------------
 * CastTo<B<Width / 2, Depth>>       5240 ns         5240 ns       133091
 * CastTo<A<Width, Depth>>            866 ns          866 ns       808600
 *
 *
 */
