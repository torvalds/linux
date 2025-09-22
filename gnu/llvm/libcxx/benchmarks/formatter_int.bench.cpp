//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <array>
#include <format>
#include <random>

#include "CartesianBenchmarks.h"
#include "benchmark/benchmark.h"

// Tests the full range of the value.
template <class T>
static std::array<T, 1000> generate(std::uniform_int_distribution<T> distribution = std::uniform_int_distribution<T>{
                                        std::numeric_limits<T>::min(), std::numeric_limits<T>::max()}) {
  std::mt19937 generator;
  std::array<T, 1000> result;
  std::generate_n(result.begin(), result.size(), [&] { return distribution(generator); });
  return result;
}

template <class T>
static void BM_Basic(benchmark::State& state) {
  std::array data{generate<T>()};
  std::array<char, 100> output;

  while (state.KeepRunningBatch(data.size()))
    for (auto value : data)
      benchmark::DoNotOptimize(std::format_to(output.begin(), "{}", value));
}
BENCHMARK_TEMPLATE(BM_Basic, uint32_t);
BENCHMARK_TEMPLATE(BM_Basic, int32_t);
BENCHMARK_TEMPLATE(BM_Basic, uint64_t);
BENCHMARK_TEMPLATE(BM_Basic, int64_t);

// Ideally the low values of a 128-bit value are all dispatched to a 64-bit routine.
template <class T>
static void BM_BasicLow(benchmark::State& state) {
  using U = std::conditional_t<std::is_signed_v<T>, int64_t, uint64_t>;
  std::array data{
      generate<T>(std::uniform_int_distribution<T>{std::numeric_limits<U>::min(), std::numeric_limits<U>::max()})};
  std::array<char, 100> output;

  while (state.KeepRunningBatch(data.size()))
    for (auto value : data)
      benchmark::DoNotOptimize(std::format_to(output.begin(), "{}", value));
}
BENCHMARK_TEMPLATE(BM_BasicLow, __uint128_t);
BENCHMARK_TEMPLATE(BM_BasicLow, __int128_t);

BENCHMARK_TEMPLATE(BM_Basic, __uint128_t);
BENCHMARK_TEMPLATE(BM_Basic, __int128_t);

// *** Localization ***
enum class LocalizationE { False, True };
struct AllLocalizations : EnumValuesAsTuple<AllLocalizations, LocalizationE, 2> {
  static constexpr const char* Names[] = {"LocFalse", "LocTrue"};
};

template <LocalizationE E>
struct Localization {};

template <>
struct Localization<LocalizationE::False> {
  static constexpr const char* fmt = "";
};

template <>
struct Localization<LocalizationE::True> {
  static constexpr const char* fmt = "L";
};

// *** Base ***
enum class BaseE {
  Binary,
  Octal,
  Decimal,
  Hex,
  HexUpper,
};
struct AllBases : EnumValuesAsTuple<AllBases, BaseE, 5> {
  static constexpr const char* Names[] = {"BaseBin", "BaseOct", "BaseDec", "BaseHex", "BaseHexUpper"};
};

template <BaseE E>
struct Base {};

template <>
struct Base<BaseE::Binary> {
  static constexpr const char* fmt = "b";
};

template <>
struct Base<BaseE::Octal> {
  static constexpr const char* fmt = "o";
};

template <>
struct Base<BaseE::Decimal> {
  static constexpr const char* fmt = "d";
};

template <>
struct Base<BaseE::Hex> {
  static constexpr const char* fmt = "x";
};

template <>
struct Base<BaseE::HexUpper> {
  static constexpr const char* fmt = "X";
};

// *** Types ***
enum class TypeE { Int64, Uint64 };
struct AllTypes : EnumValuesAsTuple<AllTypes, TypeE, 2> {
  static constexpr const char* Names[] = {"Int64", "Uint64"};
};

template <TypeE E>
struct Type {};

template <>
struct Type<TypeE::Int64> {
  using type = int64_t;

  static std::array<type, 1000> make_data() { return generate<type>(); }
};

template <>
struct Type<TypeE::Uint64> {
  using type = uint64_t;

  static std::array<type, 1000> make_data() { return generate<type>(); }
};

// *** Alignment ***
enum class AlignmentE { None, Left, Center, Right, ZeroPadding };
struct AllAlignments : EnumValuesAsTuple<AllAlignments, AlignmentE, 5> {
  static constexpr const char* Names[] = {
      "AlignNone", "AlignmentLeft", "AlignmentCenter", "AlignmentRight", "ZeroPadding"};
};

template <AlignmentE E>
struct Alignment {};

template <>
struct Alignment<AlignmentE::None> {
  static constexpr const char* fmt = "";
};

template <>
struct Alignment<AlignmentE::Left> {
  static constexpr const char* fmt = "0<512";
};

template <>
struct Alignment<AlignmentE::Center> {
  static constexpr const char* fmt = "0^512";
};

template <>
struct Alignment<AlignmentE::Right> {
  static constexpr const char* fmt = "0>512";
};

template <>
struct Alignment<AlignmentE::ZeroPadding> {
  static constexpr const char* fmt = "0512";
};

template <class L, class B, class T, class A>
struct Integral {
  void run(benchmark::State& state) const {
    std::array data{Type<T::value>::make_data()};
    std::array<char, 512> output;

    while (state.KeepRunningBatch(data.size()))
      for (auto value : data)
        benchmark::DoNotOptimize(std::format_to(output.begin(), std::string_view{fmt.data(), fmt.size()}, value));
  }

  std::string name() const { return "Integral" + L::name() + B::name() + A::name() + T::name(); }

  static constexpr std::string make_fmt() {
    return std::string("{:") + Alignment<A::value>::fmt + Localization<L::value>::fmt + Base<B::value>::fmt + "}";
  }

  static constexpr auto fmt = []() {
    constexpr size_t s = make_fmt().size();
    std::array<char, s> r;
    std::ranges::copy(make_fmt(), r.begin());
    return r;
  }();
};

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  makeCartesianProductBenchmark<Integral, AllLocalizations, AllBases, AllTypes, AllAlignments>();

  benchmark::RunSpecifiedBenchmarks();
}
