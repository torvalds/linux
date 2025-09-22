#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <tuple>
#include <vector>

#include "benchmark/benchmark.h"

#include "CartesianBenchmarks.h"
#include "GenerateInput.h"

namespace {

template <typename I, typename N>
std::array<I, 10> every_10th_percentile_N(I first, N n) {
  N step = n / 10;
  std::array<I, 10> res;

  for (size_t i = 0; i < 10; ++i) {
    res[i] = first;
    std::advance(first, step);
  }

  return res;
}

template <class IntT>
struct TestIntBase {
  static std::vector<IntT> generateInput(size_t size) {
    std::vector<IntT> Res(size);
    std::generate(Res.begin(), Res.end(), [] { return getRandomInteger<IntT>(0, std::numeric_limits<IntT>::max()); });
    return Res;
  }
};

struct TestInt32 : TestIntBase<std::int32_t> {
  static constexpr const char* Name = "TestInt32";
};

struct TestInt64 : TestIntBase<std::int64_t> {
  static constexpr const char* Name = "TestInt64";
};

struct TestUint32 : TestIntBase<std::uint32_t> {
  static constexpr const char* Name = "TestUint32";
};

struct TestMediumString {
  static constexpr const char* Name  = "TestMediumString";
  static constexpr size_t StringSize = 32;

  static std::vector<std::string> generateInput(size_t size) {
    std::vector<std::string> Res(size);
    std::generate(Res.begin(), Res.end(), [] { return getRandomString(StringSize); });
    return Res;
  }
};

using AllTestTypes = std::tuple<TestInt32, TestInt64, TestUint32, TestMediumString>;

struct LowerBoundAlg {
  template <class I, class V>
  I operator()(I first, I last, const V& value) const {
    return std::lower_bound(first, last, value);
  }

  static constexpr const char* Name = "LowerBoundAlg";
};

struct UpperBoundAlg {
  template <class I, class V>
  I operator()(I first, I last, const V& value) const {
    return std::upper_bound(first, last, value);
  }

  static constexpr const char* Name = "UpperBoundAlg";
};

struct EqualRangeAlg {
  template <class I, class V>
  std::pair<I, I> operator()(I first, I last, const V& value) const {
    return std::equal_range(first, last, value);
  }

  static constexpr const char* Name = "EqualRangeAlg";
};

using AllAlgs = std::tuple<LowerBoundAlg, UpperBoundAlg, EqualRangeAlg>;

template <class Alg, class TestType>
struct PartitionPointBench {
  size_t Quantity;

  std::string name() const {
    return std::string("PartitionPointBench_") + Alg::Name + "_" + TestType::Name + '/' + std::to_string(Quantity);
  }

  void run(benchmark::State& state) const {
    auto Data = TestType::generateInput(Quantity);
    std::sort(Data.begin(), Data.end());
    auto Every10Percentile = every_10th_percentile_N(Data.begin(), Data.size());

    for (auto _ : state) {
      for (auto Test : Every10Percentile)
        benchmark::DoNotOptimize(Alg{}(Data.begin(), Data.end(), *Test));
    }
  }
};

} // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  const std::vector<size_t> Quantities = {1 << 8, 1 << 10, 1 << 20};
  makeCartesianProductBenchmark<PartitionPointBench, AllAlgs, AllTestTypes>(Quantities);
  benchmark::RunSpecifiedBenchmarks();
}
