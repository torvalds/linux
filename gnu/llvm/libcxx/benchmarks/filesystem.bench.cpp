#include <filesystem>

#include "GenerateInput.h"
#include "benchmark/benchmark.h"
#include "test_iterators.h"

namespace fs = std::filesystem;

static const size_t TestNumInputs = 1024;

template <class GenInputs>
void BM_PathConstructString(benchmark::State& st, GenInputs gen) {
  using fs::path;
  const auto in = gen(st.range(0));
  path PP;
  for (auto& Part : in)
    PP /= Part;
  benchmark::DoNotOptimize(PP.native().data());
  while (st.KeepRunning()) {
    const path P(PP.native());
    benchmark::DoNotOptimize(P.native().data());
  }
  st.SetComplexityN(st.range(0));
}
BENCHMARK_CAPTURE(BM_PathConstructString, large_string, getRandomStringInputs)->Range(8, TestNumInputs)->Complexity();

template <class GenInputs>
void BM_PathConstructCStr(benchmark::State& st, GenInputs gen) {
  using fs::path;
  const auto in = gen(st.range(0));
  path PP;
  for (auto& Part : in)
    PP /= Part;
  benchmark::DoNotOptimize(PP.native().data());
  while (st.KeepRunning()) {
    const path P(PP.native().c_str());
    benchmark::DoNotOptimize(P.native().data());
  }
}
BENCHMARK_CAPTURE(BM_PathConstructCStr, large_string, getRandomStringInputs)->Arg(TestNumInputs);

template <template <class...> class ItType, class GenInputs>
void BM_PathConstructIter(benchmark::State& st, GenInputs gen) {
  using fs::path;
  using Iter    = ItType<std::string::const_iterator>;
  const auto in = gen(st.range(0));
  path PP;
  for (auto& Part : in)
    PP /= Part;
  auto Start = Iter(PP.native().begin());
  auto End   = Iter(PP.native().end());
  benchmark::DoNotOptimize(PP.native().data());
  benchmark::DoNotOptimize(Start);
  benchmark::DoNotOptimize(End);
  while (st.KeepRunning()) {
    const path P(Start, End);
    benchmark::DoNotOptimize(P.native().data());
  }
  st.SetComplexityN(st.range(0));
}
template <class GenInputs>
void BM_PathConstructInputIter(benchmark::State& st, GenInputs gen) {
  BM_PathConstructIter<cpp17_input_iterator>(st, gen);
}
template <class GenInputs>
void BM_PathConstructForwardIter(benchmark::State& st, GenInputs gen) {
  BM_PathConstructIter<forward_iterator>(st, gen);
}
BENCHMARK_CAPTURE(BM_PathConstructInputIter, large_string, getRandomStringInputs)
    ->Range(8, TestNumInputs)
    ->Complexity();
BENCHMARK_CAPTURE(BM_PathConstructForwardIter, large_string, getRandomStringInputs)
    ->Range(8, TestNumInputs)
    ->Complexity();

template <class GenInputs>
void BM_PathIterateMultipleTimes(benchmark::State& st, GenInputs gen) {
  using fs::path;
  const auto in = gen(st.range(0));
  path PP;
  for (auto& Part : in)
    PP /= Part;
  benchmark::DoNotOptimize(PP.native().data());
  while (st.KeepRunning()) {
    for (auto const& E : PP) {
      benchmark::DoNotOptimize(E.native().data());
    }
    benchmark::ClobberMemory();
  }
  st.SetComplexityN(st.range(0));
}
BENCHMARK_CAPTURE(BM_PathIterateMultipleTimes, iterate_elements, getRandomStringInputs)
    ->Range(8, TestNumInputs)
    ->Complexity();

template <class GenInputs>
void BM_PathIterateOnce(benchmark::State& st, GenInputs gen) {
  using fs::path;
  const auto in = gen(st.range(0));
  path PP;
  for (auto& Part : in)
    PP /= Part;
  benchmark::DoNotOptimize(PP.native().data());
  while (st.KeepRunning()) {
    const path P = PP.native();
    for (auto const& E : P) {
      benchmark::DoNotOptimize(E.native().data());
    }
    benchmark::ClobberMemory();
  }
  st.SetComplexityN(st.range(0));
}
BENCHMARK_CAPTURE(BM_PathIterateOnce, iterate_elements, getRandomStringInputs)->Range(8, TestNumInputs)->Complexity();

template <class GenInputs>
void BM_PathIterateOnceBackwards(benchmark::State& st, GenInputs gen) {
  using fs::path;
  const auto in = gen(st.range(0));
  path PP;
  for (auto& Part : in)
    PP /= Part;
  benchmark::DoNotOptimize(PP.native().data());
  while (st.KeepRunning()) {
    const path P = PP.native();
    const auto B = P.begin();
    auto I       = P.end();
    while (I != B) {
      --I;
      benchmark::DoNotOptimize(*I);
    }
    benchmark::DoNotOptimize(*I);
  }
}
BENCHMARK_CAPTURE(BM_PathIterateOnceBackwards, iterate_elements, getRandomStringInputs)->Arg(TestNumInputs);

static fs::path getRandomPaths(int NumParts, int PathLen) {
  fs::path Result;
  while (NumParts--) {
    std::string Part = getRandomString(PathLen);
    Result /= Part;
  }
  return Result;
}

template <class GenInput>
void BM_LexicallyNormal(benchmark::State& st, GenInput gen, size_t PathLen) {
  using fs::path;
  auto In = gen(st.range(0), PathLen);
  benchmark::DoNotOptimize(&In);
  while (st.KeepRunning()) {
    benchmark::DoNotOptimize(In.lexically_normal());
  }
  st.SetComplexityN(st.range(0));
}
BENCHMARK_CAPTURE(BM_LexicallyNormal, small_path, getRandomPaths, /*PathLen*/ 5)
    ->RangeMultiplier(2)
    ->Range(2, 256)
    ->Complexity();
BENCHMARK_CAPTURE(BM_LexicallyNormal, large_path, getRandomPaths, /*PathLen*/ 32)
    ->RangeMultiplier(2)
    ->Range(2, 256)
    ->Complexity();

BENCHMARK_MAIN();
