//===- EarlyCSE.cpp - Simple and fast CSE pass ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs a simple dominator tree walk that eliminates trivially
// redundant instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/GuardUtils.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/RecyclingAllocator.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <deque>
#include <memory>
#include <utility>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "early-cse"

STATISTIC(NumSimplify, "Number of instructions simplified or DCE'd");
STATISTIC(NumCSE,      "Number of instructions CSE'd");
STATISTIC(NumCSECVP,   "Number of compare instructions CVP'd");
STATISTIC(NumCSELoad,  "Number of load instructions CSE'd");
STATISTIC(NumCSECall,  "Number of call instructions CSE'd");
STATISTIC(NumCSEGEP, "Number of GEP instructions CSE'd");
STATISTIC(NumDSE,      "Number of trivial dead stores removed");

DEBUG_COUNTER(CSECounter, "early-cse",
              "Controls which instructions are removed");

static cl::opt<unsigned> EarlyCSEMssaOptCap(
    "earlycse-mssa-optimization-cap", cl::init(500), cl::Hidden,
    cl::desc("Enable imprecision in EarlyCSE in pathological cases, in exchange "
             "for faster compile. Caps the MemorySSA clobbering calls."));

static cl::opt<bool> EarlyCSEDebugHash(
    "earlycse-debug-hash", cl::init(false), cl::Hidden,
    cl::desc("Perform extra assertion checking to verify that SimpleValue's hash "
             "function is well-behaved w.r.t. its isEqual predicate"));

//===----------------------------------------------------------------------===//
// SimpleValue
//===----------------------------------------------------------------------===//

namespace {

/// Struct representing the available values in the scoped hash table.
struct SimpleValue {
  Instruction *Inst;

  SimpleValue(Instruction *I) : Inst(I) {
    assert((isSentinel() || canHandle(I)) && "Inst can't be handled!");
  }

  bool isSentinel() const {
    return Inst == DenseMapInfo<Instruction *>::getEmptyKey() ||
           Inst == DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static bool canHandle(Instruction *Inst) {
    // This can only handle non-void readnone functions.
    // Also handled are constrained intrinsic that look like the types
    // of instruction handled below (UnaryOperator, etc.).
    if (CallInst *CI = dyn_cast<CallInst>(Inst)) {
      if (Function *F = CI->getCalledFunction()) {
        switch ((Intrinsic::ID)F->getIntrinsicID()) {
        case Intrinsic::experimental_constrained_fadd:
        case Intrinsic::experimental_constrained_fsub:
        case Intrinsic::experimental_constrained_fmul:
        case Intrinsic::experimental_constrained_fdiv:
        case Intrinsic::experimental_constrained_frem:
        case Intrinsic::experimental_constrained_fptosi:
        case Intrinsic::experimental_constrained_sitofp:
        case Intrinsic::experimental_constrained_fptoui:
        case Intrinsic::experimental_constrained_uitofp:
        case Intrinsic::experimental_constrained_fcmp:
        case Intrinsic::experimental_constrained_fcmps: {
          auto *CFP = cast<ConstrainedFPIntrinsic>(CI);
          if (CFP->getExceptionBehavior() &&
              CFP->getExceptionBehavior() == fp::ebStrict)
            return false;
          // Since we CSE across function calls we must not allow
          // the rounding mode to change.
          if (CFP->getRoundingMode() &&
              CFP->getRoundingMode() == RoundingMode::Dynamic)
            return false;
          return true;
        }
        }
      }
      return CI->doesNotAccessMemory() && !CI->getType()->isVoidTy() &&
             // FIXME: Currently the calls which may access the thread id may
             // be considered as not accessing the memory. But this is
             // problematic for coroutines, since coroutines may resume in a
             // different thread. So we disable the optimization here for the
             // correctness. However, it may block many other correct
             // optimizations. Revert this one when we detect the memory
             // accessing kind more precisely.
             !CI->getFunction()->isPresplitCoroutine();
    }
    return isa<CastInst>(Inst) || isa<UnaryOperator>(Inst) ||
           isa<BinaryOperator>(Inst) || isa<CmpInst>(Inst) ||
           isa<SelectInst>(Inst) || isa<ExtractElementInst>(Inst) ||
           isa<InsertElementInst>(Inst) || isa<ShuffleVectorInst>(Inst) ||
           isa<ExtractValueInst>(Inst) || isa<InsertValueInst>(Inst) ||
           isa<FreezeInst>(Inst);
  }
};

} // end anonymous namespace

namespace llvm {

template <> struct DenseMapInfo<SimpleValue> {
  static inline SimpleValue getEmptyKey() {
    return DenseMapInfo<Instruction *>::getEmptyKey();
  }

  static inline SimpleValue getTombstoneKey() {
    return DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static unsigned getHashValue(SimpleValue Val);
  static bool isEqual(SimpleValue LHS, SimpleValue RHS);
};

} // end namespace llvm

/// Match a 'select' including an optional 'not's of the condition.
static bool matchSelectWithOptionalNotCond(Value *V, Value *&Cond, Value *&A,
                                           Value *&B,
                                           SelectPatternFlavor &Flavor) {
  // Return false if V is not even a select.
  if (!match(V, m_Select(m_Value(Cond), m_Value(A), m_Value(B))))
    return false;

  // Look through a 'not' of the condition operand by swapping A/B.
  Value *CondNot;
  if (match(Cond, m_Not(m_Value(CondNot)))) {
    Cond = CondNot;
    std::swap(A, B);
  }

  // Match canonical forms of min/max. We are not using ValueTracking's
  // more powerful matchSelectPattern() because it may rely on instruction flags
  // such as "nsw". That would be incompatible with the current hashing
  // mechanism that may remove flags to increase the likelihood of CSE.

  Flavor = SPF_UNKNOWN;
  CmpInst::Predicate Pred;

  if (!match(Cond, m_ICmp(Pred, m_Specific(A), m_Specific(B)))) {
    // Check for commuted variants of min/max by swapping predicate.
    // If we do not match the standard or commuted patterns, this is not a
    // recognized form of min/max, but it is still a select, so return true.
    if (!match(Cond, m_ICmp(Pred, m_Specific(B), m_Specific(A))))
      return true;
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  switch (Pred) {
  case CmpInst::ICMP_UGT: Flavor = SPF_UMAX; break;
  case CmpInst::ICMP_ULT: Flavor = SPF_UMIN; break;
  case CmpInst::ICMP_SGT: Flavor = SPF_SMAX; break;
  case CmpInst::ICMP_SLT: Flavor = SPF_SMIN; break;
  // Non-strict inequalities.
  case CmpInst::ICMP_ULE: Flavor = SPF_UMIN; break;
  case CmpInst::ICMP_UGE: Flavor = SPF_UMAX; break;
  case CmpInst::ICMP_SLE: Flavor = SPF_SMIN; break;
  case CmpInst::ICMP_SGE: Flavor = SPF_SMAX; break;
  default: break;
  }

  return true;
}

static unsigned hashCallInst(CallInst *CI) {
  // Don't CSE convergent calls in different basic blocks, because they
  // implicitly depend on the set of threads that is currently executing.
  if (CI->isConvergent()) {
    return hash_combine(
        CI->getOpcode(), CI->getParent(),
        hash_combine_range(CI->value_op_begin(), CI->value_op_end()));
  }
  return hash_combine(
      CI->getOpcode(),
      hash_combine_range(CI->value_op_begin(), CI->value_op_end()));
}

static unsigned getHashValueImpl(SimpleValue Val) {
  Instruction *Inst = Val.Inst;
  // Hash in all of the operands as pointers.
  if (BinaryOperator *BinOp = dyn_cast<BinaryOperator>(Inst)) {
    Value *LHS = BinOp->getOperand(0);
    Value *RHS = BinOp->getOperand(1);
    if (BinOp->isCommutative() && BinOp->getOperand(0) > BinOp->getOperand(1))
      std::swap(LHS, RHS);

    return hash_combine(BinOp->getOpcode(), LHS, RHS);
  }

  if (CmpInst *CI = dyn_cast<CmpInst>(Inst)) {
    // Compares can be commuted by swapping the comparands and
    // updating the predicate.  Choose the form that has the
    // comparands in sorted order, or in the case of a tie, the
    // one with the lower predicate.
    Value *LHS = CI->getOperand(0);
    Value *RHS = CI->getOperand(1);
    CmpInst::Predicate Pred = CI->getPredicate();
    CmpInst::Predicate SwappedPred = CI->getSwappedPredicate();
    if (std::tie(LHS, Pred) > std::tie(RHS, SwappedPred)) {
      std::swap(LHS, RHS);
      Pred = SwappedPred;
    }
    return hash_combine(Inst->getOpcode(), Pred, LHS, RHS);
  }

  // Hash general selects to allow matching commuted true/false operands.
  SelectPatternFlavor SPF;
  Value *Cond, *A, *B;
  if (matchSelectWithOptionalNotCond(Inst, Cond, A, B, SPF)) {
    // Hash min/max (cmp + select) to allow for commuted operands.
    // Min/max may also have non-canonical compare predicate (eg, the compare for
    // smin may use 'sgt' rather than 'slt'), and non-canonical operands in the
    // compare.
    // TODO: We should also detect FP min/max.
    if (SPF == SPF_SMIN || SPF == SPF_SMAX ||
        SPF == SPF_UMIN || SPF == SPF_UMAX) {
      if (A > B)
        std::swap(A, B);
      return hash_combine(Inst->getOpcode(), SPF, A, B);
    }

    // Hash general selects to allow matching commuted true/false operands.

    // If we do not have a compare as the condition, just hash in the condition.
    CmpInst::Predicate Pred;
    Value *X, *Y;
    if (!match(Cond, m_Cmp(Pred, m_Value(X), m_Value(Y))))
      return hash_combine(Inst->getOpcode(), Cond, A, B);

    // Similar to cmp normalization (above) - canonicalize the predicate value:
    // select (icmp Pred, X, Y), A, B --> select (icmp InvPred, X, Y), B, A
    if (CmpInst::getInversePredicate(Pred) < Pred) {
      Pred = CmpInst::getInversePredicate(Pred);
      std::swap(A, B);
    }
    return hash_combine(Inst->getOpcode(), Pred, X, Y, A, B);
  }

  if (CastInst *CI = dyn_cast<CastInst>(Inst))
    return hash_combine(CI->getOpcode(), CI->getType(), CI->getOperand(0));

  if (FreezeInst *FI = dyn_cast<FreezeInst>(Inst))
    return hash_combine(FI->getOpcode(), FI->getOperand(0));

  if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(Inst))
    return hash_combine(EVI->getOpcode(), EVI->getOperand(0),
                        hash_combine_range(EVI->idx_begin(), EVI->idx_end()));

  if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(Inst))
    return hash_combine(IVI->getOpcode(), IVI->getOperand(0),
                        IVI->getOperand(1),
                        hash_combine_range(IVI->idx_begin(), IVI->idx_end()));

  assert((isa<CallInst>(Inst) || isa<ExtractElementInst>(Inst) ||
          isa<InsertElementInst>(Inst) || isa<ShuffleVectorInst>(Inst) ||
          isa<UnaryOperator>(Inst) || isa<FreezeInst>(Inst)) &&
         "Invalid/unknown instruction");

  // Handle intrinsics with commutative operands.
  auto *II = dyn_cast<IntrinsicInst>(Inst);
  if (II && II->isCommutative() && II->arg_size() >= 2) {
    Value *LHS = II->getArgOperand(0), *RHS = II->getArgOperand(1);
    if (LHS > RHS)
      std::swap(LHS, RHS);
    return hash_combine(
        II->getOpcode(), LHS, RHS,
        hash_combine_range(II->value_op_begin() + 2, II->value_op_end()));
  }

  // gc.relocate is 'special' call: its second and third operands are
  // not real values, but indices into statepoint's argument list.
  // Get values they point to.
  if (const GCRelocateInst *GCR = dyn_cast<GCRelocateInst>(Inst))
    return hash_combine(GCR->getOpcode(), GCR->getOperand(0),
                        GCR->getBasePtr(), GCR->getDerivedPtr());

  // Don't CSE convergent calls in different basic blocks, because they
  // implicitly depend on the set of threads that is currently executing.
  if (CallInst *CI = dyn_cast<CallInst>(Inst))
    return hashCallInst(CI);

  // Mix in the opcode.
  return hash_combine(
      Inst->getOpcode(),
      hash_combine_range(Inst->value_op_begin(), Inst->value_op_end()));
}

unsigned DenseMapInfo<SimpleValue>::getHashValue(SimpleValue Val) {
#ifndef NDEBUG
  // If -earlycse-debug-hash was specified, return a constant -- this
  // will force all hashing to collide, so we'll exhaustively search
  // the table for a match, and the assertion in isEqual will fire if
  // there's a bug causing equal keys to hash differently.
  if (EarlyCSEDebugHash)
    return 0;
#endif
  return getHashValueImpl(Val);
}

static bool isEqualImpl(SimpleValue LHS, SimpleValue RHS) {
  Instruction *LHSI = LHS.Inst, *RHSI = RHS.Inst;

  if (LHS.isSentinel() || RHS.isSentinel())
    return LHSI == RHSI;

  if (LHSI->getOpcode() != RHSI->getOpcode())
    return false;
  if (LHSI->isIdenticalToWhenDefined(RHSI)) {
    // Convergent calls implicitly depend on the set of threads that is
    // currently executing, so conservatively return false if they are in
    // different basic blocks.
    if (CallInst *CI = dyn_cast<CallInst>(LHSI);
        CI && CI->isConvergent() && LHSI->getParent() != RHSI->getParent())
      return false;

    return true;
  }

  // If we're not strictly identical, we still might be a commutable instruction
  if (BinaryOperator *LHSBinOp = dyn_cast<BinaryOperator>(LHSI)) {
    if (!LHSBinOp->isCommutative())
      return false;

    assert(isa<BinaryOperator>(RHSI) &&
           "same opcode, but different instruction type?");
    BinaryOperator *RHSBinOp = cast<BinaryOperator>(RHSI);

    // Commuted equality
    return LHSBinOp->getOperand(0) == RHSBinOp->getOperand(1) &&
           LHSBinOp->getOperand(1) == RHSBinOp->getOperand(0);
  }
  if (CmpInst *LHSCmp = dyn_cast<CmpInst>(LHSI)) {
    assert(isa<CmpInst>(RHSI) &&
           "same opcode, but different instruction type?");
    CmpInst *RHSCmp = cast<CmpInst>(RHSI);
    // Commuted equality
    return LHSCmp->getOperand(0) == RHSCmp->getOperand(1) &&
           LHSCmp->getOperand(1) == RHSCmp->getOperand(0) &&
           LHSCmp->getSwappedPredicate() == RHSCmp->getPredicate();
  }

  auto *LII = dyn_cast<IntrinsicInst>(LHSI);
  auto *RII = dyn_cast<IntrinsicInst>(RHSI);
  if (LII && RII && LII->getIntrinsicID() == RII->getIntrinsicID() &&
      LII->isCommutative() && LII->arg_size() >= 2) {
    return LII->getArgOperand(0) == RII->getArgOperand(1) &&
           LII->getArgOperand(1) == RII->getArgOperand(0) &&
           std::equal(LII->arg_begin() + 2, LII->arg_end(),
                      RII->arg_begin() + 2, RII->arg_end());
  }

  // See comment above in `getHashValue()`.
  if (const GCRelocateInst *GCR1 = dyn_cast<GCRelocateInst>(LHSI))
    if (const GCRelocateInst *GCR2 = dyn_cast<GCRelocateInst>(RHSI))
      return GCR1->getOperand(0) == GCR2->getOperand(0) &&
             GCR1->getBasePtr() == GCR2->getBasePtr() &&
             GCR1->getDerivedPtr() == GCR2->getDerivedPtr();

  // Min/max can occur with commuted operands, non-canonical predicates,
  // and/or non-canonical operands.
  // Selects can be non-trivially equivalent via inverted conditions and swaps.
  SelectPatternFlavor LSPF, RSPF;
  Value *CondL, *CondR, *LHSA, *RHSA, *LHSB, *RHSB;
  if (matchSelectWithOptionalNotCond(LHSI, CondL, LHSA, LHSB, LSPF) &&
      matchSelectWithOptionalNotCond(RHSI, CondR, RHSA, RHSB, RSPF)) {
    if (LSPF == RSPF) {
      // TODO: We should also detect FP min/max.
      if (LSPF == SPF_SMIN || LSPF == SPF_SMAX ||
          LSPF == SPF_UMIN || LSPF == SPF_UMAX)
        return ((LHSA == RHSA && LHSB == RHSB) ||
                (LHSA == RHSB && LHSB == RHSA));

      // select Cond, A, B <--> select not(Cond), B, A
      if (CondL == CondR && LHSA == RHSA && LHSB == RHSB)
        return true;
    }

    // If the true/false operands are swapped and the conditions are compares
    // with inverted predicates, the selects are equal:
    // select (icmp Pred, X, Y), A, B <--> select (icmp InvPred, X, Y), B, A
    //
    // This also handles patterns with a double-negation in the sense of not +
    // inverse, because we looked through a 'not' in the matching function and
    // swapped A/B:
    // select (cmp Pred, X, Y), A, B <--> select (not (cmp InvPred, X, Y)), B, A
    //
    // This intentionally does NOT handle patterns with a double-negation in
    // the sense of not + not, because doing so could result in values
    // comparing
    // as equal that hash differently in the min/max cases like:
    // select (cmp slt, X, Y), X, Y <--> select (not (not (cmp slt, X, Y))), X, Y
    //   ^ hashes as min                  ^ would not hash as min
    // In the context of the EarlyCSE pass, however, such cases never reach
    // this code, as we simplify the double-negation before hashing the second
    // select (and so still succeed at CSEing them).
    if (LHSA == RHSB && LHSB == RHSA) {
      CmpInst::Predicate PredL, PredR;
      Value *X, *Y;
      if (match(CondL, m_Cmp(PredL, m_Value(X), m_Value(Y))) &&
          match(CondR, m_Cmp(PredR, m_Specific(X), m_Specific(Y))) &&
          CmpInst::getInversePredicate(PredL) == PredR)
        return true;
    }
  }

  return false;
}

bool DenseMapInfo<SimpleValue>::isEqual(SimpleValue LHS, SimpleValue RHS) {
  // These comparisons are nontrivial, so assert that equality implies
  // hash equality (DenseMap demands this as an invariant).
  bool Result = isEqualImpl(LHS, RHS);
  assert(!Result || (LHS.isSentinel() && LHS.Inst == RHS.Inst) ||
         getHashValueImpl(LHS) == getHashValueImpl(RHS));
  return Result;
}

//===----------------------------------------------------------------------===//
// CallValue
//===----------------------------------------------------------------------===//

namespace {

/// Struct representing the available call values in the scoped hash
/// table.
struct CallValue {
  Instruction *Inst;

  CallValue(Instruction *I) : Inst(I) {
    assert((isSentinel() || canHandle(I)) && "Inst can't be handled!");
  }

  bool isSentinel() const {
    return Inst == DenseMapInfo<Instruction *>::getEmptyKey() ||
           Inst == DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static bool canHandle(Instruction *Inst) {
    // Don't value number anything that returns void.
    if (Inst->getType()->isVoidTy())
      return false;

    CallInst *CI = dyn_cast<CallInst>(Inst);
    if (!CI || !CI->onlyReadsMemory() ||
        // FIXME: Currently the calls which may access the thread id may
        // be considered as not accessing the memory. But this is
        // problematic for coroutines, since coroutines may resume in a
        // different thread. So we disable the optimization here for the
        // correctness. However, it may block many other correct
        // optimizations. Revert this one when we detect the memory
        // accessing kind more precisely.
        CI->getFunction()->isPresplitCoroutine())
      return false;
    return true;
  }
};

} // end anonymous namespace

namespace llvm {

template <> struct DenseMapInfo<CallValue> {
  static inline CallValue getEmptyKey() {
    return DenseMapInfo<Instruction *>::getEmptyKey();
  }

  static inline CallValue getTombstoneKey() {
    return DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static unsigned getHashValue(CallValue Val);
  static bool isEqual(CallValue LHS, CallValue RHS);
};

} // end namespace llvm

unsigned DenseMapInfo<CallValue>::getHashValue(CallValue Val) {
  Instruction *Inst = Val.Inst;

  // Hash all of the operands as pointers and mix in the opcode.
  return hashCallInst(cast<CallInst>(Inst));
}

bool DenseMapInfo<CallValue>::isEqual(CallValue LHS, CallValue RHS) {
  if (LHS.isSentinel() || RHS.isSentinel())
    return LHS.Inst == RHS.Inst;

  CallInst *LHSI = cast<CallInst>(LHS.Inst);
  CallInst *RHSI = cast<CallInst>(RHS.Inst);

  // Convergent calls implicitly depend on the set of threads that is
  // currently executing, so conservatively return false if they are in
  // different basic blocks.
  if (LHSI->isConvergent() && LHSI->getParent() != RHSI->getParent())
    return false;

  return LHSI->isIdenticalTo(RHSI);
}

//===----------------------------------------------------------------------===//
// GEPValue
//===----------------------------------------------------------------------===//

namespace {

struct GEPValue {
  Instruction *Inst;
  std::optional<int64_t> ConstantOffset;

  GEPValue(Instruction *I) : Inst(I) {
    assert((isSentinel() || canHandle(I)) && "Inst can't be handled!");
  }

  GEPValue(Instruction *I, std::optional<int64_t> ConstantOffset)
      : Inst(I), ConstantOffset(ConstantOffset) {
    assert((isSentinel() || canHandle(I)) && "Inst can't be handled!");
  }

  bool isSentinel() const {
    return Inst == DenseMapInfo<Instruction *>::getEmptyKey() ||
           Inst == DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static bool canHandle(Instruction *Inst) {
    return isa<GetElementPtrInst>(Inst);
  }
};

} // namespace

namespace llvm {

template <> struct DenseMapInfo<GEPValue> {
  static inline GEPValue getEmptyKey() {
    return DenseMapInfo<Instruction *>::getEmptyKey();
  }

  static inline GEPValue getTombstoneKey() {
    return DenseMapInfo<Instruction *>::getTombstoneKey();
  }

  static unsigned getHashValue(const GEPValue &Val);
  static bool isEqual(const GEPValue &LHS, const GEPValue &RHS);
};

} // end namespace llvm

unsigned DenseMapInfo<GEPValue>::getHashValue(const GEPValue &Val) {
  auto *GEP = cast<GetElementPtrInst>(Val.Inst);
  if (Val.ConstantOffset.has_value())
    return hash_combine(GEP->getOpcode(), GEP->getPointerOperand(),
                        Val.ConstantOffset.value());
  return hash_combine(
      GEP->getOpcode(),
      hash_combine_range(GEP->value_op_begin(), GEP->value_op_end()));
}

bool DenseMapInfo<GEPValue>::isEqual(const GEPValue &LHS, const GEPValue &RHS) {
  if (LHS.isSentinel() || RHS.isSentinel())
    return LHS.Inst == RHS.Inst;
  auto *LGEP = cast<GetElementPtrInst>(LHS.Inst);
  auto *RGEP = cast<GetElementPtrInst>(RHS.Inst);
  if (LGEP->getPointerOperand() != RGEP->getPointerOperand())
    return false;
  if (LHS.ConstantOffset.has_value() && RHS.ConstantOffset.has_value())
    return LHS.ConstantOffset.value() == RHS.ConstantOffset.value();
  return LGEP->isIdenticalToWhenDefined(RGEP);
}

//===----------------------------------------------------------------------===//
// EarlyCSE implementation
//===----------------------------------------------------------------------===//

namespace {

/// A simple and fast domtree-based CSE pass.
///
/// This pass does a simple depth-first walk over the dominator tree,
/// eliminating trivially redundant instructions and using instsimplify to
/// canonicalize things as it goes. It is intended to be fast and catch obvious
/// cases so that instcombine and other passes are more effective. It is
/// expected that a later pass of GVN will catch the interesting/hard cases.
class EarlyCSE {
public:
  const TargetLibraryInfo &TLI;
  const TargetTransformInfo &TTI;
  DominatorTree &DT;
  AssumptionCache &AC;
  const SimplifyQuery SQ;
  MemorySSA *MSSA;
  std::unique_ptr<MemorySSAUpdater> MSSAUpdater;

  using AllocatorTy =
      RecyclingAllocator<BumpPtrAllocator,
                         ScopedHashTableVal<SimpleValue, Value *>>;
  using ScopedHTType =
      ScopedHashTable<SimpleValue, Value *, DenseMapInfo<SimpleValue>,
                      AllocatorTy>;

  /// A scoped hash table of the current values of all of our simple
  /// scalar expressions.
  ///
  /// As we walk down the domtree, we look to see if instructions are in this:
  /// if so, we replace them with what we find, otherwise we insert them so
  /// that dominated values can succeed in their lookup.
  ScopedHTType AvailableValues;

  /// A scoped hash table of the current values of previously encountered
  /// memory locations.
  ///
  /// This allows us to get efficient access to dominating loads or stores when
  /// we have a fully redundant load.  In addition to the most recent load, we
  /// keep track of a generation count of the read, which is compared against
  /// the current generation count.  The current generation count is incremented
  /// after every possibly writing memory operation, which ensures that we only
  /// CSE loads with other loads that have no intervening store.  Ordering
  /// events (such as fences or atomic instructions) increment the generation
  /// count as well; essentially, we model these as writes to all possible
  /// locations.  Note that atomic and/or volatile loads and stores can be
  /// present the table; it is the responsibility of the consumer to inspect
  /// the atomicity/volatility if needed.
  struct LoadValue {
    Instruction *DefInst = nullptr;
    unsigned Generation = 0;
    int MatchingId = -1;
    bool IsAtomic = false;
    bool IsLoad = false;

    LoadValue() = default;
    LoadValue(Instruction *Inst, unsigned Generation, unsigned MatchingId,
              bool IsAtomic, bool IsLoad)
        : DefInst(Inst), Generation(Generation), MatchingId(MatchingId),
          IsAtomic(IsAtomic), IsLoad(IsLoad) {}
  };

  using LoadMapAllocator =
      RecyclingAllocator<BumpPtrAllocator,
                         ScopedHashTableVal<Value *, LoadValue>>;
  using LoadHTType =
      ScopedHashTable<Value *, LoadValue, DenseMapInfo<Value *>,
                      LoadMapAllocator>;

  LoadHTType AvailableLoads;

  // A scoped hash table mapping memory locations (represented as typed
  // addresses) to generation numbers at which that memory location became
  // (henceforth indefinitely) invariant.
  using InvariantMapAllocator =
      RecyclingAllocator<BumpPtrAllocator,
                         ScopedHashTableVal<MemoryLocation, unsigned>>;
  using InvariantHTType =
      ScopedHashTable<MemoryLocation, unsigned, DenseMapInfo<MemoryLocation>,
                      InvariantMapAllocator>;
  InvariantHTType AvailableInvariants;

  /// A scoped hash table of the current values of read-only call
  /// values.
  ///
  /// It uses the same generation count as loads.
  using CallHTType =
      ScopedHashTable<CallValue, std::pair<Instruction *, unsigned>>;
  CallHTType AvailableCalls;

  using GEPMapAllocatorTy =
      RecyclingAllocator<BumpPtrAllocator,
                         ScopedHashTableVal<GEPValue, Value *>>;
  using GEPHTType = ScopedHashTable<GEPValue, Value *, DenseMapInfo<GEPValue>,
                                    GEPMapAllocatorTy>;
  GEPHTType AvailableGEPs;

  /// This is the current generation of the memory value.
  unsigned CurrentGeneration = 0;

  /// Set up the EarlyCSE runner for a particular function.
  EarlyCSE(const DataLayout &DL, const TargetLibraryInfo &TLI,
           const TargetTransformInfo &TTI, DominatorTree &DT,
           AssumptionCache &AC, MemorySSA *MSSA)
      : TLI(TLI), TTI(TTI), DT(DT), AC(AC), SQ(DL, &TLI, &DT, &AC), MSSA(MSSA),
        MSSAUpdater(std::make_unique<MemorySSAUpdater>(MSSA)) {}

  bool run();

private:
  unsigned ClobberCounter = 0;
  // Almost a POD, but needs to call the constructors for the scoped hash
  // tables so that a new scope gets pushed on. These are RAII so that the
  // scope gets popped when the NodeScope is destroyed.
  class NodeScope {
  public:
    NodeScope(ScopedHTType &AvailableValues, LoadHTType &AvailableLoads,
              InvariantHTType &AvailableInvariants, CallHTType &AvailableCalls,
              GEPHTType &AvailableGEPs)
        : Scope(AvailableValues), LoadScope(AvailableLoads),
          InvariantScope(AvailableInvariants), CallScope(AvailableCalls),
          GEPScope(AvailableGEPs) {}
    NodeScope(const NodeScope &) = delete;
    NodeScope &operator=(const NodeScope &) = delete;

  private:
    ScopedHTType::ScopeTy Scope;
    LoadHTType::ScopeTy LoadScope;
    InvariantHTType::ScopeTy InvariantScope;
    CallHTType::ScopeTy CallScope;
    GEPHTType::ScopeTy GEPScope;
  };

  // Contains all the needed information to create a stack for doing a depth
  // first traversal of the tree. This includes scopes for values, loads, and
  // calls as well as the generation. There is a child iterator so that the
  // children do not need to be store separately.
  class StackNode {
  public:
    StackNode(ScopedHTType &AvailableValues, LoadHTType &AvailableLoads,
              InvariantHTType &AvailableInvariants, CallHTType &AvailableCalls,
              GEPHTType &AvailableGEPs, unsigned cg, DomTreeNode *n,
              DomTreeNode::const_iterator child,
              DomTreeNode::const_iterator end)
        : CurrentGeneration(cg), ChildGeneration(cg), Node(n), ChildIter(child),
          EndIter(end),
          Scopes(AvailableValues, AvailableLoads, AvailableInvariants,
                 AvailableCalls, AvailableGEPs) {}
    StackNode(const StackNode &) = delete;
    StackNode &operator=(const StackNode &) = delete;

    // Accessors.
    unsigned currentGeneration() const { return CurrentGeneration; }
    unsigned childGeneration() const { return ChildGeneration; }
    void childGeneration(unsigned generation) { ChildGeneration = generation; }
    DomTreeNode *node() { return Node; }
    DomTreeNode::const_iterator childIter() const { return ChildIter; }

    DomTreeNode *nextChild() {
      DomTreeNode *child = *ChildIter;
      ++ChildIter;
      return child;
    }

    DomTreeNode::const_iterator end() const { return EndIter; }
    bool isProcessed() const { return Processed; }
    void process() { Processed = true; }

  private:
    unsigned CurrentGeneration;
    unsigned ChildGeneration;
    DomTreeNode *Node;
    DomTreeNode::const_iterator ChildIter;
    DomTreeNode::const_iterator EndIter;
    NodeScope Scopes;
    bool Processed = false;
  };

  /// Wrapper class to handle memory instructions, including loads,
  /// stores and intrinsic loads and stores defined by the target.
  class ParseMemoryInst {
  public:
    ParseMemoryInst(Instruction *Inst, const TargetTransformInfo &TTI)
      : Inst(Inst) {
      if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(Inst)) {
        IntrID = II->getIntrinsicID();
        if (TTI.getTgtMemIntrinsic(II, Info))
          return;
        if (isHandledNonTargetIntrinsic(IntrID)) {
          switch (IntrID) {
          case Intrinsic::masked_load:
            Info.PtrVal = Inst->getOperand(0);
            Info.MatchingId = Intrinsic::masked_load;
            Info.ReadMem = true;
            Info.WriteMem = false;
            Info.IsVolatile = false;
            break;
          case Intrinsic::masked_store:
            Info.PtrVal = Inst->getOperand(1);
            // Use the ID of masked load as the "matching id". This will
            // prevent matching non-masked loads/stores with masked ones
            // (which could be done), but at the moment, the code here
            // does not support matching intrinsics with non-intrinsics,
            // so keep the MatchingIds specific to masked instructions
            // for now (TODO).
            Info.MatchingId = Intrinsic::masked_load;
            Info.ReadMem = false;
            Info.WriteMem = true;
            Info.IsVolatile = false;
            break;
          }
        }
      }
    }

    Instruction *get() { return Inst; }
    const Instruction *get() const { return Inst; }

    bool isLoad() const {
      if (IntrID != 0)
        return Info.ReadMem;
      return isa<LoadInst>(Inst);
    }

    bool isStore() const {
      if (IntrID != 0)
        return Info.WriteMem;
      return isa<StoreInst>(Inst);
    }

    bool isAtomic() const {
      if (IntrID != 0)
        return Info.Ordering != AtomicOrdering::NotAtomic;
      return Inst->isAtomic();
    }

    bool isUnordered() const {
      if (IntrID != 0)
        return Info.isUnordered();

      if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
        return LI->isUnordered();
      } else if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
        return SI->isUnordered();
      }
      // Conservative answer
      return !Inst->isAtomic();
    }

    bool isVolatile() const {
      if (IntrID != 0)
        return Info.IsVolatile;

      if (LoadInst *LI = dyn_cast<LoadInst>(Inst)) {
        return LI->isVolatile();
      } else if (StoreInst *SI = dyn_cast<StoreInst>(Inst)) {
        return SI->isVolatile();
      }
      // Conservative answer
      return true;
    }

    bool isInvariantLoad() const {
      if (auto *LI = dyn_cast<LoadInst>(Inst))
        return LI->hasMetadata(LLVMContext::MD_invariant_load);
      return false;
    }

    bool isValid() const { return getPointerOperand() != nullptr; }

    // For regular (non-intrinsic) loads/stores, this is set to -1. For
    // intrinsic loads/stores, the id is retrieved from the corresponding
    // field in the MemIntrinsicInfo structure.  That field contains
    // non-negative values only.
    int getMatchingId() const {
      if (IntrID != 0)
        return Info.MatchingId;
      return -1;
    }

    Value *getPointerOperand() const {
      if (IntrID != 0)
        return Info.PtrVal;
      return getLoadStorePointerOperand(Inst);
    }

    Type *getValueType() const {
      // TODO: handle target-specific intrinsics.
      return Inst->getAccessType();
    }

    bool mayReadFromMemory() const {
      if (IntrID != 0)
        return Info.ReadMem;
      return Inst->mayReadFromMemory();
    }

    bool mayWriteToMemory() const {
      if (IntrID != 0)
        return Info.WriteMem;
      return Inst->mayWriteToMemory();
    }

  private:
    Intrinsic::ID IntrID = 0;
    MemIntrinsicInfo Info;
    Instruction *Inst;
  };

  // This function is to prevent accidentally passing a non-target
  // intrinsic ID to TargetTransformInfo.
  static bool isHandledNonTargetIntrinsic(Intrinsic::ID ID) {
    switch (ID) {
    case Intrinsic::masked_load:
    case Intrinsic::masked_store:
      return true;
    }
    return false;
  }
  static bool isHandledNonTargetIntrinsic(const Value *V) {
    if (auto *II = dyn_cast<IntrinsicInst>(V))
      return isHandledNonTargetIntrinsic(II->getIntrinsicID());
    return false;
  }

  bool processNode(DomTreeNode *Node);

  bool handleBranchCondition(Instruction *CondInst, const BranchInst *BI,
                             const BasicBlock *BB, const BasicBlock *Pred);

  Value *getMatchingValue(LoadValue &InVal, ParseMemoryInst &MemInst,
                          unsigned CurrentGeneration);

  bool overridingStores(const ParseMemoryInst &Earlier,
                        const ParseMemoryInst &Later);

  Value *getOrCreateResult(Value *Inst, Type *ExpectedType) const {
    // TODO: We could insert relevant casts on type mismatch here.
    if (auto *LI = dyn_cast<LoadInst>(Inst))
      return LI->getType() == ExpectedType ? LI : nullptr;
    if (auto *SI = dyn_cast<StoreInst>(Inst)) {
      Value *V = SI->getValueOperand();
      return V->getType() == ExpectedType ? V : nullptr;
    }
    assert(isa<IntrinsicInst>(Inst) && "Instruction not supported");
    auto *II = cast<IntrinsicInst>(Inst);
    if (isHandledNonTargetIntrinsic(II->getIntrinsicID()))
      return getOrCreateResultNonTargetMemIntrinsic(II, ExpectedType);
    return TTI.getOrCreateResultFromMemIntrinsic(II, ExpectedType);
  }

  Value *getOrCreateResultNonTargetMemIntrinsic(IntrinsicInst *II,
                                                Type *ExpectedType) const {
    // TODO: We could insert relevant casts on type mismatch here.
    switch (II->getIntrinsicID()) {
    case Intrinsic::masked_load:
      return II->getType() == ExpectedType ? II : nullptr;
    case Intrinsic::masked_store: {
      Value *V = II->getOperand(0);
      return V->getType() == ExpectedType ? V : nullptr;
    }
    }
    return nullptr;
  }

  /// Return true if the instruction is known to only operate on memory
  /// provably invariant in the given "generation".
  bool isOperatingOnInvariantMemAt(Instruction *I, unsigned GenAt);

  bool isSameMemGeneration(unsigned EarlierGeneration, unsigned LaterGeneration,
                           Instruction *EarlierInst, Instruction *LaterInst);

  bool isNonTargetIntrinsicMatch(const IntrinsicInst *Earlier,
                                 const IntrinsicInst *Later) {
    auto IsSubmask = [](const Value *Mask0, const Value *Mask1) {
      // Is Mask0 a submask of Mask1?
      if (Mask0 == Mask1)
        return true;
      if (isa<UndefValue>(Mask0) || isa<UndefValue>(Mask1))
        return false;
      auto *Vec0 = dyn_cast<ConstantVector>(Mask0);
      auto *Vec1 = dyn_cast<ConstantVector>(Mask1);
      if (!Vec0 || !Vec1)
        return false;
      if (Vec0->getType() != Vec1->getType())
        return false;
      for (int i = 0, e = Vec0->getNumOperands(); i != e; ++i) {
        Constant *Elem0 = Vec0->getOperand(i);
        Constant *Elem1 = Vec1->getOperand(i);
        auto *Int0 = dyn_cast<ConstantInt>(Elem0);
        if (Int0 && Int0->isZero())
          continue;
        auto *Int1 = dyn_cast<ConstantInt>(Elem1);
        if (Int1 && !Int1->isZero())
          continue;
        if (isa<UndefValue>(Elem0) || isa<UndefValue>(Elem1))
          return false;
        if (Elem0 == Elem1)
          continue;
        return false;
      }
      return true;
    };
    auto PtrOp = [](const IntrinsicInst *II) {
      if (II->getIntrinsicID() == Intrinsic::masked_load)
        return II->getOperand(0);
      if (II->getIntrinsicID() == Intrinsic::masked_store)
        return II->getOperand(1);
      llvm_unreachable("Unexpected IntrinsicInst");
    };
    auto MaskOp = [](const IntrinsicInst *II) {
      if (II->getIntrinsicID() == Intrinsic::masked_load)
        return II->getOperand(2);
      if (II->getIntrinsicID() == Intrinsic::masked_store)
        return II->getOperand(3);
      llvm_unreachable("Unexpected IntrinsicInst");
    };
    auto ThruOp = [](const IntrinsicInst *II) {
      if (II->getIntrinsicID() == Intrinsic::masked_load)
        return II->getOperand(3);
      llvm_unreachable("Unexpected IntrinsicInst");
    };

    if (PtrOp(Earlier) != PtrOp(Later))
      return false;

    Intrinsic::ID IDE = Earlier->getIntrinsicID();
    Intrinsic::ID IDL = Later->getIntrinsicID();
    // We could really use specific intrinsic classes for masked loads
    // and stores in IntrinsicInst.h.
    if (IDE == Intrinsic::masked_load && IDL == Intrinsic::masked_load) {
      // Trying to replace later masked load with the earlier one.
      // Check that the pointers are the same, and
      // - masks and pass-throughs are the same, or
      // - replacee's pass-through is "undef" and replacer's mask is a
      //   super-set of the replacee's mask.
      if (MaskOp(Earlier) == MaskOp(Later) && ThruOp(Earlier) == ThruOp(Later))
        return true;
      if (!isa<UndefValue>(ThruOp(Later)))
        return false;
      return IsSubmask(MaskOp(Later), MaskOp(Earlier));
    }
    if (IDE == Intrinsic::masked_store && IDL == Intrinsic::masked_load) {
      // Trying to replace a load of a stored value with the store's value.
      // Check that the pointers are the same, and
      // - load's mask is a subset of store's mask, and
      // - load's pass-through is "undef".
      if (!IsSubmask(MaskOp(Later), MaskOp(Earlier)))
        return false;
      return isa<UndefValue>(ThruOp(Later));
    }
    if (IDE == Intrinsic::masked_load && IDL == Intrinsic::masked_store) {
      // Trying to remove a store of the loaded value.
      // Check that the pointers are the same, and
      // - store's mask is a subset of the load's mask.
      return IsSubmask(MaskOp(Later), MaskOp(Earlier));
    }
    if (IDE == Intrinsic::masked_store && IDL == Intrinsic::masked_store) {
      // Trying to remove a dead store (earlier).
      // Check that the pointers are the same,
      // - the to-be-removed store's mask is a subset of the other store's
      //   mask.
      return IsSubmask(MaskOp(Earlier), MaskOp(Later));
    }
    return false;
  }

  void removeMSSA(Instruction &Inst) {
    if (!MSSA)
      return;
    if (VerifyMemorySSA)
      MSSA->verifyMemorySSA();
    // Removing a store here can leave MemorySSA in an unoptimized state by
    // creating MemoryPhis that have identical arguments and by creating
    // MemoryUses whose defining access is not an actual clobber. The phi case
    // is handled by MemorySSA when passing OptimizePhis = true to
    // removeMemoryAccess.  The non-optimized MemoryUse case is lazily updated
    // by MemorySSA's getClobberingMemoryAccess.
    MSSAUpdater->removeMemoryAccess(&Inst, true);
  }
};

} // end anonymous namespace

/// Determine if the memory referenced by LaterInst is from the same heap
/// version as EarlierInst.
/// This is currently called in two scenarios:
///
///   load p
///   ...
///   load p
///
/// and
///
///   x = load p
///   ...
///   store x, p
///
/// in both cases we want to verify that there are no possible writes to the
/// memory referenced by p between the earlier and later instruction.
bool EarlyCSE::isSameMemGeneration(unsigned EarlierGeneration,
                                   unsigned LaterGeneration,
                                   Instruction *EarlierInst,
                                   Instruction *LaterInst) {
  // Check the simple memory generation tracking first.
  if (EarlierGeneration == LaterGeneration)
    return true;

  if (!MSSA)
    return false;

  // If MemorySSA has determined that one of EarlierInst or LaterInst does not
  // read/write memory, then we can safely return true here.
  // FIXME: We could be more aggressive when checking doesNotAccessMemory(),
  // onlyReadsMemory(), mayReadFromMemory(), and mayWriteToMemory() in this pass
  // by also checking the MemorySSA MemoryAccess on the instruction.  Initial
  // experiments suggest this isn't worthwhile, at least for C/C++ code compiled
  // with the default optimization pipeline.
  auto *EarlierMA = MSSA->getMemoryAccess(EarlierInst);
  if (!EarlierMA)
    return true;
  auto *LaterMA = MSSA->getMemoryAccess(LaterInst);
  if (!LaterMA)
    return true;

  // Since we know LaterDef dominates LaterInst and EarlierInst dominates
  // LaterInst, if LaterDef dominates EarlierInst then it can't occur between
  // EarlierInst and LaterInst and neither can any other write that potentially
  // clobbers LaterInst.
  MemoryAccess *LaterDef;
  if (ClobberCounter < EarlyCSEMssaOptCap) {
    LaterDef = MSSA->getWalker()->getClobberingMemoryAccess(LaterInst);
    ClobberCounter++;
  } else
    LaterDef = LaterMA->getDefiningAccess();

  return MSSA->dominates(LaterDef, EarlierMA);
}

bool EarlyCSE::isOperatingOnInvariantMemAt(Instruction *I, unsigned GenAt) {
  // A location loaded from with an invariant_load is assumed to *never* change
  // within the visible scope of the compilation.
  if (auto *LI = dyn_cast<LoadInst>(I))
    if (LI->hasMetadata(LLVMContext::MD_invariant_load))
      return true;

  auto MemLocOpt = MemoryLocation::getOrNone(I);
  if (!MemLocOpt)
    // "target" intrinsic forms of loads aren't currently known to
    // MemoryLocation::get.  TODO
    return false;
  MemoryLocation MemLoc = *MemLocOpt;
  if (!AvailableInvariants.count(MemLoc))
    return false;

  // Is the generation at which this became invariant older than the
  // current one?
  return AvailableInvariants.lookup(MemLoc) <= GenAt;
}

bool EarlyCSE::handleBranchCondition(Instruction *CondInst,
                                     const BranchInst *BI, const BasicBlock *BB,
                                     const BasicBlock *Pred) {
  assert(BI->isConditional() && "Should be a conditional branch!");
  assert(BI->getCondition() == CondInst && "Wrong condition?");
  assert(BI->getSuccessor(0) == BB || BI->getSuccessor(1) == BB);
  auto *TorF = (BI->getSuccessor(0) == BB)
                   ? ConstantInt::getTrue(BB->getContext())
                   : ConstantInt::getFalse(BB->getContext());
  auto MatchBinOp = [](Instruction *I, unsigned Opcode, Value *&LHS,
                       Value *&RHS) {
    if (Opcode == Instruction::And &&
        match(I, m_LogicalAnd(m_Value(LHS), m_Value(RHS))))
      return true;
    else if (Opcode == Instruction::Or &&
             match(I, m_LogicalOr(m_Value(LHS), m_Value(RHS))))
      return true;
    return false;
  };
  // If the condition is AND operation, we can propagate its operands into the
  // true branch. If it is OR operation, we can propagate them into the false
  // branch.
  unsigned PropagateOpcode =
      (BI->getSuccessor(0) == BB) ? Instruction::And : Instruction::Or;

  bool MadeChanges = false;
  SmallVector<Instruction *, 4> WorkList;
  SmallPtrSet<Instruction *, 4> Visited;
  WorkList.push_back(CondInst);
  while (!WorkList.empty()) {
    Instruction *Curr = WorkList.pop_back_val();

    AvailableValues.insert(Curr, TorF);
    LLVM_DEBUG(dbgs() << "EarlyCSE CVP: Add conditional value for '"
                      << Curr->getName() << "' as " << *TorF << " in "
                      << BB->getName() << "\n");
    if (!DebugCounter::shouldExecute(CSECounter)) {
      LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
    } else {
      // Replace all dominated uses with the known value.
      if (unsigned Count = replaceDominatedUsesWith(Curr, TorF, DT,
                                                    BasicBlockEdge(Pred, BB))) {
        NumCSECVP += Count;
        MadeChanges = true;
      }
    }

    Value *LHS, *RHS;
    if (MatchBinOp(Curr, PropagateOpcode, LHS, RHS))
      for (auto *Op : { LHS, RHS })
        if (Instruction *OPI = dyn_cast<Instruction>(Op))
          if (SimpleValue::canHandle(OPI) && Visited.insert(OPI).second)
            WorkList.push_back(OPI);
  }

  return MadeChanges;
}

Value *EarlyCSE::getMatchingValue(LoadValue &InVal, ParseMemoryInst &MemInst,
                                  unsigned CurrentGeneration) {
  if (InVal.DefInst == nullptr)
    return nullptr;
  if (InVal.MatchingId != MemInst.getMatchingId())
    return nullptr;
  // We don't yet handle removing loads with ordering of any kind.
  if (MemInst.isVolatile() || !MemInst.isUnordered())
    return nullptr;
  // We can't replace an atomic load with one which isn't also atomic.
  if (MemInst.isLoad() && !InVal.IsAtomic && MemInst.isAtomic())
    return nullptr;
  // The value V returned from this function is used differently depending
  // on whether MemInst is a load or a store. If it's a load, we will replace
  // MemInst with V, if it's a store, we will check if V is the same as the
  // available value.
  bool MemInstMatching = !MemInst.isLoad();
  Instruction *Matching = MemInstMatching ? MemInst.get() : InVal.DefInst;
  Instruction *Other = MemInstMatching ? InVal.DefInst : MemInst.get();

  // For stores check the result values before checking memory generation
  // (otherwise isSameMemGeneration may crash).
  Value *Result = MemInst.isStore()
                      ? getOrCreateResult(Matching, Other->getType())
                      : nullptr;
  if (MemInst.isStore() && InVal.DefInst != Result)
    return nullptr;

  // Deal with non-target memory intrinsics.
  bool MatchingNTI = isHandledNonTargetIntrinsic(Matching);
  bool OtherNTI = isHandledNonTargetIntrinsic(Other);
  if (OtherNTI != MatchingNTI)
    return nullptr;
  if (OtherNTI && MatchingNTI) {
    if (!isNonTargetIntrinsicMatch(cast<IntrinsicInst>(InVal.DefInst),
                                   cast<IntrinsicInst>(MemInst.get())))
      return nullptr;
  }

  if (!isOperatingOnInvariantMemAt(MemInst.get(), InVal.Generation) &&
      !isSameMemGeneration(InVal.Generation, CurrentGeneration, InVal.DefInst,
                           MemInst.get()))
    return nullptr;

  if (!Result)
    Result = getOrCreateResult(Matching, Other->getType());
  return Result;
}

static void combineIRFlags(Instruction &From, Value *To) {
  if (auto *I = dyn_cast<Instruction>(To)) {
    // If I being poison triggers UB, there is no need to drop those
    // flags. Otherwise, only retain flags present on both I and Inst.
    // TODO: Currently some fast-math flags are not treated as
    // poison-generating even though they should. Until this is fixed,
    // always retain flags present on both I and Inst for floating point
    // instructions.
    if (isa<FPMathOperator>(I) ||
        (I->hasPoisonGeneratingFlags() && !programUndefinedIfPoison(I)))
      I->andIRFlags(&From);
  }
}

bool EarlyCSE::overridingStores(const ParseMemoryInst &Earlier,
                                const ParseMemoryInst &Later) {
  // Can we remove Earlier store because of Later store?

  assert(Earlier.isUnordered() && !Earlier.isVolatile() &&
         "Violated invariant");
  if (Earlier.getPointerOperand() != Later.getPointerOperand())
    return false;
  if (!Earlier.getValueType() || !Later.getValueType() ||
      Earlier.getValueType() != Later.getValueType())
    return false;
  if (Earlier.getMatchingId() != Later.getMatchingId())
    return false;
  // At the moment, we don't remove ordered stores, but do remove
  // unordered atomic stores.  There's no special requirement (for
  // unordered atomics) about removing atomic stores only in favor of
  // other atomic stores since we were going to execute the non-atomic
  // one anyway and the atomic one might never have become visible.
  if (!Earlier.isUnordered() || !Later.isUnordered())
    return false;

  // Deal with non-target memory intrinsics.
  bool ENTI = isHandledNonTargetIntrinsic(Earlier.get());
  bool LNTI = isHandledNonTargetIntrinsic(Later.get());
  if (ENTI && LNTI)
    return isNonTargetIntrinsicMatch(cast<IntrinsicInst>(Earlier.get()),
                                     cast<IntrinsicInst>(Later.get()));

  // Because of the check above, at least one of them is false.
  // For now disallow matching intrinsics with non-intrinsics,
  // so assume that the stores match if neither is an intrinsic.
  return ENTI == LNTI;
}

bool EarlyCSE::processNode(DomTreeNode *Node) {
  bool Changed = false;
  BasicBlock *BB = Node->getBlock();

  // If this block has a single predecessor, then the predecessor is the parent
  // of the domtree node and all of the live out memory values are still current
  // in this block.  If this block has multiple predecessors, then they could
  // have invalidated the live-out memory values of our parent value.  For now,
  // just be conservative and invalidate memory if this block has multiple
  // predecessors.
  if (!BB->getSinglePredecessor())
    ++CurrentGeneration;

  // If this node has a single predecessor which ends in a conditional branch,
  // we can infer the value of the branch condition given that we took this
  // path.  We need the single predecessor to ensure there's not another path
  // which reaches this block where the condition might hold a different
  // value.  Since we're adding this to the scoped hash table (like any other
  // def), it will have been popped if we encounter a future merge block.
  if (BasicBlock *Pred = BB->getSinglePredecessor()) {
    auto *BI = dyn_cast<BranchInst>(Pred->getTerminator());
    if (BI && BI->isConditional()) {
      auto *CondInst = dyn_cast<Instruction>(BI->getCondition());
      if (CondInst && SimpleValue::canHandle(CondInst))
        Changed |= handleBranchCondition(CondInst, BI, BB, Pred);
    }
  }

  /// LastStore - Keep track of the last non-volatile store that we saw... for
  /// as long as there in no instruction that reads memory.  If we see a store
  /// to the same location, we delete the dead store.  This zaps trivial dead
  /// stores which can occur in bitfield code among other things.
  Instruction *LastStore = nullptr;

  // See if any instructions in the block can be eliminated.  If so, do it.  If
  // not, add them to AvailableValues.
  for (Instruction &Inst : make_early_inc_range(*BB)) {
    // Dead instructions should just be removed.
    if (isInstructionTriviallyDead(&Inst, &TLI)) {
      LLVM_DEBUG(dbgs() << "EarlyCSE DCE: " << Inst << '\n');
      if (!DebugCounter::shouldExecute(CSECounter)) {
        LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
        continue;
      }

      salvageKnowledge(&Inst, &AC);
      salvageDebugInfo(Inst);
      removeMSSA(Inst);
      Inst.eraseFromParent();
      Changed = true;
      ++NumSimplify;
      continue;
    }

    // Skip assume intrinsics, they don't really have side effects (although
    // they're marked as such to ensure preservation of control dependencies),
    // and this pass will not bother with its removal. However, we should mark
    // its condition as true for all dominated blocks.
    if (auto *Assume = dyn_cast<AssumeInst>(&Inst)) {
      auto *CondI = dyn_cast<Instruction>(Assume->getArgOperand(0));
      if (CondI && SimpleValue::canHandle(CondI)) {
        LLVM_DEBUG(dbgs() << "EarlyCSE considering assumption: " << Inst
                          << '\n');
        AvailableValues.insert(CondI, ConstantInt::getTrue(BB->getContext()));
      } else
        LLVM_DEBUG(dbgs() << "EarlyCSE skipping assumption: " << Inst << '\n');
      continue;
    }

    // Likewise, noalias intrinsics don't actually write.
    if (match(&Inst,
              m_Intrinsic<Intrinsic::experimental_noalias_scope_decl>())) {
      LLVM_DEBUG(dbgs() << "EarlyCSE skipping noalias intrinsic: " << Inst
                        << '\n');
      continue;
    }

    // Skip sideeffect intrinsics, for the same reason as assume intrinsics.
    if (match(&Inst, m_Intrinsic<Intrinsic::sideeffect>())) {
      LLVM_DEBUG(dbgs() << "EarlyCSE skipping sideeffect: " << Inst << '\n');
      continue;
    }

    // Skip pseudoprobe intrinsics, for the same reason as assume intrinsics.
    if (match(&Inst, m_Intrinsic<Intrinsic::pseudoprobe>())) {
      LLVM_DEBUG(dbgs() << "EarlyCSE skipping pseudoprobe: " << Inst << '\n');
      continue;
    }

    // We can skip all invariant.start intrinsics since they only read memory,
    // and we can forward values across it. For invariant starts without
    // invariant ends, we can use the fact that the invariantness never ends to
    // start a scope in the current generaton which is true for all future
    // generations.  Also, we dont need to consume the last store since the
    // semantics of invariant.start allow us to perform   DSE of the last
    // store, if there was a store following invariant.start. Consider:
    //
    // store 30, i8* p
    // invariant.start(p)
    // store 40, i8* p
    // We can DSE the store to 30, since the store 40 to invariant location p
    // causes undefined behaviour.
    if (match(&Inst, m_Intrinsic<Intrinsic::invariant_start>())) {
      // If there are any uses, the scope might end.
      if (!Inst.use_empty())
        continue;
      MemoryLocation MemLoc =
          MemoryLocation::getForArgument(&cast<CallInst>(Inst), 1, TLI);
      // Don't start a scope if we already have a better one pushed
      if (!AvailableInvariants.count(MemLoc))
        AvailableInvariants.insert(MemLoc, CurrentGeneration);
      continue;
    }

    if (isGuard(&Inst)) {
      if (auto *CondI =
              dyn_cast<Instruction>(cast<CallInst>(Inst).getArgOperand(0))) {
        if (SimpleValue::canHandle(CondI)) {
          // Do we already know the actual value of this condition?
          if (auto *KnownCond = AvailableValues.lookup(CondI)) {
            // Is the condition known to be true?
            if (isa<ConstantInt>(KnownCond) &&
                cast<ConstantInt>(KnownCond)->isOne()) {
              LLVM_DEBUG(dbgs()
                         << "EarlyCSE removing guard: " << Inst << '\n');
              salvageKnowledge(&Inst, &AC);
              removeMSSA(Inst);
              Inst.eraseFromParent();
              Changed = true;
              continue;
            } else
              // Use the known value if it wasn't true.
              cast<CallInst>(Inst).setArgOperand(0, KnownCond);
          }
          // The condition we're on guarding here is true for all dominated
          // locations.
          AvailableValues.insert(CondI, ConstantInt::getTrue(BB->getContext()));
        }
      }

      // Guard intrinsics read all memory, but don't write any memory.
      // Accordingly, don't update the generation but consume the last store (to
      // avoid an incorrect DSE).
      LastStore = nullptr;
      continue;
    }

    // If the instruction can be simplified (e.g. X+0 = X) then replace it with
    // its simpler value.
    if (Value *V = simplifyInstruction(&Inst, SQ)) {
      LLVM_DEBUG(dbgs() << "EarlyCSE Simplify: " << Inst << "  to: " << *V
                        << '\n');
      if (!DebugCounter::shouldExecute(CSECounter)) {
        LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
      } else {
        bool Killed = false;
        if (!Inst.use_empty()) {
          Inst.replaceAllUsesWith(V);
          Changed = true;
        }
        if (isInstructionTriviallyDead(&Inst, &TLI)) {
          salvageKnowledge(&Inst, &AC);
          removeMSSA(Inst);
          Inst.eraseFromParent();
          Changed = true;
          Killed = true;
        }
        if (Changed)
          ++NumSimplify;
        if (Killed)
          continue;
      }
    }

    // If this is a simple instruction that we can value number, process it.
    if (SimpleValue::canHandle(&Inst)) {
      if ([[maybe_unused]] auto *CI = dyn_cast<ConstrainedFPIntrinsic>(&Inst)) {
        assert(CI->getExceptionBehavior() != fp::ebStrict &&
               "Unexpected ebStrict from SimpleValue::canHandle()");
        assert((!CI->getRoundingMode() ||
                CI->getRoundingMode() != RoundingMode::Dynamic) &&
               "Unexpected dynamic rounding from SimpleValue::canHandle()");
      }
      // See if the instruction has an available value.  If so, use it.
      if (Value *V = AvailableValues.lookup(&Inst)) {
        LLVM_DEBUG(dbgs() << "EarlyCSE CSE: " << Inst << "  to: " << *V
                          << '\n');
        if (!DebugCounter::shouldExecute(CSECounter)) {
          LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
          continue;
        }
        combineIRFlags(Inst, V);
        Inst.replaceAllUsesWith(V);
        salvageKnowledge(&Inst, &AC);
        removeMSSA(Inst);
        Inst.eraseFromParent();
        Changed = true;
        ++NumCSE;
        continue;
      }

      // Otherwise, just remember that this value is available.
      AvailableValues.insert(&Inst, &Inst);
      continue;
    }

    ParseMemoryInst MemInst(&Inst, TTI);
    // If this is a non-volatile load, process it.
    if (MemInst.isValid() && MemInst.isLoad()) {
      // (conservatively) we can't peak past the ordering implied by this
      // operation, but we can add this load to our set of available values
      if (MemInst.isVolatile() || !MemInst.isUnordered()) {
        LastStore = nullptr;
        ++CurrentGeneration;
      }

      if (MemInst.isInvariantLoad()) {
        // If we pass an invariant load, we know that memory location is
        // indefinitely constant from the moment of first dereferenceability.
        // We conservatively treat the invariant_load as that moment.  If we
        // pass a invariant load after already establishing a scope, don't
        // restart it since we want to preserve the earliest point seen.
        auto MemLoc = MemoryLocation::get(&Inst);
        if (!AvailableInvariants.count(MemLoc))
          AvailableInvariants.insert(MemLoc, CurrentGeneration);
      }

      // If we have an available version of this load, and if it is the right
      // generation or the load is known to be from an invariant location,
      // replace this instruction.
      //
      // If either the dominating load or the current load are invariant, then
      // we can assume the current load loads the same value as the dominating
      // load.
      LoadValue InVal = AvailableLoads.lookup(MemInst.getPointerOperand());
      if (Value *Op = getMatchingValue(InVal, MemInst, CurrentGeneration)) {
        LLVM_DEBUG(dbgs() << "EarlyCSE CSE LOAD: " << Inst
                          << "  to: " << *InVal.DefInst << '\n');
        if (!DebugCounter::shouldExecute(CSECounter)) {
          LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
          continue;
        }
        if (InVal.IsLoad)
          if (auto *I = dyn_cast<Instruction>(Op))
            combineMetadataForCSE(I, &Inst, false);
        if (!Inst.use_empty())
          Inst.replaceAllUsesWith(Op);
        salvageKnowledge(&Inst, &AC);
        removeMSSA(Inst);
        Inst.eraseFromParent();
        Changed = true;
        ++NumCSELoad;
        continue;
      }

      // Otherwise, remember that we have this instruction.
      AvailableLoads.insert(MemInst.getPointerOperand(),
                            LoadValue(&Inst, CurrentGeneration,
                                      MemInst.getMatchingId(),
                                      MemInst.isAtomic(),
                                      MemInst.isLoad()));
      LastStore = nullptr;
      continue;
    }

    // If this instruction may read from memory or throw (and potentially read
    // from memory in the exception handler), forget LastStore.  Load/store
    // intrinsics will indicate both a read and a write to memory.  The target
    // may override this (e.g. so that a store intrinsic does not read from
    // memory, and thus will be treated the same as a regular store for
    // commoning purposes).
    if ((Inst.mayReadFromMemory() || Inst.mayThrow()) &&
        !(MemInst.isValid() && !MemInst.mayReadFromMemory()))
      LastStore = nullptr;

    // If this is a read-only call, process it.
    if (CallValue::canHandle(&Inst)) {
      // If we have an available version of this call, and if it is the right
      // generation, replace this instruction.
      std::pair<Instruction *, unsigned> InVal = AvailableCalls.lookup(&Inst);
      if (InVal.first != nullptr &&
          isSameMemGeneration(InVal.second, CurrentGeneration, InVal.first,
                              &Inst)) {
        LLVM_DEBUG(dbgs() << "EarlyCSE CSE CALL: " << Inst
                          << "  to: " << *InVal.first << '\n');
        if (!DebugCounter::shouldExecute(CSECounter)) {
          LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
          continue;
        }
        if (!Inst.use_empty())
          Inst.replaceAllUsesWith(InVal.first);
        salvageKnowledge(&Inst, &AC);
        removeMSSA(Inst);
        Inst.eraseFromParent();
        Changed = true;
        ++NumCSECall;
        continue;
      }

      // Otherwise, remember that we have this instruction.
      AvailableCalls.insert(&Inst, std::make_pair(&Inst, CurrentGeneration));
      continue;
    }

    // Compare GEP instructions based on offset.
    if (GEPValue::canHandle(&Inst)) {
      auto *GEP = cast<GetElementPtrInst>(&Inst);
      APInt Offset = APInt(SQ.DL.getIndexTypeSizeInBits(GEP->getType()), 0);
      GEPValue GEPVal(GEP, GEP->accumulateConstantOffset(SQ.DL, Offset)
                               ? Offset.trySExtValue()
                               : std::nullopt);
      if (Value *V = AvailableGEPs.lookup(GEPVal)) {
        LLVM_DEBUG(dbgs() << "EarlyCSE CSE GEP: " << Inst << "  to: " << *V
                          << '\n');
        combineIRFlags(Inst, V);
        Inst.replaceAllUsesWith(V);
        salvageKnowledge(&Inst, &AC);
        removeMSSA(Inst);
        Inst.eraseFromParent();
        Changed = true;
        ++NumCSEGEP;
        continue;
      }

      // Otherwise, just remember that we have this GEP.
      AvailableGEPs.insert(GEPVal, &Inst);
      continue;
    }

    // A release fence requires that all stores complete before it, but does
    // not prevent the reordering of following loads 'before' the fence.  As a
    // result, we don't need to consider it as writing to memory and don't need
    // to advance the generation.  We do need to prevent DSE across the fence,
    // but that's handled above.
    if (auto *FI = dyn_cast<FenceInst>(&Inst))
      if (FI->getOrdering() == AtomicOrdering::Release) {
        assert(Inst.mayReadFromMemory() && "relied on to prevent DSE above");
        continue;
      }

    // write back DSE - If we write back the same value we just loaded from
    // the same location and haven't passed any intervening writes or ordering
    // operations, we can remove the write.  The primary benefit is in allowing
    // the available load table to remain valid and value forward past where
    // the store originally was.
    if (MemInst.isValid() && MemInst.isStore()) {
      LoadValue InVal = AvailableLoads.lookup(MemInst.getPointerOperand());
      if (InVal.DefInst &&
          InVal.DefInst == getMatchingValue(InVal, MemInst, CurrentGeneration)) {
        // It is okay to have a LastStore to a different pointer here if MemorySSA
        // tells us that the load and store are from the same memory generation.
        // In that case, LastStore should keep its present value since we're
        // removing the current store.
        assert((!LastStore ||
                ParseMemoryInst(LastStore, TTI).getPointerOperand() ==
                    MemInst.getPointerOperand() ||
                MSSA) &&
               "can't have an intervening store if not using MemorySSA!");
        LLVM_DEBUG(dbgs() << "EarlyCSE DSE (writeback): " << Inst << '\n');
        if (!DebugCounter::shouldExecute(CSECounter)) {
          LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
          continue;
        }
        salvageKnowledge(&Inst, &AC);
        removeMSSA(Inst);
        Inst.eraseFromParent();
        Changed = true;
        ++NumDSE;
        // We can avoid incrementing the generation count since we were able
        // to eliminate this store.
        continue;
      }
    }

    // Okay, this isn't something we can CSE at all.  Check to see if it is
    // something that could modify memory.  If so, our available memory values
    // cannot be used so bump the generation count.
    if (Inst.mayWriteToMemory()) {
      ++CurrentGeneration;

      if (MemInst.isValid() && MemInst.isStore()) {
        // We do a trivial form of DSE if there are two stores to the same
        // location with no intervening loads.  Delete the earlier store.
        if (LastStore) {
          if (overridingStores(ParseMemoryInst(LastStore, TTI), MemInst)) {
            LLVM_DEBUG(dbgs() << "EarlyCSE DEAD STORE: " << *LastStore
                              << "  due to: " << Inst << '\n');
            if (!DebugCounter::shouldExecute(CSECounter)) {
              LLVM_DEBUG(dbgs() << "Skipping due to debug counter\n");
            } else {
              salvageKnowledge(&Inst, &AC);
              removeMSSA(*LastStore);
              LastStore->eraseFromParent();
              Changed = true;
              ++NumDSE;
              LastStore = nullptr;
            }
          }
          // fallthrough - we can exploit information about this store
        }

        // Okay, we just invalidated anything we knew about loaded values.  Try
        // to salvage *something* by remembering that the stored value is a live
        // version of the pointer.  It is safe to forward from volatile stores
        // to non-volatile loads, so we don't have to check for volatility of
        // the store.
        AvailableLoads.insert(MemInst.getPointerOperand(),
                              LoadValue(&Inst, CurrentGeneration,
                                        MemInst.getMatchingId(),
                                        MemInst.isAtomic(),
                                        MemInst.isLoad()));

        // Remember that this was the last unordered store we saw for DSE. We
        // don't yet handle DSE on ordered or volatile stores since we don't
        // have a good way to model the ordering requirement for following
        // passes  once the store is removed.  We could insert a fence, but
        // since fences are slightly stronger than stores in their ordering,
        // it's not clear this is a profitable transform. Another option would
        // be to merge the ordering with that of the post dominating store.
        if (MemInst.isUnordered() && !MemInst.isVolatile())
          LastStore = &Inst;
        else
          LastStore = nullptr;
      }
    }
  }

  return Changed;
}

bool EarlyCSE::run() {
  // Note, deque is being used here because there is significant performance
  // gains over vector when the container becomes very large due to the
  // specific access patterns. For more information see the mailing list
  // discussion on this:
  // http://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20120116/135228.html
  std::deque<StackNode *> nodesToProcess;

  bool Changed = false;

  // Process the root node.
  nodesToProcess.push_back(new StackNode(
      AvailableValues, AvailableLoads, AvailableInvariants, AvailableCalls,
      AvailableGEPs, CurrentGeneration, DT.getRootNode(),
      DT.getRootNode()->begin(), DT.getRootNode()->end()));

  assert(!CurrentGeneration && "Create a new EarlyCSE instance to rerun it.");

  // Process the stack.
  while (!nodesToProcess.empty()) {
    // Grab the first item off the stack. Set the current generation, remove
    // the node from the stack, and process it.
    StackNode *NodeToProcess = nodesToProcess.back();

    // Initialize class members.
    CurrentGeneration = NodeToProcess->currentGeneration();

    // Check if the node needs to be processed.
    if (!NodeToProcess->isProcessed()) {
      // Process the node.
      Changed |= processNode(NodeToProcess->node());
      NodeToProcess->childGeneration(CurrentGeneration);
      NodeToProcess->process();
    } else if (NodeToProcess->childIter() != NodeToProcess->end()) {
      // Push the next child onto the stack.
      DomTreeNode *child = NodeToProcess->nextChild();
      nodesToProcess.push_back(new StackNode(
          AvailableValues, AvailableLoads, AvailableInvariants, AvailableCalls,
          AvailableGEPs, NodeToProcess->childGeneration(), child,
          child->begin(), child->end()));
    } else {
      // It has been processed, and there are no more children to process,
      // so delete it and pop it off the stack.
      delete NodeToProcess;
      nodesToProcess.pop_back();
    }
  } // while (!nodes...)

  return Changed;
}

PreservedAnalyses EarlyCSEPass::run(Function &F,
                                    FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto *MSSA =
      UseMemorySSA ? &AM.getResult<MemorySSAAnalysis>(F).getMSSA() : nullptr;

  EarlyCSE CSE(F.getDataLayout(), TLI, TTI, DT, AC, MSSA);

  if (!CSE.run())
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  if (UseMemorySSA)
    PA.preserve<MemorySSAAnalysis>();
  return PA;
}

void EarlyCSEPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<EarlyCSEPass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  OS << '<';
  if (UseMemorySSA)
    OS << "memssa";
  OS << '>';
}

namespace {

/// A simple and fast domtree-based CSE pass.
///
/// This pass does a simple depth-first walk over the dominator tree,
/// eliminating trivially redundant instructions and using instsimplify to
/// canonicalize things as it goes. It is intended to be fast and catch obvious
/// cases so that instcombine and other passes are more effective. It is
/// expected that a later pass of GVN will catch the interesting/hard cases.
template<bool UseMemorySSA>
class EarlyCSELegacyCommonPass : public FunctionPass {
public:
  static char ID;

  EarlyCSELegacyCommonPass() : FunctionPass(ID) {
    if (UseMemorySSA)
      initializeEarlyCSEMemSSALegacyPassPass(*PassRegistry::getPassRegistry());
    else
      initializeEarlyCSELegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
    auto &TTI = getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    auto &AC = getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    auto *MSSA =
        UseMemorySSA ? &getAnalysis<MemorySSAWrapperPass>().getMSSA() : nullptr;

    EarlyCSE CSE(F.getDataLayout(), TLI, TTI, DT, AC, MSSA);

    return CSE.run();
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    if (UseMemorySSA) {
      AU.addRequired<AAResultsWrapperPass>();
      AU.addRequired<MemorySSAWrapperPass>();
      AU.addPreserved<MemorySSAWrapperPass>();
    }
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<AAResultsWrapperPass>();
    AU.setPreservesCFG();
  }
};

} // end anonymous namespace

using EarlyCSELegacyPass = EarlyCSELegacyCommonPass</*UseMemorySSA=*/false>;

template<>
char EarlyCSELegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(EarlyCSELegacyPass, "early-cse", "Early CSE", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(EarlyCSELegacyPass, "early-cse", "Early CSE", false, false)

using EarlyCSEMemSSALegacyPass =
    EarlyCSELegacyCommonPass</*UseMemorySSA=*/true>;

template<>
char EarlyCSEMemSSALegacyPass::ID = 0;

FunctionPass *llvm::createEarlyCSEPass(bool UseMemorySSA) {
  if (UseMemorySSA)
    return new EarlyCSEMemSSALegacyPass();
  else
    return new EarlyCSELegacyPass();
}

INITIALIZE_PASS_BEGIN(EarlyCSEMemSSALegacyPass, "early-cse-memssa",
                      "Early CSE w/ MemorySSA", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MemorySSAWrapperPass)
INITIALIZE_PASS_END(EarlyCSEMemSSALegacyPass, "early-cse-memssa",
                    "Early CSE w/ MemorySSA", false, false)
