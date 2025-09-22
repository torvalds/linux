//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include "CartesianBenchmarks.h"
#include "benchmark/benchmark.h"
#include "test_macros.h"

// When VALIDATE is defined the benchmark will run to validate the benchmarks.
// The time taken by several operations depend on whether or not an element
// exists. To avoid errors in the benchmark these operations have a validation
// mode to test the benchmark. Since they are not meant to be benchmarked the
// number of sizes tested is limited to 1.
// #define VALIDATE

namespace {

enum class Mode { Hit, Miss };

struct AllModes : EnumValuesAsTuple<AllModes, Mode, 2> {
  static constexpr const char* Names[] = {"ExistingElement", "NewElement"};
};

// The positions of the hints to pick:
// - Begin picks the first item. The item cannot be put before this element.
// - Thrid picks the third item. This is just an element with a valid entry
//   before and after it.
// - Correct contains the correct hint.
// - End contains a hint to the end of the map.
enum class Hint { Begin, Third, Correct, End };
struct AllHints : EnumValuesAsTuple<AllHints, Hint, 4> {
  static constexpr const char* Names[] = {"Begin", "Third", "Correct", "End"};
};

enum class Order { Sorted, Random };
struct AllOrders : EnumValuesAsTuple<AllOrders, Order, 2> {
  static constexpr const char* Names[] = {"Sorted", "Random"};
};

struct TestSets {
  std::vector<uint64_t> Keys;
  std::vector<std::map<uint64_t, int64_t> > Maps;
  std::vector<std::vector<typename std::map<uint64_t, int64_t>::const_iterator> > Hints;
};

enum class Shuffle { None, Keys, Hints };

TestSets makeTestingSets(size_t MapSize, Mode mode, Shuffle shuffle, size_t max_maps) {
  /*
   * The shuffle does not retain the random number generator to use the same
   * set of random numbers for every iteration.
   */
  TestSets R;

  int MapCount = std::min(max_maps, 1000000 / MapSize);

  for (uint64_t I = 0; I < MapSize; ++I) {
    R.Keys.push_back(mode == Mode::Hit ? 2 * I + 2 : 2 * I + 1);
  }
  if (shuffle == Shuffle::Keys)
    std::shuffle(R.Keys.begin(), R.Keys.end(), std::mt19937());

  for (int M = 0; M < MapCount; ++M) {
    auto& map   = R.Maps.emplace_back();
    auto& hints = R.Hints.emplace_back();
    for (uint64_t I = 0; I < MapSize; ++I) {
      hints.push_back(map.insert(std::make_pair(2 * I + 2, 0)).first);
    }
    if (shuffle == Shuffle::Hints)
      std::shuffle(hints.begin(), hints.end(), std::mt19937());
  }

  return R;
}

struct Base {
  size_t MapSize;
  Base(size_t T) : MapSize(T) {}

  std::string baseName() const { return "_MapSize=" + std::to_string(MapSize); }
};

//*******************************************************************|
//                       Member functions                            |
//*******************************************************************|

struct ConstructorDefault {
  void run(benchmark::State& State) const {
    for (auto _ : State) {
      benchmark::DoNotOptimize(std::map<uint64_t, int64_t>());
    }
  }

  std::string name() const { return "BM_ConstructorDefault"; }
};

struct ConstructorIterator : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    while (State.KeepRunningBatch(MapSize)) {
#ifndef VALIDATE
      benchmark::DoNotOptimize(std::map<uint64_t, int64_t>(Map.begin(), Map.end()));
#else
      std::map<uint64_t, int64_t> M{Map.begin(), Map.end()};
      if (M != Map)
        State.SkipWithError("Map copy not identical");
#endif
    }
  }

  std::string name() const { return "BM_ConstructorIterator" + baseName(); }
};

struct ConstructorCopy : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    while (State.KeepRunningBatch(MapSize)) {
#ifndef VALIDATE
      std::map<uint64_t, int64_t> M(Map);
      benchmark::DoNotOptimize(M);
#else
      std::map<uint64_t, int64_t> M(Map);
      if (M != Map)
        State.SkipWithError("Map copy not identical");
#endif
    }
  }

  std::string name() const { return "BM_ConstructorCopy" + baseName(); }
};

struct ConstructorMove : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
        std::map<uint64_t, int64_t> M(std::move(Map));
        benchmark::DoNotOptimize(M);
      }
      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_ConstructorMove" + baseName(); }
};

//*******************************************************************|
//                           Capacity                                |
//*******************************************************************|

struct Empty : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    for (auto _ : State) {
#ifndef VALIDATE
      benchmark::DoNotOptimize(Map.empty());
#else
      if (Map.empty())
        State.SkipWithError("Map contains an invalid number of elements.");
#endif
    }
  }

  std::string name() const { return "BM_Empty" + baseName(); }
};

struct Size : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    for (auto _ : State) {
#ifndef VALIDATE
      benchmark::DoNotOptimize(Map.size());
#else
      if (Map.size() != MapSize)
        State.SkipWithError("Map contains an invalid number of elements.");
#endif
    }
  }

  std::string name() const { return "BM_Size" + baseName(); }
};

//*******************************************************************|
//                           Modifiers                               |
//*******************************************************************|

struct Clear : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
        Map.clear();
        benchmark::DoNotOptimize(Map);
      }
      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_Clear" + baseName(); }
};

template <class Mode, class Order>
struct Insert : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.insert(std::make_pair(K, 1)));
#else
          bool Inserted = Map.insert(std::make_pair(K, 1)).second;
          if (Mode() == ::Mode::Hit) {
            if (Inserted)
              State.SkipWithError("Inserted a duplicate element");
          } else {
            if (!Inserted)
              State.SkipWithError("Failed to insert e new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_Insert" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Hint>
struct InsertHint : Base {
  using Base::Base;

  template < ::Hint hint>
  typename std::enable_if<hint == ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map = Data.Maps[I];
        auto H    = Data.Hints[I].begin();
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.insert(*H, std::make_pair(K, 1)));
#else
          auto Inserted = Map.insert(*H, std::make_pair(K, 1));
          if (Mode() == ::Mode::Hit) {
            if (Inserted != *H)
              State.SkipWithError("Inserted a duplicate element");
          } else {
            if (++Inserted != *H)
              State.SkipWithError("Failed to insert a new element");
          }
#endif
          ++H;
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  template < ::Hint hint>
  typename std::enable_if<hint != ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map  = Data.Maps[I];
        auto Third = *(Data.Hints[I].begin() + 2);
        for (auto K : Data.Keys) {
          auto Itor = hint == ::Hint::Begin ? Map.begin() : hint == ::Hint::Third ? Third : Map.end();
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.insert(Itor, std::make_pair(K, 1)));
#else
          size_t Size = Map.size();
          Map.insert(Itor, std::make_pair(K, 1));
          if (Mode() == ::Mode::Hit) {
            if (Size != Map.size())
              State.SkipWithError("Inserted a duplicate element");
          } else {
            if (Size + 1 != Map.size())
              State.SkipWithError("Failed to insert a new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  void run(benchmark::State& State) const {
    static constexpr auto h = Hint();
    run<h>(State);
  }

  std::string name() const { return "BM_InsertHint" + baseName() + Mode::name() + Hint::name(); }
};

template <class Mode, class Order>
struct InsertAssign : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.insert_or_assign(K, 1));
#else
          bool Inserted = Map.insert_or_assign(K, 1).second;
          if (Mode() == ::Mode::Hit) {
            if (Inserted)
              State.SkipWithError("Inserted a duplicate element");
          } else {
            if (!Inserted)
              State.SkipWithError("Failed to insert e new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_InsertAssign" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Hint>
struct InsertAssignHint : Base {
  using Base::Base;

  template < ::Hint hint>
  typename std::enable_if<hint == ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map = Data.Maps[I];
        auto H    = Data.Hints[I].begin();
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.insert_or_assign(*H, K, 1));
#else
          auto Inserted = Map.insert_or_assign(*H, K, 1);
          if (Mode() == ::Mode::Hit) {
            if (Inserted != *H)
              State.SkipWithError("Inserted a duplicate element");
          } else {
            if (++Inserted != *H)
              State.SkipWithError("Failed to insert a new element");
          }
#endif
          ++H;
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  template < ::Hint hint>
  typename std::enable_if<hint != ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map  = Data.Maps[I];
        auto Third = *(Data.Hints[I].begin() + 2);
        for (auto K : Data.Keys) {
          auto Itor = hint == ::Hint::Begin ? Map.begin() : hint == ::Hint::Third ? Third : Map.end();
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.insert_or_assign(Itor, K, 1));
#else
          size_t Size = Map.size();
          Map.insert_or_assign(Itor, K, 1);
          if (Mode() == ::Mode::Hit) {
            if (Size != Map.size())
              State.SkipWithError("Inserted a duplicate element");
          } else {
            if (Size + 1 != Map.size())
              State.SkipWithError("Failed to insert a new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  void run(benchmark::State& State) const {
    static constexpr auto h = Hint();
    run<h>(State);
  }

  std::string name() const { return "BM_InsertAssignHint" + baseName() + Mode::name() + Hint::name(); }
};

template <class Mode, class Order>
struct Emplace : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.emplace(K, 1));
#else
          bool Inserted = Map.emplace(K, 1).second;
          if (Mode() == ::Mode::Hit) {
            if (Inserted)
              State.SkipWithError("Emplaced a duplicate element");
          } else {
            if (!Inserted)
              State.SkipWithError("Failed to emplace a new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_Emplace" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Hint>
struct EmplaceHint : Base {
  using Base::Base;

  template < ::Hint hint>
  typename std::enable_if<hint == ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map = Data.Maps[I];
        auto H    = Data.Hints[I].begin();
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.emplace_hint(*H, K, 1));
#else
          auto Inserted = Map.emplace_hint(*H, K, 1);
          if (Mode() == ::Mode::Hit) {
            if (Inserted != *H)
              State.SkipWithError("Emplaced a duplicate element");
          } else {
            if (++Inserted != *H)
              State.SkipWithError("Failed to emplace a new element");
          }
#endif
          ++H;
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  template < ::Hint hint>
  typename std::enable_if<hint != ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map  = Data.Maps[I];
        auto Third = *(Data.Hints[I].begin() + 2);
        for (auto K : Data.Keys) {
          auto Itor = hint == ::Hint::Begin ? Map.begin() : hint == ::Hint::Third ? Third : Map.end();
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.emplace_hint(Itor, K, 1));
#else
          size_t Size = Map.size();
          Map.emplace_hint(Itor, K, 1);
          if (Mode() == ::Mode::Hit) {
            if (Size != Map.size())
              State.SkipWithError("Emplaced a duplicate element");
          } else {
            if (Size + 1 != Map.size())
              State.SkipWithError("Failed to emplace a new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  void run(benchmark::State& State) const {
    static constexpr auto h = Hint();
    run<h>(State);
  }

  std::string name() const { return "BM_EmplaceHint" + baseName() + Mode::name() + Hint::name(); }
};

template <class Mode, class Order>
struct TryEmplace : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.try_emplace(K, 1));
#else
          bool Inserted = Map.try_emplace(K, 1).second;
          if (Mode() == ::Mode::Hit) {
            if (Inserted)
              State.SkipWithError("Emplaced a duplicate element");
          } else {
            if (!Inserted)
              State.SkipWithError("Failed to emplace a new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_TryEmplace" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Hint>
struct TryEmplaceHint : Base {
  using Base::Base;

  template < ::Hint hint>
  typename std::enable_if<hint == ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map = Data.Maps[I];
        auto H    = Data.Hints[I].begin();
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.try_emplace(*H, K, 1));
#else
          auto Inserted = Map.try_emplace(*H, K, 1);
          if (Mode() == ::Mode::Hit) {
            if (Inserted != *H)
              State.SkipWithError("Emplaced a duplicate element");
          } else {
            if (++Inserted != *H)
              State.SkipWithError("Failed to emplace a new element");
          }
#endif
          ++H;
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  template < ::Hint hint>
  typename std::enable_if<hint != ::Hint::Correct>::type run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map  = Data.Maps[I];
        auto Third = *(Data.Hints[I].begin() + 2);
        for (auto K : Data.Keys) {
          auto Itor = hint == ::Hint::Begin ? Map.begin() : hint == ::Hint::Third ? Third : Map.end();
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.try_emplace(Itor, K, 1));
#else
          size_t Size = Map.size();
          Map.try_emplace(Itor, K, 1);
          if (Mode() == ::Mode::Hit) {
            if (Size != Map.size())
              State.SkipWithError("Emplaced a duplicate element");
          } else {
            if (Size + 1 != Map.size())
              State.SkipWithError("Failed to emplace a new element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  void run(benchmark::State& State) const {
    static constexpr auto h = Hint();
    run<h>(State);
  }

  std::string name() const { return "BM_TryEmplaceHint" + baseName() + Mode::name() + Hint::name(); }
};

template <class Mode, class Order>
struct Erase : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
        for (auto K : Data.Keys) {
#ifndef VALIDATE
          benchmark::DoNotOptimize(Map.erase(K));
#else
          size_t I = Map.erase(K);
          if (Mode() == ::Mode::Hit) {
            if (I == 0)
              State.SkipWithError("Did not find the existing element");
          } else {
            if (I == 1)
              State.SkipWithError("Did find the non-existing element");
          }
#endif
        }
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_Erase" + baseName() + Mode::name() + Order::name(); }
};

template <class Order>
struct EraseIterator : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data =
        makeTestingSets(MapSize, Mode::Hit, Order::value == ::Order::Random ? Shuffle::Hints : Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (size_t I = 0; I < Data.Maps.size(); ++I) {
        auto& Map = Data.Maps[I];
        for (auto H : Data.Hints[I]) {
          benchmark::DoNotOptimize(Map.erase(H));
        }
#ifdef VALIDATE
        if (!Map.empty())
          State.SkipWithError("Did not erase the entire map");
#endif
      }

      State.PauseTiming();
      Data =
          makeTestingSets(MapSize, Mode::Hit, Order::value == ::Order::Random ? Shuffle::Hints : Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_EraseIterator" + baseName() + Order::name(); }
};

struct EraseRange : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1000);
    while (State.KeepRunningBatch(MapSize * Data.Maps.size())) {
      for (auto& Map : Data.Maps) {
#ifndef VALIDATE
        benchmark::DoNotOptimize(Map.erase(Map.begin(), Map.end()));
#else
        Map.erase(Map.begin(), Map.end());
        if (!Map.empty())
          State.SkipWithError("Did not erase the entire map");
#endif
      }

      State.PauseTiming();
      Data = makeTestingSets(MapSize, Mode::Hit, Shuffle::None, 1000);
      State.ResumeTiming();
    }
  }

  std::string name() const { return "BM_EraseRange" + baseName(); }
};

//*******************************************************************|
//                            Lookup                                 |
//*******************************************************************|

template <class Mode, class Order>
struct Count : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    while (State.KeepRunningBatch(MapSize)) {
      for (auto K : Data.Keys) {
#ifndef VALIDATE
        benchmark::DoNotOptimize(Map.count(K));
#else
        size_t I = Map.count(K);
        if (Mode() == ::Mode::Hit) {
          if (I == 0)
            State.SkipWithError("Did not find the existing element");
        } else {
          if (I == 1)
            State.SkipWithError("Did find the non-existing element");
        }
#endif
      }
    }
  }

  std::string name() const { return "BM_Count" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Order>
struct Find : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    while (State.KeepRunningBatch(MapSize)) {
      for (auto K : Data.Keys) {
#ifndef VALIDATE
        benchmark::DoNotOptimize(Map.find(K));
#else
        auto Itor = Map.find(K);
        if (Mode() == ::Mode::Hit) {
          if (Itor == Map.end())
            State.SkipWithError("Did not find the existing element");
        } else {
          if (Itor != Map.end())
            State.SkipWithError("Did find the non-existing element");
        }
#endif
      }
    }
  }

  std::string name() const { return "BM_Find" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Order>
struct EqualRange : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    while (State.KeepRunningBatch(MapSize)) {
      for (auto K : Data.Keys) {
#ifndef VALIDATE
        benchmark::DoNotOptimize(Map.equal_range(K));
#else
        auto Range = Map.equal_range(K);
        if (Mode() == ::Mode::Hit) {
          // Adjust validation for the last element.
          auto Key = K;
          if (Range.second == Map.end() && K == 2 * MapSize) {
            --Range.second;
            Key -= 2;
          }
          if (Range.first == Map.end() || Range.first->first != K || Range.second == Map.end() ||
              Range.second->first - 2 != Key)
            State.SkipWithError("Did not find the existing element");
        } else {
          if (Range.first == Map.end() || Range.first->first - 1 != K || Range.second == Map.end() ||
              Range.second->first - 1 != K)
            State.SkipWithError("Did find the non-existing element");
        }
#endif
      }
    }
  }

  std::string name() const { return "BM_EqualRange" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Order>
struct LowerBound : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    while (State.KeepRunningBatch(MapSize)) {
      for (auto K : Data.Keys) {
#ifndef VALIDATE
        benchmark::DoNotOptimize(Map.lower_bound(K));
#else
        auto Itor = Map.lower_bound(K);
        if (Mode() == ::Mode::Hit) {
          if (Itor == Map.end() || Itor->first != K)
            State.SkipWithError("Did not find the existing element");
        } else {
          if (Itor == Map.end() || Itor->first - 1 != K)
            State.SkipWithError("Did find the non-existing element");
        }
#endif
      }
    }
  }

  std::string name() const { return "BM_LowerBound" + baseName() + Mode::name() + Order::name(); }
};

template <class Mode, class Order>
struct UpperBound : Base {
  using Base::Base;

  void run(benchmark::State& State) const {
    auto Data = makeTestingSets(MapSize, Mode(), Order::value == ::Order::Random ? Shuffle::Keys : Shuffle::None, 1);
    auto& Map = Data.Maps.front();
    while (State.KeepRunningBatch(MapSize)) {
      for (auto K : Data.Keys) {
#ifndef VALIDATE
        benchmark::DoNotOptimize(Map.upper_bound(K));
#else
        std::map<uint64_t, int64_t>::iterator Itor = Map.upper_bound(K);
        if (Mode() == ::Mode::Hit) {
          // Adjust validation for the last element.
          auto Key = K;
          if (Itor == Map.end() && K == 2 * MapSize) {
            --Itor;
            Key -= 2;
          }
          if (Itor == Map.end() || Itor->first - 2 != Key)
            State.SkipWithError("Did not find the existing element");
        } else {
          if (Itor == Map.end() || Itor->first - 1 != K)
            State.SkipWithError("Did find the non-existing element");
        }
#endif
      }
    }
  }

  std::string name() const { return "BM_UpperBound" + baseName() + Mode::name() + Order::name(); }
};

} // namespace

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;

#ifdef VALIDATE
  const std::vector<size_t> MapSize{10};
#else
  const std::vector<size_t> MapSize{10, 100, 1000, 10000, 100000, 1000000};
#endif

  // Member functions
  makeCartesianProductBenchmark<ConstructorDefault>();
  makeCartesianProductBenchmark<ConstructorIterator>(MapSize);
  makeCartesianProductBenchmark<ConstructorCopy>(MapSize);
  makeCartesianProductBenchmark<ConstructorMove>(MapSize);

  // Capacity
  makeCartesianProductBenchmark<Empty>(MapSize);
  makeCartesianProductBenchmark<Size>(MapSize);

  // Modifiers
  makeCartesianProductBenchmark<Clear>(MapSize);
  makeCartesianProductBenchmark<Insert, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<InsertHint, AllModes, AllHints>(MapSize);
  makeCartesianProductBenchmark<InsertAssign, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<InsertAssignHint, AllModes, AllHints>(MapSize);

  makeCartesianProductBenchmark<Emplace, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<EmplaceHint, AllModes, AllHints>(MapSize);
  makeCartesianProductBenchmark<TryEmplace, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<TryEmplaceHint, AllModes, AllHints>(MapSize);
  makeCartesianProductBenchmark<Erase, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<EraseIterator, AllOrders>(MapSize);
  makeCartesianProductBenchmark<EraseRange>(MapSize);

  // Lookup
  makeCartesianProductBenchmark<Count, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<Find, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<EqualRange, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<LowerBound, AllModes, AllOrders>(MapSize);
  makeCartesianProductBenchmark<UpperBound, AllModes, AllOrders>(MapSize);

  benchmark::RunSpecifiedBenchmarks();
}
