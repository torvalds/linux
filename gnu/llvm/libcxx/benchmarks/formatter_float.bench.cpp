//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <format>

#include <array>
#include <limits>
#include <random>
#include <string>

#include "CartesianBenchmarks.h"
#include "benchmark/benchmark.h"

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

// *** Types ***
enum class TypeE { Float, Double, LongDouble };
// TODO FMT Set to 3 after to_chars has long double suport.
struct AllTypes : EnumValuesAsTuple<AllTypes, TypeE, 2> {
  static constexpr const char* Names[] = {"Float", "Double", "LongDouble"};
};

template <TypeE E>
struct Type {};

template <>
struct Type<TypeE::Float> {
  using type = float;
};

template <>
struct Type<TypeE::Double> {
  using type = double;
};

template <>
struct Type<TypeE::LongDouble> {
  using type = long double;
};

// *** Values ***
enum class ValueE { Inf, Random };
struct AllValues : EnumValuesAsTuple<AllValues, ValueE, 2> {
  static constexpr const char* Names[] = {"Inf", "Random"};
};

template <ValueE E>
struct Value {};

template <>
struct Value<ValueE::Inf> {
  template <class F>
  static std::array<F, 1000> make_data() {
    std::array<F, 1000> result;
    std::fill(result.begin(), result.end(), -std::numeric_limits<F>::infinity());
    return result;
  }
};

template <>
struct Value<ValueE::Random> {
  template <class F>
  static std::array<F, 1000> make_data() {
    std::random_device seed;
    std::mt19937 generator(seed());
    std::uniform_int_distribution<std::conditional_t<sizeof(F) == sizeof(uint32_t), uint32_t, uint64_t>> distribution;

    std::array<F, 1000> result;
    std::generate(result.begin(), result.end(), [&] {
      while (true) {
        auto result = std::bit_cast<F>(distribution(generator));
        if (std::isfinite(result))
          return result;
      }
    });
    return result;
  }
};

// *** Display Type ***
enum class DisplayTypeE {
  Default,
  Hex,
  Scientific,
  Fixed,
  General,
};
struct AllDisplayTypes : EnumValuesAsTuple<AllDisplayTypes, DisplayTypeE, 5> {
  static constexpr const char* Names[] = {
      "DisplayDefault", "DisplayHex", "DisplayScientific", "DisplayFixed", "DisplayGeneral"};
};

template <DisplayTypeE E>
struct DisplayType {};

template <>
struct DisplayType<DisplayTypeE::Default> {
  static constexpr const char* fmt = "";
};

template <>
struct DisplayType<DisplayTypeE::Hex> {
  static constexpr const char* fmt = "a";
};

template <>
struct DisplayType<DisplayTypeE::Scientific> {
  static constexpr const char* fmt = "e";
};

template <>
struct DisplayType<DisplayTypeE::Fixed> {
  static constexpr const char* fmt = "f";
};

template <>
struct DisplayType<DisplayTypeE::General> {
  static constexpr const char* fmt = "g";
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
  // Width > PrecisionE::Huge
  static constexpr const char* fmt = "0<17500";
};

template <>
struct Alignment<AlignmentE::Center> {
  // Width > PrecisionE::Huge
  static constexpr const char* fmt = "0^17500";
};

template <>
struct Alignment<AlignmentE::Right> {
  // Width > PrecisionE::Huge
  static constexpr const char* fmt = "0>17500";
};

template <>
struct Alignment<AlignmentE::ZeroPadding> {
  // Width > PrecisionE::Huge
  static constexpr const char* fmt = "017500";
};

enum class PrecisionE { None, Zero, Small, Huge };
struct AllPrecisions : EnumValuesAsTuple<AllPrecisions, PrecisionE, 4> {
  static constexpr const char* Names[] = {"PrecNone", "PrecZero", "PrecSmall", "PrecHuge"};
};

template <PrecisionE E>
struct Precision {};

template <>
struct Precision<PrecisionE::None> {
  static constexpr const char* fmt = "";
};

template <>
struct Precision<PrecisionE::Zero> {
  static constexpr const char* fmt = ".0";
};

template <>
struct Precision<PrecisionE::Small> {
  static constexpr const char* fmt = ".10";
};

template <>
struct Precision<PrecisionE::Huge> {
  // The maximum precision for a minimal sub normal long double is +/- 0x1p-16494.
  // This value is always larger than that value forcing the trailing zero path
  // to be executed.
  static constexpr const char* fmt = ".17000";
};

template <class L, class DT, class T, class V, class A, class P>
struct FloatingPoint {
  using F = typename Type<T::value>::type;

  void run(benchmark::State& state) const {
    std::array<F, 1000> data{Value<V::value>::template make_data<F>()};
    std::array<char, 20'000> output;

    while (state.KeepRunningBatch(1000))
      for (F value : data)
        benchmark::DoNotOptimize(std::format_to(output.begin(), std::string_view{fmt.data(), fmt.size()}, value));
  }

  std::string name() const {
    return "FloatingPoint" + L::name() + DT::name() + T::name() + V::name() + A::name() + P::name();
  }

  static constexpr std::string make_fmt() {
    return std::string("{:") + Alignment<A::value>::fmt + Precision<P::value>::fmt + Localization<L::value>::fmt +
           DisplayType<DT::value>::fmt + "}";
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

  makeCartesianProductBenchmark<FloatingPoint,
                                AllLocalizations,
                                AllDisplayTypes,
                                AllTypes,
                                AllValues,
                                AllAlignments,
                                AllPrecisions>();

  benchmark::RunSpecifiedBenchmarks();
}
