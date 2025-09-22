// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BENCHMARK_VARIANT_BENCHMARKS_H
#define BENCHMARK_VARIANT_BENCHMARKS_H

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <variant>

#include "benchmark/benchmark.h"

#include "GenerateInput.h"

namespace VariantBenchmarks {

template <std::size_t I>
struct S {
  static constexpr size_t v = I;
};

template <std::size_t N, std::size_t... Is>
static auto genVariants(std::index_sequence<Is...>) {
  using V                 = std::variant<S<Is>...>;
  using F                 = V (*)();
  static constexpr F fs[] = {[] { return V(std::in_place_index<Is>); }...};

  std::array<V, N> result = {};
  for (auto& v : result) {
    v = fs[getRandomInteger(0ul, sizeof...(Is) - 1)]();
  }

  return result;
}

template <std::size_t N, std::size_t Alts>
static void BM_Visit(benchmark::State& state) {
  auto args = genVariants<N>(std::make_index_sequence<Alts>{});
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        std::apply([](auto... vs) { return std::visit([](auto... is) { return (is.v + ... + 0); }, vs...); }, args));
  }
}

} // end namespace VariantBenchmarks

#endif // BENCHMARK_VARIANT_BENCHMARKS_H
