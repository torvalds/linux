//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "benchmark/benchmark.h"
#include "test_macros.h"

namespace internal {

template <class D, class E, size_t I>
struct EnumValue : std::integral_constant<E, static_cast<E>(I)> {
  static std::string name() { return std::string("_") + D::Names[I]; }
};

template <class D, class E, size_t... Idxs>
constexpr auto makeEnumValueTuple(std::index_sequence<Idxs...>) {
  return std::make_tuple(EnumValue<D, E, Idxs>{}...);
}

template <class B>
static auto skip(const B& Bench, int) -> decltype(Bench.skip()) {
  return Bench.skip();
}
template <class B>
static auto skip(const B& Bench, char) {
  return false;
}

template <class B, class Args, size_t... Is>
void makeBenchmarkFromValuesImpl(const Args& A, std::index_sequence<Is...>) {
  for (auto& V : A) {
    B Bench{std::get<Is>(V)...};
    if (!internal::skip(Bench, 0)) {
      benchmark::RegisterBenchmark(Bench.name().c_str(), [=](benchmark::State& S) { Bench.run(S); });
    }
  }
}

template <class B, class... Args>
void makeBenchmarkFromValues(const std::vector<std::tuple<Args...> >& A) {
  makeBenchmarkFromValuesImpl<B>(A, std::index_sequence_for<Args...>());
}

template <template <class...> class B, class Args, class... U>
void makeBenchmarkImpl(const Args& A, std::tuple<U...> t) {
  makeBenchmarkFromValues<B<U...> >(A);
}

template <template <class...> class B, class Args, class... U, class... T, class... Tuples>
void makeBenchmarkImpl(const Args& A, std::tuple<U...>, std::tuple<T...>, Tuples... rest) {
  (internal::makeBenchmarkImpl<B>(A, std::tuple<U..., T>(), rest...), ...);
}

template <class R, class T>
void allValueCombinations(R& Result, const T& Final) {
  return Result.push_back(Final);
}

template <class R, class T, class V, class... Vs>
void allValueCombinations(R& Result, const T& Prev, const V& Value, const Vs&... Values) {
  for (const auto& E : Value) {
    allValueCombinations(Result, std::tuple_cat(Prev, std::make_tuple(E)), Values...);
  }
}

} // namespace internal

// CRTP class that enables using enum types as a dimension for
// makeCartesianProductBenchmark below.
// The type passed to `B` will be a std::integral_constant<E, e>, with the
// additional static function `name()` that returns the stringified name of the
// label.
//
// Eg:
// enum class MyEnum { A, B };
// struct AllMyEnum : EnumValuesAsTuple<AllMyEnum, MyEnum, 2> {
//   static constexpr absl::string_view Names[] = {"A", "B"};
// };
template <class Derived, class EnumType, size_t NumLabels>
using EnumValuesAsTuple =
    decltype(internal::makeEnumValueTuple<Derived, EnumType>(std::make_index_sequence<NumLabels>{}));

// Instantiates B<T0, T1, ..., TN> where <Ti...> are the combinations in the
// cartesian product of `Tuples...`, and pass (arg0, ..., argN) as constructor
// arguments where `(argi...)` are the combination in the cartesian product of
// the runtime values of `A...`.
// B<T...> requires:
//  - std::string name(args...): The name of the benchmark.
//  - void run(benchmark::State&, args...): The body of the benchmark.
// It can also optionally provide:
//  - bool skip(args...): When `true`, skips the combination. Default is false.
//
// Returns int to facilitate registration. The return value is unspecified.
template <template <class...> class B, class... Tuples, class... Args>
int makeCartesianProductBenchmark(const Args&... A) {
  std::vector<std::tuple<typename Args::value_type...> > V;
  internal::allValueCombinations(V, std::tuple<>(), A...);
  internal::makeBenchmarkImpl<B>(V, std::tuple<>(), Tuples()...);
  return 0;
}

template <class B, class... Args>
int makeCartesianProductBenchmark(const Args&... A) {
  std::vector<std::tuple<typename Args::value_type...> > V;
  internal::allValueCombinations(V, std::tuple<>(), A...);
  internal::makeBenchmarkFromValues<B>(V);
  return 0;
}

// When `opaque` is true, this function hides the runtime state of `value` from
// the optimizer.
// It returns `value`.
template <class T>
TEST_ALWAYS_INLINE inline T maybeOpaque(T value, bool opaque) {
  if (opaque)
    benchmark::DoNotOptimize(value);
  return value;
}
