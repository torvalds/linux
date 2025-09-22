#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <vector>

#include "benchmark/benchmark.h"

#include "ContainerBenchmarks.h"
#include "GenerateInput.h"

using namespace ContainerBenchmarks;

constexpr std::size_t TestNumInputs = 1024;

BENCHMARK_CAPTURE(BM_ConstructSize, vector_byte, std::vector<unsigned char>{})->Arg(5140480);

BENCHMARK_CAPTURE(BM_CopyConstruct, vector_int, std::vector<int>{})->Arg(5140480);

BENCHMARK_CAPTURE(BM_Assignment, vector_int, std::vector<int>{})->Arg(5140480);

BENCHMARK_CAPTURE(BM_ConstructSizeValue, vector_byte, std::vector<unsigned char>{}, 0)->Arg(5140480);

BENCHMARK_CAPTURE(BM_ConstructIterIter, vector_char, std::vector<char>{}, getRandomIntegerInputs<char>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_ConstructIterIter, vector_size_t, std::vector<size_t>{}, getRandomIntegerInputs<size_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_ConstructIterIter, vector_string, std::vector<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_ConstructFromRange, vector_char, std::vector<char>{}, getRandomIntegerInputs<char>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_ConstructFromRange, vector_size_t, std::vector<size_t>{}, getRandomIntegerInputs<size_t>)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_ConstructFromRange, vector_string, std::vector<std::string>{}, getRandomStringInputs)
    ->Arg(TestNumInputs);

BENCHMARK_CAPTURE(BM_Pushback_no_grow, vector_int, std::vector<int>{})->Arg(TestNumInputs);

template <class T>
void bm_grow(benchmark::State& state) {
  for (auto _ : state) {
    std::vector<T> vec;
    benchmark::DoNotOptimize(vec);
    for (size_t i = 0; i != 2048; ++i)
      vec.emplace_back();
    benchmark::DoNotOptimize(vec);
  }
}
BENCHMARK(bm_grow<int>);
BENCHMARK(bm_grow<std::string>);
BENCHMARK(bm_grow<std::unique_ptr<int>>);
BENCHMARK(bm_grow<std::deque<int>>);

BENCHMARK_MAIN();
