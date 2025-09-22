#include "benchmark/benchmark.h"
#include "test_macros.h"

#include <mutex>
#include <sstream>

TEST_NOINLINE double istream_numbers();

double istream_numbers(std::locale* loc) {
  const char* a[] = {"-6  69 -71  2.4882e-02 -100 101 -2.00005 5000000 -50000000",
                     "-25 71   7 -9.3262e+01 -100 101 -2.00005 5000000 -50000000",
                     "-14 53  46 -6.7026e-02 -100 101 -2.00005 5000000 -50000000"};

  int a1, a2, a3, a4, a5, a6, a7;
  double f1 = 0.0, f2 = 0.0, q = 0.0;
  for (int i = 0; i < 3; i++) {
    std::istringstream s(a[i]);
    if (loc)
      s.imbue(*loc);
    s >> a1 >> a2 >> a3 >> f1 >> a4 >> a5 >> f2 >> a6 >> a7;
    q += (a1 + a2 + a3 + a4 + a5 + a6 + a7 + f1 + f2) / 1000000;
  }
  return q;
}

struct LocaleSelector {
  std::locale* imbue;
  std::locale old;
  static std::mutex mutex;

  LocaleSelector(benchmark::State& state) {
    std::lock_guard guard(mutex);
    switch (state.range(0)) {
    case 0: {
      old   = std::locale::global(std::locale::classic());
      imbue = nullptr;
      break;
    }
    case 1: {
      old = std::locale::global(std::locale::classic());
      thread_local std::locale loc("en_US.UTF-8");
      imbue = &loc;
      break;
    }
    case 2: {
      old = std::locale::global(std::locale::classic());
      static std::locale loc("en_US.UTF-8");
      imbue = &loc;
      break;
    }
    case 3: {
      old   = std::locale::global(std::locale("en_US.UTF-8"));
      imbue = nullptr;
      break;
    }
    }
  }

  ~LocaleSelector() {
    std::lock_guard guard(mutex);
    std::locale::global(old);
  }
};

std::mutex LocaleSelector::mutex;

static void BM_Istream_numbers(benchmark::State& state) {
  LocaleSelector sel(state);
  double i = 0;
  while (state.KeepRunning())
    benchmark::DoNotOptimize(i += istream_numbers(sel.imbue));
}
BENCHMARK(BM_Istream_numbers)->DenseRange(0, 3)->UseRealTime()->Threads(1)->ThreadPerCpu();

static void BM_Ostream_number(benchmark::State& state) {
  LocaleSelector sel(state);
  while (state.KeepRunning()) {
    std::ostringstream ss;
    if (sel.imbue)
      ss.imbue(*sel.imbue);
    ss << 0;
    benchmark::DoNotOptimize(ss.str().c_str());
  }
}
BENCHMARK(BM_Ostream_number)->DenseRange(0, 3)->UseRealTime()->Threads(1)->ThreadPerCpu();

BENCHMARK_MAIN();
