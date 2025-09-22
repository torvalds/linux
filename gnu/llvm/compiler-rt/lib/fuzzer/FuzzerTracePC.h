//===- FuzzerTracePC.h - Internal header for the Fuzzer ---------*- C++ -* ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// fuzzer::TracePC
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_TRACE_PC
#define LLVM_FUZZER_TRACE_PC

#include "FuzzerDefs.h"
#include "FuzzerDictionary.h"
#include "FuzzerValueBitMap.h"

#include <set>
#include <unordered_map>

namespace fuzzer {

// TableOfRecentCompares (TORC) remembers the most recently performed
// comparisons of type T.
// We record the arguments of CMP instructions in this table unconditionally
// because it seems cheaper this way than to compute some expensive
// conditions inside __sanitizer_cov_trace_cmp*.
// After the unit has been executed we may decide to use the contents of
// this table to populate a Dictionary.
template<class T, size_t kSizeT>
struct TableOfRecentCompares {
  static const size_t kSize = kSizeT;
  struct Pair {
    T A, B;
  };
  ATTRIBUTE_NO_SANITIZE_ALL
  void Insert(size_t Idx, const T &Arg1, const T &Arg2) {
    Idx = Idx % kSize;
    Table[Idx].A = Arg1;
    Table[Idx].B = Arg2;
  }

  Pair Get(size_t I) { return Table[I % kSize]; }

  Pair Table[kSize];
};

template <size_t kSizeT>
struct MemMemTable {
  static const size_t kSize = kSizeT;
  Word MemMemWords[kSize];
  Word EmptyWord;

  void Add(const uint8_t *Data, size_t Size) {
    if (Size <= 2) return;
    Size = std::min(Size, Word::GetMaxSize());
    auto Idx = SimpleFastHash(Data, Size) % kSize;
    MemMemWords[Idx].Set(Data, Size);
  }
  const Word &Get(size_t Idx) {
    for (size_t i = 0; i < kSize; i++) {
      const Word &W = MemMemWords[(Idx + i) % kSize];
      if (W.size()) return W;
    }
    EmptyWord.Set(nullptr, 0);
    return EmptyWord;
  }
};

class TracePC {
 public:
  void HandleInline8bitCountersInit(uint8_t *Start, uint8_t *Stop);
  void HandlePCsInit(const uintptr_t *Start, const uintptr_t *Stop);
  void HandleCallerCallee(uintptr_t Caller, uintptr_t Callee);
  template <class T> void HandleCmp(uintptr_t PC, T Arg1, T Arg2);
  size_t GetTotalPCCoverage();
  void SetUseCounters(bool UC) { UseCounters = UC; }
  void SetUseValueProfileMask(uint32_t VPMask) { UseValueProfileMask = VPMask; }
  void SetPrintNewPCs(bool P) { DoPrintNewPCs = P; }
  void SetPrintNewFuncs(size_t P) { NumPrintNewFuncs = P; }
  void UpdateObservedPCs();
  template <class Callback> size_t CollectFeatures(Callback CB) const;

  void ResetMaps() {
    ValueProfileMap.Reset();
    ClearExtraCounters();
    ClearInlineCounters();
  }

  void ClearInlineCounters();

  void UpdateFeatureSet(size_t CurrentElementIdx, size_t CurrentElementSize);
  void PrintFeatureSet();

  void PrintModuleInfo();

  void PrintCoverage(bool PrintAllCounters);

  template<class CallBack>
  void IterateCoveredFunctions(CallBack CB);

  void AddValueForMemcmp(void *caller_pc, const void *s1, const void *s2,
                         size_t n, bool StopAtZero);

  TableOfRecentCompares<uint32_t, 32> TORC4;
  TableOfRecentCompares<uint64_t, 32> TORC8;
  TableOfRecentCompares<Word, 32> TORCW;
  MemMemTable<1024> MMT;

  void RecordInitialStack();
  uintptr_t GetMaxStackOffset() const;

  template<class CallBack>
  void ForEachObservedPC(CallBack CB) {
    for (auto PC : ObservedPCs)
      CB(PC);
  }

  void SetFocusFunction(const std::string &FuncName);
  bool ObservedFocusFunction();

  struct PCTableEntry {
    uintptr_t PC, PCFlags;
  };

  uintptr_t PCTableEntryIdx(const PCTableEntry *TE);
  const PCTableEntry *PCTableEntryByIdx(uintptr_t Idx);
  static uintptr_t GetNextInstructionPc(uintptr_t PC);
  bool PcIsFuncEntry(const PCTableEntry *TE) { return TE->PCFlags & 1; }

private:
  bool UseCounters = false;
  uint32_t UseValueProfileMask = false;
  bool DoPrintNewPCs = false;
  size_t NumPrintNewFuncs = 0;

  // Module represents the array of 8-bit counters split into regions
  // such that every region, except maybe the first and the last one, is one
  // full page.
  struct Module {
    struct Region {
      uint8_t *Start, *Stop;
      bool Enabled;
      bool OneFullPage;
    };
    Region *Regions;
    size_t NumRegions;
    uint8_t *Start() { return Regions[0].Start; }
    uint8_t *Stop()  { return Regions[NumRegions - 1].Stop; }
    size_t Size()   { return Stop() - Start(); }
    size_t  Idx(uint8_t *P) {
      assert(P >= Start() && P < Stop());
      return P - Start();
    }
  };

  Module Modules[4096];
  size_t NumModules;  // linker-initialized.
  size_t NumInline8bitCounters;

  template <class Callback>
  void IterateCounterRegions(Callback CB) {
    for (size_t m = 0; m < NumModules; m++)
      for (size_t r = 0; r < Modules[m].NumRegions; r++)
        CB(Modules[m].Regions[r]);
  }

  struct { const PCTableEntry *Start, *Stop; } ModulePCTable[4096];
  size_t NumPCTables;
  size_t NumPCsInPCTables;

  std::set<const PCTableEntry *> ObservedPCs;
  std::unordered_map<uintptr_t, uintptr_t> ObservedFuncs;  // PC => Counter.

  uint8_t *FocusFunctionCounterPtr = nullptr;

  ValueBitMap ValueProfileMap;
  uintptr_t InitialStack;
};

template <class Callback>
// void Callback(size_t FirstFeature, size_t Idx, uint8_t Value);
ATTRIBUTE_NO_SANITIZE_ALL
size_t ForEachNonZeroByte(const uint8_t *Begin, const uint8_t *End,
                        size_t FirstFeature, Callback Handle8bitCounter) {
  typedef uintptr_t LargeType;
  const size_t Step = sizeof(LargeType) / sizeof(uint8_t);
  const size_t StepMask = Step - 1;
  auto P = Begin;
  // Iterate by 1 byte until either the alignment boundary or the end.
  for (; reinterpret_cast<uintptr_t>(P) & StepMask && P < End; P++)
    if (uint8_t V = *P)
      Handle8bitCounter(FirstFeature, P - Begin, V);

  // Iterate by Step bytes at a time.
  for (; P + Step <= End; P += Step)
    if (LargeType Bundle = *reinterpret_cast<const LargeType *>(P)) {
      Bundle = HostToLE(Bundle);
      for (size_t I = 0; I < Step; I++, Bundle >>= 8)
        if (uint8_t V = Bundle & 0xff)
          Handle8bitCounter(FirstFeature, P - Begin + I, V);
    }

  // Iterate by 1 byte until the end.
  for (; P < End; P++)
    if (uint8_t V = *P)
      Handle8bitCounter(FirstFeature, P - Begin, V);
  return End - Begin;
}

// Given a non-zero Counter returns a number in the range [0,7].
template<class T>
unsigned CounterToFeature(T Counter) {
    // Returns a feature number by placing Counters into buckets as illustrated
    // below.
    //
    // Counter bucket: [1] [2] [3] [4-7] [8-15] [16-31] [32-127] [128+]
    // Feature number:  0   1   2    3     4       5       6       7
    //
    // This is a heuristic taken from AFL (see
    // http://lcamtuf.coredump.cx/afl/technical_details.txt).
    //
    // This implementation may change in the future so clients should
    // not rely on it.
    assert(Counter);
    unsigned Bit = 0;
    /**/ if (Counter >= 128) Bit = 7;
    else if (Counter >= 32) Bit = 6;
    else if (Counter >= 16) Bit = 5;
    else if (Counter >= 8) Bit = 4;
    else if (Counter >= 4) Bit = 3;
    else if (Counter >= 3) Bit = 2;
    else if (Counter >= 2) Bit = 1;
    return Bit;
}

template <class Callback> // void Callback(uint32_t Feature)
ATTRIBUTE_NO_SANITIZE_ADDRESS ATTRIBUTE_NOINLINE size_t
TracePC::CollectFeatures(Callback HandleFeature) const {
  auto Handle8bitCounter = [&](size_t FirstFeature,
                               size_t Idx, uint8_t Counter) {
    if (UseCounters)
      HandleFeature(static_cast<uint32_t>(FirstFeature + Idx * 8 +
                                          CounterToFeature(Counter)));
    else
      HandleFeature(static_cast<uint32_t>(FirstFeature + Idx));
  };

  size_t FirstFeature = 0;

  for (size_t i = 0; i < NumModules; i++) {
    for (size_t r = 0; r < Modules[i].NumRegions; r++) {
      if (!Modules[i].Regions[r].Enabled) continue;
      FirstFeature += 8 * ForEachNonZeroByte(Modules[i].Regions[r].Start,
                                             Modules[i].Regions[r].Stop,
                                             FirstFeature, Handle8bitCounter);
    }
  }

  FirstFeature +=
      8 * ForEachNonZeroByte(ExtraCountersBegin(), ExtraCountersEnd(),
                             FirstFeature, Handle8bitCounter);

  if (UseValueProfileMask) {
    ValueProfileMap.ForEach([&](size_t Idx) {
      HandleFeature(static_cast<uint32_t>(FirstFeature + Idx));
    });
    FirstFeature += ValueProfileMap.SizeInBits();
  }

  // Step function, grows similar to 8 * Log_2(A).
  auto StackDepthStepFunction = [](size_t A) -> size_t {
    if (!A)
      return A;
    auto Log2 = Log(A);
    if (Log2 < 3)
      return A;
    Log2 -= 3;
    return (Log2 + 1) * 8 + ((A >> Log2) & 7);
  };
  assert(StackDepthStepFunction(1024) == 64);
  assert(StackDepthStepFunction(1024 * 4) == 80);
  assert(StackDepthStepFunction(1024 * 1024) == 144);

  if (auto MaxStackOffset = GetMaxStackOffset()) {
    HandleFeature(static_cast<uint32_t>(
        FirstFeature + StackDepthStepFunction(MaxStackOffset / 8)));
    FirstFeature += StackDepthStepFunction(std::numeric_limits<size_t>::max());
  }

  return FirstFeature;
}

extern TracePC TPC;

}  // namespace fuzzer

#endif  // LLVM_FUZZER_TRACE_PC
