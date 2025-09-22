#include "benchmark/benchmark.h"
#include "llvm/Support/YAMLTraits.h"

static void BM_YAMLDummyIsNumeric(benchmark::State& state) {
  std::string x = "hello";
  for (auto _ : state) {
    std::string copy(x);
    llvm::yaml::isNumeric(copy);
  }
}
BENCHMARK(BM_YAMLDummyIsNumeric);

BENCHMARK_MAIN();
