//===- StackLifetime.h - Alloca Lifetime Analysis --------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_STACKLIFETIME_H
#define LLVM_ANALYSIS_STACKLIFETIME_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

namespace llvm {

class AllocaInst;
class BasicBlock;
class Function;
class Instruction;
class IntrinsicInst;

/// Compute live ranges of allocas.
/// Live ranges are represented as sets of "interesting" instructions, which are
/// defined as instructions that may start or end an alloca's lifetime. These
/// are:
/// * lifetime.start and lifetime.end intrinsics
/// * first instruction of any basic block
/// Interesting instructions are numbered in the depth-first walk of the CFG,
/// and in the program order inside each basic block.
class StackLifetime {
  /// A class representing liveness information for a single basic block.
  /// Each bit in the BitVector represents the liveness property
  /// for a different stack slot.
  struct BlockLifetimeInfo {
    explicit BlockLifetimeInfo(unsigned Size)
        : Begin(Size), End(Size), LiveIn(Size), LiveOut(Size) {}

    /// Which slots BEGINs in each basic block.
    BitVector Begin;

    /// Which slots ENDs in each basic block.
    BitVector End;

    /// Which slots are marked as LIVE_IN, coming into each basic block.
    BitVector LiveIn;

    /// Which slots are marked as LIVE_OUT, coming out of each basic block.
    BitVector LiveOut;
  };

public:
  class LifetimeAnnotationWriter;

  /// This class represents a set of interesting instructions where an alloca is
  /// live.
  class LiveRange {
    BitVector Bits;
    friend raw_ostream &operator<<(raw_ostream &OS,
                                   const StackLifetime::LiveRange &R);

  public:
    LiveRange(unsigned Size, bool Set = false) : Bits(Size, Set) {}
    void addRange(unsigned Start, unsigned End) { Bits.set(Start, End); }

    bool overlaps(const LiveRange &Other) const {
      return Bits.anyCommon(Other.Bits);
    }

    void join(const LiveRange &Other) { Bits |= Other.Bits; }

    bool test(unsigned Idx) const { return Bits.test(Idx); }
  };

  // Controls what is "alive" if control flow may reach the instruction
  // with a different liveness of the alloca.
  enum class LivenessType {
    May,  // May be alive on some path.
    Must, // Must be alive on every path.
  };

private:
  const Function &F;
  LivenessType Type;

  /// Maps active slots (per bit) for each basic block.
  using LivenessMap = DenseMap<const BasicBlock *, BlockLifetimeInfo>;
  LivenessMap BlockLiveness;

  /// Interesting instructions. Instructions of the same block are adjustent
  /// preserve in-block order.
  SmallVector<const IntrinsicInst *, 64> Instructions;

  /// A range [Start, End) of instruction ids for each basic block.
  /// Instructions inside each BB have monotonic and consecutive ids.
  DenseMap<const BasicBlock *, std::pair<unsigned, unsigned>> BlockInstRange;

  ArrayRef<const AllocaInst *> Allocas;
  unsigned NumAllocas;
  DenseMap<const AllocaInst *, unsigned> AllocaNumbering;

  /// LiveRange for allocas.
  SmallVector<LiveRange, 8> LiveRanges;

  /// The set of allocas that have at least one lifetime.start. All other
  /// allocas get LiveRange that corresponds to the entire function.
  BitVector InterestingAllocas;

  struct Marker {
    unsigned AllocaNo;
    bool IsStart;
  };

  /// List of {InstNo, {AllocaNo, IsStart}} for each BB, ordered by InstNo.
  DenseMap<const BasicBlock *, SmallVector<std::pair<unsigned, Marker>, 4>>
      BBMarkers;

  bool HasUnknownLifetimeStartOrEnd = false;

  void dumpAllocas() const;
  void dumpBlockLiveness() const;
  void dumpLiveRanges() const;

  void collectMarkers();
  void calculateLocalLiveness();
  void calculateLiveIntervals();

public:
  StackLifetime(const Function &F, ArrayRef<const AllocaInst *> Allocas,
                LivenessType Type);

  void run();

  iterator_range<
      filter_iterator<ArrayRef<const IntrinsicInst *>::const_iterator,
                      std::function<bool(const IntrinsicInst *)>>>
  getMarkers() const {
    std::function<bool(const IntrinsicInst *)> NotNull(
        [](const IntrinsicInst *I) -> bool { return I; });
    return make_filter_range(Instructions, NotNull);
  }

  /// Returns a set of "interesting" instructions where the given alloca is
  /// live. Not all instructions in a function are interesting: we pick a set
  /// that is large enough for LiveRange::Overlaps to be correct.
  const LiveRange &getLiveRange(const AllocaInst *AI) const;

  /// Returns true if instruction is reachable from entry.
  bool isReachable(const Instruction *I) const;

  /// Returns true if the alloca is alive after the instruction.
  bool isAliveAfter(const AllocaInst *AI, const Instruction *I) const;

  /// Returns a live range that represents an alloca that is live throughout the
  /// entire function.
  LiveRange getFullLiveRange() const {
    return LiveRange(Instructions.size(), true);
  }

  void print(raw_ostream &O);
};

static inline raw_ostream &operator<<(raw_ostream &OS, const BitVector &V) {
  OS << "{";
  ListSeparator LS;
  for (int Idx = V.find_first(); Idx >= 0; Idx = V.find_next(Idx))
    OS << LS << Idx;
  OS << "}";
  return OS;
}

inline raw_ostream &operator<<(raw_ostream &OS,
                               const StackLifetime::LiveRange &R) {
  return OS << R.Bits;
}

/// Printer pass for testing.
class StackLifetimePrinterPass
    : public PassInfoMixin<StackLifetimePrinterPass> {
  StackLifetime::LivenessType Type;
  raw_ostream &OS;

public:
  StackLifetimePrinterPass(raw_ostream &OS, StackLifetime::LivenessType Type)
      : Type(Type), OS(OS) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_STACKLIFETIME_H
