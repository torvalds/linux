//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "CartesianBenchmarks.h"
#include "benchmark/benchmark.h"
#include "test_macros.h"

namespace {

enum class FunctionType {
  Null,
  FunctionPointer,
  MemberFunctionPointer,
  MemberPointer,
  SmallTrivialFunctor,
  SmallNonTrivialFunctor,
  LargeTrivialFunctor,
  LargeNonTrivialFunctor
};

struct AllFunctionTypes : EnumValuesAsTuple<AllFunctionTypes, FunctionType, 8> {
  static constexpr const char* Names[] = {
      "Null",
      "FuncPtr",
      "MemFuncPtr",
      "MemPtr",
      "SmallTrivialFunctor",
      "SmallNonTrivialFunctor",
      "LargeTrivialFunctor",
      "LargeNonTrivialFunctor"};
};

enum class Opacity { kOpaque, kTransparent };

struct AllOpacity : EnumValuesAsTuple<AllOpacity, Opacity, 2> {
  static constexpr const char* Names[] = {"Opaque", "Transparent"};
};

struct S {
  int function() const { return 0; }
  int field = 0;
};

int FunctionWithS(const S*) { return 0; }

struct SmallTrivialFunctor {
  int operator()(const S*) const { return 0; }
};
struct SmallNonTrivialFunctor {
  SmallNonTrivialFunctor() {}
  SmallNonTrivialFunctor(const SmallNonTrivialFunctor&) {}
  ~SmallNonTrivialFunctor() {}
  int operator()(const S*) const { return 0; }
};
struct LargeTrivialFunctor {
  LargeTrivialFunctor() {
    // Do not spend time initializing the padding.
  }
  int padding[16];
  int operator()(const S*) const { return 0; }
};
struct LargeNonTrivialFunctor {
  int padding[16];
  LargeNonTrivialFunctor() {
    // Do not spend time initializing the padding.
  }
  LargeNonTrivialFunctor(const LargeNonTrivialFunctor&) {}
  ~LargeNonTrivialFunctor() {}
  int operator()(const S*) const { return 0; }
};

using Function = std::function<int(const S*)>;

TEST_ALWAYS_INLINE
inline Function MakeFunction(FunctionType type, bool opaque = false) {
  switch (type) {
  case FunctionType::Null:
    return nullptr;
  case FunctionType::FunctionPointer:
    return maybeOpaque(FunctionWithS, opaque);
  case FunctionType::MemberFunctionPointer:
    return maybeOpaque(&S::function, opaque);
  case FunctionType::MemberPointer:
    return maybeOpaque(&S::field, opaque);
  case FunctionType::SmallTrivialFunctor:
    return maybeOpaque(SmallTrivialFunctor{}, opaque);
  case FunctionType::SmallNonTrivialFunctor:
    return maybeOpaque(SmallNonTrivialFunctor{}, opaque);
  case FunctionType::LargeTrivialFunctor:
    return maybeOpaque(LargeTrivialFunctor{}, opaque);
  case FunctionType::LargeNonTrivialFunctor:
    return maybeOpaque(LargeNonTrivialFunctor{}, opaque);
  }
}

template <class Opacity, class FunctionType>
struct ConstructAndDestroy {
  static void run(benchmark::State& state) {
    for (auto _ : state) {
      if (Opacity() == ::Opacity::kOpaque) {
        benchmark::DoNotOptimize(MakeFunction(FunctionType(), true));
      } else {
        MakeFunction(FunctionType());
      }
    }
  }

  static std::string name() { return "BM_ConstructAndDestroy" + FunctionType::name() + Opacity::name(); }
};

template <class FunctionType>
struct Copy {
  static void run(benchmark::State& state) {
    auto value = MakeFunction(FunctionType());
    for (auto _ : state) {
      benchmark::DoNotOptimize(value);
      auto copy = value; // NOLINT
      benchmark::DoNotOptimize(copy);
    }
  }

  static std::string name() { return "BM_Copy" + FunctionType::name(); }
};

template <class FunctionType>
struct Move {
  static void run(benchmark::State& state) {
    Function values[2] = {MakeFunction(FunctionType())};
    int i              = 0;
    for (auto _ : state) {
      benchmark::DoNotOptimize(values);
      benchmark::DoNotOptimize(values[i ^ 1] = std::move(values[i]));
      i ^= 1;
    }
  }

  static std::string name() { return "BM_Move" + FunctionType::name(); }
};

template <class Function1, class Function2>
struct Swap {
  static void run(benchmark::State& state) {
    Function values[2] = {MakeFunction(Function1()), MakeFunction(Function2())};
    for (auto _ : state) {
      benchmark::DoNotOptimize(values);
      values[0].swap(values[1]);
    }
  }

  static bool skip() { return Function1() > Function2(); }

  static std::string name() { return "BM_Swap" + Function1::name() + Function2::name(); }
};

template <class FunctionType>
struct OperatorBool {
  static void run(benchmark::State& state) {
    auto f = MakeFunction(FunctionType());
    for (auto _ : state) {
      benchmark::DoNotOptimize(f);
      benchmark::DoNotOptimize(static_cast<bool>(f));
    }
  }

  static std::string name() { return "BM_OperatorBool" + FunctionType::name(); }
};

template <class FunctionType>
struct Invoke {
  static void run(benchmark::State& state) {
    S s;
    const auto value = MakeFunction(FunctionType());
    for (auto _ : state) {
      benchmark::DoNotOptimize(value);
      benchmark::DoNotOptimize(value(&s));
    }
  }

  static bool skip() { return FunctionType() == ::FunctionType::Null; }

  static std::string name() { return "BM_Invoke" + FunctionType::name(); }
};

template <class FunctionType>
struct InvokeInlined {
  static void run(benchmark::State& state) {
    S s;
    for (auto _ : state) {
      MakeFunction(FunctionType())(&s);
    }
  }

  static bool skip() { return FunctionType() == ::FunctionType::Null; }

  static std::string name() { return "BM_InvokeInlined" + FunctionType::name(); }
};

} // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  makeCartesianProductBenchmark<ConstructAndDestroy, AllOpacity, AllFunctionTypes>();
  makeCartesianProductBenchmark<Copy, AllFunctionTypes>();
  makeCartesianProductBenchmark<Move, AllFunctionTypes>();
  makeCartesianProductBenchmark<Swap, AllFunctionTypes, AllFunctionTypes>();
  makeCartesianProductBenchmark<OperatorBool, AllFunctionTypes>();
  makeCartesianProductBenchmark<Invoke, AllFunctionTypes>();
  makeCartesianProductBenchmark<InvokeInlined, AllFunctionTypes>();
  benchmark::RunSpecifiedBenchmarks();
}
