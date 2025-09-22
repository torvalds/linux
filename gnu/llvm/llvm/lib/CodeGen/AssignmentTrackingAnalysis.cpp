//===-- AssignmentTrackingAnalysis.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/AssignmentTrackingAnalysis.h"
#include "LiveDebugValues/LiveDebugValues.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/UniqueVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugProgramInstruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <assert.h>
#include <cstdint>
#include <optional>
#include <queue>
#include <sstream>
#include <unordered_map>

using namespace llvm;
#define DEBUG_TYPE "debug-ata"

STATISTIC(NumDefsScanned, "Number of dbg locs that get scanned for removal");
STATISTIC(NumDefsRemoved, "Number of dbg locs removed");
STATISTIC(NumWedgesScanned, "Number of dbg wedges scanned");
STATISTIC(NumWedgesChanged, "Number of dbg wedges changed");

static cl::opt<unsigned>
    MaxNumBlocks("debug-ata-max-blocks", cl::init(10000),
                 cl::desc("Maximum num basic blocks before debug info dropped"),
                 cl::Hidden);
/// Option for debugging the pass, determines if the memory location fragment
/// filling happens after generating the variable locations.
static cl::opt<bool> EnableMemLocFragFill("mem-loc-frag-fill", cl::init(true),
                                          cl::Hidden);
/// Print the results of the analysis. Respects -filter-print-funcs.
static cl::opt<bool> PrintResults("print-debug-ata", cl::init(false),
                                  cl::Hidden);

/// Coalesce adjacent dbg locs describing memory locations that have contiguous
/// fragments. This reduces the cost of LiveDebugValues which does SSA
/// construction for each explicitly stated variable fragment.
static cl::opt<cl::boolOrDefault>
    CoalesceAdjacentFragmentsOpt("debug-ata-coalesce-frags", cl::Hidden);

// Implicit conversions are disabled for enum class types, so unfortunately we
// need to create a DenseMapInfo wrapper around the specified underlying type.
template <> struct llvm::DenseMapInfo<VariableID> {
  using Wrapped = DenseMapInfo<unsigned>;
  static inline VariableID getEmptyKey() {
    return static_cast<VariableID>(Wrapped::getEmptyKey());
  }
  static inline VariableID getTombstoneKey() {
    return static_cast<VariableID>(Wrapped::getTombstoneKey());
  }
  static unsigned getHashValue(const VariableID &Val) {
    return Wrapped::getHashValue(static_cast<unsigned>(Val));
  }
  static bool isEqual(const VariableID &LHS, const VariableID &RHS) {
    return LHS == RHS;
  }
};

using VarLocInsertPt = PointerUnion<const Instruction *, const DbgRecord *>;

namespace std {
template <> struct hash<VarLocInsertPt> {
  using argument_type = VarLocInsertPt;
  using result_type = std::size_t;

  result_type operator()(const argument_type &Arg) const {
    return std::hash<void *>()(Arg.getOpaqueValue());
  }
};
} // namespace std

/// Helper class to build FunctionVarLocs, since that class isn't easy to
/// modify. TODO: There's not a great deal of value in the split, it could be
/// worth merging the two classes.
class FunctionVarLocsBuilder {
  friend FunctionVarLocs;
  UniqueVector<DebugVariable> Variables;
  // Use an unordered_map so we don't invalidate iterators after
  // insert/modifications.
  std::unordered_map<VarLocInsertPt, SmallVector<VarLocInfo>> VarLocsBeforeInst;

  SmallVector<VarLocInfo> SingleLocVars;

public:
  unsigned getNumVariables() const { return Variables.size(); }

  /// Find or insert \p V and return the ID.
  VariableID insertVariable(DebugVariable V) {
    return static_cast<VariableID>(Variables.insert(V));
  }

  /// Get a variable from its \p ID.
  const DebugVariable &getVariable(VariableID ID) const {
    return Variables[static_cast<unsigned>(ID)];
  }

  /// Return ptr to wedge of defs or nullptr if no defs come just before /p
  /// Before.
  const SmallVectorImpl<VarLocInfo> *getWedge(VarLocInsertPt Before) const {
    auto R = VarLocsBeforeInst.find(Before);
    if (R == VarLocsBeforeInst.end())
      return nullptr;
    return &R->second;
  }

  /// Replace the defs that come just before /p Before with /p Wedge.
  void setWedge(VarLocInsertPt Before, SmallVector<VarLocInfo> &&Wedge) {
    VarLocsBeforeInst[Before] = std::move(Wedge);
  }

  /// Add a def for a variable that is valid for its lifetime.
  void addSingleLocVar(DebugVariable Var, DIExpression *Expr, DebugLoc DL,
                       RawLocationWrapper R) {
    VarLocInfo VarLoc;
    VarLoc.VariableID = insertVariable(Var);
    VarLoc.Expr = Expr;
    VarLoc.DL = DL;
    VarLoc.Values = R;
    SingleLocVars.emplace_back(VarLoc);
  }

  /// Add a def to the wedge of defs just before /p Before.
  void addVarLoc(VarLocInsertPt Before, DebugVariable Var, DIExpression *Expr,
                 DebugLoc DL, RawLocationWrapper R) {
    VarLocInfo VarLoc;
    VarLoc.VariableID = insertVariable(Var);
    VarLoc.Expr = Expr;
    VarLoc.DL = DL;
    VarLoc.Values = R;
    VarLocsBeforeInst[Before].emplace_back(VarLoc);
  }
};

void FunctionVarLocs::print(raw_ostream &OS, const Function &Fn) const {
  // Print the variable table first. TODO: Sorting by variable could make the
  // output more stable?
  unsigned Counter = -1;
  OS << "=== Variables ===\n";
  for (const DebugVariable &V : Variables) {
    ++Counter;
    // Skip first entry because it is a dummy entry.
    if (Counter == 0) {
      continue;
    }
    OS << "[" << Counter << "] " << V.getVariable()->getName();
    if (auto F = V.getFragment())
      OS << " bits [" << F->OffsetInBits << ", "
         << F->OffsetInBits + F->SizeInBits << ")";
    if (const auto *IA = V.getInlinedAt())
      OS << " inlined-at " << *IA;
    OS << "\n";
  }

  auto PrintLoc = [&OS](const VarLocInfo &Loc) {
    OS << "DEF Var=[" << (unsigned)Loc.VariableID << "]"
       << " Expr=" << *Loc.Expr << " Values=(";
    for (auto *Op : Loc.Values.location_ops()) {
      errs() << Op->getName() << " ";
    }
    errs() << ")\n";
  };

  // Print the single location variables.
  OS << "=== Single location vars ===\n";
  for (auto It = single_locs_begin(), End = single_locs_end(); It != End;
       ++It) {
    PrintLoc(*It);
  }

  // Print the non-single-location defs in line with IR.
  OS << "=== In-line variable defs ===";
  for (const BasicBlock &BB : Fn) {
    OS << "\n" << BB.getName() << ":\n";
    for (const Instruction &I : BB) {
      for (auto It = locs_begin(&I), End = locs_end(&I); It != End; ++It) {
        PrintLoc(*It);
      }
      OS << I << "\n";
    }
  }
}

void FunctionVarLocs::init(FunctionVarLocsBuilder &Builder) {
  // Add the single-location variables first.
  for (const auto &VarLoc : Builder.SingleLocVars)
    VarLocRecords.emplace_back(VarLoc);
  // Mark the end of the section.
  SingleVarLocEnd = VarLocRecords.size();

  // Insert a contiguous block of VarLocInfos for each instruction, mapping it
  // to the start and end position in the vector with VarLocsBeforeInst. This
  // block includes VarLocs for any DbgVariableRecords attached to that
  // instruction.
  for (auto &P : Builder.VarLocsBeforeInst) {
    // Process VarLocs attached to a DbgRecord alongside their marker
    // Instruction.
    if (isa<const DbgRecord *>(P.first))
      continue;
    const Instruction *I = cast<const Instruction *>(P.first);
    unsigned BlockStart = VarLocRecords.size();
    // Any VarLocInfos attached to a DbgRecord should now be remapped to their
    // marker Instruction, in order of DbgRecord appearance and prior to any
    // VarLocInfos attached directly to that instruction.
    for (const DbgVariableRecord &DVR : filterDbgVars(I->getDbgRecordRange())) {
      // Even though DVR defines a variable location, VarLocsBeforeInst can
      // still be empty if that VarLoc was redundant.
      if (!Builder.VarLocsBeforeInst.count(&DVR))
        continue;
      for (const VarLocInfo &VarLoc : Builder.VarLocsBeforeInst[&DVR])
        VarLocRecords.emplace_back(VarLoc);
    }
    for (const VarLocInfo &VarLoc : P.second)
      VarLocRecords.emplace_back(VarLoc);
    unsigned BlockEnd = VarLocRecords.size();
    // Record the start and end indices.
    if (BlockEnd != BlockStart)
      VarLocsBeforeInst[I] = {BlockStart, BlockEnd};
  }

  // Copy the Variables vector from the builder's UniqueVector.
  assert(Variables.empty() && "Expect clear before init");
  // UniqueVectors IDs are one-based (which means the VarLocInfo VarID values
  // are one-based) so reserve an extra and insert a dummy.
  Variables.reserve(Builder.Variables.size() + 1);
  Variables.push_back(DebugVariable(nullptr, std::nullopt, nullptr));
  Variables.append(Builder.Variables.begin(), Builder.Variables.end());
}

void FunctionVarLocs::clear() {
  Variables.clear();
  VarLocRecords.clear();
  VarLocsBeforeInst.clear();
  SingleVarLocEnd = 0;
}

/// Walk backwards along constant GEPs and bitcasts to the base storage from \p
/// Start as far as possible. Prepend \Expression with the offset and append it
/// with a DW_OP_deref that haes been implicit until now. Returns the walked-to
/// value and modified expression.
static std::pair<Value *, DIExpression *>
walkToAllocaAndPrependOffsetDeref(const DataLayout &DL, Value *Start,
                                  DIExpression *Expression) {
  APInt OffsetInBytes(DL.getTypeSizeInBits(Start->getType()), false);
  Value *End =
      Start->stripAndAccumulateInBoundsConstantOffsets(DL, OffsetInBytes);
  SmallVector<uint64_t, 3> Ops;
  if (OffsetInBytes.getBoolValue()) {
    Ops = {dwarf::DW_OP_plus_uconst, OffsetInBytes.getZExtValue()};
    Expression = DIExpression::prependOpcodes(
        Expression, Ops, /*StackValue=*/false, /*EntryValue=*/false);
  }
  Expression = DIExpression::append(Expression, {dwarf::DW_OP_deref});
  return {End, Expression};
}

/// Extract the offset used in \p DIExpr. Returns std::nullopt if the expression
/// doesn't explicitly describe a memory location with DW_OP_deref or if the
/// expression is too complex to interpret.
static std::optional<int64_t>
getDerefOffsetInBytes(const DIExpression *DIExpr) {
  int64_t Offset = 0;
  const unsigned NumElements = DIExpr->getNumElements();
  const auto Elements = DIExpr->getElements();
  unsigned ExpectedDerefIdx = 0;
  // Extract the offset.
  if (NumElements > 2 && Elements[0] == dwarf::DW_OP_plus_uconst) {
    Offset = Elements[1];
    ExpectedDerefIdx = 2;
  } else if (NumElements > 3 && Elements[0] == dwarf::DW_OP_constu) {
    ExpectedDerefIdx = 3;
    if (Elements[2] == dwarf::DW_OP_plus)
      Offset = Elements[1];
    else if (Elements[2] == dwarf::DW_OP_minus)
      Offset = -Elements[1];
    else
      return std::nullopt;
  }

  // If that's all there is it means there's no deref.
  if (ExpectedDerefIdx >= NumElements)
    return std::nullopt;

  // Check the next element is DW_OP_deref - otherwise this is too complex or
  // isn't a deref expression.
  if (Elements[ExpectedDerefIdx] != dwarf::DW_OP_deref)
    return std::nullopt;

  // Check the final operation is either the DW_OP_deref or is a fragment.
  if (NumElements == ExpectedDerefIdx + 1)
    return Offset; // Ends with deref.
  unsigned ExpectedFragFirstIdx = ExpectedDerefIdx + 1;
  unsigned ExpectedFragFinalIdx = ExpectedFragFirstIdx + 2;
  if (NumElements == ExpectedFragFinalIdx + 1 &&
      Elements[ExpectedFragFirstIdx] == dwarf::DW_OP_LLVM_fragment)
    return Offset; // Ends with deref + fragment.

  // Don't bother trying to interpret anything more complex.
  return std::nullopt;
}

/// A whole (unfragmented) source variable.
using DebugAggregate = std::pair<const DILocalVariable *, const DILocation *>;
static DebugAggregate getAggregate(const DbgVariableIntrinsic *DII) {
  return DebugAggregate(DII->getVariable(), DII->getDebugLoc().getInlinedAt());
}
static DebugAggregate getAggregate(const DebugVariable &Var) {
  return DebugAggregate(Var.getVariable(), Var.getInlinedAt());
}

static bool shouldCoalesceFragments(Function &F) {
  // Enabling fragment coalescing reduces compiler run time when instruction
  // referencing is enabled. However, it may cause LiveDebugVariables to create
  // incorrect locations. Since instruction-referencing mode effectively
  // bypasses LiveDebugVariables we only enable coalescing if the cl::opt flag
  // has not been explicitly set and instruction-referencing is turned on.
  switch (CoalesceAdjacentFragmentsOpt) {
  case cl::boolOrDefault::BOU_UNSET:
    return debuginfoShouldUseDebugInstrRef(
        Triple(F.getParent()->getTargetTriple()));
  case cl::boolOrDefault::BOU_TRUE:
    return true;
  case cl::boolOrDefault::BOU_FALSE:
    return false;
  }
  llvm_unreachable("Unknown boolOrDefault value");
}

namespace {
/// In dwarf emission, the following sequence
///    1. dbg.value ... Fragment(0, 64)
///    2. dbg.value ... Fragment(0, 32)
/// effectively sets Fragment(32, 32) to undef (each def sets all bits not in
/// the intersection of the fragments to having "no location"). This makes
/// sense for implicit location values because splitting the computed values
/// could be troublesome, and is probably quite uncommon.  When we convert
/// dbg.assigns to dbg.value+deref this kind of thing is common, and describing
/// a location (memory) rather than a value means we don't need to worry about
/// splitting any values, so we try to recover the rest of the fragment
/// location here.
/// This class performs a(nother) dataflow analysis over the function, adding
/// variable locations so that any bits of a variable with a memory location
/// have that location explicitly reinstated at each subsequent variable
/// location definition that that doesn't overwrite those bits. i.e. after a
/// variable location def, insert new defs for the memory location with
/// fragments for the difference of "all bits currently in memory" and "the
/// fragment of the second def".
class MemLocFragmentFill {
  Function &Fn;
  FunctionVarLocsBuilder *FnVarLocs;
  const DenseSet<DebugAggregate> *VarsWithStackSlot;
  bool CoalesceAdjacentFragments;

  // 0 = no memory location.
  using BaseAddress = unsigned;
  using OffsetInBitsTy = unsigned;
  using FragTraits = IntervalMapHalfOpenInfo<OffsetInBitsTy>;
  using FragsInMemMap = IntervalMap<
      OffsetInBitsTy, BaseAddress,
      IntervalMapImpl::NodeSizer<OffsetInBitsTy, BaseAddress>::LeafSize,
      FragTraits>;
  FragsInMemMap::Allocator IntervalMapAlloc;
  using VarFragMap = DenseMap<unsigned, FragsInMemMap>;

  /// IDs for memory location base addresses in maps. Use 0 to indicate that
  /// there's no memory location.
  UniqueVector<RawLocationWrapper> Bases;
  UniqueVector<DebugAggregate> Aggregates;
  DenseMap<const BasicBlock *, VarFragMap> LiveIn;
  DenseMap<const BasicBlock *, VarFragMap> LiveOut;

  struct FragMemLoc {
    unsigned Var;
    unsigned Base;
    unsigned OffsetInBits;
    unsigned SizeInBits;
    DebugLoc DL;
  };
  using InsertMap = MapVector<VarLocInsertPt, SmallVector<FragMemLoc>>;

  /// BBInsertBeforeMap holds a description for the set of location defs to be
  /// inserted after the analysis is complete. It is updated during the dataflow
  /// and the entry for a block is CLEARED each time it is (re-)visited. After
  /// the dataflow is complete, each block entry will contain the set of defs
  /// calculated during the final (fixed-point) iteration.
  DenseMap<const BasicBlock *, InsertMap> BBInsertBeforeMap;

  static bool intervalMapsAreEqual(const FragsInMemMap &A,
                                   const FragsInMemMap &B) {
    auto AIt = A.begin(), AEnd = A.end();
    auto BIt = B.begin(), BEnd = B.end();
    for (; AIt != AEnd; ++AIt, ++BIt) {
      if (BIt == BEnd)
        return false; // B has fewer elements than A.
      if (AIt.start() != BIt.start() || AIt.stop() != BIt.stop())
        return false; // Interval is different.
      if (*AIt != *BIt)
        return false; // Value at interval is different.
    }
    // AIt == AEnd. Check BIt is also now at end.
    return BIt == BEnd;
  }

  static bool varFragMapsAreEqual(const VarFragMap &A, const VarFragMap &B) {
    if (A.size() != B.size())
      return false;
    for (const auto &APair : A) {
      auto BIt = B.find(APair.first);
      if (BIt == B.end())
        return false;
      if (!intervalMapsAreEqual(APair.second, BIt->second))
        return false;
    }
    return true;
  }

  /// Return a string for the value that \p BaseID represents.
  std::string toString(unsigned BaseID) {
    if (BaseID)
      return Bases[BaseID].getVariableLocationOp(0)->getName().str();
    else
      return "None";
  }

  /// Format string describing an FragsInMemMap (IntervalMap) interval.
  std::string toString(FragsInMemMap::const_iterator It, bool Newline = true) {
    std::string String;
    std::stringstream S(String);
    if (It.valid()) {
      S << "[" << It.start() << ", " << It.stop()
        << "): " << toString(It.value());
    } else {
      S << "invalid iterator (end)";
    }
    if (Newline)
      S << "\n";
    return S.str();
  };

  FragsInMemMap meetFragments(const FragsInMemMap &A, const FragsInMemMap &B) {
    FragsInMemMap Result(IntervalMapAlloc);
    for (auto AIt = A.begin(), AEnd = A.end(); AIt != AEnd; ++AIt) {
      LLVM_DEBUG(dbgs() << "a " << toString(AIt));
      // This is basically copied from process() and inverted (process is
      // performing something like a union whereas this is more of an
      // intersect).

      // There's no work to do if interval `a` overlaps no fragments in map `B`.
      if (!B.overlaps(AIt.start(), AIt.stop()))
        continue;

      // Does StartBit intersect an existing fragment?
      auto FirstOverlap = B.find(AIt.start());
      assert(FirstOverlap != B.end());
      bool IntersectStart = FirstOverlap.start() < AIt.start();
      LLVM_DEBUG(dbgs() << "- FirstOverlap " << toString(FirstOverlap, false)
                        << ", IntersectStart: " << IntersectStart << "\n");

      // Does EndBit intersect an existing fragment?
      auto LastOverlap = B.find(AIt.stop());
      bool IntersectEnd =
          LastOverlap != B.end() && LastOverlap.start() < AIt.stop();
      LLVM_DEBUG(dbgs() << "- LastOverlap " << toString(LastOverlap, false)
                        << ", IntersectEnd: " << IntersectEnd << "\n");

      // Check if both ends of `a` intersect the same interval `b`.
      if (IntersectStart && IntersectEnd && FirstOverlap == LastOverlap) {
        // Insert `a` (`a` is contained in `b`) if the values match.
        // [ a ]
        // [ - b - ]
        // -
        // [ r ]
        LLVM_DEBUG(dbgs() << "- a is contained within "
                          << toString(FirstOverlap));
        if (*AIt && *AIt == *FirstOverlap)
          Result.insert(AIt.start(), AIt.stop(), *AIt);
      } else {
        // There's an overlap but `a` is not fully contained within
        // `b`. Shorten any end-point intersections.
        //     [ - a - ]
        // [ - b - ]
        // -
        //     [ r ]
        auto Next = FirstOverlap;
        if (IntersectStart) {
          LLVM_DEBUG(dbgs() << "- insert intersection of a and "
                            << toString(FirstOverlap));
          if (*AIt && *AIt == *FirstOverlap)
            Result.insert(AIt.start(), FirstOverlap.stop(), *AIt);
          ++Next;
        }
        // [ - a - ]
        //     [ - b - ]
        // -
        //     [ r ]
        if (IntersectEnd) {
          LLVM_DEBUG(dbgs() << "- insert intersection of a and "
                            << toString(LastOverlap));
          if (*AIt && *AIt == *LastOverlap)
            Result.insert(LastOverlap.start(), AIt.stop(), *AIt);
        }

        // Insert all intervals in map `B` that are contained within interval
        // `a` where the values match.
        // [ -  - a -  - ]
        // [ b1 ]   [ b2 ]
        // -
        // [ r1 ]   [ r2 ]
        while (Next != B.end() && Next.start() < AIt.stop() &&
               Next.stop() <= AIt.stop()) {
          LLVM_DEBUG(dbgs()
                     << "- insert intersection of a and " << toString(Next));
          if (*AIt && *AIt == *Next)
            Result.insert(Next.start(), Next.stop(), *Next);
          ++Next;
        }
      }
    }
    return Result;
  }

  /// Meet \p A and \p B, storing the result in \p A.
  void meetVars(VarFragMap &A, const VarFragMap &B) {
    // Meet A and B.
    //
    // Result = meet(a, b) for a in A, b in B where Var(a) == Var(b)
    for (auto It = A.begin(), End = A.end(); It != End; ++It) {
      unsigned AVar = It->first;
      FragsInMemMap &AFrags = It->second;
      auto BIt = B.find(AVar);
      if (BIt == B.end()) {
        A.erase(It);
        continue; // Var has no bits defined in B.
      }
      LLVM_DEBUG(dbgs() << "meet fragment maps for "
                        << Aggregates[AVar].first->getName() << "\n");
      AFrags = meetFragments(AFrags, BIt->second);
    }
  }

  bool meet(const BasicBlock &BB,
            const SmallPtrSet<BasicBlock *, 16> &Visited) {
    LLVM_DEBUG(dbgs() << "meet block info from preds of " << BB.getName()
                      << "\n");

    VarFragMap BBLiveIn;
    bool FirstMeet = true;
    // LiveIn locs for BB is the meet of the already-processed preds' LiveOut
    // locs.
    for (const BasicBlock *Pred : predecessors(&BB)) {
      // Ignore preds that haven't been processed yet. This is essentially the
      // same as initialising all variables to implicit top value (⊤) which is
      // the identity value for the meet operation.
      if (!Visited.count(Pred))
        continue;

      auto PredLiveOut = LiveOut.find(Pred);
      assert(PredLiveOut != LiveOut.end());

      if (FirstMeet) {
        LLVM_DEBUG(dbgs() << "BBLiveIn = " << Pred->getName() << "\n");
        BBLiveIn = PredLiveOut->second;
        FirstMeet = false;
      } else {
        LLVM_DEBUG(dbgs() << "BBLiveIn = meet BBLiveIn, " << Pred->getName()
                          << "\n");
        meetVars(BBLiveIn, PredLiveOut->second);
      }

      // An empty set is ⊥ for the intersect-like meet operation. If we've
      // already got ⊥ there's no need to run the code - we know the result is
      // ⊥ since `meet(a, ⊥) = ⊥`.
      if (BBLiveIn.size() == 0)
        break;
    }

    auto CurrentLiveInEntry = LiveIn.find(&BB);
    // If there's no LiveIn entry for the block yet, add it.
    if (CurrentLiveInEntry == LiveIn.end()) {
      LLVM_DEBUG(dbgs() << "change=true (first) on meet on " << BB.getName()
                        << "\n");
      LiveIn[&BB] = std::move(BBLiveIn);
      return /*Changed=*/true;
    }

    // If the LiveIn set has changed (expensive check) update it and return
    // true.
    if (!varFragMapsAreEqual(BBLiveIn, CurrentLiveInEntry->second)) {
      LLVM_DEBUG(dbgs() << "change=true on meet on " << BB.getName() << "\n");
      CurrentLiveInEntry->second = std::move(BBLiveIn);
      return /*Changed=*/true;
    }

    LLVM_DEBUG(dbgs() << "change=false on meet on " << BB.getName() << "\n");
    return /*Changed=*/false;
  }

  void insertMemLoc(BasicBlock &BB, VarLocInsertPt Before, unsigned Var,
                    unsigned StartBit, unsigned EndBit, unsigned Base,
                    DebugLoc DL) {
    assert(StartBit < EndBit && "Cannot create fragment of size <= 0");
    if (!Base)
      return;
    FragMemLoc Loc;
    Loc.Var = Var;
    Loc.OffsetInBits = StartBit;
    Loc.SizeInBits = EndBit - StartBit;
    assert(Base && "Expected a non-zero ID for Base address");
    Loc.Base = Base;
    Loc.DL = DL;
    BBInsertBeforeMap[&BB][Before].push_back(Loc);
    LLVM_DEBUG(dbgs() << "Add mem def for " << Aggregates[Var].first->getName()
                      << " bits [" << StartBit << ", " << EndBit << ")\n");
  }

  /// Inserts a new dbg def if the interval found when looking up \p StartBit
  /// in \p FragMap starts before \p StartBit or ends after \p EndBit (which
  /// indicates - assuming StartBit->EndBit has just been inserted - that the
  /// slice has been coalesced in the map).
  void coalesceFragments(BasicBlock &BB, VarLocInsertPt Before, unsigned Var,
                         unsigned StartBit, unsigned EndBit, unsigned Base,
                         DebugLoc DL, const FragsInMemMap &FragMap) {
    if (!CoalesceAdjacentFragments)
      return;
    // We've inserted the location into the map. The map will have coalesced
    // adjacent intervals (variable fragments) that describe the same memory
    // location. Use this knowledge to insert a debug location that describes
    // that coalesced fragment. This may eclipse other locs we've just
    // inserted. This is okay as redundant locs will be cleaned up later.
    auto CoalescedFrag = FragMap.find(StartBit);
    // Bail if no coalescing has taken place.
    if (CoalescedFrag.start() == StartBit && CoalescedFrag.stop() == EndBit)
      return;

    LLVM_DEBUG(dbgs() << "- Insert loc for bits " << CoalescedFrag.start()
                      << " to " << CoalescedFrag.stop() << "\n");
    insertMemLoc(BB, Before, Var, CoalescedFrag.start(), CoalescedFrag.stop(),
                 Base, DL);
  }

  void addDef(const VarLocInfo &VarLoc, VarLocInsertPt Before, BasicBlock &BB,
              VarFragMap &LiveSet) {
    DebugVariable DbgVar = FnVarLocs->getVariable(VarLoc.VariableID);
    if (skipVariable(DbgVar.getVariable()))
      return;
    // Don't bother doing anything for this variables if we know it's fully
    // promoted. We're only interested in variables that (sometimes) live on
    // the stack here.
    if (!VarsWithStackSlot->count(getAggregate(DbgVar)))
      return;
    unsigned Var = Aggregates.insert(
        DebugAggregate(DbgVar.getVariable(), VarLoc.DL.getInlinedAt()));

    // [StartBit: EndBit) are the bits affected by this def.
    const DIExpression *DIExpr = VarLoc.Expr;
    unsigned StartBit;
    unsigned EndBit;
    if (auto Frag = DIExpr->getFragmentInfo()) {
      StartBit = Frag->OffsetInBits;
      EndBit = StartBit + Frag->SizeInBits;
    } else {
      assert(static_cast<bool>(DbgVar.getVariable()->getSizeInBits()));
      StartBit = 0;
      EndBit = *DbgVar.getVariable()->getSizeInBits();
    }

    // We will only fill fragments for simple memory-describing dbg.value
    // intrinsics. If the fragment offset is the same as the offset from the
    // base pointer, do The Thing, otherwise fall back to normal dbg.value
    // behaviour. AssignmentTrackingLowering has generated DIExpressions
    // written in terms of the base pointer.
    // TODO: Remove this condition since the fragment offset doesn't always
    // equal the offset from base pointer (e.g. for a SROA-split variable).
    const auto DerefOffsetInBytes = getDerefOffsetInBytes(DIExpr);
    const unsigned Base =
        DerefOffsetInBytes && *DerefOffsetInBytes * 8 == StartBit
            ? Bases.insert(VarLoc.Values)
            : 0;
    LLVM_DEBUG(dbgs() << "DEF " << DbgVar.getVariable()->getName() << " ["
                      << StartBit << ", " << EndBit << "): " << toString(Base)
                      << "\n");

    // First of all, any locs that use mem that are disrupted need reinstating.
    // Unfortunately, IntervalMap doesn't let us insert intervals that overlap
    // with existing intervals so this code involves a lot of fiddling around
    // with intervals to do that manually.
    auto FragIt = LiveSet.find(Var);

    // Check if the variable does not exist in the map.
    if (FragIt == LiveSet.end()) {
      // Add this variable to the BB map.
      auto P = LiveSet.try_emplace(Var, FragsInMemMap(IntervalMapAlloc));
      assert(P.second && "Var already in map?");
      // Add the interval to the fragment map.
      P.first->second.insert(StartBit, EndBit, Base);
      return;
    }
    // The variable has an entry in the map.

    FragsInMemMap &FragMap = FragIt->second;
    // First check the easy case: the new fragment `f` doesn't overlap with any
    // intervals.
    if (!FragMap.overlaps(StartBit, EndBit)) {
      LLVM_DEBUG(dbgs() << "- No overlaps\n");
      FragMap.insert(StartBit, EndBit, Base);
      coalesceFragments(BB, Before, Var, StartBit, EndBit, Base, VarLoc.DL,
                        FragMap);
      return;
    }
    // There is at least one overlap.

    // Does StartBit intersect an existing fragment?
    auto FirstOverlap = FragMap.find(StartBit);
    assert(FirstOverlap != FragMap.end());
    bool IntersectStart = FirstOverlap.start() < StartBit;

    // Does EndBit intersect an existing fragment?
    auto LastOverlap = FragMap.find(EndBit);
    bool IntersectEnd = LastOverlap.valid() && LastOverlap.start() < EndBit;

    // Check if both ends of `f` intersect the same interval `i`.
    if (IntersectStart && IntersectEnd && FirstOverlap == LastOverlap) {
      LLVM_DEBUG(dbgs() << "- Intersect single interval @ both ends\n");
      // Shorten `i` so that there's space to insert `f`.
      //      [ f ]
      // [  -   i   -  ]
      // +
      // [ i ][ f ][ i ]

      // Save values for use after inserting a new interval.
      auto EndBitOfOverlap = FirstOverlap.stop();
      unsigned OverlapValue = FirstOverlap.value();

      // Shorten the overlapping interval.
      FirstOverlap.setStop(StartBit);
      insertMemLoc(BB, Before, Var, FirstOverlap.start(), StartBit,
                   OverlapValue, VarLoc.DL);

      // Insert a new interval to represent the end part.
      FragMap.insert(EndBit, EndBitOfOverlap, OverlapValue);
      insertMemLoc(BB, Before, Var, EndBit, EndBitOfOverlap, OverlapValue,
                   VarLoc.DL);

      // Insert the new (middle) fragment now there is space.
      FragMap.insert(StartBit, EndBit, Base);
    } else {
      // There's an overlap but `f` may not be fully contained within
      // `i`. Shorten any end-point intersections so that we can then
      // insert `f`.
      //      [ - f - ]
      // [ - i - ]
      // |   |
      // [ i ]
      // Shorten any end-point intersections.
      if (IntersectStart) {
        LLVM_DEBUG(dbgs() << "- Intersect interval at start\n");
        // Split off at the intersection.
        FirstOverlap.setStop(StartBit);
        insertMemLoc(BB, Before, Var, FirstOverlap.start(), StartBit,
                     *FirstOverlap, VarLoc.DL);
      }
      // [ - f - ]
      //      [ - i - ]
      //          |   |
      //          [ i ]
      if (IntersectEnd) {
        LLVM_DEBUG(dbgs() << "- Intersect interval at end\n");
        // Split off at the intersection.
        LastOverlap.setStart(EndBit);
        insertMemLoc(BB, Before, Var, EndBit, LastOverlap.stop(), *LastOverlap,
                     VarLoc.DL);
      }

      LLVM_DEBUG(dbgs() << "- Erase intervals contained within\n");
      // FirstOverlap and LastOverlap have been shortened such that they're
      // no longer overlapping with [StartBit, EndBit). Delete any overlaps
      // that remain (these will be fully contained within `f`).
      // [ - f - ]       }
      //      [ - i - ]  } Intersection shortening that has happened above.
      //          |   |  }
      //          [ i ]  }
      // -----------------
      // [i2 ]           } Intervals fully contained within `f` get erased.
      // -----------------
      // [ - f - ][ i ]  } Completed insertion.
      auto It = FirstOverlap;
      if (IntersectStart)
        ++It; // IntersectStart: first overlap has been shortened.
      while (It.valid() && It.start() >= StartBit && It.stop() <= EndBit) {
        LLVM_DEBUG(dbgs() << "- Erase " << toString(It));
        It.erase(); // This increments It after removing the interval.
      }
      // We've dealt with all the overlaps now!
      assert(!FragMap.overlaps(StartBit, EndBit));
      LLVM_DEBUG(dbgs() << "- Insert DEF into now-empty space\n");
      FragMap.insert(StartBit, EndBit, Base);
    }

    coalesceFragments(BB, Before, Var, StartBit, EndBit, Base, VarLoc.DL,
                      FragMap);
  }

  bool skipVariable(const DILocalVariable *V) { return !V->getSizeInBits(); }

  void process(BasicBlock &BB, VarFragMap &LiveSet) {
    BBInsertBeforeMap[&BB].clear();
    for (auto &I : BB) {
      for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
        if (const auto *Locs = FnVarLocs->getWedge(&DVR)) {
          for (const VarLocInfo &Loc : *Locs) {
            addDef(Loc, &DVR, *I.getParent(), LiveSet);
          }
        }
      }
      if (const auto *Locs = FnVarLocs->getWedge(&I)) {
        for (const VarLocInfo &Loc : *Locs) {
          addDef(Loc, &I, *I.getParent(), LiveSet);
        }
      }
    }
  }

public:
  MemLocFragmentFill(Function &Fn,
                     const DenseSet<DebugAggregate> *VarsWithStackSlot,
                     bool CoalesceAdjacentFragments)
      : Fn(Fn), VarsWithStackSlot(VarsWithStackSlot),
        CoalesceAdjacentFragments(CoalesceAdjacentFragments) {}

  /// Add variable locations to \p FnVarLocs so that any bits of a variable
  /// with a memory location have that location explicitly reinstated at each
  /// subsequent variable location definition that that doesn't overwrite those
  /// bits. i.e. after a variable location def, insert new defs for the memory
  /// location with fragments for the difference of "all bits currently in
  /// memory" and "the fragment of the second def". e.g.
  ///
  ///     Before:
  ///
  ///     var x bits 0 to 63:  value in memory
  ///     more instructions
  ///     var x bits 0 to 31:  value is %0
  ///
  ///     After:
  ///
  ///     var x bits 0 to 63:  value in memory
  ///     more instructions
  ///     var x bits 0 to 31:  value is %0
  ///     var x bits 32 to 61: value in memory ; <-- new loc def
  ///
  void run(FunctionVarLocsBuilder *FnVarLocs) {
    if (!EnableMemLocFragFill)
      return;

    this->FnVarLocs = FnVarLocs;

    // Prepare for traversal.
    //
    ReversePostOrderTraversal<Function *> RPOT(&Fn);
    std::priority_queue<unsigned int, std::vector<unsigned int>,
                        std::greater<unsigned int>>
        Worklist;
    std::priority_queue<unsigned int, std::vector<unsigned int>,
                        std::greater<unsigned int>>
        Pending;
    DenseMap<unsigned int, BasicBlock *> OrderToBB;
    DenseMap<BasicBlock *, unsigned int> BBToOrder;
    { // Init OrderToBB and BBToOrder.
      unsigned int RPONumber = 0;
      for (BasicBlock *BB : RPOT) {
        OrderToBB[RPONumber] = BB;
        BBToOrder[BB] = RPONumber;
        Worklist.push(RPONumber);
        ++RPONumber;
      }
      LiveIn.init(RPONumber);
      LiveOut.init(RPONumber);
    }

    // Perform the traversal.
    //
    // This is a standard "intersect of predecessor outs" dataflow problem. To
    // solve it, we perform meet() and process() using the two worklist method
    // until the LiveIn data for each block becomes unchanging.
    //
    // This dataflow is essentially working on maps of sets and at each meet we
    // intersect the maps and the mapped sets. So, initialized live-in maps
    // monotonically decrease in value throughout the dataflow.
    SmallPtrSet<BasicBlock *, 16> Visited;
    while (!Worklist.empty() || !Pending.empty()) {
      // We track what is on the pending worklist to avoid inserting the same
      // thing twice.  We could avoid this with a custom priority queue, but
      // this is probably not worth it.
      SmallPtrSet<BasicBlock *, 16> OnPending;
      LLVM_DEBUG(dbgs() << "Processing Worklist\n");
      while (!Worklist.empty()) {
        BasicBlock *BB = OrderToBB[Worklist.top()];
        LLVM_DEBUG(dbgs() << "\nPop BB " << BB->getName() << "\n");
        Worklist.pop();
        bool InChanged = meet(*BB, Visited);
        // Always consider LiveIn changed on the first visit.
        InChanged |= Visited.insert(BB).second;
        if (InChanged) {
          LLVM_DEBUG(dbgs()
                     << BB->getName() << " has new InLocs, process it\n");
          //  Mutate a copy of LiveIn while processing BB. Once we've processed
          //  the terminator LiveSet is the LiveOut set for BB.
          //  This is an expensive copy!
          VarFragMap LiveSet = LiveIn[BB];

          // Process the instructions in the block.
          process(*BB, LiveSet);

          // Relatively expensive check: has anything changed in LiveOut for BB?
          if (!varFragMapsAreEqual(LiveOut[BB], LiveSet)) {
            LLVM_DEBUG(dbgs() << BB->getName()
                              << " has new OutLocs, add succs to worklist: [ ");
            LiveOut[BB] = std::move(LiveSet);
            for (BasicBlock *Succ : successors(BB)) {
              if (OnPending.insert(Succ).second) {
                LLVM_DEBUG(dbgs() << Succ->getName() << " ");
                Pending.push(BBToOrder[Succ]);
              }
            }
            LLVM_DEBUG(dbgs() << "]\n");
          }
        }
      }
      Worklist.swap(Pending);
      // At this point, pending must be empty, since it was just the empty
      // worklist
      assert(Pending.empty() && "Pending should be empty");
    }

    // Insert new location defs.
    for (auto &Pair : BBInsertBeforeMap) {
      InsertMap &Map = Pair.second;
      for (auto &Pair : Map) {
        auto InsertBefore = Pair.first;
        assert(InsertBefore && "should never be null");
        auto FragMemLocs = Pair.second;
        auto &Ctx = Fn.getContext();

        for (auto &FragMemLoc : FragMemLocs) {
          DIExpression *Expr = DIExpression::get(Ctx, std::nullopt);
          if (FragMemLoc.SizeInBits !=
              *Aggregates[FragMemLoc.Var].first->getSizeInBits())
            Expr = *DIExpression::createFragmentExpression(
                Expr, FragMemLoc.OffsetInBits, FragMemLoc.SizeInBits);
          Expr = DIExpression::prepend(Expr, DIExpression::DerefAfter,
                                       FragMemLoc.OffsetInBits / 8);
          DebugVariable Var(Aggregates[FragMemLoc.Var].first, Expr,
                            FragMemLoc.DL.getInlinedAt());
          FnVarLocs->addVarLoc(InsertBefore, Var, Expr, FragMemLoc.DL,
                               Bases[FragMemLoc.Base]);
        }
      }
    }
  }
};

/// AssignmentTrackingLowering encapsulates a dataflow analysis over a function
/// that interprets assignment tracking debug info metadata and stores in IR to
/// create a map of variable locations.
class AssignmentTrackingLowering {
public:
  /// The kind of location in use for a variable, where Mem is the stack home,
  /// Val is an SSA value or const, and None means that there is not one single
  /// kind (either because there are multiple or because there is none; it may
  /// prove useful to split this into two values in the future).
  ///
  /// LocKind is a join-semilattice with the partial order:
  /// None > Mem, Val
  ///
  /// i.e.
  /// join(Mem, Mem)   = Mem
  /// join(Val, Val)   = Val
  /// join(Mem, Val)   = None
  /// join(None, Mem)  = None
  /// join(None, Val)  = None
  /// join(None, None) = None
  ///
  /// Note: the order is not `None > Val > Mem` because we're using DIAssignID
  /// to name assignments and are not tracking the actual stored values.
  /// Therefore currently there's no way to ensure that Mem values and Val
  /// values are the same. This could be a future extension, though it's not
  /// clear that many additional locations would be recovered that way in
  /// practice as the likelihood of this sitation arising naturally seems
  /// incredibly low.
  enum class LocKind { Mem, Val, None };

  /// An abstraction of the assignment of a value to a variable or memory
  /// location.
  ///
  /// An Assignment is Known or NoneOrPhi. A Known Assignment means we have a
  /// DIAssignID ptr that represents it. NoneOrPhi means that we don't (or
  /// can't) know the ID of the last assignment that took place.
  ///
  /// The Status of the Assignment (Known or NoneOrPhi) is another
  /// join-semilattice. The partial order is:
  /// NoneOrPhi > Known {id_0, id_1, ...id_N}
  ///
  /// i.e. for all values x and y where x != y:
  /// join(x, x) = x
  /// join(x, y) = NoneOrPhi
  using AssignRecord = PointerUnion<DbgAssignIntrinsic *, DbgVariableRecord *>;
  struct Assignment {
    enum S { Known, NoneOrPhi } Status;
    /// ID of the assignment. nullptr if Status is not Known.
    DIAssignID *ID;
    /// The dbg.assign that marks this dbg-def. Mem-defs don't use this field.
    /// May be nullptr.
    AssignRecord Source;

    bool isSameSourceAssignment(const Assignment &Other) const {
      // Don't include Source in the equality check. Assignments are
      // defined by their ID, not debug intrinsic(s).
      return std::tie(Status, ID) == std::tie(Other.Status, Other.ID);
    }
    void dump(raw_ostream &OS) {
      static const char *LUT[] = {"Known", "NoneOrPhi"};
      OS << LUT[Status] << "(id=";
      if (ID)
        OS << ID;
      else
        OS << "null";
      OS << ", s=";
      if (Source.isNull())
        OS << "null";
      else if (isa<DbgAssignIntrinsic *>(Source))
        OS << Source.get<DbgAssignIntrinsic *>();
      else
        OS << Source.get<DbgVariableRecord *>();
      OS << ")";
    }

    static Assignment make(DIAssignID *ID, DbgAssignIntrinsic *Source) {
      return Assignment(Known, ID, Source);
    }
    static Assignment make(DIAssignID *ID, DbgVariableRecord *Source) {
      assert(Source->isDbgAssign() &&
             "Cannot make an assignment from a non-assign DbgVariableRecord");
      return Assignment(Known, ID, Source);
    }
    static Assignment make(DIAssignID *ID, AssignRecord Source) {
      return Assignment(Known, ID, Source);
    }
    static Assignment makeFromMemDef(DIAssignID *ID) {
      return Assignment(Known, ID);
    }
    static Assignment makeNoneOrPhi() { return Assignment(NoneOrPhi, nullptr); }
    // Again, need a Top value?
    Assignment() : Status(NoneOrPhi), ID(nullptr) {} // Can we delete this?
    Assignment(S Status, DIAssignID *ID) : Status(Status), ID(ID) {
      // If the Status is Known then we expect there to be an assignment ID.
      assert(Status == NoneOrPhi || ID);
    }
    Assignment(S Status, DIAssignID *ID, DbgAssignIntrinsic *Source)
        : Status(Status), ID(ID), Source(Source) {
      // If the Status is Known then we expect there to be an assignment ID.
      assert(Status == NoneOrPhi || ID);
    }
    Assignment(S Status, DIAssignID *ID, DbgVariableRecord *Source)
        : Status(Status), ID(ID), Source(Source) {
      // If the Status is Known then we expect there to be an assignment ID.
      assert(Status == NoneOrPhi || ID);
    }
    Assignment(S Status, DIAssignID *ID, AssignRecord Source)
        : Status(Status), ID(ID), Source(Source) {
      // If the Status is Known then we expect there to be an assignment ID.
      assert(Status == NoneOrPhi || ID);
    }
  };

  using AssignmentMap = SmallVector<Assignment>;
  using LocMap = SmallVector<LocKind>;
  using OverlapMap = DenseMap<VariableID, SmallVector<VariableID>>;
  using UntaggedStoreAssignmentMap =
      DenseMap<const Instruction *,
               SmallVector<std::pair<VariableID, at::AssignmentInfo>>>;

private:
  /// The highest numbered VariableID for partially promoted variables plus 1,
  /// the values for which start at 1.
  unsigned TrackedVariablesVectorSize = 0;
  /// Map a variable to the set of variables that it fully contains.
  OverlapMap VarContains;
  /// Map untagged stores to the variable fragments they assign to. Used by
  /// processUntaggedInstruction.
  UntaggedStoreAssignmentMap UntaggedStoreVars;

  // Machinery to defer inserting dbg.values.
  using InstInsertMap = MapVector<VarLocInsertPt, SmallVector<VarLocInfo>>;
  InstInsertMap InsertBeforeMap;
  /// Clear the location definitions currently cached for insertion after /p
  /// After.
  void resetInsertionPoint(Instruction &After);
  void resetInsertionPoint(DbgVariableRecord &After);

  // emitDbgValue can be called with:
  //   Source=[AssignRecord|DbgValueInst*|DbgAssignIntrinsic*|DbgVariableRecord*]
  // Since AssignRecord can be cast to one of the latter two types, and all
  // other types have a shared interface, we use a template to handle the latter
  // three types, and an explicit overload for AssignRecord that forwards to
  // the template version with the right type.
  void emitDbgValue(LocKind Kind, AssignRecord Source, VarLocInsertPt After);
  template <typename T>
  void emitDbgValue(LocKind Kind, const T Source, VarLocInsertPt After);

  static bool mapsAreEqual(const BitVector &Mask, const AssignmentMap &A,
                           const AssignmentMap &B) {
    return llvm::all_of(Mask.set_bits(), [&](unsigned VarID) {
      return A[VarID].isSameSourceAssignment(B[VarID]);
    });
  }

  /// Represents the stack and debug assignments in a block. Used to describe
  /// the live-in and live-out values for blocks, as well as the "current"
  /// value as we process each instruction in a block.
  struct BlockInfo {
    /// The set of variables (VariableID) being tracked in this block.
    BitVector VariableIDsInBlock;
    /// Dominating assignment to memory for each variable, indexed by
    /// VariableID.
    AssignmentMap StackHomeValue;
    /// Dominating assignemnt to each variable, indexed by VariableID.
    AssignmentMap DebugValue;
    /// Location kind for each variable. LiveLoc indicates whether the
    /// dominating assignment in StackHomeValue (LocKind::Mem), DebugValue
    /// (LocKind::Val), or neither (LocKind::None) is valid, in that order of
    /// preference. This cannot be derived by inspecting DebugValue and
    /// StackHomeValue due to the fact that there's no distinction in
    /// Assignment (the class) between whether an assignment is unknown or a
    /// merge of multiple assignments (both are Status::NoneOrPhi). In other
    /// words, the memory location may well be valid while both DebugValue and
    /// StackHomeValue contain Assignments that have a Status of NoneOrPhi.
    /// Indexed by VariableID.
    LocMap LiveLoc;

  public:
    enum AssignmentKind { Stack, Debug };
    const AssignmentMap &getAssignmentMap(AssignmentKind Kind) const {
      switch (Kind) {
      case Stack:
        return StackHomeValue;
      case Debug:
        return DebugValue;
      }
      llvm_unreachable("Unknown AssignmentKind");
    }
    AssignmentMap &getAssignmentMap(AssignmentKind Kind) {
      return const_cast<AssignmentMap &>(
          const_cast<const BlockInfo *>(this)->getAssignmentMap(Kind));
    }

    bool isVariableTracked(VariableID Var) const {
      return VariableIDsInBlock[static_cast<unsigned>(Var)];
    }

    const Assignment &getAssignment(AssignmentKind Kind, VariableID Var) const {
      assert(isVariableTracked(Var) && "Var not tracked in block");
      return getAssignmentMap(Kind)[static_cast<unsigned>(Var)];
    }

    LocKind getLocKind(VariableID Var) const {
      assert(isVariableTracked(Var) && "Var not tracked in block");
      return LiveLoc[static_cast<unsigned>(Var)];
    }

    /// Set LocKind for \p Var only: does not set LocKind for VariableIDs of
    /// fragments contained win \p Var.
    void setLocKind(VariableID Var, LocKind K) {
      VariableIDsInBlock.set(static_cast<unsigned>(Var));
      LiveLoc[static_cast<unsigned>(Var)] = K;
    }

    /// Set the assignment in the \p Kind assignment map for \p Var only: does
    /// not set the assignment for VariableIDs of fragments contained win \p
    /// Var.
    void setAssignment(AssignmentKind Kind, VariableID Var,
                       const Assignment &AV) {
      VariableIDsInBlock.set(static_cast<unsigned>(Var));
      getAssignmentMap(Kind)[static_cast<unsigned>(Var)] = AV;
    }

    /// Return true if there is an assignment matching \p AV in the \p Kind
    /// assignment map. Does consider assignments for VariableIDs of fragments
    /// contained win \p Var.
    bool hasAssignment(AssignmentKind Kind, VariableID Var,
                       const Assignment &AV) const {
      if (!isVariableTracked(Var))
        return false;
      return AV.isSameSourceAssignment(getAssignment(Kind, Var));
    }

    /// Compare every element in each map to determine structural equality
    /// (slow).
    bool operator==(const BlockInfo &Other) const {
      return VariableIDsInBlock == Other.VariableIDsInBlock &&
             LiveLoc == Other.LiveLoc &&
             mapsAreEqual(VariableIDsInBlock, StackHomeValue,
                          Other.StackHomeValue) &&
             mapsAreEqual(VariableIDsInBlock, DebugValue, Other.DebugValue);
    }
    bool operator!=(const BlockInfo &Other) const { return !(*this == Other); }
    bool isValid() {
      return LiveLoc.size() == DebugValue.size() &&
             LiveLoc.size() == StackHomeValue.size();
    }

    /// Clear everything and initialise with ⊤-values for all variables.
    void init(int NumVars) {
      StackHomeValue.clear();
      DebugValue.clear();
      LiveLoc.clear();
      VariableIDsInBlock = BitVector(NumVars);
      StackHomeValue.insert(StackHomeValue.begin(), NumVars,
                            Assignment::makeNoneOrPhi());
      DebugValue.insert(DebugValue.begin(), NumVars,
                        Assignment::makeNoneOrPhi());
      LiveLoc.insert(LiveLoc.begin(), NumVars, LocKind::None);
    }

    /// Helper for join.
    template <typename ElmtType, typename FnInputType>
    static void joinElmt(int Index, SmallVector<ElmtType> &Target,
                         const SmallVector<ElmtType> &A,
                         const SmallVector<ElmtType> &B,
                         ElmtType (*Fn)(FnInputType, FnInputType)) {
      Target[Index] = Fn(A[Index], B[Index]);
    }

    /// See comment for AssignmentTrackingLowering::joinBlockInfo.
    static BlockInfo join(const BlockInfo &A, const BlockInfo &B, int NumVars) {
      // Join A and B.
      //
      // Intersect = join(a, b) for a in A, b in B where Var(a) == Var(b)
      // Difference = join(x, ⊤) for x where Var(x) is in A xor B
      // Join = Intersect ∪ Difference
      //
      // This is achieved by performing a join on elements from A and B with
      // variables common to both A and B (join elements indexed by var
      // intersect), then adding ⊤-value elements for vars in A xor B. The
      // latter part is equivalent to performing join on elements with variables
      // in A xor B with the ⊤-value for the map element since join(x, ⊤) = ⊤.
      // BlockInfo::init initializes all variable entries to the ⊤ value so we
      // don't need to explicitly perform that step as Join.VariableIDsInBlock
      // is set to the union of the variables in A and B at the end of this
      // function.
      BlockInfo Join;
      Join.init(NumVars);

      BitVector Intersect = A.VariableIDsInBlock;
      Intersect &= B.VariableIDsInBlock;

      for (auto VarID : Intersect.set_bits()) {
        joinElmt(VarID, Join.LiveLoc, A.LiveLoc, B.LiveLoc, joinKind);
        joinElmt(VarID, Join.DebugValue, A.DebugValue, B.DebugValue,
                 joinAssignment);
        joinElmt(VarID, Join.StackHomeValue, A.StackHomeValue, B.StackHomeValue,
                 joinAssignment);
      }

      Join.VariableIDsInBlock = A.VariableIDsInBlock;
      Join.VariableIDsInBlock |= B.VariableIDsInBlock;
      assert(Join.isValid());
      return Join;
    }
  };

  Function &Fn;
  const DataLayout &Layout;
  const DenseSet<DebugAggregate> *VarsWithStackSlot;
  FunctionVarLocsBuilder *FnVarLocs;
  DenseMap<const BasicBlock *, BlockInfo> LiveIn;
  DenseMap<const BasicBlock *, BlockInfo> LiveOut;

  /// Helper for process methods to track variables touched each frame.
  DenseSet<VariableID> VarsTouchedThisFrame;

  /// The set of variables that sometimes are not located in their stack home.
  DenseSet<DebugAggregate> NotAlwaysStackHomed;

  VariableID getVariableID(const DebugVariable &Var) {
    return static_cast<VariableID>(FnVarLocs->insertVariable(Var));
  }

  /// Join the LiveOut values of preds that are contained in \p Visited into
  /// LiveIn[BB]. Return True if LiveIn[BB] has changed as a result. LiveIn[BB]
  /// values monotonically increase. See the @link joinMethods join methods
  /// @endlink documentation for more info.
  bool join(const BasicBlock &BB, const SmallPtrSet<BasicBlock *, 16> &Visited);
  ///@name joinMethods
  /// Functions that implement `join` (the least upper bound) for the
  /// join-semilattice types used in the dataflow. There is an explicit bottom
  /// value (⊥) for some types and and explicit top value (⊤) for all types.
  /// By definition:
  ///
  ///     Join(A, B) >= A && Join(A, B) >= B
  ///     Join(A, ⊥) = A
  ///     Join(A, ⊤) = ⊤
  ///
  /// These invariants are important for monotonicity.
  ///
  /// For the map-type functions, all unmapped keys in an empty map are
  /// associated with a bottom value (⊥). This represents their values being
  /// unknown. Unmapped keys in non-empty maps (joining two maps with a key
  /// only present in one) represents either a variable going out of scope or
  /// dropped debug info. It is assumed the key is associated with a top value
  /// (⊤) in this case (unknown location / assignment).
  ///@{
  static LocKind joinKind(LocKind A, LocKind B);
  static Assignment joinAssignment(const Assignment &A, const Assignment &B);
  BlockInfo joinBlockInfo(const BlockInfo &A, const BlockInfo &B);
  ///@}

  /// Process the instructions in \p BB updating \p LiveSet along the way. \p
  /// LiveSet must be initialized with the current live-in locations before
  /// calling this.
  void process(BasicBlock &BB, BlockInfo *LiveSet);
  ///@name processMethods
  /// Methods to process instructions in order to update the LiveSet (current
  /// location information).
  ///@{
  void processNonDbgInstruction(Instruction &I, BlockInfo *LiveSet);
  void processDbgInstruction(DbgInfoIntrinsic &I, BlockInfo *LiveSet);
  /// Update \p LiveSet after encountering an instruction with a DIAssignID
  /// attachment, \p I.
  void processTaggedInstruction(Instruction &I, BlockInfo *LiveSet);
  /// Update \p LiveSet after encountering an instruciton without a DIAssignID
  /// attachment, \p I.
  void processUntaggedInstruction(Instruction &I, BlockInfo *LiveSet);
  void processDbgAssign(AssignRecord Assign, BlockInfo *LiveSet);
  void processDbgVariableRecord(DbgVariableRecord &DVR, BlockInfo *LiveSet);
  void processDbgValue(
      PointerUnion<DbgValueInst *, DbgVariableRecord *> DbgValueRecord,
      BlockInfo *LiveSet);
  /// Add an assignment to memory for the variable /p Var.
  void addMemDef(BlockInfo *LiveSet, VariableID Var, const Assignment &AV);
  /// Add an assignment to the variable /p Var.
  void addDbgDef(BlockInfo *LiveSet, VariableID Var, const Assignment &AV);
  ///@}

  /// Set the LocKind for \p Var.
  void setLocKind(BlockInfo *LiveSet, VariableID Var, LocKind K);
  /// Get the live LocKind for a \p Var. Requires addMemDef or addDbgDef to
  /// have been called for \p Var first.
  LocKind getLocKind(BlockInfo *LiveSet, VariableID Var);
  /// Return true if \p Var has an assignment in \p M matching \p AV.
  bool hasVarWithAssignment(BlockInfo *LiveSet, BlockInfo::AssignmentKind Kind,
                            VariableID Var, const Assignment &AV);
  /// Return the set of VariableIDs corresponding the fragments contained fully
  /// within the variable/fragment \p Var.
  ArrayRef<VariableID> getContainedFragments(VariableID Var) const;

  /// Mark \p Var as having been touched this frame. Note, this applies only
  /// to the exact fragment \p Var and not to any fragments contained within.
  void touchFragment(VariableID Var);

  /// Emit info for variables that are fully promoted.
  bool emitPromotedVarLocs(FunctionVarLocsBuilder *FnVarLocs);

public:
  AssignmentTrackingLowering(Function &Fn, const DataLayout &Layout,
                             const DenseSet<DebugAggregate> *VarsWithStackSlot)
      : Fn(Fn), Layout(Layout), VarsWithStackSlot(VarsWithStackSlot) {}
  /// Run the analysis, adding variable location info to \p FnVarLocs. Returns
  /// true if any variable locations have been added to FnVarLocs.
  bool run(FunctionVarLocsBuilder *FnVarLocs);
};
} // namespace

ArrayRef<VariableID>
AssignmentTrackingLowering::getContainedFragments(VariableID Var) const {
  auto R = VarContains.find(Var);
  if (R == VarContains.end())
    return std::nullopt;
  return R->second;
}

void AssignmentTrackingLowering::touchFragment(VariableID Var) {
  VarsTouchedThisFrame.insert(Var);
}

void AssignmentTrackingLowering::setLocKind(BlockInfo *LiveSet, VariableID Var,
                                            LocKind K) {
  auto SetKind = [this](BlockInfo *LiveSet, VariableID Var, LocKind K) {
    LiveSet->setLocKind(Var, K);
    touchFragment(Var);
  };
  SetKind(LiveSet, Var, K);

  // Update the LocKind for all fragments contained within Var.
  for (VariableID Frag : getContainedFragments(Var))
    SetKind(LiveSet, Frag, K);
}

AssignmentTrackingLowering::LocKind
AssignmentTrackingLowering::getLocKind(BlockInfo *LiveSet, VariableID Var) {
  return LiveSet->getLocKind(Var);
}

void AssignmentTrackingLowering::addMemDef(BlockInfo *LiveSet, VariableID Var,
                                           const Assignment &AV) {
  LiveSet->setAssignment(BlockInfo::Stack, Var, AV);

  // Use this assigment for all fragments contained within Var, but do not
  // provide a Source because we cannot convert Var's value to a value for the
  // fragment.
  Assignment FragAV = AV;
  FragAV.Source = nullptr;
  for (VariableID Frag : getContainedFragments(Var))
    LiveSet->setAssignment(BlockInfo::Stack, Frag, FragAV);
}

void AssignmentTrackingLowering::addDbgDef(BlockInfo *LiveSet, VariableID Var,
                                           const Assignment &AV) {
  LiveSet->setAssignment(BlockInfo::Debug, Var, AV);

  // Use this assigment for all fragments contained within Var, but do not
  // provide a Source because we cannot convert Var's value to a value for the
  // fragment.
  Assignment FragAV = AV;
  FragAV.Source = nullptr;
  for (VariableID Frag : getContainedFragments(Var))
    LiveSet->setAssignment(BlockInfo::Debug, Frag, FragAV);
}

static DIAssignID *getIDFromInst(const Instruction &I) {
  return cast<DIAssignID>(I.getMetadata(LLVMContext::MD_DIAssignID));
}

static DIAssignID *getIDFromMarker(const DbgAssignIntrinsic &DAI) {
  return cast<DIAssignID>(DAI.getAssignID());
}

static DIAssignID *getIDFromMarker(const DbgVariableRecord &DVR) {
  assert(DVR.isDbgAssign() &&
         "Cannot get a DIAssignID from a non-assign DbgVariableRecord!");
  return DVR.getAssignID();
}

/// Return true if \p Var has an assignment in \p M matching \p AV.
bool AssignmentTrackingLowering::hasVarWithAssignment(
    BlockInfo *LiveSet, BlockInfo::AssignmentKind Kind, VariableID Var,
    const Assignment &AV) {
  if (!LiveSet->hasAssignment(Kind, Var, AV))
    return false;

  // Check all the frags contained within Var as these will have all been
  // mapped to AV at the last store to Var.
  for (VariableID Frag : getContainedFragments(Var))
    if (!LiveSet->hasAssignment(Kind, Frag, AV))
      return false;
  return true;
}

#ifndef NDEBUG
const char *locStr(AssignmentTrackingLowering::LocKind Loc) {
  using LocKind = AssignmentTrackingLowering::LocKind;
  switch (Loc) {
  case LocKind::Val:
    return "Val";
  case LocKind::Mem:
    return "Mem";
  case LocKind::None:
    return "None";
  };
  llvm_unreachable("unknown LocKind");
}
#endif

VarLocInsertPt getNextNode(const DbgRecord *DVR) {
  auto NextIt = ++(DVR->getIterator());
  if (NextIt == DVR->getMarker()->getDbgRecordRange().end())
    return DVR->getMarker()->MarkedInstr;
  return &*NextIt;
}
VarLocInsertPt getNextNode(const Instruction *Inst) {
  const Instruction *Next = Inst->getNextNode();
  if (!Next->hasDbgRecords())
    return Next;
  return &*Next->getDbgRecordRange().begin();
}
VarLocInsertPt getNextNode(VarLocInsertPt InsertPt) {
  if (isa<const Instruction *>(InsertPt))
    return getNextNode(cast<const Instruction *>(InsertPt));
  return getNextNode(cast<const DbgRecord *>(InsertPt));
}

DbgAssignIntrinsic *CastToDbgAssign(DbgVariableIntrinsic *DVI) {
  return cast<DbgAssignIntrinsic>(DVI);
}

DbgVariableRecord *CastToDbgAssign(DbgVariableRecord *DVR) {
  assert(DVR->isDbgAssign() &&
         "Attempted to cast non-assign DbgVariableRecord to DVRAssign.");
  return DVR;
}

void AssignmentTrackingLowering::emitDbgValue(
    AssignmentTrackingLowering::LocKind Kind,
    AssignmentTrackingLowering::AssignRecord Source, VarLocInsertPt After) {
  if (isa<DbgAssignIntrinsic *>(Source))
    emitDbgValue(Kind, cast<DbgAssignIntrinsic *>(Source), After);
  else
    emitDbgValue(Kind, cast<DbgVariableRecord *>(Source), After);
}
template <typename T>
void AssignmentTrackingLowering::emitDbgValue(
    AssignmentTrackingLowering::LocKind Kind, const T Source,
    VarLocInsertPt After) {

  DILocation *DL = Source->getDebugLoc();
  auto Emit = [this, Source, After, DL](Metadata *Val, DIExpression *Expr) {
    assert(Expr);
    if (!Val)
      Val = ValueAsMetadata::get(
          PoisonValue::get(Type::getInt1Ty(Source->getContext())));

    // Find a suitable insert point.
    auto InsertBefore = getNextNode(After);
    assert(InsertBefore && "Shouldn't be inserting after a terminator");

    VariableID Var = getVariableID(DebugVariable(Source));
    VarLocInfo VarLoc;
    VarLoc.VariableID = static_cast<VariableID>(Var);
    VarLoc.Expr = Expr;
    VarLoc.Values = RawLocationWrapper(Val);
    VarLoc.DL = DL;
    // Insert it into the map for later.
    InsertBeforeMap[InsertBefore].push_back(VarLoc);
  };

  // NOTE: This block can mutate Kind.
  if (Kind == LocKind::Mem) {
    const auto *Assign = CastToDbgAssign(Source);
    // Check the address hasn't been dropped (e.g. the debug uses may not have
    // been replaced before deleting a Value).
    if (Assign->isKillAddress()) {
      // The address isn't valid so treat this as a non-memory def.
      Kind = LocKind::Val;
    } else {
      Value *Val = Assign->getAddress();
      DIExpression *Expr = Assign->getAddressExpression();
      assert(!Expr->getFragmentInfo() &&
             "fragment info should be stored in value-expression only");
      // Copy the fragment info over from the value-expression to the new
      // DIExpression.
      if (auto OptFragInfo = Source->getExpression()->getFragmentInfo()) {
        auto FragInfo = *OptFragInfo;
        Expr = *DIExpression::createFragmentExpression(
            Expr, FragInfo.OffsetInBits, FragInfo.SizeInBits);
      }
      // The address-expression has an implicit deref, add it now.
      std::tie(Val, Expr) =
          walkToAllocaAndPrependOffsetDeref(Layout, Val, Expr);
      Emit(ValueAsMetadata::get(Val), Expr);
      return;
    }
  }

  if (Kind == LocKind::Val) {
    Emit(Source->getRawLocation(), Source->getExpression());
    return;
  }

  if (Kind == LocKind::None) {
    Emit(nullptr, Source->getExpression());
    return;
  }
}

void AssignmentTrackingLowering::processNonDbgInstruction(
    Instruction &I, AssignmentTrackingLowering::BlockInfo *LiveSet) {
  if (I.hasMetadata(LLVMContext::MD_DIAssignID))
    processTaggedInstruction(I, LiveSet);
  else
    processUntaggedInstruction(I, LiveSet);
}

void AssignmentTrackingLowering::processUntaggedInstruction(
    Instruction &I, AssignmentTrackingLowering::BlockInfo *LiveSet) {
  // Interpret stack stores that are not tagged as an assignment in memory for
  // the variables associated with that address. These stores may not be tagged
  // because a) the store cannot be represented using dbg.assigns (non-const
  // length or offset) or b) the tag was accidentally dropped during
  // optimisations. For these stores we fall back to assuming that the stack
  // home is a valid location for the variables. The benefit is that this
  // prevents us missing an assignment and therefore incorrectly maintaining
  // earlier location definitions, and in many cases it should be a reasonable
  // assumption. However, this will occasionally lead to slight
  // inaccuracies. The value of a hoisted untagged store will be visible
  // "early", for example.
  assert(!I.hasMetadata(LLVMContext::MD_DIAssignID));
  auto It = UntaggedStoreVars.find(&I);
  if (It == UntaggedStoreVars.end())
    return; // No variables associated with the store destination.

  LLVM_DEBUG(dbgs() << "processUntaggedInstruction on UNTAGGED INST " << I
                    << "\n");
  // Iterate over the variables that this store affects, add a NoneOrPhi dbg
  // and mem def, set lockind to Mem, and emit a location def for each.
  for (auto [Var, Info] : It->second) {
    // This instruction is treated as both a debug and memory assignment,
    // meaning the memory location should be used. We don't have an assignment
    // ID though so use Assignment::makeNoneOrPhi() to create an imaginary one.
    addMemDef(LiveSet, Var, Assignment::makeNoneOrPhi());
    addDbgDef(LiveSet, Var, Assignment::makeNoneOrPhi());
    setLocKind(LiveSet, Var, LocKind::Mem);
    LLVM_DEBUG(dbgs() << "  setting Stack LocKind to: " << locStr(LocKind::Mem)
                      << "\n");
    // Build the dbg location def to insert.
    //
    // DIExpression: Add fragment and offset.
    DebugVariable V = FnVarLocs->getVariable(Var);
    DIExpression *DIE = DIExpression::get(I.getContext(), std::nullopt);
    if (auto Frag = V.getFragment()) {
      auto R = DIExpression::createFragmentExpression(DIE, Frag->OffsetInBits,
                                                      Frag->SizeInBits);
      assert(R && "unexpected createFragmentExpression failure");
      DIE = *R;
    }
    SmallVector<uint64_t, 3> Ops;
    if (Info.OffsetInBits)
      Ops = {dwarf::DW_OP_plus_uconst, Info.OffsetInBits / 8};
    Ops.push_back(dwarf::DW_OP_deref);
    DIE = DIExpression::prependOpcodes(DIE, Ops, /*StackValue=*/false,
                                       /*EntryValue=*/false);
    // Find a suitable insert point, before the next instruction or DbgRecord
    // after I.
    auto InsertBefore = getNextNode(&I);
    assert(InsertBefore && "Shouldn't be inserting after a terminator");

    // Get DILocation for this unrecorded assignment.
    DILocation *InlinedAt = const_cast<DILocation *>(V.getInlinedAt());
    const DILocation *DILoc = DILocation::get(
        Fn.getContext(), 0, 0, V.getVariable()->getScope(), InlinedAt);

    VarLocInfo VarLoc;
    VarLoc.VariableID = static_cast<VariableID>(Var);
    VarLoc.Expr = DIE;
    VarLoc.Values = RawLocationWrapper(
        ValueAsMetadata::get(const_cast<AllocaInst *>(Info.Base)));
    VarLoc.DL = DILoc;
    // 3. Insert it into the map for later.
    InsertBeforeMap[InsertBefore].push_back(VarLoc);
  }
}

void AssignmentTrackingLowering::processTaggedInstruction(
    Instruction &I, AssignmentTrackingLowering::BlockInfo *LiveSet) {
  auto Linked = at::getAssignmentMarkers(&I);
  auto LinkedDPAssigns = at::getDVRAssignmentMarkers(&I);
  // No dbg.assign intrinsics linked.
  // FIXME: All vars that have a stack slot this store modifies that don't have
  // a dbg.assign linked to it should probably treat this like an untagged
  // store.
  if (Linked.empty() && LinkedDPAssigns.empty())
    return;

  LLVM_DEBUG(dbgs() << "processTaggedInstruction on " << I << "\n");
  auto ProcessLinkedAssign = [&](auto *Assign) {
    VariableID Var = getVariableID(DebugVariable(Assign));
    // Something has gone wrong if VarsWithStackSlot doesn't contain a variable
    // that is linked to a store.
    assert(VarsWithStackSlot->count(getAggregate(Assign)) &&
           "expected Assign's variable to have stack slot");

    Assignment AV = Assignment::makeFromMemDef(getIDFromInst(I));
    addMemDef(LiveSet, Var, AV);

    LLVM_DEBUG(dbgs() << "   linked to " << *Assign << "\n");
    LLVM_DEBUG(dbgs() << "   LiveLoc " << locStr(getLocKind(LiveSet, Var))
                      << " -> ");

    // The last assignment to the stack is now AV. Check if the last debug
    // assignment has a matching Assignment.
    if (hasVarWithAssignment(LiveSet, BlockInfo::Debug, Var, AV)) {
      // The StackHomeValue and DebugValue for this variable match so we can
      // emit a stack home location here.
      LLVM_DEBUG(dbgs() << "Mem, Stack matches Debug program\n";);
      LLVM_DEBUG(dbgs() << "   Stack val: "; AV.dump(dbgs()); dbgs() << "\n");
      LLVM_DEBUG(dbgs() << "   Debug val: ";
                 LiveSet->DebugValue[static_cast<unsigned>(Var)].dump(dbgs());
                 dbgs() << "\n");
      setLocKind(LiveSet, Var, LocKind::Mem);
      emitDbgValue(LocKind::Mem, Assign, &I);
      return;
    }

    // The StackHomeValue and DebugValue for this variable do not match. I.e.
    // The value currently stored in the stack is not what we'd expect to
    // see, so we cannot use emit a stack home location here. Now we will
    // look at the live LocKind for the variable and determine an appropriate
    // dbg.value to emit.
    LocKind PrevLoc = getLocKind(LiveSet, Var);
    switch (PrevLoc) {
    case LocKind::Val: {
      // The value in memory in memory has changed but we're not currently
      // using the memory location. Do nothing.
      LLVM_DEBUG(dbgs() << "Val, (unchanged)\n";);
      setLocKind(LiveSet, Var, LocKind::Val);
    } break;
    case LocKind::Mem: {
      // There's been an assignment to memory that we were using as a
      // location for this variable, and the Assignment doesn't match what
      // we'd expect to see in memory.
      Assignment DbgAV = LiveSet->getAssignment(BlockInfo::Debug, Var);
      if (DbgAV.Status == Assignment::NoneOrPhi) {
        // We need to terminate any previously open location now.
        LLVM_DEBUG(dbgs() << "None, No Debug value available\n";);
        setLocKind(LiveSet, Var, LocKind::None);
        emitDbgValue(LocKind::None, Assign, &I);
      } else {
        // The previous DebugValue Value can be used here.
        LLVM_DEBUG(dbgs() << "Val, Debug value is Known\n";);
        setLocKind(LiveSet, Var, LocKind::Val);
        if (DbgAV.Source) {
          emitDbgValue(LocKind::Val, DbgAV.Source, &I);
        } else {
          // PrevAV.Source is nullptr so we must emit undef here.
          emitDbgValue(LocKind::None, Assign, &I);
        }
      }
    } break;
    case LocKind::None: {
      // There's been an assignment to memory and we currently are
      // not tracking a location for the variable. Do not emit anything.
      LLVM_DEBUG(dbgs() << "None, (unchanged)\n";);
      setLocKind(LiveSet, Var, LocKind::None);
    } break;
    }
  };
  for (DbgAssignIntrinsic *DAI : Linked)
    ProcessLinkedAssign(DAI);
  for (DbgVariableRecord *DVR : LinkedDPAssigns)
    ProcessLinkedAssign(DVR);
}

void AssignmentTrackingLowering::processDbgAssign(AssignRecord Assign,
                                                  BlockInfo *LiveSet) {
  auto ProcessDbgAssignImpl = [&](auto *DbgAssign) {
    // Only bother tracking variables that are at some point stack homed. Other
    // variables can be dealt with trivially later.
    if (!VarsWithStackSlot->count(getAggregate(DbgAssign)))
      return;

    VariableID Var = getVariableID(DebugVariable(DbgAssign));
    Assignment AV = Assignment::make(getIDFromMarker(*DbgAssign), DbgAssign);
    addDbgDef(LiveSet, Var, AV);

    LLVM_DEBUG(dbgs() << "processDbgAssign on " << *DbgAssign << "\n";);
    LLVM_DEBUG(dbgs() << "   LiveLoc " << locStr(getLocKind(LiveSet, Var))
                      << " -> ");

    // Check if the DebugValue and StackHomeValue both hold the same
    // Assignment.
    if (hasVarWithAssignment(LiveSet, BlockInfo::Stack, Var, AV)) {
      // They match. We can use the stack home because the debug intrinsics
      // state that an assignment happened here, and we know that specific
      // assignment was the last one to take place in memory for this variable.
      LocKind Kind;
      if (DbgAssign->isKillAddress()) {
        LLVM_DEBUG(
            dbgs()
                << "Val, Stack matches Debug program but address is killed\n";);
        Kind = LocKind::Val;
      } else {
        LLVM_DEBUG(dbgs() << "Mem, Stack matches Debug program\n";);
        Kind = LocKind::Mem;
      };
      setLocKind(LiveSet, Var, Kind);
      emitDbgValue(Kind, DbgAssign, DbgAssign);
    } else {
      // The last assignment to the memory location isn't the one that we want
      // to show to the user so emit a dbg.value(Value). Value may be undef.
      LLVM_DEBUG(dbgs() << "Val, Stack contents is unknown\n";);
      setLocKind(LiveSet, Var, LocKind::Val);
      emitDbgValue(LocKind::Val, DbgAssign, DbgAssign);
    }
  };
  if (isa<DbgVariableRecord *>(Assign))
    return ProcessDbgAssignImpl(cast<DbgVariableRecord *>(Assign));
  return ProcessDbgAssignImpl(cast<DbgAssignIntrinsic *>(Assign));
}

void AssignmentTrackingLowering::processDbgValue(
    PointerUnion<DbgValueInst *, DbgVariableRecord *> DbgValueRecord,
    BlockInfo *LiveSet) {
  auto ProcessDbgValueImpl = [&](auto *DbgValue) {
    // Only other tracking variables that are at some point stack homed.
    // Other variables can be dealt with trivally later.
    if (!VarsWithStackSlot->count(getAggregate(DbgValue)))
      return;

    VariableID Var = getVariableID(DebugVariable(DbgValue));
    // We have no ID to create an Assignment with so we mark this assignment as
    // NoneOrPhi. Note that the dbg.value still exists, we just cannot determine
    // the assignment responsible for setting this value.
    // This is fine; dbg.values are essentially interchangable with unlinked
    // dbg.assigns, and some passes such as mem2reg and instcombine add them to
    // PHIs for promoted variables.
    Assignment AV = Assignment::makeNoneOrPhi();
    addDbgDef(LiveSet, Var, AV);

    LLVM_DEBUG(dbgs() << "processDbgValue on " << *DbgValue << "\n";);
    LLVM_DEBUG(dbgs() << "   LiveLoc " << locStr(getLocKind(LiveSet, Var))
                      << " -> Val, dbg.value override");

    setLocKind(LiveSet, Var, LocKind::Val);
    emitDbgValue(LocKind::Val, DbgValue, DbgValue);
  };
  if (isa<DbgVariableRecord *>(DbgValueRecord))
    return ProcessDbgValueImpl(cast<DbgVariableRecord *>(DbgValueRecord));
  return ProcessDbgValueImpl(cast<DbgValueInst *>(DbgValueRecord));
}

template <typename T> static bool hasZeroSizedFragment(T &DbgValue) {
  if (auto F = DbgValue.getExpression()->getFragmentInfo())
    return F->SizeInBits == 0;
  return false;
}

void AssignmentTrackingLowering::processDbgInstruction(
    DbgInfoIntrinsic &I, AssignmentTrackingLowering::BlockInfo *LiveSet) {
  auto *DVI = dyn_cast<DbgVariableIntrinsic>(&I);
  if (!DVI)
    return;

  // Ignore assignments to zero bits of the variable.
  if (hasZeroSizedFragment(*DVI))
    return;

  if (auto *DAI = dyn_cast<DbgAssignIntrinsic>(&I))
    processDbgAssign(DAI, LiveSet);
  else if (auto *DVI = dyn_cast<DbgValueInst>(&I))
    processDbgValue(DVI, LiveSet);
}
void AssignmentTrackingLowering::processDbgVariableRecord(
    DbgVariableRecord &DVR, AssignmentTrackingLowering::BlockInfo *LiveSet) {
  // Ignore assignments to zero bits of the variable.
  if (hasZeroSizedFragment(DVR))
    return;

  if (DVR.isDbgAssign())
    processDbgAssign(&DVR, LiveSet);
  else if (DVR.isDbgValue())
    processDbgValue(&DVR, LiveSet);
}

void AssignmentTrackingLowering::resetInsertionPoint(Instruction &After) {
  assert(!After.isTerminator() && "Can't insert after a terminator");
  auto *R = InsertBeforeMap.find(getNextNode(&After));
  if (R == InsertBeforeMap.end())
    return;
  R->second.clear();
}
void AssignmentTrackingLowering::resetInsertionPoint(DbgVariableRecord &After) {
  auto *R = InsertBeforeMap.find(getNextNode(&After));
  if (R == InsertBeforeMap.end())
    return;
  R->second.clear();
}

void AssignmentTrackingLowering::process(BasicBlock &BB, BlockInfo *LiveSet) {
  // If the block starts with DbgRecords, we need to process those DbgRecords as
  // their own frame without processing any instructions first.
  bool ProcessedLeadingDbgRecords = !BB.begin()->hasDbgRecords();
  for (auto II = BB.begin(), EI = BB.end(); II != EI;) {
    assert(VarsTouchedThisFrame.empty());
    // Process the instructions in "frames". A "frame" includes a single
    // non-debug instruction followed any debug instructions before the
    // next non-debug instruction.

    // Skip the current instruction if it has unprocessed DbgRecords attached
    // (see comment above `ProcessedLeadingDbgRecords`).
    if (ProcessedLeadingDbgRecords) {
      // II is now either a debug intrinsic, a non-debug instruction with no
      // attached DbgRecords, or a non-debug instruction with attached processed
      // DbgRecords.
      // II has not been processed.
      if (!isa<DbgInfoIntrinsic>(&*II)) {
        if (II->isTerminator())
          break;
        resetInsertionPoint(*II);
        processNonDbgInstruction(*II, LiveSet);
        assert(LiveSet->isValid());
        ++II;
      }
    }
    // II is now either a debug intrinsic, a non-debug instruction with no
    // attached DbgRecords, or a non-debug instruction with attached unprocessed
    // DbgRecords.
    if (II != EI && II->hasDbgRecords()) {
      // Skip over non-variable debug records (i.e., labels). They're going to
      // be read from IR (possibly re-ordering them within the debug record
      // range) rather than from the analysis results.
      for (DbgVariableRecord &DVR : filterDbgVars(II->getDbgRecordRange())) {
        resetInsertionPoint(DVR);
        processDbgVariableRecord(DVR, LiveSet);
        assert(LiveSet->isValid());
      }
    }
    ProcessedLeadingDbgRecords = true;
    while (II != EI) {
      auto *Dbg = dyn_cast<DbgInfoIntrinsic>(&*II);
      if (!Dbg)
        break;
      resetInsertionPoint(*II);
      processDbgInstruction(*Dbg, LiveSet);
      assert(LiveSet->isValid());
      ++II;
    }
    // II is now a non-debug instruction either with no attached DbgRecords, or
    // with attached processed DbgRecords. II has not been processed, and all
    // debug instructions or DbgRecords in the frame preceding II have been
    // processed.

    // We've processed everything in the "frame". Now determine which variables
    // cannot be represented by a dbg.declare.
    for (auto Var : VarsTouchedThisFrame) {
      LocKind Loc = getLocKind(LiveSet, Var);
      // If a variable's LocKind is anything other than LocKind::Mem then we
      // must note that it cannot be represented with a dbg.declare.
      // Note that this check is enough without having to check the result of
      // joins() because for join to produce anything other than Mem after
      // we've already seen a Mem we'd be joining None or Val with Mem. In that
      // case, we've already hit this codepath when we set the LocKind to Val
      // or None in that block.
      if (Loc != LocKind::Mem) {
        DebugVariable DbgVar = FnVarLocs->getVariable(Var);
        DebugAggregate Aggr{DbgVar.getVariable(), DbgVar.getInlinedAt()};
        NotAlwaysStackHomed.insert(Aggr);
      }
    }
    VarsTouchedThisFrame.clear();
  }
}

AssignmentTrackingLowering::LocKind
AssignmentTrackingLowering::joinKind(LocKind A, LocKind B) {
  // Partial order:
  // None > Mem, Val
  return A == B ? A : LocKind::None;
}

AssignmentTrackingLowering::Assignment
AssignmentTrackingLowering::joinAssignment(const Assignment &A,
                                           const Assignment &B) {
  // Partial order:
  // NoneOrPhi(null, null) > Known(v, ?s)

  // If either are NoneOrPhi the join is NoneOrPhi.
  // If either value is different then the result is
  // NoneOrPhi (joining two values is a Phi).
  if (!A.isSameSourceAssignment(B))
    return Assignment::makeNoneOrPhi();
  if (A.Status == Assignment::NoneOrPhi)
    return Assignment::makeNoneOrPhi();

  // Source is used to lookup the value + expression in the debug program if
  // the stack slot gets assigned a value earlier than expected. Because
  // we're only tracking the one dbg.assign, we can't capture debug PHIs.
  // It's unlikely that we're losing out on much coverage by avoiding that
  // extra work.
  // The Source may differ in this situation:
  // Pred.1:
  //   dbg.assign i32 0, ..., !1, ...
  // Pred.2:
  //   dbg.assign i32 1, ..., !1, ...
  // Here the same assignment (!1) was performed in both preds in the source,
  // but we can't use either one unless they are identical (e.g. .we don't
  // want to arbitrarily pick between constant values).
  auto JoinSource = [&]() -> AssignRecord {
    if (A.Source == B.Source)
      return A.Source;
    if (!A.Source || !B.Source)
      return AssignRecord();
    assert(isa<DbgVariableRecord *>(A.Source) ==
           isa<DbgVariableRecord *>(B.Source));
    if (isa<DbgVariableRecord *>(A.Source) &&
        cast<DbgVariableRecord *>(A.Source)->isEquivalentTo(
            *cast<DbgVariableRecord *>(B.Source)))
      return A.Source;
    if (isa<DbgAssignIntrinsic *>(A.Source) &&
        cast<DbgAssignIntrinsic *>(A.Source)->isIdenticalTo(
            cast<DbgAssignIntrinsic *>(B.Source)))
      return A.Source;
    return AssignRecord();
  };
  AssignRecord Source = JoinSource();
  assert(A.Status == B.Status && A.Status == Assignment::Known);
  assert(A.ID == B.ID);
  return Assignment::make(A.ID, Source);
}

AssignmentTrackingLowering::BlockInfo
AssignmentTrackingLowering::joinBlockInfo(const BlockInfo &A,
                                          const BlockInfo &B) {
  return BlockInfo::join(A, B, TrackedVariablesVectorSize);
}

bool AssignmentTrackingLowering::join(
    const BasicBlock &BB, const SmallPtrSet<BasicBlock *, 16> &Visited) {

  SmallVector<const BasicBlock *> VisitedPreds;
  // Ignore backedges if we have not visited the predecessor yet. As the
  // predecessor hasn't yet had locations propagated into it, most locations
  // will not yet be valid, so treat them as all being uninitialized and
  // potentially valid. If a location guessed to be correct here is
  // invalidated later, we will remove it when we revisit this block. This
  // is essentially the same as initialising all LocKinds and Assignments to
  // an implicit ⊥ value which is the identity value for the join operation.
  for (const BasicBlock *Pred : predecessors(&BB)) {
    if (Visited.count(Pred))
      VisitedPreds.push_back(Pred);
  }

  // No preds visited yet.
  if (VisitedPreds.empty()) {
    auto It = LiveIn.try_emplace(&BB, BlockInfo());
    bool DidInsert = It.second;
    if (DidInsert)
      It.first->second.init(TrackedVariablesVectorSize);
    return /*Changed*/ DidInsert;
  }

  // Exactly one visited pred. Copy the LiveOut from that pred into BB LiveIn.
  if (VisitedPreds.size() == 1) {
    const BlockInfo &PredLiveOut = LiveOut.find(VisitedPreds[0])->second;
    auto CurrentLiveInEntry = LiveIn.find(&BB);

    // Check if there isn't an entry, or there is but the LiveIn set has
    // changed (expensive check).
    if (CurrentLiveInEntry == LiveIn.end())
      LiveIn.insert(std::make_pair(&BB, PredLiveOut));
    else if (PredLiveOut != CurrentLiveInEntry->second)
      CurrentLiveInEntry->second = PredLiveOut;
    else
      return /*Changed*/ false;
    return /*Changed*/ true;
  }

  // More than one pred. Join LiveOuts of blocks 1 and 2.
  assert(VisitedPreds.size() > 1);
  const BlockInfo &PredLiveOut0 = LiveOut.find(VisitedPreds[0])->second;
  const BlockInfo &PredLiveOut1 = LiveOut.find(VisitedPreds[1])->second;
  BlockInfo BBLiveIn = joinBlockInfo(PredLiveOut0, PredLiveOut1);

  // Join the LiveOuts of subsequent blocks.
  ArrayRef Tail = ArrayRef(VisitedPreds).drop_front(2);
  for (const BasicBlock *Pred : Tail) {
    const auto &PredLiveOut = LiveOut.find(Pred);
    assert(PredLiveOut != LiveOut.end() &&
           "block should have been processed already");
    BBLiveIn = joinBlockInfo(std::move(BBLiveIn), PredLiveOut->second);
  }

  // Save the joined result for BB.
  auto CurrentLiveInEntry = LiveIn.find(&BB);
  // Check if there isn't an entry, or there is but the LiveIn set has changed
  // (expensive check).
  if (CurrentLiveInEntry == LiveIn.end())
    LiveIn.try_emplace(&BB, std::move(BBLiveIn));
  else if (BBLiveIn != CurrentLiveInEntry->second)
    CurrentLiveInEntry->second = std::move(BBLiveIn);
  else
    return /*Changed*/ false;
  return /*Changed*/ true;
}

/// Return true if A fully contains B.
static bool fullyContains(DIExpression::FragmentInfo A,
                          DIExpression::FragmentInfo B) {
  auto ALeft = A.OffsetInBits;
  auto BLeft = B.OffsetInBits;
  if (BLeft < ALeft)
    return false;

  auto ARight = ALeft + A.SizeInBits;
  auto BRight = BLeft + B.SizeInBits;
  if (BRight > ARight)
    return false;
  return true;
}

static std::optional<at::AssignmentInfo>
getUntaggedStoreAssignmentInfo(const Instruction &I, const DataLayout &Layout) {
  // Don't bother checking if this is an AllocaInst. We know this
  // instruction has no tag which means there are no variables associated
  // with it.
  if (const auto *SI = dyn_cast<StoreInst>(&I))
    return at::getAssignmentInfo(Layout, SI);
  if (const auto *MI = dyn_cast<MemIntrinsic>(&I))
    return at::getAssignmentInfo(Layout, MI);
  // Alloca or non-store-like inst.
  return std::nullopt;
}

DbgDeclareInst *DynCastToDbgDeclare(DbgVariableIntrinsic *DVI) {
  return dyn_cast<DbgDeclareInst>(DVI);
}

DbgVariableRecord *DynCastToDbgDeclare(DbgVariableRecord *DVR) {
  return DVR->isDbgDeclare() ? DVR : nullptr;
}

/// Build a map of {Variable x: Variables y} where all variable fragments
/// contained within the variable fragment x are in set y. This means that
/// y does not contain all overlaps because partial overlaps are excluded.
///
/// While we're iterating over the function, add single location defs for
/// dbg.declares to \p FnVarLocs.
///
/// Variables that are interesting to this pass in are added to
/// FnVarLocs->Variables first. TrackedVariablesVectorSize is set to the ID of
/// the last interesting variable plus 1, meaning variables with ID 1
/// (inclusive) to TrackedVariablesVectorSize (exclusive) are interesting. The
/// subsequent variables are either stack homed or fully promoted.
///
/// Finally, populate UntaggedStoreVars with a mapping of untagged stores to
/// the stored-to variable fragments.
///
/// These tasks are bundled together to reduce the number of times we need
/// to iterate over the function as they can be achieved together in one pass.
static AssignmentTrackingLowering::OverlapMap buildOverlapMapAndRecordDeclares(
    Function &Fn, FunctionVarLocsBuilder *FnVarLocs,
    const DenseSet<DebugAggregate> &VarsWithStackSlot,
    AssignmentTrackingLowering::UntaggedStoreAssignmentMap &UntaggedStoreVars,
    unsigned &TrackedVariablesVectorSize) {
  DenseSet<DebugVariable> Seen;
  // Map of Variable: [Fragments].
  DenseMap<DebugAggregate, SmallVector<DebugVariable, 8>> FragmentMap;
  // Iterate over all instructions:
  // - dbg.declare    -> add single location variable record
  // - dbg.*          -> Add fragments to FragmentMap
  // - untagged store -> Add fragments to FragmentMap and update
  //                     UntaggedStoreVars.
  // We need to add fragments for untagged stores too so that we can correctly
  // clobber overlapped fragment locations later.
  SmallVector<DbgDeclareInst *> InstDeclares;
  SmallVector<DbgVariableRecord *> DPDeclares;
  auto ProcessDbgRecord = [&](auto *Record, auto &DeclareList) {
    if (auto *Declare = DynCastToDbgDeclare(Record)) {
      DeclareList.push_back(Declare);
      return;
    }
    DebugVariable DV = DebugVariable(Record);
    DebugAggregate DA = {DV.getVariable(), DV.getInlinedAt()};
    if (!VarsWithStackSlot.contains(DA))
      return;
    if (Seen.insert(DV).second)
      FragmentMap[DA].push_back(DV);
  };
  for (auto &BB : Fn) {
    for (auto &I : BB) {
      for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
        ProcessDbgRecord(&DVR, DPDeclares);
      if (auto *DII = dyn_cast<DbgVariableIntrinsic>(&I)) {
        ProcessDbgRecord(DII, InstDeclares);
      } else if (auto Info = getUntaggedStoreAssignmentInfo(
                     I, Fn.getDataLayout())) {
        // Find markers linked to this alloca.
        auto HandleDbgAssignForStore = [&](auto *Assign) {
          std::optional<DIExpression::FragmentInfo> FragInfo;

          // Skip this assignment if the affected bits are outside of the
          // variable fragment.
          if (!at::calculateFragmentIntersect(
                  I.getDataLayout(), Info->Base,
                  Info->OffsetInBits, Info->SizeInBits, Assign, FragInfo) ||
              (FragInfo && FragInfo->SizeInBits == 0))
            return;

          // FragInfo from calculateFragmentIntersect is nullopt if the
          // resultant fragment matches DAI's fragment or entire variable - in
          // which case copy the fragment info from DAI. If FragInfo is still
          // nullopt after the copy it means "no fragment info" instead, which
          // is how it is usually interpreted.
          if (!FragInfo)
            FragInfo = Assign->getExpression()->getFragmentInfo();

          DebugVariable DV =
              DebugVariable(Assign->getVariable(), FragInfo,
                            Assign->getDebugLoc().getInlinedAt());
          DebugAggregate DA = {DV.getVariable(), DV.getInlinedAt()};
          if (!VarsWithStackSlot.contains(DA))
            return;

          // Cache this info for later.
          UntaggedStoreVars[&I].push_back(
              {FnVarLocs->insertVariable(DV), *Info});

          if (Seen.insert(DV).second)
            FragmentMap[DA].push_back(DV);
        };
        for (DbgAssignIntrinsic *DAI : at::getAssignmentMarkers(Info->Base))
          HandleDbgAssignForStore(DAI);
        for (DbgVariableRecord *DVR : at::getDVRAssignmentMarkers(Info->Base))
          HandleDbgAssignForStore(DVR);
      }
    }
  }

  // Sort the fragment map for each DebugAggregate in ascending
  // order of fragment size - there should be no duplicates.
  for (auto &Pair : FragmentMap) {
    SmallVector<DebugVariable, 8> &Frags = Pair.second;
    std::sort(Frags.begin(), Frags.end(),
              [](const DebugVariable &Next, const DebugVariable &Elmt) {
                return Elmt.getFragmentOrDefault().SizeInBits >
                       Next.getFragmentOrDefault().SizeInBits;
              });
    // Check for duplicates.
    assert(std::adjacent_find(Frags.begin(), Frags.end()) == Frags.end());
  }

  // Build the map.
  AssignmentTrackingLowering::OverlapMap Map;
  for (auto &Pair : FragmentMap) {
    auto &Frags = Pair.second;
    for (auto It = Frags.begin(), IEnd = Frags.end(); It != IEnd; ++It) {
      DIExpression::FragmentInfo Frag = It->getFragmentOrDefault();
      // Find the frags that this is contained within.
      //
      // Because Frags is sorted by size and none have the same offset and
      // size, we know that this frag can only be contained by subsequent
      // elements.
      SmallVector<DebugVariable, 8>::iterator OtherIt = It;
      ++OtherIt;
      VariableID ThisVar = FnVarLocs->insertVariable(*It);
      for (; OtherIt != IEnd; ++OtherIt) {
        DIExpression::FragmentInfo OtherFrag = OtherIt->getFragmentOrDefault();
        VariableID OtherVar = FnVarLocs->insertVariable(*OtherIt);
        if (fullyContains(OtherFrag, Frag))
          Map[OtherVar].push_back(ThisVar);
      }
    }
  }

  // VariableIDs are 1-based so the variable-tracking bitvector needs
  // NumVariables plus 1 bits.
  TrackedVariablesVectorSize = FnVarLocs->getNumVariables() + 1;

  // Finally, insert the declares afterwards, so the first IDs are all
  // partially stack homed vars.
  for (auto *DDI : InstDeclares)
    FnVarLocs->addSingleLocVar(DebugVariable(DDI), DDI->getExpression(),
                               DDI->getDebugLoc(), DDI->getWrappedLocation());
  for (auto *DVR : DPDeclares)
    FnVarLocs->addSingleLocVar(DebugVariable(DVR), DVR->getExpression(),
                               DVR->getDebugLoc(),
                               RawLocationWrapper(DVR->getRawLocation()));
  return Map;
}

bool AssignmentTrackingLowering::run(FunctionVarLocsBuilder *FnVarLocsBuilder) {
  if (Fn.size() > MaxNumBlocks) {
    LLVM_DEBUG(dbgs() << "[AT] Dropping var locs in: " << Fn.getName()
                      << ": too many blocks (" << Fn.size() << ")\n");
    at::deleteAll(&Fn);
    return false;
  }

  FnVarLocs = FnVarLocsBuilder;

  // The general structure here is inspired by VarLocBasedImpl.cpp
  // (LiveDebugValues).

  // Build the variable fragment overlap map.
  // Note that this pass doesn't handle partial overlaps correctly (FWIW
  // neither does LiveDebugVariables) because that is difficult to do and
  // appears to be rare occurance.
  VarContains = buildOverlapMapAndRecordDeclares(
      Fn, FnVarLocs, *VarsWithStackSlot, UntaggedStoreVars,
      TrackedVariablesVectorSize);

  // Prepare for traversal.
  ReversePostOrderTraversal<Function *> RPOT(&Fn);
  std::priority_queue<unsigned int, std::vector<unsigned int>,
                      std::greater<unsigned int>>
      Worklist;
  std::priority_queue<unsigned int, std::vector<unsigned int>,
                      std::greater<unsigned int>>
      Pending;
  DenseMap<unsigned int, BasicBlock *> OrderToBB;
  DenseMap<BasicBlock *, unsigned int> BBToOrder;
  { // Init OrderToBB and BBToOrder.
    unsigned int RPONumber = 0;
    for (BasicBlock *BB : RPOT) {
      OrderToBB[RPONumber] = BB;
      BBToOrder[BB] = RPONumber;
      Worklist.push(RPONumber);
      ++RPONumber;
    }
    LiveIn.init(RPONumber);
    LiveOut.init(RPONumber);
  }

  // Perform the traversal.
  //
  // This is a standard "union of predecessor outs" dataflow problem. To solve
  // it, we perform join() and process() using the two worklist method until
  // the LiveIn data for each block becomes unchanging. The "proof" that this
  // terminates can be put together by looking at the comments around LocKind,
  // Assignment, and the various join methods, which show that all the elements
  // involved are made up of join-semilattices; LiveIn(n) can only
  // monotonically increase in value throughout the dataflow.
  //
  SmallPtrSet<BasicBlock *, 16> Visited;
  while (!Worklist.empty()) {
    // We track what is on the pending worklist to avoid inserting the same
    // thing twice.
    SmallPtrSet<BasicBlock *, 16> OnPending;
    LLVM_DEBUG(dbgs() << "Processing Worklist\n");
    while (!Worklist.empty()) {
      BasicBlock *BB = OrderToBB[Worklist.top()];
      LLVM_DEBUG(dbgs() << "\nPop BB " << BB->getName() << "\n");
      Worklist.pop();
      bool InChanged = join(*BB, Visited);
      // Always consider LiveIn changed on the first visit.
      InChanged |= Visited.insert(BB).second;
      if (InChanged) {
        LLVM_DEBUG(dbgs() << BB->getName() << " has new InLocs, process it\n");
        // Mutate a copy of LiveIn while processing BB. After calling process
        // LiveSet is the LiveOut set for BB.
        BlockInfo LiveSet = LiveIn[BB];

        // Process the instructions in the block.
        process(*BB, &LiveSet);

        // Relatively expensive check: has anything changed in LiveOut for BB?
        if (LiveOut[BB] != LiveSet) {
          LLVM_DEBUG(dbgs() << BB->getName()
                            << " has new OutLocs, add succs to worklist: [ ");
          LiveOut[BB] = std::move(LiveSet);
          for (BasicBlock *Succ : successors(BB)) {
            if (OnPending.insert(Succ).second) {
              LLVM_DEBUG(dbgs() << Succ->getName() << " ");
              Pending.push(BBToOrder[Succ]);
            }
          }
          LLVM_DEBUG(dbgs() << "]\n");
        }
      }
    }
    Worklist.swap(Pending);
    // At this point, pending must be empty, since it was just the empty
    // worklist
    assert(Pending.empty() && "Pending should be empty");
  }

  // That's the hard part over. Now we just have some admin to do.

  // Record whether we inserted any intrinsics.
  bool InsertedAnyIntrinsics = false;

  // Identify and add defs for single location variables.
  //
  // Go through all of the defs that we plan to add. If the aggregate variable
  // it's a part of is not in the NotAlwaysStackHomed set we can emit a single
  // location def and omit the rest. Add an entry to AlwaysStackHomed so that
  // we can identify those uneeded defs later.
  DenseSet<DebugAggregate> AlwaysStackHomed;
  for (const auto &Pair : InsertBeforeMap) {
    auto &Vec = Pair.second;
    for (VarLocInfo VarLoc : Vec) {
      DebugVariable Var = FnVarLocs->getVariable(VarLoc.VariableID);
      DebugAggregate Aggr{Var.getVariable(), Var.getInlinedAt()};

      // Skip this Var if it's not always stack homed.
      if (NotAlwaysStackHomed.contains(Aggr))
        continue;

      // Skip complex cases such as when different fragments of a variable have
      // been split into different allocas. Skipping in this case means falling
      // back to using a list of defs (which could reduce coverage, but is no
      // less correct).
      bool Simple =
          VarLoc.Expr->getNumElements() == 1 && VarLoc.Expr->startsWithDeref();
      if (!Simple) {
        NotAlwaysStackHomed.insert(Aggr);
        continue;
      }

      // All source assignments to this variable remain and all stores to any
      // part of the variable store to the same address (with varying
      // offsets). We can just emit a single location for the whole variable.
      //
      // Unless we've already done so, create the single location def now.
      if (AlwaysStackHomed.insert(Aggr).second) {
        assert(!VarLoc.Values.hasArgList());
        // TODO: When more complex cases are handled VarLoc.Expr should be
        // built appropriately rather than always using an empty DIExpression.
        // The assert below is a reminder.
        assert(Simple);
        VarLoc.Expr = DIExpression::get(Fn.getContext(), std::nullopt);
        DebugVariable Var = FnVarLocs->getVariable(VarLoc.VariableID);
        FnVarLocs->addSingleLocVar(Var, VarLoc.Expr, VarLoc.DL, VarLoc.Values);
        InsertedAnyIntrinsics = true;
      }
    }
  }

  // Insert the other DEFs.
  for (const auto &[InsertBefore, Vec] : InsertBeforeMap) {
    SmallVector<VarLocInfo> NewDefs;
    for (const VarLocInfo &VarLoc : Vec) {
      DebugVariable Var = FnVarLocs->getVariable(VarLoc.VariableID);
      DebugAggregate Aggr{Var.getVariable(), Var.getInlinedAt()};
      // If this variable is always stack homed then we have already inserted a
      // dbg.declare and deleted this dbg.value.
      if (AlwaysStackHomed.contains(Aggr))
        continue;
      NewDefs.push_back(VarLoc);
      InsertedAnyIntrinsics = true;
    }

    FnVarLocs->setWedge(InsertBefore, std::move(NewDefs));
  }

  InsertedAnyIntrinsics |= emitPromotedVarLocs(FnVarLocs);

  return InsertedAnyIntrinsics;
}

bool AssignmentTrackingLowering::emitPromotedVarLocs(
    FunctionVarLocsBuilder *FnVarLocs) {
  bool InsertedAnyIntrinsics = false;
  // Go through every block, translating debug intrinsics for fully promoted
  // variables into FnVarLocs location defs. No analysis required for these.
  auto TranslateDbgRecord = [&](auto *Record) {
    // Skip variables that haven't been promoted - we've dealt with those
    // already.
    if (VarsWithStackSlot->contains(getAggregate(Record)))
      return;
    auto InsertBefore = getNextNode(Record);
    assert(InsertBefore && "Unexpected: debug intrinsics after a terminator");
    FnVarLocs->addVarLoc(InsertBefore, DebugVariable(Record),
                         Record->getExpression(), Record->getDebugLoc(),
                         RawLocationWrapper(Record->getRawLocation()));
    InsertedAnyIntrinsics = true;
  };
  for (auto &BB : Fn) {
    for (auto &I : BB) {
      // Skip instructions other than dbg.values and dbg.assigns.
      for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
        if (DVR.isDbgValue() || DVR.isDbgAssign())
          TranslateDbgRecord(&DVR);
      auto *DVI = dyn_cast<DbgValueInst>(&I);
      if (DVI)
        TranslateDbgRecord(DVI);
    }
  }
  return InsertedAnyIntrinsics;
}

/// Remove redundant definitions within sequences of consecutive location defs.
/// This is done using a backward scan to keep the last def describing a
/// specific variable/fragment.
///
/// This implements removeRedundantDbgInstrsUsingBackwardScan from
/// lib/Transforms/Utils/BasicBlockUtils.cpp for locations described with
/// FunctionVarLocsBuilder instead of with intrinsics.
static bool
removeRedundantDbgLocsUsingBackwardScan(const BasicBlock *BB,
                                        FunctionVarLocsBuilder &FnVarLocs) {
  bool Changed = false;
  SmallDenseMap<DebugAggregate, BitVector> VariableDefinedBytes;
  // Scan over the entire block, not just over the instructions mapped by
  // FnVarLocs, because wedges in FnVarLocs may only be separated by debug
  // instructions.
  for (const Instruction &I : reverse(*BB)) {
    if (!isa<DbgVariableIntrinsic>(I)) {
      // Sequence of consecutive defs ended. Clear map for the next one.
      VariableDefinedBytes.clear();
    }

    auto HandleLocsForWedge = [&](auto *WedgePosition) {
      // Get the location defs that start just before this instruction.
      const auto *Locs = FnVarLocs.getWedge(WedgePosition);
      if (!Locs)
        return;

      NumWedgesScanned++;
      bool ChangedThisWedge = false;
      // The new pruned set of defs, reversed because we're scanning backwards.
      SmallVector<VarLocInfo> NewDefsReversed;

      // Iterate over the existing defs in reverse.
      for (auto RIt = Locs->rbegin(), REnd = Locs->rend(); RIt != REnd; ++RIt) {
        NumDefsScanned++;
        DebugAggregate Aggr =
            getAggregate(FnVarLocs.getVariable(RIt->VariableID));
        uint64_t SizeInBits = Aggr.first->getSizeInBits().value_or(0);
        uint64_t SizeInBytes = divideCeil(SizeInBits, 8);

        // Cutoff for large variables to prevent expensive bitvector operations.
        const uint64_t MaxSizeBytes = 2048;

        if (SizeInBytes == 0 || SizeInBytes > MaxSizeBytes) {
          // If the size is unknown (0) then keep this location def to be safe.
          // Do the same for defs of large variables, which would be expensive
          // to represent with a BitVector.
          NewDefsReversed.push_back(*RIt);
          continue;
        }

        // Only keep this location definition if it is not fully eclipsed by
        // other definitions in this wedge that come after it

        // Inert the bytes the location definition defines.
        auto InsertResult =
            VariableDefinedBytes.try_emplace(Aggr, BitVector(SizeInBytes));
        bool FirstDefinition = InsertResult.second;
        BitVector &DefinedBytes = InsertResult.first->second;

        DIExpression::FragmentInfo Fragment =
            RIt->Expr->getFragmentInfo().value_or(
                DIExpression::FragmentInfo(SizeInBits, 0));
        bool InvalidFragment = Fragment.endInBits() > SizeInBits;
        uint64_t StartInBytes = Fragment.startInBits() / 8;
        uint64_t EndInBytes = divideCeil(Fragment.endInBits(), 8);

        // If this defines any previously undefined bytes, keep it.
        if (FirstDefinition || InvalidFragment ||
            DefinedBytes.find_first_unset_in(StartInBytes, EndInBytes) != -1) {
          if (!InvalidFragment)
            DefinedBytes.set(StartInBytes, EndInBytes);
          NewDefsReversed.push_back(*RIt);
          continue;
        }

        // Redundant def found: throw it away. Since the wedge of defs is being
        // rebuilt, doing nothing is the same as deleting an entry.
        ChangedThisWedge = true;
        NumDefsRemoved++;
      }

      // Un-reverse the defs and replace the wedge with the pruned version.
      if (ChangedThisWedge) {
        std::reverse(NewDefsReversed.begin(), NewDefsReversed.end());
        FnVarLocs.setWedge(WedgePosition, std::move(NewDefsReversed));
        NumWedgesChanged++;
        Changed = true;
      }
    };
    HandleLocsForWedge(&I);
    for (DbgVariableRecord &DVR : reverse(filterDbgVars(I.getDbgRecordRange())))
      HandleLocsForWedge(&DVR);
  }

  return Changed;
}

/// Remove redundant location defs using a forward scan. This can remove a
/// location definition that is redundant due to indicating that a variable has
/// the same value as is already being indicated by an earlier def.
///
/// This implements removeRedundantDbgInstrsUsingForwardScan from
/// lib/Transforms/Utils/BasicBlockUtils.cpp for locations described with
/// FunctionVarLocsBuilder instead of with intrinsics
static bool
removeRedundantDbgLocsUsingForwardScan(const BasicBlock *BB,
                                       FunctionVarLocsBuilder &FnVarLocs) {
  bool Changed = false;
  DenseMap<DebugVariable, std::pair<RawLocationWrapper, DIExpression *>>
      VariableMap;

  // Scan over the entire block, not just over the instructions mapped by
  // FnVarLocs, because wedges in FnVarLocs may only be separated by debug
  // instructions.
  for (const Instruction &I : *BB) {
    // Get the defs that come just before this instruction.
    auto HandleLocsForWedge = [&](auto *WedgePosition) {
      const auto *Locs = FnVarLocs.getWedge(WedgePosition);
      if (!Locs)
        return;

      NumWedgesScanned++;
      bool ChangedThisWedge = false;
      // The new pruned set of defs.
      SmallVector<VarLocInfo> NewDefs;

      // Iterate over the existing defs.
      for (const VarLocInfo &Loc : *Locs) {
        NumDefsScanned++;
        DebugVariable Key(FnVarLocs.getVariable(Loc.VariableID).getVariable(),
                          std::nullopt, Loc.DL.getInlinedAt());
        auto VMI = VariableMap.find(Key);

        // Update the map if we found a new value/expression describing the
        // variable, or if the variable wasn't mapped already.
        if (VMI == VariableMap.end() || VMI->second.first != Loc.Values ||
            VMI->second.second != Loc.Expr) {
          VariableMap[Key] = {Loc.Values, Loc.Expr};
          NewDefs.push_back(Loc);
          continue;
        }

        // Did not insert this Loc, which is the same as removing it.
        ChangedThisWedge = true;
        NumDefsRemoved++;
      }

      // Replace the existing wedge with the pruned version.
      if (ChangedThisWedge) {
        FnVarLocs.setWedge(WedgePosition, std::move(NewDefs));
        NumWedgesChanged++;
        Changed = true;
      }
    };

    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
      HandleLocsForWedge(&DVR);
    HandleLocsForWedge(&I);
  }

  return Changed;
}

static bool
removeUndefDbgLocsFromEntryBlock(const BasicBlock *BB,
                                 FunctionVarLocsBuilder &FnVarLocs) {
  assert(BB->isEntryBlock());
  // Do extra work to ensure that we remove semantically unimportant undefs.
  //
  // This is to work around the fact that SelectionDAG will hoist dbg.values
  // using argument values to the top of the entry block. That can move arg
  // dbg.values before undef and constant dbg.values which they previously
  // followed. The easiest thing to do is to just try to feed SelectionDAG
  // input it's happy with.
  //
  // Map of {Variable x: Fragments y} where the fragments y of variable x have
  // have at least one non-undef location defined already. Don't use directly,
  // instead call DefineBits and HasDefinedBits.
  SmallDenseMap<DebugAggregate, SmallDenseSet<DIExpression::FragmentInfo>>
      VarsWithDef;
  // Specify that V (a fragment of A) has a non-undef location.
  auto DefineBits = [&VarsWithDef](DebugAggregate A, DebugVariable V) {
    VarsWithDef[A].insert(V.getFragmentOrDefault());
  };
  // Return true if a non-undef location has been defined for V (a fragment of
  // A). Doesn't imply that the location is currently non-undef, just that a
  // non-undef location has been seen previously.
  auto HasDefinedBits = [&VarsWithDef](DebugAggregate A, DebugVariable V) {
    auto FragsIt = VarsWithDef.find(A);
    if (FragsIt == VarsWithDef.end())
      return false;
    return llvm::any_of(FragsIt->second, [V](auto Frag) {
      return DIExpression::fragmentsOverlap(Frag, V.getFragmentOrDefault());
    });
  };

  bool Changed = false;
  DenseMap<DebugVariable, std::pair<Value *, DIExpression *>> VariableMap;

  // Scan over the entire block, not just over the instructions mapped by
  // FnVarLocs, because wedges in FnVarLocs may only be separated by debug
  // instructions.
  for (const Instruction &I : *BB) {
    // Get the defs that come just before this instruction.
    auto HandleLocsForWedge = [&](auto *WedgePosition) {
      const auto *Locs = FnVarLocs.getWedge(WedgePosition);
      if (!Locs)
        return;

      NumWedgesScanned++;
      bool ChangedThisWedge = false;
      // The new pruned set of defs.
      SmallVector<VarLocInfo> NewDefs;

      // Iterate over the existing defs.
      for (const VarLocInfo &Loc : *Locs) {
        NumDefsScanned++;
        DebugAggregate Aggr{FnVarLocs.getVariable(Loc.VariableID).getVariable(),
                            Loc.DL.getInlinedAt()};
        DebugVariable Var = FnVarLocs.getVariable(Loc.VariableID);

        // Remove undef entries that are encountered before any non-undef
        // intrinsics from the entry block.
        if (Loc.Values.isKillLocation(Loc.Expr) && !HasDefinedBits(Aggr, Var)) {
          // Did not insert this Loc, which is the same as removing it.
          NumDefsRemoved++;
          ChangedThisWedge = true;
          continue;
        }

        DefineBits(Aggr, Var);
        NewDefs.push_back(Loc);
      }

      // Replace the existing wedge with the pruned version.
      if (ChangedThisWedge) {
        FnVarLocs.setWedge(WedgePosition, std::move(NewDefs));
        NumWedgesChanged++;
        Changed = true;
      }
    };
    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
      HandleLocsForWedge(&DVR);
    HandleLocsForWedge(&I);
  }

  return Changed;
}

static bool removeRedundantDbgLocs(const BasicBlock *BB,
                                   FunctionVarLocsBuilder &FnVarLocs) {
  bool MadeChanges = false;
  MadeChanges |= removeRedundantDbgLocsUsingBackwardScan(BB, FnVarLocs);
  if (BB->isEntryBlock())
    MadeChanges |= removeUndefDbgLocsFromEntryBlock(BB, FnVarLocs);
  MadeChanges |= removeRedundantDbgLocsUsingForwardScan(BB, FnVarLocs);

  if (MadeChanges)
    LLVM_DEBUG(dbgs() << "Removed redundant dbg locs from: " << BB->getName()
                      << "\n");
  return MadeChanges;
}

static DenseSet<DebugAggregate> findVarsWithStackSlot(Function &Fn) {
  DenseSet<DebugAggregate> Result;
  for (auto &BB : Fn) {
    for (auto &I : BB) {
      // Any variable linked to an instruction is considered
      // interesting. Ideally we only need to check Allocas, however, a
      // DIAssignID might get dropped from an alloca but not stores. In that
      // case, we need to consider the variable interesting for NFC behaviour
      // with this change. TODO: Consider only looking at allocas.
      for (DbgAssignIntrinsic *DAI : at::getAssignmentMarkers(&I)) {
        Result.insert({DAI->getVariable(), DAI->getDebugLoc().getInlinedAt()});
      }
      for (DbgVariableRecord *DVR : at::getDVRAssignmentMarkers(&I)) {
        Result.insert({DVR->getVariable(), DVR->getDebugLoc().getInlinedAt()});
      }
    }
  }
  return Result;
}

static void analyzeFunction(Function &Fn, const DataLayout &Layout,
                            FunctionVarLocsBuilder *FnVarLocs) {
  // The analysis will generate location definitions for all variables, but we
  // only need to perform a dataflow on the set of variables which have a stack
  // slot. Find those now.
  DenseSet<DebugAggregate> VarsWithStackSlot = findVarsWithStackSlot(Fn);

  bool Changed = false;

  // Use a scope block to clean up AssignmentTrackingLowering before running
  // MemLocFragmentFill to reduce peak memory consumption.
  {
    AssignmentTrackingLowering Pass(Fn, Layout, &VarsWithStackSlot);
    Changed = Pass.run(FnVarLocs);
  }

  if (Changed) {
    MemLocFragmentFill Pass(Fn, &VarsWithStackSlot,
                            shouldCoalesceFragments(Fn));
    Pass.run(FnVarLocs);

    // Remove redundant entries. As well as reducing memory consumption and
    // avoiding waiting cycles later by burning some now, this has another
    // important job. That is to work around some SelectionDAG quirks. See
    // removeRedundantDbgLocsUsingForwardScan comments for more info on that.
    for (auto &BB : Fn)
      removeRedundantDbgLocs(&BB, *FnVarLocs);
  }
}

FunctionVarLocs
DebugAssignmentTrackingAnalysis::run(Function &F,
                                     FunctionAnalysisManager &FAM) {
  if (!isAssignmentTrackingEnabled(*F.getParent()))
    return FunctionVarLocs();

  auto &DL = F.getDataLayout();

  FunctionVarLocsBuilder Builder;
  analyzeFunction(F, DL, &Builder);

  // Save these results.
  FunctionVarLocs Results;
  Results.init(Builder);
  return Results;
}

AnalysisKey DebugAssignmentTrackingAnalysis::Key;

PreservedAnalyses
DebugAssignmentTrackingPrinterPass::run(Function &F,
                                        FunctionAnalysisManager &FAM) {
  FAM.getResult<DebugAssignmentTrackingAnalysis>(F).print(OS, F);
  return PreservedAnalyses::all();
}

bool AssignmentTrackingAnalysis::runOnFunction(Function &F) {
  if (!isAssignmentTrackingEnabled(*F.getParent()))
    return false;

  LLVM_DEBUG(dbgs() << "AssignmentTrackingAnalysis run on " << F.getName()
                    << "\n");
  auto DL = std::make_unique<DataLayout>(F.getParent());

  // Clear previous results.
  Results->clear();

  FunctionVarLocsBuilder Builder;
  analyzeFunction(F, *DL.get(), &Builder);

  // Save these results.
  Results->init(Builder);

  if (PrintResults && isFunctionInPrintList(F.getName()))
    Results->print(errs(), F);

  // Return false because this pass does not modify the function.
  return false;
}

AssignmentTrackingAnalysis::AssignmentTrackingAnalysis()
    : FunctionPass(ID), Results(std::make_unique<FunctionVarLocs>()) {}

char AssignmentTrackingAnalysis::ID = 0;

INITIALIZE_PASS(AssignmentTrackingAnalysis, DEBUG_TYPE,
                "Assignment Tracking Analysis", false, true)
