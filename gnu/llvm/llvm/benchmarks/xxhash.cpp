#include "llvm/Support/xxhash.h"
#include "benchmark/benchmark.h"

#include <memory>

static uint32_t xorshift(uint32_t State) {
  State ^= State << 13;
  State ^= State >> 17;
  State ^= State << 5;
  return State;
}

static void BM_xxh3_64bits(benchmark::State &State) {
  std::unique_ptr<uint32_t[]> Data(new uint32_t[State.range(0) / 4]);

  uint32_t Prev = 0xcafebabe;
  for (int64_t I = 0; I < State.range(0) / 4; I++)
    Data[I] = Prev = xorshift(Prev);

  llvm::ArrayRef DataRef =
      llvm::ArrayRef(reinterpret_cast<uint8_t *>(Data.get()), State.range(0));

  for (auto _ : State)
    llvm::xxh3_64bits(DataRef);
}

BENCHMARK(BM_xxh3_64bits)->Arg(32)->Arg(512)->Arg(64 * 1024)->Arg(1024 * 1024);

BENCHMARK_MAIN();
