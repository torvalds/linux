//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstddef>

#include "benchmark/benchmark.h"

template <std::size_t Depth>
struct Chain : Chain<Depth - 1> {};

template <>
struct Chain<0> {
  virtual ~Chain() noexcept = default;
};

template <std::size_t Index, std::size_t Depth>
struct Dag : Dag<Index, Depth - 1>, Dag<Index + 1, Depth - 1> {};

template <std::size_t Index>
struct Dag<Index, 0> {
  virtual ~Dag() noexcept = default;
};

template <std::size_t Depth>
struct VChain : virtual VChain<Depth - 1> {};

template <>
struct VChain<0> {
  virtual ~VChain() noexcept = default;
};

template <std::size_t Index, std::size_t Depth>
struct VDag : virtual VDag<Index, Depth - 1>, virtual VDag<Index + 1, Depth - 1> {};

template <std::size_t Index>
struct VDag<Index, 0> {
  virtual ~VDag() noexcept = default;
};

template <typename Dyn, typename From, typename To = Dyn>
static void DynCast(benchmark::State& state) {
  Dyn obj;
  From* from_ptr = &obj;
  for (auto _ : state) {
    To* to_ptr = dynamic_cast<To*>(from_ptr);
    benchmark::DoNotOptimize(to_ptr);
  }
}

static void StaticCast(benchmark::State& state) {
  Chain<9> obj;
  Chain<0>* from_ptr = &obj;
  for (auto _ : state) {
    Chain<9>* to_ptr = static_cast<Chain<9>*>(from_ptr);
    benchmark::DoNotOptimize(to_ptr);
  }
}

// Downcast along a chain from base to the most derived type
BENCHMARK(DynCast<Chain<1>, Chain<0>>)->Name("Chain, 1 level");
BENCHMARK(DynCast<Chain<2>, Chain<0>>)->Name("Chain, 2 levels");
BENCHMARK(DynCast<Chain<3>, Chain<0>>)->Name("Chain, 3 levels");
BENCHMARK(DynCast<Chain<4>, Chain<0>>)->Name("Chain, 4 levels");
BENCHMARK(DynCast<Chain<5>, Chain<0>>)->Name("Chain, 5 levels");
BENCHMARK(DynCast<Chain<6>, Chain<0>>)->Name("Chain, 6 levels");
BENCHMARK(DynCast<Chain<7>, Chain<0>>)->Name("Chain, 7 levels");
BENCHMARK(DynCast<Chain<8>, Chain<0>>)->Name("Chain, 8 levels");
BENCHMARK(DynCast<Chain<9>, Chain<0>>)->Name("Chain, 9 levels");

// Downcast along a chain from base to the middle of the chain
BENCHMARK(DynCast<Chain<2>, Chain<0>, Chain<1>>)->Name("Chain middle, 1 level");
BENCHMARK(DynCast<Chain<4>, Chain<0>, Chain<2>>)->Name("Chain middle, 2 levels");
BENCHMARK(DynCast<Chain<6>, Chain<0>, Chain<3>>)->Name("Chain middle, 3 levels");
BENCHMARK(DynCast<Chain<8>, Chain<0>, Chain<4>>)->Name("Chain middle, 4 levels");

// Downcast along a chain that fails
BENCHMARK(DynCast<Chain<1>, Chain<0>, Chain<9>>)->Name("Chain fail, 1 level");
BENCHMARK(DynCast<Chain<2>, Chain<0>, Chain<9>>)->Name("Chain fail, 2 levels");
BENCHMARK(DynCast<Chain<3>, Chain<0>, Chain<9>>)->Name("Chain fail, 3 levels");
BENCHMARK(DynCast<Chain<4>, Chain<0>, Chain<9>>)->Name("Chain fail, 4 levels");
BENCHMARK(DynCast<Chain<5>, Chain<0>, Chain<9>>)->Name("Chain fail, 5 levels");
BENCHMARK(DynCast<Chain<6>, Chain<0>, Chain<9>>)->Name("Chain fail, 6 levels");
BENCHMARK(DynCast<Chain<7>, Chain<0>, Chain<9>>)->Name("Chain fail, 7 levels");
BENCHMARK(DynCast<Chain<8>, Chain<0>, Chain<9>>)->Name("Chain fail, 8 levels");

// Downcast along a virtual inheritance chain from base to the most derived type
BENCHMARK(DynCast<VChain<1>, VChain<0>>)->Name("VChain, 1 level");
BENCHMARK(DynCast<VChain<2>, VChain<0>>)->Name("VChain, 2 levels");
BENCHMARK(DynCast<VChain<3>, VChain<0>>)->Name("VChain, 3 levels");
BENCHMARK(DynCast<VChain<4>, VChain<0>>)->Name("VChain, 4 levels");
BENCHMARK(DynCast<VChain<5>, VChain<0>>)->Name("VChain, 5 levels");

// Downcast along a virtual inheritance chain from base to the middle of the chain
BENCHMARK(DynCast<VChain<2>, VChain<0>, VChain<1>>)->Name("VChain middle, 1 level");
BENCHMARK(DynCast<VChain<4>, VChain<0>, VChain<2>>)->Name("VChain middle, 2 levels");
BENCHMARK(DynCast<VChain<6>, VChain<0>, VChain<3>>)->Name("VChain middle, 3 levels");
BENCHMARK(DynCast<VChain<8>, VChain<0>, VChain<4>>)->Name("VChain middle, 4 levels");

// Downcast along a virtual chain that fails
BENCHMARK(DynCast<VChain<1>, VChain<0>, VChain<8>>)->Name("VChain fail, 1 level");
BENCHMARK(DynCast<VChain<2>, VChain<0>, VChain<8>>)->Name("VChain fail, 2 levels");
BENCHMARK(DynCast<VChain<3>, VChain<0>, VChain<8>>)->Name("VChain fail, 3 levels");
BENCHMARK(DynCast<VChain<4>, VChain<0>, VChain<8>>)->Name("VChain fail, 4 levels");
BENCHMARK(DynCast<VChain<5>, VChain<0>, VChain<8>>)->Name("VChain fail, 5 levels");

// Downcast along a DAG from base to the most derived type
BENCHMARK(DynCast<Dag<0, 3>, Dag<3, 0>>)->Name("DAG rightmost, 3 levels");
BENCHMARK(DynCast<Dag<0, 4>, Dag<4, 0>>)->Name("DAG rightmost, 4 levels");
BENCHMARK(DynCast<Dag<0, 5>, Dag<5, 0>>)->Name("DAG rightmost, 5 levels");
BENCHMARK(DynCast<Dag<0, 3>, Dag<0, 0>>)->Name("DAG leftmost, 3 levels");
BENCHMARK(DynCast<Dag<0, 4>, Dag<0, 0>>)->Name("DAG leftmost, 4 levels");
BENCHMARK(DynCast<Dag<0, 5>, Dag<0, 0>>)->Name("DAG leftmost, 5 levels");

// Downcast along a DAG from base to the middle of the DAG
BENCHMARK(DynCast<Dag<0, 4>, Dag<4, 0>, Dag<3, 1>>)->Name("DAG rightmost middle, 1 level");
BENCHMARK(DynCast<Dag<0, 4>, Dag<4, 0>, Dag<2, 2>>)->Name("DAG rightmost middle, 2 levels");
BENCHMARK(DynCast<Dag<0, 4>, Dag<4, 0>, Dag<1, 3>>)->Name("DAG rightmost middle, 3 levels");
BENCHMARK(DynCast<Dag<0, 4>, Dag<0, 0>, Dag<0, 1>>)->Name("DAG leftmost middle, 1 level");
BENCHMARK(DynCast<Dag<0, 4>, Dag<0, 0>, Dag<0, 2>>)->Name("DAG leftmost middle, 2 levels");
BENCHMARK(DynCast<Dag<0, 4>, Dag<0, 0>, Dag<0, 3>>)->Name("DAG leftmost middle, 3 levels");

// Sidecast along a DAG
BENCHMARK(DynCast<Dag<0, 3>, Dag<3, 0>, Dag<0, 0>>)->Name("DAG sidecast, 3 levels");
BENCHMARK(DynCast<Dag<0, 3>, Dag<2, 1>, Dag<0, 1>>)->Name("DAG sidecast, 2 levels");
BENCHMARK(DynCast<Dag<0, 3>, Dag<1, 2>, Dag<0, 2>>)->Name("DAG sidecast, 1 level");

// Sidecast along a DAG that fails
BENCHMARK(DynCast<Dag<0, 3>, Dag<3, 0>, Dag<0, 4>>)->Name("DAG sidecast fail, 3 levels");
BENCHMARK(DynCast<Dag<0, 3>, Dag<2, 1>, Dag<0, 4>>)->Name("DAG sidecast fail, 2 levels");
BENCHMARK(DynCast<Dag<0, 3>, Dag<1, 2>, Dag<0, 4>>)->Name("DAG sidecast fail, 1 level");

// Downcast along a virtual inheritance DAG from base to the most derived type
BENCHMARK(DynCast<VDag<0, 3>, VDag<3, 0>>)->Name("VDAG rightmost, 3 levels");
BENCHMARK(DynCast<VDag<0, 4>, VDag<4, 0>>)->Name("VDAG rightmost, 4 levels");
BENCHMARK(DynCast<VDag<0, 5>, VDag<5, 0>>)->Name("VDAG rightmost, 5 levels");
BENCHMARK(DynCast<VDag<0, 3>, VDag<0, 0>>)->Name("VDAG leftmost, 3 levels");
BENCHMARK(DynCast<VDag<0, 4>, VDag<0, 0>>)->Name("VDAG leftmost, 4 levels");
BENCHMARK(DynCast<VDag<0, 5>, VDag<0, 0>>)->Name("VDAG leftmost, 5 levels");

// Downcast along a virtual inheritance DAG from base to the middle of the DAG
BENCHMARK(DynCast<VDag<0, 3>, VDag<3, 0>, VDag<2, 1>>)->Name("VDAG rightmost middle, 1 level");
BENCHMARK(DynCast<VDag<0, 4>, VDag<4, 0>, VDag<2, 2>>)->Name("VDAG rightmost middle, 2 levels");
BENCHMARK(DynCast<VDag<0, 5>, VDag<5, 0>, VDag<2, 3>>)->Name("VDAG rightmost middle, 3 levels");
BENCHMARK(DynCast<VDag<0, 3>, VDag<0, 0>, VDag<0, 1>>)->Name("VDAG leftmost middle, 1 level");
BENCHMARK(DynCast<VDag<0, 4>, VDag<0, 0>, VDag<0, 2>>)->Name("VDAG leftmost middle, 2 levels");
BENCHMARK(DynCast<VDag<0, 5>, VDag<0, 0>, VDag<0, 3>>)->Name("VDAG leftmost middle, 3 levels");

// Sidecast along a virtual inheritance DAG
BENCHMARK(DynCast<VDag<0, 3>, VDag<3, 0>, VDag<0, 0>>)->Name("VDAG sidecast, 3 levels");
BENCHMARK(DynCast<VDag<0, 3>, VDag<2, 1>, VDag<0, 1>>)->Name("VDAG sidecast, 2 levels");
BENCHMARK(DynCast<VDag<0, 3>, VDag<1, 2>, VDag<0, 2>>)->Name("VDAG sidecast, 1 level");

// Sidecast along a virtual inheritance DAG that fails
BENCHMARK(DynCast<VDag<0, 3>, VDag<3, 0>, VDag<0, 4>>)->Name("VDAG sidecast fail, 3 levels");
BENCHMARK(DynCast<VDag<0, 3>, VDag<2, 1>, VDag<0, 4>>)->Name("VDAG sidecast fail, 2 levels");
BENCHMARK(DynCast<VDag<0, 3>, VDag<1, 2>, VDag<0, 4>>)->Name("VDAG sidecast fail, 1 level");

// Cast to complete object pointer
BENCHMARK(DynCast<Chain<8>, Chain<0>, void>)->Name("Chain to complete");
BENCHMARK(DynCast<VChain<5>, VChain<0>, void>)->Name("VChain to complete");
BENCHMARK(DynCast<Dag<0, 3>, Dag<3, 0>, void>)->Name("DAG to complete");
BENCHMARK(DynCast<VDag<0, 3>, VDag<3, 0>, void>)->Name("VDAG to complete");

// Static cast as the baseline.
BENCHMARK(StaticCast)->Name("Static");

BENCHMARK_MAIN();
