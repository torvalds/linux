#include <algorithm>
#include <cassert>

#include <benchmark/benchmark.h>

void run_sizes(auto benchmark) {
  benchmark->Arg(1)
      ->Arg(2)
      ->Arg(3)
      ->Arg(4)
      ->Arg(5)
      ->Arg(6)
      ->Arg(7)
      ->Arg(8)
      ->Arg(9)
      ->Arg(10)
      ->Arg(11)
      ->Arg(12)
      ->Arg(13)
      ->Arg(14)
      ->Arg(15)
      ->Arg(16)
      ->Arg(17)
      ->Arg(18)
      ->Arg(19)
      ->Arg(20)
      ->Arg(21)
      ->Arg(22)
      ->Arg(23)
      ->Arg(24)
      ->Arg(25)
      ->Arg(26)
      ->Arg(27)
      ->Arg(28)
      ->Arg(29)
      ->Arg(30)
      ->Arg(31)
      ->Arg(32)
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

template <class T>
static void BM_std_minmax(benchmark::State& state) {
  std::vector<T> vec(state.range(), 3);

  for (auto _ : state) {
    benchmark::DoNotOptimize(vec);
    benchmark::DoNotOptimize(std::ranges::minmax(vec));
  }
}
BENCHMARK(BM_std_minmax<char>)->Apply(run_sizes);
BENCHMARK(BM_std_minmax<short>)->Apply(run_sizes);
BENCHMARK(BM_std_minmax<int>)->Apply(run_sizes);
BENCHMARK(BM_std_minmax<long long>)->Apply(run_sizes);
BENCHMARK(BM_std_minmax<unsigned char>)->Apply(run_sizes);
BENCHMARK(BM_std_minmax<unsigned short>)->Apply(run_sizes);
BENCHMARK(BM_std_minmax<unsigned int>)->Apply(run_sizes);
BENCHMARK(BM_std_minmax<unsigned long long>)->Apply(run_sizes);

BENCHMARK_MAIN();
