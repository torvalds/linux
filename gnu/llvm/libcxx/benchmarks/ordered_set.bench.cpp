//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "CartesianBenchmarks.h"
#include "benchmark/benchmark.h"
#include "test_macros.h"

namespace {

enum class HitType { Hit, Miss };

struct AllHitTypes : EnumValuesAsTuple<AllHitTypes, HitType, 2> {
  static constexpr const char* Names[] = {"Hit", "Miss"};
};

enum class AccessPattern { Ordered, Random };

struct AllAccessPattern : EnumValuesAsTuple<AllAccessPattern, AccessPattern, 2> {
  static constexpr const char* Names[] = {"Ordered", "Random"};
};

void sortKeysBy(std::vector<uint64_t>& Keys, AccessPattern AP) {
  if (AP == AccessPattern::Random) {
    std::random_device R;
    std::mt19937 M(R());
    std::shuffle(std::begin(Keys), std::end(Keys), M);
  }
}

struct TestSets {
  std::vector<std::set<uint64_t> > Sets;
  std::vector<uint64_t> Keys;
};

TestSets makeTestingSets(size_t TableSize, size_t NumTables, HitType Hit, AccessPattern Access) {
  TestSets R;
  R.Sets.resize(1);

  for (uint64_t I = 0; I < TableSize; ++I) {
    R.Sets[0].insert(2 * I);
    R.Keys.push_back(Hit == HitType::Hit ? 2 * I : 2 * I + 1);
  }
  R.Sets.resize(NumTables, R.Sets[0]);
  sortKeysBy(R.Keys, Access);

  return R;
}

struct Base {
  size_t TableSize;
  size_t NumTables;
  Base(size_t T, size_t N) : TableSize(T), NumTables(N) {}

  bool skip() const {
    size_t Total = TableSize * NumTables;
    return Total < 100 || Total > 1000000;
  }

  std::string baseName() const {
    return "_TableSize" + std::to_string(TableSize) + "_NumTables" + std::to_string(NumTables);
  }
};

template <class Access>
struct Create : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    std::vector<uint64_t> Keys(TableSize);
    std::iota(Keys.begin(), Keys.end(), uint64_t{0});
    sortKeysBy(Keys, Access());

    while (State.KeepRunningBatch(TableSize * NumTables)) {
      std::vector<std::set<uint64_t>> Sets(NumTables);
      for (auto K : Keys) {
        for (auto& Set : Sets) {
          benchmark::DoNotOptimize(Set.insert(K));
        }
      }
    }
  }

  std::string name() const { return "BM_Create" + Access::name() + baseName(); }
};

template <class Hit, class Access>
struct Find : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(TableSize, NumTables, Hit(), Access());

    while (State.KeepRunningBatch(TableSize * NumTables)) {
      for (auto K : Data.Keys) {
        for (auto& Set : Data.Sets) {
          benchmark::DoNotOptimize(Set.find(K));
        }
      }
    }
  }

  std::string name() const { return "BM_Find" + Hit::name() + Access::name() + baseName(); }
};

template <class Hit, class Access>
struct FindNeEnd : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(TableSize, NumTables, Hit(), Access());

    while (State.KeepRunningBatch(TableSize * NumTables)) {
      for (auto K : Data.Keys) {
        for (auto& Set : Data.Sets) {
          benchmark::DoNotOptimize(Set.find(K) != Set.end());
        }
      }
    }
  }

  std::string name() const { return "BM_FindNeEnd" + Hit::name() + Access::name() + baseName(); }
};

template <class Access>
struct InsertHit : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(TableSize, NumTables, HitType::Hit, Access());

    while (State.KeepRunningBatch(TableSize * NumTables)) {
      for (auto K : Data.Keys) {
        for (auto& Set : Data.Sets) {
          benchmark::DoNotOptimize(Set.insert(K));
        }
      }
    }
  }

  std::string name() const { return "BM_InsertHit" + Access::name() + baseName(); }
};

template <class Access>
struct InsertMissAndErase : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(TableSize, NumTables, HitType::Miss, Access());

    while (State.KeepRunningBatch(TableSize * NumTables)) {
      for (auto K : Data.Keys) {
        for (auto& Set : Data.Sets) {
          benchmark::DoNotOptimize(Set.erase(Set.insert(K).first));
        }
      }
    }
  }

  std::string name() const { return "BM_InsertMissAndErase" + Access::name() + baseName(); }
};

struct IterateRangeFor : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(TableSize, NumTables, HitType::Miss, AccessPattern::Ordered);

    while (State.KeepRunningBatch(TableSize * NumTables)) {
      for (auto& Set : Data.Sets) {
        for (auto& V : Set) {
          benchmark::DoNotOptimize(V);
        }
      }
    }
  }

  std::string name() const { return "BM_IterateRangeFor" + baseName(); }
};

struct IterateBeginEnd : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(TableSize, NumTables, HitType::Miss, AccessPattern::Ordered);

    while (State.KeepRunningBatch(TableSize * NumTables)) {
      for (auto& Set : Data.Sets) {
        for (auto it = Set.begin(); it != Set.end(); ++it) {
          benchmark::DoNotOptimize(*it);
        }
      }
    }
  }

  std::string name() const { return "BM_IterateBeginEnd" + baseName(); }
};

} // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

  const std::vector<size_t> TableSize{1, 10, 100, 1000, 10000, 100000, 1000000};
  const std::vector<size_t> NumTables{1, 10, 100, 1000, 10000, 100000, 1000000};

  makeCartesianProductBenchmark<Create, AllAccessPattern>(TableSize, NumTables);
  makeCartesianProductBenchmark<Find, AllHitTypes, AllAccessPattern>(TableSize, NumTables);
  makeCartesianProductBenchmark<FindNeEnd, AllHitTypes, AllAccessPattern>(TableSize, NumTables);
  makeCartesianProductBenchmark<InsertHit, AllAccessPattern>(TableSize, NumTables);
  makeCartesianProductBenchmark<InsertMissAndErase, AllAccessPattern>(TableSize, NumTables);
  makeCartesianProductBenchmark<IterateRangeFor>(TableSize, NumTables);
  makeCartesianProductBenchmark<IterateBeginEnd>(TableSize, NumTables);
  benchmark::RunSpecifiedBenchmarks();
}
