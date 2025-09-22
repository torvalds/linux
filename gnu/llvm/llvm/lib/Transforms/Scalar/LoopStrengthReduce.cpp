//===- LoopStrengthReduce.cpp - Strength Reduce IVs in Loops --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This transformation analyzes and transforms the induction variables (and
// computations derived from them) into forms suitable for efficient execution
// on the target.
//
// This pass performs a strength reduction on array references inside loops that
// have as one or more of their components the loop induction variable, it
// rewrites expressions to take advantage of scaled-index addressing modes
// available on the target, and it performs a variety of other optimizations
// related to loop induction variables.
//
// Terminology note: this code has a lot of handling for "post-increment" or
// "post-inc" users. This is not talking about post-increment addressing modes;
// it is instead talking about code like this:
//
//   %i = phi [ 0, %entry ], [ %i.next, %latch ]
//   ...
//   %i.next = add %i, 1
//   %c = icmp eq %i.next, %n
//
// The SCEV for %i is {0,+,1}<%L>. The SCEV for %i.next is {1,+,1}<%L>, however
// it's useful to think about these as the same register, with some uses using
// the value of the register before the add and some using it after. In this
// example, the icmp is a post-increment user, since it uses %i.next, which is
// the value of the induction variable after the increment. The other common
// case of post-increment users is users outside the loop.
//
// TODO: More sophistication in the way Formulae are generated and filtered.
//
// TODO: Handle multiple loops at a time.
//
// TODO: Should the addressing mode BaseGV be changed to a ConstantExpr instead
//       of a GlobalValue?
//
// TODO: When truncation is free, truncate ICmp users' operands to make it a
//       smaller encoding (on x86 at least).
//
// TODO: When a negated register is used by an add (such as in a list of
//       multiple base registers, or as the increment expression in an addrec),
//       we may not actually need both reg and (-1 * reg) in registers; the
//       negation can be implemented by using a sub instead of an add. The
//       lack of support for taking this into consideration when making
//       register pressure decisions is partly worked around by the "Special"
//       use kind.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/LoopStrengthReduce.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/IVUsers.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolutionNormalization.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "loop-reduce"

/// MaxIVUsers is an arbitrary threshold that provides an early opportunity for
/// bail out. This threshold is far beyond the number of users that LSR can
/// conceivably solve, so it should not affect generated code, but catches the
/// worst cases before LSR burns too much compile time and stack space.
static const unsigned MaxIVUsers = 200;

/// Limit the size of expression that SCEV-based salvaging will attempt to
/// translate into a DIExpression.
/// Choose a maximum size such that debuginfo is not excessively increased and
/// the salvaging is not too expensive for the compiler.
static const unsigned MaxSCEVSalvageExpressionSize = 64;

// Cleanup congruent phis after LSR phi expansion.
static cl::opt<bool> EnablePhiElim(
  "enable-lsr-phielim", cl::Hidden, cl::init(true),
  cl::desc("Enable LSR phi elimination"));

// The flag adds instruction count to solutions cost comparison.
static cl::opt<bool> InsnsCost(
  "lsr-insns-cost", cl::Hidden, cl::init(true),
  cl::desc("Add instruction count to a LSR cost model"));

// Flag to choose how to narrow complex lsr solution
static cl::opt<bool> LSRExpNarrow(
  "lsr-exp-narrow", cl::Hidden, cl::init(false),
  cl::desc("Narrow LSR complex solution using"
           " expectation of registers number"));

// Flag to narrow search space by filtering non-optimal formulae with
// the same ScaledReg and Scale.
static cl::opt<bool> FilterSameScaledReg(
    "lsr-filter-same-scaled-reg", cl::Hidden, cl::init(true),
    cl::desc("Narrow LSR search space by filtering non-optimal formulae"
             " with the same ScaledReg and Scale"));

static cl::opt<TTI::AddressingModeKind> PreferredAddresingMode(
  "lsr-preferred-addressing-mode", cl::Hidden, cl::init(TTI::AMK_None),
   cl::desc("A flag that overrides the target's preferred addressing mode."),
   cl::values(clEnumValN(TTI::AMK_None,
                         "none",
                         "Don't prefer any addressing mode"),
              clEnumValN(TTI::AMK_PreIndexed,
                         "preindexed",
                         "Prefer pre-indexed addressing mode"),
              clEnumValN(TTI::AMK_PostIndexed,
                         "postindexed",
                         "Prefer post-indexed addressing mode")));

static cl::opt<unsigned> ComplexityLimit(
  "lsr-complexity-limit", cl::Hidden,
  cl::init(std::numeric_limits<uint16_t>::max()),
  cl::desc("LSR search space complexity limit"));

static cl::opt<unsigned> SetupCostDepthLimit(
    "lsr-setupcost-depth-limit", cl::Hidden, cl::init(7),
    cl::desc("The limit on recursion depth for LSRs setup cost"));

static cl::opt<cl::boolOrDefault> AllowTerminatingConditionFoldingAfterLSR(
    "lsr-term-fold", cl::Hidden,
    cl::desc("Attempt to replace primary IV with other IV."));

static cl::opt<cl::boolOrDefault> AllowDropSolutionIfLessProfitable(
    "lsr-drop-solution", cl::Hidden,
    cl::desc("Attempt to drop solution if it is less profitable"));

static cl::opt<bool> EnableVScaleImmediates(
    "lsr-enable-vscale-immediates", cl::Hidden, cl::init(true),
    cl::desc("Enable analysis of vscale-relative immediates in LSR"));

static cl::opt<bool> DropScaledForVScale(
    "lsr-drop-scaled-reg-for-vscale", cl::Hidden, cl::init(true),
    cl::desc("Avoid using scaled registers with vscale-relative addressing"));

STATISTIC(NumTermFold,
          "Number of terminating condition fold recognized and performed");

#ifndef NDEBUG
// Stress test IV chain generation.
static cl::opt<bool> StressIVChain(
  "stress-ivchain", cl::Hidden, cl::init(false),
  cl::desc("Stress test LSR IV chains"));
#else
static bool StressIVChain = false;
#endif

namespace {

struct MemAccessTy {
  /// Used in situations where the accessed memory type is unknown.
  static const unsigned UnknownAddressSpace =
      std::numeric_limits<unsigned>::max();

  Type *MemTy = nullptr;
  unsigned AddrSpace = UnknownAddressSpace;

  MemAccessTy() = default;
  MemAccessTy(Type *Ty, unsigned AS) : MemTy(Ty), AddrSpace(AS) {}

  bool operator==(MemAccessTy Other) const {
    return MemTy == Other.MemTy && AddrSpace == Other.AddrSpace;
  }

  bool operator!=(MemAccessTy Other) const { return !(*this == Other); }

  static MemAccessTy getUnknown(LLVMContext &Ctx,
                                unsigned AS = UnknownAddressSpace) {
    return MemAccessTy(Type::getVoidTy(Ctx), AS);
  }

  Type *getType() { return MemTy; }
};

/// This class holds data which is used to order reuse candidates.
class RegSortData {
public:
  /// This represents the set of LSRUse indices which reference
  /// a particular register.
  SmallBitVector UsedByIndices;

  void print(raw_ostream &OS) const;
  void dump() const;
};

// An offset from an address that is either scalable or fixed. Used for
// per-target optimizations of addressing modes.
class Immediate : public details::FixedOrScalableQuantity<Immediate, int64_t> {
  constexpr Immediate(ScalarTy MinVal, bool Scalable)
      : FixedOrScalableQuantity(MinVal, Scalable) {}

  constexpr Immediate(const FixedOrScalableQuantity<Immediate, int64_t> &V)
      : FixedOrScalableQuantity(V) {}

public:
  constexpr Immediate() = delete;

  static constexpr Immediate getFixed(ScalarTy MinVal) {
    return {MinVal, false};
  }
  static constexpr Immediate getScalable(ScalarTy MinVal) {
    return {MinVal, true};
  }
  static constexpr Immediate get(ScalarTy MinVal, bool Scalable) {
    return {MinVal, Scalable};
  }
  static constexpr Immediate getZero() { return {0, false}; }
  static constexpr Immediate getFixedMin() {
    return {std::numeric_limits<int64_t>::min(), false};
  }
  static constexpr Immediate getFixedMax() {
    return {std::numeric_limits<int64_t>::max(), false};
  }
  static constexpr Immediate getScalableMin() {
    return {std::numeric_limits<int64_t>::min(), true};
  }
  static constexpr Immediate getScalableMax() {
    return {std::numeric_limits<int64_t>::max(), true};
  }

  constexpr bool isLessThanZero() const { return Quantity < 0; }

  constexpr bool isGreaterThanZero() const { return Quantity > 0; }

  constexpr bool isCompatibleImmediate(const Immediate &Imm) const {
    return isZero() || Imm.isZero() || Imm.Scalable == Scalable;
  }

  constexpr bool isMin() const {
    return Quantity == std::numeric_limits<ScalarTy>::min();
  }

  constexpr bool isMax() const {
    return Quantity == std::numeric_limits<ScalarTy>::max();
  }

  // Arithmetic 'operators' that cast to unsigned types first.
  constexpr Immediate addUnsigned(const Immediate &RHS) const {
    assert(isCompatibleImmediate(RHS) && "Incompatible Immediates");
    ScalarTy Value = (uint64_t)Quantity + RHS.getKnownMinValue();
    return {Value, Scalable || RHS.isScalable()};
  }

  constexpr Immediate subUnsigned(const Immediate &RHS) const {
    assert(isCompatibleImmediate(RHS) && "Incompatible Immediates");
    ScalarTy Value = (uint64_t)Quantity - RHS.getKnownMinValue();
    return {Value, Scalable || RHS.isScalable()};
  }

  // Scale the quantity by a constant without caring about runtime scalability.
  constexpr Immediate mulUnsigned(const ScalarTy RHS) const {
    ScalarTy Value = (uint64_t)Quantity * RHS;
    return {Value, Scalable};
  }

  // Helpers for generating SCEVs with vscale terms where needed.
  const SCEV *getSCEV(ScalarEvolution &SE, Type *Ty) const {
    const SCEV *S = SE.getConstant(Ty, Quantity);
    if (Scalable)
      S = SE.getMulExpr(S, SE.getVScale(S->getType()));
    return S;
  }

  const SCEV *getNegativeSCEV(ScalarEvolution &SE, Type *Ty) const {
    const SCEV *NegS = SE.getConstant(Ty, -(uint64_t)Quantity);
    if (Scalable)
      NegS = SE.getMulExpr(NegS, SE.getVScale(NegS->getType()));
    return NegS;
  }

  const SCEV *getUnknownSCEV(ScalarEvolution &SE, Type *Ty) const {
    const SCEV *SU = SE.getUnknown(ConstantInt::getSigned(Ty, Quantity));
    if (Scalable)
      SU = SE.getMulExpr(SU, SE.getVScale(SU->getType()));
    return SU;
  }
};

// This is needed for the Compare type of std::map when Immediate is used
// as a key. We don't need it to be fully correct against any value of vscale,
// just to make sure that vscale-related terms in the map are considered against
// each other rather than being mixed up and potentially missing opportunities.
struct KeyOrderTargetImmediate {
  bool operator()(const Immediate &LHS, const Immediate &RHS) const {
    if (LHS.isScalable() && !RHS.isScalable())
      return false;
    if (!LHS.isScalable() && RHS.isScalable())
      return true;
    return LHS.getKnownMinValue() < RHS.getKnownMinValue();
  }
};

// This would be nicer if we could be generic instead of directly using size_t,
// but there doesn't seem to be a type trait for is_orderable or
// is_lessthan_comparable or similar.
struct KeyOrderSizeTAndImmediate {
  bool operator()(const std::pair<size_t, Immediate> &LHS,
                  const std::pair<size_t, Immediate> &RHS) const {
    size_t LSize = LHS.first;
    size_t RSize = RHS.first;
    if (LSize != RSize)
      return LSize < RSize;
    return KeyOrderTargetImmediate()(LHS.second, RHS.second);
  }
};
} // end anonymous namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void RegSortData::print(raw_ostream &OS) const {
  OS << "[NumUses=" << UsedByIndices.count() << ']';
}

LLVM_DUMP_METHOD void RegSortData::dump() const {
  print(errs()); errs() << '\n';
}
#endif

namespace {

/// Map register candidates to information about how they are used.
class RegUseTracker {
  using RegUsesTy = DenseMap<const SCEV *, RegSortData>;

  RegUsesTy RegUsesMap;
  SmallVector<const SCEV *, 16> RegSequence;

public:
  void countRegister(const SCEV *Reg, size_t LUIdx);
  void dropRegister(const SCEV *Reg, size_t LUIdx);
  void swapAndDropUse(size_t LUIdx, size_t LastLUIdx);

  bool isRegUsedByUsesOtherThan(const SCEV *Reg, size_t LUIdx) const;

  const SmallBitVector &getUsedByIndices(const SCEV *Reg) const;

  void clear();

  using iterator = SmallVectorImpl<const SCEV *>::iterator;
  using const_iterator = SmallVectorImpl<const SCEV *>::const_iterator;

  iterator begin() { return RegSequence.begin(); }
  iterator end()   { return RegSequence.end(); }
  const_iterator begin() const { return RegSequence.begin(); }
  const_iterator end() const   { return RegSequence.end(); }
};

} // end anonymous namespace

void
RegUseTracker::countRegister(const SCEV *Reg, size_t LUIdx) {
  std::pair<RegUsesTy::iterator, bool> Pair =
    RegUsesMap.insert(std::make_pair(Reg, RegSortData()));
  RegSortData &RSD = Pair.first->second;
  if (Pair.second)
    RegSequence.push_back(Reg);
  RSD.UsedByIndices.resize(std::max(RSD.UsedByIndices.size(), LUIdx + 1));
  RSD.UsedByIndices.set(LUIdx);
}

void
RegUseTracker::dropRegister(const SCEV *Reg, size_t LUIdx) {
  RegUsesTy::iterator It = RegUsesMap.find(Reg);
  assert(It != RegUsesMap.end());
  RegSortData &RSD = It->second;
  assert(RSD.UsedByIndices.size() > LUIdx);
  RSD.UsedByIndices.reset(LUIdx);
}

void
RegUseTracker::swapAndDropUse(size_t LUIdx, size_t LastLUIdx) {
  assert(LUIdx <= LastLUIdx);

  // Update RegUses. The data structure is not optimized for this purpose;
  // we must iterate through it and update each of the bit vectors.
  for (auto &Pair : RegUsesMap) {
    SmallBitVector &UsedByIndices = Pair.second.UsedByIndices;
    if (LUIdx < UsedByIndices.size())
      UsedByIndices[LUIdx] =
        LastLUIdx < UsedByIndices.size() ? UsedByIndices[LastLUIdx] : false;
    UsedByIndices.resize(std::min(UsedByIndices.size(), LastLUIdx));
  }
}

bool
RegUseTracker::isRegUsedByUsesOtherThan(const SCEV *Reg, size_t LUIdx) const {
  RegUsesTy::const_iterator I = RegUsesMap.find(Reg);
  if (I == RegUsesMap.end())
    return false;
  const SmallBitVector &UsedByIndices = I->second.UsedByIndices;
  int i = UsedByIndices.find_first();
  if (i == -1) return false;
  if ((size_t)i != LUIdx) return true;
  return UsedByIndices.find_next(i) != -1;
}

const SmallBitVector &RegUseTracker::getUsedByIndices(const SCEV *Reg) const {
  RegUsesTy::const_iterator I = RegUsesMap.find(Reg);
  assert(I != RegUsesMap.end() && "Unknown register!");
  return I->second.UsedByIndices;
}

void RegUseTracker::clear() {
  RegUsesMap.clear();
  RegSequence.clear();
}

namespace {

/// This class holds information that describes a formula for computing
/// satisfying a use. It may include broken-out immediates and scaled registers.
struct Formula {
  /// Global base address used for complex addressing.
  GlobalValue *BaseGV = nullptr;

  /// Base offset for complex addressing.
  Immediate BaseOffset = Immediate::getZero();

  /// Whether any complex addressing has a base register.
  bool HasBaseReg = false;

  /// The scale of any complex addressing.
  int64_t Scale = 0;

  /// The list of "base" registers for this use. When this is non-empty. The
  /// canonical representation of a formula is
  /// 1. BaseRegs.size > 1 implies ScaledReg != NULL and
  /// 2. ScaledReg != NULL implies Scale != 1 || !BaseRegs.empty().
  /// 3. The reg containing recurrent expr related with currect loop in the
  /// formula should be put in the ScaledReg.
  /// #1 enforces that the scaled register is always used when at least two
  /// registers are needed by the formula: e.g., reg1 + reg2 is reg1 + 1 * reg2.
  /// #2 enforces that 1 * reg is reg.
  /// #3 ensures invariant regs with respect to current loop can be combined
  /// together in LSR codegen.
  /// This invariant can be temporarily broken while building a formula.
  /// However, every formula inserted into the LSRInstance must be in canonical
  /// form.
  SmallVector<const SCEV *, 4> BaseRegs;

  /// The 'scaled' register for this use. This should be non-null when Scale is
  /// not zero.
  const SCEV *ScaledReg = nullptr;

  /// An additional constant offset which added near the use. This requires a
  /// temporary register, but the offset itself can live in an add immediate
  /// field rather than a register.
  Immediate UnfoldedOffset = Immediate::getZero();

  Formula() = default;

  void initialMatch(const SCEV *S, Loop *L, ScalarEvolution &SE);

  bool isCanonical(const Loop &L) const;

  void canonicalize(const Loop &L);

  bool unscale();

  bool hasZeroEnd() const;

  size_t getNumRegs() const;
  Type *getType() const;

  void deleteBaseReg(const SCEV *&S);

  bool referencesReg(const SCEV *S) const;
  bool hasRegsUsedByUsesOtherThan(size_t LUIdx,
                                  const RegUseTracker &RegUses) const;

  void print(raw_ostream &OS) const;
  void dump() const;
};

} // end anonymous namespace

/// Recursion helper for initialMatch.
static void DoInitialMatch(const SCEV *S, Loop *L,
                           SmallVectorImpl<const SCEV *> &Good,
                           SmallVectorImpl<const SCEV *> &Bad,
                           ScalarEvolution &SE) {
  // Collect expressions which properly dominate the loop header.
  if (SE.properlyDominates(S, L->getHeader())) {
    Good.push_back(S);
    return;
  }

  // Look at add operands.
  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    for (const SCEV *S : Add->operands())
      DoInitialMatch(S, L, Good, Bad, SE);
    return;
  }

  // Look at addrec operands.
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S))
    if (!AR->getStart()->isZero() && AR->isAffine()) {
      DoInitialMatch(AR->getStart(), L, Good, Bad, SE);
      DoInitialMatch(SE.getAddRecExpr(SE.getConstant(AR->getType(), 0),
                                      AR->getStepRecurrence(SE),
                                      // FIXME: AR->getNoWrapFlags()
                                      AR->getLoop(), SCEV::FlagAnyWrap),
                     L, Good, Bad, SE);
      return;
    }

  // Handle a multiplication by -1 (negation) if it didn't fold.
  if (const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(S))
    if (Mul->getOperand(0)->isAllOnesValue()) {
      SmallVector<const SCEV *, 4> Ops(drop_begin(Mul->operands()));
      const SCEV *NewMul = SE.getMulExpr(Ops);

      SmallVector<const SCEV *, 4> MyGood;
      SmallVector<const SCEV *, 4> MyBad;
      DoInitialMatch(NewMul, L, MyGood, MyBad, SE);
      const SCEV *NegOne = SE.getSCEV(ConstantInt::getAllOnesValue(
        SE.getEffectiveSCEVType(NewMul->getType())));
      for (const SCEV *S : MyGood)
        Good.push_back(SE.getMulExpr(NegOne, S));
      for (const SCEV *S : MyBad)
        Bad.push_back(SE.getMulExpr(NegOne, S));
      return;
    }

  // Ok, we can't do anything interesting. Just stuff the whole thing into a
  // register and hope for the best.
  Bad.push_back(S);
}

/// Incorporate loop-variant parts of S into this Formula, attempting to keep
/// all loop-invariant and loop-computable values in a single base register.
void Formula::initialMatch(const SCEV *S, Loop *L, ScalarEvolution &SE) {
  SmallVector<const SCEV *, 4> Good;
  SmallVector<const SCEV *, 4> Bad;
  DoInitialMatch(S, L, Good, Bad, SE);
  if (!Good.empty()) {
    const SCEV *Sum = SE.getAddExpr(Good);
    if (!Sum->isZero())
      BaseRegs.push_back(Sum);
    HasBaseReg = true;
  }
  if (!Bad.empty()) {
    const SCEV *Sum = SE.getAddExpr(Bad);
    if (!Sum->isZero())
      BaseRegs.push_back(Sum);
    HasBaseReg = true;
  }
  canonicalize(*L);
}

static bool containsAddRecDependentOnLoop(const SCEV *S, const Loop &L) {
  return SCEVExprContains(S, [&L](const SCEV *S) {
    return isa<SCEVAddRecExpr>(S) && (cast<SCEVAddRecExpr>(S)->getLoop() == &L);
  });
}

/// Check whether or not this formula satisfies the canonical
/// representation.
/// \see Formula::BaseRegs.
bool Formula::isCanonical(const Loop &L) const {
  if (!ScaledReg)
    return BaseRegs.size() <= 1;

  if (Scale != 1)
    return true;

  if (Scale == 1 && BaseRegs.empty())
    return false;

  if (containsAddRecDependentOnLoop(ScaledReg, L))
    return true;

  // If ScaledReg is not a recurrent expr, or it is but its loop is not current
  // loop, meanwhile BaseRegs contains a recurrent expr reg related with current
  // loop, we want to swap the reg in BaseRegs with ScaledReg.
  return none_of(BaseRegs, [&L](const SCEV *S) {
    return containsAddRecDependentOnLoop(S, L);
  });
}

/// Helper method to morph a formula into its canonical representation.
/// \see Formula::BaseRegs.
/// Every formula having more than one base register, must use the ScaledReg
/// field. Otherwise, we would have to do special cases everywhere in LSR
/// to treat reg1 + reg2 + ... the same way as reg1 + 1*reg2 + ...
/// On the other hand, 1*reg should be canonicalized into reg.
void Formula::canonicalize(const Loop &L) {
  if (isCanonical(L))
    return;

  if (BaseRegs.empty()) {
    // No base reg? Use scale reg with scale = 1 as such.
    assert(ScaledReg && "Expected 1*reg => reg");
    assert(Scale == 1 && "Expected 1*reg => reg");
    BaseRegs.push_back(ScaledReg);
    Scale = 0;
    ScaledReg = nullptr;
    return;
  }

  // Keep the invariant sum in BaseRegs and one of the variant sum in ScaledReg.
  if (!ScaledReg) {
    ScaledReg = BaseRegs.pop_back_val();
    Scale = 1;
  }

  // If ScaledReg is an invariant with respect to L, find the reg from
  // BaseRegs containing the recurrent expr related with Loop L. Swap the
  // reg with ScaledReg.
  if (!containsAddRecDependentOnLoop(ScaledReg, L)) {
    auto I = find_if(BaseRegs, [&L](const SCEV *S) {
      return containsAddRecDependentOnLoop(S, L);
    });
    if (I != BaseRegs.end())
      std::swap(ScaledReg, *I);
  }
  assert(isCanonical(L) && "Failed to canonicalize?");
}

/// Get rid of the scale in the formula.
/// In other words, this method morphes reg1 + 1*reg2 into reg1 + reg2.
/// \return true if it was possible to get rid of the scale, false otherwise.
/// \note After this operation the formula may not be in the canonical form.
bool Formula::unscale() {
  if (Scale != 1)
    return false;
  Scale = 0;
  BaseRegs.push_back(ScaledReg);
  ScaledReg = nullptr;
  return true;
}

bool Formula::hasZeroEnd() const {
  if (UnfoldedOffset || BaseOffset)
    return false;
  if (BaseRegs.size() != 1 || ScaledReg)
    return false;
  return true;
}

/// Return the total number of register operands used by this formula. This does
/// not include register uses implied by non-constant addrec strides.
size_t Formula::getNumRegs() const {
  return !!ScaledReg + BaseRegs.size();
}

/// Return the type of this formula, if it has one, or null otherwise. This type
/// is meaningless except for the bit size.
Type *Formula::getType() const {
  return !BaseRegs.empty() ? BaseRegs.front()->getType() :
         ScaledReg ? ScaledReg->getType() :
         BaseGV ? BaseGV->getType() :
         nullptr;
}

/// Delete the given base reg from the BaseRegs list.
void Formula::deleteBaseReg(const SCEV *&S) {
  if (&S != &BaseRegs.back())
    std::swap(S, BaseRegs.back());
  BaseRegs.pop_back();
}

/// Test if this formula references the given register.
bool Formula::referencesReg(const SCEV *S) const {
  return S == ScaledReg || is_contained(BaseRegs, S);
}

/// Test whether this formula uses registers which are used by uses other than
/// the use with the given index.
bool Formula::hasRegsUsedByUsesOtherThan(size_t LUIdx,
                                         const RegUseTracker &RegUses) const {
  if (ScaledReg)
    if (RegUses.isRegUsedByUsesOtherThan(ScaledReg, LUIdx))
      return true;
  for (const SCEV *BaseReg : BaseRegs)
    if (RegUses.isRegUsedByUsesOtherThan(BaseReg, LUIdx))
      return true;
  return false;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void Formula::print(raw_ostream &OS) const {
  bool First = true;
  if (BaseGV) {
    if (!First) OS << " + "; else First = false;
    BaseGV->printAsOperand(OS, /*PrintType=*/false);
  }
  if (BaseOffset.isNonZero()) {
    if (!First) OS << " + "; else First = false;
    OS << BaseOffset;
  }
  for (const SCEV *BaseReg : BaseRegs) {
    if (!First) OS << " + "; else First = false;
    OS << "reg(" << *BaseReg << ')';
  }
  if (HasBaseReg && BaseRegs.empty()) {
    if (!First) OS << " + "; else First = false;
    OS << "**error: HasBaseReg**";
  } else if (!HasBaseReg && !BaseRegs.empty()) {
    if (!First) OS << " + "; else First = false;
    OS << "**error: !HasBaseReg**";
  }
  if (Scale != 0) {
    if (!First) OS << " + "; else First = false;
    OS << Scale << "*reg(";
    if (ScaledReg)
      OS << *ScaledReg;
    else
      OS << "<unknown>";
    OS << ')';
  }
  if (UnfoldedOffset.isNonZero()) {
    if (!First) OS << " + ";
    OS << "imm(" << UnfoldedOffset << ')';
  }
}

LLVM_DUMP_METHOD void Formula::dump() const {
  print(errs()); errs() << '\n';
}
#endif

/// Return true if the given addrec can be sign-extended without changing its
/// value.
static bool isAddRecSExtable(const SCEVAddRecExpr *AR, ScalarEvolution &SE) {
  Type *WideTy =
    IntegerType::get(SE.getContext(), SE.getTypeSizeInBits(AR->getType()) + 1);
  return isa<SCEVAddRecExpr>(SE.getSignExtendExpr(AR, WideTy));
}

/// Return true if the given add can be sign-extended without changing its
/// value.
static bool isAddSExtable(const SCEVAddExpr *A, ScalarEvolution &SE) {
  Type *WideTy =
    IntegerType::get(SE.getContext(), SE.getTypeSizeInBits(A->getType()) + 1);
  return isa<SCEVAddExpr>(SE.getSignExtendExpr(A, WideTy));
}

/// Return true if the given mul can be sign-extended without changing its
/// value.
static bool isMulSExtable(const SCEVMulExpr *M, ScalarEvolution &SE) {
  Type *WideTy =
    IntegerType::get(SE.getContext(),
                     SE.getTypeSizeInBits(M->getType()) * M->getNumOperands());
  return isa<SCEVMulExpr>(SE.getSignExtendExpr(M, WideTy));
}

/// Return an expression for LHS /s RHS, if it can be determined and if the
/// remainder is known to be zero, or null otherwise. If IgnoreSignificantBits
/// is true, expressions like (X * Y) /s Y are simplified to X, ignoring that
/// the multiplication may overflow, which is useful when the result will be
/// used in a context where the most significant bits are ignored.
static const SCEV *getExactSDiv(const SCEV *LHS, const SCEV *RHS,
                                ScalarEvolution &SE,
                                bool IgnoreSignificantBits = false) {
  // Handle the trivial case, which works for any SCEV type.
  if (LHS == RHS)
    return SE.getConstant(LHS->getType(), 1);

  // Handle a few RHS special cases.
  const SCEVConstant *RC = dyn_cast<SCEVConstant>(RHS);
  if (RC) {
    const APInt &RA = RC->getAPInt();
    // Handle x /s -1 as x * -1, to give ScalarEvolution a chance to do
    // some folding.
    if (RA.isAllOnes()) {
      if (LHS->getType()->isPointerTy())
        return nullptr;
      return SE.getMulExpr(LHS, RC);
    }
    // Handle x /s 1 as x.
    if (RA == 1)
      return LHS;
  }

  // Check for a division of a constant by a constant.
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(LHS)) {
    if (!RC)
      return nullptr;
    const APInt &LA = C->getAPInt();
    const APInt &RA = RC->getAPInt();
    if (LA.srem(RA) != 0)
      return nullptr;
    return SE.getConstant(LA.sdiv(RA));
  }

  // Distribute the sdiv over addrec operands, if the addrec doesn't overflow.
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(LHS)) {
    if ((IgnoreSignificantBits || isAddRecSExtable(AR, SE)) && AR->isAffine()) {
      const SCEV *Step = getExactSDiv(AR->getStepRecurrence(SE), RHS, SE,
                                      IgnoreSignificantBits);
      if (!Step) return nullptr;
      const SCEV *Start = getExactSDiv(AR->getStart(), RHS, SE,
                                       IgnoreSignificantBits);
      if (!Start) return nullptr;
      // FlagNW is independent of the start value, step direction, and is
      // preserved with smaller magnitude steps.
      // FIXME: AR->getNoWrapFlags(SCEV::FlagNW)
      return SE.getAddRecExpr(Start, Step, AR->getLoop(), SCEV::FlagAnyWrap);
    }
    return nullptr;
  }

  // Distribute the sdiv over add operands, if the add doesn't overflow.
  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(LHS)) {
    if (IgnoreSignificantBits || isAddSExtable(Add, SE)) {
      SmallVector<const SCEV *, 8> Ops;
      for (const SCEV *S : Add->operands()) {
        const SCEV *Op = getExactSDiv(S, RHS, SE, IgnoreSignificantBits);
        if (!Op) return nullptr;
        Ops.push_back(Op);
      }
      return SE.getAddExpr(Ops);
    }
    return nullptr;
  }

  // Check for a multiply operand that we can pull RHS out of.
  if (const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(LHS)) {
    if (IgnoreSignificantBits || isMulSExtable(Mul, SE)) {
      // Handle special case C1*X*Y /s C2*X*Y.
      if (const SCEVMulExpr *MulRHS = dyn_cast<SCEVMulExpr>(RHS)) {
        if (IgnoreSignificantBits || isMulSExtable(MulRHS, SE)) {
          const SCEVConstant *LC = dyn_cast<SCEVConstant>(Mul->getOperand(0));
          const SCEVConstant *RC =
              dyn_cast<SCEVConstant>(MulRHS->getOperand(0));
          if (LC && RC) {
            SmallVector<const SCEV *, 4> LOps(drop_begin(Mul->operands()));
            SmallVector<const SCEV *, 4> ROps(drop_begin(MulRHS->operands()));
            if (LOps == ROps)
              return getExactSDiv(LC, RC, SE, IgnoreSignificantBits);
          }
        }
      }

      SmallVector<const SCEV *, 4> Ops;
      bool Found = false;
      for (const SCEV *S : Mul->operands()) {
        if (!Found)
          if (const SCEV *Q = getExactSDiv(S, RHS, SE,
                                           IgnoreSignificantBits)) {
            S = Q;
            Found = true;
          }
        Ops.push_back(S);
      }
      return Found ? SE.getMulExpr(Ops) : nullptr;
    }
    return nullptr;
  }

  // Otherwise we don't know.
  return nullptr;
}

/// If S involves the addition of a constant integer value, return that integer
/// value, and mutate S to point to a new SCEV with that value excluded.
static Immediate ExtractImmediate(const SCEV *&S, ScalarEvolution &SE) {
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(S)) {
    if (C->getAPInt().getSignificantBits() <= 64) {
      S = SE.getConstant(C->getType(), 0);
      return Immediate::getFixed(C->getValue()->getSExtValue());
    }
  } else if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    SmallVector<const SCEV *, 8> NewOps(Add->operands());
    Immediate Result = ExtractImmediate(NewOps.front(), SE);
    if (Result.isNonZero())
      S = SE.getAddExpr(NewOps);
    return Result;
  } else if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    SmallVector<const SCEV *, 8> NewOps(AR->operands());
    Immediate Result = ExtractImmediate(NewOps.front(), SE);
    if (Result.isNonZero())
      S = SE.getAddRecExpr(NewOps, AR->getLoop(),
                           // FIXME: AR->getNoWrapFlags(SCEV::FlagNW)
                           SCEV::FlagAnyWrap);
    return Result;
  } else if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(S)) {
    if (EnableVScaleImmediates && M->getNumOperands() == 2) {
      if (const SCEVConstant *C = dyn_cast<SCEVConstant>(M->getOperand(0)))
        if (isa<SCEVVScale>(M->getOperand(1))) {
          S = SE.getConstant(M->getType(), 0);
          return Immediate::getScalable(C->getValue()->getSExtValue());
        }
    }
  }
  return Immediate::getZero();
}

/// If S involves the addition of a GlobalValue address, return that symbol, and
/// mutate S to point to a new SCEV with that value excluded.
static GlobalValue *ExtractSymbol(const SCEV *&S, ScalarEvolution &SE) {
  if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S)) {
    if (GlobalValue *GV = dyn_cast<GlobalValue>(U->getValue())) {
      S = SE.getConstant(GV->getType(), 0);
      return GV;
    }
  } else if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    SmallVector<const SCEV *, 8> NewOps(Add->operands());
    GlobalValue *Result = ExtractSymbol(NewOps.back(), SE);
    if (Result)
      S = SE.getAddExpr(NewOps);
    return Result;
  } else if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    SmallVector<const SCEV *, 8> NewOps(AR->operands());
    GlobalValue *Result = ExtractSymbol(NewOps.front(), SE);
    if (Result)
      S = SE.getAddRecExpr(NewOps, AR->getLoop(),
                           // FIXME: AR->getNoWrapFlags(SCEV::FlagNW)
                           SCEV::FlagAnyWrap);
    return Result;
  }
  return nullptr;
}

/// Returns true if the specified instruction is using the specified value as an
/// address.
static bool isAddressUse(const TargetTransformInfo &TTI,
                         Instruction *Inst, Value *OperandVal) {
  bool isAddress = isa<LoadInst>(Inst);
  if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
    if (SI->getPointerOperand() == OperandVal)
      isAddress = true;
  } else if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
    // Addressing modes can also be folded into prefetches and a variety
    // of intrinsics.
    switch (II->getIntrinsicID()) {
    case Intrinsic::memset:
    case Intrinsic::prefetch:
    case Intrinsic::masked_load:
      if (II->getArgOperand(0) == OperandVal)
        isAddress = true;
      break;
    case Intrinsic::masked_store:
      if (II->getArgOperand(1) == OperandVal)
        isAddress = true;
      break;
    case Intrinsic::memmove:
    case Intrinsic::memcpy:
      if (II->getArgOperand(0) == OperandVal ||
          II->getArgOperand(1) == OperandVal)
        isAddress = true;
      break;
    default: {
      MemIntrinsicInfo IntrInfo;
      if (TTI.getTgtMemIntrinsic(II, IntrInfo)) {
        if (IntrInfo.PtrVal == OperandVal)
          isAddress = true;
      }
    }
    }
  } else if (AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(Inst)) {
    if (RMW->getPointerOperand() == OperandVal)
      isAddress = true;
  } else if (AtomicCmpXchgInst *CmpX = dyn_cast<AtomicCmpXchgInst>(Inst)) {
    if (CmpX->getPointerOperand() == OperandVal)
      isAddress = true;
  }
  return isAddress;
}

/// Return the type of the memory being accessed.
static MemAccessTy getAccessType(const TargetTransformInfo &TTI,
                                 Instruction *Inst, Value *OperandVal) {
  MemAccessTy AccessTy = MemAccessTy::getUnknown(Inst->getContext());

  // First get the type of memory being accessed.
  if (Type *Ty = Inst->getAccessType())
    AccessTy.MemTy = Ty;

  // Then get the pointer address space.
  if (const StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
    AccessTy.AddrSpace = SI->getPointerAddressSpace();
  } else if (const LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
    AccessTy.AddrSpace = LI->getPointerAddressSpace();
  } else if (const AtomicRMWInst *RMW = dyn_cast<AtomicRMWInst>(Inst)) {
    AccessTy.AddrSpace = RMW->getPointerAddressSpace();
  } else if (const AtomicCmpXchgInst *CmpX = dyn_cast<AtomicCmpXchgInst>(Inst)) {
    AccessTy.AddrSpace = CmpX->getPointerAddressSpace();
  } else if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::prefetch:
    case Intrinsic::memset:
      AccessTy.AddrSpace = II->getArgOperand(0)->getType()->getPointerAddressSpace();
      AccessTy.MemTy = OperandVal->getType();
      break;
    case Intrinsic::memmove:
    case Intrinsic::memcpy:
      AccessTy.AddrSpace = OperandVal->getType()->getPointerAddressSpace();
      AccessTy.MemTy = OperandVal->getType();
      break;
    case Intrinsic::masked_load:
      AccessTy.AddrSpace =
          II->getArgOperand(0)->getType()->getPointerAddressSpace();
      break;
    case Intrinsic::masked_store:
      AccessTy.AddrSpace =
          II->getArgOperand(1)->getType()->getPointerAddressSpace();
      break;
    default: {
      MemIntrinsicInfo IntrInfo;
      if (TTI.getTgtMemIntrinsic(II, IntrInfo) && IntrInfo.PtrVal) {
        AccessTy.AddrSpace
          = IntrInfo.PtrVal->getType()->getPointerAddressSpace();
      }

      break;
    }
    }
  }

  return AccessTy;
}

/// Return true if this AddRec is already a phi in its loop.
static bool isExistingPhi(const SCEVAddRecExpr *AR, ScalarEvolution &SE) {
  for (PHINode &PN : AR->getLoop()->getHeader()->phis()) {
    if (SE.isSCEVable(PN.getType()) &&
        (SE.getEffectiveSCEVType(PN.getType()) ==
         SE.getEffectiveSCEVType(AR->getType())) &&
        SE.getSCEV(&PN) == AR)
      return true;
  }
  return false;
}

/// Check if expanding this expression is likely to incur significant cost. This
/// is tricky because SCEV doesn't track which expressions are actually computed
/// by the current IR.
///
/// We currently allow expansion of IV increments that involve adds,
/// multiplication by constants, and AddRecs from existing phis.
///
/// TODO: Allow UDivExpr if we can find an existing IV increment that is an
/// obvious multiple of the UDivExpr.
static bool isHighCostExpansion(const SCEV *S,
                                SmallPtrSetImpl<const SCEV*> &Processed,
                                ScalarEvolution &SE) {
  // Zero/One operand expressions
  switch (S->getSCEVType()) {
  case scUnknown:
  case scConstant:
  case scVScale:
    return false;
  case scTruncate:
    return isHighCostExpansion(cast<SCEVTruncateExpr>(S)->getOperand(),
                               Processed, SE);
  case scZeroExtend:
    return isHighCostExpansion(cast<SCEVZeroExtendExpr>(S)->getOperand(),
                               Processed, SE);
  case scSignExtend:
    return isHighCostExpansion(cast<SCEVSignExtendExpr>(S)->getOperand(),
                               Processed, SE);
  default:
    break;
  }

  if (!Processed.insert(S).second)
    return false;

  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    for (const SCEV *S : Add->operands()) {
      if (isHighCostExpansion(S, Processed, SE))
        return true;
    }
    return false;
  }

  if (const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(S)) {
    if (Mul->getNumOperands() == 2) {
      // Multiplication by a constant is ok
      if (isa<SCEVConstant>(Mul->getOperand(0)))
        return isHighCostExpansion(Mul->getOperand(1), Processed, SE);

      // If we have the value of one operand, check if an existing
      // multiplication already generates this expression.
      if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(Mul->getOperand(1))) {
        Value *UVal = U->getValue();
        for (User *UR : UVal->users()) {
          // If U is a constant, it may be used by a ConstantExpr.
          Instruction *UI = dyn_cast<Instruction>(UR);
          if (UI && UI->getOpcode() == Instruction::Mul &&
              SE.isSCEVable(UI->getType())) {
            return SE.getSCEV(UI) == Mul;
          }
        }
      }
    }
  }

  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    if (isExistingPhi(AR, SE))
      return false;
  }

  // Fow now, consider any other type of expression (div/mul/min/max) high cost.
  return true;
}

namespace {

class LSRUse;

} // end anonymous namespace

/// Check if the addressing mode defined by \p F is completely
/// folded in \p LU at isel time.
/// This includes address-mode folding and special icmp tricks.
/// This function returns true if \p LU can accommodate what \p F
/// defines and up to 1 base + 1 scaled + offset.
/// In other words, if \p F has several base registers, this function may
/// still return true. Therefore, users still need to account for
/// additional base registers and/or unfolded offsets to derive an
/// accurate cost model.
static bool isAMCompletelyFolded(const TargetTransformInfo &TTI,
                                 const LSRUse &LU, const Formula &F);

// Get the cost of the scaling factor used in F for LU.
static InstructionCost getScalingFactorCost(const TargetTransformInfo &TTI,
                                            const LSRUse &LU, const Formula &F,
                                            const Loop &L);

namespace {

/// This class is used to measure and compare candidate formulae.
class Cost {
  const Loop *L = nullptr;
  ScalarEvolution *SE = nullptr;
  const TargetTransformInfo *TTI = nullptr;
  TargetTransformInfo::LSRCost C;
  TTI::AddressingModeKind AMK = TTI::AMK_None;

public:
  Cost() = delete;
  Cost(const Loop *L, ScalarEvolution &SE, const TargetTransformInfo &TTI,
       TTI::AddressingModeKind AMK) :
    L(L), SE(&SE), TTI(&TTI), AMK(AMK) {
    C.Insns = 0;
    C.NumRegs = 0;
    C.AddRecCost = 0;
    C.NumIVMuls = 0;
    C.NumBaseAdds = 0;
    C.ImmCost = 0;
    C.SetupCost = 0;
    C.ScaleCost = 0;
  }

  bool isLess(const Cost &Other) const;

  void Lose();

#ifndef NDEBUG
  // Once any of the metrics loses, they must all remain losers.
  bool isValid() {
    return ((C.Insns | C.NumRegs | C.AddRecCost | C.NumIVMuls | C.NumBaseAdds
             | C.ImmCost | C.SetupCost | C.ScaleCost) != ~0u)
      || ((C.Insns & C.NumRegs & C.AddRecCost & C.NumIVMuls & C.NumBaseAdds
           & C.ImmCost & C.SetupCost & C.ScaleCost) == ~0u);
  }
#endif

  bool isLoser() {
    assert(isValid() && "invalid cost");
    return C.NumRegs == ~0u;
  }

  void RateFormula(const Formula &F,
                   SmallPtrSetImpl<const SCEV *> &Regs,
                   const DenseSet<const SCEV *> &VisitedRegs,
                   const LSRUse &LU,
                   SmallPtrSetImpl<const SCEV *> *LoserRegs = nullptr);

  void print(raw_ostream &OS) const;
  void dump() const;

private:
  void RateRegister(const Formula &F, const SCEV *Reg,
                    SmallPtrSetImpl<const SCEV *> &Regs);
  void RatePrimaryRegister(const Formula &F, const SCEV *Reg,
                           SmallPtrSetImpl<const SCEV *> &Regs,
                           SmallPtrSetImpl<const SCEV *> *LoserRegs);
};

/// An operand value in an instruction which is to be replaced with some
/// equivalent, possibly strength-reduced, replacement.
struct LSRFixup {
  /// The instruction which will be updated.
  Instruction *UserInst = nullptr;

  /// The operand of the instruction which will be replaced. The operand may be
  /// used more than once; every instance will be replaced.
  Value *OperandValToReplace = nullptr;

  /// If this user is to use the post-incremented value of an induction
  /// variable, this set is non-empty and holds the loops associated with the
  /// induction variable.
  PostIncLoopSet PostIncLoops;

  /// A constant offset to be added to the LSRUse expression.  This allows
  /// multiple fixups to share the same LSRUse with different offsets, for
  /// example in an unrolled loop.
  Immediate Offset = Immediate::getZero();

  LSRFixup() = default;

  bool isUseFullyOutsideLoop(const Loop *L) const;

  void print(raw_ostream &OS) const;
  void dump() const;
};

/// A DenseMapInfo implementation for holding DenseMaps and DenseSets of sorted
/// SmallVectors of const SCEV*.
struct UniquifierDenseMapInfo {
  static SmallVector<const SCEV *, 4> getEmptyKey() {
    SmallVector<const SCEV *, 4>  V;
    V.push_back(reinterpret_cast<const SCEV *>(-1));
    return V;
  }

  static SmallVector<const SCEV *, 4> getTombstoneKey() {
    SmallVector<const SCEV *, 4> V;
    V.push_back(reinterpret_cast<const SCEV *>(-2));
    return V;
  }

  static unsigned getHashValue(const SmallVector<const SCEV *, 4> &V) {
    return static_cast<unsigned>(hash_combine_range(V.begin(), V.end()));
  }

  static bool isEqual(const SmallVector<const SCEV *, 4> &LHS,
                      const SmallVector<const SCEV *, 4> &RHS) {
    return LHS == RHS;
  }
};

/// This class holds the state that LSR keeps for each use in IVUsers, as well
/// as uses invented by LSR itself. It includes information about what kinds of
/// things can be folded into the user, information about the user itself, and
/// information about how the use may be satisfied.  TODO: Represent multiple
/// users of the same expression in common?
class LSRUse {
  DenseSet<SmallVector<const SCEV *, 4>, UniquifierDenseMapInfo> Uniquifier;

public:
  /// An enum for a kind of use, indicating what types of scaled and immediate
  /// operands it might support.
  enum KindType {
    Basic,   ///< A normal use, with no folding.
    Special, ///< A special case of basic, allowing -1 scales.
    Address, ///< An address use; folding according to TargetLowering
    ICmpZero ///< An equality icmp with both operands folded into one.
    // TODO: Add a generic icmp too?
  };

  using SCEVUseKindPair = PointerIntPair<const SCEV *, 2, KindType>;

  KindType Kind;
  MemAccessTy AccessTy;

  /// The list of operands which are to be replaced.
  SmallVector<LSRFixup, 8> Fixups;

  /// Keep track of the min and max offsets of the fixups.
  Immediate MinOffset = Immediate::getFixedMax();
  Immediate MaxOffset = Immediate::getFixedMin();

  /// This records whether all of the fixups using this LSRUse are outside of
  /// the loop, in which case some special-case heuristics may be used.
  bool AllFixupsOutsideLoop = true;

  /// RigidFormula is set to true to guarantee that this use will be associated
  /// with a single formula--the one that initially matched. Some SCEV
  /// expressions cannot be expanded. This allows LSR to consider the registers
  /// used by those expressions without the need to expand them later after
  /// changing the formula.
  bool RigidFormula = false;

  /// This records the widest use type for any fixup using this
  /// LSRUse. FindUseWithSimilarFormula can't consider uses with different max
  /// fixup widths to be equivalent, because the narrower one may be relying on
  /// the implicit truncation to truncate away bogus bits.
  Type *WidestFixupType = nullptr;

  /// A list of ways to build a value that can satisfy this user.  After the
  /// list is populated, one of these is selected heuristically and used to
  /// formulate a replacement for OperandValToReplace in UserInst.
  SmallVector<Formula, 12> Formulae;

  /// The set of register candidates used by all formulae in this LSRUse.
  SmallPtrSet<const SCEV *, 4> Regs;

  LSRUse(KindType K, MemAccessTy AT) : Kind(K), AccessTy(AT) {}

  LSRFixup &getNewFixup() {
    Fixups.push_back(LSRFixup());
    return Fixups.back();
  }

  void pushFixup(LSRFixup &f) {
    Fixups.push_back(f);
    if (Immediate::isKnownGT(f.Offset, MaxOffset))
      MaxOffset = f.Offset;
    if (Immediate::isKnownLT(f.Offset, MinOffset))
      MinOffset = f.Offset;
  }

  bool HasFormulaWithSameRegs(const Formula &F) const;
  float getNotSelectedProbability(const SCEV *Reg) const;
  bool InsertFormula(const Formula &F, const Loop &L);
  void DeleteFormula(Formula &F);
  void RecomputeRegs(size_t LUIdx, RegUseTracker &Reguses);

  void print(raw_ostream &OS) const;
  void dump() const;
};

} // end anonymous namespace

static bool isAMCompletelyFolded(const TargetTransformInfo &TTI,
                                 LSRUse::KindType Kind, MemAccessTy AccessTy,
                                 GlobalValue *BaseGV, Immediate BaseOffset,
                                 bool HasBaseReg, int64_t Scale,
                                 Instruction *Fixup = nullptr);

static unsigned getSetupCost(const SCEV *Reg, unsigned Depth) {
  if (isa<SCEVUnknown>(Reg) || isa<SCEVConstant>(Reg))
    return 1;
  if (Depth == 0)
    return 0;
  if (const auto *S = dyn_cast<SCEVAddRecExpr>(Reg))
    return getSetupCost(S->getStart(), Depth - 1);
  if (auto S = dyn_cast<SCEVIntegralCastExpr>(Reg))
    return getSetupCost(S->getOperand(), Depth - 1);
  if (auto S = dyn_cast<SCEVNAryExpr>(Reg))
    return std::accumulate(S->operands().begin(), S->operands().end(), 0,
                           [&](unsigned i, const SCEV *Reg) {
                             return i + getSetupCost(Reg, Depth - 1);
                           });
  if (auto S = dyn_cast<SCEVUDivExpr>(Reg))
    return getSetupCost(S->getLHS(), Depth - 1) +
           getSetupCost(S->getRHS(), Depth - 1);
  return 0;
}

/// Tally up interesting quantities from the given register.
void Cost::RateRegister(const Formula &F, const SCEV *Reg,
                        SmallPtrSetImpl<const SCEV *> &Regs) {
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Reg)) {
    // If this is an addrec for another loop, it should be an invariant
    // with respect to L since L is the innermost loop (at least
    // for now LSR only handles innermost loops).
    if (AR->getLoop() != L) {
      // If the AddRec exists, consider it's register free and leave it alone.
      if (isExistingPhi(AR, *SE) && AMK != TTI::AMK_PostIndexed)
        return;

      // It is bad to allow LSR for current loop to add induction variables
      // for its sibling loops.
      if (!AR->getLoop()->contains(L)) {
        Lose();
        return;
      }

      // Otherwise, it will be an invariant with respect to Loop L.
      ++C.NumRegs;
      return;
    }

    unsigned LoopCost = 1;
    if (TTI->isIndexedLoadLegal(TTI->MIM_PostInc, AR->getType()) ||
        TTI->isIndexedStoreLegal(TTI->MIM_PostInc, AR->getType())) {

      // If the step size matches the base offset, we could use pre-indexed
      // addressing.
      if (AMK == TTI::AMK_PreIndexed && F.BaseOffset.isFixed()) {
        if (auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(*SE)))
          if (Step->getAPInt() == F.BaseOffset.getFixedValue())
            LoopCost = 0;
      } else if (AMK == TTI::AMK_PostIndexed) {
        const SCEV *LoopStep = AR->getStepRecurrence(*SE);
        if (isa<SCEVConstant>(LoopStep)) {
          const SCEV *LoopStart = AR->getStart();
          if (!isa<SCEVConstant>(LoopStart) &&
              SE->isLoopInvariant(LoopStart, L))
            LoopCost = 0;
        }
      }
    }
    C.AddRecCost += LoopCost;

    // Add the step value register, if it needs one.
    // TODO: The non-affine case isn't precisely modeled here.
    if (!AR->isAffine() || !isa<SCEVConstant>(AR->getOperand(1))) {
      if (!Regs.count(AR->getOperand(1))) {
        RateRegister(F, AR->getOperand(1), Regs);
        if (isLoser())
          return;
      }
    }
  }
  ++C.NumRegs;

  // Rough heuristic; favor registers which don't require extra setup
  // instructions in the preheader.
  C.SetupCost += getSetupCost(Reg, SetupCostDepthLimit);
  // Ensure we don't, even with the recusion limit, produce invalid costs.
  C.SetupCost = std::min<unsigned>(C.SetupCost, 1 << 16);

  C.NumIVMuls += isa<SCEVMulExpr>(Reg) &&
               SE->hasComputableLoopEvolution(Reg, L);
}

/// Record this register in the set. If we haven't seen it before, rate
/// it. Optional LoserRegs provides a way to declare any formula that refers to
/// one of those regs an instant loser.
void Cost::RatePrimaryRegister(const Formula &F, const SCEV *Reg,
                               SmallPtrSetImpl<const SCEV *> &Regs,
                               SmallPtrSetImpl<const SCEV *> *LoserRegs) {
  if (LoserRegs && LoserRegs->count(Reg)) {
    Lose();
    return;
  }
  if (Regs.insert(Reg).second) {
    RateRegister(F, Reg, Regs);
    if (LoserRegs && isLoser())
      LoserRegs->insert(Reg);
  }
}

void Cost::RateFormula(const Formula &F,
                       SmallPtrSetImpl<const SCEV *> &Regs,
                       const DenseSet<const SCEV *> &VisitedRegs,
                       const LSRUse &LU,
                       SmallPtrSetImpl<const SCEV *> *LoserRegs) {
  if (isLoser())
    return;
  assert(F.isCanonical(*L) && "Cost is accurate only for canonical formula");
  // Tally up the registers.
  unsigned PrevAddRecCost = C.AddRecCost;
  unsigned PrevNumRegs = C.NumRegs;
  unsigned PrevNumBaseAdds = C.NumBaseAdds;
  if (const SCEV *ScaledReg = F.ScaledReg) {
    if (VisitedRegs.count(ScaledReg)) {
      Lose();
      return;
    }
    RatePrimaryRegister(F, ScaledReg, Regs, LoserRegs);
    if (isLoser())
      return;
  }
  for (const SCEV *BaseReg : F.BaseRegs) {
    if (VisitedRegs.count(BaseReg)) {
      Lose();
      return;
    }
    RatePrimaryRegister(F, BaseReg, Regs, LoserRegs);
    if (isLoser())
      return;
  }

  // Determine how many (unfolded) adds we'll need inside the loop.
  size_t NumBaseParts = F.getNumRegs();
  if (NumBaseParts > 1)
    // Do not count the base and a possible second register if the target
    // allows to fold 2 registers.
    C.NumBaseAdds +=
        NumBaseParts - (1 + (F.Scale && isAMCompletelyFolded(*TTI, LU, F)));
  C.NumBaseAdds += (F.UnfoldedOffset.isNonZero());

  // Accumulate non-free scaling amounts.
  C.ScaleCost += *getScalingFactorCost(*TTI, LU, F, *L).getValue();

  // Tally up the non-zero immediates.
  for (const LSRFixup &Fixup : LU.Fixups) {
    if (Fixup.Offset.isCompatibleImmediate(F.BaseOffset)) {
      Immediate Offset = Fixup.Offset.addUnsigned(F.BaseOffset);
      if (F.BaseGV)
        C.ImmCost += 64; // Handle symbolic values conservatively.
                         // TODO: This should probably be the pointer size.
      else if (Offset.isNonZero())
        C.ImmCost +=
            APInt(64, Offset.getKnownMinValue(), true).getSignificantBits();

      // Check with target if this offset with this instruction is
      // specifically not supported.
      if (LU.Kind == LSRUse::Address && Offset.isNonZero() &&
          !isAMCompletelyFolded(*TTI, LSRUse::Address, LU.AccessTy, F.BaseGV,
                                Offset, F.HasBaseReg, F.Scale, Fixup.UserInst))
        C.NumBaseAdds++;
    } else {
      // Incompatible immediate type, increase cost to avoid using
      C.ImmCost += 2048;
    }
  }

  // If we don't count instruction cost exit here.
  if (!InsnsCost) {
    assert(isValid() && "invalid cost");
    return;
  }

  // Treat every new register that exceeds TTI.getNumberOfRegisters() - 1 as
  // additional instruction (at least fill).
  // TODO: Need distinguish register class?
  unsigned TTIRegNum = TTI->getNumberOfRegisters(
                       TTI->getRegisterClassForType(false, F.getType())) - 1;
  if (C.NumRegs > TTIRegNum) {
    // Cost already exceeded TTIRegNum, then only newly added register can add
    // new instructions.
    if (PrevNumRegs > TTIRegNum)
      C.Insns += (C.NumRegs - PrevNumRegs);
    else
      C.Insns += (C.NumRegs - TTIRegNum);
  }

  // If ICmpZero formula ends with not 0, it could not be replaced by
  // just add or sub. We'll need to compare final result of AddRec.
  // That means we'll need an additional instruction. But if the target can
  // macro-fuse a compare with a branch, don't count this extra instruction.
  // For -10 + {0, +, 1}:
  // i = i + 1;
  // cmp i, 10
  //
  // For {-10, +, 1}:
  // i = i + 1;
  if (LU.Kind == LSRUse::ICmpZero && !F.hasZeroEnd() &&
      !TTI->canMacroFuseCmp())
    C.Insns++;
  // Each new AddRec adds 1 instruction to calculation.
  C.Insns += (C.AddRecCost - PrevAddRecCost);

  // BaseAdds adds instructions for unfolded registers.
  if (LU.Kind != LSRUse::ICmpZero)
    C.Insns += C.NumBaseAdds - PrevNumBaseAdds;
  assert(isValid() && "invalid cost");
}

/// Set this cost to a losing value.
void Cost::Lose() {
  C.Insns = std::numeric_limits<unsigned>::max();
  C.NumRegs = std::numeric_limits<unsigned>::max();
  C.AddRecCost = std::numeric_limits<unsigned>::max();
  C.NumIVMuls = std::numeric_limits<unsigned>::max();
  C.NumBaseAdds = std::numeric_limits<unsigned>::max();
  C.ImmCost = std::numeric_limits<unsigned>::max();
  C.SetupCost = std::numeric_limits<unsigned>::max();
  C.ScaleCost = std::numeric_limits<unsigned>::max();
}

/// Choose the lower cost.
bool Cost::isLess(const Cost &Other) const {
  if (InsnsCost.getNumOccurrences() > 0 && InsnsCost &&
      C.Insns != Other.C.Insns)
    return C.Insns < Other.C.Insns;
  return TTI->isLSRCostLess(C, Other.C);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void Cost::print(raw_ostream &OS) const {
  if (InsnsCost)
    OS << C.Insns << " instruction" << (C.Insns == 1 ? " " : "s ");
  OS << C.NumRegs << " reg" << (C.NumRegs == 1 ? "" : "s");
  if (C.AddRecCost != 0)
    OS << ", with addrec cost " << C.AddRecCost;
  if (C.NumIVMuls != 0)
    OS << ", plus " << C.NumIVMuls << " IV mul"
       << (C.NumIVMuls == 1 ? "" : "s");
  if (C.NumBaseAdds != 0)
    OS << ", plus " << C.NumBaseAdds << " base add"
       << (C.NumBaseAdds == 1 ? "" : "s");
  if (C.ScaleCost != 0)
    OS << ", plus " << C.ScaleCost << " scale cost";
  if (C.ImmCost != 0)
    OS << ", plus " << C.ImmCost << " imm cost";
  if (C.SetupCost != 0)
    OS << ", plus " << C.SetupCost << " setup cost";
}

LLVM_DUMP_METHOD void Cost::dump() const {
  print(errs()); errs() << '\n';
}
#endif

/// Test whether this fixup always uses its value outside of the given loop.
bool LSRFixup::isUseFullyOutsideLoop(const Loop *L) const {
  // PHI nodes use their value in their incoming blocks.
  if (const PHINode *PN = dyn_cast<PHINode>(UserInst)) {
    for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
      if (PN->getIncomingValue(i) == OperandValToReplace &&
          L->contains(PN->getIncomingBlock(i)))
        return false;
    return true;
  }

  return !L->contains(UserInst);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void LSRFixup::print(raw_ostream &OS) const {
  OS << "UserInst=";
  // Store is common and interesting enough to be worth special-casing.
  if (StoreInst *Store = dyn_cast<StoreInst>(UserInst)) {
    OS << "store ";
    Store->getOperand(0)->printAsOperand(OS, /*PrintType=*/false);
  } else if (UserInst->getType()->isVoidTy())
    OS << UserInst->getOpcodeName();
  else
    UserInst->printAsOperand(OS, /*PrintType=*/false);

  OS << ", OperandValToReplace=";
  OperandValToReplace->printAsOperand(OS, /*PrintType=*/false);

  for (const Loop *PIL : PostIncLoops) {
    OS << ", PostIncLoop=";
    PIL->getHeader()->printAsOperand(OS, /*PrintType=*/false);
  }

  if (Offset.isNonZero())
    OS << ", Offset=" << Offset;
}

LLVM_DUMP_METHOD void LSRFixup::dump() const {
  print(errs()); errs() << '\n';
}
#endif

/// Test whether this use as a formula which has the same registers as the given
/// formula.
bool LSRUse::HasFormulaWithSameRegs(const Formula &F) const {
  SmallVector<const SCEV *, 4> Key = F.BaseRegs;
  if (F.ScaledReg) Key.push_back(F.ScaledReg);
  // Unstable sort by host order ok, because this is only used for uniquifying.
  llvm::sort(Key);
  return Uniquifier.count(Key);
}

/// The function returns a probability of selecting formula without Reg.
float LSRUse::getNotSelectedProbability(const SCEV *Reg) const {
  unsigned FNum = 0;
  for (const Formula &F : Formulae)
    if (F.referencesReg(Reg))
      FNum++;
  return ((float)(Formulae.size() - FNum)) / Formulae.size();
}

/// If the given formula has not yet been inserted, add it to the list, and
/// return true. Return false otherwise.  The formula must be in canonical form.
bool LSRUse::InsertFormula(const Formula &F, const Loop &L) {
  assert(F.isCanonical(L) && "Invalid canonical representation");

  if (!Formulae.empty() && RigidFormula)
    return false;

  SmallVector<const SCEV *, 4> Key = F.BaseRegs;
  if (F.ScaledReg) Key.push_back(F.ScaledReg);
  // Unstable sort by host order ok, because this is only used for uniquifying.
  llvm::sort(Key);

  if (!Uniquifier.insert(Key).second)
    return false;

  // Using a register to hold the value of 0 is not profitable.
  assert((!F.ScaledReg || !F.ScaledReg->isZero()) &&
         "Zero allocated in a scaled register!");
#ifndef NDEBUG
  for (const SCEV *BaseReg : F.BaseRegs)
    assert(!BaseReg->isZero() && "Zero allocated in a base register!");
#endif

  // Add the formula to the list.
  Formulae.push_back(F);

  // Record registers now being used by this use.
  Regs.insert(F.BaseRegs.begin(), F.BaseRegs.end());
  if (F.ScaledReg)
    Regs.insert(F.ScaledReg);

  return true;
}

/// Remove the given formula from this use's list.
void LSRUse::DeleteFormula(Formula &F) {
  if (&F != &Formulae.back())
    std::swap(F, Formulae.back());
  Formulae.pop_back();
}

/// Recompute the Regs field, and update RegUses.
void LSRUse::RecomputeRegs(size_t LUIdx, RegUseTracker &RegUses) {
  // Now that we've filtered out some formulae, recompute the Regs set.
  SmallPtrSet<const SCEV *, 4> OldRegs = std::move(Regs);
  Regs.clear();
  for (const Formula &F : Formulae) {
    if (F.ScaledReg) Regs.insert(F.ScaledReg);
    Regs.insert(F.BaseRegs.begin(), F.BaseRegs.end());
  }

  // Update the RegTracker.
  for (const SCEV *S : OldRegs)
    if (!Regs.count(S))
      RegUses.dropRegister(S, LUIdx);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void LSRUse::print(raw_ostream &OS) const {
  OS << "LSR Use: Kind=";
  switch (Kind) {
  case Basic:    OS << "Basic"; break;
  case Special:  OS << "Special"; break;
  case ICmpZero: OS << "ICmpZero"; break;
  case Address:
    OS << "Address of ";
    if (AccessTy.MemTy->isPointerTy())
      OS << "pointer"; // the full pointer type could be really verbose
    else {
      OS << *AccessTy.MemTy;
    }

    OS << " in addrspace(" << AccessTy.AddrSpace << ')';
  }

  OS << ", Offsets={";
  bool NeedComma = false;
  for (const LSRFixup &Fixup : Fixups) {
    if (NeedComma) OS << ',';
    OS << Fixup.Offset;
    NeedComma = true;
  }
  OS << '}';

  if (AllFixupsOutsideLoop)
    OS << ", all-fixups-outside-loop";

  if (WidestFixupType)
    OS << ", widest fixup type: " << *WidestFixupType;
}

LLVM_DUMP_METHOD void LSRUse::dump() const {
  print(errs()); errs() << '\n';
}
#endif

static bool isAMCompletelyFolded(const TargetTransformInfo &TTI,
                                 LSRUse::KindType Kind, MemAccessTy AccessTy,
                                 GlobalValue *BaseGV, Immediate BaseOffset,
                                 bool HasBaseReg, int64_t Scale,
                                 Instruction *Fixup /* = nullptr */) {
  switch (Kind) {
  case LSRUse::Address: {
    int64_t FixedOffset =
        BaseOffset.isScalable() ? 0 : BaseOffset.getFixedValue();
    int64_t ScalableOffset =
        BaseOffset.isScalable() ? BaseOffset.getKnownMinValue() : 0;
    return TTI.isLegalAddressingMode(AccessTy.MemTy, BaseGV, FixedOffset,
                                     HasBaseReg, Scale, AccessTy.AddrSpace,
                                     Fixup, ScalableOffset);
  }
  case LSRUse::ICmpZero:
    // There's not even a target hook for querying whether it would be legal to
    // fold a GV into an ICmp.
    if (BaseGV)
      return false;

    // ICmp only has two operands; don't allow more than two non-trivial parts.
    if (Scale != 0 && HasBaseReg && BaseOffset.isNonZero())
      return false;

    // ICmp only supports no scale or a -1 scale, as we can "fold" a -1 scale by
    // putting the scaled register in the other operand of the icmp.
    if (Scale != 0 && Scale != -1)
      return false;

    // If we have low-level target information, ask the target if it can fold an
    // integer immediate on an icmp.
    if (BaseOffset.isNonZero()) {
      // We don't have an interface to query whether the target supports
      // icmpzero against scalable quantities yet.
      if (BaseOffset.isScalable())
        return false;

      // We have one of:
      // ICmpZero     BaseReg + BaseOffset => ICmp BaseReg, -BaseOffset
      // ICmpZero -1*ScaleReg + BaseOffset => ICmp ScaleReg, BaseOffset
      // Offs is the ICmp immediate.
      if (Scale == 0)
        // The cast does the right thing with
        // std::numeric_limits<int64_t>::min().
        BaseOffset = BaseOffset.getFixed(-(uint64_t)BaseOffset.getFixedValue());
      return TTI.isLegalICmpImmediate(BaseOffset.getFixedValue());
    }

    // ICmpZero BaseReg + -1*ScaleReg => ICmp BaseReg, ScaleReg
    return true;

  case LSRUse::Basic:
    // Only handle single-register values.
    return !BaseGV && Scale == 0 && BaseOffset.isZero();

  case LSRUse::Special:
    // Special case Basic to handle -1 scales.
    return !BaseGV && (Scale == 0 || Scale == -1) && BaseOffset.isZero();
  }

  llvm_unreachable("Invalid LSRUse Kind!");
}

static bool isAMCompletelyFolded(const TargetTransformInfo &TTI,
                                 Immediate MinOffset, Immediate MaxOffset,
                                 LSRUse::KindType Kind, MemAccessTy AccessTy,
                                 GlobalValue *BaseGV, Immediate BaseOffset,
                                 bool HasBaseReg, int64_t Scale) {
  if (BaseOffset.isNonZero() &&
      (BaseOffset.isScalable() != MinOffset.isScalable() ||
       BaseOffset.isScalable() != MaxOffset.isScalable()))
    return false;
  // Check for overflow.
  int64_t Base = BaseOffset.getKnownMinValue();
  int64_t Min = MinOffset.getKnownMinValue();
  int64_t Max = MaxOffset.getKnownMinValue();
  if (((int64_t)((uint64_t)Base + Min) > Base) != (Min > 0))
    return false;
  MinOffset = Immediate::get((uint64_t)Base + Min, MinOffset.isScalable());
  if (((int64_t)((uint64_t)Base + Max) > Base) != (Max > 0))
    return false;
  MaxOffset = Immediate::get((uint64_t)Base + Max, MaxOffset.isScalable());

  return isAMCompletelyFolded(TTI, Kind, AccessTy, BaseGV, MinOffset,
                              HasBaseReg, Scale) &&
         isAMCompletelyFolded(TTI, Kind, AccessTy, BaseGV, MaxOffset,
                              HasBaseReg, Scale);
}

static bool isAMCompletelyFolded(const TargetTransformInfo &TTI,
                                 Immediate MinOffset, Immediate MaxOffset,
                                 LSRUse::KindType Kind, MemAccessTy AccessTy,
                                 const Formula &F, const Loop &L) {
  // For the purpose of isAMCompletelyFolded either having a canonical formula
  // or a scale not equal to zero is correct.
  // Problems may arise from non canonical formulae having a scale == 0.
  // Strictly speaking it would best to just rely on canonical formulae.
  // However, when we generate the scaled formulae, we first check that the
  // scaling factor is profitable before computing the actual ScaledReg for
  // compile time sake.
  assert((F.isCanonical(L) || F.Scale != 0));
  return isAMCompletelyFolded(TTI, MinOffset, MaxOffset, Kind, AccessTy,
                              F.BaseGV, F.BaseOffset, F.HasBaseReg, F.Scale);
}

/// Test whether we know how to expand the current formula.
static bool isLegalUse(const TargetTransformInfo &TTI, Immediate MinOffset,
                       Immediate MaxOffset, LSRUse::KindType Kind,
                       MemAccessTy AccessTy, GlobalValue *BaseGV,
                       Immediate BaseOffset, bool HasBaseReg, int64_t Scale) {
  // We know how to expand completely foldable formulae.
  return isAMCompletelyFolded(TTI, MinOffset, MaxOffset, Kind, AccessTy, BaseGV,
                              BaseOffset, HasBaseReg, Scale) ||
         // Or formulae that use a base register produced by a sum of base
         // registers.
         (Scale == 1 &&
          isAMCompletelyFolded(TTI, MinOffset, MaxOffset, Kind, AccessTy,
                               BaseGV, BaseOffset, true, 0));
}

static bool isLegalUse(const TargetTransformInfo &TTI, Immediate MinOffset,
                       Immediate MaxOffset, LSRUse::KindType Kind,
                       MemAccessTy AccessTy, const Formula &F) {
  return isLegalUse(TTI, MinOffset, MaxOffset, Kind, AccessTy, F.BaseGV,
                    F.BaseOffset, F.HasBaseReg, F.Scale);
}

static bool isLegalAddImmediate(const TargetTransformInfo &TTI,
                                Immediate Offset) {
  if (Offset.isScalable())
    return TTI.isLegalAddScalableImmediate(Offset.getKnownMinValue());

  return TTI.isLegalAddImmediate(Offset.getFixedValue());
}

static bool isAMCompletelyFolded(const TargetTransformInfo &TTI,
                                 const LSRUse &LU, const Formula &F) {
  // Target may want to look at the user instructions.
  if (LU.Kind == LSRUse::Address && TTI.LSRWithInstrQueries()) {
    for (const LSRFixup &Fixup : LU.Fixups)
      if (!isAMCompletelyFolded(TTI, LSRUse::Address, LU.AccessTy, F.BaseGV,
                                (F.BaseOffset + Fixup.Offset), F.HasBaseReg,
                                F.Scale, Fixup.UserInst))
        return false;
    return true;
  }

  return isAMCompletelyFolded(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind,
                              LU.AccessTy, F.BaseGV, F.BaseOffset, F.HasBaseReg,
                              F.Scale);
}

static InstructionCost getScalingFactorCost(const TargetTransformInfo &TTI,
                                            const LSRUse &LU, const Formula &F,
                                            const Loop &L) {
  if (!F.Scale)
    return 0;

  // If the use is not completely folded in that instruction, we will have to
  // pay an extra cost only for scale != 1.
  if (!isAMCompletelyFolded(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind,
                            LU.AccessTy, F, L))
    return F.Scale != 1;

  switch (LU.Kind) {
  case LSRUse::Address: {
    // Check the scaling factor cost with both the min and max offsets.
    int64_t ScalableMin = 0, ScalableMax = 0, FixedMin = 0, FixedMax = 0;
    if (F.BaseOffset.isScalable()) {
      ScalableMin = (F.BaseOffset + LU.MinOffset).getKnownMinValue();
      ScalableMax = (F.BaseOffset + LU.MaxOffset).getKnownMinValue();
    } else {
      FixedMin = (F.BaseOffset + LU.MinOffset).getFixedValue();
      FixedMax = (F.BaseOffset + LU.MaxOffset).getFixedValue();
    }
    InstructionCost ScaleCostMinOffset = TTI.getScalingFactorCost(
        LU.AccessTy.MemTy, F.BaseGV, StackOffset::get(FixedMin, ScalableMin),
        F.HasBaseReg, F.Scale, LU.AccessTy.AddrSpace);
    InstructionCost ScaleCostMaxOffset = TTI.getScalingFactorCost(
        LU.AccessTy.MemTy, F.BaseGV, StackOffset::get(FixedMax, ScalableMax),
        F.HasBaseReg, F.Scale, LU.AccessTy.AddrSpace);

    assert(ScaleCostMinOffset.isValid() && ScaleCostMaxOffset.isValid() &&
           "Legal addressing mode has an illegal cost!");
    return std::max(ScaleCostMinOffset, ScaleCostMaxOffset);
  }
  case LSRUse::ICmpZero:
  case LSRUse::Basic:
  case LSRUse::Special:
    // The use is completely folded, i.e., everything is folded into the
    // instruction.
    return 0;
  }

  llvm_unreachable("Invalid LSRUse Kind!");
}

static bool isAlwaysFoldable(const TargetTransformInfo &TTI,
                             LSRUse::KindType Kind, MemAccessTy AccessTy,
                             GlobalValue *BaseGV, Immediate BaseOffset,
                             bool HasBaseReg) {
  // Fast-path: zero is always foldable.
  if (BaseOffset.isZero() && !BaseGV)
    return true;

  // Conservatively, create an address with an immediate and a
  // base and a scale.
  int64_t Scale = Kind == LSRUse::ICmpZero ? -1 : 1;

  // Canonicalize a scale of 1 to a base register if the formula doesn't
  // already have a base register.
  if (!HasBaseReg && Scale == 1) {
    Scale = 0;
    HasBaseReg = true;
  }

  // FIXME: Try with + without a scale? Maybe based on TTI?
  // I think basereg + scaledreg + immediateoffset isn't a good 'conservative'
  // default for many architectures, not just AArch64 SVE. More investigation
  // needed later to determine if this should be used more widely than just
  // on scalable types.
  if (HasBaseReg && BaseOffset.isNonZero() && Kind != LSRUse::ICmpZero &&
      AccessTy.MemTy && AccessTy.MemTy->isScalableTy() && DropScaledForVScale)
    Scale = 0;

  return isAMCompletelyFolded(TTI, Kind, AccessTy, BaseGV, BaseOffset,
                              HasBaseReg, Scale);
}

static bool isAlwaysFoldable(const TargetTransformInfo &TTI,
                             ScalarEvolution &SE, Immediate MinOffset,
                             Immediate MaxOffset, LSRUse::KindType Kind,
                             MemAccessTy AccessTy, const SCEV *S,
                             bool HasBaseReg) {
  // Fast-path: zero is always foldable.
  if (S->isZero()) return true;

  // Conservatively, create an address with an immediate and a
  // base and a scale.
  Immediate BaseOffset = ExtractImmediate(S, SE);
  GlobalValue *BaseGV = ExtractSymbol(S, SE);

  // If there's anything else involved, it's not foldable.
  if (!S->isZero()) return false;

  // Fast-path: zero is always foldable.
  if (BaseOffset.isZero() && !BaseGV)
    return true;

  if (BaseOffset.isScalable())
    return false;

  // Conservatively, create an address with an immediate and a
  // base and a scale.
  int64_t Scale = Kind == LSRUse::ICmpZero ? -1 : 1;

  return isAMCompletelyFolded(TTI, MinOffset, MaxOffset, Kind, AccessTy, BaseGV,
                              BaseOffset, HasBaseReg, Scale);
}

namespace {

/// An individual increment in a Chain of IV increments.  Relate an IV user to
/// an expression that computes the IV it uses from the IV used by the previous
/// link in the Chain.
///
/// For the head of a chain, IncExpr holds the absolute SCEV expression for the
/// original IVOperand. The head of the chain's IVOperand is only valid during
/// chain collection, before LSR replaces IV users. During chain generation,
/// IncExpr can be used to find the new IVOperand that computes the same
/// expression.
struct IVInc {
  Instruction *UserInst;
  Value* IVOperand;
  const SCEV *IncExpr;

  IVInc(Instruction *U, Value *O, const SCEV *E)
      : UserInst(U), IVOperand(O), IncExpr(E) {}
};

// The list of IV increments in program order.  We typically add the head of a
// chain without finding subsequent links.
struct IVChain {
  SmallVector<IVInc, 1> Incs;
  const SCEV *ExprBase = nullptr;

  IVChain() = default;
  IVChain(const IVInc &Head, const SCEV *Base)
      : Incs(1, Head), ExprBase(Base) {}

  using const_iterator = SmallVectorImpl<IVInc>::const_iterator;

  // Return the first increment in the chain.
  const_iterator begin() const {
    assert(!Incs.empty());
    return std::next(Incs.begin());
  }
  const_iterator end() const {
    return Incs.end();
  }

  // Returns true if this chain contains any increments.
  bool hasIncs() const { return Incs.size() >= 2; }

  // Add an IVInc to the end of this chain.
  void add(const IVInc &X) { Incs.push_back(X); }

  // Returns the last UserInst in the chain.
  Instruction *tailUserInst() const { return Incs.back().UserInst; }

  // Returns true if IncExpr can be profitably added to this chain.
  bool isProfitableIncrement(const SCEV *OperExpr,
                             const SCEV *IncExpr,
                             ScalarEvolution&);
};

/// Helper for CollectChains to track multiple IV increment uses.  Distinguish
/// between FarUsers that definitely cross IV increments and NearUsers that may
/// be used between IV increments.
struct ChainUsers {
  SmallPtrSet<Instruction*, 4> FarUsers;
  SmallPtrSet<Instruction*, 4> NearUsers;
};

/// This class holds state for the main loop strength reduction logic.
class LSRInstance {
  IVUsers &IU;
  ScalarEvolution &SE;
  DominatorTree &DT;
  LoopInfo &LI;
  AssumptionCache &AC;
  TargetLibraryInfo &TLI;
  const TargetTransformInfo &TTI;
  Loop *const L;
  MemorySSAUpdater *MSSAU;
  TTI::AddressingModeKind AMK;
  mutable SCEVExpander Rewriter;
  bool Changed = false;

  /// This is the insert position that the current loop's induction variable
  /// increment should be placed. In simple loops, this is the latch block's
  /// terminator. But in more complicated cases, this is a position which will
  /// dominate all the in-loop post-increment users.
  Instruction *IVIncInsertPos = nullptr;

  /// Interesting factors between use strides.
  ///
  /// We explicitly use a SetVector which contains a SmallSet, instead of the
  /// default, a SmallDenseSet, because we need to use the full range of
  /// int64_ts, and there's currently no good way of doing that with
  /// SmallDenseSet.
  SetVector<int64_t, SmallVector<int64_t, 8>, SmallSet<int64_t, 8>> Factors;

  /// The cost of the current SCEV, the best solution by LSR will be dropped if
  /// the solution is not profitable.
  Cost BaselineCost;

  /// Interesting use types, to facilitate truncation reuse.
  SmallSetVector<Type *, 4> Types;

  /// The list of interesting uses.
  mutable SmallVector<LSRUse, 16> Uses;

  /// Track which uses use which register candidates.
  RegUseTracker RegUses;

  // Limit the number of chains to avoid quadratic behavior. We don't expect to
  // have more than a few IV increment chains in a loop. Missing a Chain falls
  // back to normal LSR behavior for those uses.
  static const unsigned MaxChains = 8;

  /// IV users can form a chain of IV increments.
  SmallVector<IVChain, MaxChains> IVChainVec;

  /// IV users that belong to profitable IVChains.
  SmallPtrSet<Use*, MaxChains> IVIncSet;

  /// Induction variables that were generated and inserted by the SCEV Expander.
  SmallVector<llvm::WeakVH, 2> ScalarEvolutionIVs;

  void OptimizeShadowIV();
  bool FindIVUserForCond(ICmpInst *Cond, IVStrideUse *&CondUse);
  ICmpInst *OptimizeMax(ICmpInst *Cond, IVStrideUse* &CondUse);
  void OptimizeLoopTermCond();

  void ChainInstruction(Instruction *UserInst, Instruction *IVOper,
                        SmallVectorImpl<ChainUsers> &ChainUsersVec);
  void FinalizeChain(IVChain &Chain);
  void CollectChains();
  void GenerateIVChain(const IVChain &Chain,
                       SmallVectorImpl<WeakTrackingVH> &DeadInsts);

  void CollectInterestingTypesAndFactors();
  void CollectFixupsAndInitialFormulae();

  // Support for sharing of LSRUses between LSRFixups.
  using UseMapTy = DenseMap<LSRUse::SCEVUseKindPair, size_t>;
  UseMapTy UseMap;

  bool reconcileNewOffset(LSRUse &LU, Immediate NewOffset, bool HasBaseReg,
                          LSRUse::KindType Kind, MemAccessTy AccessTy);

  std::pair<size_t, Immediate> getUse(const SCEV *&Expr, LSRUse::KindType Kind,
                                      MemAccessTy AccessTy);

  void DeleteUse(LSRUse &LU, size_t LUIdx);

  LSRUse *FindUseWithSimilarFormula(const Formula &F, const LSRUse &OrigLU);

  void InsertInitialFormula(const SCEV *S, LSRUse &LU, size_t LUIdx);
  void InsertSupplementalFormula(const SCEV *S, LSRUse &LU, size_t LUIdx);
  void CountRegisters(const Formula &F, size_t LUIdx);
  bool InsertFormula(LSRUse &LU, unsigned LUIdx, const Formula &F);

  void CollectLoopInvariantFixupsAndFormulae();

  void GenerateReassociations(LSRUse &LU, unsigned LUIdx, Formula Base,
                              unsigned Depth = 0);

  void GenerateReassociationsImpl(LSRUse &LU, unsigned LUIdx,
                                  const Formula &Base, unsigned Depth,
                                  size_t Idx, bool IsScaledReg = false);
  void GenerateCombinations(LSRUse &LU, unsigned LUIdx, Formula Base);
  void GenerateSymbolicOffsetsImpl(LSRUse &LU, unsigned LUIdx,
                                   const Formula &Base, size_t Idx,
                                   bool IsScaledReg = false);
  void GenerateSymbolicOffsets(LSRUse &LU, unsigned LUIdx, Formula Base);
  void GenerateConstantOffsetsImpl(LSRUse &LU, unsigned LUIdx,
                                   const Formula &Base,
                                   const SmallVectorImpl<Immediate> &Worklist,
                                   size_t Idx, bool IsScaledReg = false);
  void GenerateConstantOffsets(LSRUse &LU, unsigned LUIdx, Formula Base);
  void GenerateICmpZeroScales(LSRUse &LU, unsigned LUIdx, Formula Base);
  void GenerateScales(LSRUse &LU, unsigned LUIdx, Formula Base);
  void GenerateTruncates(LSRUse &LU, unsigned LUIdx, Formula Base);
  void GenerateCrossUseConstantOffsets();
  void GenerateAllReuseFormulae();

  void FilterOutUndesirableDedicatedRegisters();

  size_t EstimateSearchSpaceComplexity() const;
  void NarrowSearchSpaceByDetectingSupersets();
  void NarrowSearchSpaceByCollapsingUnrolledCode();
  void NarrowSearchSpaceByRefilteringUndesirableDedicatedRegisters();
  void NarrowSearchSpaceByFilterFormulaWithSameScaledReg();
  void NarrowSearchSpaceByFilterPostInc();
  void NarrowSearchSpaceByDeletingCostlyFormulas();
  void NarrowSearchSpaceByPickingWinnerRegs();
  void NarrowSearchSpaceUsingHeuristics();

  void SolveRecurse(SmallVectorImpl<const Formula *> &Solution,
                    Cost &SolutionCost,
                    SmallVectorImpl<const Formula *> &Workspace,
                    const Cost &CurCost,
                    const SmallPtrSet<const SCEV *, 16> &CurRegs,
                    DenseSet<const SCEV *> &VisitedRegs) const;
  void Solve(SmallVectorImpl<const Formula *> &Solution) const;

  BasicBlock::iterator
  HoistInsertPosition(BasicBlock::iterator IP,
                      const SmallVectorImpl<Instruction *> &Inputs) const;
  BasicBlock::iterator AdjustInsertPositionForExpand(BasicBlock::iterator IP,
                                                     const LSRFixup &LF,
                                                     const LSRUse &LU) const;

  Value *Expand(const LSRUse &LU, const LSRFixup &LF, const Formula &F,
                BasicBlock::iterator IP,
                SmallVectorImpl<WeakTrackingVH> &DeadInsts) const;
  void RewriteForPHI(PHINode *PN, const LSRUse &LU, const LSRFixup &LF,
                     const Formula &F,
                     SmallVectorImpl<WeakTrackingVH> &DeadInsts) const;
  void Rewrite(const LSRUse &LU, const LSRFixup &LF, const Formula &F,
               SmallVectorImpl<WeakTrackingVH> &DeadInsts) const;
  void ImplementSolution(const SmallVectorImpl<const Formula *> &Solution);

public:
  LSRInstance(Loop *L, IVUsers &IU, ScalarEvolution &SE, DominatorTree &DT,
              LoopInfo &LI, const TargetTransformInfo &TTI, AssumptionCache &AC,
              TargetLibraryInfo &TLI, MemorySSAUpdater *MSSAU);

  bool getChanged() const { return Changed; }
  const SmallVectorImpl<WeakVH> &getScalarEvolutionIVs() const {
    return ScalarEvolutionIVs;
  }

  void print_factors_and_types(raw_ostream &OS) const;
  void print_fixups(raw_ostream &OS) const;
  void print_uses(raw_ostream &OS) const;
  void print(raw_ostream &OS) const;
  void dump() const;
};

} // end anonymous namespace

/// If IV is used in a int-to-float cast inside the loop then try to eliminate
/// the cast operation.
void LSRInstance::OptimizeShadowIV() {
  const SCEV *BackedgeTakenCount = SE.getBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(BackedgeTakenCount))
    return;

  for (IVUsers::const_iterator UI = IU.begin(), E = IU.end();
       UI != E; /* empty */) {
    IVUsers::const_iterator CandidateUI = UI;
    ++UI;
    Instruction *ShadowUse = CandidateUI->getUser();
    Type *DestTy = nullptr;
    bool IsSigned = false;

    /* If shadow use is a int->float cast then insert a second IV
       to eliminate this cast.

         for (unsigned i = 0; i < n; ++i)
           foo((double)i);

       is transformed into

         double d = 0.0;
         for (unsigned i = 0; i < n; ++i, ++d)
           foo(d);
    */
    if (UIToFPInst *UCast = dyn_cast<UIToFPInst>(CandidateUI->getUser())) {
      IsSigned = false;
      DestTy = UCast->getDestTy();
    }
    else if (SIToFPInst *SCast = dyn_cast<SIToFPInst>(CandidateUI->getUser())) {
      IsSigned = true;
      DestTy = SCast->getDestTy();
    }
    if (!DestTy) continue;

    // If target does not support DestTy natively then do not apply
    // this transformation.
    if (!TTI.isTypeLegal(DestTy)) continue;

    PHINode *PH = dyn_cast<PHINode>(ShadowUse->getOperand(0));
    if (!PH) continue;
    if (PH->getNumIncomingValues() != 2) continue;

    // If the calculation in integers overflows, the result in FP type will
    // differ. So we only can do this transformation if we are guaranteed to not
    // deal with overflowing values
    const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(PH));
    if (!AR) continue;
    if (IsSigned && !AR->hasNoSignedWrap()) continue;
    if (!IsSigned && !AR->hasNoUnsignedWrap()) continue;

    Type *SrcTy = PH->getType();
    int Mantissa = DestTy->getFPMantissaWidth();
    if (Mantissa == -1) continue;
    if ((int)SE.getTypeSizeInBits(SrcTy) > Mantissa)
      continue;

    unsigned Entry, Latch;
    if (PH->getIncomingBlock(0) == L->getLoopPreheader()) {
      Entry = 0;
      Latch = 1;
    } else {
      Entry = 1;
      Latch = 0;
    }

    ConstantInt *Init = dyn_cast<ConstantInt>(PH->getIncomingValue(Entry));
    if (!Init) continue;
    Constant *NewInit = ConstantFP::get(DestTy, IsSigned ?
                                        (double)Init->getSExtValue() :
                                        (double)Init->getZExtValue());

    BinaryOperator *Incr =
      dyn_cast<BinaryOperator>(PH->getIncomingValue(Latch));
    if (!Incr) continue;
    if (Incr->getOpcode() != Instruction::Add
        && Incr->getOpcode() != Instruction::Sub)
      continue;

    /* Initialize new IV, double d = 0.0 in above example. */
    ConstantInt *C = nullptr;
    if (Incr->getOperand(0) == PH)
      C = dyn_cast<ConstantInt>(Incr->getOperand(1));
    else if (Incr->getOperand(1) == PH)
      C = dyn_cast<ConstantInt>(Incr->getOperand(0));
    else
      continue;

    if (!C) continue;

    // Ignore negative constants, as the code below doesn't handle them
    // correctly. TODO: Remove this restriction.
    if (!C->getValue().isStrictlyPositive())
      continue;

    /* Add new PHINode. */
    PHINode *NewPH = PHINode::Create(DestTy, 2, "IV.S.", PH->getIterator());
    NewPH->setDebugLoc(PH->getDebugLoc());

    /* create new increment. '++d' in above example. */
    Constant *CFP = ConstantFP::get(DestTy, C->getZExtValue());
    BinaryOperator *NewIncr = BinaryOperator::Create(
        Incr->getOpcode() == Instruction::Add ? Instruction::FAdd
                                              : Instruction::FSub,
        NewPH, CFP, "IV.S.next.", Incr->getIterator());
    NewIncr->setDebugLoc(Incr->getDebugLoc());

    NewPH->addIncoming(NewInit, PH->getIncomingBlock(Entry));
    NewPH->addIncoming(NewIncr, PH->getIncomingBlock(Latch));

    /* Remove cast operation */
    ShadowUse->replaceAllUsesWith(NewPH);
    ShadowUse->eraseFromParent();
    Changed = true;
    break;
  }
}

/// If Cond has an operand that is an expression of an IV, set the IV user and
/// stride information and return true, otherwise return false.
bool LSRInstance::FindIVUserForCond(ICmpInst *Cond, IVStrideUse *&CondUse) {
  for (IVStrideUse &U : IU)
    if (U.getUser() == Cond) {
      // NOTE: we could handle setcc instructions with multiple uses here, but
      // InstCombine does it as well for simple uses, it's not clear that it
      // occurs enough in real life to handle.
      CondUse = &U;
      return true;
    }
  return false;
}

/// Rewrite the loop's terminating condition if it uses a max computation.
///
/// This is a narrow solution to a specific, but acute, problem. For loops
/// like this:
///
///   i = 0;
///   do {
///     p[i] = 0.0;
///   } while (++i < n);
///
/// the trip count isn't just 'n', because 'n' might not be positive. And
/// unfortunately this can come up even for loops where the user didn't use
/// a C do-while loop. For example, seemingly well-behaved top-test loops
/// will commonly be lowered like this:
///
///   if (n > 0) {
///     i = 0;
///     do {
///       p[i] = 0.0;
///     } while (++i < n);
///   }
///
/// and then it's possible for subsequent optimization to obscure the if
/// test in such a way that indvars can't find it.
///
/// When indvars can't find the if test in loops like this, it creates a
/// max expression, which allows it to give the loop a canonical
/// induction variable:
///
///   i = 0;
///   max = n < 1 ? 1 : n;
///   do {
///     p[i] = 0.0;
///   } while (++i != max);
///
/// Canonical induction variables are necessary because the loop passes
/// are designed around them. The most obvious example of this is the
/// LoopInfo analysis, which doesn't remember trip count values. It
/// expects to be able to rediscover the trip count each time it is
/// needed, and it does this using a simple analysis that only succeeds if
/// the loop has a canonical induction variable.
///
/// However, when it comes time to generate code, the maximum operation
/// can be quite costly, especially if it's inside of an outer loop.
///
/// This function solves this problem by detecting this type of loop and
/// rewriting their conditions from ICMP_NE back to ICMP_SLT, and deleting
/// the instructions for the maximum computation.
ICmpInst *LSRInstance::OptimizeMax(ICmpInst *Cond, IVStrideUse* &CondUse) {
  // Check that the loop matches the pattern we're looking for.
  if (Cond->getPredicate() != CmpInst::ICMP_EQ &&
      Cond->getPredicate() != CmpInst::ICMP_NE)
    return Cond;

  SelectInst *Sel = dyn_cast<SelectInst>(Cond->getOperand(1));
  if (!Sel || !Sel->hasOneUse()) return Cond;

  const SCEV *BackedgeTakenCount = SE.getBackedgeTakenCount(L);
  if (isa<SCEVCouldNotCompute>(BackedgeTakenCount))
    return Cond;
  const SCEV *One = SE.getConstant(BackedgeTakenCount->getType(), 1);

  // Add one to the backedge-taken count to get the trip count.
  const SCEV *IterationCount = SE.getAddExpr(One, BackedgeTakenCount);
  if (IterationCount != SE.getSCEV(Sel)) return Cond;

  // Check for a max calculation that matches the pattern. There's no check
  // for ICMP_ULE here because the comparison would be with zero, which
  // isn't interesting.
  CmpInst::Predicate Pred = ICmpInst::BAD_ICMP_PREDICATE;
  const SCEVNAryExpr *Max = nullptr;
  if (const SCEVSMaxExpr *S = dyn_cast<SCEVSMaxExpr>(BackedgeTakenCount)) {
    Pred = ICmpInst::ICMP_SLE;
    Max = S;
  } else if (const SCEVSMaxExpr *S = dyn_cast<SCEVSMaxExpr>(IterationCount)) {
    Pred = ICmpInst::ICMP_SLT;
    Max = S;
  } else if (const SCEVUMaxExpr *U = dyn_cast<SCEVUMaxExpr>(IterationCount)) {
    Pred = ICmpInst::ICMP_ULT;
    Max = U;
  } else {
    // No match; bail.
    return Cond;
  }

  // To handle a max with more than two operands, this optimization would
  // require additional checking and setup.
  if (Max->getNumOperands() != 2)
    return Cond;

  const SCEV *MaxLHS = Max->getOperand(0);
  const SCEV *MaxRHS = Max->getOperand(1);

  // ScalarEvolution canonicalizes constants to the left. For < and >, look
  // for a comparison with 1. For <= and >=, a comparison with zero.
  if (!MaxLHS ||
      (ICmpInst::isTrueWhenEqual(Pred) ? !MaxLHS->isZero() : (MaxLHS != One)))
    return Cond;

  // Check the relevant induction variable for conformance to
  // the pattern.
  const SCEV *IV = SE.getSCEV(Cond->getOperand(0));
  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(IV);
  if (!AR || !AR->isAffine() ||
      AR->getStart() != One ||
      AR->getStepRecurrence(SE) != One)
    return Cond;

  assert(AR->getLoop() == L &&
         "Loop condition operand is an addrec in a different loop!");

  // Check the right operand of the select, and remember it, as it will
  // be used in the new comparison instruction.
  Value *NewRHS = nullptr;
  if (ICmpInst::isTrueWhenEqual(Pred)) {
    // Look for n+1, and grab n.
    if (AddOperator *BO = dyn_cast<AddOperator>(Sel->getOperand(1)))
      if (ConstantInt *BO1 = dyn_cast<ConstantInt>(BO->getOperand(1)))
         if (BO1->isOne() && SE.getSCEV(BO->getOperand(0)) == MaxRHS)
           NewRHS = BO->getOperand(0);
    if (AddOperator *BO = dyn_cast<AddOperator>(Sel->getOperand(2)))
      if (ConstantInt *BO1 = dyn_cast<ConstantInt>(BO->getOperand(1)))
        if (BO1->isOne() && SE.getSCEV(BO->getOperand(0)) == MaxRHS)
          NewRHS = BO->getOperand(0);
    if (!NewRHS)
      return Cond;
  } else if (SE.getSCEV(Sel->getOperand(1)) == MaxRHS)
    NewRHS = Sel->getOperand(1);
  else if (SE.getSCEV(Sel->getOperand(2)) == MaxRHS)
    NewRHS = Sel->getOperand(2);
  else if (const SCEVUnknown *SU = dyn_cast<SCEVUnknown>(MaxRHS))
    NewRHS = SU->getValue();
  else
    // Max doesn't match expected pattern.
    return Cond;

  // Determine the new comparison opcode. It may be signed or unsigned,
  // and the original comparison may be either equality or inequality.
  if (Cond->getPredicate() == CmpInst::ICMP_EQ)
    Pred = CmpInst::getInversePredicate(Pred);

  // Ok, everything looks ok to change the condition into an SLT or SGE and
  // delete the max calculation.
  ICmpInst *NewCond = new ICmpInst(Cond->getIterator(), Pred,
                                   Cond->getOperand(0), NewRHS, "scmp");

  // Delete the max calculation instructions.
  NewCond->setDebugLoc(Cond->getDebugLoc());
  Cond->replaceAllUsesWith(NewCond);
  CondUse->setUser(NewCond);
  Instruction *Cmp = cast<Instruction>(Sel->getOperand(0));
  Cond->eraseFromParent();
  Sel->eraseFromParent();
  if (Cmp->use_empty())
    Cmp->eraseFromParent();
  return NewCond;
}

/// Change loop terminating condition to use the postinc iv when possible.
void
LSRInstance::OptimizeLoopTermCond() {
  SmallPtrSet<Instruction *, 4> PostIncs;

  // We need a different set of heuristics for rotated and non-rotated loops.
  // If a loop is rotated then the latch is also the backedge, so inserting
  // post-inc expressions just before the latch is ideal. To reduce live ranges
  // it also makes sense to rewrite terminating conditions to use post-inc
  // expressions.
  //
  // If the loop is not rotated then the latch is not a backedge; the latch
  // check is done in the loop head. Adding post-inc expressions before the
  // latch will cause overlapping live-ranges of pre-inc and post-inc expressions
  // in the loop body. In this case we do *not* want to use post-inc expressions
  // in the latch check, and we want to insert post-inc expressions before
  // the backedge.
  BasicBlock *LatchBlock = L->getLoopLatch();
  SmallVector<BasicBlock*, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  if (!llvm::is_contained(ExitingBlocks, LatchBlock)) {
    // The backedge doesn't exit the loop; treat this as a head-tested loop.
    IVIncInsertPos = LatchBlock->getTerminator();
    return;
  }

  // Otherwise treat this as a rotated loop.
  for (BasicBlock *ExitingBlock : ExitingBlocks) {
    // Get the terminating condition for the loop if possible.  If we
    // can, we want to change it to use a post-incremented version of its
    // induction variable, to allow coalescing the live ranges for the IV into
    // one register value.

    BranchInst *TermBr = dyn_cast<BranchInst>(ExitingBlock->getTerminator());
    if (!TermBr)
      continue;
    // FIXME: Overly conservative, termination condition could be an 'or' etc..
    if (TermBr->isUnconditional() || !isa<ICmpInst>(TermBr->getCondition()))
      continue;

    // Search IVUsesByStride to find Cond's IVUse if there is one.
    IVStrideUse *CondUse = nullptr;
    ICmpInst *Cond = cast<ICmpInst>(TermBr->getCondition());
    if (!FindIVUserForCond(Cond, CondUse))
      continue;

    // If the trip count is computed in terms of a max (due to ScalarEvolution
    // being unable to find a sufficient guard, for example), change the loop
    // comparison to use SLT or ULT instead of NE.
    // One consequence of doing this now is that it disrupts the count-down
    // optimization. That's not always a bad thing though, because in such
    // cases it may still be worthwhile to avoid a max.
    Cond = OptimizeMax(Cond, CondUse);

    // If this exiting block dominates the latch block, it may also use
    // the post-inc value if it won't be shared with other uses.
    // Check for dominance.
    if (!DT.dominates(ExitingBlock, LatchBlock))
      continue;

    // Conservatively avoid trying to use the post-inc value in non-latch
    // exits if there may be pre-inc users in intervening blocks.
    if (LatchBlock != ExitingBlock)
      for (IVUsers::const_iterator UI = IU.begin(), E = IU.end(); UI != E; ++UI)
        // Test if the use is reachable from the exiting block. This dominator
        // query is a conservative approximation of reachability.
        if (&*UI != CondUse &&
            !DT.properlyDominates(UI->getUser()->getParent(), ExitingBlock)) {
          // Conservatively assume there may be reuse if the quotient of their
          // strides could be a legal scale.
          const SCEV *A = IU.getStride(*CondUse, L);
          const SCEV *B = IU.getStride(*UI, L);
          if (!A || !B) continue;
          if (SE.getTypeSizeInBits(A->getType()) !=
              SE.getTypeSizeInBits(B->getType())) {
            if (SE.getTypeSizeInBits(A->getType()) >
                SE.getTypeSizeInBits(B->getType()))
              B = SE.getSignExtendExpr(B, A->getType());
            else
              A = SE.getSignExtendExpr(A, B->getType());
          }
          if (const SCEVConstant *D =
                dyn_cast_or_null<SCEVConstant>(getExactSDiv(B, A, SE))) {
            const ConstantInt *C = D->getValue();
            // Stride of one or negative one can have reuse with non-addresses.
            if (C->isOne() || C->isMinusOne())
              goto decline_post_inc;
            // Avoid weird situations.
            if (C->getValue().getSignificantBits() >= 64 ||
                C->getValue().isMinSignedValue())
              goto decline_post_inc;
            // Check for possible scaled-address reuse.
            if (isAddressUse(TTI, UI->getUser(), UI->getOperandValToReplace())) {
              MemAccessTy AccessTy = getAccessType(
                  TTI, UI->getUser(), UI->getOperandValToReplace());
              int64_t Scale = C->getSExtValue();
              if (TTI.isLegalAddressingMode(AccessTy.MemTy, /*BaseGV=*/nullptr,
                                            /*BaseOffset=*/0,
                                            /*HasBaseReg=*/true, Scale,
                                            AccessTy.AddrSpace))
                goto decline_post_inc;
              Scale = -Scale;
              if (TTI.isLegalAddressingMode(AccessTy.MemTy, /*BaseGV=*/nullptr,
                                            /*BaseOffset=*/0,
                                            /*HasBaseReg=*/true, Scale,
                                            AccessTy.AddrSpace))
                goto decline_post_inc;
            }
          }
        }

    LLVM_DEBUG(dbgs() << "  Change loop exiting icmp to use postinc iv: "
                      << *Cond << '\n');

    // It's possible for the setcc instruction to be anywhere in the loop, and
    // possible for it to have multiple users.  If it is not immediately before
    // the exiting block branch, move it.
    if (Cond->getNextNonDebugInstruction() != TermBr) {
      if (Cond->hasOneUse()) {
        Cond->moveBefore(TermBr);
      } else {
        // Clone the terminating condition and insert into the loopend.
        ICmpInst *OldCond = Cond;
        Cond = cast<ICmpInst>(Cond->clone());
        Cond->setName(L->getHeader()->getName() + ".termcond");
        Cond->insertInto(ExitingBlock, TermBr->getIterator());

        // Clone the IVUse, as the old use still exists!
        CondUse = &IU.AddUser(Cond, CondUse->getOperandValToReplace());
        TermBr->replaceUsesOfWith(OldCond, Cond);
      }
    }

    // If we get to here, we know that we can transform the setcc instruction to
    // use the post-incremented version of the IV, allowing us to coalesce the
    // live ranges for the IV correctly.
    CondUse->transformToPostInc(L);
    Changed = true;

    PostIncs.insert(Cond);
  decline_post_inc:;
  }

  // Determine an insertion point for the loop induction variable increment. It
  // must dominate all the post-inc comparisons we just set up, and it must
  // dominate the loop latch edge.
  IVIncInsertPos = L->getLoopLatch()->getTerminator();
  for (Instruction *Inst : PostIncs)
    IVIncInsertPos = DT.findNearestCommonDominator(IVIncInsertPos, Inst);
}

/// Determine if the given use can accommodate a fixup at the given offset and
/// other details. If so, update the use and return true.
bool LSRInstance::reconcileNewOffset(LSRUse &LU, Immediate NewOffset,
                                     bool HasBaseReg, LSRUse::KindType Kind,
                                     MemAccessTy AccessTy) {
  Immediate NewMinOffset = LU.MinOffset;
  Immediate NewMaxOffset = LU.MaxOffset;
  MemAccessTy NewAccessTy = AccessTy;

  // Check for a mismatched kind. It's tempting to collapse mismatched kinds to
  // something conservative, however this can pessimize in the case that one of
  // the uses will have all its uses outside the loop, for example.
  if (LU.Kind != Kind)
    return false;

  // Check for a mismatched access type, and fall back conservatively as needed.
  // TODO: Be less conservative when the type is similar and can use the same
  // addressing modes.
  if (Kind == LSRUse::Address) {
    if (AccessTy.MemTy != LU.AccessTy.MemTy) {
      NewAccessTy = MemAccessTy::getUnknown(AccessTy.MemTy->getContext(),
                                            AccessTy.AddrSpace);
    }
  }

  // Conservatively assume HasBaseReg is true for now.
  if (Immediate::isKnownLT(NewOffset, LU.MinOffset)) {
    if (!isAlwaysFoldable(TTI, Kind, NewAccessTy, /*BaseGV=*/nullptr,
                          LU.MaxOffset - NewOffset, HasBaseReg))
      return false;
    NewMinOffset = NewOffset;
  } else if (Immediate::isKnownGT(NewOffset, LU.MaxOffset)) {
    if (!isAlwaysFoldable(TTI, Kind, NewAccessTy, /*BaseGV=*/nullptr,
                          NewOffset - LU.MinOffset, HasBaseReg))
      return false;
    NewMaxOffset = NewOffset;
  }

  // FIXME: We should be able to handle some level of scalable offset support
  // for 'void', but in order to get basic support up and running this is
  // being left out.
  if (NewAccessTy.MemTy && NewAccessTy.MemTy->isVoidTy() &&
      (NewMinOffset.isScalable() || NewMaxOffset.isScalable()))
    return false;

  // Update the use.
  LU.MinOffset = NewMinOffset;
  LU.MaxOffset = NewMaxOffset;
  LU.AccessTy = NewAccessTy;
  return true;
}

/// Return an LSRUse index and an offset value for a fixup which needs the given
/// expression, with the given kind and optional access type.  Either reuse an
/// existing use or create a new one, as needed.
std::pair<size_t, Immediate> LSRInstance::getUse(const SCEV *&Expr,
                                                 LSRUse::KindType Kind,
                                                 MemAccessTy AccessTy) {
  const SCEV *Copy = Expr;
  Immediate Offset = ExtractImmediate(Expr, SE);

  // Basic uses can't accept any offset, for example.
  if (!isAlwaysFoldable(TTI, Kind, AccessTy, /*BaseGV=*/ nullptr,
                        Offset, /*HasBaseReg=*/ true)) {
    Expr = Copy;
    Offset = Immediate::getFixed(0);
  }

  std::pair<UseMapTy::iterator, bool> P =
    UseMap.insert(std::make_pair(LSRUse::SCEVUseKindPair(Expr, Kind), 0));
  if (!P.second) {
    // A use already existed with this base.
    size_t LUIdx = P.first->second;
    LSRUse &LU = Uses[LUIdx];
    if (reconcileNewOffset(LU, Offset, /*HasBaseReg=*/true, Kind, AccessTy))
      // Reuse this use.
      return std::make_pair(LUIdx, Offset);
  }

  // Create a new use.
  size_t LUIdx = Uses.size();
  P.first->second = LUIdx;
  Uses.push_back(LSRUse(Kind, AccessTy));
  LSRUse &LU = Uses[LUIdx];

  LU.MinOffset = Offset;
  LU.MaxOffset = Offset;
  return std::make_pair(LUIdx, Offset);
}

/// Delete the given use from the Uses list.
void LSRInstance::DeleteUse(LSRUse &LU, size_t LUIdx) {
  if (&LU != &Uses.back())
    std::swap(LU, Uses.back());
  Uses.pop_back();

  // Update RegUses.
  RegUses.swapAndDropUse(LUIdx, Uses.size());
}

/// Look for a use distinct from OrigLU which is has a formula that has the same
/// registers as the given formula.
LSRUse *
LSRInstance::FindUseWithSimilarFormula(const Formula &OrigF,
                                       const LSRUse &OrigLU) {
  // Search all uses for the formula. This could be more clever.
  for (LSRUse &LU : Uses) {
    // Check whether this use is close enough to OrigLU, to see whether it's
    // worthwhile looking through its formulae.
    // Ignore ICmpZero uses because they may contain formulae generated by
    // GenerateICmpZeroScales, in which case adding fixup offsets may
    // be invalid.
    if (&LU != &OrigLU &&
        LU.Kind != LSRUse::ICmpZero &&
        LU.Kind == OrigLU.Kind && OrigLU.AccessTy == LU.AccessTy &&
        LU.WidestFixupType == OrigLU.WidestFixupType &&
        LU.HasFormulaWithSameRegs(OrigF)) {
      // Scan through this use's formulae.
      for (const Formula &F : LU.Formulae) {
        // Check to see if this formula has the same registers and symbols
        // as OrigF.
        if (F.BaseRegs == OrigF.BaseRegs &&
            F.ScaledReg == OrigF.ScaledReg &&
            F.BaseGV == OrigF.BaseGV &&
            F.Scale == OrigF.Scale &&
            F.UnfoldedOffset == OrigF.UnfoldedOffset) {
          if (F.BaseOffset.isZero())
            return &LU;
          // This is the formula where all the registers and symbols matched;
          // there aren't going to be any others. Since we declined it, we
          // can skip the rest of the formulae and proceed to the next LSRUse.
          break;
        }
      }
    }
  }

  // Nothing looked good.
  return nullptr;
}

void LSRInstance::CollectInterestingTypesAndFactors() {
  SmallSetVector<const SCEV *, 4> Strides;

  // Collect interesting types and strides.
  SmallVector<const SCEV *, 4> Worklist;
  for (const IVStrideUse &U : IU) {
    const SCEV *Expr = IU.getExpr(U);
    if (!Expr)
      continue;

    // Collect interesting types.
    Types.insert(SE.getEffectiveSCEVType(Expr->getType()));

    // Add strides for mentioned loops.
    Worklist.push_back(Expr);
    do {
      const SCEV *S = Worklist.pop_back_val();
      if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
        if (AR->getLoop() == L)
          Strides.insert(AR->getStepRecurrence(SE));
        Worklist.push_back(AR->getStart());
      } else if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
        append_range(Worklist, Add->operands());
      }
    } while (!Worklist.empty());
  }

  // Compute interesting factors from the set of interesting strides.
  for (SmallSetVector<const SCEV *, 4>::const_iterator
       I = Strides.begin(), E = Strides.end(); I != E; ++I)
    for (SmallSetVector<const SCEV *, 4>::const_iterator NewStrideIter =
         std::next(I); NewStrideIter != E; ++NewStrideIter) {
      const SCEV *OldStride = *I;
      const SCEV *NewStride = *NewStrideIter;

      if (SE.getTypeSizeInBits(OldStride->getType()) !=
          SE.getTypeSizeInBits(NewStride->getType())) {
        if (SE.getTypeSizeInBits(OldStride->getType()) >
            SE.getTypeSizeInBits(NewStride->getType()))
          NewStride = SE.getSignExtendExpr(NewStride, OldStride->getType());
        else
          OldStride = SE.getSignExtendExpr(OldStride, NewStride->getType());
      }
      if (const SCEVConstant *Factor =
            dyn_cast_or_null<SCEVConstant>(getExactSDiv(NewStride, OldStride,
                                                        SE, true))) {
        if (Factor->getAPInt().getSignificantBits() <= 64 && !Factor->isZero())
          Factors.insert(Factor->getAPInt().getSExtValue());
      } else if (const SCEVConstant *Factor =
                   dyn_cast_or_null<SCEVConstant>(getExactSDiv(OldStride,
                                                               NewStride,
                                                               SE, true))) {
        if (Factor->getAPInt().getSignificantBits() <= 64 && !Factor->isZero())
          Factors.insert(Factor->getAPInt().getSExtValue());
      }
    }

  // If all uses use the same type, don't bother looking for truncation-based
  // reuse.
  if (Types.size() == 1)
    Types.clear();

  LLVM_DEBUG(print_factors_and_types(dbgs()));
}

/// Helper for CollectChains that finds an IV operand (computed by an AddRec in
/// this loop) within [OI,OE) or returns OE. If IVUsers mapped Instructions to
/// IVStrideUses, we could partially skip this.
static User::op_iterator
findIVOperand(User::op_iterator OI, User::op_iterator OE,
              Loop *L, ScalarEvolution &SE) {
  for(; OI != OE; ++OI) {
    if (Instruction *Oper = dyn_cast<Instruction>(*OI)) {
      if (!SE.isSCEVable(Oper->getType()))
        continue;

      if (const SCEVAddRecExpr *AR =
          dyn_cast<SCEVAddRecExpr>(SE.getSCEV(Oper))) {
        if (AR->getLoop() == L)
          break;
      }
    }
  }
  return OI;
}

/// IVChain logic must consistently peek base TruncInst operands, so wrap it in
/// a convenient helper.
static Value *getWideOperand(Value *Oper) {
  if (TruncInst *Trunc = dyn_cast<TruncInst>(Oper))
    return Trunc->getOperand(0);
  return Oper;
}

/// Return an approximation of this SCEV expression's "base", or NULL for any
/// constant. Returning the expression itself is conservative. Returning a
/// deeper subexpression is more precise and valid as long as it isn't less
/// complex than another subexpression. For expressions involving multiple
/// unscaled values, we need to return the pointer-type SCEVUnknown. This avoids
/// forming chains across objects, such as: PrevOper==a[i], IVOper==b[i],
/// IVInc==b-a.
///
/// Since SCEVUnknown is the rightmost type, and pointers are the rightmost
/// SCEVUnknown, we simply return the rightmost SCEV operand.
static const SCEV *getExprBase(const SCEV *S) {
  switch (S->getSCEVType()) {
  default: // including scUnknown.
    return S;
  case scConstant:
  case scVScale:
    return nullptr;
  case scTruncate:
    return getExprBase(cast<SCEVTruncateExpr>(S)->getOperand());
  case scZeroExtend:
    return getExprBase(cast<SCEVZeroExtendExpr>(S)->getOperand());
  case scSignExtend:
    return getExprBase(cast<SCEVSignExtendExpr>(S)->getOperand());
  case scAddExpr: {
    // Skip over scaled operands (scMulExpr) to follow add operands as long as
    // there's nothing more complex.
    // FIXME: not sure if we want to recognize negation.
    const SCEVAddExpr *Add = cast<SCEVAddExpr>(S);
    for (const SCEV *SubExpr : reverse(Add->operands())) {
      if (SubExpr->getSCEVType() == scAddExpr)
        return getExprBase(SubExpr);

      if (SubExpr->getSCEVType() != scMulExpr)
        return SubExpr;
    }
    return S; // all operands are scaled, be conservative.
  }
  case scAddRecExpr:
    return getExprBase(cast<SCEVAddRecExpr>(S)->getStart());
  }
  llvm_unreachable("Unknown SCEV kind!");
}

/// Return true if the chain increment is profitable to expand into a loop
/// invariant value, which may require its own register. A profitable chain
/// increment will be an offset relative to the same base. We allow such offsets
/// to potentially be used as chain increment as long as it's not obviously
/// expensive to expand using real instructions.
bool IVChain::isProfitableIncrement(const SCEV *OperExpr,
                                    const SCEV *IncExpr,
                                    ScalarEvolution &SE) {
  // Aggressively form chains when -stress-ivchain.
  if (StressIVChain)
    return true;

  // Do not replace a constant offset from IV head with a nonconstant IV
  // increment.
  if (!isa<SCEVConstant>(IncExpr)) {
    const SCEV *HeadExpr = SE.getSCEV(getWideOperand(Incs[0].IVOperand));
    if (isa<SCEVConstant>(SE.getMinusSCEV(OperExpr, HeadExpr)))
      return false;
  }

  SmallPtrSet<const SCEV*, 8> Processed;
  return !isHighCostExpansion(IncExpr, Processed, SE);
}

/// Return true if the number of registers needed for the chain is estimated to
/// be less than the number required for the individual IV users. First prohibit
/// any IV users that keep the IV live across increments (the Users set should
/// be empty). Next count the number and type of increments in the chain.
///
/// Chaining IVs can lead to considerable code bloat if ISEL doesn't
/// effectively use postinc addressing modes. Only consider it profitable it the
/// increments can be computed in fewer registers when chained.
///
/// TODO: Consider IVInc free if it's already used in another chains.
static bool isProfitableChain(IVChain &Chain,
                              SmallPtrSetImpl<Instruction *> &Users,
                              ScalarEvolution &SE,
                              const TargetTransformInfo &TTI) {
  if (StressIVChain)
    return true;

  if (!Chain.hasIncs())
    return false;

  if (!Users.empty()) {
    LLVM_DEBUG(dbgs() << "Chain: " << *Chain.Incs[0].UserInst << " users:\n";
               for (Instruction *Inst
                    : Users) { dbgs() << "  " << *Inst << "\n"; });
    return false;
  }
  assert(!Chain.Incs.empty() && "empty IV chains are not allowed");

  // The chain itself may require a register, so intialize cost to 1.
  int cost = 1;

  // A complete chain likely eliminates the need for keeping the original IV in
  // a register. LSR does not currently know how to form a complete chain unless
  // the header phi already exists.
  if (isa<PHINode>(Chain.tailUserInst())
      && SE.getSCEV(Chain.tailUserInst()) == Chain.Incs[0].IncExpr) {
    --cost;
  }
  const SCEV *LastIncExpr = nullptr;
  unsigned NumConstIncrements = 0;
  unsigned NumVarIncrements = 0;
  unsigned NumReusedIncrements = 0;

  if (TTI.isProfitableLSRChainElement(Chain.Incs[0].UserInst))
    return true;

  for (const IVInc &Inc : Chain) {
    if (TTI.isProfitableLSRChainElement(Inc.UserInst))
      return true;
    if (Inc.IncExpr->isZero())
      continue;

    // Incrementing by zero or some constant is neutral. We assume constants can
    // be folded into an addressing mode or an add's immediate operand.
    if (isa<SCEVConstant>(Inc.IncExpr)) {
      ++NumConstIncrements;
      continue;
    }

    if (Inc.IncExpr == LastIncExpr)
      ++NumReusedIncrements;
    else
      ++NumVarIncrements;

    LastIncExpr = Inc.IncExpr;
  }
  // An IV chain with a single increment is handled by LSR's postinc
  // uses. However, a chain with multiple increments requires keeping the IV's
  // value live longer than it needs to be if chained.
  if (NumConstIncrements > 1)
    --cost;

  // Materializing increment expressions in the preheader that didn't exist in
  // the original code may cost a register. For example, sign-extended array
  // indices can produce ridiculous increments like this:
  // IV + ((sext i32 (2 * %s) to i64) + (-1 * (sext i32 %s to i64)))
  cost += NumVarIncrements;

  // Reusing variable increments likely saves a register to hold the multiple of
  // the stride.
  cost -= NumReusedIncrements;

  LLVM_DEBUG(dbgs() << "Chain: " << *Chain.Incs[0].UserInst << " Cost: " << cost
                    << "\n");

  return cost < 0;
}

/// Add this IV user to an existing chain or make it the head of a new chain.
void LSRInstance::ChainInstruction(Instruction *UserInst, Instruction *IVOper,
                                   SmallVectorImpl<ChainUsers> &ChainUsersVec) {
  // When IVs are used as types of varying widths, they are generally converted
  // to a wider type with some uses remaining narrow under a (free) trunc.
  Value *const NextIV = getWideOperand(IVOper);
  const SCEV *const OperExpr = SE.getSCEV(NextIV);
  const SCEV *const OperExprBase = getExprBase(OperExpr);

  // Visit all existing chains. Check if its IVOper can be computed as a
  // profitable loop invariant increment from the last link in the Chain.
  unsigned ChainIdx = 0, NChains = IVChainVec.size();
  const SCEV *LastIncExpr = nullptr;
  for (; ChainIdx < NChains; ++ChainIdx) {
    IVChain &Chain = IVChainVec[ChainIdx];

    // Prune the solution space aggressively by checking that both IV operands
    // are expressions that operate on the same unscaled SCEVUnknown. This
    // "base" will be canceled by the subsequent getMinusSCEV call. Checking
    // first avoids creating extra SCEV expressions.
    if (!StressIVChain && Chain.ExprBase != OperExprBase)
      continue;

    Value *PrevIV = getWideOperand(Chain.Incs.back().IVOperand);
    if (PrevIV->getType() != NextIV->getType())
      continue;

    // A phi node terminates a chain.
    if (isa<PHINode>(UserInst) && isa<PHINode>(Chain.tailUserInst()))
      continue;

    // The increment must be loop-invariant so it can be kept in a register.
    const SCEV *PrevExpr = SE.getSCEV(PrevIV);
    const SCEV *IncExpr = SE.getMinusSCEV(OperExpr, PrevExpr);
    if (isa<SCEVCouldNotCompute>(IncExpr) || !SE.isLoopInvariant(IncExpr, L))
      continue;

    if (Chain.isProfitableIncrement(OperExpr, IncExpr, SE)) {
      LastIncExpr = IncExpr;
      break;
    }
  }
  // If we haven't found a chain, create a new one, unless we hit the max. Don't
  // bother for phi nodes, because they must be last in the chain.
  if (ChainIdx == NChains) {
    if (isa<PHINode>(UserInst))
      return;
    if (NChains >= MaxChains && !StressIVChain) {
      LLVM_DEBUG(dbgs() << "IV Chain Limit\n");
      return;
    }
    LastIncExpr = OperExpr;
    // IVUsers may have skipped over sign/zero extensions. We don't currently
    // attempt to form chains involving extensions unless they can be hoisted
    // into this loop's AddRec.
    if (!isa<SCEVAddRecExpr>(LastIncExpr))
      return;
    ++NChains;
    IVChainVec.push_back(IVChain(IVInc(UserInst, IVOper, LastIncExpr),
                                 OperExprBase));
    ChainUsersVec.resize(NChains);
    LLVM_DEBUG(dbgs() << "IV Chain#" << ChainIdx << " Head: (" << *UserInst
                      << ") IV=" << *LastIncExpr << "\n");
  } else {
    LLVM_DEBUG(dbgs() << "IV Chain#" << ChainIdx << "  Inc: (" << *UserInst
                      << ") IV+" << *LastIncExpr << "\n");
    // Add this IV user to the end of the chain.
    IVChainVec[ChainIdx].add(IVInc(UserInst, IVOper, LastIncExpr));
  }
  IVChain &Chain = IVChainVec[ChainIdx];

  SmallPtrSet<Instruction*,4> &NearUsers = ChainUsersVec[ChainIdx].NearUsers;
  // This chain's NearUsers become FarUsers.
  if (!LastIncExpr->isZero()) {
    ChainUsersVec[ChainIdx].FarUsers.insert(NearUsers.begin(),
                                            NearUsers.end());
    NearUsers.clear();
  }

  // All other uses of IVOperand become near uses of the chain.
  // We currently ignore intermediate values within SCEV expressions, assuming
  // they will eventually be used be the current chain, or can be computed
  // from one of the chain increments. To be more precise we could
  // transitively follow its user and only add leaf IV users to the set.
  for (User *U : IVOper->users()) {
    Instruction *OtherUse = dyn_cast<Instruction>(U);
    if (!OtherUse)
      continue;
    // Uses in the chain will no longer be uses if the chain is formed.
    // Include the head of the chain in this iteration (not Chain.begin()).
    IVChain::const_iterator IncIter = Chain.Incs.begin();
    IVChain::const_iterator IncEnd = Chain.Incs.end();
    for( ; IncIter != IncEnd; ++IncIter) {
      if (IncIter->UserInst == OtherUse)
        break;
    }
    if (IncIter != IncEnd)
      continue;

    if (SE.isSCEVable(OtherUse->getType())
        && !isa<SCEVUnknown>(SE.getSCEV(OtherUse))
        && IU.isIVUserOrOperand(OtherUse)) {
      continue;
    }
    NearUsers.insert(OtherUse);
  }

  // Since this user is part of the chain, it's no longer considered a use
  // of the chain.
  ChainUsersVec[ChainIdx].FarUsers.erase(UserInst);
}

/// Populate the vector of Chains.
///
/// This decreases ILP at the architecture level. Targets with ample registers,
/// multiple memory ports, and no register renaming probably don't want
/// this. However, such targets should probably disable LSR altogether.
///
/// The job of LSR is to make a reasonable choice of induction variables across
/// the loop. Subsequent passes can easily "unchain" computation exposing more
/// ILP *within the loop* if the target wants it.
///
/// Finding the best IV chain is potentially a scheduling problem. Since LSR
/// will not reorder memory operations, it will recognize this as a chain, but
/// will generate redundant IV increments. Ideally this would be corrected later
/// by a smart scheduler:
///        = A[i]
///        = A[i+x]
/// A[i]   =
/// A[i+x] =
///
/// TODO: Walk the entire domtree within this loop, not just the path to the
/// loop latch. This will discover chains on side paths, but requires
/// maintaining multiple copies of the Chains state.
void LSRInstance::CollectChains() {
  LLVM_DEBUG(dbgs() << "Collecting IV Chains.\n");
  SmallVector<ChainUsers, 8> ChainUsersVec;

  SmallVector<BasicBlock *,8> LatchPath;
  BasicBlock *LoopHeader = L->getHeader();
  for (DomTreeNode *Rung = DT.getNode(L->getLoopLatch());
       Rung->getBlock() != LoopHeader; Rung = Rung->getIDom()) {
    LatchPath.push_back(Rung->getBlock());
  }
  LatchPath.push_back(LoopHeader);

  // Walk the instruction stream from the loop header to the loop latch.
  for (BasicBlock *BB : reverse(LatchPath)) {
    for (Instruction &I : *BB) {
      // Skip instructions that weren't seen by IVUsers analysis.
      if (isa<PHINode>(I) || !IU.isIVUserOrOperand(&I))
        continue;

      // Ignore users that are part of a SCEV expression. This way we only
      // consider leaf IV Users. This effectively rediscovers a portion of
      // IVUsers analysis but in program order this time.
      if (SE.isSCEVable(I.getType()) && !isa<SCEVUnknown>(SE.getSCEV(&I)))
          continue;

      // Remove this instruction from any NearUsers set it may be in.
      for (unsigned ChainIdx = 0, NChains = IVChainVec.size();
           ChainIdx < NChains; ++ChainIdx) {
        ChainUsersVec[ChainIdx].NearUsers.erase(&I);
      }
      // Search for operands that can be chained.
      SmallPtrSet<Instruction*, 4> UniqueOperands;
      User::op_iterator IVOpEnd = I.op_end();
      User::op_iterator IVOpIter = findIVOperand(I.op_begin(), IVOpEnd, L, SE);
      while (IVOpIter != IVOpEnd) {
        Instruction *IVOpInst = cast<Instruction>(*IVOpIter);
        if (UniqueOperands.insert(IVOpInst).second)
          ChainInstruction(&I, IVOpInst, ChainUsersVec);
        IVOpIter = findIVOperand(std::next(IVOpIter), IVOpEnd, L, SE);
      }
    } // Continue walking down the instructions.
  } // Continue walking down the domtree.
  // Visit phi backedges to determine if the chain can generate the IV postinc.
  for (PHINode &PN : L->getHeader()->phis()) {
    if (!SE.isSCEVable(PN.getType()))
      continue;

    Instruction *IncV =
        dyn_cast<Instruction>(PN.getIncomingValueForBlock(L->getLoopLatch()));
    if (IncV)
      ChainInstruction(&PN, IncV, ChainUsersVec);
  }
  // Remove any unprofitable chains.
  unsigned ChainIdx = 0;
  for (unsigned UsersIdx = 0, NChains = IVChainVec.size();
       UsersIdx < NChains; ++UsersIdx) {
    if (!isProfitableChain(IVChainVec[UsersIdx],
                           ChainUsersVec[UsersIdx].FarUsers, SE, TTI))
      continue;
    // Preserve the chain at UsesIdx.
    if (ChainIdx != UsersIdx)
      IVChainVec[ChainIdx] = IVChainVec[UsersIdx];
    FinalizeChain(IVChainVec[ChainIdx]);
    ++ChainIdx;
  }
  IVChainVec.resize(ChainIdx);
}

void LSRInstance::FinalizeChain(IVChain &Chain) {
  assert(!Chain.Incs.empty() && "empty IV chains are not allowed");
  LLVM_DEBUG(dbgs() << "Final Chain: " << *Chain.Incs[0].UserInst << "\n");
  
  for (const IVInc &Inc : Chain) {
    LLVM_DEBUG(dbgs() << "        Inc: " << *Inc.UserInst << "\n");
    auto UseI = find(Inc.UserInst->operands(), Inc.IVOperand);
    assert(UseI != Inc.UserInst->op_end() && "cannot find IV operand");
    IVIncSet.insert(UseI);
  }
}

/// Return true if the IVInc can be folded into an addressing mode.
static bool canFoldIVIncExpr(const SCEV *IncExpr, Instruction *UserInst,
                             Value *Operand, const TargetTransformInfo &TTI) {
  const SCEVConstant *IncConst = dyn_cast<SCEVConstant>(IncExpr);
  Immediate IncOffset = Immediate::getZero();
  if (IncConst) {
    if (IncConst && IncConst->getAPInt().getSignificantBits() > 64)
      return false;
    IncOffset = Immediate::getFixed(IncConst->getValue()->getSExtValue());
  } else {
    // Look for mul(vscale, constant), to detect a scalable offset.
    auto *IncVScale = dyn_cast<SCEVMulExpr>(IncExpr);
    if (!IncVScale || IncVScale->getNumOperands() != 2 ||
        !isa<SCEVVScale>(IncVScale->getOperand(1)))
      return false;
    auto *Scale = dyn_cast<SCEVConstant>(IncVScale->getOperand(0));
    if (!Scale || Scale->getType()->getScalarSizeInBits() > 64)
      return false;
    IncOffset = Immediate::getScalable(Scale->getValue()->getSExtValue());
  }

  if (!isAddressUse(TTI, UserInst, Operand))
    return false;

  MemAccessTy AccessTy = getAccessType(TTI, UserInst, Operand);
  if (!isAlwaysFoldable(TTI, LSRUse::Address, AccessTy, /*BaseGV=*/nullptr,
                        IncOffset, /*HasBaseReg=*/false))
    return false;

  return true;
}

/// Generate an add or subtract for each IVInc in a chain to materialize the IV
/// user's operand from the previous IV user's operand.
void LSRInstance::GenerateIVChain(const IVChain &Chain,
                                  SmallVectorImpl<WeakTrackingVH> &DeadInsts) {
  // Find the new IVOperand for the head of the chain. It may have been replaced
  // by LSR.
  const IVInc &Head = Chain.Incs[0];
  User::op_iterator IVOpEnd = Head.UserInst->op_end();
  // findIVOperand returns IVOpEnd if it can no longer find a valid IV user.
  User::op_iterator IVOpIter = findIVOperand(Head.UserInst->op_begin(),
                                             IVOpEnd, L, SE);
  Value *IVSrc = nullptr;
  while (IVOpIter != IVOpEnd) {
    IVSrc = getWideOperand(*IVOpIter);

    // If this operand computes the expression that the chain needs, we may use
    // it. (Check this after setting IVSrc which is used below.)
    //
    // Note that if Head.IncExpr is wider than IVSrc, then this phi is too
    // narrow for the chain, so we can no longer use it. We do allow using a
    // wider phi, assuming the LSR checked for free truncation. In that case we
    // should already have a truncate on this operand such that
    // getSCEV(IVSrc) == IncExpr.
    if (SE.getSCEV(*IVOpIter) == Head.IncExpr
        || SE.getSCEV(IVSrc) == Head.IncExpr) {
      break;
    }
    IVOpIter = findIVOperand(std::next(IVOpIter), IVOpEnd, L, SE);
  }
  if (IVOpIter == IVOpEnd) {
    // Gracefully give up on this chain.
    LLVM_DEBUG(dbgs() << "Concealed chain head: " << *Head.UserInst << "\n");
    return;
  }
  assert(IVSrc && "Failed to find IV chain source");

  LLVM_DEBUG(dbgs() << "Generate chain at: " << *IVSrc << "\n");
  Type *IVTy = IVSrc->getType();
  Type *IntTy = SE.getEffectiveSCEVType(IVTy);
  const SCEV *LeftOverExpr = nullptr;
  const SCEV *Accum = SE.getZero(IntTy);
  SmallVector<std::pair<const SCEV *, Value *>> Bases;
  Bases.emplace_back(Accum, IVSrc);

  for (const IVInc &Inc : Chain) {
    Instruction *InsertPt = Inc.UserInst;
    if (isa<PHINode>(InsertPt))
      InsertPt = L->getLoopLatch()->getTerminator();

    // IVOper will replace the current IV User's operand. IVSrc is the IV
    // value currently held in a register.
    Value *IVOper = IVSrc;
    if (!Inc.IncExpr->isZero()) {
      // IncExpr was the result of subtraction of two narrow values, so must
      // be signed.
      const SCEV *IncExpr = SE.getNoopOrSignExtend(Inc.IncExpr, IntTy);
      Accum = SE.getAddExpr(Accum, IncExpr);
      LeftOverExpr = LeftOverExpr ?
        SE.getAddExpr(LeftOverExpr, IncExpr) : IncExpr;
    }

    // Look through each base to see if any can produce a nice addressing mode.
    bool FoundBase = false;
    for (auto [MapScev, MapIVOper] : reverse(Bases)) {
      const SCEV *Remainder = SE.getMinusSCEV(Accum, MapScev);
      if (canFoldIVIncExpr(Remainder, Inc.UserInst, Inc.IVOperand, TTI)) {
        if (!Remainder->isZero()) {
          Rewriter.clearPostInc();
          Value *IncV = Rewriter.expandCodeFor(Remainder, IntTy, InsertPt);
          const SCEV *IVOperExpr =
              SE.getAddExpr(SE.getUnknown(MapIVOper), SE.getUnknown(IncV));
          IVOper = Rewriter.expandCodeFor(IVOperExpr, IVTy, InsertPt);
        } else {
          IVOper = MapIVOper;
        }

        FoundBase = true;
        break;
      }
    }
    if (!FoundBase && LeftOverExpr && !LeftOverExpr->isZero()) {
      // Expand the IV increment.
      Rewriter.clearPostInc();
      Value *IncV = Rewriter.expandCodeFor(LeftOverExpr, IntTy, InsertPt);
      const SCEV *IVOperExpr = SE.getAddExpr(SE.getUnknown(IVSrc),
                                             SE.getUnknown(IncV));
      IVOper = Rewriter.expandCodeFor(IVOperExpr, IVTy, InsertPt);

      // If an IV increment can't be folded, use it as the next IV value.
      if (!canFoldIVIncExpr(LeftOverExpr, Inc.UserInst, Inc.IVOperand, TTI)) {
        assert(IVTy == IVOper->getType() && "inconsistent IV increment type");
        Bases.emplace_back(Accum, IVOper);
        IVSrc = IVOper;
        LeftOverExpr = nullptr;
      }
    }
    Type *OperTy = Inc.IVOperand->getType();
    if (IVTy != OperTy) {
      assert(SE.getTypeSizeInBits(IVTy) >= SE.getTypeSizeInBits(OperTy) &&
             "cannot extend a chained IV");
      IRBuilder<> Builder(InsertPt);
      IVOper = Builder.CreateTruncOrBitCast(IVOper, OperTy, "lsr.chain");
    }
    Inc.UserInst->replaceUsesOfWith(Inc.IVOperand, IVOper);
    if (auto *OperandIsInstr = dyn_cast<Instruction>(Inc.IVOperand))
      DeadInsts.emplace_back(OperandIsInstr);
  }
  // If LSR created a new, wider phi, we may also replace its postinc. We only
  // do this if we also found a wide value for the head of the chain.
  if (isa<PHINode>(Chain.tailUserInst())) {
    for (PHINode &Phi : L->getHeader()->phis()) {
      if (Phi.getType() != IVSrc->getType())
        continue;
      Instruction *PostIncV = dyn_cast<Instruction>(
          Phi.getIncomingValueForBlock(L->getLoopLatch()));
      if (!PostIncV || (SE.getSCEV(PostIncV) != SE.getSCEV(IVSrc)))
        continue;
      Value *IVOper = IVSrc;
      Type *PostIncTy = PostIncV->getType();
      if (IVTy != PostIncTy) {
        assert(PostIncTy->isPointerTy() && "mixing int/ptr IV types");
        IRBuilder<> Builder(L->getLoopLatch()->getTerminator());
        Builder.SetCurrentDebugLocation(PostIncV->getDebugLoc());
        IVOper = Builder.CreatePointerCast(IVSrc, PostIncTy, "lsr.chain");
      }
      Phi.replaceUsesOfWith(PostIncV, IVOper);
      DeadInsts.emplace_back(PostIncV);
    }
  }
}

void LSRInstance::CollectFixupsAndInitialFormulae() {
  BranchInst *ExitBranch = nullptr;
  bool SaveCmp = TTI.canSaveCmp(L, &ExitBranch, &SE, &LI, &DT, &AC, &TLI);

  // For calculating baseline cost
  SmallPtrSet<const SCEV *, 16> Regs;
  DenseSet<const SCEV *> VisitedRegs;
  DenseSet<size_t> VisitedLSRUse;

  for (const IVStrideUse &U : IU) {
    Instruction *UserInst = U.getUser();
    // Skip IV users that are part of profitable IV Chains.
    User::op_iterator UseI =
        find(UserInst->operands(), U.getOperandValToReplace());
    assert(UseI != UserInst->op_end() && "cannot find IV operand");
    if (IVIncSet.count(UseI)) {
      LLVM_DEBUG(dbgs() << "Use is in profitable chain: " << **UseI << '\n');
      continue;
    }

    LSRUse::KindType Kind = LSRUse::Basic;
    MemAccessTy AccessTy;
    if (isAddressUse(TTI, UserInst, U.getOperandValToReplace())) {
      Kind = LSRUse::Address;
      AccessTy = getAccessType(TTI, UserInst, U.getOperandValToReplace());
    }

    const SCEV *S = IU.getExpr(U);
    if (!S)
      continue;
    PostIncLoopSet TmpPostIncLoops = U.getPostIncLoops();

    // Equality (== and !=) ICmps are special. We can rewrite (i == N) as
    // (N - i == 0), and this allows (N - i) to be the expression that we work
    // with rather than just N or i, so we can consider the register
    // requirements for both N and i at the same time. Limiting this code to
    // equality icmps is not a problem because all interesting loops use
    // equality icmps, thanks to IndVarSimplify.
    if (ICmpInst *CI = dyn_cast<ICmpInst>(UserInst)) {
      // If CI can be saved in some target, like replaced inside hardware loop
      // in PowerPC, no need to generate initial formulae for it.
      if (SaveCmp && CI == dyn_cast<ICmpInst>(ExitBranch->getCondition()))
        continue;
      if (CI->isEquality()) {
        // Swap the operands if needed to put the OperandValToReplace on the
        // left, for consistency.
        Value *NV = CI->getOperand(1);
        if (NV == U.getOperandValToReplace()) {
          CI->setOperand(1, CI->getOperand(0));
          CI->setOperand(0, NV);
          NV = CI->getOperand(1);
          Changed = true;
        }

        // x == y  -->  x - y == 0
        const SCEV *N = SE.getSCEV(NV);
        if (SE.isLoopInvariant(N, L) && Rewriter.isSafeToExpand(N) &&
            (!NV->getType()->isPointerTy() ||
             SE.getPointerBase(N) == SE.getPointerBase(S))) {
          // S is normalized, so normalize N before folding it into S
          // to keep the result normalized.
          N = normalizeForPostIncUse(N, TmpPostIncLoops, SE);
          if (!N)
            continue;
          Kind = LSRUse::ICmpZero;
          S = SE.getMinusSCEV(N, S);
        } else if (L->isLoopInvariant(NV) &&
                   (!isa<Instruction>(NV) ||
                    DT.dominates(cast<Instruction>(NV), L->getHeader())) &&
                   !NV->getType()->isPointerTy()) {
          // If we can't generally expand the expression (e.g. it contains
          // a divide), but it is already at a loop invariant point before the
          // loop, wrap it in an unknown (to prevent the expander from trying
          // to re-expand in a potentially unsafe way.)  The restriction to
          // integer types is required because the unknown hides the base, and
          // SCEV can't compute the difference of two unknown pointers.
          N = SE.getUnknown(NV);
          N = normalizeForPostIncUse(N, TmpPostIncLoops, SE);
          if (!N)
            continue;
          Kind = LSRUse::ICmpZero;
          S = SE.getMinusSCEV(N, S);
          assert(!isa<SCEVCouldNotCompute>(S));
        }

        // -1 and the negations of all interesting strides (except the negation
        // of -1) are now also interesting.
        for (size_t i = 0, e = Factors.size(); i != e; ++i)
          if (Factors[i] != -1)
            Factors.insert(-(uint64_t)Factors[i]);
        Factors.insert(-1);
      }
    }

    // Get or create an LSRUse.
    std::pair<size_t, Immediate> P = getUse(S, Kind, AccessTy);
    size_t LUIdx = P.first;
    Immediate Offset = P.second;
    LSRUse &LU = Uses[LUIdx];

    // Record the fixup.
    LSRFixup &LF = LU.getNewFixup();
    LF.UserInst = UserInst;
    LF.OperandValToReplace = U.getOperandValToReplace();
    LF.PostIncLoops = TmpPostIncLoops;
    LF.Offset = Offset;
    LU.AllFixupsOutsideLoop &= LF.isUseFullyOutsideLoop(L);

    // Create SCEV as Formula for calculating baseline cost
    if (!VisitedLSRUse.count(LUIdx) && !LF.isUseFullyOutsideLoop(L)) {
      Formula F;
      F.initialMatch(S, L, SE);
      BaselineCost.RateFormula(F, Regs, VisitedRegs, LU);
      VisitedLSRUse.insert(LUIdx);
    }

    if (!LU.WidestFixupType ||
        SE.getTypeSizeInBits(LU.WidestFixupType) <
        SE.getTypeSizeInBits(LF.OperandValToReplace->getType()))
      LU.WidestFixupType = LF.OperandValToReplace->getType();

    // If this is the first use of this LSRUse, give it a formula.
    if (LU.Formulae.empty()) {
      InsertInitialFormula(S, LU, LUIdx);
      CountRegisters(LU.Formulae.back(), LUIdx);
    }
  }

  LLVM_DEBUG(print_fixups(dbgs()));
}

/// Insert a formula for the given expression into the given use, separating out
/// loop-variant portions from loop-invariant and loop-computable portions.
void LSRInstance::InsertInitialFormula(const SCEV *S, LSRUse &LU,
                                       size_t LUIdx) {
  // Mark uses whose expressions cannot be expanded.
  if (!Rewriter.isSafeToExpand(S))
    LU.RigidFormula = true;

  Formula F;
  F.initialMatch(S, L, SE);
  bool Inserted = InsertFormula(LU, LUIdx, F);
  assert(Inserted && "Initial formula already exists!"); (void)Inserted;
}

/// Insert a simple single-register formula for the given expression into the
/// given use.
void
LSRInstance::InsertSupplementalFormula(const SCEV *S,
                                       LSRUse &LU, size_t LUIdx) {
  Formula F;
  F.BaseRegs.push_back(S);
  F.HasBaseReg = true;
  bool Inserted = InsertFormula(LU, LUIdx, F);
  assert(Inserted && "Supplemental formula already exists!"); (void)Inserted;
}

/// Note which registers are used by the given formula, updating RegUses.
void LSRInstance::CountRegisters(const Formula &F, size_t LUIdx) {
  if (F.ScaledReg)
    RegUses.countRegister(F.ScaledReg, LUIdx);
  for (const SCEV *BaseReg : F.BaseRegs)
    RegUses.countRegister(BaseReg, LUIdx);
}

/// If the given formula has not yet been inserted, add it to the list, and
/// return true. Return false otherwise.
bool LSRInstance::InsertFormula(LSRUse &LU, unsigned LUIdx, const Formula &F) {
  // Do not insert formula that we will not be able to expand.
  assert(isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind, LU.AccessTy, F) &&
         "Formula is illegal");

  if (!LU.InsertFormula(F, *L))
    return false;

  CountRegisters(F, LUIdx);
  return true;
}

/// Check for other uses of loop-invariant values which we're tracking. These
/// other uses will pin these values in registers, making them less profitable
/// for elimination.
/// TODO: This currently misses non-constant addrec step registers.
/// TODO: Should this give more weight to users inside the loop?
void
LSRInstance::CollectLoopInvariantFixupsAndFormulae() {
  SmallVector<const SCEV *, 8> Worklist(RegUses.begin(), RegUses.end());
  SmallPtrSet<const SCEV *, 32> Visited;

  // Don't collect outside uses if we are favoring postinc - the instructions in
  // the loop are more important than the ones outside of it.
  if (AMK == TTI::AMK_PostIndexed)
    return;

  while (!Worklist.empty()) {
    const SCEV *S = Worklist.pop_back_val();

    // Don't process the same SCEV twice
    if (!Visited.insert(S).second)
      continue;

    if (const SCEVNAryExpr *N = dyn_cast<SCEVNAryExpr>(S))
      append_range(Worklist, N->operands());
    else if (const SCEVIntegralCastExpr *C = dyn_cast<SCEVIntegralCastExpr>(S))
      Worklist.push_back(C->getOperand());
    else if (const SCEVUDivExpr *D = dyn_cast<SCEVUDivExpr>(S)) {
      Worklist.push_back(D->getLHS());
      Worklist.push_back(D->getRHS());
    } else if (const SCEVUnknown *US = dyn_cast<SCEVUnknown>(S)) {
      const Value *V = US->getValue();
      if (const Instruction *Inst = dyn_cast<Instruction>(V)) {
        // Look for instructions defined outside the loop.
        if (L->contains(Inst)) continue;
      } else if (isa<Constant>(V))
        // Constants can be re-materialized.
        continue;
      for (const Use &U : V->uses()) {
        const Instruction *UserInst = dyn_cast<Instruction>(U.getUser());
        // Ignore non-instructions.
        if (!UserInst)
          continue;
        // Don't bother if the instruction is an EHPad.
        if (UserInst->isEHPad())
          continue;
        // Ignore instructions in other functions (as can happen with
        // Constants).
        if (UserInst->getParent()->getParent() != L->getHeader()->getParent())
          continue;
        // Ignore instructions not dominated by the loop.
        const BasicBlock *UseBB = !isa<PHINode>(UserInst) ?
          UserInst->getParent() :
          cast<PHINode>(UserInst)->getIncomingBlock(
            PHINode::getIncomingValueNumForOperand(U.getOperandNo()));
        if (!DT.dominates(L->getHeader(), UseBB))
          continue;
        // Don't bother if the instruction is in a BB which ends in an EHPad.
        if (UseBB->getTerminator()->isEHPad())
          continue;

        // Ignore cases in which the currently-examined value could come from
        // a basic block terminated with an EHPad. This checks all incoming
        // blocks of the phi node since it is possible that the same incoming
        // value comes from multiple basic blocks, only some of which may end
        // in an EHPad. If any of them do, a subsequent rewrite attempt by this
        // pass would try to insert instructions into an EHPad, hitting an
        // assertion.
        if (isa<PHINode>(UserInst)) {
          const auto *PhiNode = cast<PHINode>(UserInst);
          bool HasIncompatibleEHPTerminatedBlock = false;
          llvm::Value *ExpectedValue = U;
          for (unsigned int I = 0; I < PhiNode->getNumIncomingValues(); I++) {
            if (PhiNode->getIncomingValue(I) == ExpectedValue) {
              if (PhiNode->getIncomingBlock(I)->getTerminator()->isEHPad()) {
                HasIncompatibleEHPTerminatedBlock = true;
                break;
              }
            }
          }
          if (HasIncompatibleEHPTerminatedBlock) {
            continue;
          }
        }

        // Don't bother rewriting PHIs in catchswitch blocks.
        if (isa<CatchSwitchInst>(UserInst->getParent()->getTerminator()))
          continue;
        // Ignore uses which are part of other SCEV expressions, to avoid
        // analyzing them multiple times.
        if (SE.isSCEVable(UserInst->getType())) {
          const SCEV *UserS = SE.getSCEV(const_cast<Instruction *>(UserInst));
          // If the user is a no-op, look through to its uses.
          if (!isa<SCEVUnknown>(UserS))
            continue;
          if (UserS == US) {
            Worklist.push_back(
              SE.getUnknown(const_cast<Instruction *>(UserInst)));
            continue;
          }
        }
        // Ignore icmp instructions which are already being analyzed.
        if (const ICmpInst *ICI = dyn_cast<ICmpInst>(UserInst)) {
          unsigned OtherIdx = !U.getOperandNo();
          Value *OtherOp = const_cast<Value *>(ICI->getOperand(OtherIdx));
          if (SE.hasComputableLoopEvolution(SE.getSCEV(OtherOp), L))
            continue;
        }

        std::pair<size_t, Immediate> P =
            getUse(S, LSRUse::Basic, MemAccessTy());
        size_t LUIdx = P.first;
        Immediate Offset = P.second;
        LSRUse &LU = Uses[LUIdx];
        LSRFixup &LF = LU.getNewFixup();
        LF.UserInst = const_cast<Instruction *>(UserInst);
        LF.OperandValToReplace = U;
        LF.Offset = Offset;
        LU.AllFixupsOutsideLoop &= LF.isUseFullyOutsideLoop(L);
        if (!LU.WidestFixupType ||
            SE.getTypeSizeInBits(LU.WidestFixupType) <
            SE.getTypeSizeInBits(LF.OperandValToReplace->getType()))
          LU.WidestFixupType = LF.OperandValToReplace->getType();
        InsertSupplementalFormula(US, LU, LUIdx);
        CountRegisters(LU.Formulae.back(), Uses.size() - 1);
        break;
      }
    }
  }
}

/// Split S into subexpressions which can be pulled out into separate
/// registers. If C is non-null, multiply each subexpression by C.
///
/// Return remainder expression after factoring the subexpressions captured by
/// Ops. If Ops is complete, return NULL.
static const SCEV *CollectSubexprs(const SCEV *S, const SCEVConstant *C,
                                   SmallVectorImpl<const SCEV *> &Ops,
                                   const Loop *L,
                                   ScalarEvolution &SE,
                                   unsigned Depth = 0) {
  // Arbitrarily cap recursion to protect compile time.
  if (Depth >= 3)
    return S;

  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S)) {
    // Break out add operands.
    for (const SCEV *S : Add->operands()) {
      const SCEV *Remainder = CollectSubexprs(S, C, Ops, L, SE, Depth+1);
      if (Remainder)
        Ops.push_back(C ? SE.getMulExpr(C, Remainder) : Remainder);
    }
    return nullptr;
  } else if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    // Split a non-zero base out of an addrec.
    if (AR->getStart()->isZero() || !AR->isAffine())
      return S;

    const SCEV *Remainder = CollectSubexprs(AR->getStart(),
                                            C, Ops, L, SE, Depth+1);
    // Split the non-zero AddRec unless it is part of a nested recurrence that
    // does not pertain to this loop.
    if (Remainder && (AR->getLoop() == L || !isa<SCEVAddRecExpr>(Remainder))) {
      Ops.push_back(C ? SE.getMulExpr(C, Remainder) : Remainder);
      Remainder = nullptr;
    }
    if (Remainder != AR->getStart()) {
      if (!Remainder)
        Remainder = SE.getConstant(AR->getType(), 0);
      return SE.getAddRecExpr(Remainder,
                              AR->getStepRecurrence(SE),
                              AR->getLoop(),
                              //FIXME: AR->getNoWrapFlags(SCEV::FlagNW)
                              SCEV::FlagAnyWrap);
    }
  } else if (const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(S)) {
    // Break (C * (a + b + c)) into C*a + C*b + C*c.
    if (Mul->getNumOperands() != 2)
      return S;
    if (const SCEVConstant *Op0 =
        dyn_cast<SCEVConstant>(Mul->getOperand(0))) {
      C = C ? cast<SCEVConstant>(SE.getMulExpr(C, Op0)) : Op0;
      const SCEV *Remainder =
        CollectSubexprs(Mul->getOperand(1), C, Ops, L, SE, Depth+1);
      if (Remainder)
        Ops.push_back(SE.getMulExpr(C, Remainder));
      return nullptr;
    }
  }
  return S;
}

/// Return true if the SCEV represents a value that may end up as a
/// post-increment operation.
static bool mayUsePostIncMode(const TargetTransformInfo &TTI,
                              LSRUse &LU, const SCEV *S, const Loop *L,
                              ScalarEvolution &SE) {
  if (LU.Kind != LSRUse::Address ||
      !LU.AccessTy.getType()->isIntOrIntVectorTy())
    return false;
  const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S);
  if (!AR)
    return false;
  const SCEV *LoopStep = AR->getStepRecurrence(SE);
  if (!isa<SCEVConstant>(LoopStep))
    return false;
  // Check if a post-indexed load/store can be used.
  if (TTI.isIndexedLoadLegal(TTI.MIM_PostInc, AR->getType()) ||
      TTI.isIndexedStoreLegal(TTI.MIM_PostInc, AR->getType())) {
    const SCEV *LoopStart = AR->getStart();
    if (!isa<SCEVConstant>(LoopStart) && SE.isLoopInvariant(LoopStart, L))
      return true;
  }
  return false;
}

/// Helper function for LSRInstance::GenerateReassociations.
void LSRInstance::GenerateReassociationsImpl(LSRUse &LU, unsigned LUIdx,
                                             const Formula &Base,
                                             unsigned Depth, size_t Idx,
                                             bool IsScaledReg) {
  const SCEV *BaseReg = IsScaledReg ? Base.ScaledReg : Base.BaseRegs[Idx];
  // Don't generate reassociations for the base register of a value that
  // may generate a post-increment operator. The reason is that the
  // reassociations cause extra base+register formula to be created,
  // and possibly chosen, but the post-increment is more efficient.
  if (AMK == TTI::AMK_PostIndexed && mayUsePostIncMode(TTI, LU, BaseReg, L, SE))
    return;
  SmallVector<const SCEV *, 8> AddOps;
  const SCEV *Remainder = CollectSubexprs(BaseReg, nullptr, AddOps, L, SE);
  if (Remainder)
    AddOps.push_back(Remainder);

  if (AddOps.size() == 1)
    return;

  for (SmallVectorImpl<const SCEV *>::const_iterator J = AddOps.begin(),
                                                     JE = AddOps.end();
       J != JE; ++J) {
    // Loop-variant "unknown" values are uninteresting; we won't be able to
    // do anything meaningful with them.
    if (isa<SCEVUnknown>(*J) && !SE.isLoopInvariant(*J, L))
      continue;

    // Don't pull a constant into a register if the constant could be folded
    // into an immediate field.
    if (isAlwaysFoldable(TTI, SE, LU.MinOffset, LU.MaxOffset, LU.Kind,
                         LU.AccessTy, *J, Base.getNumRegs() > 1))
      continue;

    // Collect all operands except *J.
    SmallVector<const SCEV *, 8> InnerAddOps(
        ((const SmallVector<const SCEV *, 8> &)AddOps).begin(), J);
    InnerAddOps.append(std::next(J),
                       ((const SmallVector<const SCEV *, 8> &)AddOps).end());

    // Don't leave just a constant behind in a register if the constant could
    // be folded into an immediate field.
    if (InnerAddOps.size() == 1 &&
        isAlwaysFoldable(TTI, SE, LU.MinOffset, LU.MaxOffset, LU.Kind,
                         LU.AccessTy, InnerAddOps[0], Base.getNumRegs() > 1))
      continue;

    const SCEV *InnerSum = SE.getAddExpr(InnerAddOps);
    if (InnerSum->isZero())
      continue;
    Formula F = Base;

    if (F.UnfoldedOffset.isNonZero() && F.UnfoldedOffset.isScalable())
      continue;

    // Add the remaining pieces of the add back into the new formula.
    const SCEVConstant *InnerSumSC = dyn_cast<SCEVConstant>(InnerSum);
    if (InnerSumSC && SE.getTypeSizeInBits(InnerSumSC->getType()) <= 64 &&
        TTI.isLegalAddImmediate((uint64_t)F.UnfoldedOffset.getFixedValue() +
                                InnerSumSC->getValue()->getZExtValue())) {
      F.UnfoldedOffset =
          Immediate::getFixed((uint64_t)F.UnfoldedOffset.getFixedValue() +
                              InnerSumSC->getValue()->getZExtValue());
      if (IsScaledReg)
        F.ScaledReg = nullptr;
      else
        F.BaseRegs.erase(F.BaseRegs.begin() + Idx);
    } else if (IsScaledReg)
      F.ScaledReg = InnerSum;
    else
      F.BaseRegs[Idx] = InnerSum;

    // Add J as its own register, or an unfolded immediate.
    const SCEVConstant *SC = dyn_cast<SCEVConstant>(*J);
    if (SC && SE.getTypeSizeInBits(SC->getType()) <= 64 &&
        TTI.isLegalAddImmediate((uint64_t)F.UnfoldedOffset.getFixedValue() +
                                SC->getValue()->getZExtValue()))
      F.UnfoldedOffset =
          Immediate::getFixed((uint64_t)F.UnfoldedOffset.getFixedValue() +
                              SC->getValue()->getZExtValue());
    else
      F.BaseRegs.push_back(*J);
    // We may have changed the number of register in base regs, adjust the
    // formula accordingly.
    F.canonicalize(*L);

    if (InsertFormula(LU, LUIdx, F))
      // If that formula hadn't been seen before, recurse to find more like
      // it.
      // Add check on Log16(AddOps.size()) - same as Log2_32(AddOps.size()) >> 2)
      // Because just Depth is not enough to bound compile time.
      // This means that every time AddOps.size() is greater 16^x we will add
      // x to Depth.
      GenerateReassociations(LU, LUIdx, LU.Formulae.back(),
                             Depth + 1 + (Log2_32(AddOps.size()) >> 2));
  }
}

/// Split out subexpressions from adds and the bases of addrecs.
void LSRInstance::GenerateReassociations(LSRUse &LU, unsigned LUIdx,
                                         Formula Base, unsigned Depth) {
  assert(Base.isCanonical(*L) && "Input must be in the canonical form");
  // Arbitrarily cap recursion to protect compile time.
  if (Depth >= 3)
    return;

  for (size_t i = 0, e = Base.BaseRegs.size(); i != e; ++i)
    GenerateReassociationsImpl(LU, LUIdx, Base, Depth, i);

  if (Base.Scale == 1)
    GenerateReassociationsImpl(LU, LUIdx, Base, Depth,
                               /* Idx */ -1, /* IsScaledReg */ true);
}

///  Generate a formula consisting of all of the loop-dominating registers added
/// into a single register.
void LSRInstance::GenerateCombinations(LSRUse &LU, unsigned LUIdx,
                                       Formula Base) {
  // This method is only interesting on a plurality of registers.
  if (Base.BaseRegs.size() + (Base.Scale == 1) +
          (Base.UnfoldedOffset.isNonZero()) <=
      1)
    return;

  // Flatten the representation, i.e., reg1 + 1*reg2 => reg1 + reg2, before
  // processing the formula.
  Base.unscale();
  SmallVector<const SCEV *, 4> Ops;
  Formula NewBase = Base;
  NewBase.BaseRegs.clear();
  Type *CombinedIntegerType = nullptr;
  for (const SCEV *BaseReg : Base.BaseRegs) {
    if (SE.properlyDominates(BaseReg, L->getHeader()) &&
        !SE.hasComputableLoopEvolution(BaseReg, L)) {
      if (!CombinedIntegerType)
        CombinedIntegerType = SE.getEffectiveSCEVType(BaseReg->getType());
      Ops.push_back(BaseReg);
    }
    else
      NewBase.BaseRegs.push_back(BaseReg);
  }

  // If no register is relevant, we're done.
  if (Ops.size() == 0)
    return;

  // Utility function for generating the required variants of the combined
  // registers.
  auto GenerateFormula = [&](const SCEV *Sum) {
    Formula F = NewBase;

    // TODO: If Sum is zero, it probably means ScalarEvolution missed an
    // opportunity to fold something. For now, just ignore such cases
    // rather than proceed with zero in a register.
    if (Sum->isZero())
      return;

    F.BaseRegs.push_back(Sum);
    F.canonicalize(*L);
    (void)InsertFormula(LU, LUIdx, F);
  };

  // If we collected at least two registers, generate a formula combining them.
  if (Ops.size() > 1) {
    SmallVector<const SCEV *, 4> OpsCopy(Ops); // Don't let SE modify Ops.
    GenerateFormula(SE.getAddExpr(OpsCopy));
  }

  // If we have an unfolded offset, generate a formula combining it with the
  // registers collected.
  if (NewBase.UnfoldedOffset.isNonZero() && NewBase.UnfoldedOffset.isFixed()) {
    assert(CombinedIntegerType && "Missing a type for the unfolded offset");
    Ops.push_back(SE.getConstant(CombinedIntegerType,
                                 NewBase.UnfoldedOffset.getFixedValue(), true));
    NewBase.UnfoldedOffset = Immediate::getFixed(0);
    GenerateFormula(SE.getAddExpr(Ops));
  }
}

/// Helper function for LSRInstance::GenerateSymbolicOffsets.
void LSRInstance::GenerateSymbolicOffsetsImpl(LSRUse &LU, unsigned LUIdx,
                                              const Formula &Base, size_t Idx,
                                              bool IsScaledReg) {
  const SCEV *G = IsScaledReg ? Base.ScaledReg : Base.BaseRegs[Idx];
  GlobalValue *GV = ExtractSymbol(G, SE);
  if (G->isZero() || !GV)
    return;
  Formula F = Base;
  F.BaseGV = GV;
  if (!isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind, LU.AccessTy, F))
    return;
  if (IsScaledReg)
    F.ScaledReg = G;
  else
    F.BaseRegs[Idx] = G;
  (void)InsertFormula(LU, LUIdx, F);
}

/// Generate reuse formulae using symbolic offsets.
void LSRInstance::GenerateSymbolicOffsets(LSRUse &LU, unsigned LUIdx,
                                          Formula Base) {
  // We can't add a symbolic offset if the address already contains one.
  if (Base.BaseGV) return;

  for (size_t i = 0, e = Base.BaseRegs.size(); i != e; ++i)
    GenerateSymbolicOffsetsImpl(LU, LUIdx, Base, i);
  if (Base.Scale == 1)
    GenerateSymbolicOffsetsImpl(LU, LUIdx, Base, /* Idx */ -1,
                                /* IsScaledReg */ true);
}

/// Helper function for LSRInstance::GenerateConstantOffsets.
void LSRInstance::GenerateConstantOffsetsImpl(
    LSRUse &LU, unsigned LUIdx, const Formula &Base,
    const SmallVectorImpl<Immediate> &Worklist, size_t Idx, bool IsScaledReg) {

  auto GenerateOffset = [&](const SCEV *G, Immediate Offset) {
    Formula F = Base;
    if (!Base.BaseOffset.isCompatibleImmediate(Offset))
      return;
    F.BaseOffset = Base.BaseOffset.subUnsigned(Offset);

    if (isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind, LU.AccessTy, F)) {
      // Add the offset to the base register.
      const SCEV *NewOffset = Offset.getSCEV(SE, G->getType());
      const SCEV *NewG = SE.getAddExpr(NewOffset, G);
      // If it cancelled out, drop the base register, otherwise update it.
      if (NewG->isZero()) {
        if (IsScaledReg) {
          F.Scale = 0;
          F.ScaledReg = nullptr;
        } else
          F.deleteBaseReg(F.BaseRegs[Idx]);
        F.canonicalize(*L);
      } else if (IsScaledReg)
        F.ScaledReg = NewG;
      else
        F.BaseRegs[Idx] = NewG;

      (void)InsertFormula(LU, LUIdx, F);
    }
  };

  const SCEV *G = IsScaledReg ? Base.ScaledReg : Base.BaseRegs[Idx];

  // With constant offsets and constant steps, we can generate pre-inc
  // accesses by having the offset equal the step. So, for access #0 with a
  // step of 8, we generate a G - 8 base which would require the first access
  // to be ((G - 8) + 8),+,8. The pre-indexed access then updates the pointer
  // for itself and hopefully becomes the base for other accesses. This means
  // means that a single pre-indexed access can be generated to become the new
  // base pointer for each iteration of the loop, resulting in no extra add/sub
  // instructions for pointer updating.
  if (AMK == TTI::AMK_PreIndexed && LU.Kind == LSRUse::Address) {
    if (auto *GAR = dyn_cast<SCEVAddRecExpr>(G)) {
      if (auto *StepRec =
          dyn_cast<SCEVConstant>(GAR->getStepRecurrence(SE))) {
        const APInt &StepInt = StepRec->getAPInt();
        int64_t Step = StepInt.isNegative() ?
          StepInt.getSExtValue() : StepInt.getZExtValue();

        for (Immediate Offset : Worklist) {
          if (Offset.isFixed()) {
            Offset = Immediate::getFixed(Offset.getFixedValue() - Step);
            GenerateOffset(G, Offset);
          }
        }
      }
    }
  }
  for (Immediate Offset : Worklist)
    GenerateOffset(G, Offset);

  Immediate Imm = ExtractImmediate(G, SE);
  if (G->isZero() || Imm.isZero() ||
      !Base.BaseOffset.isCompatibleImmediate(Imm))
    return;
  Formula F = Base;
  F.BaseOffset = F.BaseOffset.addUnsigned(Imm);
  if (!isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind, LU.AccessTy, F))
    return;
  if (IsScaledReg) {
    F.ScaledReg = G;
  } else {
    F.BaseRegs[Idx] = G;
    // We may generate non canonical Formula if G is a recurrent expr reg
    // related with current loop while F.ScaledReg is not.
    F.canonicalize(*L);
  }
  (void)InsertFormula(LU, LUIdx, F);
}

/// GenerateConstantOffsets - Generate reuse formulae using symbolic offsets.
void LSRInstance::GenerateConstantOffsets(LSRUse &LU, unsigned LUIdx,
                                          Formula Base) {
  // TODO: For now, just add the min and max offset, because it usually isn't
  // worthwhile looking at everything inbetween.
  SmallVector<Immediate, 2> Worklist;
  Worklist.push_back(LU.MinOffset);
  if (LU.MaxOffset != LU.MinOffset)
    Worklist.push_back(LU.MaxOffset);

  for (size_t i = 0, e = Base.BaseRegs.size(); i != e; ++i)
    GenerateConstantOffsetsImpl(LU, LUIdx, Base, Worklist, i);
  if (Base.Scale == 1)
    GenerateConstantOffsetsImpl(LU, LUIdx, Base, Worklist, /* Idx */ -1,
                                /* IsScaledReg */ true);
}

/// For ICmpZero, check to see if we can scale up the comparison. For example, x
/// == y -> x*c == y*c.
void LSRInstance::GenerateICmpZeroScales(LSRUse &LU, unsigned LUIdx,
                                         Formula Base) {
  if (LU.Kind != LSRUse::ICmpZero) return;

  // Determine the integer type for the base formula.
  Type *IntTy = Base.getType();
  if (!IntTy) return;
  if (SE.getTypeSizeInBits(IntTy) > 64) return;

  // Don't do this if there is more than one offset.
  if (LU.MinOffset != LU.MaxOffset) return;

  // Check if transformation is valid. It is illegal to multiply pointer.
  if (Base.ScaledReg && Base.ScaledReg->getType()->isPointerTy())
    return;
  for (const SCEV *BaseReg : Base.BaseRegs)
    if (BaseReg->getType()->isPointerTy())
      return;
  assert(!Base.BaseGV && "ICmpZero use is not legal!");

  // Check each interesting stride.
  for (int64_t Factor : Factors) {
    // Check that Factor can be represented by IntTy
    if (!ConstantInt::isValueValidForType(IntTy, Factor))
      continue;
    // Check that the multiplication doesn't overflow.
    if (Base.BaseOffset.isMin() && Factor == -1)
      continue;
    // Not supporting scalable immediates.
    if (Base.BaseOffset.isNonZero() && Base.BaseOffset.isScalable())
      continue;
    Immediate NewBaseOffset = Base.BaseOffset.mulUnsigned(Factor);
    assert(Factor != 0 && "Zero factor not expected!");
    if (NewBaseOffset.getFixedValue() / Factor !=
        Base.BaseOffset.getFixedValue())
      continue;
    // If the offset will be truncated at this use, check that it is in bounds.
    if (!IntTy->isPointerTy() &&
        !ConstantInt::isValueValidForType(IntTy, NewBaseOffset.getFixedValue()))
      continue;

    // Check that multiplying with the use offset doesn't overflow.
    Immediate Offset = LU.MinOffset;
    if (Offset.isMin() && Factor == -1)
      continue;
    Offset = Offset.mulUnsigned(Factor);
    if (Offset.getFixedValue() / Factor != LU.MinOffset.getFixedValue())
      continue;
    // If the offset will be truncated at this use, check that it is in bounds.
    if (!IntTy->isPointerTy() &&
        !ConstantInt::isValueValidForType(IntTy, Offset.getFixedValue()))
      continue;

    Formula F = Base;
    F.BaseOffset = NewBaseOffset;

    // Check that this scale is legal.
    if (!isLegalUse(TTI, Offset, Offset, LU.Kind, LU.AccessTy, F))
      continue;

    // Compensate for the use having MinOffset built into it.
    F.BaseOffset = F.BaseOffset.addUnsigned(Offset).subUnsigned(LU.MinOffset);

    const SCEV *FactorS = SE.getConstant(IntTy, Factor);

    // Check that multiplying with each base register doesn't overflow.
    for (size_t i = 0, e = F.BaseRegs.size(); i != e; ++i) {
      F.BaseRegs[i] = SE.getMulExpr(F.BaseRegs[i], FactorS);
      if (getExactSDiv(F.BaseRegs[i], FactorS, SE) != Base.BaseRegs[i])
        goto next;
    }

    // Check that multiplying with the scaled register doesn't overflow.
    if (F.ScaledReg) {
      F.ScaledReg = SE.getMulExpr(F.ScaledReg, FactorS);
      if (getExactSDiv(F.ScaledReg, FactorS, SE) != Base.ScaledReg)
        continue;
    }

    // Check that multiplying with the unfolded offset doesn't overflow.
    if (F.UnfoldedOffset.isNonZero()) {
      if (F.UnfoldedOffset.isMin() && Factor == -1)
        continue;
      F.UnfoldedOffset = F.UnfoldedOffset.mulUnsigned(Factor);
      if (F.UnfoldedOffset.getFixedValue() / Factor !=
          Base.UnfoldedOffset.getFixedValue())
        continue;
      // If the offset will be truncated, check that it is in bounds.
      if (!IntTy->isPointerTy() && !ConstantInt::isValueValidForType(
                                       IntTy, F.UnfoldedOffset.getFixedValue()))
        continue;
    }

    // If we make it here and it's legal, add it.
    (void)InsertFormula(LU, LUIdx, F);
  next:;
  }
}

/// Generate stride factor reuse formulae by making use of scaled-offset address
/// modes, for example.
void LSRInstance::GenerateScales(LSRUse &LU, unsigned LUIdx, Formula Base) {
  // Determine the integer type for the base formula.
  Type *IntTy = Base.getType();
  if (!IntTy) return;

  // If this Formula already has a scaled register, we can't add another one.
  // Try to unscale the formula to generate a better scale.
  if (Base.Scale != 0 && !Base.unscale())
    return;

  assert(Base.Scale == 0 && "unscale did not did its job!");

  // Check each interesting stride.
  for (int64_t Factor : Factors) {
    Base.Scale = Factor;
    Base.HasBaseReg = Base.BaseRegs.size() > 1;
    // Check whether this scale is going to be legal.
    if (!isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind, LU.AccessTy,
                    Base)) {
      // As a special-case, handle special out-of-loop Basic users specially.
      // TODO: Reconsider this special case.
      if (LU.Kind == LSRUse::Basic &&
          isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LSRUse::Special,
                     LU.AccessTy, Base) &&
          LU.AllFixupsOutsideLoop)
        LU.Kind = LSRUse::Special;
      else
        continue;
    }
    // For an ICmpZero, negating a solitary base register won't lead to
    // new solutions.
    if (LU.Kind == LSRUse::ICmpZero && !Base.HasBaseReg &&
        Base.BaseOffset.isZero() && !Base.BaseGV)
      continue;
    // For each addrec base reg, if its loop is current loop, apply the scale.
    for (size_t i = 0, e = Base.BaseRegs.size(); i != e; ++i) {
      const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Base.BaseRegs[i]);
      if (AR && (AR->getLoop() == L || LU.AllFixupsOutsideLoop)) {
        const SCEV *FactorS = SE.getConstant(IntTy, Factor);
        if (FactorS->isZero())
          continue;
        // Divide out the factor, ignoring high bits, since we'll be
        // scaling the value back up in the end.
        if (const SCEV *Quotient = getExactSDiv(AR, FactorS, SE, true))
          if (!Quotient->isZero()) {
            // TODO: This could be optimized to avoid all the copying.
            Formula F = Base;
            F.ScaledReg = Quotient;
            F.deleteBaseReg(F.BaseRegs[i]);
            // The canonical representation of 1*reg is reg, which is already in
            // Base. In that case, do not try to insert the formula, it will be
            // rejected anyway.
            if (F.Scale == 1 && (F.BaseRegs.empty() ||
                                 (AR->getLoop() != L && LU.AllFixupsOutsideLoop)))
              continue;
            // If AllFixupsOutsideLoop is true and F.Scale is 1, we may generate
            // non canonical Formula with ScaledReg's loop not being L.
            if (F.Scale == 1 && LU.AllFixupsOutsideLoop)
              F.canonicalize(*L);
            (void)InsertFormula(LU, LUIdx, F);
          }
      }
    }
  }
}

/// Extend/Truncate \p Expr to \p ToTy considering post-inc uses in \p Loops.
/// For all PostIncLoopSets in \p Loops, first de-normalize \p Expr, then
/// perform the extension/truncate and normalize again, as the normalized form
/// can result in folds that are not valid in the post-inc use contexts. The
/// expressions for all PostIncLoopSets must match, otherwise return nullptr.
static const SCEV *
getAnyExtendConsideringPostIncUses(ArrayRef<PostIncLoopSet> Loops,
                                   const SCEV *Expr, Type *ToTy,
                                   ScalarEvolution &SE) {
  const SCEV *Result = nullptr;
  for (auto &L : Loops) {
    auto *DenormExpr = denormalizeForPostIncUse(Expr, L, SE);
    const SCEV *NewDenormExpr = SE.getAnyExtendExpr(DenormExpr, ToTy);
    const SCEV *New = normalizeForPostIncUse(NewDenormExpr, L, SE);
    if (!New || (Result && New != Result))
      return nullptr;
    Result = New;
  }

  assert(Result && "failed to create expression");
  return Result;
}

/// Generate reuse formulae from different IV types.
void LSRInstance::GenerateTruncates(LSRUse &LU, unsigned LUIdx, Formula Base) {
  // Don't bother truncating symbolic values.
  if (Base.BaseGV) return;

  // Determine the integer type for the base formula.
  Type *DstTy = Base.getType();
  if (!DstTy) return;
  if (DstTy->isPointerTy())
    return;

  // It is invalid to extend a pointer type so exit early if ScaledReg or
  // any of the BaseRegs are pointers.
  if (Base.ScaledReg && Base.ScaledReg->getType()->isPointerTy())
    return;
  if (any_of(Base.BaseRegs,
             [](const SCEV *S) { return S->getType()->isPointerTy(); }))
    return;

  SmallVector<PostIncLoopSet> Loops;
  for (auto &LF : LU.Fixups)
    Loops.push_back(LF.PostIncLoops);

  for (Type *SrcTy : Types) {
    if (SrcTy != DstTy && TTI.isTruncateFree(SrcTy, DstTy)) {
      Formula F = Base;

      // Sometimes SCEV is able to prove zero during ext transform. It may
      // happen if SCEV did not do all possible transforms while creating the
      // initial node (maybe due to depth limitations), but it can do them while
      // taking ext.
      if (F.ScaledReg) {
        const SCEV *NewScaledReg =
            getAnyExtendConsideringPostIncUses(Loops, F.ScaledReg, SrcTy, SE);
        if (!NewScaledReg || NewScaledReg->isZero())
          continue;
        F.ScaledReg = NewScaledReg;
      }
      bool HasZeroBaseReg = false;
      for (const SCEV *&BaseReg : F.BaseRegs) {
        const SCEV *NewBaseReg =
            getAnyExtendConsideringPostIncUses(Loops, BaseReg, SrcTy, SE);
        if (!NewBaseReg || NewBaseReg->isZero()) {
          HasZeroBaseReg = true;
          break;
        }
        BaseReg = NewBaseReg;
      }
      if (HasZeroBaseReg)
        continue;

      // TODO: This assumes we've done basic processing on all uses and
      // have an idea what the register usage is.
      if (!F.hasRegsUsedByUsesOtherThan(LUIdx, RegUses))
        continue;

      F.canonicalize(*L);
      (void)InsertFormula(LU, LUIdx, F);
    }
  }
}

namespace {

/// Helper class for GenerateCrossUseConstantOffsets. It's used to defer
/// modifications so that the search phase doesn't have to worry about the data
/// structures moving underneath it.
struct WorkItem {
  size_t LUIdx;
  Immediate Imm;
  const SCEV *OrigReg;

  WorkItem(size_t LI, Immediate I, const SCEV *R)
      : LUIdx(LI), Imm(I), OrigReg(R) {}

  void print(raw_ostream &OS) const;
  void dump() const;
};

} // end anonymous namespace

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void WorkItem::print(raw_ostream &OS) const {
  OS << "in formulae referencing " << *OrigReg << " in use " << LUIdx
     << " , add offset " << Imm;
}

LLVM_DUMP_METHOD void WorkItem::dump() const {
  print(errs()); errs() << '\n';
}
#endif

/// Look for registers which are a constant distance apart and try to form reuse
/// opportunities between them.
void LSRInstance::GenerateCrossUseConstantOffsets() {
  // Group the registers by their value without any added constant offset.
  using ImmMapTy = std::map<Immediate, const SCEV *, KeyOrderTargetImmediate>;

  DenseMap<const SCEV *, ImmMapTy> Map;
  DenseMap<const SCEV *, SmallBitVector> UsedByIndicesMap;
  SmallVector<const SCEV *, 8> Sequence;
  for (const SCEV *Use : RegUses) {
    const SCEV *Reg = Use; // Make a copy for ExtractImmediate to modify.
    Immediate Imm = ExtractImmediate(Reg, SE);
    auto Pair = Map.insert(std::make_pair(Reg, ImmMapTy()));
    if (Pair.second)
      Sequence.push_back(Reg);
    Pair.first->second.insert(std::make_pair(Imm, Use));
    UsedByIndicesMap[Reg] |= RegUses.getUsedByIndices(Use);
  }

  // Now examine each set of registers with the same base value. Build up
  // a list of work to do and do the work in a separate step so that we're
  // not adding formulae and register counts while we're searching.
  SmallVector<WorkItem, 32> WorkItems;
  SmallSet<std::pair<size_t, Immediate>, 32, KeyOrderSizeTAndImmediate>
      UniqueItems;
  for (const SCEV *Reg : Sequence) {
    const ImmMapTy &Imms = Map.find(Reg)->second;

    // It's not worthwhile looking for reuse if there's only one offset.
    if (Imms.size() == 1)
      continue;

    LLVM_DEBUG(dbgs() << "Generating cross-use offsets for " << *Reg << ':';
               for (const auto &Entry
                    : Imms) dbgs()
               << ' ' << Entry.first;
               dbgs() << '\n');

    // Examine each offset.
    for (ImmMapTy::const_iterator J = Imms.begin(), JE = Imms.end();
         J != JE; ++J) {
      const SCEV *OrigReg = J->second;

      Immediate JImm = J->first;
      const SmallBitVector &UsedByIndices = RegUses.getUsedByIndices(OrigReg);

      if (!isa<SCEVConstant>(OrigReg) &&
          UsedByIndicesMap[Reg].count() == 1) {
        LLVM_DEBUG(dbgs() << "Skipping cross-use reuse for " << *OrigReg
                          << '\n');
        continue;
      }

      // Conservatively examine offsets between this orig reg a few selected
      // other orig regs.
      Immediate First = Imms.begin()->first;
      Immediate Last = std::prev(Imms.end())->first;
      if (!First.isCompatibleImmediate(Last)) {
        LLVM_DEBUG(dbgs() << "Skipping cross-use reuse for " << *OrigReg
                          << "\n");
        continue;
      }
      // Only scalable if both terms are scalable, or if one is scalable and
      // the other is 0.
      bool Scalable = First.isScalable() || Last.isScalable();
      int64_t FI = First.getKnownMinValue();
      int64_t LI = Last.getKnownMinValue();
      // Compute (First + Last)  / 2 without overflow using the fact that
      // First + Last = 2 * (First + Last) + (First ^ Last).
      int64_t Avg = (FI & LI) + ((FI ^ LI) >> 1);
      // If the result is negative and FI is odd and LI even (or vice versa),
      // we rounded towards -inf. Add 1 in that case, to round towards 0.
      Avg = Avg + ((FI ^ LI) & ((uint64_t)Avg >> 63));
      ImmMapTy::const_iterator OtherImms[] = {
          Imms.begin(), std::prev(Imms.end()),
          Imms.lower_bound(Immediate::get(Avg, Scalable))};
      for (const auto &M : OtherImms) {
        if (M == J || M == JE) continue;
        if (!JImm.isCompatibleImmediate(M->first))
          continue;

        // Compute the difference between the two.
        Immediate Imm = JImm.subUnsigned(M->first);
        for (unsigned LUIdx : UsedByIndices.set_bits())
          // Make a memo of this use, offset, and register tuple.
          if (UniqueItems.insert(std::make_pair(LUIdx, Imm)).second)
            WorkItems.push_back(WorkItem(LUIdx, Imm, OrigReg));
      }
    }
  }

  Map.clear();
  Sequence.clear();
  UsedByIndicesMap.clear();
  UniqueItems.clear();

  // Now iterate through the worklist and add new formulae.
  for (const WorkItem &WI : WorkItems) {
    size_t LUIdx = WI.LUIdx;
    LSRUse &LU = Uses[LUIdx];
    Immediate Imm = WI.Imm;
    const SCEV *OrigReg = WI.OrigReg;

    Type *IntTy = SE.getEffectiveSCEVType(OrigReg->getType());
    const SCEV *NegImmS = Imm.getNegativeSCEV(SE, IntTy);
    unsigned BitWidth = SE.getTypeSizeInBits(IntTy);

    // TODO: Use a more targeted data structure.
    for (size_t L = 0, LE = LU.Formulae.size(); L != LE; ++L) {
      Formula F = LU.Formulae[L];
      // FIXME: The code for the scaled and unscaled registers looks
      // very similar but slightly different. Investigate if they
      // could be merged. That way, we would not have to unscale the
      // Formula.
      F.unscale();
      // Use the immediate in the scaled register.
      if (F.ScaledReg == OrigReg) {
        if (!F.BaseOffset.isCompatibleImmediate(Imm))
          continue;
        Immediate Offset = F.BaseOffset.addUnsigned(Imm.mulUnsigned(F.Scale));
        // Don't create 50 + reg(-50).
        const SCEV *S = Offset.getNegativeSCEV(SE, IntTy);
        if (F.referencesReg(S))
          continue;
        Formula NewF = F;
        NewF.BaseOffset = Offset;
        if (!isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind, LU.AccessTy,
                        NewF))
          continue;
        NewF.ScaledReg = SE.getAddExpr(NegImmS, NewF.ScaledReg);

        // If the new scale is a constant in a register, and adding the constant
        // value to the immediate would produce a value closer to zero than the
        // immediate itself, then the formula isn't worthwhile.
        if (const SCEVConstant *C = dyn_cast<SCEVConstant>(NewF.ScaledReg)) {
          // FIXME: Do we need to do something for scalable immediates here?
          //        A scalable SCEV won't be constant, but we might still have
          //        something in the offset? Bail out for now to be safe.
          if (NewF.BaseOffset.isNonZero() && NewF.BaseOffset.isScalable())
            continue;
          if (C->getValue()->isNegative() !=
                  (NewF.BaseOffset.isLessThanZero()) &&
              (C->getAPInt().abs() * APInt(BitWidth, F.Scale))
                  .ule(std::abs(NewF.BaseOffset.getFixedValue())))
            continue;
        }

        // OK, looks good.
        NewF.canonicalize(*this->L);
        (void)InsertFormula(LU, LUIdx, NewF);
      } else {
        // Use the immediate in a base register.
        for (size_t N = 0, NE = F.BaseRegs.size(); N != NE; ++N) {
          const SCEV *BaseReg = F.BaseRegs[N];
          if (BaseReg != OrigReg)
            continue;
          Formula NewF = F;
          if (!NewF.BaseOffset.isCompatibleImmediate(Imm) ||
              !NewF.UnfoldedOffset.isCompatibleImmediate(Imm) ||
              !NewF.BaseOffset.isCompatibleImmediate(NewF.UnfoldedOffset))
            continue;
          NewF.BaseOffset = NewF.BaseOffset.addUnsigned(Imm);
          if (!isLegalUse(TTI, LU.MinOffset, LU.MaxOffset,
                          LU.Kind, LU.AccessTy, NewF)) {
            if (AMK == TTI::AMK_PostIndexed &&
                mayUsePostIncMode(TTI, LU, OrigReg, this->L, SE))
              continue;
            Immediate NewUnfoldedOffset = NewF.UnfoldedOffset.addUnsigned(Imm);
            if (!isLegalAddImmediate(TTI, NewUnfoldedOffset))
              continue;
            NewF = F;
            NewF.UnfoldedOffset = NewUnfoldedOffset;
          }
          NewF.BaseRegs[N] = SE.getAddExpr(NegImmS, BaseReg);

          // If the new formula has a constant in a register, and adding the
          // constant value to the immediate would produce a value closer to
          // zero than the immediate itself, then the formula isn't worthwhile.
          for (const SCEV *NewReg : NewF.BaseRegs)
            if (const SCEVConstant *C = dyn_cast<SCEVConstant>(NewReg)) {
              if (NewF.BaseOffset.isNonZero() && NewF.BaseOffset.isScalable())
                goto skip_formula;
              if ((C->getAPInt() + NewF.BaseOffset.getFixedValue())
                      .abs()
                      .slt(std::abs(NewF.BaseOffset.getFixedValue())) &&
                  (C->getAPInt() + NewF.BaseOffset.getFixedValue())
                          .countr_zero() >=
                      (unsigned)llvm::countr_zero<uint64_t>(
                          NewF.BaseOffset.getFixedValue()))
                goto skip_formula;
            }

          // Ok, looks good.
          NewF.canonicalize(*this->L);
          (void)InsertFormula(LU, LUIdx, NewF);
          break;
        skip_formula:;
        }
      }
    }
  }
}

/// Generate formulae for each use.
void
LSRInstance::GenerateAllReuseFormulae() {
  // This is split into multiple loops so that hasRegsUsedByUsesOtherThan
  // queries are more precise.
  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];
    for (size_t i = 0, f = LU.Formulae.size(); i != f; ++i)
      GenerateReassociations(LU, LUIdx, LU.Formulae[i]);
    for (size_t i = 0, f = LU.Formulae.size(); i != f; ++i)
      GenerateCombinations(LU, LUIdx, LU.Formulae[i]);
  }
  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];
    for (size_t i = 0, f = LU.Formulae.size(); i != f; ++i)
      GenerateSymbolicOffsets(LU, LUIdx, LU.Formulae[i]);
    for (size_t i = 0, f = LU.Formulae.size(); i != f; ++i)
      GenerateConstantOffsets(LU, LUIdx, LU.Formulae[i]);
    for (size_t i = 0, f = LU.Formulae.size(); i != f; ++i)
      GenerateICmpZeroScales(LU, LUIdx, LU.Formulae[i]);
    for (size_t i = 0, f = LU.Formulae.size(); i != f; ++i)
      GenerateScales(LU, LUIdx, LU.Formulae[i]);
  }
  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];
    for (size_t i = 0, f = LU.Formulae.size(); i != f; ++i)
      GenerateTruncates(LU, LUIdx, LU.Formulae[i]);
  }

  GenerateCrossUseConstantOffsets();

  LLVM_DEBUG(dbgs() << "\n"
                       "After generating reuse formulae:\n";
             print_uses(dbgs()));
}

/// If there are multiple formulae with the same set of registers used
/// by other uses, pick the best one and delete the others.
void LSRInstance::FilterOutUndesirableDedicatedRegisters() {
  DenseSet<const SCEV *> VisitedRegs;
  SmallPtrSet<const SCEV *, 16> Regs;
  SmallPtrSet<const SCEV *, 16> LoserRegs;
#ifndef NDEBUG
  bool ChangedFormulae = false;
#endif

  // Collect the best formula for each unique set of shared registers. This
  // is reset for each use.
  using BestFormulaeTy =
      DenseMap<SmallVector<const SCEV *, 4>, size_t, UniquifierDenseMapInfo>;

  BestFormulaeTy BestFormulae;

  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];
    LLVM_DEBUG(dbgs() << "Filtering for use "; LU.print(dbgs());
               dbgs() << '\n');

    bool Any = false;
    for (size_t FIdx = 0, NumForms = LU.Formulae.size();
         FIdx != NumForms; ++FIdx) {
      Formula &F = LU.Formulae[FIdx];

      // Some formulas are instant losers. For example, they may depend on
      // nonexistent AddRecs from other loops. These need to be filtered
      // immediately, otherwise heuristics could choose them over others leading
      // to an unsatisfactory solution. Passing LoserRegs into RateFormula here
      // avoids the need to recompute this information across formulae using the
      // same bad AddRec. Passing LoserRegs is also essential unless we remove
      // the corresponding bad register from the Regs set.
      Cost CostF(L, SE, TTI, AMK);
      Regs.clear();
      CostF.RateFormula(F, Regs, VisitedRegs, LU, &LoserRegs);
      if (CostF.isLoser()) {
        // During initial formula generation, undesirable formulae are generated
        // by uses within other loops that have some non-trivial address mode or
        // use the postinc form of the IV. LSR needs to provide these formulae
        // as the basis of rediscovering the desired formula that uses an AddRec
        // corresponding to the existing phi. Once all formulae have been
        // generated, these initial losers may be pruned.
        LLVM_DEBUG(dbgs() << "  Filtering loser "; F.print(dbgs());
                   dbgs() << "\n");
      }
      else {
        SmallVector<const SCEV *, 4> Key;
        for (const SCEV *Reg : F.BaseRegs) {
          if (RegUses.isRegUsedByUsesOtherThan(Reg, LUIdx))
            Key.push_back(Reg);
        }
        if (F.ScaledReg &&
            RegUses.isRegUsedByUsesOtherThan(F.ScaledReg, LUIdx))
          Key.push_back(F.ScaledReg);
        // Unstable sort by host order ok, because this is only used for
        // uniquifying.
        llvm::sort(Key);

        std::pair<BestFormulaeTy::const_iterator, bool> P =
          BestFormulae.insert(std::make_pair(Key, FIdx));
        if (P.second)
          continue;

        Formula &Best = LU.Formulae[P.first->second];

        Cost CostBest(L, SE, TTI, AMK);
        Regs.clear();
        CostBest.RateFormula(Best, Regs, VisitedRegs, LU);
        if (CostF.isLess(CostBest))
          std::swap(F, Best);
        LLVM_DEBUG(dbgs() << "  Filtering out formula "; F.print(dbgs());
                   dbgs() << "\n"
                             "    in favor of formula ";
                   Best.print(dbgs()); dbgs() << '\n');
      }
#ifndef NDEBUG
      ChangedFormulae = true;
#endif
      LU.DeleteFormula(F);
      --FIdx;
      --NumForms;
      Any = true;
    }

    // Now that we've filtered out some formulae, recompute the Regs set.
    if (Any)
      LU.RecomputeRegs(LUIdx, RegUses);

    // Reset this to prepare for the next use.
    BestFormulae.clear();
  }

  LLVM_DEBUG(if (ChangedFormulae) {
    dbgs() << "\n"
              "After filtering out undesirable candidates:\n";
    print_uses(dbgs());
  });
}

/// Estimate the worst-case number of solutions the solver might have to
/// consider. It almost never considers this many solutions because it prune the
/// search space, but the pruning isn't always sufficient.
size_t LSRInstance::EstimateSearchSpaceComplexity() const {
  size_t Power = 1;
  for (const LSRUse &LU : Uses) {
    size_t FSize = LU.Formulae.size();
    if (FSize >= ComplexityLimit) {
      Power = ComplexityLimit;
      break;
    }
    Power *= FSize;
    if (Power >= ComplexityLimit)
      break;
  }
  return Power;
}

/// When one formula uses a superset of the registers of another formula, it
/// won't help reduce register pressure (though it may not necessarily hurt
/// register pressure); remove it to simplify the system.
void LSRInstance::NarrowSearchSpaceByDetectingSupersets() {
  if (EstimateSearchSpaceComplexity() >= ComplexityLimit) {
    LLVM_DEBUG(dbgs() << "The search space is too complex.\n");

    LLVM_DEBUG(dbgs() << "Narrowing the search space by eliminating formulae "
                         "which use a superset of registers used by other "
                         "formulae.\n");

    for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
      LSRUse &LU = Uses[LUIdx];
      bool Any = false;
      for (size_t i = 0, e = LU.Formulae.size(); i != e; ++i) {
        Formula &F = LU.Formulae[i];
        if (F.BaseOffset.isNonZero() && F.BaseOffset.isScalable())
          continue;
        // Look for a formula with a constant or GV in a register. If the use
        // also has a formula with that same value in an immediate field,
        // delete the one that uses a register.
        for (SmallVectorImpl<const SCEV *>::const_iterator
             I = F.BaseRegs.begin(), E = F.BaseRegs.end(); I != E; ++I) {
          if (const SCEVConstant *C = dyn_cast<SCEVConstant>(*I)) {
            Formula NewF = F;
            //FIXME: Formulas should store bitwidth to do wrapping properly.
            //       See PR41034.
            NewF.BaseOffset =
                Immediate::getFixed(NewF.BaseOffset.getFixedValue() +
                                    (uint64_t)C->getValue()->getSExtValue());
            NewF.BaseRegs.erase(NewF.BaseRegs.begin() +
                                (I - F.BaseRegs.begin()));
            if (LU.HasFormulaWithSameRegs(NewF)) {
              LLVM_DEBUG(dbgs() << "  Deleting "; F.print(dbgs());
                         dbgs() << '\n');
              LU.DeleteFormula(F);
              --i;
              --e;
              Any = true;
              break;
            }
          } else if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(*I)) {
            if (GlobalValue *GV = dyn_cast<GlobalValue>(U->getValue()))
              if (!F.BaseGV) {
                Formula NewF = F;
                NewF.BaseGV = GV;
                NewF.BaseRegs.erase(NewF.BaseRegs.begin() +
                                    (I - F.BaseRegs.begin()));
                if (LU.HasFormulaWithSameRegs(NewF)) {
                  LLVM_DEBUG(dbgs() << "  Deleting "; F.print(dbgs());
                             dbgs() << '\n');
                  LU.DeleteFormula(F);
                  --i;
                  --e;
                  Any = true;
                  break;
                }
              }
          }
        }
      }
      if (Any)
        LU.RecomputeRegs(LUIdx, RegUses);
    }

    LLVM_DEBUG(dbgs() << "After pre-selection:\n"; print_uses(dbgs()));
  }
}

/// When there are many registers for expressions like A, A+1, A+2, etc.,
/// allocate a single register for them.
void LSRInstance::NarrowSearchSpaceByCollapsingUnrolledCode() {
  if (EstimateSearchSpaceComplexity() < ComplexityLimit)
    return;

  LLVM_DEBUG(
      dbgs() << "The search space is too complex.\n"
                "Narrowing the search space by assuming that uses separated "
                "by a constant offset will use the same registers.\n");

  // This is especially useful for unrolled loops.

  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];
    for (const Formula &F : LU.Formulae) {
      if (F.BaseOffset.isZero() || (F.Scale != 0 && F.Scale != 1))
        continue;

      LSRUse *LUThatHas = FindUseWithSimilarFormula(F, LU);
      if (!LUThatHas)
        continue;

      if (!reconcileNewOffset(*LUThatHas, F.BaseOffset, /*HasBaseReg=*/ false,
                              LU.Kind, LU.AccessTy))
        continue;

      LLVM_DEBUG(dbgs() << "  Deleting use "; LU.print(dbgs()); dbgs() << '\n');

      LUThatHas->AllFixupsOutsideLoop &= LU.AllFixupsOutsideLoop;

      // Transfer the fixups of LU to LUThatHas.
      for (LSRFixup &Fixup : LU.Fixups) {
        Fixup.Offset += F.BaseOffset;
        LUThatHas->pushFixup(Fixup);
        LLVM_DEBUG(dbgs() << "New fixup has offset " << Fixup.Offset << '\n');
      }

      // Delete formulae from the new use which are no longer legal.
      bool Any = false;
      for (size_t i = 0, e = LUThatHas->Formulae.size(); i != e; ++i) {
        Formula &F = LUThatHas->Formulae[i];
        if (!isLegalUse(TTI, LUThatHas->MinOffset, LUThatHas->MaxOffset,
                        LUThatHas->Kind, LUThatHas->AccessTy, F)) {
          LLVM_DEBUG(dbgs() << "  Deleting "; F.print(dbgs()); dbgs() << '\n');
          LUThatHas->DeleteFormula(F);
          --i;
          --e;
          Any = true;
        }
      }

      if (Any)
        LUThatHas->RecomputeRegs(LUThatHas - &Uses.front(), RegUses);

      // Delete the old use.
      DeleteUse(LU, LUIdx);
      --LUIdx;
      --NumUses;
      break;
    }
  }

  LLVM_DEBUG(dbgs() << "After pre-selection:\n"; print_uses(dbgs()));
}

/// Call FilterOutUndesirableDedicatedRegisters again, if necessary, now that
/// we've done more filtering, as it may be able to find more formulae to
/// eliminate.
void LSRInstance::NarrowSearchSpaceByRefilteringUndesirableDedicatedRegisters(){
  if (EstimateSearchSpaceComplexity() >= ComplexityLimit) {
    LLVM_DEBUG(dbgs() << "The search space is too complex.\n");

    LLVM_DEBUG(dbgs() << "Narrowing the search space by re-filtering out "
                         "undesirable dedicated registers.\n");

    FilterOutUndesirableDedicatedRegisters();

    LLVM_DEBUG(dbgs() << "After pre-selection:\n"; print_uses(dbgs()));
  }
}

/// If a LSRUse has multiple formulae with the same ScaledReg and Scale.
/// Pick the best one and delete the others.
/// This narrowing heuristic is to keep as many formulae with different
/// Scale and ScaledReg pair as possible while narrowing the search space.
/// The benefit is that it is more likely to find out a better solution
/// from a formulae set with more Scale and ScaledReg variations than
/// a formulae set with the same Scale and ScaledReg. The picking winner
/// reg heuristic will often keep the formulae with the same Scale and
/// ScaledReg and filter others, and we want to avoid that if possible.
void LSRInstance::NarrowSearchSpaceByFilterFormulaWithSameScaledReg() {
  if (EstimateSearchSpaceComplexity() < ComplexityLimit)
    return;

  LLVM_DEBUG(
      dbgs() << "The search space is too complex.\n"
                "Narrowing the search space by choosing the best Formula "
                "from the Formulae with the same Scale and ScaledReg.\n");

  // Map the "Scale * ScaledReg" pair to the best formula of current LSRUse.
  using BestFormulaeTy = DenseMap<std::pair<const SCEV *, int64_t>, size_t>;

  BestFormulaeTy BestFormulae;
#ifndef NDEBUG
  bool ChangedFormulae = false;
#endif
  DenseSet<const SCEV *> VisitedRegs;
  SmallPtrSet<const SCEV *, 16> Regs;

  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];
    LLVM_DEBUG(dbgs() << "Filtering for use "; LU.print(dbgs());
               dbgs() << '\n');

    // Return true if Formula FA is better than Formula FB.
    auto IsBetterThan = [&](Formula &FA, Formula &FB) {
      // First we will try to choose the Formula with fewer new registers.
      // For a register used by current Formula, the more the register is
      // shared among LSRUses, the less we increase the register number
      // counter of the formula.
      size_t FARegNum = 0;
      for (const SCEV *Reg : FA.BaseRegs) {
        const SmallBitVector &UsedByIndices = RegUses.getUsedByIndices(Reg);
        FARegNum += (NumUses - UsedByIndices.count() + 1);
      }
      size_t FBRegNum = 0;
      for (const SCEV *Reg : FB.BaseRegs) {
        const SmallBitVector &UsedByIndices = RegUses.getUsedByIndices(Reg);
        FBRegNum += (NumUses - UsedByIndices.count() + 1);
      }
      if (FARegNum != FBRegNum)
        return FARegNum < FBRegNum;

      // If the new register numbers are the same, choose the Formula with
      // less Cost.
      Cost CostFA(L, SE, TTI, AMK);
      Cost CostFB(L, SE, TTI, AMK);
      Regs.clear();
      CostFA.RateFormula(FA, Regs, VisitedRegs, LU);
      Regs.clear();
      CostFB.RateFormula(FB, Regs, VisitedRegs, LU);
      return CostFA.isLess(CostFB);
    };

    bool Any = false;
    for (size_t FIdx = 0, NumForms = LU.Formulae.size(); FIdx != NumForms;
         ++FIdx) {
      Formula &F = LU.Formulae[FIdx];
      if (!F.ScaledReg)
        continue;
      auto P = BestFormulae.insert({{F.ScaledReg, F.Scale}, FIdx});
      if (P.second)
        continue;

      Formula &Best = LU.Formulae[P.first->second];
      if (IsBetterThan(F, Best))
        std::swap(F, Best);
      LLVM_DEBUG(dbgs() << "  Filtering out formula "; F.print(dbgs());
                 dbgs() << "\n"
                           "    in favor of formula ";
                 Best.print(dbgs()); dbgs() << '\n');
#ifndef NDEBUG
      ChangedFormulae = true;
#endif
      LU.DeleteFormula(F);
      --FIdx;
      --NumForms;
      Any = true;
    }
    if (Any)
      LU.RecomputeRegs(LUIdx, RegUses);

    // Reset this to prepare for the next use.
    BestFormulae.clear();
  }

  LLVM_DEBUG(if (ChangedFormulae) {
    dbgs() << "\n"
              "After filtering out undesirable candidates:\n";
    print_uses(dbgs());
  });
}

/// If we are over the complexity limit, filter out any post-inc prefering
/// variables to only post-inc values.
void LSRInstance::NarrowSearchSpaceByFilterPostInc() {
  if (AMK != TTI::AMK_PostIndexed)
    return;
  if (EstimateSearchSpaceComplexity() < ComplexityLimit)
    return;

  LLVM_DEBUG(dbgs() << "The search space is too complex.\n"
                       "Narrowing the search space by choosing the lowest "
                       "register Formula for PostInc Uses.\n");

  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];

    if (LU.Kind != LSRUse::Address)
      continue;
    if (!TTI.isIndexedLoadLegal(TTI.MIM_PostInc, LU.AccessTy.getType()) &&
        !TTI.isIndexedStoreLegal(TTI.MIM_PostInc, LU.AccessTy.getType()))
      continue;

    size_t MinRegs = std::numeric_limits<size_t>::max();
    for (const Formula &F : LU.Formulae)
      MinRegs = std::min(F.getNumRegs(), MinRegs);

    bool Any = false;
    for (size_t FIdx = 0, NumForms = LU.Formulae.size(); FIdx != NumForms;
         ++FIdx) {
      Formula &F = LU.Formulae[FIdx];
      if (F.getNumRegs() > MinRegs) {
        LLVM_DEBUG(dbgs() << "  Filtering out formula "; F.print(dbgs());
                   dbgs() << "\n");
        LU.DeleteFormula(F);
        --FIdx;
        --NumForms;
        Any = true;
      }
    }
    if (Any)
      LU.RecomputeRegs(LUIdx, RegUses);

    if (EstimateSearchSpaceComplexity() < ComplexityLimit)
      break;
  }

  LLVM_DEBUG(dbgs() << "After pre-selection:\n"; print_uses(dbgs()));
}

/// The function delete formulas with high registers number expectation.
/// Assuming we don't know the value of each formula (already delete
/// all inefficient), generate probability of not selecting for each
/// register.
/// For example,
/// Use1:
///  reg(a) + reg({0,+,1})
///  reg(a) + reg({-1,+,1}) + 1
///  reg({a,+,1})
/// Use2:
///  reg(b) + reg({0,+,1})
///  reg(b) + reg({-1,+,1}) + 1
///  reg({b,+,1})
/// Use3:
///  reg(c) + reg(b) + reg({0,+,1})
///  reg(c) + reg({b,+,1})
///
/// Probability of not selecting
///                 Use1   Use2    Use3
/// reg(a)         (1/3) *   1   *   1
/// reg(b)           1   * (1/3) * (1/2)
/// reg({0,+,1})   (2/3) * (2/3) * (1/2)
/// reg({-1,+,1})  (2/3) * (2/3) *   1
/// reg({a,+,1})   (2/3) *   1   *   1
/// reg({b,+,1})     1   * (2/3) * (2/3)
/// reg(c)           1   *   1   *   0
///
/// Now count registers number mathematical expectation for each formula:
/// Note that for each use we exclude probability if not selecting for the use.
/// For example for Use1 probability for reg(a) would be just 1 * 1 (excluding
/// probabilty 1/3 of not selecting for Use1).
/// Use1:
///  reg(a) + reg({0,+,1})          1 + 1/3       -- to be deleted
///  reg(a) + reg({-1,+,1}) + 1     1 + 4/9       -- to be deleted
///  reg({a,+,1})                   1
/// Use2:
///  reg(b) + reg({0,+,1})          1/2 + 1/3     -- to be deleted
///  reg(b) + reg({-1,+,1}) + 1     1/2 + 2/3     -- to be deleted
///  reg({b,+,1})                   2/3
/// Use3:
///  reg(c) + reg(b) + reg({0,+,1}) 1 + 1/3 + 4/9 -- to be deleted
///  reg(c) + reg({b,+,1})          1 + 2/3
void LSRInstance::NarrowSearchSpaceByDeletingCostlyFormulas() {
  if (EstimateSearchSpaceComplexity() < ComplexityLimit)
    return;
  // Ok, we have too many of formulae on our hands to conveniently handle.
  // Use a rough heuristic to thin out the list.

  // Set of Regs wich will be 100% used in final solution.
  // Used in each formula of a solution (in example above this is reg(c)).
  // We can skip them in calculations.
  SmallPtrSet<const SCEV *, 4> UniqRegs;
  LLVM_DEBUG(dbgs() << "The search space is too complex.\n");

  // Map each register to probability of not selecting
  DenseMap <const SCEV *, float> RegNumMap;
  for (const SCEV *Reg : RegUses) {
    if (UniqRegs.count(Reg))
      continue;
    float PNotSel = 1;
    for (const LSRUse &LU : Uses) {
      if (!LU.Regs.count(Reg))
        continue;
      float P = LU.getNotSelectedProbability(Reg);
      if (P != 0.0)
        PNotSel *= P;
      else
        UniqRegs.insert(Reg);
    }
    RegNumMap.insert(std::make_pair(Reg, PNotSel));
  }

  LLVM_DEBUG(
      dbgs() << "Narrowing the search space by deleting costly formulas\n");

  // Delete formulas where registers number expectation is high.
  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
    LSRUse &LU = Uses[LUIdx];
    // If nothing to delete - continue.
    if (LU.Formulae.size() < 2)
      continue;
    // This is temporary solution to test performance. Float should be
    // replaced with round independent type (based on integers) to avoid
    // different results for different target builds.
    float FMinRegNum = LU.Formulae[0].getNumRegs();
    float FMinARegNum = LU.Formulae[0].getNumRegs();
    size_t MinIdx = 0;
    for (size_t i = 0, e = LU.Formulae.size(); i != e; ++i) {
      Formula &F = LU.Formulae[i];
      float FRegNum = 0;
      float FARegNum = 0;
      for (const SCEV *BaseReg : F.BaseRegs) {
        if (UniqRegs.count(BaseReg))
          continue;
        FRegNum += RegNumMap[BaseReg] / LU.getNotSelectedProbability(BaseReg);
        if (isa<SCEVAddRecExpr>(BaseReg))
          FARegNum +=
              RegNumMap[BaseReg] / LU.getNotSelectedProbability(BaseReg);
      }
      if (const SCEV *ScaledReg = F.ScaledReg) {
        if (!UniqRegs.count(ScaledReg)) {
          FRegNum +=
              RegNumMap[ScaledReg] / LU.getNotSelectedProbability(ScaledReg);
          if (isa<SCEVAddRecExpr>(ScaledReg))
            FARegNum +=
                RegNumMap[ScaledReg] / LU.getNotSelectedProbability(ScaledReg);
        }
      }
      if (FMinRegNum > FRegNum ||
          (FMinRegNum == FRegNum && FMinARegNum > FARegNum)) {
        FMinRegNum = FRegNum;
        FMinARegNum = FARegNum;
        MinIdx = i;
      }
    }
    LLVM_DEBUG(dbgs() << "  The formula "; LU.Formulae[MinIdx].print(dbgs());
               dbgs() << " with min reg num " << FMinRegNum << '\n');
    if (MinIdx != 0)
      std::swap(LU.Formulae[MinIdx], LU.Formulae[0]);
    while (LU.Formulae.size() != 1) {
      LLVM_DEBUG(dbgs() << "  Deleting "; LU.Formulae.back().print(dbgs());
                 dbgs() << '\n');
      LU.Formulae.pop_back();
    }
    LU.RecomputeRegs(LUIdx, RegUses);
    assert(LU.Formulae.size() == 1 && "Should be exactly 1 min regs formula");
    Formula &F = LU.Formulae[0];
    LLVM_DEBUG(dbgs() << "  Leaving only "; F.print(dbgs()); dbgs() << '\n');
    // When we choose the formula, the regs become unique.
    UniqRegs.insert(F.BaseRegs.begin(), F.BaseRegs.end());
    if (F.ScaledReg)
      UniqRegs.insert(F.ScaledReg);
  }
  LLVM_DEBUG(dbgs() << "After pre-selection:\n"; print_uses(dbgs()));
}

// Check if Best and Reg are SCEVs separated by a constant amount C, and if so
// would the addressing offset +C would be legal where the negative offset -C is
// not.
static bool IsSimplerBaseSCEVForTarget(const TargetTransformInfo &TTI,
                                       ScalarEvolution &SE, const SCEV *Best,
                                       const SCEV *Reg,
                                       MemAccessTy AccessType) {
  if (Best->getType() != Reg->getType() ||
      (isa<SCEVAddRecExpr>(Best) && isa<SCEVAddRecExpr>(Reg) &&
       cast<SCEVAddRecExpr>(Best)->getLoop() !=
           cast<SCEVAddRecExpr>(Reg)->getLoop()))
    return false;
  const auto *Diff = dyn_cast<SCEVConstant>(SE.getMinusSCEV(Best, Reg));
  if (!Diff)
    return false;

  return TTI.isLegalAddressingMode(
             AccessType.MemTy, /*BaseGV=*/nullptr,
             /*BaseOffset=*/Diff->getAPInt().getSExtValue(),
             /*HasBaseReg=*/true, /*Scale=*/0, AccessType.AddrSpace) &&
         !TTI.isLegalAddressingMode(
             AccessType.MemTy, /*BaseGV=*/nullptr,
             /*BaseOffset=*/-Diff->getAPInt().getSExtValue(),
             /*HasBaseReg=*/true, /*Scale=*/0, AccessType.AddrSpace);
}

/// Pick a register which seems likely to be profitable, and then in any use
/// which has any reference to that register, delete all formulae which do not
/// reference that register.
void LSRInstance::NarrowSearchSpaceByPickingWinnerRegs() {
  // With all other options exhausted, loop until the system is simple
  // enough to handle.
  SmallPtrSet<const SCEV *, 4> Taken;
  while (EstimateSearchSpaceComplexity() >= ComplexityLimit) {
    // Ok, we have too many of formulae on our hands to conveniently handle.
    // Use a rough heuristic to thin out the list.
    LLVM_DEBUG(dbgs() << "The search space is too complex.\n");

    // Pick the register which is used by the most LSRUses, which is likely
    // to be a good reuse register candidate.
    const SCEV *Best = nullptr;
    unsigned BestNum = 0;
    for (const SCEV *Reg : RegUses) {
      if (Taken.count(Reg))
        continue;
      if (!Best) {
        Best = Reg;
        BestNum = RegUses.getUsedByIndices(Reg).count();
      } else {
        unsigned Count = RegUses.getUsedByIndices(Reg).count();
        if (Count > BestNum) {
          Best = Reg;
          BestNum = Count;
        }

        // If the scores are the same, but the Reg is simpler for the target
        // (for example {x,+,1} as opposed to {x+C,+,1}, where the target can
        // handle +C but not -C), opt for the simpler formula.
        if (Count == BestNum) {
          int LUIdx = RegUses.getUsedByIndices(Reg).find_first();
          if (LUIdx >= 0 && Uses[LUIdx].Kind == LSRUse::Address &&
              IsSimplerBaseSCEVForTarget(TTI, SE, Best, Reg,
                                         Uses[LUIdx].AccessTy)) {
            Best = Reg;
            BestNum = Count;
          }
        }
      }
    }
    assert(Best && "Failed to find best LSRUse candidate");

    LLVM_DEBUG(dbgs() << "Narrowing the search space by assuming " << *Best
                      << " will yield profitable reuse.\n");
    Taken.insert(Best);

    // In any use with formulae which references this register, delete formulae
    // which don't reference it.
    for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx) {
      LSRUse &LU = Uses[LUIdx];
      if (!LU.Regs.count(Best)) continue;

      bool Any = false;
      for (size_t i = 0, e = LU.Formulae.size(); i != e; ++i) {
        Formula &F = LU.Formulae[i];
        if (!F.referencesReg(Best)) {
          LLVM_DEBUG(dbgs() << "  Deleting "; F.print(dbgs()); dbgs() << '\n');
          LU.DeleteFormula(F);
          --e;
          --i;
          Any = true;
          assert(e != 0 && "Use has no formulae left! Is Regs inconsistent?");
          continue;
        }
      }

      if (Any)
        LU.RecomputeRegs(LUIdx, RegUses);
    }

    LLVM_DEBUG(dbgs() << "After pre-selection:\n"; print_uses(dbgs()));
  }
}

/// If there are an extraordinary number of formulae to choose from, use some
/// rough heuristics to prune down the number of formulae. This keeps the main
/// solver from taking an extraordinary amount of time in some worst-case
/// scenarios.
void LSRInstance::NarrowSearchSpaceUsingHeuristics() {
  NarrowSearchSpaceByDetectingSupersets();
  NarrowSearchSpaceByCollapsingUnrolledCode();
  NarrowSearchSpaceByRefilteringUndesirableDedicatedRegisters();
  if (FilterSameScaledReg)
    NarrowSearchSpaceByFilterFormulaWithSameScaledReg();
  NarrowSearchSpaceByFilterPostInc();
  if (LSRExpNarrow)
    NarrowSearchSpaceByDeletingCostlyFormulas();
  else
    NarrowSearchSpaceByPickingWinnerRegs();
}

/// This is the recursive solver.
void LSRInstance::SolveRecurse(SmallVectorImpl<const Formula *> &Solution,
                               Cost &SolutionCost,
                               SmallVectorImpl<const Formula *> &Workspace,
                               const Cost &CurCost,
                               const SmallPtrSet<const SCEV *, 16> &CurRegs,
                               DenseSet<const SCEV *> &VisitedRegs) const {
  // Some ideas:
  //  - prune more:
  //    - use more aggressive filtering
  //    - sort the formula so that the most profitable solutions are found first
  //    - sort the uses too
  //  - search faster:
  //    - don't compute a cost, and then compare. compare while computing a cost
  //      and bail early.
  //    - track register sets with SmallBitVector

  const LSRUse &LU = Uses[Workspace.size()];

  // If this use references any register that's already a part of the
  // in-progress solution, consider it a requirement that a formula must
  // reference that register in order to be considered. This prunes out
  // unprofitable searching.
  SmallSetVector<const SCEV *, 4> ReqRegs;
  for (const SCEV *S : CurRegs)
    if (LU.Regs.count(S))
      ReqRegs.insert(S);

  SmallPtrSet<const SCEV *, 16> NewRegs;
  Cost NewCost(L, SE, TTI, AMK);
  for (const Formula &F : LU.Formulae) {
    // Ignore formulae which may not be ideal in terms of register reuse of
    // ReqRegs.  The formula should use all required registers before
    // introducing new ones.
    // This can sometimes (notably when trying to favour postinc) lead to
    // sub-optimial decisions. There it is best left to the cost modelling to
    // get correct.
    if (AMK != TTI::AMK_PostIndexed || LU.Kind != LSRUse::Address) {
      int NumReqRegsToFind = std::min(F.getNumRegs(), ReqRegs.size());
      for (const SCEV *Reg : ReqRegs) {
        if ((F.ScaledReg && F.ScaledReg == Reg) ||
            is_contained(F.BaseRegs, Reg)) {
          --NumReqRegsToFind;
          if (NumReqRegsToFind == 0)
            break;
        }
      }
      if (NumReqRegsToFind != 0) {
        // If none of the formulae satisfied the required registers, then we could
        // clear ReqRegs and try again. Currently, we simply give up in this case.
        continue;
      }
    }

    // Evaluate the cost of the current formula. If it's already worse than
    // the current best, prune the search at that point.
    NewCost = CurCost;
    NewRegs = CurRegs;
    NewCost.RateFormula(F, NewRegs, VisitedRegs, LU);
    if (NewCost.isLess(SolutionCost)) {
      Workspace.push_back(&F);
      if (Workspace.size() != Uses.size()) {
        SolveRecurse(Solution, SolutionCost, Workspace, NewCost,
                     NewRegs, VisitedRegs);
        if (F.getNumRegs() == 1 && Workspace.size() == 1)
          VisitedRegs.insert(F.ScaledReg ? F.ScaledReg : F.BaseRegs[0]);
      } else {
        LLVM_DEBUG(dbgs() << "New best at "; NewCost.print(dbgs());
                   dbgs() << ".\nRegs:\n";
                   for (const SCEV *S : NewRegs) dbgs()
                      << "- " << *S << "\n";
                   dbgs() << '\n');

        SolutionCost = NewCost;
        Solution = Workspace;
      }
      Workspace.pop_back();
    }
  }
}

/// Choose one formula from each use. Return the results in the given Solution
/// vector.
void LSRInstance::Solve(SmallVectorImpl<const Formula *> &Solution) const {
  SmallVector<const Formula *, 8> Workspace;
  Cost SolutionCost(L, SE, TTI, AMK);
  SolutionCost.Lose();
  Cost CurCost(L, SE, TTI, AMK);
  SmallPtrSet<const SCEV *, 16> CurRegs;
  DenseSet<const SCEV *> VisitedRegs;
  Workspace.reserve(Uses.size());

  // SolveRecurse does all the work.
  SolveRecurse(Solution, SolutionCost, Workspace, CurCost,
               CurRegs, VisitedRegs);
  if (Solution.empty()) {
    LLVM_DEBUG(dbgs() << "\nNo Satisfactory Solution\n");
    return;
  }

  // Ok, we've now made all our decisions.
  LLVM_DEBUG(dbgs() << "\n"
                       "The chosen solution requires ";
             SolutionCost.print(dbgs()); dbgs() << ":\n";
             for (size_t i = 0, e = Uses.size(); i != e; ++i) {
               dbgs() << "  ";
               Uses[i].print(dbgs());
               dbgs() << "\n"
                         "    ";
               Solution[i]->print(dbgs());
               dbgs() << '\n';
             });

  assert(Solution.size() == Uses.size() && "Malformed solution!");

  const bool EnableDropUnprofitableSolution = [&] {
    switch (AllowDropSolutionIfLessProfitable) {
    case cl::BOU_TRUE:
      return true;
    case cl::BOU_FALSE:
      return false;
    case cl::BOU_UNSET:
      return TTI.shouldDropLSRSolutionIfLessProfitable();
    }
    llvm_unreachable("Unhandled cl::boolOrDefault enum");
  }();

  if (BaselineCost.isLess(SolutionCost)) {
    if (!EnableDropUnprofitableSolution)
      LLVM_DEBUG(
          dbgs() << "Baseline is more profitable than chosen solution, "
                    "add option 'lsr-drop-solution' to drop LSR solution.\n");
    else {
      LLVM_DEBUG(dbgs() << "Baseline is more profitable than chosen "
                           "solution, dropping LSR solution.\n";);
      Solution.clear();
    }
  }
}

/// Helper for AdjustInsertPositionForExpand. Climb up the dominator tree far as
/// we can go while still being dominated by the input positions. This helps
/// canonicalize the insert position, which encourages sharing.
BasicBlock::iterator
LSRInstance::HoistInsertPosition(BasicBlock::iterator IP,
                                 const SmallVectorImpl<Instruction *> &Inputs)
                                                                         const {
  Instruction *Tentative = &*IP;
  while (true) {
    bool AllDominate = true;
    Instruction *BetterPos = nullptr;
    // Don't bother attempting to insert before a catchswitch, their basic block
    // cannot have other non-PHI instructions.
    if (isa<CatchSwitchInst>(Tentative))
      return IP;

    for (Instruction *Inst : Inputs) {
      if (Inst == Tentative || !DT.dominates(Inst, Tentative)) {
        AllDominate = false;
        break;
      }
      // Attempt to find an insert position in the middle of the block,
      // instead of at the end, so that it can be used for other expansions.
      if (Tentative->getParent() == Inst->getParent() &&
          (!BetterPos || !DT.dominates(Inst, BetterPos)))
        BetterPos = &*std::next(BasicBlock::iterator(Inst));
    }
    if (!AllDominate)
      break;
    if (BetterPos)
      IP = BetterPos->getIterator();
    else
      IP = Tentative->getIterator();

    const Loop *IPLoop = LI.getLoopFor(IP->getParent());
    unsigned IPLoopDepth = IPLoop ? IPLoop->getLoopDepth() : 0;

    BasicBlock *IDom;
    for (DomTreeNode *Rung = DT.getNode(IP->getParent()); ; ) {
      if (!Rung) return IP;
      Rung = Rung->getIDom();
      if (!Rung) return IP;
      IDom = Rung->getBlock();

      // Don't climb into a loop though.
      const Loop *IDomLoop = LI.getLoopFor(IDom);
      unsigned IDomDepth = IDomLoop ? IDomLoop->getLoopDepth() : 0;
      if (IDomDepth <= IPLoopDepth &&
          (IDomDepth != IPLoopDepth || IDomLoop == IPLoop))
        break;
    }

    Tentative = IDom->getTerminator();
  }

  return IP;
}

/// Determine an input position which will be dominated by the operands and
/// which will dominate the result.
BasicBlock::iterator LSRInstance::AdjustInsertPositionForExpand(
    BasicBlock::iterator LowestIP, const LSRFixup &LF, const LSRUse &LU) const {
  // Collect some instructions which must be dominated by the
  // expanding replacement. These must be dominated by any operands that
  // will be required in the expansion.
  SmallVector<Instruction *, 4> Inputs;
  if (Instruction *I = dyn_cast<Instruction>(LF.OperandValToReplace))
    Inputs.push_back(I);
  if (LU.Kind == LSRUse::ICmpZero)
    if (Instruction *I =
          dyn_cast<Instruction>(cast<ICmpInst>(LF.UserInst)->getOperand(1)))
      Inputs.push_back(I);
  if (LF.PostIncLoops.count(L)) {
    if (LF.isUseFullyOutsideLoop(L))
      Inputs.push_back(L->getLoopLatch()->getTerminator());
    else
      Inputs.push_back(IVIncInsertPos);
  }
  // The expansion must also be dominated by the increment positions of any
  // loops it for which it is using post-inc mode.
  for (const Loop *PIL : LF.PostIncLoops) {
    if (PIL == L) continue;

    // Be dominated by the loop exit.
    SmallVector<BasicBlock *, 4> ExitingBlocks;
    PIL->getExitingBlocks(ExitingBlocks);
    if (!ExitingBlocks.empty()) {
      BasicBlock *BB = ExitingBlocks[0];
      for (unsigned i = 1, e = ExitingBlocks.size(); i != e; ++i)
        BB = DT.findNearestCommonDominator(BB, ExitingBlocks[i]);
      Inputs.push_back(BB->getTerminator());
    }
  }

  assert(!isa<PHINode>(LowestIP) && !LowestIP->isEHPad()
         && !isa<DbgInfoIntrinsic>(LowestIP) &&
         "Insertion point must be a normal instruction");

  // Then, climb up the immediate dominator tree as far as we can go while
  // still being dominated by the input positions.
  BasicBlock::iterator IP = HoistInsertPosition(LowestIP, Inputs);

  // Don't insert instructions before PHI nodes.
  while (isa<PHINode>(IP)) ++IP;

  // Ignore landingpad instructions.
  while (IP->isEHPad()) ++IP;

  // Ignore debug intrinsics.
  while (isa<DbgInfoIntrinsic>(IP)) ++IP;

  // Set IP below instructions recently inserted by SCEVExpander. This keeps the
  // IP consistent across expansions and allows the previously inserted
  // instructions to be reused by subsequent expansion.
  while (Rewriter.isInsertedInstruction(&*IP) && IP != LowestIP)
    ++IP;

  return IP;
}

/// Emit instructions for the leading candidate expression for this LSRUse (this
/// is called "expanding").
Value *LSRInstance::Expand(const LSRUse &LU, const LSRFixup &LF,
                           const Formula &F, BasicBlock::iterator IP,
                           SmallVectorImpl<WeakTrackingVH> &DeadInsts) const {
  if (LU.RigidFormula)
    return LF.OperandValToReplace;

  // Determine an input position which will be dominated by the operands and
  // which will dominate the result.
  IP = AdjustInsertPositionForExpand(IP, LF, LU);
  Rewriter.setInsertPoint(&*IP);

  // Inform the Rewriter if we have a post-increment use, so that it can
  // perform an advantageous expansion.
  Rewriter.setPostInc(LF.PostIncLoops);

  // This is the type that the user actually needs.
  Type *OpTy = LF.OperandValToReplace->getType();
  // This will be the type that we'll initially expand to.
  Type *Ty = F.getType();
  if (!Ty)
    // No type known; just expand directly to the ultimate type.
    Ty = OpTy;
  else if (SE.getEffectiveSCEVType(Ty) == SE.getEffectiveSCEVType(OpTy))
    // Expand directly to the ultimate type if it's the right size.
    Ty = OpTy;
  // This is the type to do integer arithmetic in.
  Type *IntTy = SE.getEffectiveSCEVType(Ty);

  // Build up a list of operands to add together to form the full base.
  SmallVector<const SCEV *, 8> Ops;

  // Expand the BaseRegs portion.
  for (const SCEV *Reg : F.BaseRegs) {
    assert(!Reg->isZero() && "Zero allocated in a base register!");

    // If we're expanding for a post-inc user, make the post-inc adjustment.
    Reg = denormalizeForPostIncUse(Reg, LF.PostIncLoops, SE);
    Ops.push_back(SE.getUnknown(Rewriter.expandCodeFor(Reg, nullptr)));
  }

  // Expand the ScaledReg portion.
  Value *ICmpScaledV = nullptr;
  if (F.Scale != 0) {
    const SCEV *ScaledS = F.ScaledReg;

    // If we're expanding for a post-inc user, make the post-inc adjustment.
    PostIncLoopSet &Loops = const_cast<PostIncLoopSet &>(LF.PostIncLoops);
    ScaledS = denormalizeForPostIncUse(ScaledS, Loops, SE);

    if (LU.Kind == LSRUse::ICmpZero) {
      // Expand ScaleReg as if it was part of the base regs.
      if (F.Scale == 1)
        Ops.push_back(
            SE.getUnknown(Rewriter.expandCodeFor(ScaledS, nullptr)));
      else {
        // An interesting way of "folding" with an icmp is to use a negated
        // scale, which we'll implement by inserting it into the other operand
        // of the icmp.
        assert(F.Scale == -1 &&
               "The only scale supported by ICmpZero uses is -1!");
        ICmpScaledV = Rewriter.expandCodeFor(ScaledS, nullptr);
      }
    } else {
      // Otherwise just expand the scaled register and an explicit scale,
      // which is expected to be matched as part of the address.

      // Flush the operand list to suppress SCEVExpander hoisting address modes.
      // Unless the addressing mode will not be folded.
      if (!Ops.empty() && LU.Kind == LSRUse::Address &&
          isAMCompletelyFolded(TTI, LU, F)) {
        Value *FullV = Rewriter.expandCodeFor(SE.getAddExpr(Ops), nullptr);
        Ops.clear();
        Ops.push_back(SE.getUnknown(FullV));
      }
      ScaledS = SE.getUnknown(Rewriter.expandCodeFor(ScaledS, nullptr));
      if (F.Scale != 1)
        ScaledS =
            SE.getMulExpr(ScaledS, SE.getConstant(ScaledS->getType(), F.Scale));
      Ops.push_back(ScaledS);
    }
  }

  // Expand the GV portion.
  if (F.BaseGV) {
    // Flush the operand list to suppress SCEVExpander hoisting.
    if (!Ops.empty()) {
      Value *FullV = Rewriter.expandCodeFor(SE.getAddExpr(Ops), IntTy);
      Ops.clear();
      Ops.push_back(SE.getUnknown(FullV));
    }
    Ops.push_back(SE.getUnknown(F.BaseGV));
  }

  // Flush the operand list to suppress SCEVExpander hoisting of both folded and
  // unfolded offsets. LSR assumes they both live next to their uses.
  if (!Ops.empty()) {
    Value *FullV = Rewriter.expandCodeFor(SE.getAddExpr(Ops), Ty);
    Ops.clear();
    Ops.push_back(SE.getUnknown(FullV));
  }

  // FIXME: Are we sure we won't get a mismatch here? Is there a way to bail
  // out at this point, or should we generate a SCEV adding together mixed
  // offsets?
  assert(F.BaseOffset.isCompatibleImmediate(LF.Offset) &&
         "Expanding mismatched offsets\n");
  // Expand the immediate portion.
  Immediate Offset = F.BaseOffset.addUnsigned(LF.Offset);
  if (Offset.isNonZero()) {
    if (LU.Kind == LSRUse::ICmpZero) {
      // The other interesting way of "folding" with an ICmpZero is to use a
      // negated immediate.
      if (!ICmpScaledV)
        ICmpScaledV =
            ConstantInt::get(IntTy, -(uint64_t)Offset.getFixedValue());
      else {
        Ops.push_back(SE.getUnknown(ICmpScaledV));
        ICmpScaledV = ConstantInt::get(IntTy, Offset.getFixedValue());
      }
    } else {
      // Just add the immediate values. These again are expected to be matched
      // as part of the address.
      Ops.push_back(Offset.getUnknownSCEV(SE, IntTy));
    }
  }

  // Expand the unfolded offset portion.
  Immediate UnfoldedOffset = F.UnfoldedOffset;
  if (UnfoldedOffset.isNonZero()) {
    // Just add the immediate values.
    Ops.push_back(UnfoldedOffset.getUnknownSCEV(SE, IntTy));
  }

  // Emit instructions summing all the operands.
  const SCEV *FullS = Ops.empty() ?
                      SE.getConstant(IntTy, 0) :
                      SE.getAddExpr(Ops);
  Value *FullV = Rewriter.expandCodeFor(FullS, Ty);

  // We're done expanding now, so reset the rewriter.
  Rewriter.clearPostInc();

  // An ICmpZero Formula represents an ICmp which we're handling as a
  // comparison against zero. Now that we've expanded an expression for that
  // form, update the ICmp's other operand.
  if (LU.Kind == LSRUse::ICmpZero) {
    ICmpInst *CI = cast<ICmpInst>(LF.UserInst);
    if (auto *OperandIsInstr = dyn_cast<Instruction>(CI->getOperand(1)))
      DeadInsts.emplace_back(OperandIsInstr);
    assert(!F.BaseGV && "ICmp does not support folding a global value and "
                           "a scale at the same time!");
    if (F.Scale == -1) {
      if (ICmpScaledV->getType() != OpTy) {
        Instruction *Cast = CastInst::Create(
            CastInst::getCastOpcode(ICmpScaledV, false, OpTy, false),
            ICmpScaledV, OpTy, "tmp", CI->getIterator());
        ICmpScaledV = Cast;
      }
      CI->setOperand(1, ICmpScaledV);
    } else {
      // A scale of 1 means that the scale has been expanded as part of the
      // base regs.
      assert((F.Scale == 0 || F.Scale == 1) &&
             "ICmp does not support folding a global value and "
             "a scale at the same time!");
      Constant *C = ConstantInt::getSigned(SE.getEffectiveSCEVType(OpTy),
                                           -(uint64_t)Offset.getFixedValue());
      if (C->getType() != OpTy) {
        C = ConstantFoldCastOperand(
            CastInst::getCastOpcode(C, false, OpTy, false), C, OpTy,
            CI->getDataLayout());
        assert(C && "Cast of ConstantInt should have folded");
      }

      CI->setOperand(1, C);
    }
  }

  return FullV;
}

/// Helper for Rewrite. PHI nodes are special because the use of their operands
/// effectively happens in their predecessor blocks, so the expression may need
/// to be expanded in multiple places.
void LSRInstance::RewriteForPHI(
    PHINode *PN, const LSRUse &LU, const LSRFixup &LF, const Formula &F,
    SmallVectorImpl<WeakTrackingVH> &DeadInsts) const {
  DenseMap<BasicBlock *, Value *> Inserted;

  // Inserting instructions in the loop and using them as PHI's input could
  // break LCSSA in case if PHI's parent block is not a loop exit (i.e. the
  // corresponding incoming block is not loop exiting). So collect all such
  // instructions to form LCSSA for them later.
  SmallVector<Instruction *, 4> InsertedNonLCSSAInsts;

  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
    if (PN->getIncomingValue(i) == LF.OperandValToReplace) {
      bool needUpdateFixups = false;
      BasicBlock *BB = PN->getIncomingBlock(i);

      // If this is a critical edge, split the edge so that we do not insert
      // the code on all predecessor/successor paths.  We do this unless this
      // is the canonical backedge for this loop, which complicates post-inc
      // users.
      if (e != 1 && BB->getTerminator()->getNumSuccessors() > 1 &&
          !isa<IndirectBrInst>(BB->getTerminator()) &&
          !isa<CatchSwitchInst>(BB->getTerminator())) {
        BasicBlock *Parent = PN->getParent();
        Loop *PNLoop = LI.getLoopFor(Parent);
        if (!PNLoop || Parent != PNLoop->getHeader()) {
          // Split the critical edge.
          BasicBlock *NewBB = nullptr;
          if (!Parent->isLandingPad()) {
            NewBB =
                SplitCriticalEdge(BB, Parent,
                                  CriticalEdgeSplittingOptions(&DT, &LI, MSSAU)
                                      .setMergeIdenticalEdges()
                                      .setKeepOneInputPHIs());
          } else {
            SmallVector<BasicBlock*, 2> NewBBs;
            DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
            SplitLandingPadPredecessors(Parent, BB, "", "", NewBBs, &DTU, &LI);
            NewBB = NewBBs[0];
          }
          // If NewBB==NULL, then SplitCriticalEdge refused to split because all
          // phi predecessors are identical. The simple thing to do is skip
          // splitting in this case rather than complicate the API.
          if (NewBB) {
            // If PN is outside of the loop and BB is in the loop, we want to
            // move the block to be immediately before the PHI block, not
            // immediately after BB.
            if (L->contains(BB) && !L->contains(PN))
              NewBB->moveBefore(PN->getParent());

            // Splitting the edge can reduce the number of PHI entries we have.
            e = PN->getNumIncomingValues();
            BB = NewBB;
            i = PN->getBasicBlockIndex(BB);

            needUpdateFixups = true;
          }
        }
      }

      std::pair<DenseMap<BasicBlock *, Value *>::iterator, bool> Pair =
        Inserted.insert(std::make_pair(BB, static_cast<Value *>(nullptr)));
      if (!Pair.second)
        PN->setIncomingValue(i, Pair.first->second);
      else {
        Value *FullV =
            Expand(LU, LF, F, BB->getTerminator()->getIterator(), DeadInsts);

        // If this is reuse-by-noop-cast, insert the noop cast.
        Type *OpTy = LF.OperandValToReplace->getType();
        if (FullV->getType() != OpTy)
          FullV = CastInst::Create(
              CastInst::getCastOpcode(FullV, false, OpTy, false), FullV,
              LF.OperandValToReplace->getType(), "tmp",
              BB->getTerminator()->getIterator());

        // If the incoming block for this value is not in the loop, it means the
        // current PHI is not in a loop exit, so we must create a LCSSA PHI for
        // the inserted value.
        if (auto *I = dyn_cast<Instruction>(FullV))
          if (L->contains(I) && !L->contains(BB))
            InsertedNonLCSSAInsts.push_back(I);

        PN->setIncomingValue(i, FullV);
        Pair.first->second = FullV;
      }

      // If LSR splits critical edge and phi node has other pending
      // fixup operands, we need to update those pending fixups. Otherwise
      // formulae will not be implemented completely and some instructions
      // will not be eliminated.
      if (needUpdateFixups) {
        for (LSRUse &LU : Uses)
          for (LSRFixup &Fixup : LU.Fixups)
            // If fixup is supposed to rewrite some operand in the phi
            // that was just updated, it may be already moved to
            // another phi node. Such fixup requires update.
            if (Fixup.UserInst == PN) {
              // Check if the operand we try to replace still exists in the
              // original phi.
              bool foundInOriginalPHI = false;
              for (const auto &val : PN->incoming_values())
                if (val == Fixup.OperandValToReplace) {
                  foundInOriginalPHI = true;
                  break;
                }

              // If fixup operand found in original PHI - nothing to do.
              if (foundInOriginalPHI)
                continue;

              // Otherwise it might be moved to another PHI and requires update.
              // If fixup operand not found in any of the incoming blocks that
              // means we have already rewritten it - nothing to do.
              for (const auto &Block : PN->blocks())
                for (BasicBlock::iterator I = Block->begin(); isa<PHINode>(I);
                     ++I) {
                  PHINode *NewPN = cast<PHINode>(I);
                  for (const auto &val : NewPN->incoming_values())
                    if (val == Fixup.OperandValToReplace)
                      Fixup.UserInst = NewPN;
                }
            }
      }
    }

  formLCSSAForInstructions(InsertedNonLCSSAInsts, DT, LI, &SE);
}

/// Emit instructions for the leading candidate expression for this LSRUse (this
/// is called "expanding"), and update the UserInst to reference the newly
/// expanded value.
void LSRInstance::Rewrite(const LSRUse &LU, const LSRFixup &LF,
                          const Formula &F,
                          SmallVectorImpl<WeakTrackingVH> &DeadInsts) const {
  // First, find an insertion point that dominates UserInst. For PHI nodes,
  // find the nearest block which dominates all the relevant uses.
  if (PHINode *PN = dyn_cast<PHINode>(LF.UserInst)) {
    RewriteForPHI(PN, LU, LF, F, DeadInsts);
  } else {
    Value *FullV = Expand(LU, LF, F, LF.UserInst->getIterator(), DeadInsts);

    // If this is reuse-by-noop-cast, insert the noop cast.
    Type *OpTy = LF.OperandValToReplace->getType();
    if (FullV->getType() != OpTy) {
      Instruction *Cast =
          CastInst::Create(CastInst::getCastOpcode(FullV, false, OpTy, false),
                           FullV, OpTy, "tmp", LF.UserInst->getIterator());
      FullV = Cast;
    }

    // Update the user. ICmpZero is handled specially here (for now) because
    // Expand may have updated one of the operands of the icmp already, and
    // its new value may happen to be equal to LF.OperandValToReplace, in
    // which case doing replaceUsesOfWith leads to replacing both operands
    // with the same value. TODO: Reorganize this.
    if (LU.Kind == LSRUse::ICmpZero)
      LF.UserInst->setOperand(0, FullV);
    else
      LF.UserInst->replaceUsesOfWith(LF.OperandValToReplace, FullV);
  }

  if (auto *OperandIsInstr = dyn_cast<Instruction>(LF.OperandValToReplace))
    DeadInsts.emplace_back(OperandIsInstr);
}

// Trying to hoist the IVInc to loop header if all IVInc users are in
// the loop header. It will help backend to generate post index load/store
// when the latch block is different from loop header block.
static bool canHoistIVInc(const TargetTransformInfo &TTI, const LSRFixup &Fixup,
                          const LSRUse &LU, Instruction *IVIncInsertPos,
                          Loop *L) {
  if (LU.Kind != LSRUse::Address)
    return false;

  // For now this code do the conservative optimization, only work for
  // the header block. Later we can hoist the IVInc to the block post
  // dominate all users.
  BasicBlock *LHeader = L->getHeader();
  if (IVIncInsertPos->getParent() == LHeader)
    return false;

  if (!Fixup.OperandValToReplace ||
      any_of(Fixup.OperandValToReplace->users(), [&LHeader](User *U) {
        Instruction *UI = cast<Instruction>(U);
        return UI->getParent() != LHeader;
      }))
    return false;

  Instruction *I = Fixup.UserInst;
  Type *Ty = I->getType();
  return Ty->isIntegerTy() &&
         ((isa<LoadInst>(I) && TTI.isIndexedLoadLegal(TTI.MIM_PostInc, Ty)) ||
          (isa<StoreInst>(I) && TTI.isIndexedStoreLegal(TTI.MIM_PostInc, Ty)));
}

/// Rewrite all the fixup locations with new values, following the chosen
/// solution.
void LSRInstance::ImplementSolution(
    const SmallVectorImpl<const Formula *> &Solution) {
  // Keep track of instructions we may have made dead, so that
  // we can remove them after we are done working.
  SmallVector<WeakTrackingVH, 16> DeadInsts;

  // Mark phi nodes that terminate chains so the expander tries to reuse them.
  for (const IVChain &Chain : IVChainVec) {
    if (PHINode *PN = dyn_cast<PHINode>(Chain.tailUserInst()))
      Rewriter.setChainedPhi(PN);
  }

  // Expand the new value definitions and update the users.
  for (size_t LUIdx = 0, NumUses = Uses.size(); LUIdx != NumUses; ++LUIdx)
    for (const LSRFixup &Fixup : Uses[LUIdx].Fixups) {
      Instruction *InsertPos =
          canHoistIVInc(TTI, Fixup, Uses[LUIdx], IVIncInsertPos, L)
              ? L->getHeader()->getTerminator()
              : IVIncInsertPos;
      Rewriter.setIVIncInsertPos(L, InsertPos);
      Rewrite(Uses[LUIdx], Fixup, *Solution[LUIdx], DeadInsts);
      Changed = true;
    }

  for (const IVChain &Chain : IVChainVec) {
    GenerateIVChain(Chain, DeadInsts);
    Changed = true;
  }

  for (const WeakVH &IV : Rewriter.getInsertedIVs())
    if (IV && dyn_cast<Instruction>(&*IV)->getParent())
      ScalarEvolutionIVs.push_back(IV);

  // Clean up after ourselves. This must be done before deleting any
  // instructions.
  Rewriter.clear();

  Changed |= RecursivelyDeleteTriviallyDeadInstructionsPermissive(DeadInsts,
                                                                  &TLI, MSSAU);

  // In our cost analysis above, we assume that each addrec consumes exactly
  // one register, and arrange to have increments inserted just before the
  // latch to maximimize the chance this is true.  However, if we reused
  // existing IVs, we now need to move the increments to match our
  // expectations.  Otherwise, our cost modeling results in us having a
  // chosen a non-optimal result for the actual schedule.  (And yes, this
  // scheduling decision does impact later codegen.)
  for (PHINode &PN : L->getHeader()->phis()) {
    BinaryOperator *BO = nullptr;
    Value *Start = nullptr, *Step = nullptr;
    if (!matchSimpleRecurrence(&PN, BO, Start, Step))
      continue;

    switch (BO->getOpcode()) {
    case Instruction::Sub:
      if (BO->getOperand(0) != &PN)
        // sub is non-commutative - match handling elsewhere in LSR
        continue;
      break;
    case Instruction::Add:
      break;
    default:
      continue;
    };

    if (!isa<Constant>(Step))
      // If not a constant step, might increase register pressure
      // (We assume constants have been canonicalized to RHS)
      continue;

    if (BO->getParent() == IVIncInsertPos->getParent())
      // Only bother moving across blocks.  Isel can handle block local case.
      continue;

    // Can we legally schedule inc at the desired point?
    if (!llvm::all_of(BO->uses(),
                      [&](Use &U) {return DT.dominates(IVIncInsertPos, U);}))
      continue;
    BO->moveBefore(IVIncInsertPos);
    Changed = true;
  }


}

LSRInstance::LSRInstance(Loop *L, IVUsers &IU, ScalarEvolution &SE,
                         DominatorTree &DT, LoopInfo &LI,
                         const TargetTransformInfo &TTI, AssumptionCache &AC,
                         TargetLibraryInfo &TLI, MemorySSAUpdater *MSSAU)
    : IU(IU), SE(SE), DT(DT), LI(LI), AC(AC), TLI(TLI), TTI(TTI), L(L),
      MSSAU(MSSAU), AMK(PreferredAddresingMode.getNumOccurrences() > 0
                            ? PreferredAddresingMode
                            : TTI.getPreferredAddressingMode(L, &SE)),
      Rewriter(SE, L->getHeader()->getDataLayout(), "lsr", false),
      BaselineCost(L, SE, TTI, AMK) {
  // If LoopSimplify form is not available, stay out of trouble.
  if (!L->isLoopSimplifyForm())
    return;

  // If there's no interesting work to be done, bail early.
  if (IU.empty()) return;

  // If there's too much analysis to be done, bail early. We won't be able to
  // model the problem anyway.
  unsigned NumUsers = 0;
  for (const IVStrideUse &U : IU) {
    if (++NumUsers > MaxIVUsers) {
      (void)U;
      LLVM_DEBUG(dbgs() << "LSR skipping loop, too many IV Users in " << U
                        << "\n");
      return;
    }
    // Bail out if we have a PHI on an EHPad that gets a value from a
    // CatchSwitchInst.  Because the CatchSwitchInst cannot be split, there is
    // no good place to stick any instructions.
    if (auto *PN = dyn_cast<PHINode>(U.getUser())) {
       auto *FirstNonPHI = PN->getParent()->getFirstNonPHI();
       if (isa<FuncletPadInst>(FirstNonPHI) ||
           isa<CatchSwitchInst>(FirstNonPHI))
         for (BasicBlock *PredBB : PN->blocks())
           if (isa<CatchSwitchInst>(PredBB->getFirstNonPHI()))
             return;
    }
  }

  LLVM_DEBUG(dbgs() << "\nLSR on loop ";
             L->getHeader()->printAsOperand(dbgs(), /*PrintType=*/false);
             dbgs() << ":\n");

  // Configure SCEVExpander already now, so the correct mode is used for
  // isSafeToExpand() checks.
#ifndef NDEBUG
  Rewriter.setDebugType(DEBUG_TYPE);
#endif
  Rewriter.disableCanonicalMode();
  Rewriter.enableLSRMode();

  // First, perform some low-level loop optimizations.
  OptimizeShadowIV();
  OptimizeLoopTermCond();

  // If loop preparation eliminates all interesting IV users, bail.
  if (IU.empty()) return;

  // Skip nested loops until we can model them better with formulae.
  if (!L->isInnermost()) {
    LLVM_DEBUG(dbgs() << "LSR skipping outer loop " << *L << "\n");
    return;
  }

  // Start collecting data and preparing for the solver.
  // If number of registers is not the major cost, we cannot benefit from the
  // current profitable chain optimization which is based on number of
  // registers.
  // FIXME: add profitable chain optimization for other kinds major cost, for
  // example number of instructions.
  if (TTI.isNumRegsMajorCostOfLSR() || StressIVChain)
    CollectChains();
  CollectInterestingTypesAndFactors();
  CollectFixupsAndInitialFormulae();
  CollectLoopInvariantFixupsAndFormulae();

  if (Uses.empty())
    return;

  LLVM_DEBUG(dbgs() << "LSR found " << Uses.size() << " uses:\n";
             print_uses(dbgs()));
  LLVM_DEBUG(dbgs() << "The baseline solution requires ";
             BaselineCost.print(dbgs()); dbgs() << "\n");

  // Now use the reuse data to generate a bunch of interesting ways
  // to formulate the values needed for the uses.
  GenerateAllReuseFormulae();

  FilterOutUndesirableDedicatedRegisters();
  NarrowSearchSpaceUsingHeuristics();

  SmallVector<const Formula *, 8> Solution;
  Solve(Solution);

  // Release memory that is no longer needed.
  Factors.clear();
  Types.clear();
  RegUses.clear();

  if (Solution.empty())
    return;

#ifndef NDEBUG
  // Formulae should be legal.
  for (const LSRUse &LU : Uses) {
    for (const Formula &F : LU.Formulae)
      assert(isLegalUse(TTI, LU.MinOffset, LU.MaxOffset, LU.Kind, LU.AccessTy,
                        F) && "Illegal formula generated!");
  };
#endif

  // Now that we've decided what we want, make it so.
  ImplementSolution(Solution);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void LSRInstance::print_factors_and_types(raw_ostream &OS) const {
  if (Factors.empty() && Types.empty()) return;

  OS << "LSR has identified the following interesting factors and types: ";
  bool First = true;

  for (int64_t Factor : Factors) {
    if (!First) OS << ", ";
    First = false;
    OS << '*' << Factor;
  }

  for (Type *Ty : Types) {
    if (!First) OS << ", ";
    First = false;
    OS << '(' << *Ty << ')';
  }
  OS << '\n';
}

void LSRInstance::print_fixups(raw_ostream &OS) const {
  OS << "LSR is examining the following fixup sites:\n";
  for (const LSRUse &LU : Uses)
    for (const LSRFixup &LF : LU.Fixups) {
      dbgs() << "  ";
      LF.print(OS);
      OS << '\n';
    }
}

void LSRInstance::print_uses(raw_ostream &OS) const {
  OS << "LSR is examining the following uses:\n";
  for (const LSRUse &LU : Uses) {
    dbgs() << "  ";
    LU.print(OS);
    OS << '\n';
    for (const Formula &F : LU.Formulae) {
      OS << "    ";
      F.print(OS);
      OS << '\n';
    }
  }
}

void LSRInstance::print(raw_ostream &OS) const {
  print_factors_and_types(OS);
  print_fixups(OS);
  print_uses(OS);
}

LLVM_DUMP_METHOD void LSRInstance::dump() const {
  print(errs()); errs() << '\n';
}
#endif

namespace {

class LoopStrengthReduce : public LoopPass {
public:
  static char ID; // Pass ID, replacement for typeid

  LoopStrengthReduce();

private:
  bool runOnLoop(Loop *L, LPPassManager &LPM) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // end anonymous namespace

LoopStrengthReduce::LoopStrengthReduce() : LoopPass(ID) {
  initializeLoopStrengthReducePass(*PassRegistry::getPassRegistry());
}

void LoopStrengthReduce::getAnalysisUsage(AnalysisUsage &AU) const {
  // We split critical edges, so we change the CFG.  However, we do update
  // many analyses if they are around.
  AU.addPreservedID(LoopSimplifyID);

  AU.addRequired<LoopInfoWrapperPass>();
  AU.addPreserved<LoopInfoWrapperPass>();
  AU.addRequiredID(LoopSimplifyID);
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addPreserved<DominatorTreeWrapperPass>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.addPreserved<ScalarEvolutionWrapperPass>();
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  // Requiring LoopSimplify a second time here prevents IVUsers from running
  // twice, since LoopSimplify was invalidated by running ScalarEvolution.
  AU.addRequiredID(LoopSimplifyID);
  AU.addRequired<IVUsersWrapperPass>();
  AU.addPreserved<IVUsersWrapperPass>();
  AU.addRequired<TargetTransformInfoWrapperPass>();
  AU.addPreserved<MemorySSAWrapperPass>();
}

namespace {

/// Enables more convenient iteration over a DWARF expression vector.
static iterator_range<llvm::DIExpression::expr_op_iterator>
ToDwarfOpIter(SmallVectorImpl<uint64_t> &Expr) {
  llvm::DIExpression::expr_op_iterator Begin =
      llvm::DIExpression::expr_op_iterator(Expr.begin());
  llvm::DIExpression::expr_op_iterator End =
      llvm::DIExpression::expr_op_iterator(Expr.end());
  return {Begin, End};
}

struct SCEVDbgValueBuilder {
  SCEVDbgValueBuilder() = default;
  SCEVDbgValueBuilder(const SCEVDbgValueBuilder &Base) { clone(Base); }

  void clone(const SCEVDbgValueBuilder &Base) {
    LocationOps = Base.LocationOps;
    Expr = Base.Expr;
  }

  void clear() {
    LocationOps.clear();
    Expr.clear();
  }

  /// The DIExpression as we translate the SCEV.
  SmallVector<uint64_t, 6> Expr;
  /// The location ops of the DIExpression.
  SmallVector<Value *, 2> LocationOps;

  void pushOperator(uint64_t Op) { Expr.push_back(Op); }
  void pushUInt(uint64_t Operand) { Expr.push_back(Operand); }

  /// Add a DW_OP_LLVM_arg to the expression, followed by the index of the value
  /// in the set of values referenced by the expression.
  void pushLocation(llvm::Value *V) {
    Expr.push_back(llvm::dwarf::DW_OP_LLVM_arg);
    auto *It = llvm::find(LocationOps, V);
    unsigned ArgIndex = 0;
    if (It != LocationOps.end()) {
      ArgIndex = std::distance(LocationOps.begin(), It);
    } else {
      ArgIndex = LocationOps.size();
      LocationOps.push_back(V);
    }
    Expr.push_back(ArgIndex);
  }

  void pushValue(const SCEVUnknown *U) {
    llvm::Value *V = cast<SCEVUnknown>(U)->getValue();
    pushLocation(V);
  }

  bool pushConst(const SCEVConstant *C) {
    if (C->getAPInt().getSignificantBits() > 64)
      return false;
    Expr.push_back(llvm::dwarf::DW_OP_consts);
    Expr.push_back(C->getAPInt().getSExtValue());
    return true;
  }

  // Iterating the expression as DWARF ops is convenient when updating
  // DWARF_OP_LLVM_args.
  iterator_range<llvm::DIExpression::expr_op_iterator> expr_ops() {
    return ToDwarfOpIter(Expr);
  }

  /// Several SCEV types are sequences of the same arithmetic operator applied
  /// to constants and values that may be extended or truncated.
  bool pushArithmeticExpr(const llvm::SCEVCommutativeExpr *CommExpr,
                          uint64_t DwarfOp) {
    assert((isa<llvm::SCEVAddExpr>(CommExpr) || isa<SCEVMulExpr>(CommExpr)) &&
           "Expected arithmetic SCEV type");
    bool Success = true;
    unsigned EmitOperator = 0;
    for (const auto &Op : CommExpr->operands()) {
      Success &= pushSCEV(Op);

      if (EmitOperator >= 1)
        pushOperator(DwarfOp);
      ++EmitOperator;
    }
    return Success;
  }

  // TODO: Identify and omit noop casts.
  bool pushCast(const llvm::SCEVCastExpr *C, bool IsSigned) {
    const llvm::SCEV *Inner = C->getOperand(0);
    const llvm::Type *Type = C->getType();
    uint64_t ToWidth = Type->getIntegerBitWidth();
    bool Success = pushSCEV(Inner);
    uint64_t CastOps[] = {dwarf::DW_OP_LLVM_convert, ToWidth,
                          IsSigned ? llvm::dwarf::DW_ATE_signed
                                   : llvm::dwarf::DW_ATE_unsigned};
    for (const auto &Op : CastOps)
      pushOperator(Op);
    return Success;
  }

  // TODO: MinMax - although these haven't been encountered in the test suite.
  bool pushSCEV(const llvm::SCEV *S) {
    bool Success = true;
    if (const SCEVConstant *StartInt = dyn_cast<SCEVConstant>(S)) {
      Success &= pushConst(StartInt);

    } else if (const SCEVUnknown *U = dyn_cast<SCEVUnknown>(S)) {
      if (!U->getValue())
        return false;
      pushLocation(U->getValue());

    } else if (const SCEVMulExpr *MulRec = dyn_cast<SCEVMulExpr>(S)) {
      Success &= pushArithmeticExpr(MulRec, llvm::dwarf::DW_OP_mul);

    } else if (const SCEVUDivExpr *UDiv = dyn_cast<SCEVUDivExpr>(S)) {
      Success &= pushSCEV(UDiv->getLHS());
      Success &= pushSCEV(UDiv->getRHS());
      pushOperator(llvm::dwarf::DW_OP_div);

    } else if (const SCEVCastExpr *Cast = dyn_cast<SCEVCastExpr>(S)) {
      // Assert if a new and unknown SCEVCastEXpr type is encountered.
      assert((isa<SCEVZeroExtendExpr>(Cast) || isa<SCEVTruncateExpr>(Cast) ||
              isa<SCEVPtrToIntExpr>(Cast) || isa<SCEVSignExtendExpr>(Cast)) &&
             "Unexpected cast type in SCEV.");
      Success &= pushCast(Cast, (isa<SCEVSignExtendExpr>(Cast)));

    } else if (const SCEVAddExpr *AddExpr = dyn_cast<SCEVAddExpr>(S)) {
      Success &= pushArithmeticExpr(AddExpr, llvm::dwarf::DW_OP_plus);

    } else if (isa<SCEVAddRecExpr>(S)) {
      // Nested SCEVAddRecExpr are generated by nested loops and are currently
      // unsupported.
      return false;

    } else {
      return false;
    }
    return Success;
  }

  /// Return true if the combination of arithmetic operator and underlying
  /// SCEV constant value is an identity function.
  bool isIdentityFunction(uint64_t Op, const SCEV *S) {
    if (const SCEVConstant *C = dyn_cast<SCEVConstant>(S)) {
      if (C->getAPInt().getSignificantBits() > 64)
        return false;
      int64_t I = C->getAPInt().getSExtValue();
      switch (Op) {
      case llvm::dwarf::DW_OP_plus:
      case llvm::dwarf::DW_OP_minus:
        return I == 0;
      case llvm::dwarf::DW_OP_mul:
      case llvm::dwarf::DW_OP_div:
        return I == 1;
      }
    }
    return false;
  }

  /// Convert a SCEV of a value to a DIExpression that is pushed onto the
  /// builder's expression stack. The stack should already contain an
  /// expression for the iteration count, so that it can be multiplied by
  /// the stride and added to the start.
  /// Components of the expression are omitted if they are an identity function.
  /// Chain (non-affine) SCEVs are not supported.
  bool SCEVToValueExpr(const llvm::SCEVAddRecExpr &SAR, ScalarEvolution &SE) {
    assert(SAR.isAffine() && "Expected affine SCEV");
    // TODO: Is this check needed?
    if (isa<SCEVAddRecExpr>(SAR.getStart()))
      return false;

    const SCEV *Start = SAR.getStart();
    const SCEV *Stride = SAR.getStepRecurrence(SE);

    // Skip pushing arithmetic noops.
    if (!isIdentityFunction(llvm::dwarf::DW_OP_mul, Stride)) {
      if (!pushSCEV(Stride))
        return false;
      pushOperator(llvm::dwarf::DW_OP_mul);
    }
    if (!isIdentityFunction(llvm::dwarf::DW_OP_plus, Start)) {
      if (!pushSCEV(Start))
        return false;
      pushOperator(llvm::dwarf::DW_OP_plus);
    }
    return true;
  }

  /// Create an expression that is an offset from a value (usually the IV).
  void createOffsetExpr(int64_t Offset, Value *OffsetValue) {
    pushLocation(OffsetValue);
    DIExpression::appendOffset(Expr, Offset);
    LLVM_DEBUG(
        dbgs() << "scev-salvage: Generated IV offset expression. Offset: "
               << std::to_string(Offset) << "\n");
  }

  /// Combine a translation of the SCEV and the IV to create an expression that
  /// recovers a location's value.
  /// returns true if an expression was created.
  bool createIterCountExpr(const SCEV *S,
                           const SCEVDbgValueBuilder &IterationCount,
                           ScalarEvolution &SE) {
    // SCEVs for SSA values are most frquently of the form
    // {start,+,stride}, but sometimes they are ({start,+,stride} + %a + ..).
    // This is because %a is a PHI node that is not the IV. However, these
    // SCEVs have not been observed to result in debuginfo-lossy optimisations,
    // so its not expected this point will be reached.
    if (!isa<SCEVAddRecExpr>(S))
      return false;

    LLVM_DEBUG(dbgs() << "scev-salvage: Location to salvage SCEV: " << *S
                      << '\n');

    const auto *Rec = cast<SCEVAddRecExpr>(S);
    if (!Rec->isAffine())
      return false;

    if (S->getExpressionSize() > MaxSCEVSalvageExpressionSize)
      return false;

    // Initialise a new builder with the iteration count expression. In
    // combination with the value's SCEV this enables recovery.
    clone(IterationCount);
    if (!SCEVToValueExpr(*Rec, SE))
      return false;

    return true;
  }

  /// Convert a SCEV of a value to a DIExpression that is pushed onto the
  /// builder's expression stack. The stack should already contain an
  /// expression for the iteration count, so that it can be multiplied by
  /// the stride and added to the start.
  /// Components of the expression are omitted if they are an identity function.
  bool SCEVToIterCountExpr(const llvm::SCEVAddRecExpr &SAR,
                           ScalarEvolution &SE) {
    assert(SAR.isAffine() && "Expected affine SCEV");
    if (isa<SCEVAddRecExpr>(SAR.getStart())) {
      LLVM_DEBUG(dbgs() << "scev-salvage: IV SCEV. Unsupported nested AddRec: "
                        << SAR << '\n');
      return false;
    }
    const SCEV *Start = SAR.getStart();
    const SCEV *Stride = SAR.getStepRecurrence(SE);

    // Skip pushing arithmetic noops.
    if (!isIdentityFunction(llvm::dwarf::DW_OP_minus, Start)) {
      if (!pushSCEV(Start))
        return false;
      pushOperator(llvm::dwarf::DW_OP_minus);
    }
    if (!isIdentityFunction(llvm::dwarf::DW_OP_div, Stride)) {
      if (!pushSCEV(Stride))
        return false;
      pushOperator(llvm::dwarf::DW_OP_div);
    }
    return true;
  }

  // Append the current expression and locations to a location list and an
  // expression list. Modify the DW_OP_LLVM_arg indexes to account for
  // the locations already present in the destination list.
  void appendToVectors(SmallVectorImpl<uint64_t> &DestExpr,
                       SmallVectorImpl<Value *> &DestLocations) {
    assert(!DestLocations.empty() &&
           "Expected the locations vector to contain the IV");
    // The DWARF_OP_LLVM_arg arguments of the expression being appended must be
    // modified to account for the locations already in the destination vector.
    // All builders contain the IV as the first location op.
    assert(!LocationOps.empty() &&
           "Expected the location ops to contain the IV.");
    // DestIndexMap[n] contains the index in DestLocations for the nth
    // location in this SCEVDbgValueBuilder.
    SmallVector<uint64_t, 2> DestIndexMap;
    for (const auto &Op : LocationOps) {
      auto It = find(DestLocations, Op);
      if (It != DestLocations.end()) {
        // Location already exists in DestLocations, reuse existing ArgIndex.
        DestIndexMap.push_back(std::distance(DestLocations.begin(), It));
        continue;
      }
      // Location is not in DestLocations, add it.
      DestIndexMap.push_back(DestLocations.size());
      DestLocations.push_back(Op);
    }

    for (const auto &Op : expr_ops()) {
      if (Op.getOp() != dwarf::DW_OP_LLVM_arg) {
        Op.appendToVector(DestExpr);
        continue;
      } 

      DestExpr.push_back(dwarf::DW_OP_LLVM_arg);
      // `DW_OP_LLVM_arg n` represents the nth LocationOp in this SCEV,
      // DestIndexMap[n] contains its new index in DestLocations.
      uint64_t NewIndex = DestIndexMap[Op.getArg(0)];
      DestExpr.push_back(NewIndex);
    }
  }
};

/// Holds all the required data to salvage a dbg.value using the pre-LSR SCEVs
/// and DIExpression.
struct DVIRecoveryRec {
  DVIRecoveryRec(DbgValueInst *DbgValue)
      : DbgRef(DbgValue), Expr(DbgValue->getExpression()),
        HadLocationArgList(false) {}
  DVIRecoveryRec(DbgVariableRecord *DVR)
      : DbgRef(DVR), Expr(DVR->getExpression()), HadLocationArgList(false) {}

  PointerUnion<DbgValueInst *, DbgVariableRecord *> DbgRef;
  DIExpression *Expr;
  bool HadLocationArgList;
  SmallVector<WeakVH, 2> LocationOps;
  SmallVector<const llvm::SCEV *, 2> SCEVs;
  SmallVector<std::unique_ptr<SCEVDbgValueBuilder>, 2> RecoveryExprs;

  void clear() {
    for (auto &RE : RecoveryExprs)
      RE.reset();
    RecoveryExprs.clear();
  }

  ~DVIRecoveryRec() { clear(); }
};
} // namespace

/// Returns the total number of DW_OP_llvm_arg operands in the expression.
/// This helps in determining if a DIArglist is necessary or can be omitted from
/// the dbg.value.
static unsigned numLLVMArgOps(SmallVectorImpl<uint64_t> &Expr) {
  auto expr_ops = ToDwarfOpIter(Expr);
  unsigned Count = 0;
  for (auto Op : expr_ops)
    if (Op.getOp() == dwarf::DW_OP_LLVM_arg)
      Count++;
  return Count;
}

/// Overwrites DVI with the location and Ops as the DIExpression. This will
/// create an invalid expression if Ops has any dwarf::DW_OP_llvm_arg operands,
/// because a DIArglist is not created for the first argument of the dbg.value.
template <typename T>
static void updateDVIWithLocation(T &DbgVal, Value *Location,
                                  SmallVectorImpl<uint64_t> &Ops) {
  assert(numLLVMArgOps(Ops) == 0 && "Expected expression that does not "
                                    "contain any DW_OP_llvm_arg operands.");
  DbgVal.setRawLocation(ValueAsMetadata::get(Location));
  DbgVal.setExpression(DIExpression::get(DbgVal.getContext(), Ops));
  DbgVal.setExpression(DIExpression::get(DbgVal.getContext(), Ops));
}

/// Overwrite DVI with locations placed into a DIArglist.
template <typename T>
static void updateDVIWithLocations(T &DbgVal,
                                   SmallVectorImpl<Value *> &Locations,
                                   SmallVectorImpl<uint64_t> &Ops) {
  assert(numLLVMArgOps(Ops) != 0 &&
         "Expected expression that references DIArglist locations using "
         "DW_OP_llvm_arg operands.");
  SmallVector<ValueAsMetadata *, 3> MetadataLocs;
  for (Value *V : Locations)
    MetadataLocs.push_back(ValueAsMetadata::get(V));
  auto ValArrayRef = llvm::ArrayRef<llvm::ValueAsMetadata *>(MetadataLocs);
  DbgVal.setRawLocation(llvm::DIArgList::get(DbgVal.getContext(), ValArrayRef));
  DbgVal.setExpression(DIExpression::get(DbgVal.getContext(), Ops));
}

/// Write the new expression and new location ops for the dbg.value. If possible
/// reduce the szie of the dbg.value intrinsic by omitting DIArglist. This
/// can be omitted if:
/// 1. There is only a single location, refenced by a single DW_OP_llvm_arg.
/// 2. The DW_OP_LLVM_arg is the first operand in the expression.
static void UpdateDbgValueInst(DVIRecoveryRec &DVIRec,
                               SmallVectorImpl<Value *> &NewLocationOps,
                               SmallVectorImpl<uint64_t> &NewExpr) {
  auto UpdateDbgValueInstImpl = [&](auto *DbgVal) {
    unsigned NumLLVMArgs = numLLVMArgOps(NewExpr);
    if (NumLLVMArgs == 0) {
      // Location assumed to be on the stack.
      updateDVIWithLocation(*DbgVal, NewLocationOps[0], NewExpr);
    } else if (NumLLVMArgs == 1 && NewExpr[0] == dwarf::DW_OP_LLVM_arg) {
      // There is only a single DW_OP_llvm_arg at the start of the expression,
      // so it can be omitted along with DIArglist.
      assert(NewExpr[1] == 0 &&
             "Lone LLVM_arg in a DIExpression should refer to location-op 0.");
      llvm::SmallVector<uint64_t, 6> ShortenedOps(llvm::drop_begin(NewExpr, 2));
      updateDVIWithLocation(*DbgVal, NewLocationOps[0], ShortenedOps);
    } else {
      // Multiple DW_OP_llvm_arg, so DIArgList is strictly necessary.
      updateDVIWithLocations(*DbgVal, NewLocationOps, NewExpr);
    }

    // If the DIExpression was previously empty then add the stack terminator.
    // Non-empty expressions have only had elements inserted into them and so
    // the terminator should already be present e.g. stack_value or fragment.
    DIExpression *SalvageExpr = DbgVal->getExpression();
    if (!DVIRec.Expr->isComplex() && SalvageExpr->isComplex()) {
      SalvageExpr =
          DIExpression::append(SalvageExpr, {dwarf::DW_OP_stack_value});
      DbgVal->setExpression(SalvageExpr);
    }
  };
  if (isa<DbgValueInst *>(DVIRec.DbgRef))
    UpdateDbgValueInstImpl(cast<DbgValueInst *>(DVIRec.DbgRef));
  else
    UpdateDbgValueInstImpl(cast<DbgVariableRecord *>(DVIRec.DbgRef));
}

/// Cached location ops may be erased during LSR, in which case a poison is
/// required when restoring from the cache. The type of that location is no
/// longer available, so just use int8. The poison will be replaced by one or
/// more locations later when a SCEVDbgValueBuilder selects alternative
/// locations to use for the salvage.
static Value *getValueOrPoison(WeakVH &VH, LLVMContext &C) {
  return (VH) ? VH : PoisonValue::get(llvm::Type::getInt8Ty(C));
}

/// Restore the DVI's pre-LSR arguments. Substitute undef for any erased values.
static void restorePreTransformState(DVIRecoveryRec &DVIRec) {
  auto RestorePreTransformStateImpl = [&](auto *DbgVal) {
    LLVM_DEBUG(dbgs() << "scev-salvage: restore dbg.value to pre-LSR state\n"
                      << "scev-salvage: post-LSR: " << *DbgVal << '\n');
    assert(DVIRec.Expr && "Expected an expression");
    DbgVal->setExpression(DVIRec.Expr);

    // Even a single location-op may be inside a DIArgList and referenced with
    // DW_OP_LLVM_arg, which is valid only with a DIArgList.
    if (!DVIRec.HadLocationArgList) {
      assert(DVIRec.LocationOps.size() == 1 &&
             "Unexpected number of location ops.");
      // LSR's unsuccessful salvage attempt may have added DIArgList, which in
      // this case was not present before, so force the location back to a
      // single uncontained Value.
      Value *CachedValue =
          getValueOrPoison(DVIRec.LocationOps[0], DbgVal->getContext());
      DbgVal->setRawLocation(ValueAsMetadata::get(CachedValue));
    } else {
      SmallVector<ValueAsMetadata *, 3> MetadataLocs;
      for (WeakVH VH : DVIRec.LocationOps) {
        Value *CachedValue = getValueOrPoison(VH, DbgVal->getContext());
        MetadataLocs.push_back(ValueAsMetadata::get(CachedValue));
      }
      auto ValArrayRef = llvm::ArrayRef<llvm::ValueAsMetadata *>(MetadataLocs);
      DbgVal->setRawLocation(
          llvm::DIArgList::get(DbgVal->getContext(), ValArrayRef));
    }
    LLVM_DEBUG(dbgs() << "scev-salvage: pre-LSR: " << *DbgVal << '\n');
  };
  if (isa<DbgValueInst *>(DVIRec.DbgRef))
    RestorePreTransformStateImpl(cast<DbgValueInst *>(DVIRec.DbgRef));
  else
    RestorePreTransformStateImpl(cast<DbgVariableRecord *>(DVIRec.DbgRef));
}

static bool SalvageDVI(llvm::Loop *L, ScalarEvolution &SE,
                       llvm::PHINode *LSRInductionVar, DVIRecoveryRec &DVIRec,
                       const SCEV *SCEVInductionVar,
                       SCEVDbgValueBuilder IterCountExpr) {

  if (isa<DbgValueInst *>(DVIRec.DbgRef)
          ? !cast<DbgValueInst *>(DVIRec.DbgRef)->isKillLocation()
          : !cast<DbgVariableRecord *>(DVIRec.DbgRef)->isKillLocation())
    return false;

  // LSR may have caused several changes to the dbg.value in the failed salvage
  // attempt. So restore the DIExpression, the location ops and also the
  // location ops format, which is always DIArglist for multiple ops, but only
  // sometimes for a single op.
  restorePreTransformState(DVIRec);

  // LocationOpIndexMap[i] will store the post-LSR location index of
  // the non-optimised out location at pre-LSR index i.
  SmallVector<int64_t, 2> LocationOpIndexMap;
  LocationOpIndexMap.assign(DVIRec.LocationOps.size(), -1);
  SmallVector<Value *, 2> NewLocationOps;
  NewLocationOps.push_back(LSRInductionVar);

  for (unsigned i = 0; i < DVIRec.LocationOps.size(); i++) {
    WeakVH VH = DVIRec.LocationOps[i];
    // Place the locations not optimised out in the list first, avoiding
    // inserts later. The map is used to update the DIExpression's
    // DW_OP_LLVM_arg arguments as the expression is updated.
    if (VH && !isa<UndefValue>(VH)) {
      NewLocationOps.push_back(VH);
      LocationOpIndexMap[i] = NewLocationOps.size() - 1;
      LLVM_DEBUG(dbgs() << "scev-salvage: Location index " << i
                        << " now at index " << LocationOpIndexMap[i] << "\n");
      continue;
    }

    // It's possible that a value referred to in the SCEV may have been
    // optimised out by LSR.
    if (SE.containsErasedValue(DVIRec.SCEVs[i]) ||
        SE.containsUndefs(DVIRec.SCEVs[i])) {
      LLVM_DEBUG(dbgs() << "scev-salvage: SCEV for location at index: " << i
                        << " refers to a location that is now undef or erased. "
                           "Salvage abandoned.\n");
      return false;
    }

    LLVM_DEBUG(dbgs() << "scev-salvage: salvaging location at index " << i
                      << " with SCEV: " << *DVIRec.SCEVs[i] << "\n");

    DVIRec.RecoveryExprs[i] = std::make_unique<SCEVDbgValueBuilder>();
    SCEVDbgValueBuilder *SalvageExpr = DVIRec.RecoveryExprs[i].get();

    // Create an offset-based salvage expression if possible, as it requires
    // less DWARF ops than an iteration count-based expression.
    if (std::optional<APInt> Offset =
            SE.computeConstantDifference(DVIRec.SCEVs[i], SCEVInductionVar)) {
      if (Offset->getSignificantBits() <= 64)
        SalvageExpr->createOffsetExpr(Offset->getSExtValue(), LSRInductionVar);
    } else if (!SalvageExpr->createIterCountExpr(DVIRec.SCEVs[i], IterCountExpr,
                                                 SE))
      return false;
  }

  // Merge the DbgValueBuilder generated expressions and the original
  // DIExpression, place the result into an new vector.
  SmallVector<uint64_t, 3> NewExpr;
  if (DVIRec.Expr->getNumElements() == 0) {
    assert(DVIRec.RecoveryExprs.size() == 1 &&
           "Expected only a single recovery expression for an empty "
           "DIExpression.");
    assert(DVIRec.RecoveryExprs[0] &&
           "Expected a SCEVDbgSalvageBuilder for location 0");
    SCEVDbgValueBuilder *B = DVIRec.RecoveryExprs[0].get();
    B->appendToVectors(NewExpr, NewLocationOps);
  }
  for (const auto &Op : DVIRec.Expr->expr_ops()) {
    // Most Ops needn't be updated.
    if (Op.getOp() != dwarf::DW_OP_LLVM_arg) {
      Op.appendToVector(NewExpr);
      continue;
    }

    uint64_t LocationArgIndex = Op.getArg(0);
    SCEVDbgValueBuilder *DbgBuilder =
        DVIRec.RecoveryExprs[LocationArgIndex].get();
    // The location doesn't have s SCEVDbgValueBuilder, so LSR did not
    // optimise it away. So just translate the argument to the updated
    // location index.
    if (!DbgBuilder) {
      NewExpr.push_back(dwarf::DW_OP_LLVM_arg);
      assert(LocationOpIndexMap[Op.getArg(0)] != -1 &&
             "Expected a positive index for the location-op position.");
      NewExpr.push_back(LocationOpIndexMap[Op.getArg(0)]);
      continue;
    }
    // The location has a recovery expression.
    DbgBuilder->appendToVectors(NewExpr, NewLocationOps);
  }

  UpdateDbgValueInst(DVIRec, NewLocationOps, NewExpr);
  if (isa<DbgValueInst *>(DVIRec.DbgRef))
    LLVM_DEBUG(dbgs() << "scev-salvage: Updated DVI: "
                      << *cast<DbgValueInst *>(DVIRec.DbgRef) << "\n");
  else
    LLVM_DEBUG(dbgs() << "scev-salvage: Updated DVI: "
                      << *cast<DbgVariableRecord *>(DVIRec.DbgRef) << "\n");
  return true;
}

/// Obtain an expression for the iteration count, then attempt to salvage the
/// dbg.value intrinsics.
static void DbgRewriteSalvageableDVIs(
    llvm::Loop *L, ScalarEvolution &SE, llvm::PHINode *LSRInductionVar,
    SmallVector<std::unique_ptr<DVIRecoveryRec>, 2> &DVIToUpdate) {
  if (DVIToUpdate.empty())
    return;

  const llvm::SCEV *SCEVInductionVar = SE.getSCEV(LSRInductionVar);
  assert(SCEVInductionVar &&
         "Anticipated a SCEV for the post-LSR induction variable");

  if (const SCEVAddRecExpr *IVAddRec =
          dyn_cast<SCEVAddRecExpr>(SCEVInductionVar)) {
    if (!IVAddRec->isAffine())
      return;

    // Prevent translation using excessive resources.
    if (IVAddRec->getExpressionSize() > MaxSCEVSalvageExpressionSize)
      return;

    // The iteration count is required to recover location values.
    SCEVDbgValueBuilder IterCountExpr;
    IterCountExpr.pushLocation(LSRInductionVar);
    if (!IterCountExpr.SCEVToIterCountExpr(*IVAddRec, SE))
      return;

    LLVM_DEBUG(dbgs() << "scev-salvage: IV SCEV: " << *SCEVInductionVar
                      << '\n');

    for (auto &DVIRec : DVIToUpdate) {
      SalvageDVI(L, SE, LSRInductionVar, *DVIRec, SCEVInductionVar,
                 IterCountExpr);
    }
  }
}

/// Identify and cache salvageable DVI locations and expressions along with the
/// corresponding SCEV(s). Also ensure that the DVI is not deleted between
/// cacheing and salvaging.
static void DbgGatherSalvagableDVI(
    Loop *L, ScalarEvolution &SE,
    SmallVector<std::unique_ptr<DVIRecoveryRec>, 2> &SalvageableDVISCEVs,
    SmallSet<AssertingVH<DbgValueInst>, 2> &DVIHandles) {
  for (const auto &B : L->getBlocks()) {
    for (auto &I : *B) {
      auto ProcessDbgValue = [&](auto *DbgVal) -> bool {
        // Ensure that if any location op is undef that the dbg.vlue is not
        // cached.
        if (DbgVal->isKillLocation())
          return false;

        // Check that the location op SCEVs are suitable for translation to
        // DIExpression.
        const auto &HasTranslatableLocationOps =
            [&](const auto *DbgValToTranslate) -> bool {
          for (const auto LocOp : DbgValToTranslate->location_ops()) {
            if (!LocOp)
              return false;

            if (!SE.isSCEVable(LocOp->getType()))
              return false;

            const SCEV *S = SE.getSCEV(LocOp);
            if (SE.containsUndefs(S))
              return false;
          }
          return true;
        };

        if (!HasTranslatableLocationOps(DbgVal))
          return false;

        std::unique_ptr<DVIRecoveryRec> NewRec =
            std::make_unique<DVIRecoveryRec>(DbgVal);
        // Each location Op may need a SCEVDbgValueBuilder in order to recover
        // it. Pre-allocating a vector will enable quick lookups of the builder
        // later during the salvage.
        NewRec->RecoveryExprs.resize(DbgVal->getNumVariableLocationOps());
        for (const auto LocOp : DbgVal->location_ops()) {
          NewRec->SCEVs.push_back(SE.getSCEV(LocOp));
          NewRec->LocationOps.push_back(LocOp);
          NewRec->HadLocationArgList = DbgVal->hasArgList();
        }
        SalvageableDVISCEVs.push_back(std::move(NewRec));
        return true;
      };
      for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange())) {
        if (DVR.isDbgValue() || DVR.isDbgAssign())
          ProcessDbgValue(&DVR);
      }
      auto DVI = dyn_cast<DbgValueInst>(&I);
      if (!DVI)
        continue;
      if (ProcessDbgValue(DVI))
        DVIHandles.insert(DVI);
    }
  }
}

/// Ideally pick the PHI IV inserted by ScalarEvolutionExpander. As a fallback
/// any PHi from the loop header is usable, but may have less chance of
/// surviving subsequent transforms.
static llvm::PHINode *GetInductionVariable(const Loop &L, ScalarEvolution &SE,
                                           const LSRInstance &LSR) {

  auto IsSuitableIV = [&](PHINode *P) {
    if (!SE.isSCEVable(P->getType()))
      return false;
    if (const SCEVAddRecExpr *Rec = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(P)))
      return Rec->isAffine() && !SE.containsUndefs(SE.getSCEV(P));
    return false;
  };

  // For now, just pick the first IV that was generated and inserted by
  // ScalarEvolution. Ideally pick an IV that is unlikely to be optimised away
  // by subsequent transforms.
  for (const WeakVH &IV : LSR.getScalarEvolutionIVs()) {
    if (!IV)
      continue;

    // There should only be PHI node IVs.
    PHINode *P = cast<PHINode>(&*IV);

    if (IsSuitableIV(P))
      return P;
  }

  for (PHINode &P : L.getHeader()->phis()) {
    if (IsSuitableIV(&P))
      return &P;
  }
  return nullptr;
}

static std::optional<std::tuple<PHINode *, PHINode *, const SCEV *, bool>>
canFoldTermCondOfLoop(Loop *L, ScalarEvolution &SE, DominatorTree &DT,
                      const LoopInfo &LI, const TargetTransformInfo &TTI) {
  if (!L->isInnermost()) {
    LLVM_DEBUG(dbgs() << "Cannot fold on non-innermost loop\n");
    return std::nullopt;
  }
  // Only inspect on simple loop structure
  if (!L->isLoopSimplifyForm()) {
    LLVM_DEBUG(dbgs() << "Cannot fold on non-simple loop\n");
    return std::nullopt;
  }

  if (!SE.hasLoopInvariantBackedgeTakenCount(L)) {
    LLVM_DEBUG(dbgs() << "Cannot fold on backedge that is loop variant\n");
    return std::nullopt;
  }

  BasicBlock *LoopLatch = L->getLoopLatch();
  BranchInst *BI = dyn_cast<BranchInst>(LoopLatch->getTerminator());
  if (!BI || BI->isUnconditional())
    return std::nullopt;
  auto *TermCond = dyn_cast<ICmpInst>(BI->getCondition());
  if (!TermCond) {
    LLVM_DEBUG(
        dbgs() << "Cannot fold on branching condition that is not an ICmpInst");
    return std::nullopt;
  }
  if (!TermCond->hasOneUse()) {
    LLVM_DEBUG(
        dbgs()
        << "Cannot replace terminating condition with more than one use\n");
    return std::nullopt;
  }

  BinaryOperator *LHS = dyn_cast<BinaryOperator>(TermCond->getOperand(0));
  Value *RHS = TermCond->getOperand(1);
  if (!LHS || !L->isLoopInvariant(RHS))
    // We could pattern match the inverse form of the icmp, but that is
    // non-canonical, and this pass is running *very* late in the pipeline.
    return std::nullopt;

  // Find the IV used by the current exit condition.
  PHINode *ToFold;
  Value *ToFoldStart, *ToFoldStep;
  if (!matchSimpleRecurrence(LHS, ToFold, ToFoldStart, ToFoldStep))
    return std::nullopt;

  // Ensure the simple recurrence is a part of the current loop.
  if (ToFold->getParent() != L->getHeader())
    return std::nullopt;

  // If that IV isn't dead after we rewrite the exit condition in terms of
  // another IV, there's no point in doing the transform.
  if (!isAlmostDeadIV(ToFold, LoopLatch, TermCond))
    return std::nullopt;

  // Inserting instructions in the preheader has a runtime cost, scale
  // the allowed cost with the loops trip count as best we can.
  const unsigned ExpansionBudget = [&]() {
    unsigned Budget = 2 * SCEVCheapExpansionBudget;
    if (unsigned SmallTC = SE.getSmallConstantMaxTripCount(L))
      return std::min(Budget, SmallTC);
    if (std::optional<unsigned> SmallTC = getLoopEstimatedTripCount(L))
      return std::min(Budget, *SmallTC);
    // Unknown trip count, assume long running by default.
    return Budget;
  }();

  const SCEV *BECount = SE.getBackedgeTakenCount(L);
  const DataLayout &DL = L->getHeader()->getDataLayout();
  SCEVExpander Expander(SE, DL, "lsr_fold_term_cond");

  PHINode *ToHelpFold = nullptr;
  const SCEV *TermValueS = nullptr;
  bool MustDropPoison = false;
  auto InsertPt = L->getLoopPreheader()->getTerminator();
  for (PHINode &PN : L->getHeader()->phis()) {
    if (ToFold == &PN)
      continue;

    if (!SE.isSCEVable(PN.getType())) {
      LLVM_DEBUG(dbgs() << "IV of phi '" << PN
                        << "' is not SCEV-able, not qualified for the "
                           "terminating condition folding.\n");
      continue;
    }
    const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(SE.getSCEV(&PN));
    // Only speculate on affine AddRec
    if (!AddRec || !AddRec->isAffine()) {
      LLVM_DEBUG(dbgs() << "SCEV of phi '" << PN
                        << "' is not an affine add recursion, not qualified "
                           "for the terminating condition folding.\n");
      continue;
    }

    // Check that we can compute the value of AddRec on the exiting iteration
    // without soundness problems.  evaluateAtIteration internally needs
    // to multiply the stride of the iteration number - which may wrap around.
    // The issue here is subtle because computing the result accounting for
    // wrap is insufficient. In order to use the result in an exit test, we
    // must also know that AddRec doesn't take the same value on any previous
    // iteration. The simplest case to consider is a candidate IV which is
    // narrower than the trip count (and thus original IV), but this can
    // also happen due to non-unit strides on the candidate IVs.
    if (!AddRec->hasNoSelfWrap() ||
        !SE.isKnownNonZero(AddRec->getStepRecurrence(SE)))
      continue;

    const SCEVAddRecExpr *PostInc = AddRec->getPostIncExpr(SE);
    const SCEV *TermValueSLocal = PostInc->evaluateAtIteration(BECount, SE);
    if (!Expander.isSafeToExpand(TermValueSLocal)) {
      LLVM_DEBUG(
          dbgs() << "Is not safe to expand terminating value for phi node" << PN
                 << "\n");
      continue;
    }

    if (Expander.isHighCostExpansion(TermValueSLocal, L, ExpansionBudget,
                                     &TTI, InsertPt)) {
      LLVM_DEBUG(
          dbgs() << "Is too expensive to expand terminating value for phi node"
                 << PN << "\n");
      continue;
    }

    // The candidate IV may have been otherwise dead and poison from the
    // very first iteration.  If we can't disprove that, we can't use the IV.
    if (!mustExecuteUBIfPoisonOnPathTo(&PN, LoopLatch->getTerminator(), &DT)) {
      LLVM_DEBUG(dbgs() << "Can not prove poison safety for IV "
                        << PN << "\n");
      continue;
    }

    // The candidate IV may become poison on the last iteration.  If this
    // value is not branched on, this is a well defined program.  We're
    // about to add a new use to this IV, and we have to ensure we don't
    // insert UB which didn't previously exist.
    bool MustDropPoisonLocal = false;
    Instruction *PostIncV =
      cast<Instruction>(PN.getIncomingValueForBlock(LoopLatch));
    if (!mustExecuteUBIfPoisonOnPathTo(PostIncV, LoopLatch->getTerminator(),
                                       &DT)) {
      LLVM_DEBUG(dbgs() << "Can not prove poison safety to insert use"
                        << PN << "\n");

      // If this is a complex recurrance with multiple instructions computing
      // the backedge value, we might need to strip poison flags from all of
      // them.
      if (PostIncV->getOperand(0) != &PN)
        continue;

      // In order to perform the transform, we need to drop the poison generating
      // flags on this instruction (if any).
      MustDropPoisonLocal = PostIncV->hasPoisonGeneratingFlags();
    }

    // We pick the last legal alternate IV.  We could expore choosing an optimal
    // alternate IV if we had a decent heuristic to do so.
    ToHelpFold = &PN;
    TermValueS = TermValueSLocal;
    MustDropPoison = MustDropPoisonLocal;
  }

  LLVM_DEBUG(if (ToFold && !ToHelpFold) dbgs()
                 << "Cannot find other AddRec IV to help folding\n";);

  LLVM_DEBUG(if (ToFold && ToHelpFold) dbgs()
             << "\nFound loop that can fold terminating condition\n"
             << "  BECount (SCEV): " << *SE.getBackedgeTakenCount(L) << "\n"
             << "  TermCond: " << *TermCond << "\n"
             << "  BrandInst: " << *BI << "\n"
             << "  ToFold: " << *ToFold << "\n"
             << "  ToHelpFold: " << *ToHelpFold << "\n");

  if (!ToFold || !ToHelpFold)
    return std::nullopt;
  return std::make_tuple(ToFold, ToHelpFold, TermValueS, MustDropPoison);
}

static bool ReduceLoopStrength(Loop *L, IVUsers &IU, ScalarEvolution &SE,
                               DominatorTree &DT, LoopInfo &LI,
                               const TargetTransformInfo &TTI,
                               AssumptionCache &AC, TargetLibraryInfo &TLI,
                               MemorySSA *MSSA) {

  // Debug preservation - before we start removing anything identify which DVI
  // meet the salvageable criteria and store their DIExpression and SCEVs.
  SmallVector<std::unique_ptr<DVIRecoveryRec>, 2> SalvageableDVIRecords;
  SmallSet<AssertingVH<DbgValueInst>, 2> DVIHandles;
  DbgGatherSalvagableDVI(L, SE, SalvageableDVIRecords, DVIHandles);

  bool Changed = false;
  std::unique_ptr<MemorySSAUpdater> MSSAU;
  if (MSSA)
    MSSAU = std::make_unique<MemorySSAUpdater>(MSSA);

  // Run the main LSR transformation.
  const LSRInstance &Reducer =
      LSRInstance(L, IU, SE, DT, LI, TTI, AC, TLI, MSSAU.get());
  Changed |= Reducer.getChanged();

  // Remove any extra phis created by processing inner loops.
  Changed |= DeleteDeadPHIs(L->getHeader(), &TLI, MSSAU.get());
  if (EnablePhiElim && L->isLoopSimplifyForm()) {
    SmallVector<WeakTrackingVH, 16> DeadInsts;
    const DataLayout &DL = L->getHeader()->getDataLayout();
    SCEVExpander Rewriter(SE, DL, "lsr", false);
#ifndef NDEBUG
    Rewriter.setDebugType(DEBUG_TYPE);
#endif
    unsigned numFolded = Rewriter.replaceCongruentIVs(L, &DT, DeadInsts, &TTI);
    Rewriter.clear();
    if (numFolded) {
      Changed = true;
      RecursivelyDeleteTriviallyDeadInstructionsPermissive(DeadInsts, &TLI,
                                                           MSSAU.get());
      DeleteDeadPHIs(L->getHeader(), &TLI, MSSAU.get());
    }
  }
  // LSR may at times remove all uses of an induction variable from a loop.
  // The only remaining use is the PHI in the exit block.
  // When this is the case, if the exit value of the IV can be calculated using
  // SCEV, we can replace the exit block PHI with the final value of the IV and
  // skip the updates in each loop iteration.
  if (L->isRecursivelyLCSSAForm(DT, LI) && L->getExitBlock()) {
    SmallVector<WeakTrackingVH, 16> DeadInsts;
    const DataLayout &DL = L->getHeader()->getDataLayout();
    SCEVExpander Rewriter(SE, DL, "lsr", true);
    int Rewrites = rewriteLoopExitValues(L, &LI, &TLI, &SE, &TTI, Rewriter, &DT,
                                         UnusedIndVarInLoop, DeadInsts);
    Rewriter.clear();
    if (Rewrites) {
      Changed = true;
      RecursivelyDeleteTriviallyDeadInstructionsPermissive(DeadInsts, &TLI,
                                                           MSSAU.get());
      DeleteDeadPHIs(L->getHeader(), &TLI, MSSAU.get());
    }
  }

  const bool EnableFormTerm = [&] {
    switch (AllowTerminatingConditionFoldingAfterLSR) {
    case cl::BOU_TRUE:
      return true;
    case cl::BOU_FALSE:
      return false;
    case cl::BOU_UNSET:
      return TTI.shouldFoldTerminatingConditionAfterLSR();
    }
    llvm_unreachable("Unhandled cl::boolOrDefault enum");
  }();

  if (EnableFormTerm) {
    if (auto Opt = canFoldTermCondOfLoop(L, SE, DT, LI, TTI)) {
      auto [ToFold, ToHelpFold, TermValueS, MustDrop] = *Opt;

      Changed = true;
      NumTermFold++;

      BasicBlock *LoopPreheader = L->getLoopPreheader();
      BasicBlock *LoopLatch = L->getLoopLatch();

      (void)ToFold;
      LLVM_DEBUG(dbgs() << "To fold phi-node:\n"
                        << *ToFold << "\n"
                        << "New term-cond phi-node:\n"
                        << *ToHelpFold << "\n");

      Value *StartValue = ToHelpFold->getIncomingValueForBlock(LoopPreheader);
      (void)StartValue;
      Value *LoopValue = ToHelpFold->getIncomingValueForBlock(LoopLatch);

      // See comment in canFoldTermCondOfLoop on why this is sufficient.
      if (MustDrop)
        cast<Instruction>(LoopValue)->dropPoisonGeneratingFlags();

      // SCEVExpander for both use in preheader and latch
      const DataLayout &DL = L->getHeader()->getDataLayout();
      SCEVExpander Expander(SE, DL, "lsr_fold_term_cond");

      assert(Expander.isSafeToExpand(TermValueS) &&
             "Terminating value was checked safe in canFoldTerminatingCondition");

      // Create new terminating value at loop preheader
      Value *TermValue = Expander.expandCodeFor(TermValueS, ToHelpFold->getType(),
                                                LoopPreheader->getTerminator());

      LLVM_DEBUG(dbgs() << "Start value of new term-cond phi-node:\n"
                        << *StartValue << "\n"
                        << "Terminating value of new term-cond phi-node:\n"
                        << *TermValue << "\n");

      // Create new terminating condition at loop latch
      BranchInst *BI = cast<BranchInst>(LoopLatch->getTerminator());
      ICmpInst *OldTermCond = cast<ICmpInst>(BI->getCondition());
      IRBuilder<> LatchBuilder(LoopLatch->getTerminator());
      Value *NewTermCond =
          LatchBuilder.CreateICmp(CmpInst::ICMP_EQ, LoopValue, TermValue,
                                  "lsr_fold_term_cond.replaced_term_cond");
      // Swap successors to exit loop body if IV equals to new TermValue
      if (BI->getSuccessor(0) == L->getHeader())
        BI->swapSuccessors();

      LLVM_DEBUG(dbgs() << "Old term-cond:\n"
                        << *OldTermCond << "\n"
                        << "New term-cond:\n" << *NewTermCond << "\n");

      BI->setCondition(NewTermCond);

      Expander.clear();
      OldTermCond->eraseFromParent();
      DeleteDeadPHIs(L->getHeader(), &TLI, MSSAU.get());
    }
  }

  if (SalvageableDVIRecords.empty())
    return Changed;

  // Obtain relevant IVs and attempt to rewrite the salvageable DVIs with
  // expressions composed using the derived iteration count.
  // TODO: Allow for multiple IV references for nested AddRecSCEVs
  for (const auto &L : LI) {
    if (llvm::PHINode *IV = GetInductionVariable(*L, SE, Reducer))
      DbgRewriteSalvageableDVIs(L, SE, IV, SalvageableDVIRecords);
    else {
      LLVM_DEBUG(dbgs() << "scev-salvage: SCEV salvaging not possible. An IV "
                           "could not be identified.\n");
    }
  }

  for (auto &Rec : SalvageableDVIRecords)
    Rec->clear();
  SalvageableDVIRecords.clear();
  DVIHandles.clear();
  return Changed;
}

bool LoopStrengthReduce::runOnLoop(Loop *L, LPPassManager & /*LPM*/) {
  if (skipLoop(L))
    return false;

  auto &IU = getAnalysis<IVUsersWrapperPass>().getIU();
  auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  const auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(
      *L->getHeader()->getParent());
  auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(
      *L->getHeader()->getParent());
  auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(
      *L->getHeader()->getParent());
  auto *MSSAAnalysis = getAnalysisIfAvailable<MemorySSAWrapperPass>();
  MemorySSA *MSSA = nullptr;
  if (MSSAAnalysis)
    MSSA = &MSSAAnalysis->getMSSA();
  return ReduceLoopStrength(L, IU, SE, DT, LI, TTI, AC, TLI, MSSA);
}

PreservedAnalyses LoopStrengthReducePass::run(Loop &L, LoopAnalysisManager &AM,
                                              LoopStandardAnalysisResults &AR,
                                              LPMUpdater &) {
  if (!ReduceLoopStrength(&L, AM.getResult<IVUsersAnalysis>(L, AR), AR.SE,
                          AR.DT, AR.LI, AR.TTI, AR.AC, AR.TLI, AR.MSSA))
    return PreservedAnalyses::all();

  auto PA = getLoopPassPreservedAnalyses();
  if (AR.MSSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}

char LoopStrengthReduce::ID = 0;

INITIALIZE_PASS_BEGIN(LoopStrengthReduce, "loop-reduce",
                      "Loop Strength Reduction", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(IVUsersWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(LoopStrengthReduce, "loop-reduce",
                    "Loop Strength Reduction", false, false)

Pass *llvm::createLoopStrengthReducePass() { return new LoopStrengthReduce(); }
