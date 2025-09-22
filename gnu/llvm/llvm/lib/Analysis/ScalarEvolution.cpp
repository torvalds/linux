//===- ScalarEvolution.cpp - Scalar Evolution Analysis --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the scalar evolution analysis
// engine, which is used primarily to analyze expressions involving induction
// variables in loops.
//
// There are several aspects to this library.  First is the representation of
// scalar expressions, which are represented as subclasses of the SCEV class.
// These classes are used to represent certain types of subexpressions that we
// can handle. We only create one SCEV of a particular shape, so
// pointer-comparisons for equality are legal.
//
// One important aspect of the SCEV objects is that they are never cyclic, even
// if there is a cycle in the dataflow for an expression (ie, a PHI node).  If
// the PHI node is one of the idioms that we can represent (e.g., a polynomial
// recurrence) then we represent it directly as a recurrence node, otherwise we
// represent it as a SCEVUnknown node.
//
// In addition to being able to represent expressions of various types, we also
// have folders that are used to build the *canonical* representation for a
// particular expression.  These folders are capable of using a variety of
// rewrite rules to simplify the expressions.
//
// Once the folders are defined, we can implement the more interesting
// higher-level code, such as the code that recognizes PHI nodes of various
// types, computes the execution count of a loop, etc.
//
// TODO: We should use these routines and value representations to implement
// dependence analysis!
//
//===----------------------------------------------------------------------===//
//
// There are several good references for the techniques used in this analysis.
//
//  Chains of recurrences -- a method to expedite the evaluation
//  of closed-form functions
//  Olaf Bachmann, Paul S. Wang, Eugene V. Zima
//
//  On computational properties of chains of recurrences
//  Eugene V. Zima
//
//  Symbolic Evaluation of Chains of Recurrences for Loop Optimization
//  Robert A. van Engelen
//
//  Efficient Symbolic Analysis for Optimizing Compilers
//  Robert A. van Engelen
//
//  Using the chains of recurrences algebra for data dependence testing and
//  induction variable substitution
//  MS Thesis, Johnie Birch
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "scalar-evolution"

STATISTIC(NumExitCountsComputed,
          "Number of loop exits with predictable exit counts");
STATISTIC(NumExitCountsNotComputed,
          "Number of loop exits without predictable exit counts");
STATISTIC(NumBruteForceTripCountsComputed,
          "Number of loops with trip counts computed by force");

#ifdef EXPENSIVE_CHECKS
bool llvm::VerifySCEV = true;
#else
bool llvm::VerifySCEV = false;
#endif

static cl::opt<unsigned>
    MaxBruteForceIterations("scalar-evolution-max-iterations", cl::ReallyHidden,
                            cl::desc("Maximum number of iterations SCEV will "
                                     "symbolically execute a constant "
                                     "derived loop"),
                            cl::init(100));

static cl::opt<bool, true> VerifySCEVOpt(
    "verify-scev", cl::Hidden, cl::location(VerifySCEV),
    cl::desc("Verify ScalarEvolution's backedge taken counts (slow)"));
static cl::opt<bool> VerifySCEVStrict(
    "verify-scev-strict", cl::Hidden,
    cl::desc("Enable stricter verification with -verify-scev is passed"));

static cl::opt<bool> VerifyIR(
    "scev-verify-ir", cl::Hidden,
    cl::desc("Verify IR correctness when making sensitive SCEV queries (slow)"),
    cl::init(false));

static cl::opt<unsigned> MulOpsInlineThreshold(
    "scev-mulops-inline-threshold", cl::Hidden,
    cl::desc("Threshold for inlining multiplication operands into a SCEV"),
    cl::init(32));

static cl::opt<unsigned> AddOpsInlineThreshold(
    "scev-addops-inline-threshold", cl::Hidden,
    cl::desc("Threshold for inlining addition operands into a SCEV"),
    cl::init(500));

static cl::opt<unsigned> MaxSCEVCompareDepth(
    "scalar-evolution-max-scev-compare-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive SCEV complexity comparisons"),
    cl::init(32));

static cl::opt<unsigned> MaxSCEVOperationsImplicationDepth(
    "scalar-evolution-max-scev-operations-implication-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive SCEV operations implication analysis"),
    cl::init(2));

static cl::opt<unsigned> MaxValueCompareDepth(
    "scalar-evolution-max-value-compare-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive value complexity comparisons"),
    cl::init(2));

static cl::opt<unsigned>
    MaxArithDepth("scalar-evolution-max-arith-depth", cl::Hidden,
                  cl::desc("Maximum depth of recursive arithmetics"),
                  cl::init(32));

static cl::opt<unsigned> MaxConstantEvolvingDepth(
    "scalar-evolution-max-constant-evolving-depth", cl::Hidden,
    cl::desc("Maximum depth of recursive constant evolving"), cl::init(32));

static cl::opt<unsigned>
    MaxCastDepth("scalar-evolution-max-cast-depth", cl::Hidden,
                 cl::desc("Maximum depth of recursive SExt/ZExt/Trunc"),
                 cl::init(8));

static cl::opt<unsigned>
    MaxAddRecSize("scalar-evolution-max-add-rec-size", cl::Hidden,
                  cl::desc("Max coefficients in AddRec during evolving"),
                  cl::init(8));

static cl::opt<unsigned>
    HugeExprThreshold("scalar-evolution-huge-expr-threshold", cl::Hidden,
                  cl::desc("Size of the expression which is considered huge"),
                  cl::init(4096));

static cl::opt<unsigned> RangeIterThreshold(
    "scev-range-iter-threshold", cl::Hidden,
    cl::desc("Threshold for switching to iteratively computing SCEV ranges"),
    cl::init(32));

static cl::opt<bool>
ClassifyExpressions("scalar-evolution-classify-expressions",
    cl::Hidden, cl::init(true),
    cl::desc("When printing analysis, include information on every instruction"));

static cl::opt<bool> UseExpensiveRangeSharpening(
    "scalar-evolution-use-expensive-range-sharpening", cl::Hidden,
    cl::init(false),
    cl::desc("Use more powerful methods of sharpening expression ranges. May "
             "be costly in terms of compile time"));

static cl::opt<unsigned> MaxPhiSCCAnalysisSize(
    "scalar-evolution-max-scc-analysis-depth", cl::Hidden,
    cl::desc("Maximum amount of nodes to process while searching SCEVUnknown "
             "Phi strongly connected components"),
    cl::init(8));

static cl::opt<bool>
    EnableFiniteLoopControl("scalar-evolution-finite-loop", cl::Hidden,
                            cl::desc("Handle <= and >= in finite loops"),
                            cl::init(true));

static cl::opt<bool> UseContextForNoWrapFlagInference(
    "scalar-evolution-use-context-for-no-wrap-flag-strenghening", cl::Hidden,
    cl::desc("Infer nuw/nsw flags using context where suitable"),
    cl::init(true));

//===----------------------------------------------------------------------===//
//                           SCEV class definitions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Implementation of the SCEV class.
//

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SCEV::dump() const {
  print(dbgs());
  dbgs() << '\n';
}
#endif

void SCEV::print(raw_ostream &OS) const {
  switch (getSCEVType()) {
  case scConstant:
    cast<SCEVConstant>(this)->getValue()->printAsOperand(OS, false);
    return;
  case scVScale:
    OS << "vscale";
    return;
  case scPtrToInt: {
    const SCEVPtrToIntExpr *PtrToInt = cast<SCEVPtrToIntExpr>(this);
    const SCEV *Op = PtrToInt->getOperand();
    OS << "(ptrtoint " << *Op->getType() << " " << *Op << " to "
       << *PtrToInt->getType() << ")";
    return;
  }
  case scTruncate: {
    const SCEVTruncateExpr *Trunc = cast<SCEVTruncateExpr>(this);
    const SCEV *Op = Trunc->getOperand();
    OS << "(trunc " << *Op->getType() << " " << *Op << " to "
       << *Trunc->getType() << ")";
    return;
  }
  case scZeroExtend: {
    const SCEVZeroExtendExpr *ZExt = cast<SCEVZeroExtendExpr>(this);
    const SCEV *Op = ZExt->getOperand();
    OS << "(zext " << *Op->getType() << " " << *Op << " to "
       << *ZExt->getType() << ")";
    return;
  }
  case scSignExtend: {
    const SCEVSignExtendExpr *SExt = cast<SCEVSignExtendExpr>(this);
    const SCEV *Op = SExt->getOperand();
    OS << "(sext " << *Op->getType() << " " << *Op << " to "
       << *SExt->getType() << ")";
    return;
  }
  case scAddRecExpr: {
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(this);
    OS << "{" << *AR->getOperand(0);
    for (unsigned i = 1, e = AR->getNumOperands(); i != e; ++i)
      OS << ",+," << *AR->getOperand(i);
    OS << "}<";
    if (AR->hasNoUnsignedWrap())
      OS << "nuw><";
    if (AR->hasNoSignedWrap())
      OS << "nsw><";
    if (AR->hasNoSelfWrap() &&
        !AR->getNoWrapFlags((NoWrapFlags)(FlagNUW | FlagNSW)))
      OS << "nw><";
    AR->getLoop()->getHeader()->printAsOperand(OS, /*PrintType=*/false);
    OS << ">";
    return;
  }
  case scAddExpr:
  case scMulExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr: {
    const SCEVNAryExpr *NAry = cast<SCEVNAryExpr>(this);
    const char *OpStr = nullptr;
    switch (NAry->getSCEVType()) {
    case scAddExpr: OpStr = " + "; break;
    case scMulExpr: OpStr = " * "; break;
    case scUMaxExpr: OpStr = " umax "; break;
    case scSMaxExpr: OpStr = " smax "; break;
    case scUMinExpr:
      OpStr = " umin ";
      break;
    case scSMinExpr:
      OpStr = " smin ";
      break;
    case scSequentialUMinExpr:
      OpStr = " umin_seq ";
      break;
    default:
      llvm_unreachable("There are no other nary expression types.");
    }
    OS << "(";
    ListSeparator LS(OpStr);
    for (const SCEV *Op : NAry->operands())
      OS << LS << *Op;
    OS << ")";
    switch (NAry->getSCEVType()) {
    case scAddExpr:
    case scMulExpr:
      if (NAry->hasNoUnsignedWrap())
        OS << "<nuw>";
      if (NAry->hasNoSignedWrap())
        OS << "<nsw>";
      break;
    default:
      // Nothing to print for other nary expressions.
      break;
    }
    return;
  }
  case scUDivExpr: {
    const SCEVUDivExpr *UDiv = cast<SCEVUDivExpr>(this);
    OS << "(" << *UDiv->getLHS() << " /u " << *UDiv->getRHS() << ")";
    return;
  }
  case scUnknown:
    cast<SCEVUnknown>(this)->getValue()->printAsOperand(OS, false);
    return;
  case scCouldNotCompute:
    OS << "***COULDNOTCOMPUTE***";
    return;
  }
  llvm_unreachable("Unknown SCEV kind!");
}

Type *SCEV::getType() const {
  switch (getSCEVType()) {
  case scConstant:
    return cast<SCEVConstant>(this)->getType();
  case scVScale:
    return cast<SCEVVScale>(this)->getType();
  case scPtrToInt:
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
    return cast<SCEVCastExpr>(this)->getType();
  case scAddRecExpr:
    return cast<SCEVAddRecExpr>(this)->getType();
  case scMulExpr:
    return cast<SCEVMulExpr>(this)->getType();
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
    return cast<SCEVMinMaxExpr>(this)->getType();
  case scSequentialUMinExpr:
    return cast<SCEVSequentialMinMaxExpr>(this)->getType();
  case scAddExpr:
    return cast<SCEVAddExpr>(this)->getType();
  case scUDivExpr:
    return cast<SCEVUDivExpr>(this)->getType();
  case scUnknown:
    return cast<SCEVUnknown>(this)->getType();
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

ArrayRef<const SCEV *> SCEV::operands() const {
  switch (getSCEVType()) {
  case scConstant:
  case scVScale:
  case scUnknown:
    return {};
  case scPtrToInt:
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
    return cast<SCEVCastExpr>(this)->operands();
  case scAddRecExpr:
  case scAddExpr:
  case scMulExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr:
    return cast<SCEVNAryExpr>(this)->operands();
  case scUDivExpr:
    return cast<SCEVUDivExpr>(this)->operands();
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

bool SCEV::isZero() const {
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(this))
    return SC->getValue()->isZero();
  return false;
}

bool SCEV::isOne() const {
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(this))
    return SC->getValue()->isOne();
  return false;
}

bool SCEV::isAllOnesValue() const {
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(this))
    return SC->getValue()->isMinusOne();
  return false;
}

bool SCEV::isNonConstantNegative() const {
  const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(this);
  if (!Mul) return false;

  // If there is a constant factor, it will be first.
  const SCEVConstant *SC = dyn_cast<SCEVConstant>(Mul->getOperand(0));
  if (!SC) return false;

  // Return true if the value is negative, this matches things like (-42 * V).
  return SC->getAPInt().isNegative();
}

SCEVCouldNotCompute::SCEVCouldNotCompute() :
  SCEV(FoldingSetNodeIDRef(), scCouldNotCompute, 0) {}

bool SCEVCouldNotCompute::classof(const SCEV *S) {
  return S->getSCEVType() == scCouldNotCompute;
}

const SCEV *ScalarEvolution::getConstant(ConstantInt *V) {
  FoldingSetNodeID ID;
  ID.AddInteger(scConstant);
  ID.AddPointer(V);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVConstant(ID.Intern(SCEVAllocator), V);
  UniqueSCEVs.InsertNode(S, IP);
  return S;
}

const SCEV *ScalarEvolution::getConstant(const APInt &Val) {
  return getConstant(ConstantInt::get(getContext(), Val));
}

const SCEV *
ScalarEvolution::getConstant(Type *Ty, uint64_t V, bool isSigned) {
  IntegerType *ITy = cast<IntegerType>(getEffectiveSCEVType(Ty));
  return getConstant(ConstantInt::get(ITy, V, isSigned));
}

const SCEV *ScalarEvolution::getVScale(Type *Ty) {
  FoldingSetNodeID ID;
  ID.AddInteger(scVScale);
  ID.AddPointer(Ty);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP))
    return S;
  SCEV *S = new (SCEVAllocator) SCEVVScale(ID.Intern(SCEVAllocator), Ty);
  UniqueSCEVs.InsertNode(S, IP);
  return S;
}

const SCEV *ScalarEvolution::getElementCount(Type *Ty, ElementCount EC) {
  const SCEV *Res = getConstant(Ty, EC.getKnownMinValue());
  if (EC.isScalable())
    Res = getMulExpr(Res, getVScale(Ty));
  return Res;
}

SCEVCastExpr::SCEVCastExpr(const FoldingSetNodeIDRef ID, SCEVTypes SCEVTy,
                           const SCEV *op, Type *ty)
    : SCEV(ID, SCEVTy, computeExpressionSize(op)), Op(op), Ty(ty) {}

SCEVPtrToIntExpr::SCEVPtrToIntExpr(const FoldingSetNodeIDRef ID, const SCEV *Op,
                                   Type *ITy)
    : SCEVCastExpr(ID, scPtrToInt, Op, ITy) {
  assert(getOperand()->getType()->isPointerTy() && Ty->isIntegerTy() &&
         "Must be a non-bit-width-changing pointer-to-integer cast!");
}

SCEVIntegralCastExpr::SCEVIntegralCastExpr(const FoldingSetNodeIDRef ID,
                                           SCEVTypes SCEVTy, const SCEV *op,
                                           Type *ty)
    : SCEVCastExpr(ID, SCEVTy, op, ty) {}

SCEVTruncateExpr::SCEVTruncateExpr(const FoldingSetNodeIDRef ID, const SCEV *op,
                                   Type *ty)
    : SCEVIntegralCastExpr(ID, scTruncate, op, ty) {
  assert(getOperand()->getType()->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate non-integer value!");
}

SCEVZeroExtendExpr::SCEVZeroExtendExpr(const FoldingSetNodeIDRef ID,
                                       const SCEV *op, Type *ty)
    : SCEVIntegralCastExpr(ID, scZeroExtend, op, ty) {
  assert(getOperand()->getType()->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot zero extend non-integer value!");
}

SCEVSignExtendExpr::SCEVSignExtendExpr(const FoldingSetNodeIDRef ID,
                                       const SCEV *op, Type *ty)
    : SCEVIntegralCastExpr(ID, scSignExtend, op, ty) {
  assert(getOperand()->getType()->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot sign extend non-integer value!");
}

void SCEVUnknown::deleted() {
  // Clear this SCEVUnknown from various maps.
  SE->forgetMemoizedResults(this);

  // Remove this SCEVUnknown from the uniquing map.
  SE->UniqueSCEVs.RemoveNode(this);

  // Release the value.
  setValPtr(nullptr);
}

void SCEVUnknown::allUsesReplacedWith(Value *New) {
  // Clear this SCEVUnknown from various maps.
  SE->forgetMemoizedResults(this);

  // Remove this SCEVUnknown from the uniquing map.
  SE->UniqueSCEVs.RemoveNode(this);

  // Replace the value pointer in case someone is still using this SCEVUnknown.
  setValPtr(New);
}

//===----------------------------------------------------------------------===//
//                               SCEV Utilities
//===----------------------------------------------------------------------===//

/// Compare the two values \p LV and \p RV in terms of their "complexity" where
/// "complexity" is a partial (and somewhat ad-hoc) relation used to order
/// operands in SCEV expressions.  \p EqCache is a set of pairs of values that
/// have been previously deemed to be "equally complex" by this routine.  It is
/// intended to avoid exponential time complexity in cases like:
///
///   %a = f(%x, %y)
///   %b = f(%a, %a)
///   %c = f(%b, %b)
///
///   %d = f(%x, %y)
///   %e = f(%d, %d)
///   %f = f(%e, %e)
///
///   CompareValueComplexity(%f, %c)
///
/// Since we do not continue running this routine on expression trees once we
/// have seen unequal values, there is no need to track them in the cache.
static int
CompareValueComplexity(EquivalenceClasses<const Value *> &EqCacheValue,
                       const LoopInfo *const LI, Value *LV, Value *RV,
                       unsigned Depth) {
  if (Depth > MaxValueCompareDepth || EqCacheValue.isEquivalent(LV, RV))
    return 0;

  // Order pointer values after integer values. This helps SCEVExpander form
  // GEPs.
  bool LIsPointer = LV->getType()->isPointerTy(),
       RIsPointer = RV->getType()->isPointerTy();
  if (LIsPointer != RIsPointer)
    return (int)LIsPointer - (int)RIsPointer;

  // Compare getValueID values.
  unsigned LID = LV->getValueID(), RID = RV->getValueID();
  if (LID != RID)
    return (int)LID - (int)RID;

  // Sort arguments by their position.
  if (const auto *LA = dyn_cast<Argument>(LV)) {
    const auto *RA = cast<Argument>(RV);
    unsigned LArgNo = LA->getArgNo(), RArgNo = RA->getArgNo();
    return (int)LArgNo - (int)RArgNo;
  }

  if (const auto *LGV = dyn_cast<GlobalValue>(LV)) {
    const auto *RGV = cast<GlobalValue>(RV);

    const auto IsGVNameSemantic = [&](const GlobalValue *GV) {
      auto LT = GV->getLinkage();
      return !(GlobalValue::isPrivateLinkage(LT) ||
               GlobalValue::isInternalLinkage(LT));
    };

    // Use the names to distinguish the two values, but only if the
    // names are semantically important.
    if (IsGVNameSemantic(LGV) && IsGVNameSemantic(RGV))
      return LGV->getName().compare(RGV->getName());
  }

  // For instructions, compare their loop depth, and their operand count.  This
  // is pretty loose.
  if (const auto *LInst = dyn_cast<Instruction>(LV)) {
    const auto *RInst = cast<Instruction>(RV);

    // Compare loop depths.
    const BasicBlock *LParent = LInst->getParent(),
                     *RParent = RInst->getParent();
    if (LParent != RParent) {
      unsigned LDepth = LI->getLoopDepth(LParent),
               RDepth = LI->getLoopDepth(RParent);
      if (LDepth != RDepth)
        return (int)LDepth - (int)RDepth;
    }

    // Compare the number of operands.
    unsigned LNumOps = LInst->getNumOperands(),
             RNumOps = RInst->getNumOperands();
    if (LNumOps != RNumOps)
      return (int)LNumOps - (int)RNumOps;

    for (unsigned Idx : seq(LNumOps)) {
      int Result =
          CompareValueComplexity(EqCacheValue, LI, LInst->getOperand(Idx),
                                 RInst->getOperand(Idx), Depth + 1);
      if (Result != 0)
        return Result;
    }
  }

  EqCacheValue.unionSets(LV, RV);
  return 0;
}

// Return negative, zero, or positive, if LHS is less than, equal to, or greater
// than RHS, respectively. A three-way result allows recursive comparisons to be
// more efficient.
// If the max analysis depth was reached, return std::nullopt, assuming we do
// not know if they are equivalent for sure.
static std::optional<int>
CompareSCEVComplexity(EquivalenceClasses<const SCEV *> &EqCacheSCEV,
                      EquivalenceClasses<const Value *> &EqCacheValue,
                      const LoopInfo *const LI, const SCEV *LHS,
                      const SCEV *RHS, DominatorTree &DT, unsigned Depth = 0) {
  // Fast-path: SCEVs are uniqued so we can do a quick equality check.
  if (LHS == RHS)
    return 0;

  // Primarily, sort the SCEVs by their getSCEVType().
  SCEVTypes LType = LHS->getSCEVType(), RType = RHS->getSCEVType();
  if (LType != RType)
    return (int)LType - (int)RType;

  if (EqCacheSCEV.isEquivalent(LHS, RHS))
    return 0;

  if (Depth > MaxSCEVCompareDepth)
    return std::nullopt;

  // Aside from the getSCEVType() ordering, the particular ordering
  // isn't very important except that it's beneficial to be consistent,
  // so that (a + b) and (b + a) don't end up as different expressions.
  switch (LType) {
  case scUnknown: {
    const SCEVUnknown *LU = cast<SCEVUnknown>(LHS);
    const SCEVUnknown *RU = cast<SCEVUnknown>(RHS);

    int X = CompareValueComplexity(EqCacheValue, LI, LU->getValue(),
                                   RU->getValue(), Depth + 1);
    if (X == 0)
      EqCacheSCEV.unionSets(LHS, RHS);
    return X;
  }

  case scConstant: {
    const SCEVConstant *LC = cast<SCEVConstant>(LHS);
    const SCEVConstant *RC = cast<SCEVConstant>(RHS);

    // Compare constant values.
    const APInt &LA = LC->getAPInt();
    const APInt &RA = RC->getAPInt();
    unsigned LBitWidth = LA.getBitWidth(), RBitWidth = RA.getBitWidth();
    if (LBitWidth != RBitWidth)
      return (int)LBitWidth - (int)RBitWidth;
    return LA.ult(RA) ? -1 : 1;
  }

  case scVScale: {
    const auto *LTy = cast<IntegerType>(cast<SCEVVScale>(LHS)->getType());
    const auto *RTy = cast<IntegerType>(cast<SCEVVScale>(RHS)->getType());
    return LTy->getBitWidth() - RTy->getBitWidth();
  }

  case scAddRecExpr: {
    const SCEVAddRecExpr *LA = cast<SCEVAddRecExpr>(LHS);
    const SCEVAddRecExpr *RA = cast<SCEVAddRecExpr>(RHS);

    // There is always a dominance between two recs that are used by one SCEV,
    // so we can safely sort recs by loop header dominance. We require such
    // order in getAddExpr.
    const Loop *LLoop = LA->getLoop(), *RLoop = RA->getLoop();
    if (LLoop != RLoop) {
      const BasicBlock *LHead = LLoop->getHeader(), *RHead = RLoop->getHeader();
      assert(LHead != RHead && "Two loops share the same header?");
      if (DT.dominates(LHead, RHead))
        return 1;
      assert(DT.dominates(RHead, LHead) &&
             "No dominance between recurrences used by one SCEV?");
      return -1;
    }

    [[fallthrough]];
  }

  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
  case scPtrToInt:
  case scAddExpr:
  case scMulExpr:
  case scUDivExpr:
  case scSMaxExpr:
  case scUMaxExpr:
  case scSMinExpr:
  case scUMinExpr:
  case scSequentialUMinExpr: {
    ArrayRef<const SCEV *> LOps = LHS->operands();
    ArrayRef<const SCEV *> ROps = RHS->operands();

    // Lexicographically compare n-ary-like expressions.
    unsigned LNumOps = LOps.size(), RNumOps = ROps.size();
    if (LNumOps != RNumOps)
      return (int)LNumOps - (int)RNumOps;

    for (unsigned i = 0; i != LNumOps; ++i) {
      auto X = CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI, LOps[i],
                                     ROps[i], DT, Depth + 1);
      if (X != 0)
        return X;
    }
    EqCacheSCEV.unionSets(LHS, RHS);
    return 0;
  }

  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

/// Given a list of SCEV objects, order them by their complexity, and group
/// objects of the same complexity together by value.  When this routine is
/// finished, we know that any duplicates in the vector are consecutive and that
/// complexity is monotonically increasing.
///
/// Note that we go take special precautions to ensure that we get deterministic
/// results from this routine.  In other words, we don't want the results of
/// this to depend on where the addresses of various SCEV objects happened to
/// land in memory.
static void GroupByComplexity(SmallVectorImpl<const SCEV *> &Ops,
                              LoopInfo *LI, DominatorTree &DT) {
  if (Ops.size() < 2) return;  // Noop

  EquivalenceClasses<const SCEV *> EqCacheSCEV;
  EquivalenceClasses<const Value *> EqCacheValue;

  // Whether LHS has provably less complexity than RHS.
  auto IsLessComplex = [&](const SCEV *LHS, const SCEV *RHS) {
    auto Complexity =
        CompareSCEVComplexity(EqCacheSCEV, EqCacheValue, LI, LHS, RHS, DT);
    return Complexity && *Complexity < 0;
  };
  if (Ops.size() == 2) {
    // This is the common case, which also happens to be trivially simple.
    // Special case it.
    const SCEV *&LHS = Ops[0], *&RHS = Ops[1];
    if (IsLessComplex(RHS, LHS))
      std::swap(LHS, RHS);
    return;
  }

  // Do the rough sort by complexity.
  llvm::stable_sort(Ops, [&](const SCEV *LHS, const SCEV *RHS) {
    return IsLessComplex(LHS, RHS);
  });

  // Now that we are sorted by complexity, group elements of the same
  // complexity.  Note that this is, at worst, N^2, but the vector is likely to
  // be extremely short in practice.  Note that we take this approach because we
  // do not want to depend on the addresses of the objects we are grouping.
  for (unsigned i = 0, e = Ops.size(); i != e-2; ++i) {
    const SCEV *S = Ops[i];
    unsigned Complexity = S->getSCEVType();

    // If there are any objects of the same complexity and same value as this
    // one, group them.
    for (unsigned j = i+1; j != e && Ops[j]->getSCEVType() == Complexity; ++j) {
      if (Ops[j] == S) { // Found a duplicate.
        // Move it to immediately after i'th element.
        std::swap(Ops[i+1], Ops[j]);
        ++i;   // no need to rescan it.
        if (i == e-2) return;  // Done!
      }
    }
  }
}

/// Returns true if \p Ops contains a huge SCEV (the subtree of S contains at
/// least HugeExprThreshold nodes).
static bool hasHugeExpression(ArrayRef<const SCEV *> Ops) {
  return any_of(Ops, [](const SCEV *S) {
    return S->getExpressionSize() >= HugeExprThreshold;
  });
}

//===----------------------------------------------------------------------===//
//                      Simple SCEV method implementations
//===----------------------------------------------------------------------===//

/// Compute BC(It, K).  The result has width W.  Assume, K > 0.
static const SCEV *BinomialCoefficient(const SCEV *It, unsigned K,
                                       ScalarEvolution &SE,
                                       Type *ResultTy) {
  // Handle the simplest case efficiently.
  if (K == 1)
    return SE.getTruncateOrZeroExtend(It, ResultTy);

  // We are using the following formula for BC(It, K):
  //
  //   BC(It, K) = (It * (It - 1) * ... * (It - K + 1)) / K!
  //
  // Suppose, W is the bitwidth of the return value.  We must be prepared for
  // overflow.  Hence, we must assure that the result of our computation is
  // equal to the accurate one modulo 2^W.  Unfortunately, division isn't
  // safe in modular arithmetic.
  //
  // However, this code doesn't use exactly that formula; the formula it uses
  // is something like the following, where T is the number of factors of 2 in
  // K! (i.e. trailing zeros in the binary representation of K!), and ^ is
  // exponentiation:
  //
  //   BC(It, K) = (It * (It - 1) * ... * (It - K + 1)) / 2^T / (K! / 2^T)
  //
  // This formula is trivially equivalent to the previous formula.  However,
  // this formula can be implemented much more efficiently.  The trick is that
  // K! / 2^T is odd, and exact division by an odd number *is* safe in modular
  // arithmetic.  To do exact division in modular arithmetic, all we have
  // to do is multiply by the inverse.  Therefore, this step can be done at
  // width W.
  //
  // The next issue is how to safely do the division by 2^T.  The way this
  // is done is by doing the multiplication step at a width of at least W + T
  // bits.  This way, the bottom W+T bits of the product are accurate. Then,
  // when we perform the division by 2^T (which is equivalent to a right shift
  // by T), the bottom W bits are accurate.  Extra bits are okay; they'll get
  // truncated out after the division by 2^T.
  //
  // In comparison to just directly using the first formula, this technique
  // is much more efficient; using the first formula requires W * K bits,
  // but this formula less than W + K bits. Also, the first formula requires
  // a division step, whereas this formula only requires multiplies and shifts.
  //
  // It doesn't matter whether the subtraction step is done in the calculation
  // width or the input iteration count's width; if the subtraction overflows,
  // the result must be zero anyway.  We prefer here to do it in the width of
  // the induction variable because it helps a lot for certain cases; CodeGen
  // isn't smart enough to ignore the overflow, which leads to much less
  // efficient code if the width of the subtraction is wider than the native
  // register width.
  //
  // (It's possible to not widen at all by pulling out factors of 2 before
  // the multiplication; for example, K=2 can be calculated as
  // It/2*(It+(It*INT_MIN/INT_MIN)+-1). However, it requires
  // extra arithmetic, so it's not an obvious win, and it gets
  // much more complicated for K > 3.)

  // Protection from insane SCEVs; this bound is conservative,
  // but it probably doesn't matter.
  if (K > 1000)
    return SE.getCouldNotCompute();

  unsigned W = SE.getTypeSizeInBits(ResultTy);

  // Calculate K! / 2^T and T; we divide out the factors of two before
  // multiplying for calculating K! / 2^T to avoid overflow.
  // Other overflow doesn't matter because we only care about the bottom
  // W bits of the result.
  APInt OddFactorial(W, 1);
  unsigned T = 1;
  for (unsigned i = 3; i <= K; ++i) {
    unsigned TwoFactors = countr_zero(i);
    T += TwoFactors;
    OddFactorial *= (i >> TwoFactors);
  }

  // We need at least W + T bits for the multiplication step
  unsigned CalculationBits = W + T;

  // Calculate 2^T, at width T+W.
  APInt DivFactor = APInt::getOneBitSet(CalculationBits, T);

  // Calculate the multiplicative inverse of K! / 2^T;
  // this multiplication factor will perform the exact division by
  // K! / 2^T.
  APInt MultiplyFactor = OddFactorial.multiplicativeInverse();

  // Calculate the product, at width T+W
  IntegerType *CalculationTy = IntegerType::get(SE.getContext(),
                                                      CalculationBits);
  const SCEV *Dividend = SE.getTruncateOrZeroExtend(It, CalculationTy);
  for (unsigned i = 1; i != K; ++i) {
    const SCEV *S = SE.getMinusSCEV(It, SE.getConstant(It->getType(), i));
    Dividend = SE.getMulExpr(Dividend,
                             SE.getTruncateOrZeroExtend(S, CalculationTy));
  }

  // Divide by 2^T
  const SCEV *DivResult = SE.getUDivExpr(Dividend, SE.getConstant(DivFactor));

  // Truncate the result, and divide by K! / 2^T.

  return SE.getMulExpr(SE.getConstant(MultiplyFactor),
                       SE.getTruncateOrZeroExtend(DivResult, ResultTy));
}

/// Return the value of this chain of recurrences at the specified iteration
/// number.  We can evaluate this recurrence by multiplying each element in the
/// chain by the binomial coefficient corresponding to it.  In other words, we
/// can evaluate {A,+,B,+,C,+,D} as:
///
///   A*BC(It, 0) + B*BC(It, 1) + C*BC(It, 2) + D*BC(It, 3)
///
/// where BC(It, k) stands for binomial coefficient.
const SCEV *SCEVAddRecExpr::evaluateAtIteration(const SCEV *It,
                                                ScalarEvolution &SE) const {
  return evaluateAtIteration(operands(), It, SE);
}

const SCEV *
SCEVAddRecExpr::evaluateAtIteration(ArrayRef<const SCEV *> Operands,
                                    const SCEV *It, ScalarEvolution &SE) {
  assert(Operands.size() > 0);
  const SCEV *Result = Operands[0];
  for (unsigned i = 1, e = Operands.size(); i != e; ++i) {
    // The computation is correct in the face of overflow provided that the
    // multiplication is performed _after_ the evaluation of the binomial
    // coefficient.
    const SCEV *Coeff = BinomialCoefficient(It, i, SE, Result->getType());
    if (isa<SCEVCouldNotCompute>(Coeff))
      return Coeff;

    Result = SE.getAddExpr(Result, SE.getMulExpr(Operands[i], Coeff));
  }
  return Result;
}

//===----------------------------------------------------------------------===//
//                    SCEV Expression folder implementations
//===----------------------------------------------------------------------===//

const SCEV *ScalarEvolution::getLosslessPtrToIntExpr(const SCEV *Op,
                                                     unsigned Depth) {
  assert(Depth <= 1 &&
         "getLosslessPtrToIntExpr() should self-recurse at most once.");

  // We could be called with an integer-typed operands during SCEV rewrites.
  // Since the operand is an integer already, just perform zext/trunc/self cast.
  if (!Op->getType()->isPointerTy())
    return Op;

  // What would be an ID for such a SCEV cast expression?
  FoldingSetNodeID ID;
  ID.AddInteger(scPtrToInt);
  ID.AddPointer(Op);

  void *IP = nullptr;

  // Is there already an expression for such a cast?
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP))
    return S;

  // It isn't legal for optimizations to construct new ptrtoint expressions
  // for non-integral pointers.
  if (getDataLayout().isNonIntegralPointerType(Op->getType()))
    return getCouldNotCompute();

  Type *IntPtrTy = getDataLayout().getIntPtrType(Op->getType());

  // We can only trivially model ptrtoint if SCEV's effective (integer) type
  // is sufficiently wide to represent all possible pointer values.
  // We could theoretically teach SCEV to truncate wider pointers, but
  // that isn't implemented for now.
  if (getDataLayout().getTypeSizeInBits(getEffectiveSCEVType(Op->getType())) !=
      getDataLayout().getTypeSizeInBits(IntPtrTy))
    return getCouldNotCompute();

  // If not, is this expression something we can't reduce any further?
  if (auto *U = dyn_cast<SCEVUnknown>(Op)) {
    // Perform some basic constant folding. If the operand of the ptr2int cast
    // is a null pointer, don't create a ptr2int SCEV expression (that will be
    // left as-is), but produce a zero constant.
    // NOTE: We could handle a more general case, but lack motivational cases.
    if (isa<ConstantPointerNull>(U->getValue()))
      return getZero(IntPtrTy);

    // Create an explicit cast node.
    // We can reuse the existing insert position since if we get here,
    // we won't have made any changes which would invalidate it.
    SCEV *S = new (SCEVAllocator)
        SCEVPtrToIntExpr(ID.Intern(SCEVAllocator), Op, IntPtrTy);
    UniqueSCEVs.InsertNode(S, IP);
    registerUser(S, Op);
    return S;
  }

  assert(Depth == 0 && "getLosslessPtrToIntExpr() should not self-recurse for "
                       "non-SCEVUnknown's.");

  // Otherwise, we've got some expression that is more complex than just a
  // single SCEVUnknown. But we don't want to have a SCEVPtrToIntExpr of an
  // arbitrary expression, we want to have SCEVPtrToIntExpr of an SCEVUnknown
  // only, and the expressions must otherwise be integer-typed.
  // So sink the cast down to the SCEVUnknown's.

  /// The SCEVPtrToIntSinkingRewriter takes a scalar evolution expression,
  /// which computes a pointer-typed value, and rewrites the whole expression
  /// tree so that *all* the computations are done on integers, and the only
  /// pointer-typed operands in the expression are SCEVUnknown.
  class SCEVPtrToIntSinkingRewriter
      : public SCEVRewriteVisitor<SCEVPtrToIntSinkingRewriter> {
    using Base = SCEVRewriteVisitor<SCEVPtrToIntSinkingRewriter>;

  public:
    SCEVPtrToIntSinkingRewriter(ScalarEvolution &SE) : SCEVRewriteVisitor(SE) {}

    static const SCEV *rewrite(const SCEV *Scev, ScalarEvolution &SE) {
      SCEVPtrToIntSinkingRewriter Rewriter(SE);
      return Rewriter.visit(Scev);
    }

    const SCEV *visit(const SCEV *S) {
      Type *STy = S->getType();
      // If the expression is not pointer-typed, just keep it as-is.
      if (!STy->isPointerTy())
        return S;
      // Else, recursively sink the cast down into it.
      return Base::visit(S);
    }

    const SCEV *visitAddExpr(const SCEVAddExpr *Expr) {
      SmallVector<const SCEV *, 2> Operands;
      bool Changed = false;
      for (const auto *Op : Expr->operands()) {
        Operands.push_back(visit(Op));
        Changed |= Op != Operands.back();
      }
      return !Changed ? Expr : SE.getAddExpr(Operands, Expr->getNoWrapFlags());
    }

    const SCEV *visitMulExpr(const SCEVMulExpr *Expr) {
      SmallVector<const SCEV *, 2> Operands;
      bool Changed = false;
      for (const auto *Op : Expr->operands()) {
        Operands.push_back(visit(Op));
        Changed |= Op != Operands.back();
      }
      return !Changed ? Expr : SE.getMulExpr(Operands, Expr->getNoWrapFlags());
    }

    const SCEV *visitUnknown(const SCEVUnknown *Expr) {
      assert(Expr->getType()->isPointerTy() &&
             "Should only reach pointer-typed SCEVUnknown's.");
      return SE.getLosslessPtrToIntExpr(Expr, /*Depth=*/1);
    }
  };

  // And actually perform the cast sinking.
  const SCEV *IntOp = SCEVPtrToIntSinkingRewriter::rewrite(Op, *this);
  assert(IntOp->getType()->isIntegerTy() &&
         "We must have succeeded in sinking the cast, "
         "and ending up with an integer-typed expression!");
  return IntOp;
}

const SCEV *ScalarEvolution::getPtrToIntExpr(const SCEV *Op, Type *Ty) {
  assert(Ty->isIntegerTy() && "Target type must be an integer type!");

  const SCEV *IntOp = getLosslessPtrToIntExpr(Op);
  if (isa<SCEVCouldNotCompute>(IntOp))
    return IntOp;

  return getTruncateOrZeroExtend(IntOp, Ty);
}

const SCEV *ScalarEvolution::getTruncateExpr(const SCEV *Op, Type *Ty,
                                             unsigned Depth) {
  assert(getTypeSizeInBits(Op->getType()) > getTypeSizeInBits(Ty) &&
         "This is not a truncating conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  assert(!Op->getType()->isPointerTy() && "Can't truncate pointer!");
  Ty = getEffectiveSCEVType(Ty);

  FoldingSetNodeID ID;
  ID.AddInteger(scTruncate);
  ID.AddPointer(Op);
  ID.AddPointer(Ty);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;

  // Fold if the operand is constant.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    return getConstant(
      cast<ConstantInt>(ConstantExpr::getTrunc(SC->getValue(), Ty)));

  // trunc(trunc(x)) --> trunc(x)
  if (const SCEVTruncateExpr *ST = dyn_cast<SCEVTruncateExpr>(Op))
    return getTruncateExpr(ST->getOperand(), Ty, Depth + 1);

  // trunc(sext(x)) --> sext(x) if widening or trunc(x) if narrowing
  if (const SCEVSignExtendExpr *SS = dyn_cast<SCEVSignExtendExpr>(Op))
    return getTruncateOrSignExtend(SS->getOperand(), Ty, Depth + 1);

  // trunc(zext(x)) --> zext(x) if widening or trunc(x) if narrowing
  if (const SCEVZeroExtendExpr *SZ = dyn_cast<SCEVZeroExtendExpr>(Op))
    return getTruncateOrZeroExtend(SZ->getOperand(), Ty, Depth + 1);

  if (Depth > MaxCastDepth) {
    SCEV *S =
        new (SCEVAllocator) SCEVTruncateExpr(ID.Intern(SCEVAllocator), Op, Ty);
    UniqueSCEVs.InsertNode(S, IP);
    registerUser(S, Op);
    return S;
  }

  // trunc(x1 + ... + xN) --> trunc(x1) + ... + trunc(xN) and
  // trunc(x1 * ... * xN) --> trunc(x1) * ... * trunc(xN),
  // if after transforming we have at most one truncate, not counting truncates
  // that replace other casts.
  if (isa<SCEVAddExpr>(Op) || isa<SCEVMulExpr>(Op)) {
    auto *CommOp = cast<SCEVCommutativeExpr>(Op);
    SmallVector<const SCEV *, 4> Operands;
    unsigned numTruncs = 0;
    for (unsigned i = 0, e = CommOp->getNumOperands(); i != e && numTruncs < 2;
         ++i) {
      const SCEV *S = getTruncateExpr(CommOp->getOperand(i), Ty, Depth + 1);
      if (!isa<SCEVIntegralCastExpr>(CommOp->getOperand(i)) &&
          isa<SCEVTruncateExpr>(S))
        numTruncs++;
      Operands.push_back(S);
    }
    if (numTruncs < 2) {
      if (isa<SCEVAddExpr>(Op))
        return getAddExpr(Operands);
      if (isa<SCEVMulExpr>(Op))
        return getMulExpr(Operands);
      llvm_unreachable("Unexpected SCEV type for Op.");
    }
    // Although we checked in the beginning that ID is not in the cache, it is
    // possible that during recursion and different modification ID was inserted
    // into the cache. So if we find it, just return it.
    if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP))
      return S;
  }

  // If the input value is a chrec scev, truncate the chrec's operands.
  if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(Op)) {
    SmallVector<const SCEV *, 4> Operands;
    for (const SCEV *Op : AddRec->operands())
      Operands.push_back(getTruncateExpr(Op, Ty, Depth + 1));
    return getAddRecExpr(Operands, AddRec->getLoop(), SCEV::FlagAnyWrap);
  }

  // Return zero if truncating to known zeros.
  uint32_t MinTrailingZeros = getMinTrailingZeros(Op);
  if (MinTrailingZeros >= getTypeSizeInBits(Ty))
    return getZero(Ty);

  // The cast wasn't folded; create an explicit cast node. We can reuse
  // the existing insert position since if we get here, we won't have
  // made any changes which would invalidate it.
  SCEV *S = new (SCEVAllocator) SCEVTruncateExpr(ID.Intern(SCEVAllocator),
                                                 Op, Ty);
  UniqueSCEVs.InsertNode(S, IP);
  registerUser(S, Op);
  return S;
}

// Get the limit of a recurrence such that incrementing by Step cannot cause
// signed overflow as long as the value of the recurrence within the
// loop does not exceed this limit before incrementing.
static const SCEV *getSignedOverflowLimitForStep(const SCEV *Step,
                                                 ICmpInst::Predicate *Pred,
                                                 ScalarEvolution *SE) {
  unsigned BitWidth = SE->getTypeSizeInBits(Step->getType());
  if (SE->isKnownPositive(Step)) {
    *Pred = ICmpInst::ICMP_SLT;
    return SE->getConstant(APInt::getSignedMinValue(BitWidth) -
                           SE->getSignedRangeMax(Step));
  }
  if (SE->isKnownNegative(Step)) {
    *Pred = ICmpInst::ICMP_SGT;
    return SE->getConstant(APInt::getSignedMaxValue(BitWidth) -
                           SE->getSignedRangeMin(Step));
  }
  return nullptr;
}

// Get the limit of a recurrence such that incrementing by Step cannot cause
// unsigned overflow as long as the value of the recurrence within the loop does
// not exceed this limit before incrementing.
static const SCEV *getUnsignedOverflowLimitForStep(const SCEV *Step,
                                                   ICmpInst::Predicate *Pred,
                                                   ScalarEvolution *SE) {
  unsigned BitWidth = SE->getTypeSizeInBits(Step->getType());
  *Pred = ICmpInst::ICMP_ULT;

  return SE->getConstant(APInt::getMinValue(BitWidth) -
                         SE->getUnsignedRangeMax(Step));
}

namespace {

struct ExtendOpTraitsBase {
  typedef const SCEV *(ScalarEvolution::*GetExtendExprTy)(const SCEV *, Type *,
                                                          unsigned);
};

// Used to make code generic over signed and unsigned overflow.
template <typename ExtendOp> struct ExtendOpTraits {
  // Members present:
  //
  // static const SCEV::NoWrapFlags WrapType;
  //
  // static const ExtendOpTraitsBase::GetExtendExprTy GetExtendExpr;
  //
  // static const SCEV *getOverflowLimitForStep(const SCEV *Step,
  //                                           ICmpInst::Predicate *Pred,
  //                                           ScalarEvolution *SE);
};

template <>
struct ExtendOpTraits<SCEVSignExtendExpr> : public ExtendOpTraitsBase {
  static const SCEV::NoWrapFlags WrapType = SCEV::FlagNSW;

  static const GetExtendExprTy GetExtendExpr;

  static const SCEV *getOverflowLimitForStep(const SCEV *Step,
                                             ICmpInst::Predicate *Pred,
                                             ScalarEvolution *SE) {
    return getSignedOverflowLimitForStep(Step, Pred, SE);
  }
};

const ExtendOpTraitsBase::GetExtendExprTy ExtendOpTraits<
    SCEVSignExtendExpr>::GetExtendExpr = &ScalarEvolution::getSignExtendExpr;

template <>
struct ExtendOpTraits<SCEVZeroExtendExpr> : public ExtendOpTraitsBase {
  static const SCEV::NoWrapFlags WrapType = SCEV::FlagNUW;

  static const GetExtendExprTy GetExtendExpr;

  static const SCEV *getOverflowLimitForStep(const SCEV *Step,
                                             ICmpInst::Predicate *Pred,
                                             ScalarEvolution *SE) {
    return getUnsignedOverflowLimitForStep(Step, Pred, SE);
  }
};

const ExtendOpTraitsBase::GetExtendExprTy ExtendOpTraits<
    SCEVZeroExtendExpr>::GetExtendExpr = &ScalarEvolution::getZeroExtendExpr;

} // end anonymous namespace

// The recurrence AR has been shown to have no signed/unsigned wrap or something
// close to it. Typically, if we can prove NSW/NUW for AR, then we can just as
// easily prove NSW/NUW for its preincrement or postincrement sibling. This
// allows normalizing a sign/zero extended AddRec as such: {sext/zext(Step +
// Start),+,Step} => {(Step + sext/zext(Start),+,Step} As a result, the
// expression "Step + sext/zext(PreIncAR)" is congruent with
// "sext/zext(PostIncAR)"
template <typename ExtendOpTy>
static const SCEV *getPreStartForExtend(const SCEVAddRecExpr *AR, Type *Ty,
                                        ScalarEvolution *SE, unsigned Depth) {
  auto WrapType = ExtendOpTraits<ExtendOpTy>::WrapType;
  auto GetExtendExpr = ExtendOpTraits<ExtendOpTy>::GetExtendExpr;

  const Loop *L = AR->getLoop();
  const SCEV *Start = AR->getStart();
  const SCEV *Step = AR->getStepRecurrence(*SE);

  // Check for a simple looking step prior to loop entry.
  const SCEVAddExpr *SA = dyn_cast<SCEVAddExpr>(Start);
  if (!SA)
    return nullptr;

  // Create an AddExpr for "PreStart" after subtracting Step. Full SCEV
  // subtraction is expensive. For this purpose, perform a quick and dirty
  // difference, by checking for Step in the operand list. Note, that
  // SA might have repeated ops, like %a + %a + ..., so only remove one.
  SmallVector<const SCEV *, 4> DiffOps(SA->operands());
  for (auto It = DiffOps.begin(); It != DiffOps.end(); ++It)
    if (*It == Step) {
      DiffOps.erase(It);
      break;
    }

  if (DiffOps.size() == SA->getNumOperands())
    return nullptr;

  // Try to prove `WrapType` (SCEV::FlagNSW or SCEV::FlagNUW) on `PreStart` +
  // `Step`:

  // 1. NSW/NUW flags on the step increment.
  auto PreStartFlags =
    ScalarEvolution::maskFlags(SA->getNoWrapFlags(), SCEV::FlagNUW);
  const SCEV *PreStart = SE->getAddExpr(DiffOps, PreStartFlags);
  const SCEVAddRecExpr *PreAR = dyn_cast<SCEVAddRecExpr>(
      SE->getAddRecExpr(PreStart, Step, L, SCEV::FlagAnyWrap));

  // "{S,+,X} is <nsw>/<nuw>" and "the backedge is taken at least once" implies
  // "S+X does not sign/unsign-overflow".
  //

  const SCEV *BECount = SE->getBackedgeTakenCount(L);
  if (PreAR && PreAR->getNoWrapFlags(WrapType) &&
      !isa<SCEVCouldNotCompute>(BECount) && SE->isKnownPositive(BECount))
    return PreStart;

  // 2. Direct overflow check on the step operation's expression.
  unsigned BitWidth = SE->getTypeSizeInBits(AR->getType());
  Type *WideTy = IntegerType::get(SE->getContext(), BitWidth * 2);
  const SCEV *OperandExtendedStart =
      SE->getAddExpr((SE->*GetExtendExpr)(PreStart, WideTy, Depth),
                     (SE->*GetExtendExpr)(Step, WideTy, Depth));
  if ((SE->*GetExtendExpr)(Start, WideTy, Depth) == OperandExtendedStart) {
    if (PreAR && AR->getNoWrapFlags(WrapType)) {
      // If we know `AR` == {`PreStart`+`Step`,+,`Step`} is `WrapType` (FlagNSW
      // or FlagNUW) and that `PreStart` + `Step` is `WrapType` too, then
      // `PreAR` == {`PreStart`,+,`Step`} is also `WrapType`.  Cache this fact.
      SE->setNoWrapFlags(const_cast<SCEVAddRecExpr *>(PreAR), WrapType);
    }
    return PreStart;
  }

  // 3. Loop precondition.
  ICmpInst::Predicate Pred;
  const SCEV *OverflowLimit =
      ExtendOpTraits<ExtendOpTy>::getOverflowLimitForStep(Step, &Pred, SE);

  if (OverflowLimit &&
      SE->isLoopEntryGuardedByCond(L, Pred, PreStart, OverflowLimit))
    return PreStart;

  return nullptr;
}

// Get the normalized zero or sign extended expression for this AddRec's Start.
template <typename ExtendOpTy>
static const SCEV *getExtendAddRecStart(const SCEVAddRecExpr *AR, Type *Ty,
                                        ScalarEvolution *SE,
                                        unsigned Depth) {
  auto GetExtendExpr = ExtendOpTraits<ExtendOpTy>::GetExtendExpr;

  const SCEV *PreStart = getPreStartForExtend<ExtendOpTy>(AR, Ty, SE, Depth);
  if (!PreStart)
    return (SE->*GetExtendExpr)(AR->getStart(), Ty, Depth);

  return SE->getAddExpr((SE->*GetExtendExpr)(AR->getStepRecurrence(*SE), Ty,
                                             Depth),
                        (SE->*GetExtendExpr)(PreStart, Ty, Depth));
}

// Try to prove away overflow by looking at "nearby" add recurrences.  A
// motivating example for this rule: if we know `{0,+,4}` is `ult` `-1` and it
// does not itself wrap then we can conclude that `{1,+,4}` is `nuw`.
//
// Formally:
//
//     {S,+,X} == {S-T,+,X} + T
//  => Ext({S,+,X}) == Ext({S-T,+,X} + T)
//
// If ({S-T,+,X} + T) does not overflow  ... (1)
//
//  RHS == Ext({S-T,+,X} + T) == Ext({S-T,+,X}) + Ext(T)
//
// If {S-T,+,X} does not overflow  ... (2)
//
//  RHS == Ext({S-T,+,X}) + Ext(T) == {Ext(S-T),+,Ext(X)} + Ext(T)
//      == {Ext(S-T)+Ext(T),+,Ext(X)}
//
// If (S-T)+T does not overflow  ... (3)
//
//  RHS == {Ext(S-T)+Ext(T),+,Ext(X)} == {Ext(S-T+T),+,Ext(X)}
//      == {Ext(S),+,Ext(X)} == LHS
//
// Thus, if (1), (2) and (3) are true for some T, then
//   Ext({S,+,X}) == {Ext(S),+,Ext(X)}
//
// (3) is implied by (1) -- "(S-T)+T does not overflow" is simply "({S-T,+,X}+T)
// does not overflow" restricted to the 0th iteration.  Therefore we only need
// to check for (1) and (2).
//
// In the current context, S is `Start`, X is `Step`, Ext is `ExtendOpTy` and T
// is `Delta` (defined below).
template <typename ExtendOpTy>
bool ScalarEvolution::proveNoWrapByVaryingStart(const SCEV *Start,
                                                const SCEV *Step,
                                                const Loop *L) {
  auto WrapType = ExtendOpTraits<ExtendOpTy>::WrapType;

  // We restrict `Start` to a constant to prevent SCEV from spending too much
  // time here.  It is correct (but more expensive) to continue with a
  // non-constant `Start` and do a general SCEV subtraction to compute
  // `PreStart` below.
  const SCEVConstant *StartC = dyn_cast<SCEVConstant>(Start);
  if (!StartC)
    return false;

  APInt StartAI = StartC->getAPInt();

  for (unsigned Delta : {-2, -1, 1, 2}) {
    const SCEV *PreStart = getConstant(StartAI - Delta);

    FoldingSetNodeID ID;
    ID.AddInteger(scAddRecExpr);
    ID.AddPointer(PreStart);
    ID.AddPointer(Step);
    ID.AddPointer(L);
    void *IP = nullptr;
    const auto *PreAR =
      static_cast<SCEVAddRecExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));

    // Give up if we don't already have the add recurrence we need because
    // actually constructing an add recurrence is relatively expensive.
    if (PreAR && PreAR->getNoWrapFlags(WrapType)) {  // proves (2)
      const SCEV *DeltaS = getConstant(StartC->getType(), Delta);
      ICmpInst::Predicate Pred = ICmpInst::BAD_ICMP_PREDICATE;
      const SCEV *Limit = ExtendOpTraits<ExtendOpTy>::getOverflowLimitForStep(
          DeltaS, &Pred, this);
      if (Limit && isKnownPredicate(Pred, PreAR, Limit))  // proves (1)
        return true;
    }
  }

  return false;
}

// Finds an integer D for an expression (C + x + y + ...) such that the top
// level addition in (D + (C - D + x + y + ...)) would not wrap (signed or
// unsigned) and the number of trailing zeros of (C - D + x + y + ...) is
// maximized, where C is the \p ConstantTerm, x, y, ... are arbitrary SCEVs, and
// the (C + x + y + ...) expression is \p WholeAddExpr.
static APInt extractConstantWithoutWrapping(ScalarEvolution &SE,
                                            const SCEVConstant *ConstantTerm,
                                            const SCEVAddExpr *WholeAddExpr) {
  const APInt &C = ConstantTerm->getAPInt();
  const unsigned BitWidth = C.getBitWidth();
  // Find number of trailing zeros of (x + y + ...) w/o the C first:
  uint32_t TZ = BitWidth;
  for (unsigned I = 1, E = WholeAddExpr->getNumOperands(); I < E && TZ; ++I)
    TZ = std::min(TZ, SE.getMinTrailingZeros(WholeAddExpr->getOperand(I)));
  if (TZ) {
    // Set D to be as many least significant bits of C as possible while still
    // guaranteeing that adding D to (C - D + x + y + ...) won't cause a wrap:
    return TZ < BitWidth ? C.trunc(TZ).zext(BitWidth) : C;
  }
  return APInt(BitWidth, 0);
}

// Finds an integer D for an affine AddRec expression {C,+,x} such that the top
// level addition in (D + {C-D,+,x}) would not wrap (signed or unsigned) and the
// number of trailing zeros of (C - D + x * n) is maximized, where C is the \p
// ConstantStart, x is an arbitrary \p Step, and n is the loop trip count.
static APInt extractConstantWithoutWrapping(ScalarEvolution &SE,
                                            const APInt &ConstantStart,
                                            const SCEV *Step) {
  const unsigned BitWidth = ConstantStart.getBitWidth();
  const uint32_t TZ = SE.getMinTrailingZeros(Step);
  if (TZ)
    return TZ < BitWidth ? ConstantStart.trunc(TZ).zext(BitWidth)
                         : ConstantStart;
  return APInt(BitWidth, 0);
}

static void insertFoldCacheEntry(
    const ScalarEvolution::FoldID &ID, const SCEV *S,
    DenseMap<ScalarEvolution::FoldID, const SCEV *> &FoldCache,
    DenseMap<const SCEV *, SmallVector<ScalarEvolution::FoldID, 2>>
        &FoldCacheUser) {
  auto I = FoldCache.insert({ID, S});
  if (!I.second) {
    // Remove FoldCacheUser entry for ID when replacing an existing FoldCache
    // entry.
    auto &UserIDs = FoldCacheUser[I.first->second];
    assert(count(UserIDs, ID) == 1 && "unexpected duplicates in UserIDs");
    for (unsigned I = 0; I != UserIDs.size(); ++I)
      if (UserIDs[I] == ID) {
        std::swap(UserIDs[I], UserIDs.back());
        break;
      }
    UserIDs.pop_back();
    I.first->second = S;
  }
  auto R = FoldCacheUser.insert({S, {}});
  R.first->second.push_back(ID);
}

const SCEV *
ScalarEvolution::getZeroExtendExpr(const SCEV *Op, Type *Ty, unsigned Depth) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  assert(!Op->getType()->isPointerTy() && "Can't extend pointer!");
  Ty = getEffectiveSCEVType(Ty);

  FoldID ID(scZeroExtend, Op, Ty);
  auto Iter = FoldCache.find(ID);
  if (Iter != FoldCache.end())
    return Iter->second;

  const SCEV *S = getZeroExtendExprImpl(Op, Ty, Depth);
  if (!isa<SCEVZeroExtendExpr>(S))
    insertFoldCacheEntry(ID, S, FoldCache, FoldCacheUser);
  return S;
}

const SCEV *ScalarEvolution::getZeroExtendExprImpl(const SCEV *Op, Type *Ty,
                                                   unsigned Depth) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) && "This is not a conversion to a SCEVable type!");
  assert(!Op->getType()->isPointerTy() && "Can't extend pointer!");

  // Fold if the operand is constant.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    return getConstant(SC->getAPInt().zext(getTypeSizeInBits(Ty)));

  // zext(zext(x)) --> zext(x)
  if (const SCEVZeroExtendExpr *SZ = dyn_cast<SCEVZeroExtendExpr>(Op))
    return getZeroExtendExpr(SZ->getOperand(), Ty, Depth + 1);

  // Before doing any expensive analysis, check to see if we've already
  // computed a SCEV for this Op and Ty.
  FoldingSetNodeID ID;
  ID.AddInteger(scZeroExtend);
  ID.AddPointer(Op);
  ID.AddPointer(Ty);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  if (Depth > MaxCastDepth) {
    SCEV *S = new (SCEVAllocator) SCEVZeroExtendExpr(ID.Intern(SCEVAllocator),
                                                     Op, Ty);
    UniqueSCEVs.InsertNode(S, IP);
    registerUser(S, Op);
    return S;
  }

  // zext(trunc(x)) --> zext(x) or x or trunc(x)
  if (const SCEVTruncateExpr *ST = dyn_cast<SCEVTruncateExpr>(Op)) {
    // It's possible the bits taken off by the truncate were all zero bits. If
    // so, we should be able to simplify this further.
    const SCEV *X = ST->getOperand();
    ConstantRange CR = getUnsignedRange(X);
    unsigned TruncBits = getTypeSizeInBits(ST->getType());
    unsigned NewBits = getTypeSizeInBits(Ty);
    if (CR.truncate(TruncBits).zeroExtend(NewBits).contains(
            CR.zextOrTrunc(NewBits)))
      return getTruncateOrZeroExtend(X, Ty, Depth);
  }

  // If the input value is a chrec scev, and we can prove that the value
  // did not overflow the old, smaller, value, we can zero extend all of the
  // operands (often constants).  This allows analysis of something like
  // this:  for (unsigned char X = 0; X < 100; ++X) { int Y = X; }
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Op))
    if (AR->isAffine()) {
      const SCEV *Start = AR->getStart();
      const SCEV *Step = AR->getStepRecurrence(*this);
      unsigned BitWidth = getTypeSizeInBits(AR->getType());
      const Loop *L = AR->getLoop();

      // If we have special knowledge that this addrec won't overflow,
      // we don't need to do any further analysis.
      if (AR->hasNoUnsignedWrap()) {
        Start =
            getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this, Depth + 1);
        Step = getZeroExtendExpr(Step, Ty, Depth + 1);
        return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
      }

      // Check whether the backedge-taken count is SCEVCouldNotCompute.
      // Note that this serves two purposes: It filters out loops that are
      // simply not analyzable, and it covers the case where this code is
      // being called from within backedge-taken count analysis, such that
      // attempting to ask for the backedge-taken count would likely result
      // in infinite recursion. In the later case, the analysis code will
      // cope with a conservative value, and it will take care to purge
      // that value once it has finished.
      const SCEV *MaxBECount = getConstantMaxBackedgeTakenCount(L);
      if (!isa<SCEVCouldNotCompute>(MaxBECount)) {
        // Manually compute the final value for AR, checking for overflow.

        // Check whether the backedge-taken count can be losslessly casted to
        // the addrec's type. The count is always unsigned.
        const SCEV *CastedMaxBECount =
            getTruncateOrZeroExtend(MaxBECount, Start->getType(), Depth);
        const SCEV *RecastedMaxBECount = getTruncateOrZeroExtend(
            CastedMaxBECount, MaxBECount->getType(), Depth);
        if (MaxBECount == RecastedMaxBECount) {
          Type *WideTy = IntegerType::get(getContext(), BitWidth * 2);
          // Check whether Start+Step*MaxBECount has no unsigned overflow.
          const SCEV *ZMul = getMulExpr(CastedMaxBECount, Step,
                                        SCEV::FlagAnyWrap, Depth + 1);
          const SCEV *ZAdd = getZeroExtendExpr(getAddExpr(Start, ZMul,
                                                          SCEV::FlagAnyWrap,
                                                          Depth + 1),
                                               WideTy, Depth + 1);
          const SCEV *WideStart = getZeroExtendExpr(Start, WideTy, Depth + 1);
          const SCEV *WideMaxBECount =
            getZeroExtendExpr(CastedMaxBECount, WideTy, Depth + 1);
          const SCEV *OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getZeroExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (ZAdd == OperandExtendedAdd) {
            // Cache knowledge of AR NUW, which is propagated to this AddRec.
            setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), SCEV::FlagNUW);
            // Return the expression with the addrec on the outside.
            Start = getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this,
                                                             Depth + 1);
            Step = getZeroExtendExpr(Step, Ty, Depth + 1);
            return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
          }
          // Similar to above, only this time treat the step value as signed.
          // This covers loops that count down.
          OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getSignExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (ZAdd == OperandExtendedAdd) {
            // Cache knowledge of AR NW, which is propagated to this AddRec.
            // Negative step causes unsigned wrap, but it still can't self-wrap.
            setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), SCEV::FlagNW);
            // Return the expression with the addrec on the outside.
            Start = getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this,
                                                             Depth + 1);
            Step = getSignExtendExpr(Step, Ty, Depth + 1);
            return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
          }
        }
      }

      // Normally, in the cases we can prove no-overflow via a
      // backedge guarding condition, we can also compute a backedge
      // taken count for the loop.  The exceptions are assumptions and
      // guards present in the loop -- SCEV is not great at exploiting
      // these to compute max backedge taken counts, but can still use
      // these to prove lack of overflow.  Use this fact to avoid
      // doing extra work that may not pay off.
      if (!isa<SCEVCouldNotCompute>(MaxBECount) || HasGuards ||
          !AC.assumptions().empty()) {

        auto NewFlags = proveNoUnsignedWrapViaInduction(AR);
        setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), NewFlags);
        if (AR->hasNoUnsignedWrap()) {
          // Same as nuw case above - duplicated here to avoid a compile time
          // issue.  It's not clear that the order of checks does matter, but
          // it's one of two issue possible causes for a change which was
          // reverted.  Be conservative for the moment.
          Start =
              getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this, Depth + 1);
          Step = getZeroExtendExpr(Step, Ty, Depth + 1);
          return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
        }

        // For a negative step, we can extend the operands iff doing so only
        // traverses values in the range zext([0,UINT_MAX]).
        if (isKnownNegative(Step)) {
          const SCEV *N = getConstant(APInt::getMaxValue(BitWidth) -
                                      getSignedRangeMin(Step));
          if (isLoopBackedgeGuardedByCond(L, ICmpInst::ICMP_UGT, AR, N) ||
              isKnownOnEveryIteration(ICmpInst::ICMP_UGT, AR, N)) {
            // Cache knowledge of AR NW, which is propagated to this
            // AddRec.  Negative step causes unsigned wrap, but it
            // still can't self-wrap.
            setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), SCEV::FlagNW);
            // Return the expression with the addrec on the outside.
            Start = getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this,
                                                             Depth + 1);
            Step = getSignExtendExpr(Step, Ty, Depth + 1);
            return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
          }
        }
      }

      // zext({C,+,Step}) --> (zext(D) + zext({C-D,+,Step}))<nuw><nsw>
      // if D + (C - D + Step * n) could be proven to not unsigned wrap
      // where D maximizes the number of trailing zeros of (C - D + Step * n)
      if (const auto *SC = dyn_cast<SCEVConstant>(Start)) {
        const APInt &C = SC->getAPInt();
        const APInt &D = extractConstantWithoutWrapping(*this, C, Step);
        if (D != 0) {
          const SCEV *SZExtD = getZeroExtendExpr(getConstant(D), Ty, Depth);
          const SCEV *SResidual =
              getAddRecExpr(getConstant(C - D), Step, L, AR->getNoWrapFlags());
          const SCEV *SZExtR = getZeroExtendExpr(SResidual, Ty, Depth + 1);
          return getAddExpr(SZExtD, SZExtR,
                            (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                            Depth + 1);
        }
      }

      if (proveNoWrapByVaryingStart<SCEVZeroExtendExpr>(Start, Step, L)) {
        setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), SCEV::FlagNUW);
        Start =
            getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this, Depth + 1);
        Step = getZeroExtendExpr(Step, Ty, Depth + 1);
        return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
      }
    }

  // zext(A % B) --> zext(A) % zext(B)
  {
    const SCEV *LHS;
    const SCEV *RHS;
    if (matchURem(Op, LHS, RHS))
      return getURemExpr(getZeroExtendExpr(LHS, Ty, Depth + 1),
                         getZeroExtendExpr(RHS, Ty, Depth + 1));
  }

  // zext(A / B) --> zext(A) / zext(B).
  if (auto *Div = dyn_cast<SCEVUDivExpr>(Op))
    return getUDivExpr(getZeroExtendExpr(Div->getLHS(), Ty, Depth + 1),
                       getZeroExtendExpr(Div->getRHS(), Ty, Depth + 1));

  if (auto *SA = dyn_cast<SCEVAddExpr>(Op)) {
    // zext((A + B + ...)<nuw>) --> (zext(A) + zext(B) + ...)<nuw>
    if (SA->hasNoUnsignedWrap()) {
      // If the addition does not unsign overflow then we can, by definition,
      // commute the zero extension with the addition operation.
      SmallVector<const SCEV *, 4> Ops;
      for (const auto *Op : SA->operands())
        Ops.push_back(getZeroExtendExpr(Op, Ty, Depth + 1));
      return getAddExpr(Ops, SCEV::FlagNUW, Depth + 1);
    }

    // zext(C + x + y + ...) --> (zext(D) + zext((C - D) + x + y + ...))
    // if D + (C - D + x + y + ...) could be proven to not unsigned wrap
    // where D maximizes the number of trailing zeros of (C - D + x + y + ...)
    //
    // Often address arithmetics contain expressions like
    // (zext (add (shl X, C1), C2)), for instance, (zext (5 + (4 * X))).
    // This transformation is useful while proving that such expressions are
    // equal or differ by a small constant amount, see LoadStoreVectorizer pass.
    if (const auto *SC = dyn_cast<SCEVConstant>(SA->getOperand(0))) {
      const APInt &D = extractConstantWithoutWrapping(*this, SC, SA);
      if (D != 0) {
        const SCEV *SZExtD = getZeroExtendExpr(getConstant(D), Ty, Depth);
        const SCEV *SResidual =
            getAddExpr(getConstant(-D), SA, SCEV::FlagAnyWrap, Depth);
        const SCEV *SZExtR = getZeroExtendExpr(SResidual, Ty, Depth + 1);
        return getAddExpr(SZExtD, SZExtR,
                          (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                          Depth + 1);
      }
    }
  }

  if (auto *SM = dyn_cast<SCEVMulExpr>(Op)) {
    // zext((A * B * ...)<nuw>) --> (zext(A) * zext(B) * ...)<nuw>
    if (SM->hasNoUnsignedWrap()) {
      // If the multiply does not unsign overflow then we can, by definition,
      // commute the zero extension with the multiply operation.
      SmallVector<const SCEV *, 4> Ops;
      for (const auto *Op : SM->operands())
        Ops.push_back(getZeroExtendExpr(Op, Ty, Depth + 1));
      return getMulExpr(Ops, SCEV::FlagNUW, Depth + 1);
    }

    // zext(2^K * (trunc X to iN)) to iM ->
    // 2^K * (zext(trunc X to i{N-K}) to iM)<nuw>
    //
    // Proof:
    //
    //     zext(2^K * (trunc X to iN)) to iM
    //   = zext((trunc X to iN) << K) to iM
    //   = zext((trunc X to i{N-K}) << K)<nuw> to iM
    //     (because shl removes the top K bits)
    //   = zext((2^K * (trunc X to i{N-K}))<nuw>) to iM
    //   = (2^K * (zext(trunc X to i{N-K}) to iM))<nuw>.
    //
    if (SM->getNumOperands() == 2)
      if (auto *MulLHS = dyn_cast<SCEVConstant>(SM->getOperand(0)))
        if (MulLHS->getAPInt().isPowerOf2())
          if (auto *TruncRHS = dyn_cast<SCEVTruncateExpr>(SM->getOperand(1))) {
            int NewTruncBits = getTypeSizeInBits(TruncRHS->getType()) -
                               MulLHS->getAPInt().logBase2();
            Type *NewTruncTy = IntegerType::get(getContext(), NewTruncBits);
            return getMulExpr(
                getZeroExtendExpr(MulLHS, Ty),
                getZeroExtendExpr(
                    getTruncateExpr(TruncRHS->getOperand(), NewTruncTy), Ty),
                SCEV::FlagNUW, Depth + 1);
          }
  }

  // zext(umin(x, y)) -> umin(zext(x), zext(y))
  // zext(umax(x, y)) -> umax(zext(x), zext(y))
  if (isa<SCEVUMinExpr>(Op) || isa<SCEVUMaxExpr>(Op)) {
    auto *MinMax = cast<SCEVMinMaxExpr>(Op);
    SmallVector<const SCEV *, 4> Operands;
    for (auto *Operand : MinMax->operands())
      Operands.push_back(getZeroExtendExpr(Operand, Ty));
    if (isa<SCEVUMinExpr>(MinMax))
      return getUMinExpr(Operands);
    return getUMaxExpr(Operands);
  }

  // zext(umin_seq(x, y)) -> umin_seq(zext(x), zext(y))
  if (auto *MinMax = dyn_cast<SCEVSequentialMinMaxExpr>(Op)) {
    assert(isa<SCEVSequentialUMinExpr>(MinMax) && "Not supported!");
    SmallVector<const SCEV *, 4> Operands;
    for (auto *Operand : MinMax->operands())
      Operands.push_back(getZeroExtendExpr(Operand, Ty));
    return getUMinExpr(Operands, /*Sequential*/ true);
  }

  // The cast wasn't folded; create an explicit cast node.
  // Recompute the insert position, as it may have been invalidated.
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVZeroExtendExpr(ID.Intern(SCEVAllocator),
                                                   Op, Ty);
  UniqueSCEVs.InsertNode(S, IP);
  registerUser(S, Op);
  return S;
}

const SCEV *
ScalarEvolution::getSignExtendExpr(const SCEV *Op, Type *Ty, unsigned Depth) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  assert(!Op->getType()->isPointerTy() && "Can't extend pointer!");
  Ty = getEffectiveSCEVType(Ty);

  FoldID ID(scSignExtend, Op, Ty);
  auto Iter = FoldCache.find(ID);
  if (Iter != FoldCache.end())
    return Iter->second;

  const SCEV *S = getSignExtendExprImpl(Op, Ty, Depth);
  if (!isa<SCEVSignExtendExpr>(S))
    insertFoldCacheEntry(ID, S, FoldCache, FoldCacheUser);
  return S;
}

const SCEV *ScalarEvolution::getSignExtendExprImpl(const SCEV *Op, Type *Ty,
                                                   unsigned Depth) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) && "This is not a conversion to a SCEVable type!");
  assert(!Op->getType()->isPointerTy() && "Can't extend pointer!");
  Ty = getEffectiveSCEVType(Ty);

  // Fold if the operand is constant.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    return getConstant(SC->getAPInt().sext(getTypeSizeInBits(Ty)));

  // sext(sext(x)) --> sext(x)
  if (const SCEVSignExtendExpr *SS = dyn_cast<SCEVSignExtendExpr>(Op))
    return getSignExtendExpr(SS->getOperand(), Ty, Depth + 1);

  // sext(zext(x)) --> zext(x)
  if (const SCEVZeroExtendExpr *SZ = dyn_cast<SCEVZeroExtendExpr>(Op))
    return getZeroExtendExpr(SZ->getOperand(), Ty, Depth + 1);

  // Before doing any expensive analysis, check to see if we've already
  // computed a SCEV for this Op and Ty.
  FoldingSetNodeID ID;
  ID.AddInteger(scSignExtend);
  ID.AddPointer(Op);
  ID.AddPointer(Ty);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  // Limit recursion depth.
  if (Depth > MaxCastDepth) {
    SCEV *S = new (SCEVAllocator) SCEVSignExtendExpr(ID.Intern(SCEVAllocator),
                                                     Op, Ty);
    UniqueSCEVs.InsertNode(S, IP);
    registerUser(S, Op);
    return S;
  }

  // sext(trunc(x)) --> sext(x) or x or trunc(x)
  if (const SCEVTruncateExpr *ST = dyn_cast<SCEVTruncateExpr>(Op)) {
    // It's possible the bits taken off by the truncate were all sign bits. If
    // so, we should be able to simplify this further.
    const SCEV *X = ST->getOperand();
    ConstantRange CR = getSignedRange(X);
    unsigned TruncBits = getTypeSizeInBits(ST->getType());
    unsigned NewBits = getTypeSizeInBits(Ty);
    if (CR.truncate(TruncBits).signExtend(NewBits).contains(
            CR.sextOrTrunc(NewBits)))
      return getTruncateOrSignExtend(X, Ty, Depth);
  }

  if (auto *SA = dyn_cast<SCEVAddExpr>(Op)) {
    // sext((A + B + ...)<nsw>) --> (sext(A) + sext(B) + ...)<nsw>
    if (SA->hasNoSignedWrap()) {
      // If the addition does not sign overflow then we can, by definition,
      // commute the sign extension with the addition operation.
      SmallVector<const SCEV *, 4> Ops;
      for (const auto *Op : SA->operands())
        Ops.push_back(getSignExtendExpr(Op, Ty, Depth + 1));
      return getAddExpr(Ops, SCEV::FlagNSW, Depth + 1);
    }

    // sext(C + x + y + ...) --> (sext(D) + sext((C - D) + x + y + ...))
    // if D + (C - D + x + y + ...) could be proven to not signed wrap
    // where D maximizes the number of trailing zeros of (C - D + x + y + ...)
    //
    // For instance, this will bring two seemingly different expressions:
    //     1 + sext(5 + 20 * %x + 24 * %y)  and
    //         sext(6 + 20 * %x + 24 * %y)
    // to the same form:
    //     2 + sext(4 + 20 * %x + 24 * %y)
    if (const auto *SC = dyn_cast<SCEVConstant>(SA->getOperand(0))) {
      const APInt &D = extractConstantWithoutWrapping(*this, SC, SA);
      if (D != 0) {
        const SCEV *SSExtD = getSignExtendExpr(getConstant(D), Ty, Depth);
        const SCEV *SResidual =
            getAddExpr(getConstant(-D), SA, SCEV::FlagAnyWrap, Depth);
        const SCEV *SSExtR = getSignExtendExpr(SResidual, Ty, Depth + 1);
        return getAddExpr(SSExtD, SSExtR,
                          (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                          Depth + 1);
      }
    }
  }
  // If the input value is a chrec scev, and we can prove that the value
  // did not overflow the old, smaller, value, we can sign extend all of the
  // operands (often constants).  This allows analysis of something like
  // this:  for (signed char X = 0; X < 100; ++X) { int Y = X; }
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Op))
    if (AR->isAffine()) {
      const SCEV *Start = AR->getStart();
      const SCEV *Step = AR->getStepRecurrence(*this);
      unsigned BitWidth = getTypeSizeInBits(AR->getType());
      const Loop *L = AR->getLoop();

      // If we have special knowledge that this addrec won't overflow,
      // we don't need to do any further analysis.
      if (AR->hasNoSignedWrap()) {
        Start =
            getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this, Depth + 1);
        Step = getSignExtendExpr(Step, Ty, Depth + 1);
        return getAddRecExpr(Start, Step, L, SCEV::FlagNSW);
      }

      // Check whether the backedge-taken count is SCEVCouldNotCompute.
      // Note that this serves two purposes: It filters out loops that are
      // simply not analyzable, and it covers the case where this code is
      // being called from within backedge-taken count analysis, such that
      // attempting to ask for the backedge-taken count would likely result
      // in infinite recursion. In the later case, the analysis code will
      // cope with a conservative value, and it will take care to purge
      // that value once it has finished.
      const SCEV *MaxBECount = getConstantMaxBackedgeTakenCount(L);
      if (!isa<SCEVCouldNotCompute>(MaxBECount)) {
        // Manually compute the final value for AR, checking for
        // overflow.

        // Check whether the backedge-taken count can be losslessly casted to
        // the addrec's type. The count is always unsigned.
        const SCEV *CastedMaxBECount =
            getTruncateOrZeroExtend(MaxBECount, Start->getType(), Depth);
        const SCEV *RecastedMaxBECount = getTruncateOrZeroExtend(
            CastedMaxBECount, MaxBECount->getType(), Depth);
        if (MaxBECount == RecastedMaxBECount) {
          Type *WideTy = IntegerType::get(getContext(), BitWidth * 2);
          // Check whether Start+Step*MaxBECount has no signed overflow.
          const SCEV *SMul = getMulExpr(CastedMaxBECount, Step,
                                        SCEV::FlagAnyWrap, Depth + 1);
          const SCEV *SAdd = getSignExtendExpr(getAddExpr(Start, SMul,
                                                          SCEV::FlagAnyWrap,
                                                          Depth + 1),
                                               WideTy, Depth + 1);
          const SCEV *WideStart = getSignExtendExpr(Start, WideTy, Depth + 1);
          const SCEV *WideMaxBECount =
            getZeroExtendExpr(CastedMaxBECount, WideTy, Depth + 1);
          const SCEV *OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getSignExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (SAdd == OperandExtendedAdd) {
            // Cache knowledge of AR NSW, which is propagated to this AddRec.
            setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), SCEV::FlagNSW);
            // Return the expression with the addrec on the outside.
            Start = getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this,
                                                             Depth + 1);
            Step = getSignExtendExpr(Step, Ty, Depth + 1);
            return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
          }
          // Similar to above, only this time treat the step value as unsigned.
          // This covers loops that count up with an unsigned step.
          OperandExtendedAdd =
            getAddExpr(WideStart,
                       getMulExpr(WideMaxBECount,
                                  getZeroExtendExpr(Step, WideTy, Depth + 1),
                                  SCEV::FlagAnyWrap, Depth + 1),
                       SCEV::FlagAnyWrap, Depth + 1);
          if (SAdd == OperandExtendedAdd) {
            // If AR wraps around then
            //
            //    abs(Step) * MaxBECount > unsigned-max(AR->getType())
            // => SAdd != OperandExtendedAdd
            //
            // Thus (AR is not NW => SAdd != OperandExtendedAdd) <=>
            // (SAdd == OperandExtendedAdd => AR is NW)

            setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), SCEV::FlagNW);

            // Return the expression with the addrec on the outside.
            Start = getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this,
                                                             Depth + 1);
            Step = getZeroExtendExpr(Step, Ty, Depth + 1);
            return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
          }
        }
      }

      auto NewFlags = proveNoSignedWrapViaInduction(AR);
      setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), NewFlags);
      if (AR->hasNoSignedWrap()) {
        // Same as nsw case above - duplicated here to avoid a compile time
        // issue.  It's not clear that the order of checks does matter, but
        // it's one of two issue possible causes for a change which was
        // reverted.  Be conservative for the moment.
        Start =
            getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this, Depth + 1);
        Step = getSignExtendExpr(Step, Ty, Depth + 1);
        return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
      }

      // sext({C,+,Step}) --> (sext(D) + sext({C-D,+,Step}))<nuw><nsw>
      // if D + (C - D + Step * n) could be proven to not signed wrap
      // where D maximizes the number of trailing zeros of (C - D + Step * n)
      if (const auto *SC = dyn_cast<SCEVConstant>(Start)) {
        const APInt &C = SC->getAPInt();
        const APInt &D = extractConstantWithoutWrapping(*this, C, Step);
        if (D != 0) {
          const SCEV *SSExtD = getSignExtendExpr(getConstant(D), Ty, Depth);
          const SCEV *SResidual =
              getAddRecExpr(getConstant(C - D), Step, L, AR->getNoWrapFlags());
          const SCEV *SSExtR = getSignExtendExpr(SResidual, Ty, Depth + 1);
          return getAddExpr(SSExtD, SSExtR,
                            (SCEV::NoWrapFlags)(SCEV::FlagNSW | SCEV::FlagNUW),
                            Depth + 1);
        }
      }

      if (proveNoWrapByVaryingStart<SCEVSignExtendExpr>(Start, Step, L)) {
        setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), SCEV::FlagNSW);
        Start =
            getExtendAddRecStart<SCEVSignExtendExpr>(AR, Ty, this, Depth + 1);
        Step = getSignExtendExpr(Step, Ty, Depth + 1);
        return getAddRecExpr(Start, Step, L, AR->getNoWrapFlags());
      }
    }

  // If the input value is provably positive and we could not simplify
  // away the sext build a zext instead.
  if (isKnownNonNegative(Op))
    return getZeroExtendExpr(Op, Ty, Depth + 1);

  // sext(smin(x, y)) -> smin(sext(x), sext(y))
  // sext(smax(x, y)) -> smax(sext(x), sext(y))
  if (isa<SCEVSMinExpr>(Op) || isa<SCEVSMaxExpr>(Op)) {
    auto *MinMax = cast<SCEVMinMaxExpr>(Op);
    SmallVector<const SCEV *, 4> Operands;
    for (auto *Operand : MinMax->operands())
      Operands.push_back(getSignExtendExpr(Operand, Ty));
    if (isa<SCEVSMinExpr>(MinMax))
      return getSMinExpr(Operands);
    return getSMaxExpr(Operands);
  }

  // The cast wasn't folded; create an explicit cast node.
  // Recompute the insert position, as it may have been invalidated.
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVSignExtendExpr(ID.Intern(SCEVAllocator),
                                                   Op, Ty);
  UniqueSCEVs.InsertNode(S, IP);
  registerUser(S, { Op });
  return S;
}

const SCEV *ScalarEvolution::getCastExpr(SCEVTypes Kind, const SCEV *Op,
                                         Type *Ty) {
  switch (Kind) {
  case scTruncate:
    return getTruncateExpr(Op, Ty);
  case scZeroExtend:
    return getZeroExtendExpr(Op, Ty);
  case scSignExtend:
    return getSignExtendExpr(Op, Ty);
  case scPtrToInt:
    return getPtrToIntExpr(Op, Ty);
  default:
    llvm_unreachable("Not a SCEV cast expression!");
  }
}

/// getAnyExtendExpr - Return a SCEV for the given operand extended with
/// unspecified bits out to the given type.
const SCEV *ScalarEvolution::getAnyExtendExpr(const SCEV *Op,
                                              Type *Ty) {
  assert(getTypeSizeInBits(Op->getType()) < getTypeSizeInBits(Ty) &&
         "This is not an extending conversion!");
  assert(isSCEVable(Ty) &&
         "This is not a conversion to a SCEVable type!");
  Ty = getEffectiveSCEVType(Ty);

  // Sign-extend negative constants.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(Op))
    if (SC->getAPInt().isNegative())
      return getSignExtendExpr(Op, Ty);

  // Peel off a truncate cast.
  if (const SCEVTruncateExpr *T = dyn_cast<SCEVTruncateExpr>(Op)) {
    const SCEV *NewOp = T->getOperand();
    if (getTypeSizeInBits(NewOp->getType()) < getTypeSizeInBits(Ty))
      return getAnyExtendExpr(NewOp, Ty);
    return getTruncateOrNoop(NewOp, Ty);
  }

  // Next try a zext cast. If the cast is folded, use it.
  const SCEV *ZExt = getZeroExtendExpr(Op, Ty);
  if (!isa<SCEVZeroExtendExpr>(ZExt))
    return ZExt;

  // Next try a sext cast. If the cast is folded, use it.
  const SCEV *SExt = getSignExtendExpr(Op, Ty);
  if (!isa<SCEVSignExtendExpr>(SExt))
    return SExt;

  // Force the cast to be folded into the operands of an addrec.
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Op)) {
    SmallVector<const SCEV *, 4> Ops;
    for (const SCEV *Op : AR->operands())
      Ops.push_back(getAnyExtendExpr(Op, Ty));
    return getAddRecExpr(Ops, AR->getLoop(), SCEV::FlagNW);
  }

  // If the expression is obviously signed, use the sext cast value.
  if (isa<SCEVSMaxExpr>(Op))
    return SExt;

  // Absent any other information, use the zext cast value.
  return ZExt;
}

/// Process the given Ops list, which is a list of operands to be added under
/// the given scale, update the given map. This is a helper function for
/// getAddRecExpr. As an example of what it does, given a sequence of operands
/// that would form an add expression like this:
///
///    m + n + 13 + (A * (o + p + (B * (q + m + 29)))) + r + (-1 * r)
///
/// where A and B are constants, update the map with these values:
///
///    (m, 1+A*B), (n, 1), (o, A), (p, A), (q, A*B), (r, 0)
///
/// and add 13 + A*B*29 to AccumulatedConstant.
/// This will allow getAddRecExpr to produce this:
///
///    13+A*B*29 + n + (m * (1+A*B)) + ((o + p) * A) + (q * A*B)
///
/// This form often exposes folding opportunities that are hidden in
/// the original operand list.
///
/// Return true iff it appears that any interesting folding opportunities
/// may be exposed. This helps getAddRecExpr short-circuit extra work in
/// the common case where no interesting opportunities are present, and
/// is also used as a check to avoid infinite recursion.
static bool
CollectAddOperandsWithScales(DenseMap<const SCEV *, APInt> &M,
                             SmallVectorImpl<const SCEV *> &NewOps,
                             APInt &AccumulatedConstant,
                             ArrayRef<const SCEV *> Ops, const APInt &Scale,
                             ScalarEvolution &SE) {
  bool Interesting = false;

  // Iterate over the add operands. They are sorted, with constants first.
  unsigned i = 0;
  while (const SCEVConstant *C = dyn_cast<SCEVConstant>(Ops[i])) {
    ++i;
    // Pull a buried constant out to the outside.
    if (Scale != 1 || AccumulatedConstant != 0 || C->getValue()->isZero())
      Interesting = true;
    AccumulatedConstant += Scale * C->getAPInt();
  }

  // Next comes everything else. We're especially interested in multiplies
  // here, but they're in the middle, so just visit the rest with one loop.
  for (; i != Ops.size(); ++i) {
    const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(Ops[i]);
    if (Mul && isa<SCEVConstant>(Mul->getOperand(0))) {
      APInt NewScale =
          Scale * cast<SCEVConstant>(Mul->getOperand(0))->getAPInt();
      if (Mul->getNumOperands() == 2 && isa<SCEVAddExpr>(Mul->getOperand(1))) {
        // A multiplication of a constant with another add; recurse.
        const SCEVAddExpr *Add = cast<SCEVAddExpr>(Mul->getOperand(1));
        Interesting |=
          CollectAddOperandsWithScales(M, NewOps, AccumulatedConstant,
                                       Add->operands(), NewScale, SE);
      } else {
        // A multiplication of a constant with some other value. Update
        // the map.
        SmallVector<const SCEV *, 4> MulOps(drop_begin(Mul->operands()));
        const SCEV *Key = SE.getMulExpr(MulOps);
        auto Pair = M.insert({Key, NewScale});
        if (Pair.second) {
          NewOps.push_back(Pair.first->first);
        } else {
          Pair.first->second += NewScale;
          // The map already had an entry for this value, which may indicate
          // a folding opportunity.
          Interesting = true;
        }
      }
    } else {
      // An ordinary operand. Update the map.
      std::pair<DenseMap<const SCEV *, APInt>::iterator, bool> Pair =
          M.insert({Ops[i], Scale});
      if (Pair.second) {
        NewOps.push_back(Pair.first->first);
      } else {
        Pair.first->second += Scale;
        // The map already had an entry for this value, which may indicate
        // a folding opportunity.
        Interesting = true;
      }
    }
  }

  return Interesting;
}

bool ScalarEvolution::willNotOverflow(Instruction::BinaryOps BinOp, bool Signed,
                                      const SCEV *LHS, const SCEV *RHS,
                                      const Instruction *CtxI) {
  const SCEV *(ScalarEvolution::*Operation)(const SCEV *, const SCEV *,
                                            SCEV::NoWrapFlags, unsigned);
  switch (BinOp) {
  default:
    llvm_unreachable("Unsupported binary op");
  case Instruction::Add:
    Operation = &ScalarEvolution::getAddExpr;
    break;
  case Instruction::Sub:
    Operation = &ScalarEvolution::getMinusSCEV;
    break;
  case Instruction::Mul:
    Operation = &ScalarEvolution::getMulExpr;
    break;
  }

  const SCEV *(ScalarEvolution::*Extension)(const SCEV *, Type *, unsigned) =
      Signed ? &ScalarEvolution::getSignExtendExpr
             : &ScalarEvolution::getZeroExtendExpr;

  // Check ext(LHS op RHS) == ext(LHS) op ext(RHS)
  auto *NarrowTy = cast<IntegerType>(LHS->getType());
  auto *WideTy =
      IntegerType::get(NarrowTy->getContext(), NarrowTy->getBitWidth() * 2);

  const SCEV *A = (this->*Extension)(
      (this->*Operation)(LHS, RHS, SCEV::FlagAnyWrap, 0), WideTy, 0);
  const SCEV *LHSB = (this->*Extension)(LHS, WideTy, 0);
  const SCEV *RHSB = (this->*Extension)(RHS, WideTy, 0);
  const SCEV *B = (this->*Operation)(LHSB, RHSB, SCEV::FlagAnyWrap, 0);
  if (A == B)
    return true;
  // Can we use context to prove the fact we need?
  if (!CtxI)
    return false;
  // TODO: Support mul.
  if (BinOp == Instruction::Mul)
    return false;
  auto *RHSC = dyn_cast<SCEVConstant>(RHS);
  // TODO: Lift this limitation.
  if (!RHSC)
    return false;
  APInt C = RHSC->getAPInt();
  unsigned NumBits = C.getBitWidth();
  bool IsSub = (BinOp == Instruction::Sub);
  bool IsNegativeConst = (Signed && C.isNegative());
  // Compute the direction and magnitude by which we need to check overflow.
  bool OverflowDown = IsSub ^ IsNegativeConst;
  APInt Magnitude = C;
  if (IsNegativeConst) {
    if (C == APInt::getSignedMinValue(NumBits))
      // TODO: SINT_MIN on inversion gives the same negative value, we don't
      // want to deal with that.
      return false;
    Magnitude = -C;
  }

  ICmpInst::Predicate Pred = Signed ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE;
  if (OverflowDown) {
    // To avoid overflow down, we need to make sure that MIN + Magnitude <= LHS.
    APInt Min = Signed ? APInt::getSignedMinValue(NumBits)
                       : APInt::getMinValue(NumBits);
    APInt Limit = Min + Magnitude;
    return isKnownPredicateAt(Pred, getConstant(Limit), LHS, CtxI);
  } else {
    // To avoid overflow up, we need to make sure that LHS <= MAX - Magnitude.
    APInt Max = Signed ? APInt::getSignedMaxValue(NumBits)
                       : APInt::getMaxValue(NumBits);
    APInt Limit = Max - Magnitude;
    return isKnownPredicateAt(Pred, LHS, getConstant(Limit), CtxI);
  }
}

std::optional<SCEV::NoWrapFlags>
ScalarEvolution::getStrengthenedNoWrapFlagsFromBinOp(
    const OverflowingBinaryOperator *OBO) {
  // It cannot be done any better.
  if (OBO->hasNoUnsignedWrap() && OBO->hasNoSignedWrap())
    return std::nullopt;

  SCEV::NoWrapFlags Flags = SCEV::NoWrapFlags::FlagAnyWrap;

  if (OBO->hasNoUnsignedWrap())
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
  if (OBO->hasNoSignedWrap())
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNSW);

  bool Deduced = false;

  if (OBO->getOpcode() != Instruction::Add &&
      OBO->getOpcode() != Instruction::Sub &&
      OBO->getOpcode() != Instruction::Mul)
    return std::nullopt;

  const SCEV *LHS = getSCEV(OBO->getOperand(0));
  const SCEV *RHS = getSCEV(OBO->getOperand(1));

  const Instruction *CtxI =
      UseContextForNoWrapFlagInference ? dyn_cast<Instruction>(OBO) : nullptr;
  if (!OBO->hasNoUnsignedWrap() &&
      willNotOverflow((Instruction::BinaryOps)OBO->getOpcode(),
                      /* Signed */ false, LHS, RHS, CtxI)) {
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
    Deduced = true;
  }

  if (!OBO->hasNoSignedWrap() &&
      willNotOverflow((Instruction::BinaryOps)OBO->getOpcode(),
                      /* Signed */ true, LHS, RHS, CtxI)) {
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNSW);
    Deduced = true;
  }

  if (Deduced)
    return Flags;
  return std::nullopt;
}

// We're trying to construct a SCEV of type `Type' with `Ops' as operands and
// `OldFlags' as can't-wrap behavior.  Infer a more aggressive set of
// can't-overflow flags for the operation if possible.
static SCEV::NoWrapFlags
StrengthenNoWrapFlags(ScalarEvolution *SE, SCEVTypes Type,
                      const ArrayRef<const SCEV *> Ops,
                      SCEV::NoWrapFlags Flags) {
  using namespace std::placeholders;

  using OBO = OverflowingBinaryOperator;

  bool CanAnalyze =
      Type == scAddExpr || Type == scAddRecExpr || Type == scMulExpr;
  (void)CanAnalyze;
  assert(CanAnalyze && "don't call from other places!");

  int SignOrUnsignMask = SCEV::FlagNUW | SCEV::FlagNSW;
  SCEV::NoWrapFlags SignOrUnsignWrap =
      ScalarEvolution::maskFlags(Flags, SignOrUnsignMask);

  // If FlagNSW is true and all the operands are non-negative, infer FlagNUW.
  auto IsKnownNonNegative = [&](const SCEV *S) {
    return SE->isKnownNonNegative(S);
  };

  if (SignOrUnsignWrap == SCEV::FlagNSW && all_of(Ops, IsKnownNonNegative))
    Flags =
        ScalarEvolution::setFlags(Flags, (SCEV::NoWrapFlags)SignOrUnsignMask);

  SignOrUnsignWrap = ScalarEvolution::maskFlags(Flags, SignOrUnsignMask);

  if (SignOrUnsignWrap != SignOrUnsignMask &&
      (Type == scAddExpr || Type == scMulExpr) && Ops.size() == 2 &&
      isa<SCEVConstant>(Ops[0])) {

    auto Opcode = [&] {
      switch (Type) {
      case scAddExpr:
        return Instruction::Add;
      case scMulExpr:
        return Instruction::Mul;
      default:
        llvm_unreachable("Unexpected SCEV op.");
      }
    }();

    const APInt &C = cast<SCEVConstant>(Ops[0])->getAPInt();

    // (A <opcode> C) --> (A <opcode> C)<nsw> if the op doesn't sign overflow.
    if (!(SignOrUnsignWrap & SCEV::FlagNSW)) {
      auto NSWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
          Opcode, C, OBO::NoSignedWrap);
      if (NSWRegion.contains(SE->getSignedRange(Ops[1])))
        Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNSW);
    }

    // (A <opcode> C) --> (A <opcode> C)<nuw> if the op doesn't unsign overflow.
    if (!(SignOrUnsignWrap & SCEV::FlagNUW)) {
      auto NUWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
          Opcode, C, OBO::NoUnsignedWrap);
      if (NUWRegion.contains(SE->getUnsignedRange(Ops[1])))
        Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
    }
  }

  // <0,+,nonnegative><nw> is also nuw
  // TODO: Add corresponding nsw case
  if (Type == scAddRecExpr && ScalarEvolution::hasFlags(Flags, SCEV::FlagNW) &&
      !ScalarEvolution::hasFlags(Flags, SCEV::FlagNUW) && Ops.size() == 2 &&
      Ops[0]->isZero() && IsKnownNonNegative(Ops[1]))
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);

  // both (udiv X, Y) * Y and Y * (udiv X, Y) are always NUW
  if (Type == scMulExpr && !ScalarEvolution::hasFlags(Flags, SCEV::FlagNUW) &&
      Ops.size() == 2) {
    if (auto *UDiv = dyn_cast<SCEVUDivExpr>(Ops[0]))
      if (UDiv->getOperand(1) == Ops[1])
        Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
    if (auto *UDiv = dyn_cast<SCEVUDivExpr>(Ops[1]))
      if (UDiv->getOperand(1) == Ops[0])
        Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
  }

  return Flags;
}

bool ScalarEvolution::isAvailableAtLoopEntry(const SCEV *S, const Loop *L) {
  return isLoopInvariant(S, L) && properlyDominates(S, L->getHeader());
}

/// Get a canonical add expression, or something simpler if possible.
const SCEV *ScalarEvolution::getAddExpr(SmallVectorImpl<const SCEV *> &Ops,
                                        SCEV::NoWrapFlags OrigFlags,
                                        unsigned Depth) {
  assert(!(OrigFlags & ~(SCEV::FlagNUW | SCEV::FlagNSW)) &&
         "only nuw or nsw allowed");
  assert(!Ops.empty() && "Cannot get empty add!");
  if (Ops.size() == 1) return Ops[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Ops[0]->getType());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i)
    assert(getEffectiveSCEVType(Ops[i]->getType()) == ETy &&
           "SCEVAddExpr operand types don't match!");
  unsigned NumPtrs = count_if(
      Ops, [](const SCEV *Op) { return Op->getType()->isPointerTy(); });
  assert(NumPtrs <= 1 && "add has at most one pointer operand");
#endif

  // Sort by complexity, this groups all similar expression types together.
  GroupByComplexity(Ops, &LI, DT);

  // If there are any constants, fold them together.
  unsigned Idx = 0;
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {
    ++Idx;
    assert(Idx < Ops.size());
    while (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(Ops[Idx])) {
      // We found two constants, fold them together!
      Ops[0] = getConstant(LHSC->getAPInt() + RHSC->getAPInt());
      if (Ops.size() == 2) return Ops[0];
      Ops.erase(Ops.begin()+1);  // Erase the folded element
      LHSC = cast<SCEVConstant>(Ops[0]);
    }

    // If we are left with a constant zero being added, strip it off.
    if (LHSC->getValue()->isZero()) {
      Ops.erase(Ops.begin());
      --Idx;
    }

    if (Ops.size() == 1) return Ops[0];
  }

  // Delay expensive flag strengthening until necessary.
  auto ComputeFlags = [this, OrigFlags](const ArrayRef<const SCEV *> Ops) {
    return StrengthenNoWrapFlags(this, scAddExpr, Ops, OrigFlags);
  };

  // Limit recursion calls depth.
  if (Depth > MaxArithDepth || hasHugeExpression(Ops))
    return getOrCreateAddExpr(Ops, ComputeFlags(Ops));

  if (SCEV *S = findExistingSCEVInCache(scAddExpr, Ops)) {
    // Don't strengthen flags if we have no new information.
    SCEVAddExpr *Add = static_cast<SCEVAddExpr *>(S);
    if (Add->getNoWrapFlags(OrigFlags) != OrigFlags)
      Add->setNoWrapFlags(ComputeFlags(Ops));
    return S;
  }

  // Okay, check to see if the same value occurs in the operand list more than
  // once.  If so, merge them together into an multiply expression.  Since we
  // sorted the list, these values are required to be adjacent.
  Type *Ty = Ops[0]->getType();
  bool FoundMatch = false;
  for (unsigned i = 0, e = Ops.size(); i != e-1; ++i)
    if (Ops[i] == Ops[i+1]) {      //  X + Y + Y  -->  X + Y*2
      // Scan ahead to count how many equal operands there are.
      unsigned Count = 2;
      while (i+Count != e && Ops[i+Count] == Ops[i])
        ++Count;
      // Merge the values into a multiply.
      const SCEV *Scale = getConstant(Ty, Count);
      const SCEV *Mul = getMulExpr(Scale, Ops[i], SCEV::FlagAnyWrap, Depth + 1);
      if (Ops.size() == Count)
        return Mul;
      Ops[i] = Mul;
      Ops.erase(Ops.begin()+i+1, Ops.begin()+i+Count);
      --i; e -= Count - 1;
      FoundMatch = true;
    }
  if (FoundMatch)
    return getAddExpr(Ops, OrigFlags, Depth + 1);

  // Check for truncates. If all the operands are truncated from the same
  // type, see if factoring out the truncate would permit the result to be
  // folded. eg., n*trunc(x) + m*trunc(y) --> trunc(trunc(m)*x + trunc(n)*y)
  // if the contents of the resulting outer trunc fold to something simple.
  auto FindTruncSrcType = [&]() -> Type * {
    // We're ultimately looking to fold an addrec of truncs and muls of only
    // constants and truncs, so if we find any other types of SCEV
    // as operands of the addrec then we bail and return nullptr here.
    // Otherwise, we return the type of the operand of a trunc that we find.
    if (auto *T = dyn_cast<SCEVTruncateExpr>(Ops[Idx]))
      return T->getOperand()->getType();
    if (const auto *Mul = dyn_cast<SCEVMulExpr>(Ops[Idx])) {
      const auto *LastOp = Mul->getOperand(Mul->getNumOperands() - 1);
      if (const auto *T = dyn_cast<SCEVTruncateExpr>(LastOp))
        return T->getOperand()->getType();
    }
    return nullptr;
  };
  if (auto *SrcType = FindTruncSrcType()) {
    SmallVector<const SCEV *, 8> LargeOps;
    bool Ok = true;
    // Check all the operands to see if they can be represented in the
    // source type of the truncate.
    for (const SCEV *Op : Ops) {
      if (const SCEVTruncateExpr *T = dyn_cast<SCEVTruncateExpr>(Op)) {
        if (T->getOperand()->getType() != SrcType) {
          Ok = false;
          break;
        }
        LargeOps.push_back(T->getOperand());
      } else if (const SCEVConstant *C = dyn_cast<SCEVConstant>(Op)) {
        LargeOps.push_back(getAnyExtendExpr(C, SrcType));
      } else if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(Op)) {
        SmallVector<const SCEV *, 8> LargeMulOps;
        for (unsigned j = 0, f = M->getNumOperands(); j != f && Ok; ++j) {
          if (const SCEVTruncateExpr *T =
                dyn_cast<SCEVTruncateExpr>(M->getOperand(j))) {
            if (T->getOperand()->getType() != SrcType) {
              Ok = false;
              break;
            }
            LargeMulOps.push_back(T->getOperand());
          } else if (const auto *C = dyn_cast<SCEVConstant>(M->getOperand(j))) {
            LargeMulOps.push_back(getAnyExtendExpr(C, SrcType));
          } else {
            Ok = false;
            break;
          }
        }
        if (Ok)
          LargeOps.push_back(getMulExpr(LargeMulOps, SCEV::FlagAnyWrap, Depth + 1));
      } else {
        Ok = false;
        break;
      }
    }
    if (Ok) {
      // Evaluate the expression in the larger type.
      const SCEV *Fold = getAddExpr(LargeOps, SCEV::FlagAnyWrap, Depth + 1);
      // If it folds to something simple, use it. Otherwise, don't.
      if (isa<SCEVConstant>(Fold) || isa<SCEVUnknown>(Fold))
        return getTruncateExpr(Fold, Ty);
    }
  }

  if (Ops.size() == 2) {
    // Check if we have an expression of the form ((X + C1) - C2), where C1 and
    // C2 can be folded in a way that allows retaining wrapping flags of (X +
    // C1).
    const SCEV *A = Ops[0];
    const SCEV *B = Ops[1];
    auto *AddExpr = dyn_cast<SCEVAddExpr>(B);
    auto *C = dyn_cast<SCEVConstant>(A);
    if (AddExpr && C && isa<SCEVConstant>(AddExpr->getOperand(0))) {
      auto C1 = cast<SCEVConstant>(AddExpr->getOperand(0))->getAPInt();
      auto C2 = C->getAPInt();
      SCEV::NoWrapFlags PreservedFlags = SCEV::FlagAnyWrap;

      APInt ConstAdd = C1 + C2;
      auto AddFlags = AddExpr->getNoWrapFlags();
      // Adding a smaller constant is NUW if the original AddExpr was NUW.
      if (ScalarEvolution::hasFlags(AddFlags, SCEV::FlagNUW) &&
          ConstAdd.ule(C1)) {
        PreservedFlags =
            ScalarEvolution::setFlags(PreservedFlags, SCEV::FlagNUW);
      }

      // Adding a constant with the same sign and small magnitude is NSW, if the
      // original AddExpr was NSW.
      if (ScalarEvolution::hasFlags(AddFlags, SCEV::FlagNSW) &&
          C1.isSignBitSet() == ConstAdd.isSignBitSet() &&
          ConstAdd.abs().ule(C1.abs())) {
        PreservedFlags =
            ScalarEvolution::setFlags(PreservedFlags, SCEV::FlagNSW);
      }

      if (PreservedFlags != SCEV::FlagAnyWrap) {
        SmallVector<const SCEV *, 4> NewOps(AddExpr->operands());
        NewOps[0] = getConstant(ConstAdd);
        return getAddExpr(NewOps, PreservedFlags);
      }
    }
  }

  // Canonicalize (-1 * urem X, Y) + X --> (Y * X/Y)
  if (Ops.size() == 2) {
    const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(Ops[0]);
    if (Mul && Mul->getNumOperands() == 2 &&
        Mul->getOperand(0)->isAllOnesValue()) {
      const SCEV *X;
      const SCEV *Y;
      if (matchURem(Mul->getOperand(1), X, Y) && X == Ops[1]) {
        return getMulExpr(Y, getUDivExpr(X, Y));
      }
    }
  }

  // Skip past any other cast SCEVs.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scAddExpr)
    ++Idx;

  // If there are add operands they would be next.
  if (Idx < Ops.size()) {
    bool DeletedAdd = false;
    // If the original flags and all inlined SCEVAddExprs are NUW, use the
    // common NUW flag for expression after inlining. Other flags cannot be
    // preserved, because they may depend on the original order of operations.
    SCEV::NoWrapFlags CommonFlags = maskFlags(OrigFlags, SCEV::FlagNUW);
    while (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Ops[Idx])) {
      if (Ops.size() > AddOpsInlineThreshold ||
          Add->getNumOperands() > AddOpsInlineThreshold)
        break;
      // If we have an add, expand the add operands onto the end of the operands
      // list.
      Ops.erase(Ops.begin()+Idx);
      append_range(Ops, Add->operands());
      DeletedAdd = true;
      CommonFlags = maskFlags(CommonFlags, Add->getNoWrapFlags());
    }

    // If we deleted at least one add, we added operands to the end of the list,
    // and they are not necessarily sorted.  Recurse to resort and resimplify
    // any operands we just acquired.
    if (DeletedAdd)
      return getAddExpr(Ops, CommonFlags, Depth + 1);
  }

  // Skip over the add expression until we get to a multiply.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scMulExpr)
    ++Idx;

  // Check to see if there are any folding opportunities present with
  // operands multiplied by constant values.
  if (Idx < Ops.size() && isa<SCEVMulExpr>(Ops[Idx])) {
    uint64_t BitWidth = getTypeSizeInBits(Ty);
    DenseMap<const SCEV *, APInt> M;
    SmallVector<const SCEV *, 8> NewOps;
    APInt AccumulatedConstant(BitWidth, 0);
    if (CollectAddOperandsWithScales(M, NewOps, AccumulatedConstant,
                                     Ops, APInt(BitWidth, 1), *this)) {
      struct APIntCompare {
        bool operator()(const APInt &LHS, const APInt &RHS) const {
          return LHS.ult(RHS);
        }
      };

      // Some interesting folding opportunity is present, so its worthwhile to
      // re-generate the operands list. Group the operands by constant scale,
      // to avoid multiplying by the same constant scale multiple times.
      std::map<APInt, SmallVector<const SCEV *, 4>, APIntCompare> MulOpLists;
      for (const SCEV *NewOp : NewOps)
        MulOpLists[M.find(NewOp)->second].push_back(NewOp);
      // Re-generate the operands list.
      Ops.clear();
      if (AccumulatedConstant != 0)
        Ops.push_back(getConstant(AccumulatedConstant));
      for (auto &MulOp : MulOpLists) {
        if (MulOp.first == 1) {
          Ops.push_back(getAddExpr(MulOp.second, SCEV::FlagAnyWrap, Depth + 1));
        } else if (MulOp.first != 0) {
          Ops.push_back(getMulExpr(
              getConstant(MulOp.first),
              getAddExpr(MulOp.second, SCEV::FlagAnyWrap, Depth + 1),
              SCEV::FlagAnyWrap, Depth + 1));
        }
      }
      if (Ops.empty())
        return getZero(Ty);
      if (Ops.size() == 1)
        return Ops[0];
      return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
    }
  }

  // If we are adding something to a multiply expression, make sure the
  // something is not already an operand of the multiply.  If so, merge it into
  // the multiply.
  for (; Idx < Ops.size() && isa<SCEVMulExpr>(Ops[Idx]); ++Idx) {
    const SCEVMulExpr *Mul = cast<SCEVMulExpr>(Ops[Idx]);
    for (unsigned MulOp = 0, e = Mul->getNumOperands(); MulOp != e; ++MulOp) {
      const SCEV *MulOpSCEV = Mul->getOperand(MulOp);
      if (isa<SCEVConstant>(MulOpSCEV))
        continue;
      for (unsigned AddOp = 0, e = Ops.size(); AddOp != e; ++AddOp)
        if (MulOpSCEV == Ops[AddOp]) {
          // Fold W + X + (X * Y * Z)  -->  W + (X * ((Y*Z)+1))
          const SCEV *InnerMul = Mul->getOperand(MulOp == 0);
          if (Mul->getNumOperands() != 2) {
            // If the multiply has more than two operands, we must get the
            // Y*Z term.
            SmallVector<const SCEV *, 4> MulOps(
                Mul->operands().take_front(MulOp));
            append_range(MulOps, Mul->operands().drop_front(MulOp + 1));
            InnerMul = getMulExpr(MulOps, SCEV::FlagAnyWrap, Depth + 1);
          }
          SmallVector<const SCEV *, 2> TwoOps = {getOne(Ty), InnerMul};
          const SCEV *AddOne = getAddExpr(TwoOps, SCEV::FlagAnyWrap, Depth + 1);
          const SCEV *OuterMul = getMulExpr(AddOne, MulOpSCEV,
                                            SCEV::FlagAnyWrap, Depth + 1);
          if (Ops.size() == 2) return OuterMul;
          if (AddOp < Idx) {
            Ops.erase(Ops.begin()+AddOp);
            Ops.erase(Ops.begin()+Idx-1);
          } else {
            Ops.erase(Ops.begin()+Idx);
            Ops.erase(Ops.begin()+AddOp-1);
          }
          Ops.push_back(OuterMul);
          return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
        }

      // Check this multiply against other multiplies being added together.
      for (unsigned OtherMulIdx = Idx+1;
           OtherMulIdx < Ops.size() && isa<SCEVMulExpr>(Ops[OtherMulIdx]);
           ++OtherMulIdx) {
        const SCEVMulExpr *OtherMul = cast<SCEVMulExpr>(Ops[OtherMulIdx]);
        // If MulOp occurs in OtherMul, we can fold the two multiplies
        // together.
        for (unsigned OMulOp = 0, e = OtherMul->getNumOperands();
             OMulOp != e; ++OMulOp)
          if (OtherMul->getOperand(OMulOp) == MulOpSCEV) {
            // Fold X + (A*B*C) + (A*D*E) --> X + (A*(B*C+D*E))
            const SCEV *InnerMul1 = Mul->getOperand(MulOp == 0);
            if (Mul->getNumOperands() != 2) {
              SmallVector<const SCEV *, 4> MulOps(
                  Mul->operands().take_front(MulOp));
              append_range(MulOps, Mul->operands().drop_front(MulOp+1));
              InnerMul1 = getMulExpr(MulOps, SCEV::FlagAnyWrap, Depth + 1);
            }
            const SCEV *InnerMul2 = OtherMul->getOperand(OMulOp == 0);
            if (OtherMul->getNumOperands() != 2) {
              SmallVector<const SCEV *, 4> MulOps(
                  OtherMul->operands().take_front(OMulOp));
              append_range(MulOps, OtherMul->operands().drop_front(OMulOp+1));
              InnerMul2 = getMulExpr(MulOps, SCEV::FlagAnyWrap, Depth + 1);
            }
            SmallVector<const SCEV *, 2> TwoOps = {InnerMul1, InnerMul2};
            const SCEV *InnerMulSum =
                getAddExpr(TwoOps, SCEV::FlagAnyWrap, Depth + 1);
            const SCEV *OuterMul = getMulExpr(MulOpSCEV, InnerMulSum,
                                              SCEV::FlagAnyWrap, Depth + 1);
            if (Ops.size() == 2) return OuterMul;
            Ops.erase(Ops.begin()+Idx);
            Ops.erase(Ops.begin()+OtherMulIdx-1);
            Ops.push_back(OuterMul);
            return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
          }
      }
    }
  }

  // If there are any add recurrences in the operands list, see if any other
  // added values are loop invariant.  If so, we can fold them into the
  // recurrence.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scAddRecExpr)
    ++Idx;

  // Scan over all recurrences, trying to fold loop invariants into them.
  for (; Idx < Ops.size() && isa<SCEVAddRecExpr>(Ops[Idx]); ++Idx) {
    // Scan all of the other operands to this add and add them to the vector if
    // they are loop invariant w.r.t. the recurrence.
    SmallVector<const SCEV *, 8> LIOps;
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(Ops[Idx]);
    const Loop *AddRecLoop = AddRec->getLoop();
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      if (isAvailableAtLoopEntry(Ops[i], AddRecLoop)) {
        LIOps.push_back(Ops[i]);
        Ops.erase(Ops.begin()+i);
        --i; --e;
      }

    // If we found some loop invariants, fold them into the recurrence.
    if (!LIOps.empty()) {
      // Compute nowrap flags for the addition of the loop-invariant ops and
      // the addrec. Temporarily push it as an operand for that purpose. These
      // flags are valid in the scope of the addrec only.
      LIOps.push_back(AddRec);
      SCEV::NoWrapFlags Flags = ComputeFlags(LIOps);
      LIOps.pop_back();

      //  NLI + LI + {Start,+,Step}  -->  NLI + {LI+Start,+,Step}
      LIOps.push_back(AddRec->getStart());

      SmallVector<const SCEV *, 4> AddRecOps(AddRec->operands());

      // It is not in general safe to propagate flags valid on an add within
      // the addrec scope to one outside it.  We must prove that the inner
      // scope is guaranteed to execute if the outer one does to be able to
      // safely propagate.  We know the program is undefined if poison is
      // produced on the inner scoped addrec.  We also know that *for this use*
      // the outer scoped add can't overflow (because of the flags we just
      // computed for the inner scoped add) without the program being undefined.
      // Proving that entry to the outer scope neccesitates entry to the inner
      // scope, thus proves the program undefined if the flags would be violated
      // in the outer scope.
      SCEV::NoWrapFlags AddFlags = Flags;
      if (AddFlags != SCEV::FlagAnyWrap) {
        auto *DefI = getDefiningScopeBound(LIOps);
        auto *ReachI = &*AddRecLoop->getHeader()->begin();
        if (!isGuaranteedToTransferExecutionTo(DefI, ReachI))
          AddFlags = SCEV::FlagAnyWrap;
      }
      AddRecOps[0] = getAddExpr(LIOps, AddFlags, Depth + 1);

      // Build the new addrec. Propagate the NUW and NSW flags if both the
      // outer add and the inner addrec are guaranteed to have no overflow.
      // Always propagate NW.
      Flags = AddRec->getNoWrapFlags(setFlags(Flags, SCEV::FlagNW));
      const SCEV *NewRec = getAddRecExpr(AddRecOps, AddRecLoop, Flags);

      // If all of the other operands were loop invariant, we are done.
      if (Ops.size() == 1) return NewRec;

      // Otherwise, add the folded AddRec by the non-invariant parts.
      for (unsigned i = 0;; ++i)
        if (Ops[i] == AddRec) {
          Ops[i] = NewRec;
          break;
        }
      return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
    }

    // Okay, if there weren't any loop invariants to be folded, check to see if
    // there are multiple AddRec's with the same loop induction variable being
    // added together.  If so, we can fold them.
    for (unsigned OtherIdx = Idx+1;
         OtherIdx < Ops.size() && isa<SCEVAddRecExpr>(Ops[OtherIdx]);
         ++OtherIdx) {
      // We expect the AddRecExpr's to be sorted in reverse dominance order,
      // so that the 1st found AddRecExpr is dominated by all others.
      assert(DT.dominates(
           cast<SCEVAddRecExpr>(Ops[OtherIdx])->getLoop()->getHeader(),
           AddRec->getLoop()->getHeader()) &&
        "AddRecExprs are not sorted in reverse dominance order?");
      if (AddRecLoop == cast<SCEVAddRecExpr>(Ops[OtherIdx])->getLoop()) {
        // Other + {A,+,B}<L> + {C,+,D}<L>  -->  Other + {A+C,+,B+D}<L>
        SmallVector<const SCEV *, 4> AddRecOps(AddRec->operands());
        for (; OtherIdx != Ops.size() && isa<SCEVAddRecExpr>(Ops[OtherIdx]);
             ++OtherIdx) {
          const auto *OtherAddRec = cast<SCEVAddRecExpr>(Ops[OtherIdx]);
          if (OtherAddRec->getLoop() == AddRecLoop) {
            for (unsigned i = 0, e = OtherAddRec->getNumOperands();
                 i != e; ++i) {
              if (i >= AddRecOps.size()) {
                append_range(AddRecOps, OtherAddRec->operands().drop_front(i));
                break;
              }
              SmallVector<const SCEV *, 2> TwoOps = {
                  AddRecOps[i], OtherAddRec->getOperand(i)};
              AddRecOps[i] = getAddExpr(TwoOps, SCEV::FlagAnyWrap, Depth + 1);
            }
            Ops.erase(Ops.begin() + OtherIdx); --OtherIdx;
          }
        }
        // Step size has changed, so we cannot guarantee no self-wraparound.
        Ops[Idx] = getAddRecExpr(AddRecOps, AddRecLoop, SCEV::FlagAnyWrap);
        return getAddExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
      }
    }

    // Otherwise couldn't fold anything into this recurrence.  Move onto the
    // next one.
  }

  // Okay, it looks like we really DO need an add expr.  Check to see if we
  // already have one, otherwise create a new one.
  return getOrCreateAddExpr(Ops, ComputeFlags(Ops));
}

const SCEV *
ScalarEvolution::getOrCreateAddExpr(ArrayRef<const SCEV *> Ops,
                                    SCEV::NoWrapFlags Flags) {
  FoldingSetNodeID ID;
  ID.AddInteger(scAddExpr);
  for (const SCEV *Op : Ops)
    ID.AddPointer(Op);
  void *IP = nullptr;
  SCEVAddExpr *S =
      static_cast<SCEVAddExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));
  if (!S) {
    const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
    std::uninitialized_copy(Ops.begin(), Ops.end(), O);
    S = new (SCEVAllocator)
        SCEVAddExpr(ID.Intern(SCEVAllocator), O, Ops.size());
    UniqueSCEVs.InsertNode(S, IP);
    registerUser(S, Ops);
  }
  S->setNoWrapFlags(Flags);
  return S;
}

const SCEV *
ScalarEvolution::getOrCreateAddRecExpr(ArrayRef<const SCEV *> Ops,
                                       const Loop *L, SCEV::NoWrapFlags Flags) {
  FoldingSetNodeID ID;
  ID.AddInteger(scAddRecExpr);
  for (const SCEV *Op : Ops)
    ID.AddPointer(Op);
  ID.AddPointer(L);
  void *IP = nullptr;
  SCEVAddRecExpr *S =
      static_cast<SCEVAddRecExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));
  if (!S) {
    const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
    std::uninitialized_copy(Ops.begin(), Ops.end(), O);
    S = new (SCEVAllocator)
        SCEVAddRecExpr(ID.Intern(SCEVAllocator), O, Ops.size(), L);
    UniqueSCEVs.InsertNode(S, IP);
    LoopUsers[L].push_back(S);
    registerUser(S, Ops);
  }
  setNoWrapFlags(S, Flags);
  return S;
}

const SCEV *
ScalarEvolution::getOrCreateMulExpr(ArrayRef<const SCEV *> Ops,
                                    SCEV::NoWrapFlags Flags) {
  FoldingSetNodeID ID;
  ID.AddInteger(scMulExpr);
  for (const SCEV *Op : Ops)
    ID.AddPointer(Op);
  void *IP = nullptr;
  SCEVMulExpr *S =
    static_cast<SCEVMulExpr *>(UniqueSCEVs.FindNodeOrInsertPos(ID, IP));
  if (!S) {
    const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
    std::uninitialized_copy(Ops.begin(), Ops.end(), O);
    S = new (SCEVAllocator) SCEVMulExpr(ID.Intern(SCEVAllocator),
                                        O, Ops.size());
    UniqueSCEVs.InsertNode(S, IP);
    registerUser(S, Ops);
  }
  S->setNoWrapFlags(Flags);
  return S;
}

static uint64_t umul_ov(uint64_t i, uint64_t j, bool &Overflow) {
  uint64_t k = i*j;
  if (j > 1 && k / j != i) Overflow = true;
  return k;
}

/// Compute the result of "n choose k", the binomial coefficient.  If an
/// intermediate computation overflows, Overflow will be set and the return will
/// be garbage. Overflow is not cleared on absence of overflow.
static uint64_t Choose(uint64_t n, uint64_t k, bool &Overflow) {
  // We use the multiplicative formula:
  //     n(n-1)(n-2)...(n-(k-1)) / k(k-1)(k-2)...1 .
  // At each iteration, we take the n-th term of the numeral and divide by the
  // (k-n)th term of the denominator.  This division will always produce an
  // integral result, and helps reduce the chance of overflow in the
  // intermediate computations. However, we can still overflow even when the
  // final result would fit.

  if (n == 0 || n == k) return 1;
  if (k > n) return 0;

  if (k > n/2)
    k = n-k;

  uint64_t r = 1;
  for (uint64_t i = 1; i <= k; ++i) {
    r = umul_ov(r, n-(i-1), Overflow);
    r /= i;
  }
  return r;
}

/// Determine if any of the operands in this SCEV are a constant or if
/// any of the add or multiply expressions in this SCEV contain a constant.
static bool containsConstantInAddMulChain(const SCEV *StartExpr) {
  struct FindConstantInAddMulChain {
    bool FoundConstant = false;

    bool follow(const SCEV *S) {
      FoundConstant |= isa<SCEVConstant>(S);
      return isa<SCEVAddExpr>(S) || isa<SCEVMulExpr>(S);
    }

    bool isDone() const {
      return FoundConstant;
    }
  };

  FindConstantInAddMulChain F;
  SCEVTraversal<FindConstantInAddMulChain> ST(F);
  ST.visitAll(StartExpr);
  return F.FoundConstant;
}

/// Get a canonical multiply expression, or something simpler if possible.
const SCEV *ScalarEvolution::getMulExpr(SmallVectorImpl<const SCEV *> &Ops,
                                        SCEV::NoWrapFlags OrigFlags,
                                        unsigned Depth) {
  assert(OrigFlags == maskFlags(OrigFlags, SCEV::FlagNUW | SCEV::FlagNSW) &&
         "only nuw or nsw allowed");
  assert(!Ops.empty() && "Cannot get empty mul!");
  if (Ops.size() == 1) return Ops[0];
#ifndef NDEBUG
  Type *ETy = Ops[0]->getType();
  assert(!ETy->isPointerTy());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i)
    assert(Ops[i]->getType() == ETy &&
           "SCEVMulExpr operand types don't match!");
#endif

  // Sort by complexity, this groups all similar expression types together.
  GroupByComplexity(Ops, &LI, DT);

  // If there are any constants, fold them together.
  unsigned Idx = 0;
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {
    ++Idx;
    assert(Idx < Ops.size());
    while (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(Ops[Idx])) {
      // We found two constants, fold them together!
      Ops[0] = getConstant(LHSC->getAPInt() * RHSC->getAPInt());
      if (Ops.size() == 2) return Ops[0];
      Ops.erase(Ops.begin()+1);  // Erase the folded element
      LHSC = cast<SCEVConstant>(Ops[0]);
    }

    // If we have a multiply of zero, it will always be zero.
    if (LHSC->getValue()->isZero())
      return LHSC;

    // If we are left with a constant one being multiplied, strip it off.
    if (LHSC->getValue()->isOne()) {
      Ops.erase(Ops.begin());
      --Idx;
    }

    if (Ops.size() == 1)
      return Ops[0];
  }

  // Delay expensive flag strengthening until necessary.
  auto ComputeFlags = [this, OrigFlags](const ArrayRef<const SCEV *> Ops) {
    return StrengthenNoWrapFlags(this, scMulExpr, Ops, OrigFlags);
  };

  // Limit recursion calls depth.
  if (Depth > MaxArithDepth || hasHugeExpression(Ops))
    return getOrCreateMulExpr(Ops, ComputeFlags(Ops));

  if (SCEV *S = findExistingSCEVInCache(scMulExpr, Ops)) {
    // Don't strengthen flags if we have no new information.
    SCEVMulExpr *Mul = static_cast<SCEVMulExpr *>(S);
    if (Mul->getNoWrapFlags(OrigFlags) != OrigFlags)
      Mul->setNoWrapFlags(ComputeFlags(Ops));
    return S;
  }

  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {
    if (Ops.size() == 2) {
      // C1*(C2+V) -> C1*C2 + C1*V
      if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Ops[1]))
        // If any of Add's ops are Adds or Muls with a constant, apply this
        // transformation as well.
        //
        // TODO: There are some cases where this transformation is not
        // profitable; for example, Add = (C0 + X) * Y + Z.  Maybe the scope of
        // this transformation should be narrowed down.
        if (Add->getNumOperands() == 2 && containsConstantInAddMulChain(Add)) {
          const SCEV *LHS = getMulExpr(LHSC, Add->getOperand(0),
                                       SCEV::FlagAnyWrap, Depth + 1);
          const SCEV *RHS = getMulExpr(LHSC, Add->getOperand(1),
                                       SCEV::FlagAnyWrap, Depth + 1);
          return getAddExpr(LHS, RHS, SCEV::FlagAnyWrap, Depth + 1);
        }

      if (Ops[0]->isAllOnesValue()) {
        // If we have a mul by -1 of an add, try distributing the -1 among the
        // add operands.
        if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Ops[1])) {
          SmallVector<const SCEV *, 4> NewOps;
          bool AnyFolded = false;
          for (const SCEV *AddOp : Add->operands()) {
            const SCEV *Mul = getMulExpr(Ops[0], AddOp, SCEV::FlagAnyWrap,
                                         Depth + 1);
            if (!isa<SCEVMulExpr>(Mul)) AnyFolded = true;
            NewOps.push_back(Mul);
          }
          if (AnyFolded)
            return getAddExpr(NewOps, SCEV::FlagAnyWrap, Depth + 1);
        } else if (const auto *AddRec = dyn_cast<SCEVAddRecExpr>(Ops[1])) {
          // Negation preserves a recurrence's no self-wrap property.
          SmallVector<const SCEV *, 4> Operands;
          for (const SCEV *AddRecOp : AddRec->operands())
            Operands.push_back(getMulExpr(Ops[0], AddRecOp, SCEV::FlagAnyWrap,
                                          Depth + 1));
          // Let M be the minimum representable signed value. AddRec with nsw
          // multiplied by -1 can have signed overflow if and only if it takes a
          // value of M: M * (-1) would stay M and (M + 1) * (-1) would be the
          // maximum signed value. In all other cases signed overflow is
          // impossible.
          auto FlagsMask = SCEV::FlagNW;
          if (hasFlags(AddRec->getNoWrapFlags(), SCEV::FlagNSW)) {
            auto MinInt =
                APInt::getSignedMinValue(getTypeSizeInBits(AddRec->getType()));
            if (getSignedRangeMin(AddRec) != MinInt)
              FlagsMask = setFlags(FlagsMask, SCEV::FlagNSW);
          }
          return getAddRecExpr(Operands, AddRec->getLoop(),
                               AddRec->getNoWrapFlags(FlagsMask));
        }
      }
    }
  }

  // Skip over the add expression until we get to a multiply.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scMulExpr)
    ++Idx;

  // If there are mul operands inline them all into this expression.
  if (Idx < Ops.size()) {
    bool DeletedMul = false;
    while (const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(Ops[Idx])) {
      if (Ops.size() > MulOpsInlineThreshold)
        break;
      // If we have an mul, expand the mul operands onto the end of the
      // operands list.
      Ops.erase(Ops.begin()+Idx);
      append_range(Ops, Mul->operands());
      DeletedMul = true;
    }

    // If we deleted at least one mul, we added operands to the end of the
    // list, and they are not necessarily sorted.  Recurse to resort and
    // resimplify any operands we just acquired.
    if (DeletedMul)
      return getMulExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
  }

  // If there are any add recurrences in the operands list, see if any other
  // added values are loop invariant.  If so, we can fold them into the
  // recurrence.
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < scAddRecExpr)
    ++Idx;

  // Scan over all recurrences, trying to fold loop invariants into them.
  for (; Idx < Ops.size() && isa<SCEVAddRecExpr>(Ops[Idx]); ++Idx) {
    // Scan all of the other operands to this mul and add them to the vector
    // if they are loop invariant w.r.t. the recurrence.
    SmallVector<const SCEV *, 8> LIOps;
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(Ops[Idx]);
    for (unsigned i = 0, e = Ops.size(); i != e; ++i)
      if (isAvailableAtLoopEntry(Ops[i], AddRec->getLoop())) {
        LIOps.push_back(Ops[i]);
        Ops.erase(Ops.begin()+i);
        --i; --e;
      }

    // If we found some loop invariants, fold them into the recurrence.
    if (!LIOps.empty()) {
      //  NLI * LI * {Start,+,Step}  -->  NLI * {LI*Start,+,LI*Step}
      SmallVector<const SCEV *, 4> NewOps;
      NewOps.reserve(AddRec->getNumOperands());
      const SCEV *Scale = getMulExpr(LIOps, SCEV::FlagAnyWrap, Depth + 1);

      // If both the mul and addrec are nuw, we can preserve nuw.
      // If both the mul and addrec are nsw, we can only preserve nsw if either
      // a) they are also nuw, or
      // b) all multiplications of addrec operands with scale are nsw.
      SCEV::NoWrapFlags Flags =
          AddRec->getNoWrapFlags(ComputeFlags({Scale, AddRec}));

      for (unsigned i = 0, e = AddRec->getNumOperands(); i != e; ++i) {
        NewOps.push_back(getMulExpr(Scale, AddRec->getOperand(i),
                                    SCEV::FlagAnyWrap, Depth + 1));

        if (hasFlags(Flags, SCEV::FlagNSW) && !hasFlags(Flags, SCEV::FlagNUW)) {
          ConstantRange NSWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
              Instruction::Mul, getSignedRange(Scale),
              OverflowingBinaryOperator::NoSignedWrap);
          if (!NSWRegion.contains(getSignedRange(AddRec->getOperand(i))))
            Flags = clearFlags(Flags, SCEV::FlagNSW);
        }
      }

      const SCEV *NewRec = getAddRecExpr(NewOps, AddRec->getLoop(), Flags);

      // If all of the other operands were loop invariant, we are done.
      if (Ops.size() == 1) return NewRec;

      // Otherwise, multiply the folded AddRec by the non-invariant parts.
      for (unsigned i = 0;; ++i)
        if (Ops[i] == AddRec) {
          Ops[i] = NewRec;
          break;
        }
      return getMulExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);
    }

    // Okay, if there weren't any loop invariants to be folded, check to see
    // if there are multiple AddRec's with the same loop induction variable
    // being multiplied together.  If so, we can fold them.

    // {A1,+,A2,+,...,+,An}<L> * {B1,+,B2,+,...,+,Bn}<L>
    // = {x=1 in [ sum y=x..2x [ sum z=max(y-x, y-n)..min(x,n) [
    //       choose(x, 2x)*choose(2x-y, x-z)*A_{y-z}*B_z
    //   ]]],+,...up to x=2n}.
    // Note that the arguments to choose() are always integers with values
    // known at compile time, never SCEV objects.
    //
    // The implementation avoids pointless extra computations when the two
    // addrec's are of different length (mathematically, it's equivalent to
    // an infinite stream of zeros on the right).
    bool OpsModified = false;
    for (unsigned OtherIdx = Idx+1;
         OtherIdx != Ops.size() && isa<SCEVAddRecExpr>(Ops[OtherIdx]);
         ++OtherIdx) {
      const SCEVAddRecExpr *OtherAddRec =
        dyn_cast<SCEVAddRecExpr>(Ops[OtherIdx]);
      if (!OtherAddRec || OtherAddRec->getLoop() != AddRec->getLoop())
        continue;

      // Limit max number of arguments to avoid creation of unreasonably big
      // SCEVAddRecs with very complex operands.
      if (AddRec->getNumOperands() + OtherAddRec->getNumOperands() - 1 >
          MaxAddRecSize || hasHugeExpression({AddRec, OtherAddRec}))
        continue;

      bool Overflow = false;
      Type *Ty = AddRec->getType();
      bool LargerThan64Bits = getTypeSizeInBits(Ty) > 64;
      SmallVector<const SCEV*, 7> AddRecOps;
      for (int x = 0, xe = AddRec->getNumOperands() +
             OtherAddRec->getNumOperands() - 1; x != xe && !Overflow; ++x) {
        SmallVector <const SCEV *, 7> SumOps;
        for (int y = x, ye = 2*x+1; y != ye && !Overflow; ++y) {
          uint64_t Coeff1 = Choose(x, 2*x - y, Overflow);
          for (int z = std::max(y-x, y-(int)AddRec->getNumOperands()+1),
                 ze = std::min(x+1, (int)OtherAddRec->getNumOperands());
               z < ze && !Overflow; ++z) {
            uint64_t Coeff2 = Choose(2*x - y, x-z, Overflow);
            uint64_t Coeff;
            if (LargerThan64Bits)
              Coeff = umul_ov(Coeff1, Coeff2, Overflow);
            else
              Coeff = Coeff1*Coeff2;
            const SCEV *CoeffTerm = getConstant(Ty, Coeff);
            const SCEV *Term1 = AddRec->getOperand(y-z);
            const SCEV *Term2 = OtherAddRec->getOperand(z);
            SumOps.push_back(getMulExpr(CoeffTerm, Term1, Term2,
                                        SCEV::FlagAnyWrap, Depth + 1));
          }
        }
        if (SumOps.empty())
          SumOps.push_back(getZero(Ty));
        AddRecOps.push_back(getAddExpr(SumOps, SCEV::FlagAnyWrap, Depth + 1));
      }
      if (!Overflow) {
        const SCEV *NewAddRec = getAddRecExpr(AddRecOps, AddRec->getLoop(),
                                              SCEV::FlagAnyWrap);
        if (Ops.size() == 2) return NewAddRec;
        Ops[Idx] = NewAddRec;
        Ops.erase(Ops.begin() + OtherIdx); --OtherIdx;
        OpsModified = true;
        AddRec = dyn_cast<SCEVAddRecExpr>(NewAddRec);
        if (!AddRec)
          break;
      }
    }
    if (OpsModified)
      return getMulExpr(Ops, SCEV::FlagAnyWrap, Depth + 1);

    // Otherwise couldn't fold anything into this recurrence.  Move onto the
    // next one.
  }

  // Okay, it looks like we really DO need an mul expr.  Check to see if we
  // already have one, otherwise create a new one.
  return getOrCreateMulExpr(Ops, ComputeFlags(Ops));
}

/// Represents an unsigned remainder expression based on unsigned division.
const SCEV *ScalarEvolution::getURemExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  assert(getEffectiveSCEVType(LHS->getType()) ==
         getEffectiveSCEVType(RHS->getType()) &&
         "SCEVURemExpr operand types don't match!");

  // Short-circuit easy cases
  if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS)) {
    // If constant is one, the result is trivial
    if (RHSC->getValue()->isOne())
      return getZero(LHS->getType()); // X urem 1 --> 0

    // If constant is a power of two, fold into a zext(trunc(LHS)).
    if (RHSC->getAPInt().isPowerOf2()) {
      Type *FullTy = LHS->getType();
      Type *TruncTy =
          IntegerType::get(getContext(), RHSC->getAPInt().logBase2());
      return getZeroExtendExpr(getTruncateExpr(LHS, TruncTy), FullTy);
    }
  }

  // Fallback to %a == %x urem %y == %x -<nuw> ((%x udiv %y) *<nuw> %y)
  const SCEV *UDiv = getUDivExpr(LHS, RHS);
  const SCEV *Mult = getMulExpr(UDiv, RHS, SCEV::FlagNUW);
  return getMinusSCEV(LHS, Mult, SCEV::FlagNUW);
}

/// Get a canonical unsigned division expression, or something simpler if
/// possible.
const SCEV *ScalarEvolution::getUDivExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  assert(!LHS->getType()->isPointerTy() &&
         "SCEVUDivExpr operand can't be pointer!");
  assert(LHS->getType() == RHS->getType() &&
         "SCEVUDivExpr operand types don't match!");

  FoldingSetNodeID ID;
  ID.AddInteger(scUDivExpr);
  ID.AddPointer(LHS);
  ID.AddPointer(RHS);
  void *IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP))
    return S;

  // 0 udiv Y == 0
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(LHS))
    if (LHSC->getValue()->isZero())
      return LHS;

  if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS)) {
    if (RHSC->getValue()->isOne())
      return LHS;                               // X udiv 1 --> x
    // If the denominator is zero, the result of the udiv is undefined. Don't
    // try to analyze it, because the resolution chosen here may differ from
    // the resolution chosen in other parts of the compiler.
    if (!RHSC->getValue()->isZero()) {
      // Determine if the division can be folded into the operands of
      // its operands.
      // TODO: Generalize this to non-constants by using known-bits information.
      Type *Ty = LHS->getType();
      unsigned LZ = RHSC->getAPInt().countl_zero();
      unsigned MaxShiftAmt = getTypeSizeInBits(Ty) - LZ - 1;
      // For non-power-of-two values, effectively round the value up to the
      // nearest power of two.
      if (!RHSC->getAPInt().isPowerOf2())
        ++MaxShiftAmt;
      IntegerType *ExtTy =
        IntegerType::get(getContext(), getTypeSizeInBits(Ty) + MaxShiftAmt);
      if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(LHS))
        if (const SCEVConstant *Step =
            dyn_cast<SCEVConstant>(AR->getStepRecurrence(*this))) {
          // {X,+,N}/C --> {X/C,+,N/C} if safe and N/C can be folded.
          const APInt &StepInt = Step->getAPInt();
          const APInt &DivInt = RHSC->getAPInt();
          if (!StepInt.urem(DivInt) &&
              getZeroExtendExpr(AR, ExtTy) ==
              getAddRecExpr(getZeroExtendExpr(AR->getStart(), ExtTy),
                            getZeroExtendExpr(Step, ExtTy),
                            AR->getLoop(), SCEV::FlagAnyWrap)) {
            SmallVector<const SCEV *, 4> Operands;
            for (const SCEV *Op : AR->operands())
              Operands.push_back(getUDivExpr(Op, RHS));
            return getAddRecExpr(Operands, AR->getLoop(), SCEV::FlagNW);
          }
          /// Get a canonical UDivExpr for a recurrence.
          /// {X,+,N}/C => {Y,+,N}/C where Y=X-(X%N). Safe when C%N=0.
          // We can currently only fold X%N if X is constant.
          const SCEVConstant *StartC = dyn_cast<SCEVConstant>(AR->getStart());
          if (StartC && !DivInt.urem(StepInt) &&
              getZeroExtendExpr(AR, ExtTy) ==
              getAddRecExpr(getZeroExtendExpr(AR->getStart(), ExtTy),
                            getZeroExtendExpr(Step, ExtTy),
                            AR->getLoop(), SCEV::FlagAnyWrap)) {
            const APInt &StartInt = StartC->getAPInt();
            const APInt &StartRem = StartInt.urem(StepInt);
            if (StartRem != 0) {
              const SCEV *NewLHS =
                  getAddRecExpr(getConstant(StartInt - StartRem), Step,
                                AR->getLoop(), SCEV::FlagNW);
              if (LHS != NewLHS) {
                LHS = NewLHS;

                // Reset the ID to include the new LHS, and check if it is
                // already cached.
                ID.clear();
                ID.AddInteger(scUDivExpr);
                ID.AddPointer(LHS);
                ID.AddPointer(RHS);
                IP = nullptr;
                if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP))
                  return S;
              }
            }
          }
        }
      // (A*B)/C --> A*(B/C) if safe and B/C can be folded.
      if (const SCEVMulExpr *M = dyn_cast<SCEVMulExpr>(LHS)) {
        SmallVector<const SCEV *, 4> Operands;
        for (const SCEV *Op : M->operands())
          Operands.push_back(getZeroExtendExpr(Op, ExtTy));
        if (getZeroExtendExpr(M, ExtTy) == getMulExpr(Operands))
          // Find an operand that's safely divisible.
          for (unsigned i = 0, e = M->getNumOperands(); i != e; ++i) {
            const SCEV *Op = M->getOperand(i);
            const SCEV *Div = getUDivExpr(Op, RHSC);
            if (!isa<SCEVUDivExpr>(Div) && getMulExpr(Div, RHSC) == Op) {
              Operands = SmallVector<const SCEV *, 4>(M->operands());
              Operands[i] = Div;
              return getMulExpr(Operands);
            }
          }
      }

      // (A/B)/C --> A/(B*C) if safe and B*C can be folded.
      if (const SCEVUDivExpr *OtherDiv = dyn_cast<SCEVUDivExpr>(LHS)) {
        if (auto *DivisorConstant =
                dyn_cast<SCEVConstant>(OtherDiv->getRHS())) {
          bool Overflow = false;
          APInt NewRHS =
              DivisorConstant->getAPInt().umul_ov(RHSC->getAPInt(), Overflow);
          if (Overflow) {
            return getConstant(RHSC->getType(), 0, false);
          }
          return getUDivExpr(OtherDiv->getLHS(), getConstant(NewRHS));
        }
      }

      // (A+B)/C --> (A/C + B/C) if safe and A/C and B/C can be folded.
      if (const SCEVAddExpr *A = dyn_cast<SCEVAddExpr>(LHS)) {
        SmallVector<const SCEV *, 4> Operands;
        for (const SCEV *Op : A->operands())
          Operands.push_back(getZeroExtendExpr(Op, ExtTy));
        if (getZeroExtendExpr(A, ExtTy) == getAddExpr(Operands)) {
          Operands.clear();
          for (unsigned i = 0, e = A->getNumOperands(); i != e; ++i) {
            const SCEV *Op = getUDivExpr(A->getOperand(i), RHS);
            if (isa<SCEVUDivExpr>(Op) ||
                getMulExpr(Op, RHS) != A->getOperand(i))
              break;
            Operands.push_back(Op);
          }
          if (Operands.size() == A->getNumOperands())
            return getAddExpr(Operands);
        }
      }

      // Fold if both operands are constant.
      if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(LHS))
        return getConstant(LHSC->getAPInt().udiv(RHSC->getAPInt()));
    }
  }

  // The Insertion Point (IP) might be invalid by now (due to UniqueSCEVs
  // changes). Make sure we get a new one.
  IP = nullptr;
  if (const SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) return S;
  SCEV *S = new (SCEVAllocator) SCEVUDivExpr(ID.Intern(SCEVAllocator),
                                             LHS, RHS);
  UniqueSCEVs.InsertNode(S, IP);
  registerUser(S, {LHS, RHS});
  return S;
}

APInt gcd(const SCEVConstant *C1, const SCEVConstant *C2) {
  APInt A = C1->getAPInt().abs();
  APInt B = C2->getAPInt().abs();
  uint32_t ABW = A.getBitWidth();
  uint32_t BBW = B.getBitWidth();

  if (ABW > BBW)
    B = B.zext(ABW);
  else if (ABW < BBW)
    A = A.zext(BBW);

  return APIntOps::GreatestCommonDivisor(std::move(A), std::move(B));
}

/// Get a canonical unsigned division expression, or something simpler if
/// possible. There is no representation for an exact udiv in SCEV IR, but we
/// can attempt to remove factors from the LHS and RHS.  We can't do this when
/// it's not exact because the udiv may be clearing bits.
const SCEV *ScalarEvolution::getUDivExactExpr(const SCEV *LHS,
                                              const SCEV *RHS) {
  // TODO: we could try to find factors in all sorts of things, but for now we
  // just deal with u/exact (multiply, constant). See SCEVDivision towards the
  // end of this file for inspiration.

  const SCEVMulExpr *Mul = dyn_cast<SCEVMulExpr>(LHS);
  if (!Mul || !Mul->hasNoUnsignedWrap())
    return getUDivExpr(LHS, RHS);

  if (const SCEVConstant *RHSCst = dyn_cast<SCEVConstant>(RHS)) {
    // If the mulexpr multiplies by a constant, then that constant must be the
    // first element of the mulexpr.
    if (const auto *LHSCst = dyn_cast<SCEVConstant>(Mul->getOperand(0))) {
      if (LHSCst == RHSCst) {
        SmallVector<const SCEV *, 2> Operands(drop_begin(Mul->operands()));
        return getMulExpr(Operands);
      }

      // We can't just assume that LHSCst divides RHSCst cleanly, it could be
      // that there's a factor provided by one of the other terms. We need to
      // check.
      APInt Factor = gcd(LHSCst, RHSCst);
      if (!Factor.isIntN(1)) {
        LHSCst =
            cast<SCEVConstant>(getConstant(LHSCst->getAPInt().udiv(Factor)));
        RHSCst =
            cast<SCEVConstant>(getConstant(RHSCst->getAPInt().udiv(Factor)));
        SmallVector<const SCEV *, 2> Operands;
        Operands.push_back(LHSCst);
        append_range(Operands, Mul->operands().drop_front());
        LHS = getMulExpr(Operands);
        RHS = RHSCst;
        Mul = dyn_cast<SCEVMulExpr>(LHS);
        if (!Mul)
          return getUDivExactExpr(LHS, RHS);
      }
    }
  }

  for (int i = 0, e = Mul->getNumOperands(); i != e; ++i) {
    if (Mul->getOperand(i) == RHS) {
      SmallVector<const SCEV *, 2> Operands;
      append_range(Operands, Mul->operands().take_front(i));
      append_range(Operands, Mul->operands().drop_front(i + 1));
      return getMulExpr(Operands);
    }
  }

  return getUDivExpr(LHS, RHS);
}

/// Get an add recurrence expression for the specified loop.  Simplify the
/// expression as much as possible.
const SCEV *ScalarEvolution::getAddRecExpr(const SCEV *Start, const SCEV *Step,
                                           const Loop *L,
                                           SCEV::NoWrapFlags Flags) {
  SmallVector<const SCEV *, 4> Operands;
  Operands.push_back(Start);
  if (const SCEVAddRecExpr *StepChrec = dyn_cast<SCEVAddRecExpr>(Step))
    if (StepChrec->getLoop() == L) {
      append_range(Operands, StepChrec->operands());
      return getAddRecExpr(Operands, L, maskFlags(Flags, SCEV::FlagNW));
    }

  Operands.push_back(Step);
  return getAddRecExpr(Operands, L, Flags);
}

/// Get an add recurrence expression for the specified loop.  Simplify the
/// expression as much as possible.
const SCEV *
ScalarEvolution::getAddRecExpr(SmallVectorImpl<const SCEV *> &Operands,
                               const Loop *L, SCEV::NoWrapFlags Flags) {
  if (Operands.size() == 1) return Operands[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Operands[0]->getType());
  for (const SCEV *Op : llvm::drop_begin(Operands)) {
    assert(getEffectiveSCEVType(Op->getType()) == ETy &&
           "SCEVAddRecExpr operand types don't match!");
    assert(!Op->getType()->isPointerTy() && "Step must be integer");
  }
  for (const SCEV *Op : Operands)
    assert(isAvailableAtLoopEntry(Op, L) &&
           "SCEVAddRecExpr operand is not available at loop entry!");
#endif

  if (Operands.back()->isZero()) {
    Operands.pop_back();
    return getAddRecExpr(Operands, L, SCEV::FlagAnyWrap); // {X,+,0}  -->  X
  }

  // It's tempting to want to call getConstantMaxBackedgeTakenCount count here and
  // use that information to infer NUW and NSW flags. However, computing a
  // BE count requires calling getAddRecExpr, so we may not yet have a
  // meaningful BE count at this point (and if we don't, we'd be stuck
  // with a SCEVCouldNotCompute as the cached BE count).

  Flags = StrengthenNoWrapFlags(this, scAddRecExpr, Operands, Flags);

  // Canonicalize nested AddRecs in by nesting them in order of loop depth.
  if (const SCEVAddRecExpr *NestedAR = dyn_cast<SCEVAddRecExpr>(Operands[0])) {
    const Loop *NestedLoop = NestedAR->getLoop();
    if (L->contains(NestedLoop)
            ? (L->getLoopDepth() < NestedLoop->getLoopDepth())
            : (!NestedLoop->contains(L) &&
               DT.dominates(L->getHeader(), NestedLoop->getHeader()))) {
      SmallVector<const SCEV *, 4> NestedOperands(NestedAR->operands());
      Operands[0] = NestedAR->getStart();
      // AddRecs require their operands be loop-invariant with respect to their
      // loops. Don't perform this transformation if it would break this
      // requirement.
      bool AllInvariant = all_of(
          Operands, [&](const SCEV *Op) { return isLoopInvariant(Op, L); });

      if (AllInvariant) {
        // Create a recurrence for the outer loop with the same step size.
        //
        // The outer recurrence keeps its NW flag but only keeps NUW/NSW if the
        // inner recurrence has the same property.
        SCEV::NoWrapFlags OuterFlags =
          maskFlags(Flags, SCEV::FlagNW | NestedAR->getNoWrapFlags());

        NestedOperands[0] = getAddRecExpr(Operands, L, OuterFlags);
        AllInvariant = all_of(NestedOperands, [&](const SCEV *Op) {
          return isLoopInvariant(Op, NestedLoop);
        });

        if (AllInvariant) {
          // Ok, both add recurrences are valid after the transformation.
          //
          // The inner recurrence keeps its NW flag but only keeps NUW/NSW if
          // the outer recurrence has the same property.
          SCEV::NoWrapFlags InnerFlags =
            maskFlags(NestedAR->getNoWrapFlags(), SCEV::FlagNW | Flags);
          return getAddRecExpr(NestedOperands, NestedLoop, InnerFlags);
        }
      }
      // Reset Operands to its original state.
      Operands[0] = NestedAR;
    }
  }

  // Okay, it looks like we really DO need an addrec expr.  Check to see if we
  // already have one, otherwise create a new one.
  return getOrCreateAddRecExpr(Operands, L, Flags);
}

const SCEV *
ScalarEvolution::getGEPExpr(GEPOperator *GEP,
                            const SmallVectorImpl<const SCEV *> &IndexExprs) {
  const SCEV *BaseExpr = getSCEV(GEP->getPointerOperand());
  // getSCEV(Base)->getType() has the same address space as Base->getType()
  // because SCEV::getType() preserves the address space.
  Type *IntIdxTy = getEffectiveSCEVType(BaseExpr->getType());
  GEPNoWrapFlags NW = GEP->getNoWrapFlags();
  if (NW != GEPNoWrapFlags::none()) {
    // We'd like to propagate flags from the IR to the corresponding SCEV nodes,
    // but to do that, we have to ensure that said flag is valid in the entire
    // defined scope of the SCEV.
    // TODO: non-instructions have global scope.  We might be able to prove
    // some global scope cases
    auto *GEPI = dyn_cast<Instruction>(GEP);
    if (!GEPI || !isSCEVExprNeverPoison(GEPI))
      NW = GEPNoWrapFlags::none();
  }

  SCEV::NoWrapFlags OffsetWrap = SCEV::FlagAnyWrap;
  if (NW.hasNoUnsignedSignedWrap())
    OffsetWrap = setFlags(OffsetWrap, SCEV::FlagNSW);
  if (NW.hasNoUnsignedWrap())
    OffsetWrap = setFlags(OffsetWrap, SCEV::FlagNUW);

  Type *CurTy = GEP->getType();
  bool FirstIter = true;
  SmallVector<const SCEV *, 4> Offsets;
  for (const SCEV *IndexExpr : IndexExprs) {
    // Compute the (potentially symbolic) offset in bytes for this index.
    if (StructType *STy = dyn_cast<StructType>(CurTy)) {
      // For a struct, add the member offset.
      ConstantInt *Index = cast<SCEVConstant>(IndexExpr)->getValue();
      unsigned FieldNo = Index->getZExtValue();
      const SCEV *FieldOffset = getOffsetOfExpr(IntIdxTy, STy, FieldNo);
      Offsets.push_back(FieldOffset);

      // Update CurTy to the type of the field at Index.
      CurTy = STy->getTypeAtIndex(Index);
    } else {
      // Update CurTy to its element type.
      if (FirstIter) {
        assert(isa<PointerType>(CurTy) &&
               "The first index of a GEP indexes a pointer");
        CurTy = GEP->getSourceElementType();
        FirstIter = false;
      } else {
        CurTy = GetElementPtrInst::getTypeAtIndex(CurTy, (uint64_t)0);
      }
      // For an array, add the element offset, explicitly scaled.
      const SCEV *ElementSize = getSizeOfExpr(IntIdxTy, CurTy);
      // Getelementptr indices are signed.
      IndexExpr = getTruncateOrSignExtend(IndexExpr, IntIdxTy);

      // Multiply the index by the element size to compute the element offset.
      const SCEV *LocalOffset = getMulExpr(IndexExpr, ElementSize, OffsetWrap);
      Offsets.push_back(LocalOffset);
    }
  }

  // Handle degenerate case of GEP without offsets.
  if (Offsets.empty())
    return BaseExpr;

  // Add the offsets together, assuming nsw if inbounds.
  const SCEV *Offset = getAddExpr(Offsets, OffsetWrap);
  // Add the base address and the offset. We cannot use the nsw flag, as the
  // base address is unsigned. However, if we know that the offset is
  // non-negative, we can use nuw.
  bool NUW = NW.hasNoUnsignedWrap() ||
             (NW.hasNoUnsignedSignedWrap() && isKnownNonNegative(Offset));
  SCEV::NoWrapFlags BaseWrap = NUW ? SCEV::FlagNUW : SCEV::FlagAnyWrap;
  auto *GEPExpr = getAddExpr(BaseExpr, Offset, BaseWrap);
  assert(BaseExpr->getType() == GEPExpr->getType() &&
         "GEP should not change type mid-flight.");
  return GEPExpr;
}

SCEV *ScalarEvolution::findExistingSCEVInCache(SCEVTypes SCEVType,
                                               ArrayRef<const SCEV *> Ops) {
  FoldingSetNodeID ID;
  ID.AddInteger(SCEVType);
  for (const SCEV *Op : Ops)
    ID.AddPointer(Op);
  void *IP = nullptr;
  return UniqueSCEVs.FindNodeOrInsertPos(ID, IP);
}

const SCEV *ScalarEvolution::getAbsExpr(const SCEV *Op, bool IsNSW) {
  SCEV::NoWrapFlags Flags = IsNSW ? SCEV::FlagNSW : SCEV::FlagAnyWrap;
  return getSMaxExpr(Op, getNegativeSCEV(Op, Flags));
}

const SCEV *ScalarEvolution::getMinMaxExpr(SCEVTypes Kind,
                                           SmallVectorImpl<const SCEV *> &Ops) {
  assert(SCEVMinMaxExpr::isMinMaxType(Kind) && "Not a SCEVMinMaxExpr!");
  assert(!Ops.empty() && "Cannot get empty (u|s)(min|max)!");
  if (Ops.size() == 1) return Ops[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Ops[0]->getType());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i) {
    assert(getEffectiveSCEVType(Ops[i]->getType()) == ETy &&
           "Operand types don't match!");
    assert(Ops[0]->getType()->isPointerTy() ==
               Ops[i]->getType()->isPointerTy() &&
           "min/max should be consistently pointerish");
  }
#endif

  bool IsSigned = Kind == scSMaxExpr || Kind == scSMinExpr;
  bool IsMax = Kind == scSMaxExpr || Kind == scUMaxExpr;

  // Sort by complexity, this groups all similar expression types together.
  GroupByComplexity(Ops, &LI, DT);

  // Check if we have created the same expression before.
  if (const SCEV *S = findExistingSCEVInCache(Kind, Ops)) {
    return S;
  }

  // If there are any constants, fold them together.
  unsigned Idx = 0;
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(Ops[0])) {
    ++Idx;
    assert(Idx < Ops.size());
    auto FoldOp = [&](const APInt &LHS, const APInt &RHS) {
      switch (Kind) {
      case scSMaxExpr:
        return APIntOps::smax(LHS, RHS);
      case scSMinExpr:
        return APIntOps::smin(LHS, RHS);
      case scUMaxExpr:
        return APIntOps::umax(LHS, RHS);
      case scUMinExpr:
        return APIntOps::umin(LHS, RHS);
      default:
        llvm_unreachable("Unknown SCEV min/max opcode");
      }
    };

    while (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(Ops[Idx])) {
      // We found two constants, fold them together!
      ConstantInt *Fold = ConstantInt::get(
          getContext(), FoldOp(LHSC->getAPInt(), RHSC->getAPInt()));
      Ops[0] = getConstant(Fold);
      Ops.erase(Ops.begin()+1);  // Erase the folded element
      if (Ops.size() == 1) return Ops[0];
      LHSC = cast<SCEVConstant>(Ops[0]);
    }

    bool IsMinV = LHSC->getValue()->isMinValue(IsSigned);
    bool IsMaxV = LHSC->getValue()->isMaxValue(IsSigned);

    if (IsMax ? IsMinV : IsMaxV) {
      // If we are left with a constant minimum(/maximum)-int, strip it off.
      Ops.erase(Ops.begin());
      --Idx;
    } else if (IsMax ? IsMaxV : IsMinV) {
      // If we have a max(/min) with a constant maximum(/minimum)-int,
      // it will always be the extremum.
      return LHSC;
    }

    if (Ops.size() == 1) return Ops[0];
  }

  // Find the first operation of the same kind
  while (Idx < Ops.size() && Ops[Idx]->getSCEVType() < Kind)
    ++Idx;

  // Check to see if one of the operands is of the same kind. If so, expand its
  // operands onto our operand list, and recurse to simplify.
  if (Idx < Ops.size()) {
    bool DeletedAny = false;
    while (Ops[Idx]->getSCEVType() == Kind) {
      const SCEVMinMaxExpr *SMME = cast<SCEVMinMaxExpr>(Ops[Idx]);
      Ops.erase(Ops.begin()+Idx);
      append_range(Ops, SMME->operands());
      DeletedAny = true;
    }

    if (DeletedAny)
      return getMinMaxExpr(Kind, Ops);
  }

  // Okay, check to see if the same value occurs in the operand list twice.  If
  // so, delete one.  Since we sorted the list, these values are required to
  // be adjacent.
  llvm::CmpInst::Predicate GEPred =
      IsSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE;
  llvm::CmpInst::Predicate LEPred =
      IsSigned ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE;
  llvm::CmpInst::Predicate FirstPred = IsMax ? GEPred : LEPred;
  llvm::CmpInst::Predicate SecondPred = IsMax ? LEPred : GEPred;
  for (unsigned i = 0, e = Ops.size() - 1; i != e; ++i) {
    if (Ops[i] == Ops[i + 1] ||
        isKnownViaNonRecursiveReasoning(FirstPred, Ops[i], Ops[i + 1])) {
      //  X op Y op Y  -->  X op Y
      //  X op Y       -->  X, if we know X, Y are ordered appropriately
      Ops.erase(Ops.begin() + i + 1, Ops.begin() + i + 2);
      --i;
      --e;
    } else if (isKnownViaNonRecursiveReasoning(SecondPred, Ops[i],
                                               Ops[i + 1])) {
      //  X op Y       -->  Y, if we know X, Y are ordered appropriately
      Ops.erase(Ops.begin() + i, Ops.begin() + i + 1);
      --i;
      --e;
    }
  }

  if (Ops.size() == 1) return Ops[0];

  assert(!Ops.empty() && "Reduced smax down to nothing!");

  // Okay, it looks like we really DO need an expr.  Check to see if we
  // already have one, otherwise create a new one.
  FoldingSetNodeID ID;
  ID.AddInteger(Kind);
  for (const SCEV *Op : Ops)
    ID.AddPointer(Op);
  void *IP = nullptr;
  const SCEV *ExistingSCEV = UniqueSCEVs.FindNodeOrInsertPos(ID, IP);
  if (ExistingSCEV)
    return ExistingSCEV;
  const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
  std::uninitialized_copy(Ops.begin(), Ops.end(), O);
  SCEV *S = new (SCEVAllocator)
      SCEVMinMaxExpr(ID.Intern(SCEVAllocator), Kind, O, Ops.size());

  UniqueSCEVs.InsertNode(S, IP);
  registerUser(S, Ops);
  return S;
}

namespace {

class SCEVSequentialMinMaxDeduplicatingVisitor final
    : public SCEVVisitor<SCEVSequentialMinMaxDeduplicatingVisitor,
                         std::optional<const SCEV *>> {
  using RetVal = std::optional<const SCEV *>;
  using Base = SCEVVisitor<SCEVSequentialMinMaxDeduplicatingVisitor, RetVal>;

  ScalarEvolution &SE;
  const SCEVTypes RootKind; // Must be a sequential min/max expression.
  const SCEVTypes NonSequentialRootKind; // Non-sequential variant of RootKind.
  SmallPtrSet<const SCEV *, 16> SeenOps;

  bool canRecurseInto(SCEVTypes Kind) const {
    // We can only recurse into the SCEV expression of the same effective type
    // as the type of our root SCEV expression.
    return RootKind == Kind || NonSequentialRootKind == Kind;
  };

  RetVal visitAnyMinMaxExpr(const SCEV *S) {
    assert((isa<SCEVMinMaxExpr>(S) || isa<SCEVSequentialMinMaxExpr>(S)) &&
           "Only for min/max expressions.");
    SCEVTypes Kind = S->getSCEVType();

    if (!canRecurseInto(Kind))
      return S;

    auto *NAry = cast<SCEVNAryExpr>(S);
    SmallVector<const SCEV *> NewOps;
    bool Changed = visit(Kind, NAry->operands(), NewOps);

    if (!Changed)
      return S;
    if (NewOps.empty())
      return std::nullopt;

    return isa<SCEVSequentialMinMaxExpr>(S)
               ? SE.getSequentialMinMaxExpr(Kind, NewOps)
               : SE.getMinMaxExpr(Kind, NewOps);
  }

  RetVal visit(const SCEV *S) {
    // Has the whole operand been seen already?
    if (!SeenOps.insert(S).second)
      return std::nullopt;
    return Base::visit(S);
  }

public:
  SCEVSequentialMinMaxDeduplicatingVisitor(ScalarEvolution &SE,
                                           SCEVTypes RootKind)
      : SE(SE), RootKind(RootKind),
        NonSequentialRootKind(
            SCEVSequentialMinMaxExpr::getEquivalentNonSequentialSCEVType(
                RootKind)) {}

  bool /*Changed*/ visit(SCEVTypes Kind, ArrayRef<const SCEV *> OrigOps,
                         SmallVectorImpl<const SCEV *> &NewOps) {
    bool Changed = false;
    SmallVector<const SCEV *> Ops;
    Ops.reserve(OrigOps.size());

    for (const SCEV *Op : OrigOps) {
      RetVal NewOp = visit(Op);
      if (NewOp != Op)
        Changed = true;
      if (NewOp)
        Ops.emplace_back(*NewOp);
    }

    if (Changed)
      NewOps = std::move(Ops);
    return Changed;
  }

  RetVal visitConstant(const SCEVConstant *Constant) { return Constant; }

  RetVal visitVScale(const SCEVVScale *VScale) { return VScale; }

  RetVal visitPtrToIntExpr(const SCEVPtrToIntExpr *Expr) { return Expr; }

  RetVal visitTruncateExpr(const SCEVTruncateExpr *Expr) { return Expr; }

  RetVal visitZeroExtendExpr(const SCEVZeroExtendExpr *Expr) { return Expr; }

  RetVal visitSignExtendExpr(const SCEVSignExtendExpr *Expr) { return Expr; }

  RetVal visitAddExpr(const SCEVAddExpr *Expr) { return Expr; }

  RetVal visitMulExpr(const SCEVMulExpr *Expr) { return Expr; }

  RetVal visitUDivExpr(const SCEVUDivExpr *Expr) { return Expr; }

  RetVal visitAddRecExpr(const SCEVAddRecExpr *Expr) { return Expr; }

  RetVal visitSMaxExpr(const SCEVSMaxExpr *Expr) {
    return visitAnyMinMaxExpr(Expr);
  }

  RetVal visitUMaxExpr(const SCEVUMaxExpr *Expr) {
    return visitAnyMinMaxExpr(Expr);
  }

  RetVal visitSMinExpr(const SCEVSMinExpr *Expr) {
    return visitAnyMinMaxExpr(Expr);
  }

  RetVal visitUMinExpr(const SCEVUMinExpr *Expr) {
    return visitAnyMinMaxExpr(Expr);
  }

  RetVal visitSequentialUMinExpr(const SCEVSequentialUMinExpr *Expr) {
    return visitAnyMinMaxExpr(Expr);
  }

  RetVal visitUnknown(const SCEVUnknown *Expr) { return Expr; }

  RetVal visitCouldNotCompute(const SCEVCouldNotCompute *Expr) { return Expr; }
};

} // namespace

static bool scevUnconditionallyPropagatesPoisonFromOperands(SCEVTypes Kind) {
  switch (Kind) {
  case scConstant:
  case scVScale:
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
  case scPtrToInt:
  case scAddExpr:
  case scMulExpr:
  case scUDivExpr:
  case scAddRecExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scUnknown:
    // If any operand is poison, the whole expression is poison.
    return true;
  case scSequentialUMinExpr:
    // FIXME: if the *first* operand is poison, the whole expression is poison.
    return false; // Pessimistically, say that it does not propagate poison.
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

namespace {
// The only way poison may be introduced in a SCEV expression is from a
// poison SCEVUnknown (ConstantExprs are also represented as SCEVUnknown,
// not SCEVConstant). Notably, nowrap flags in SCEV nodes can *not*
// introduce poison -- they encode guaranteed, non-speculated knowledge.
//
// Additionally, all SCEV nodes propagate poison from inputs to outputs,
// with the notable exception of umin_seq, where only poison from the first
// operand is (unconditionally) propagated.
struct SCEVPoisonCollector {
  bool LookThroughMaybePoisonBlocking;
  SmallPtrSet<const SCEVUnknown *, 4> MaybePoison;
  SCEVPoisonCollector(bool LookThroughMaybePoisonBlocking)
      : LookThroughMaybePoisonBlocking(LookThroughMaybePoisonBlocking) {}

  bool follow(const SCEV *S) {
    if (!LookThroughMaybePoisonBlocking &&
        !scevUnconditionallyPropagatesPoisonFromOperands(S->getSCEVType()))
      return false;

    if (auto *SU = dyn_cast<SCEVUnknown>(S)) {
      if (!isGuaranteedNotToBePoison(SU->getValue()))
        MaybePoison.insert(SU);
    }
    return true;
  }
  bool isDone() const { return false; }
};
} // namespace

/// Return true if V is poison given that AssumedPoison is already poison.
static bool impliesPoison(const SCEV *AssumedPoison, const SCEV *S) {
  // First collect all SCEVs that might result in AssumedPoison to be poison.
  // We need to look through potentially poison-blocking operations here,
  // because we want to find all SCEVs that *might* result in poison, not only
  // those that are *required* to.
  SCEVPoisonCollector PC1(/* LookThroughMaybePoisonBlocking */ true);
  visitAll(AssumedPoison, PC1);

  // AssumedPoison is never poison. As the assumption is false, the implication
  // is true. Don't bother walking the other SCEV in this case.
  if (PC1.MaybePoison.empty())
    return true;

  // Collect all SCEVs in S that, if poison, *will* result in S being poison
  // as well. We cannot look through potentially poison-blocking operations
  // here, as their arguments only *may* make the result poison.
  SCEVPoisonCollector PC2(/* LookThroughMaybePoisonBlocking */ false);
  visitAll(S, PC2);

  // Make sure that no matter which SCEV in PC1.MaybePoison is actually poison,
  // it will also make S poison by being part of PC2.MaybePoison.
  return all_of(PC1.MaybePoison, [&](const SCEVUnknown *S) {
    return PC2.MaybePoison.contains(S);
  });
}

void ScalarEvolution::getPoisonGeneratingValues(
    SmallPtrSetImpl<const Value *> &Result, const SCEV *S) {
  SCEVPoisonCollector PC(/* LookThroughMaybePoisonBlocking */ false);
  visitAll(S, PC);
  for (const SCEVUnknown *SU : PC.MaybePoison)
    Result.insert(SU->getValue());
}

bool ScalarEvolution::canReuseInstruction(
    const SCEV *S, Instruction *I,
    SmallVectorImpl<Instruction *> &DropPoisonGeneratingInsts) {
  // If the instruction cannot be poison, it's always safe to reuse.
  if (programUndefinedIfPoison(I))
    return true;

  // Otherwise, it is possible that I is more poisonous that S. Collect the
  // poison-contributors of S, and then check whether I has any additional
  // poison-contributors. Poison that is contributed through poison-generating
  // flags is handled by dropping those flags instead.
  SmallPtrSet<const Value *, 8> PoisonVals;
  getPoisonGeneratingValues(PoisonVals, S);

  SmallVector<Value *> Worklist;
  SmallPtrSet<Value *, 8> Visited;
  Worklist.push_back(I);
  while (!Worklist.empty()) {
    Value *V = Worklist.pop_back_val();
    if (!Visited.insert(V).second)
      continue;

    // Avoid walking large instruction graphs.
    if (Visited.size() > 16)
      return false;

    // Either the value can't be poison, or the S would also be poison if it
    // is.
    if (PoisonVals.contains(V) || isGuaranteedNotToBePoison(V))
      continue;

    auto *I = dyn_cast<Instruction>(V);
    if (!I)
      return false;

    // Disjoint or instructions are interpreted as adds by SCEV. However, we
    // can't replace an arbitrary add with disjoint or, even if we drop the
    // flag. We would need to convert the or into an add.
    if (auto *PDI = dyn_cast<PossiblyDisjointInst>(I))
      if (PDI->isDisjoint())
        return false;

    // FIXME: Ignore vscale, even though it technically could be poison. Do this
    // because SCEV currently assumes it can't be poison. Remove this special
    // case once we proper model when vscale can be poison.
    if (auto *II = dyn_cast<IntrinsicInst>(I);
        II && II->getIntrinsicID() == Intrinsic::vscale)
      continue;

    if (canCreatePoison(cast<Operator>(I), /*ConsiderFlagsAndMetadata*/ false))
      return false;

    // If the instruction can't create poison, we can recurse to its operands.
    if (I->hasPoisonGeneratingAnnotations())
      DropPoisonGeneratingInsts.push_back(I);

    for (Value *Op : I->operands())
      Worklist.push_back(Op);
  }
  return true;
}

const SCEV *
ScalarEvolution::getSequentialMinMaxExpr(SCEVTypes Kind,
                                         SmallVectorImpl<const SCEV *> &Ops) {
  assert(SCEVSequentialMinMaxExpr::isSequentialMinMaxType(Kind) &&
         "Not a SCEVSequentialMinMaxExpr!");
  assert(!Ops.empty() && "Cannot get empty (u|s)(min|max)!");
  if (Ops.size() == 1)
    return Ops[0];
#ifndef NDEBUG
  Type *ETy = getEffectiveSCEVType(Ops[0]->getType());
  for (unsigned i = 1, e = Ops.size(); i != e; ++i) {
    assert(getEffectiveSCEVType(Ops[i]->getType()) == ETy &&
           "Operand types don't match!");
    assert(Ops[0]->getType()->isPointerTy() ==
               Ops[i]->getType()->isPointerTy() &&
           "min/max should be consistently pointerish");
  }
#endif

  // Note that SCEVSequentialMinMaxExpr is *NOT* commutative,
  // so we can *NOT* do any kind of sorting of the expressions!

  // Check if we have created the same expression before.
  if (const SCEV *S = findExistingSCEVInCache(Kind, Ops))
    return S;

  // FIXME: there are *some* simplifications that we can do here.

  // Keep only the first instance of an operand.
  {
    SCEVSequentialMinMaxDeduplicatingVisitor Deduplicator(*this, Kind);
    bool Changed = Deduplicator.visit(Kind, Ops, Ops);
    if (Changed)
      return getSequentialMinMaxExpr(Kind, Ops);
  }

  // Check to see if one of the operands is of the same kind. If so, expand its
  // operands onto our operand list, and recurse to simplify.
  {
    unsigned Idx = 0;
    bool DeletedAny = false;
    while (Idx < Ops.size()) {
      if (Ops[Idx]->getSCEVType() != Kind) {
        ++Idx;
        continue;
      }
      const auto *SMME = cast<SCEVSequentialMinMaxExpr>(Ops[Idx]);
      Ops.erase(Ops.begin() + Idx);
      Ops.insert(Ops.begin() + Idx, SMME->operands().begin(),
                 SMME->operands().end());
      DeletedAny = true;
    }

    if (DeletedAny)
      return getSequentialMinMaxExpr(Kind, Ops);
  }

  const SCEV *SaturationPoint;
  ICmpInst::Predicate Pred;
  switch (Kind) {
  case scSequentialUMinExpr:
    SaturationPoint = getZero(Ops[0]->getType());
    Pred = ICmpInst::ICMP_ULE;
    break;
  default:
    llvm_unreachable("Not a sequential min/max type.");
  }

  for (unsigned i = 1, e = Ops.size(); i != e; ++i) {
    // We can replace %x umin_seq %y with %x umin %y if either:
    //  * %y being poison implies %x is also poison.
    //  * %x cannot be the saturating value (e.g. zero for umin).
    if (::impliesPoison(Ops[i], Ops[i - 1]) ||
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_NE, Ops[i - 1],
                                        SaturationPoint)) {
      SmallVector<const SCEV *> SeqOps = {Ops[i - 1], Ops[i]};
      Ops[i - 1] = getMinMaxExpr(
          SCEVSequentialMinMaxExpr::getEquivalentNonSequentialSCEVType(Kind),
          SeqOps);
      Ops.erase(Ops.begin() + i);
      return getSequentialMinMaxExpr(Kind, Ops);
    }
    // Fold %x umin_seq %y to %x if %x ule %y.
    // TODO: We might be able to prove the predicate for a later operand.
    if (isKnownViaNonRecursiveReasoning(Pred, Ops[i - 1], Ops[i])) {
      Ops.erase(Ops.begin() + i);
      return getSequentialMinMaxExpr(Kind, Ops);
    }
  }

  // Okay, it looks like we really DO need an expr.  Check to see if we
  // already have one, otherwise create a new one.
  FoldingSetNodeID ID;
  ID.AddInteger(Kind);
  for (const SCEV *Op : Ops)
    ID.AddPointer(Op);
  void *IP = nullptr;
  const SCEV *ExistingSCEV = UniqueSCEVs.FindNodeOrInsertPos(ID, IP);
  if (ExistingSCEV)
    return ExistingSCEV;

  const SCEV **O = SCEVAllocator.Allocate<const SCEV *>(Ops.size());
  std::uninitialized_copy(Ops.begin(), Ops.end(), O);
  SCEV *S = new (SCEVAllocator)
      SCEVSequentialMinMaxExpr(ID.Intern(SCEVAllocator), Kind, O, Ops.size());

  UniqueSCEVs.InsertNode(S, IP);
  registerUser(S, Ops);
  return S;
}

const SCEV *ScalarEvolution::getSMaxExpr(const SCEV *LHS, const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = {LHS, RHS};
  return getSMaxExpr(Ops);
}

const SCEV *ScalarEvolution::getSMaxExpr(SmallVectorImpl<const SCEV *> &Ops) {
  return getMinMaxExpr(scSMaxExpr, Ops);
}

const SCEV *ScalarEvolution::getUMaxExpr(const SCEV *LHS, const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = {LHS, RHS};
  return getUMaxExpr(Ops);
}

const SCEV *ScalarEvolution::getUMaxExpr(SmallVectorImpl<const SCEV *> &Ops) {
  return getMinMaxExpr(scUMaxExpr, Ops);
}

const SCEV *ScalarEvolution::getSMinExpr(const SCEV *LHS,
                                         const SCEV *RHS) {
  SmallVector<const SCEV *, 2> Ops = { LHS, RHS };
  return getSMinExpr(Ops);
}

const SCEV *ScalarEvolution::getSMinExpr(SmallVectorImpl<const SCEV *> &Ops) {
  return getMinMaxExpr(scSMinExpr, Ops);
}

const SCEV *ScalarEvolution::getUMinExpr(const SCEV *LHS, const SCEV *RHS,
                                         bool Sequential) {
  SmallVector<const SCEV *, 2> Ops = { LHS, RHS };
  return getUMinExpr(Ops, Sequential);
}

const SCEV *ScalarEvolution::getUMinExpr(SmallVectorImpl<const SCEV *> &Ops,
                                         bool Sequential) {
  return Sequential ? getSequentialMinMaxExpr(scSequentialUMinExpr, Ops)
                    : getMinMaxExpr(scUMinExpr, Ops);
}

const SCEV *
ScalarEvolution::getSizeOfExpr(Type *IntTy, TypeSize Size) {
  const SCEV *Res = getConstant(IntTy, Size.getKnownMinValue());
  if (Size.isScalable())
    Res = getMulExpr(Res, getVScale(IntTy));
  return Res;
}

const SCEV *ScalarEvolution::getSizeOfExpr(Type *IntTy, Type *AllocTy) {
  return getSizeOfExpr(IntTy, getDataLayout().getTypeAllocSize(AllocTy));
}

const SCEV *ScalarEvolution::getStoreSizeOfExpr(Type *IntTy, Type *StoreTy) {
  return getSizeOfExpr(IntTy, getDataLayout().getTypeStoreSize(StoreTy));
}

const SCEV *ScalarEvolution::getOffsetOfExpr(Type *IntTy,
                                             StructType *STy,
                                             unsigned FieldNo) {
  // We can bypass creating a target-independent constant expression and then
  // folding it back into a ConstantInt. This is just a compile-time
  // optimization.
  const StructLayout *SL = getDataLayout().getStructLayout(STy);
  assert(!SL->getSizeInBits().isScalable() &&
         "Cannot get offset for structure containing scalable vector types");
  return getConstant(IntTy, SL->getElementOffset(FieldNo));
}

const SCEV *ScalarEvolution::getUnknown(Value *V) {
  // Don't attempt to do anything other than create a SCEVUnknown object
  // here.  createSCEV only calls getUnknown after checking for all other
  // interesting possibilities, and any other code that calls getUnknown
  // is doing so in order to hide a value from SCEV canonicalization.

  FoldingSetNodeID ID;
  ID.AddInteger(scUnknown);
  ID.AddPointer(V);
  void *IP = nullptr;
  if (SCEV *S = UniqueSCEVs.FindNodeOrInsertPos(ID, IP)) {
    assert(cast<SCEVUnknown>(S)->getValue() == V &&
           "Stale SCEVUnknown in uniquing map!");
    return S;
  }
  SCEV *S = new (SCEVAllocator) SCEVUnknown(ID.Intern(SCEVAllocator), V, this,
                                            FirstUnknown);
  FirstUnknown = cast<SCEVUnknown>(S);
  UniqueSCEVs.InsertNode(S, IP);
  return S;
}

//===----------------------------------------------------------------------===//
//            Basic SCEV Analysis and PHI Idiom Recognition Code
//

/// Test if values of the given type are analyzable within the SCEV
/// framework. This primarily includes integer types, and it can optionally
/// include pointer types if the ScalarEvolution class has access to
/// target-specific information.
bool ScalarEvolution::isSCEVable(Type *Ty) const {
  // Integers and pointers are always SCEVable.
  return Ty->isIntOrPtrTy();
}

/// Return the size in bits of the specified type, for which isSCEVable must
/// return true.
uint64_t ScalarEvolution::getTypeSizeInBits(Type *Ty) const {
  assert(isSCEVable(Ty) && "Type is not SCEVable!");
  if (Ty->isPointerTy())
    return getDataLayout().getIndexTypeSizeInBits(Ty);
  return getDataLayout().getTypeSizeInBits(Ty);
}

/// Return a type with the same bitwidth as the given type and which represents
/// how SCEV will treat the given type, for which isSCEVable must return
/// true. For pointer types, this is the pointer index sized integer type.
Type *ScalarEvolution::getEffectiveSCEVType(Type *Ty) const {
  assert(isSCEVable(Ty) && "Type is not SCEVable!");

  if (Ty->isIntegerTy())
    return Ty;

  // The only other support type is pointer.
  assert(Ty->isPointerTy() && "Unexpected non-pointer non-integer type!");
  return getDataLayout().getIndexType(Ty);
}

Type *ScalarEvolution::getWiderType(Type *T1, Type *T2) const {
  return  getTypeSizeInBits(T1) >= getTypeSizeInBits(T2) ? T1 : T2;
}

bool ScalarEvolution::instructionCouldExistWithOperands(const SCEV *A,
                                                        const SCEV *B) {
  /// For a valid use point to exist, the defining scope of one operand
  /// must dominate the other.
  bool PreciseA, PreciseB;
  auto *ScopeA = getDefiningScopeBound({A}, PreciseA);
  auto *ScopeB = getDefiningScopeBound({B}, PreciseB);
  if (!PreciseA || !PreciseB)
    // Can't tell.
    return false;
  return (ScopeA == ScopeB) || DT.dominates(ScopeA, ScopeB) ||
    DT.dominates(ScopeB, ScopeA);
}

const SCEV *ScalarEvolution::getCouldNotCompute() {
  return CouldNotCompute.get();
}

bool ScalarEvolution::checkValidity(const SCEV *S) const {
  bool ContainsNulls = SCEVExprContains(S, [](const SCEV *S) {
    auto *SU = dyn_cast<SCEVUnknown>(S);
    return SU && SU->getValue() == nullptr;
  });

  return !ContainsNulls;
}

bool ScalarEvolution::containsAddRecurrence(const SCEV *S) {
  HasRecMapType::iterator I = HasRecMap.find(S);
  if (I != HasRecMap.end())
    return I->second;

  bool FoundAddRec =
      SCEVExprContains(S, [](const SCEV *S) { return isa<SCEVAddRecExpr>(S); });
  HasRecMap.insert({S, FoundAddRec});
  return FoundAddRec;
}

/// Return the ValueOffsetPair set for \p S. \p S can be represented
/// by the value and offset from any ValueOffsetPair in the set.
ArrayRef<Value *> ScalarEvolution::getSCEVValues(const SCEV *S) {
  ExprValueMapType::iterator SI = ExprValueMap.find_as(S);
  if (SI == ExprValueMap.end())
    return std::nullopt;
  return SI->second.getArrayRef();
}

/// Erase Value from ValueExprMap and ExprValueMap. ValueExprMap.erase(V)
/// cannot be used separately. eraseValueFromMap should be used to remove
/// V from ValueExprMap and ExprValueMap at the same time.
void ScalarEvolution::eraseValueFromMap(Value *V) {
  ValueExprMapType::iterator I = ValueExprMap.find_as(V);
  if (I != ValueExprMap.end()) {
    auto EVIt = ExprValueMap.find(I->second);
    bool Removed = EVIt->second.remove(V);
    (void) Removed;
    assert(Removed && "Value not in ExprValueMap?");
    ValueExprMap.erase(I);
  }
}

void ScalarEvolution::insertValueToMap(Value *V, const SCEV *S) {
  // A recursive query may have already computed the SCEV. It should be
  // equivalent, but may not necessarily be exactly the same, e.g. due to lazily
  // inferred nowrap flags.
  auto It = ValueExprMap.find_as(V);
  if (It == ValueExprMap.end()) {
    ValueExprMap.insert({SCEVCallbackVH(V, this), S});
    ExprValueMap[S].insert(V);
  }
}

/// Return an existing SCEV if it exists, otherwise analyze the expression and
/// create a new one.
const SCEV *ScalarEvolution::getSCEV(Value *V) {
  assert(isSCEVable(V->getType()) && "Value is not SCEVable!");

  if (const SCEV *S = getExistingSCEV(V))
    return S;
  return createSCEVIter(V);
}

const SCEV *ScalarEvolution::getExistingSCEV(Value *V) {
  assert(isSCEVable(V->getType()) && "Value is not SCEVable!");

  ValueExprMapType::iterator I = ValueExprMap.find_as(V);
  if (I != ValueExprMap.end()) {
    const SCEV *S = I->second;
    assert(checkValidity(S) &&
           "existing SCEV has not been properly invalidated");
    return S;
  }
  return nullptr;
}

/// Return a SCEV corresponding to -V = -1*V
const SCEV *ScalarEvolution::getNegativeSCEV(const SCEV *V,
                                             SCEV::NoWrapFlags Flags) {
  if (const SCEVConstant *VC = dyn_cast<SCEVConstant>(V))
    return getConstant(
               cast<ConstantInt>(ConstantExpr::getNeg(VC->getValue())));

  Type *Ty = V->getType();
  Ty = getEffectiveSCEVType(Ty);
  return getMulExpr(V, getMinusOne(Ty), Flags);
}

/// If Expr computes ~A, return A else return nullptr
static const SCEV *MatchNotExpr(const SCEV *Expr) {
  const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(Expr);
  if (!Add || Add->getNumOperands() != 2 ||
      !Add->getOperand(0)->isAllOnesValue())
    return nullptr;

  const SCEVMulExpr *AddRHS = dyn_cast<SCEVMulExpr>(Add->getOperand(1));
  if (!AddRHS || AddRHS->getNumOperands() != 2 ||
      !AddRHS->getOperand(0)->isAllOnesValue())
    return nullptr;

  return AddRHS->getOperand(1);
}

/// Return a SCEV corresponding to ~V = -1-V
const SCEV *ScalarEvolution::getNotSCEV(const SCEV *V) {
  assert(!V->getType()->isPointerTy() && "Can't negate pointer");

  if (const SCEVConstant *VC = dyn_cast<SCEVConstant>(V))
    return getConstant(
                cast<ConstantInt>(ConstantExpr::getNot(VC->getValue())));

  // Fold ~(u|s)(min|max)(~x, ~y) to (u|s)(max|min)(x, y)
  if (const SCEVMinMaxExpr *MME = dyn_cast<SCEVMinMaxExpr>(V)) {
    auto MatchMinMaxNegation = [&](const SCEVMinMaxExpr *MME) {
      SmallVector<const SCEV *, 2> MatchedOperands;
      for (const SCEV *Operand : MME->operands()) {
        const SCEV *Matched = MatchNotExpr(Operand);
        if (!Matched)
          return (const SCEV *)nullptr;
        MatchedOperands.push_back(Matched);
      }
      return getMinMaxExpr(SCEVMinMaxExpr::negate(MME->getSCEVType()),
                           MatchedOperands);
    };
    if (const SCEV *Replaced = MatchMinMaxNegation(MME))
      return Replaced;
  }

  Type *Ty = V->getType();
  Ty = getEffectiveSCEVType(Ty);
  return getMinusSCEV(getMinusOne(Ty), V);
}

const SCEV *ScalarEvolution::removePointerBase(const SCEV *P) {
  assert(P->getType()->isPointerTy());

  if (auto *AddRec = dyn_cast<SCEVAddRecExpr>(P)) {
    // The base of an AddRec is the first operand.
    SmallVector<const SCEV *> Ops{AddRec->operands()};
    Ops[0] = removePointerBase(Ops[0]);
    // Don't try to transfer nowrap flags for now. We could in some cases
    // (for example, if pointer operand of the AddRec is a SCEVUnknown).
    return getAddRecExpr(Ops, AddRec->getLoop(), SCEV::FlagAnyWrap);
  }
  if (auto *Add = dyn_cast<SCEVAddExpr>(P)) {
    // The base of an Add is the pointer operand.
    SmallVector<const SCEV *> Ops{Add->operands()};
    const SCEV **PtrOp = nullptr;
    for (const SCEV *&AddOp : Ops) {
      if (AddOp->getType()->isPointerTy()) {
        assert(!PtrOp && "Cannot have multiple pointer ops");
        PtrOp = &AddOp;
      }
    }
    *PtrOp = removePointerBase(*PtrOp);
    // Don't try to transfer nowrap flags for now. We could in some cases
    // (for example, if the pointer operand of the Add is a SCEVUnknown).
    return getAddExpr(Ops);
  }
  // Any other expression must be a pointer base.
  return getZero(P->getType());
}

const SCEV *ScalarEvolution::getMinusSCEV(const SCEV *LHS, const SCEV *RHS,
                                          SCEV::NoWrapFlags Flags,
                                          unsigned Depth) {
  // Fast path: X - X --> 0.
  if (LHS == RHS)
    return getZero(LHS->getType());

  // If we subtract two pointers with different pointer bases, bail.
  // Eventually, we're going to add an assertion to getMulExpr that we
  // can't multiply by a pointer.
  if (RHS->getType()->isPointerTy()) {
    if (!LHS->getType()->isPointerTy() ||
        getPointerBase(LHS) != getPointerBase(RHS))
      return getCouldNotCompute();
    LHS = removePointerBase(LHS);
    RHS = removePointerBase(RHS);
  }

  // We represent LHS - RHS as LHS + (-1)*RHS. This transformation
  // makes it so that we cannot make much use of NUW.
  auto AddFlags = SCEV::FlagAnyWrap;
  const bool RHSIsNotMinSigned =
      !getSignedRangeMin(RHS).isMinSignedValue();
  if (hasFlags(Flags, SCEV::FlagNSW)) {
    // Let M be the minimum representable signed value. Then (-1)*RHS
    // signed-wraps if and only if RHS is M. That can happen even for
    // a NSW subtraction because e.g. (-1)*M signed-wraps even though
    // -1 - M does not. So to transfer NSW from LHS - RHS to LHS +
    // (-1)*RHS, we need to prove that RHS != M.
    //
    // If LHS is non-negative and we know that LHS - RHS does not
    // signed-wrap, then RHS cannot be M. So we can rule out signed-wrap
    // either by proving that RHS > M or that LHS >= 0.
    if (RHSIsNotMinSigned || isKnownNonNegative(LHS)) {
      AddFlags = SCEV::FlagNSW;
    }
  }

  // FIXME: Find a correct way to transfer NSW to (-1)*M when LHS -
  // RHS is NSW and LHS >= 0.
  //
  // The difficulty here is that the NSW flag may have been proven
  // relative to a loop that is to be found in a recurrence in LHS and
  // not in RHS. Applying NSW to (-1)*M may then let the NSW have a
  // larger scope than intended.
  auto NegFlags = RHSIsNotMinSigned ? SCEV::FlagNSW : SCEV::FlagAnyWrap;

  return getAddExpr(LHS, getNegativeSCEV(RHS, NegFlags), AddFlags, Depth);
}

const SCEV *ScalarEvolution::getTruncateOrZeroExtend(const SCEV *V, Type *Ty,
                                                     unsigned Depth) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate or zero extend with non-integer arguments!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  if (getTypeSizeInBits(SrcTy) > getTypeSizeInBits(Ty))
    return getTruncateExpr(V, Ty, Depth);
  return getZeroExtendExpr(V, Ty, Depth);
}

const SCEV *ScalarEvolution::getTruncateOrSignExtend(const SCEV *V, Type *Ty,
                                                     unsigned Depth) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate or zero extend with non-integer arguments!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  if (getTypeSizeInBits(SrcTy) > getTypeSizeInBits(Ty))
    return getTruncateExpr(V, Ty, Depth);
  return getSignExtendExpr(V, Ty, Depth);
}

const SCEV *
ScalarEvolution::getNoopOrZeroExtend(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot noop or zero extend with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) <= getTypeSizeInBits(Ty) &&
         "getNoopOrZeroExtend cannot truncate!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getZeroExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getNoopOrSignExtend(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot noop or sign extend with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) <= getTypeSizeInBits(Ty) &&
         "getNoopOrSignExtend cannot truncate!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getSignExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getNoopOrAnyExtend(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot noop or any extend with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) <= getTypeSizeInBits(Ty) &&
         "getNoopOrAnyExtend cannot truncate!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getAnyExtendExpr(V, Ty);
}

const SCEV *
ScalarEvolution::getTruncateOrNoop(const SCEV *V, Type *Ty) {
  Type *SrcTy = V->getType();
  assert(SrcTy->isIntOrPtrTy() && Ty->isIntOrPtrTy() &&
         "Cannot truncate or noop with non-integer arguments!");
  assert(getTypeSizeInBits(SrcTy) >= getTypeSizeInBits(Ty) &&
         "getTruncateOrNoop cannot extend!");
  if (getTypeSizeInBits(SrcTy) == getTypeSizeInBits(Ty))
    return V;  // No conversion
  return getTruncateExpr(V, Ty);
}

const SCEV *ScalarEvolution::getUMaxFromMismatchedTypes(const SCEV *LHS,
                                                        const SCEV *RHS) {
  const SCEV *PromotedLHS = LHS;
  const SCEV *PromotedRHS = RHS;

  if (getTypeSizeInBits(LHS->getType()) > getTypeSizeInBits(RHS->getType()))
    PromotedRHS = getZeroExtendExpr(RHS, LHS->getType());
  else
    PromotedLHS = getNoopOrZeroExtend(LHS, RHS->getType());

  return getUMaxExpr(PromotedLHS, PromotedRHS);
}

const SCEV *ScalarEvolution::getUMinFromMismatchedTypes(const SCEV *LHS,
                                                        const SCEV *RHS,
                                                        bool Sequential) {
  SmallVector<const SCEV *, 2> Ops = { LHS, RHS };
  return getUMinFromMismatchedTypes(Ops, Sequential);
}

const SCEV *
ScalarEvolution::getUMinFromMismatchedTypes(SmallVectorImpl<const SCEV *> &Ops,
                                            bool Sequential) {
  assert(!Ops.empty() && "At least one operand must be!");
  // Trivial case.
  if (Ops.size() == 1)
    return Ops[0];

  // Find the max type first.
  Type *MaxType = nullptr;
  for (const auto *S : Ops)
    if (MaxType)
      MaxType = getWiderType(MaxType, S->getType());
    else
      MaxType = S->getType();
  assert(MaxType && "Failed to find maximum type!");

  // Extend all ops to max type.
  SmallVector<const SCEV *, 2> PromotedOps;
  for (const auto *S : Ops)
    PromotedOps.push_back(getNoopOrZeroExtend(S, MaxType));

  // Generate umin.
  return getUMinExpr(PromotedOps, Sequential);
}

const SCEV *ScalarEvolution::getPointerBase(const SCEV *V) {
  // A pointer operand may evaluate to a nonpointer expression, such as null.
  if (!V->getType()->isPointerTy())
    return V;

  while (true) {
    if (auto *AddRec = dyn_cast<SCEVAddRecExpr>(V)) {
      V = AddRec->getStart();
    } else if (auto *Add = dyn_cast<SCEVAddExpr>(V)) {
      const SCEV *PtrOp = nullptr;
      for (const SCEV *AddOp : Add->operands()) {
        if (AddOp->getType()->isPointerTy()) {
          assert(!PtrOp && "Cannot have multiple pointer ops");
          PtrOp = AddOp;
        }
      }
      assert(PtrOp && "Must have pointer op");
      V = PtrOp;
    } else // Not something we can look further into.
      return V;
  }
}

/// Push users of the given Instruction onto the given Worklist.
static void PushDefUseChildren(Instruction *I,
                               SmallVectorImpl<Instruction *> &Worklist,
                               SmallPtrSetImpl<Instruction *> &Visited) {
  // Push the def-use children onto the Worklist stack.
  for (User *U : I->users()) {
    auto *UserInsn = cast<Instruction>(U);
    if (Visited.insert(UserInsn).second)
      Worklist.push_back(UserInsn);
  }
}

namespace {

/// Takes SCEV S and Loop L. For each AddRec sub-expression, use its start
/// expression in case its Loop is L. If it is not L then
/// if IgnoreOtherLoops is true then use AddRec itself
/// otherwise rewrite cannot be done.
/// If SCEV contains non-invariant unknown SCEV rewrite cannot be done.
class SCEVInitRewriter : public SCEVRewriteVisitor<SCEVInitRewriter> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                             bool IgnoreOtherLoops = true) {
    SCEVInitRewriter Rewriter(L, SE);
    const SCEV *Result = Rewriter.visit(S);
    if (Rewriter.hasSeenLoopVariantSCEVUnknown())
      return SE.getCouldNotCompute();
    return Rewriter.hasSeenOtherLoops() && !IgnoreOtherLoops
               ? SE.getCouldNotCompute()
               : Result;
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    if (!SE.isLoopInvariant(Expr, L))
      SeenLoopVariantSCEVUnknown = true;
    return Expr;
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    // Only re-write AddRecExprs for this loop.
    if (Expr->getLoop() == L)
      return Expr->getStart();
    SeenOtherLoops = true;
    return Expr;
  }

  bool hasSeenLoopVariantSCEVUnknown() { return SeenLoopVariantSCEVUnknown; }

  bool hasSeenOtherLoops() { return SeenOtherLoops; }

private:
  explicit SCEVInitRewriter(const Loop *L, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L) {}

  const Loop *L;
  bool SeenLoopVariantSCEVUnknown = false;
  bool SeenOtherLoops = false;
};

/// Takes SCEV S and Loop L. For each AddRec sub-expression, use its post
/// increment expression in case its Loop is L. If it is not L then
/// use AddRec itself.
/// If SCEV contains non-invariant unknown SCEV rewrite cannot be done.
class SCEVPostIncRewriter : public SCEVRewriteVisitor<SCEVPostIncRewriter> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L, ScalarEvolution &SE) {
    SCEVPostIncRewriter Rewriter(L, SE);
    const SCEV *Result = Rewriter.visit(S);
    return Rewriter.hasSeenLoopVariantSCEVUnknown()
        ? SE.getCouldNotCompute()
        : Result;
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    if (!SE.isLoopInvariant(Expr, L))
      SeenLoopVariantSCEVUnknown = true;
    return Expr;
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    // Only re-write AddRecExprs for this loop.
    if (Expr->getLoop() == L)
      return Expr->getPostIncExpr(SE);
    SeenOtherLoops = true;
    return Expr;
  }

  bool hasSeenLoopVariantSCEVUnknown() { return SeenLoopVariantSCEVUnknown; }

  bool hasSeenOtherLoops() { return SeenOtherLoops; }

private:
  explicit SCEVPostIncRewriter(const Loop *L, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L) {}

  const Loop *L;
  bool SeenLoopVariantSCEVUnknown = false;
  bool SeenOtherLoops = false;
};

/// This class evaluates the compare condition by matching it against the
/// condition of loop latch. If there is a match we assume a true value
/// for the condition while building SCEV nodes.
class SCEVBackedgeConditionFolder
    : public SCEVRewriteVisitor<SCEVBackedgeConditionFolder> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L,
                             ScalarEvolution &SE) {
    bool IsPosBECond = false;
    Value *BECond = nullptr;
    if (BasicBlock *Latch = L->getLoopLatch()) {
      BranchInst *BI = dyn_cast<BranchInst>(Latch->getTerminator());
      if (BI && BI->isConditional()) {
        assert(BI->getSuccessor(0) != BI->getSuccessor(1) &&
               "Both outgoing branches should not target same header!");
        BECond = BI->getCondition();
        IsPosBECond = BI->getSuccessor(0) == L->getHeader();
      } else {
        return S;
      }
    }
    SCEVBackedgeConditionFolder Rewriter(L, BECond, IsPosBECond, SE);
    return Rewriter.visit(S);
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    const SCEV *Result = Expr;
    bool InvariantF = SE.isLoopInvariant(Expr, L);

    if (!InvariantF) {
      Instruction *I = cast<Instruction>(Expr->getValue());
      switch (I->getOpcode()) {
      case Instruction::Select: {
        SelectInst *SI = cast<SelectInst>(I);
        std::optional<const SCEV *> Res =
            compareWithBackedgeCondition(SI->getCondition());
        if (Res) {
          bool IsOne = cast<SCEVConstant>(*Res)->getValue()->isOne();
          Result = SE.getSCEV(IsOne ? SI->getTrueValue() : SI->getFalseValue());
        }
        break;
      }
      default: {
        std::optional<const SCEV *> Res = compareWithBackedgeCondition(I);
        if (Res)
          Result = *Res;
        break;
      }
      }
    }
    return Result;
  }

private:
  explicit SCEVBackedgeConditionFolder(const Loop *L, Value *BECond,
                                       bool IsPosBECond, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L), BackedgeCond(BECond),
        IsPositiveBECond(IsPosBECond) {}

  std::optional<const SCEV *> compareWithBackedgeCondition(Value *IC);

  const Loop *L;
  /// Loop back condition.
  Value *BackedgeCond = nullptr;
  /// Set to true if loop back is on positive branch condition.
  bool IsPositiveBECond;
};

std::optional<const SCEV *>
SCEVBackedgeConditionFolder::compareWithBackedgeCondition(Value *IC) {

  // If value matches the backedge condition for loop latch,
  // then return a constant evolution node based on loopback
  // branch taken.
  if (BackedgeCond == IC)
    return IsPositiveBECond ? SE.getOne(Type::getInt1Ty(SE.getContext()))
                            : SE.getZero(Type::getInt1Ty(SE.getContext()));
  return std::nullopt;
}

class SCEVShiftRewriter : public SCEVRewriteVisitor<SCEVShiftRewriter> {
public:
  static const SCEV *rewrite(const SCEV *S, const Loop *L,
                             ScalarEvolution &SE) {
    SCEVShiftRewriter Rewriter(L, SE);
    const SCEV *Result = Rewriter.visit(S);
    return Rewriter.isValid() ? Result : SE.getCouldNotCompute();
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    // Only allow AddRecExprs for this loop.
    if (!SE.isLoopInvariant(Expr, L))
      Valid = false;
    return Expr;
  }

  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) {
    if (Expr->getLoop() == L && Expr->isAffine())
      return SE.getMinusSCEV(Expr, Expr->getStepRecurrence(SE));
    Valid = false;
    return Expr;
  }

  bool isValid() { return Valid; }

private:
  explicit SCEVShiftRewriter(const Loop *L, ScalarEvolution &SE)
      : SCEVRewriteVisitor(SE), L(L) {}

  const Loop *L;
  bool Valid = true;
};

} // end anonymous namespace

SCEV::NoWrapFlags
ScalarEvolution::proveNoWrapViaConstantRanges(const SCEVAddRecExpr *AR) {
  if (!AR->isAffine())
    return SCEV::FlagAnyWrap;

  using OBO = OverflowingBinaryOperator;

  SCEV::NoWrapFlags Result = SCEV::FlagAnyWrap;

  if (!AR->hasNoSelfWrap()) {
    const SCEV *BECount = getConstantMaxBackedgeTakenCount(AR->getLoop());
    if (const SCEVConstant *BECountMax = dyn_cast<SCEVConstant>(BECount)) {
      ConstantRange StepCR = getSignedRange(AR->getStepRecurrence(*this));
      const APInt &BECountAP = BECountMax->getAPInt();
      unsigned NoOverflowBitWidth =
        BECountAP.getActiveBits() + StepCR.getMinSignedBits();
      if (NoOverflowBitWidth <= getTypeSizeInBits(AR->getType()))
        Result = ScalarEvolution::setFlags(Result, SCEV::FlagNW);
    }
  }

  if (!AR->hasNoSignedWrap()) {
    ConstantRange AddRecRange = getSignedRange(AR);
    ConstantRange IncRange = getSignedRange(AR->getStepRecurrence(*this));

    auto NSWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
        Instruction::Add, IncRange, OBO::NoSignedWrap);
    if (NSWRegion.contains(AddRecRange))
      Result = ScalarEvolution::setFlags(Result, SCEV::FlagNSW);
  }

  if (!AR->hasNoUnsignedWrap()) {
    ConstantRange AddRecRange = getUnsignedRange(AR);
    ConstantRange IncRange = getUnsignedRange(AR->getStepRecurrence(*this));

    auto NUWRegion = ConstantRange::makeGuaranteedNoWrapRegion(
        Instruction::Add, IncRange, OBO::NoUnsignedWrap);
    if (NUWRegion.contains(AddRecRange))
      Result = ScalarEvolution::setFlags(Result, SCEV::FlagNUW);
  }

  return Result;
}

SCEV::NoWrapFlags
ScalarEvolution::proveNoSignedWrapViaInduction(const SCEVAddRecExpr *AR) {
  SCEV::NoWrapFlags Result = AR->getNoWrapFlags();

  if (AR->hasNoSignedWrap())
    return Result;

  if (!AR->isAffine())
    return Result;

  // This function can be expensive, only try to prove NSW once per AddRec.
  if (!SignedWrapViaInductionTried.insert(AR).second)
    return Result;

  const SCEV *Step = AR->getStepRecurrence(*this);
  const Loop *L = AR->getLoop();

  // Check whether the backedge-taken count is SCEVCouldNotCompute.
  // Note that this serves two purposes: It filters out loops that are
  // simply not analyzable, and it covers the case where this code is
  // being called from within backedge-taken count analysis, such that
  // attempting to ask for the backedge-taken count would likely result
  // in infinite recursion. In the later case, the analysis code will
  // cope with a conservative value, and it will take care to purge
  // that value once it has finished.
  const SCEV *MaxBECount = getConstantMaxBackedgeTakenCount(L);

  // Normally, in the cases we can prove no-overflow via a
  // backedge guarding condition, we can also compute a backedge
  // taken count for the loop.  The exceptions are assumptions and
  // guards present in the loop -- SCEV is not great at exploiting
  // these to compute max backedge taken counts, but can still use
  // these to prove lack of overflow.  Use this fact to avoid
  // doing extra work that may not pay off.

  if (isa<SCEVCouldNotCompute>(MaxBECount) && !HasGuards &&
      AC.assumptions().empty())
    return Result;

  // If the backedge is guarded by a comparison with the pre-inc  value the
  // addrec is safe. Also, if the entry is guarded by a comparison with the
  // start value and the backedge is guarded by a comparison with the post-inc
  // value, the addrec is safe.
  ICmpInst::Predicate Pred;
  const SCEV *OverflowLimit =
    getSignedOverflowLimitForStep(Step, &Pred, this);
  if (OverflowLimit &&
      (isLoopBackedgeGuardedByCond(L, Pred, AR, OverflowLimit) ||
       isKnownOnEveryIteration(Pred, AR, OverflowLimit))) {
    Result = setFlags(Result, SCEV::FlagNSW);
  }
  return Result;
}
SCEV::NoWrapFlags
ScalarEvolution::proveNoUnsignedWrapViaInduction(const SCEVAddRecExpr *AR) {
  SCEV::NoWrapFlags Result = AR->getNoWrapFlags();

  if (AR->hasNoUnsignedWrap())
    return Result;

  if (!AR->isAffine())
    return Result;

  // This function can be expensive, only try to prove NUW once per AddRec.
  if (!UnsignedWrapViaInductionTried.insert(AR).second)
    return Result;

  const SCEV *Step = AR->getStepRecurrence(*this);
  unsigned BitWidth = getTypeSizeInBits(AR->getType());
  const Loop *L = AR->getLoop();

  // Check whether the backedge-taken count is SCEVCouldNotCompute.
  // Note that this serves two purposes: It filters out loops that are
  // simply not analyzable, and it covers the case where this code is
  // being called from within backedge-taken count analysis, such that
  // attempting to ask for the backedge-taken count would likely result
  // in infinite recursion. In the later case, the analysis code will
  // cope with a conservative value, and it will take care to purge
  // that value once it has finished.
  const SCEV *MaxBECount = getConstantMaxBackedgeTakenCount(L);

  // Normally, in the cases we can prove no-overflow via a
  // backedge guarding condition, we can also compute a backedge
  // taken count for the loop.  The exceptions are assumptions and
  // guards present in the loop -- SCEV is not great at exploiting
  // these to compute max backedge taken counts, but can still use
  // these to prove lack of overflow.  Use this fact to avoid
  // doing extra work that may not pay off.

  if (isa<SCEVCouldNotCompute>(MaxBECount) && !HasGuards &&
      AC.assumptions().empty())
    return Result;

  // If the backedge is guarded by a comparison with the pre-inc  value the
  // addrec is safe. Also, if the entry is guarded by a comparison with the
  // start value and the backedge is guarded by a comparison with the post-inc
  // value, the addrec is safe.
  if (isKnownPositive(Step)) {
    const SCEV *N = getConstant(APInt::getMinValue(BitWidth) -
                                getUnsignedRangeMax(Step));
    if (isLoopBackedgeGuardedByCond(L, ICmpInst::ICMP_ULT, AR, N) ||
        isKnownOnEveryIteration(ICmpInst::ICMP_ULT, AR, N)) {
      Result = setFlags(Result, SCEV::FlagNUW);
    }
  }

  return Result;
}

namespace {

/// Represents an abstract binary operation.  This may exist as a
/// normal instruction or constant expression, or may have been
/// derived from an expression tree.
struct BinaryOp {
  unsigned Opcode;
  Value *LHS;
  Value *RHS;
  bool IsNSW = false;
  bool IsNUW = false;

  /// Op is set if this BinaryOp corresponds to a concrete LLVM instruction or
  /// constant expression.
  Operator *Op = nullptr;

  explicit BinaryOp(Operator *Op)
      : Opcode(Op->getOpcode()), LHS(Op->getOperand(0)), RHS(Op->getOperand(1)),
        Op(Op) {
    if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(Op)) {
      IsNSW = OBO->hasNoSignedWrap();
      IsNUW = OBO->hasNoUnsignedWrap();
    }
  }

  explicit BinaryOp(unsigned Opcode, Value *LHS, Value *RHS, bool IsNSW = false,
                    bool IsNUW = false)
      : Opcode(Opcode), LHS(LHS), RHS(RHS), IsNSW(IsNSW), IsNUW(IsNUW) {}
};

} // end anonymous namespace

/// Try to map \p V into a BinaryOp, and return \c std::nullopt on failure.
static std::optional<BinaryOp> MatchBinaryOp(Value *V, const DataLayout &DL,
                                             AssumptionCache &AC,
                                             const DominatorTree &DT,
                                             const Instruction *CxtI) {
  auto *Op = dyn_cast<Operator>(V);
  if (!Op)
    return std::nullopt;

  // Implementation detail: all the cleverness here should happen without
  // creating new SCEV expressions -- our caller knowns tricks to avoid creating
  // SCEV expressions when possible, and we should not break that.

  switch (Op->getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::UDiv:
  case Instruction::URem:
  case Instruction::And:
  case Instruction::AShr:
  case Instruction::Shl:
    return BinaryOp(Op);

  case Instruction::Or: {
    // Convert or disjoint into add nuw nsw.
    if (cast<PossiblyDisjointInst>(Op)->isDisjoint())
      return BinaryOp(Instruction::Add, Op->getOperand(0), Op->getOperand(1),
                      /*IsNSW=*/true, /*IsNUW=*/true);
    return BinaryOp(Op);
  }

  case Instruction::Xor:
    if (auto *RHSC = dyn_cast<ConstantInt>(Op->getOperand(1)))
      // If the RHS of the xor is a signmask, then this is just an add.
      // Instcombine turns add of signmask into xor as a strength reduction step.
      if (RHSC->getValue().isSignMask())
        return BinaryOp(Instruction::Add, Op->getOperand(0), Op->getOperand(1));
    // Binary `xor` is a bit-wise `add`.
    if (V->getType()->isIntegerTy(1))
      return BinaryOp(Instruction::Add, Op->getOperand(0), Op->getOperand(1));
    return BinaryOp(Op);

  case Instruction::LShr:
    // Turn logical shift right of a constant into a unsigned divide.
    if (ConstantInt *SA = dyn_cast<ConstantInt>(Op->getOperand(1))) {
      uint32_t BitWidth = cast<IntegerType>(Op->getType())->getBitWidth();

      // If the shift count is not less than the bitwidth, the result of
      // the shift is undefined. Don't try to analyze it, because the
      // resolution chosen here may differ from the resolution chosen in
      // other parts of the compiler.
      if (SA->getValue().ult(BitWidth)) {
        Constant *X =
            ConstantInt::get(SA->getContext(),
                             APInt::getOneBitSet(BitWidth, SA->getZExtValue()));
        return BinaryOp(Instruction::UDiv, Op->getOperand(0), X);
      }
    }
    return BinaryOp(Op);

  case Instruction::ExtractValue: {
    auto *EVI = cast<ExtractValueInst>(Op);
    if (EVI->getNumIndices() != 1 || EVI->getIndices()[0] != 0)
      break;

    auto *WO = dyn_cast<WithOverflowInst>(EVI->getAggregateOperand());
    if (!WO)
      break;

    Instruction::BinaryOps BinOp = WO->getBinaryOp();
    bool Signed = WO->isSigned();
    // TODO: Should add nuw/nsw flags for mul as well.
    if (BinOp == Instruction::Mul || !isOverflowIntrinsicNoWrap(WO, DT))
      return BinaryOp(BinOp, WO->getLHS(), WO->getRHS());

    // Now that we know that all uses of the arithmetic-result component of
    // CI are guarded by the overflow check, we can go ahead and pretend
    // that the arithmetic is non-overflowing.
    return BinaryOp(BinOp, WO->getLHS(), WO->getRHS(),
                    /* IsNSW = */ Signed, /* IsNUW = */ !Signed);
  }

  default:
    break;
  }

  // Recognise intrinsic loop.decrement.reg, and as this has exactly the same
  // semantics as a Sub, return a binary sub expression.
  if (auto *II = dyn_cast<IntrinsicInst>(V))
    if (II->getIntrinsicID() == Intrinsic::loop_decrement_reg)
      return BinaryOp(Instruction::Sub, II->getOperand(0), II->getOperand(1));

  return std::nullopt;
}

/// Helper function to createAddRecFromPHIWithCasts. We have a phi
/// node whose symbolic (unknown) SCEV is \p SymbolicPHI, which is updated via
/// the loop backedge by a SCEVAddExpr, possibly also with a few casts on the
/// way. This function checks if \p Op, an operand of this SCEVAddExpr,
/// follows one of the following patterns:
/// Op == (SExt ix (Trunc iy (%SymbolicPHI) to ix) to iy)
/// Op == (ZExt ix (Trunc iy (%SymbolicPHI) to ix) to iy)
/// If the SCEV expression of \p Op conforms with one of the expected patterns
/// we return the type of the truncation operation, and indicate whether the
/// truncated type should be treated as signed/unsigned by setting
/// \p Signed to true/false, respectively.
static Type *isSimpleCastedPHI(const SCEV *Op, const SCEVUnknown *SymbolicPHI,
                               bool &Signed, ScalarEvolution &SE) {
  // The case where Op == SymbolicPHI (that is, with no type conversions on
  // the way) is handled by the regular add recurrence creating logic and
  // would have already been triggered in createAddRecForPHI. Reaching it here
  // means that createAddRecFromPHI had failed for this PHI before (e.g.,
  // because one of the other operands of the SCEVAddExpr updating this PHI is
  // not invariant).
  //
  // Here we look for the case where Op = (ext(trunc(SymbolicPHI))), and in
  // this case predicates that allow us to prove that Op == SymbolicPHI will
  // be added.
  if (Op == SymbolicPHI)
    return nullptr;

  unsigned SourceBits = SE.getTypeSizeInBits(SymbolicPHI->getType());
  unsigned NewBits = SE.getTypeSizeInBits(Op->getType());
  if (SourceBits != NewBits)
    return nullptr;

  const SCEVSignExtendExpr *SExt = dyn_cast<SCEVSignExtendExpr>(Op);
  const SCEVZeroExtendExpr *ZExt = dyn_cast<SCEVZeroExtendExpr>(Op);
  if (!SExt && !ZExt)
    return nullptr;
  const SCEVTruncateExpr *Trunc =
      SExt ? dyn_cast<SCEVTruncateExpr>(SExt->getOperand())
           : dyn_cast<SCEVTruncateExpr>(ZExt->getOperand());
  if (!Trunc)
    return nullptr;
  const SCEV *X = Trunc->getOperand();
  if (X != SymbolicPHI)
    return nullptr;
  Signed = SExt != nullptr;
  return Trunc->getType();
}

static const Loop *isIntegerLoopHeaderPHI(const PHINode *PN, LoopInfo &LI) {
  if (!PN->getType()->isIntegerTy())
    return nullptr;
  const Loop *L = LI.getLoopFor(PN->getParent());
  if (!L || L->getHeader() != PN->getParent())
    return nullptr;
  return L;
}

// Analyze \p SymbolicPHI, a SCEV expression of a phi node, and check if the
// computation that updates the phi follows the following pattern:
//   (SExt/ZExt ix (Trunc iy (%SymbolicPHI) to ix) to iy) + InvariantAccum
// which correspond to a phi->trunc->sext/zext->add->phi update chain.
// If so, try to see if it can be rewritten as an AddRecExpr under some
// Predicates. If successful, return them as a pair. Also cache the results
// of the analysis.
//
// Example usage scenario:
//    Say the Rewriter is called for the following SCEV:
//         8 * ((sext i32 (trunc i64 %X to i32) to i64) + %Step)
//    where:
//         %X = phi i64 (%Start, %BEValue)
//    It will visitMul->visitAdd->visitSExt->visitTrunc->visitUnknown(%X),
//    and call this function with %SymbolicPHI = %X.
//
//    The analysis will find that the value coming around the backedge has
//    the following SCEV:
//         BEValue = ((sext i32 (trunc i64 %X to i32) to i64) + %Step)
//    Upon concluding that this matches the desired pattern, the function
//    will return the pair {NewAddRec, SmallPredsVec} where:
//         NewAddRec = {%Start,+,%Step}
//         SmallPredsVec = {P1, P2, P3} as follows:
//           P1(WrapPred): AR: {trunc(%Start),+,(trunc %Step)}<nsw> Flags: <nssw>
//           P2(EqualPred): %Start == (sext i32 (trunc i64 %Start to i32) to i64)
//           P3(EqualPred): %Step == (sext i32 (trunc i64 %Step to i32) to i64)
//    The returned pair means that SymbolicPHI can be rewritten into NewAddRec
//    under the predicates {P1,P2,P3}.
//    This predicated rewrite will be cached in PredicatedSCEVRewrites:
//         PredicatedSCEVRewrites[{%X,L}] = {NewAddRec, {P1,P2,P3)}
//
// TODO's:
//
// 1) Extend the Induction descriptor to also support inductions that involve
//    casts: When needed (namely, when we are called in the context of the
//    vectorizer induction analysis), a Set of cast instructions will be
//    populated by this method, and provided back to isInductionPHI. This is
//    needed to allow the vectorizer to properly record them to be ignored by
//    the cost model and to avoid vectorizing them (otherwise these casts,
//    which are redundant under the runtime overflow checks, will be
//    vectorized, which can be costly).
//
// 2) Support additional induction/PHISCEV patterns: We also want to support
//    inductions where the sext-trunc / zext-trunc operations (partly) occur
//    after the induction update operation (the induction increment):
//
//      (Trunc iy (SExt/ZExt ix (%SymbolicPHI + InvariantAccum) to iy) to ix)
//    which correspond to a phi->add->trunc->sext/zext->phi update chain.
//
//      (Trunc iy ((SExt/ZExt ix (%SymbolicPhi) to iy) + InvariantAccum) to ix)
//    which correspond to a phi->trunc->add->sext/zext->phi update chain.
//
// 3) Outline common code with createAddRecFromPHI to avoid duplication.
std::optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
ScalarEvolution::createAddRecFromPHIWithCastsImpl(const SCEVUnknown *SymbolicPHI) {
  SmallVector<const SCEVPredicate *, 3> Predicates;

  // *** Part1: Analyze if we have a phi-with-cast pattern for which we can
  // return an AddRec expression under some predicate.

  auto *PN = cast<PHINode>(SymbolicPHI->getValue());
  const Loop *L = isIntegerLoopHeaderPHI(PN, LI);
  assert(L && "Expecting an integer loop header phi");

  // The loop may have multiple entrances or multiple exits; we can analyze
  // this phi as an addrec if it has a unique entry value and a unique
  // backedge value.
  Value *BEValueV = nullptr, *StartValueV = nullptr;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *V = PN->getIncomingValue(i);
    if (L->contains(PN->getIncomingBlock(i))) {
      if (!BEValueV) {
        BEValueV = V;
      } else if (BEValueV != V) {
        BEValueV = nullptr;
        break;
      }
    } else if (!StartValueV) {
      StartValueV = V;
    } else if (StartValueV != V) {
      StartValueV = nullptr;
      break;
    }
  }
  if (!BEValueV || !StartValueV)
    return std::nullopt;

  const SCEV *BEValue = getSCEV(BEValueV);

  // If the value coming around the backedge is an add with the symbolic
  // value we just inserted, possibly with casts that we can ignore under
  // an appropriate runtime guard, then we found a simple induction variable!
  const auto *Add = dyn_cast<SCEVAddExpr>(BEValue);
  if (!Add)
    return std::nullopt;

  // If there is a single occurrence of the symbolic value, possibly
  // casted, replace it with a recurrence.
  unsigned FoundIndex = Add->getNumOperands();
  Type *TruncTy = nullptr;
  bool Signed;
  for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
    if ((TruncTy =
             isSimpleCastedPHI(Add->getOperand(i), SymbolicPHI, Signed, *this)))
      if (FoundIndex == e) {
        FoundIndex = i;
        break;
      }

  if (FoundIndex == Add->getNumOperands())
    return std::nullopt;

  // Create an add with everything but the specified operand.
  SmallVector<const SCEV *, 8> Ops;
  for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
    if (i != FoundIndex)
      Ops.push_back(Add->getOperand(i));
  const SCEV *Accum = getAddExpr(Ops);

  // The runtime checks will not be valid if the step amount is
  // varying inside the loop.
  if (!isLoopInvariant(Accum, L))
    return std::nullopt;

  // *** Part2: Create the predicates

  // Analysis was successful: we have a phi-with-cast pattern for which we
  // can return an AddRec expression under the following predicates:
  //
  // P1: A Wrap predicate that guarantees that Trunc(Start) + i*Trunc(Accum)
  //     fits within the truncated type (does not overflow) for i = 0 to n-1.
  // P2: An Equal predicate that guarantees that
  //     Start = (Ext ix (Trunc iy (Start) to ix) to iy)
  // P3: An Equal predicate that guarantees that
  //     Accum = (Ext ix (Trunc iy (Accum) to ix) to iy)
  //
  // As we next prove, the above predicates guarantee that:
  //     Start + i*Accum = (Ext ix (Trunc iy ( Start + i*Accum ) to ix) to iy)
  //
  //
  // More formally, we want to prove that:
  //     Expr(i+1) = Start + (i+1) * Accum
  //               = (Ext ix (Trunc iy (Expr(i)) to ix) to iy) + Accum
  //
  // Given that:
  // 1) Expr(0) = Start
  // 2) Expr(1) = Start + Accum
  //            = (Ext ix (Trunc iy (Start) to ix) to iy) + Accum :: from P2
  // 3) Induction hypothesis (step i):
  //    Expr(i) = (Ext ix (Trunc iy (Expr(i-1)) to ix) to iy) + Accum
  //
  // Proof:
  //  Expr(i+1) =
  //   = Start + (i+1)*Accum
  //   = (Start + i*Accum) + Accum
  //   = Expr(i) + Accum
  //   = (Ext ix (Trunc iy (Expr(i-1)) to ix) to iy) + Accum + Accum
  //                                                             :: from step i
  //
  //   = (Ext ix (Trunc iy (Start + (i-1)*Accum) to ix) to iy) + Accum + Accum
  //
  //   = (Ext ix (Trunc iy (Start + (i-1)*Accum) to ix) to iy)
  //     + (Ext ix (Trunc iy (Accum) to ix) to iy)
  //     + Accum                                                     :: from P3
  //
  //   = (Ext ix (Trunc iy ((Start + (i-1)*Accum) + Accum) to ix) to iy)
  //     + Accum                            :: from P1: Ext(x)+Ext(y)=>Ext(x+y)
  //
  //   = (Ext ix (Trunc iy (Start + i*Accum) to ix) to iy) + Accum
  //   = (Ext ix (Trunc iy (Expr(i)) to ix) to iy) + Accum
  //
  // By induction, the same applies to all iterations 1<=i<n:
  //

  // Create a truncated addrec for which we will add a no overflow check (P1).
  const SCEV *StartVal = getSCEV(StartValueV);
  const SCEV *PHISCEV =
      getAddRecExpr(getTruncateExpr(StartVal, TruncTy),
                    getTruncateExpr(Accum, TruncTy), L, SCEV::FlagAnyWrap);

  // PHISCEV can be either a SCEVConstant or a SCEVAddRecExpr.
  // ex: If truncated Accum is 0 and StartVal is a constant, then PHISCEV
  // will be constant.
  //
  //  If PHISCEV is a constant, then P1 degenerates into P2 or P3, so we don't
  // add P1.
  if (const auto *AR = dyn_cast<SCEVAddRecExpr>(PHISCEV)) {
    SCEVWrapPredicate::IncrementWrapFlags AddedFlags =
        Signed ? SCEVWrapPredicate::IncrementNSSW
               : SCEVWrapPredicate::IncrementNUSW;
    const SCEVPredicate *AddRecPred = getWrapPredicate(AR, AddedFlags);
    Predicates.push_back(AddRecPred);
  }

  // Create the Equal Predicates P2,P3:

  // It is possible that the predicates P2 and/or P3 are computable at
  // compile time due to StartVal and/or Accum being constants.
  // If either one is, then we can check that now and escape if either P2
  // or P3 is false.

  // Construct the extended SCEV: (Ext ix (Trunc iy (Expr) to ix) to iy)
  // for each of StartVal and Accum
  auto getExtendedExpr = [&](const SCEV *Expr,
                             bool CreateSignExtend) -> const SCEV * {
    assert(isLoopInvariant(Expr, L) && "Expr is expected to be invariant");
    const SCEV *TruncatedExpr = getTruncateExpr(Expr, TruncTy);
    const SCEV *ExtendedExpr =
        CreateSignExtend ? getSignExtendExpr(TruncatedExpr, Expr->getType())
                         : getZeroExtendExpr(TruncatedExpr, Expr->getType());
    return ExtendedExpr;
  };

  // Given:
  //  ExtendedExpr = (Ext ix (Trunc iy (Expr) to ix) to iy
  //               = getExtendedExpr(Expr)
  // Determine whether the predicate P: Expr == ExtendedExpr
  // is known to be false at compile time
  auto PredIsKnownFalse = [&](const SCEV *Expr,
                              const SCEV *ExtendedExpr) -> bool {
    return Expr != ExtendedExpr &&
           isKnownPredicate(ICmpInst::ICMP_NE, Expr, ExtendedExpr);
  };

  const SCEV *StartExtended = getExtendedExpr(StartVal, Signed);
  if (PredIsKnownFalse(StartVal, StartExtended)) {
    LLVM_DEBUG(dbgs() << "P2 is compile-time false\n";);
    return std::nullopt;
  }

  // The Step is always Signed (because the overflow checks are either
  // NSSW or NUSW)
  const SCEV *AccumExtended = getExtendedExpr(Accum, /*CreateSignExtend=*/true);
  if (PredIsKnownFalse(Accum, AccumExtended)) {
    LLVM_DEBUG(dbgs() << "P3 is compile-time false\n";);
    return std::nullopt;
  }

  auto AppendPredicate = [&](const SCEV *Expr,
                             const SCEV *ExtendedExpr) -> void {
    if (Expr != ExtendedExpr &&
        !isKnownPredicate(ICmpInst::ICMP_EQ, Expr, ExtendedExpr)) {
      const SCEVPredicate *Pred = getEqualPredicate(Expr, ExtendedExpr);
      LLVM_DEBUG(dbgs() << "Added Predicate: " << *Pred);
      Predicates.push_back(Pred);
    }
  };

  AppendPredicate(StartVal, StartExtended);
  AppendPredicate(Accum, AccumExtended);

  // *** Part3: Predicates are ready. Now go ahead and create the new addrec in
  // which the casts had been folded away. The caller can rewrite SymbolicPHI
  // into NewAR if it will also add the runtime overflow checks specified in
  // Predicates.
  auto *NewAR = getAddRecExpr(StartVal, Accum, L, SCEV::FlagAnyWrap);

  std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>> PredRewrite =
      std::make_pair(NewAR, Predicates);
  // Remember the result of the analysis for this SCEV at this locayyytion.
  PredicatedSCEVRewrites[{SymbolicPHI, L}] = PredRewrite;
  return PredRewrite;
}

std::optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
ScalarEvolution::createAddRecFromPHIWithCasts(const SCEVUnknown *SymbolicPHI) {
  auto *PN = cast<PHINode>(SymbolicPHI->getValue());
  const Loop *L = isIntegerLoopHeaderPHI(PN, LI);
  if (!L)
    return std::nullopt;

  // Check to see if we already analyzed this PHI.
  auto I = PredicatedSCEVRewrites.find({SymbolicPHI, L});
  if (I != PredicatedSCEVRewrites.end()) {
    std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>> Rewrite =
        I->second;
    // Analysis was done before and failed to create an AddRec:
    if (Rewrite.first == SymbolicPHI)
      return std::nullopt;
    // Analysis was done before and succeeded to create an AddRec under
    // a predicate:
    assert(isa<SCEVAddRecExpr>(Rewrite.first) && "Expected an AddRec");
    assert(!(Rewrite.second).empty() && "Expected to find Predicates");
    return Rewrite;
  }

  std::optional<std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
    Rewrite = createAddRecFromPHIWithCastsImpl(SymbolicPHI);

  // Record in the cache that the analysis failed
  if (!Rewrite) {
    SmallVector<const SCEVPredicate *, 3> Predicates;
    PredicatedSCEVRewrites[{SymbolicPHI, L}] = {SymbolicPHI, Predicates};
    return std::nullopt;
  }

  return Rewrite;
}

// FIXME: This utility is currently required because the Rewriter currently
// does not rewrite this expression:
// {0, +, (sext ix (trunc iy to ix) to iy)}
// into {0, +, %step},
// even when the following Equal predicate exists:
// "%step == (sext ix (trunc iy to ix) to iy)".
bool PredicatedScalarEvolution::areAddRecsEqualWithPreds(
    const SCEVAddRecExpr *AR1, const SCEVAddRecExpr *AR2) const {
  if (AR1 == AR2)
    return true;

  auto areExprsEqual = [&](const SCEV *Expr1, const SCEV *Expr2) -> bool {
    if (Expr1 != Expr2 && !Preds->implies(SE.getEqualPredicate(Expr1, Expr2)) &&
        !Preds->implies(SE.getEqualPredicate(Expr2, Expr1)))
      return false;
    return true;
  };

  if (!areExprsEqual(AR1->getStart(), AR2->getStart()) ||
      !areExprsEqual(AR1->getStepRecurrence(SE), AR2->getStepRecurrence(SE)))
    return false;
  return true;
}

/// A helper function for createAddRecFromPHI to handle simple cases.
///
/// This function tries to find an AddRec expression for the simplest (yet most
/// common) cases: PN = PHI(Start, OP(Self, LoopInvariant)).
/// If it fails, createAddRecFromPHI will use a more general, but slow,
/// technique for finding the AddRec expression.
const SCEV *ScalarEvolution::createSimpleAffineAddRec(PHINode *PN,
                                                      Value *BEValueV,
                                                      Value *StartValueV) {
  const Loop *L = LI.getLoopFor(PN->getParent());
  assert(L && L->getHeader() == PN->getParent());
  assert(BEValueV && StartValueV);

  auto BO = MatchBinaryOp(BEValueV, getDataLayout(), AC, DT, PN);
  if (!BO)
    return nullptr;

  if (BO->Opcode != Instruction::Add)
    return nullptr;

  const SCEV *Accum = nullptr;
  if (BO->LHS == PN && L->isLoopInvariant(BO->RHS))
    Accum = getSCEV(BO->RHS);
  else if (BO->RHS == PN && L->isLoopInvariant(BO->LHS))
    Accum = getSCEV(BO->LHS);

  if (!Accum)
    return nullptr;

  SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;
  if (BO->IsNUW)
    Flags = setFlags(Flags, SCEV::FlagNUW);
  if (BO->IsNSW)
    Flags = setFlags(Flags, SCEV::FlagNSW);

  const SCEV *StartVal = getSCEV(StartValueV);
  const SCEV *PHISCEV = getAddRecExpr(StartVal, Accum, L, Flags);
  insertValueToMap(PN, PHISCEV);

  if (auto *AR = dyn_cast<SCEVAddRecExpr>(PHISCEV)) {
    setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR),
                   (SCEV::NoWrapFlags)(AR->getNoWrapFlags() |
                                       proveNoWrapViaConstantRanges(AR)));
  }

  // We can add Flags to the post-inc expression only if we
  // know that it is *undefined behavior* for BEValueV to
  // overflow.
  if (auto *BEInst = dyn_cast<Instruction>(BEValueV)) {
    assert(isLoopInvariant(Accum, L) &&
           "Accum is defined outside L, but is not invariant?");
    if (isAddRecNeverPoison(BEInst, L))
      (void)getAddRecExpr(getAddExpr(StartVal, Accum), Accum, L, Flags);
  }

  return PHISCEV;
}

const SCEV *ScalarEvolution::createAddRecFromPHI(PHINode *PN) {
  const Loop *L = LI.getLoopFor(PN->getParent());
  if (!L || L->getHeader() != PN->getParent())
    return nullptr;

  // The loop may have multiple entrances or multiple exits; we can analyze
  // this phi as an addrec if it has a unique entry value and a unique
  // backedge value.
  Value *BEValueV = nullptr, *StartValueV = nullptr;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    Value *V = PN->getIncomingValue(i);
    if (L->contains(PN->getIncomingBlock(i))) {
      if (!BEValueV) {
        BEValueV = V;
      } else if (BEValueV != V) {
        BEValueV = nullptr;
        break;
      }
    } else if (!StartValueV) {
      StartValueV = V;
    } else if (StartValueV != V) {
      StartValueV = nullptr;
      break;
    }
  }
  if (!BEValueV || !StartValueV)
    return nullptr;

  assert(ValueExprMap.find_as(PN) == ValueExprMap.end() &&
         "PHI node already processed?");

  // First, try to find AddRec expression without creating a fictituos symbolic
  // value for PN.
  if (auto *S = createSimpleAffineAddRec(PN, BEValueV, StartValueV))
    return S;

  // Handle PHI node value symbolically.
  const SCEV *SymbolicName = getUnknown(PN);
  insertValueToMap(PN, SymbolicName);

  // Using this symbolic name for the PHI, analyze the value coming around
  // the back-edge.
  const SCEV *BEValue = getSCEV(BEValueV);

  // NOTE: If BEValue is loop invariant, we know that the PHI node just
  // has a special value for the first iteration of the loop.

  // If the value coming around the backedge is an add with the symbolic
  // value we just inserted, then we found a simple induction variable!
  if (const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(BEValue)) {
    // If there is a single occurrence of the symbolic value, replace it
    // with a recurrence.
    unsigned FoundIndex = Add->getNumOperands();
    for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
      if (Add->getOperand(i) == SymbolicName)
        if (FoundIndex == e) {
          FoundIndex = i;
          break;
        }

    if (FoundIndex != Add->getNumOperands()) {
      // Create an add with everything but the specified operand.
      SmallVector<const SCEV *, 8> Ops;
      for (unsigned i = 0, e = Add->getNumOperands(); i != e; ++i)
        if (i != FoundIndex)
          Ops.push_back(SCEVBackedgeConditionFolder::rewrite(Add->getOperand(i),
                                                             L, *this));
      const SCEV *Accum = getAddExpr(Ops);

      // This is not a valid addrec if the step amount is varying each
      // loop iteration, but is not itself an addrec in this loop.
      if (isLoopInvariant(Accum, L) ||
          (isa<SCEVAddRecExpr>(Accum) &&
           cast<SCEVAddRecExpr>(Accum)->getLoop() == L)) {
        SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;

        if (auto BO = MatchBinaryOp(BEValueV, getDataLayout(), AC, DT, PN)) {
          if (BO->Opcode == Instruction::Add && BO->LHS == PN) {
            if (BO->IsNUW)
              Flags = setFlags(Flags, SCEV::FlagNUW);
            if (BO->IsNSW)
              Flags = setFlags(Flags, SCEV::FlagNSW);
          }
        } else if (GEPOperator *GEP = dyn_cast<GEPOperator>(BEValueV)) {
          if (GEP->getOperand(0) == PN) {
            GEPNoWrapFlags NW = GEP->getNoWrapFlags();
            // If the increment has any nowrap flags, then we know the address
            // space cannot be wrapped around.
            if (NW != GEPNoWrapFlags::none())
              Flags = setFlags(Flags, SCEV::FlagNW);
            // If the GEP is nuw or nusw with non-negative offset, we know that
            // no unsigned wrap occurs. We cannot set the nsw flag as only the
            // offset is treated as signed, while the base is unsigned.
            if (NW.hasNoUnsignedWrap() ||
                (NW.hasNoUnsignedSignedWrap() && isKnownNonNegative(Accum)))
              Flags = setFlags(Flags, SCEV::FlagNUW);
          }

          // We cannot transfer nuw and nsw flags from subtraction
          // operations -- sub nuw X, Y is not the same as add nuw X, -Y
          // for instance.
        }

        const SCEV *StartVal = getSCEV(StartValueV);
        const SCEV *PHISCEV = getAddRecExpr(StartVal, Accum, L, Flags);

        // Okay, for the entire analysis of this edge we assumed the PHI
        // to be symbolic.  We now need to go back and purge all of the
        // entries for the scalars that use the symbolic expression.
        forgetMemoizedResults(SymbolicName);
        insertValueToMap(PN, PHISCEV);

        if (auto *AR = dyn_cast<SCEVAddRecExpr>(PHISCEV)) {
          setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR),
                         (SCEV::NoWrapFlags)(AR->getNoWrapFlags() |
                                             proveNoWrapViaConstantRanges(AR)));
        }

        // We can add Flags to the post-inc expression only if we
        // know that it is *undefined behavior* for BEValueV to
        // overflow.
        if (auto *BEInst = dyn_cast<Instruction>(BEValueV))
          if (isLoopInvariant(Accum, L) && isAddRecNeverPoison(BEInst, L))
            (void)getAddRecExpr(getAddExpr(StartVal, Accum), Accum, L, Flags);

        return PHISCEV;
      }
    }
  } else {
    // Otherwise, this could be a loop like this:
    //     i = 0;  for (j = 1; ..; ++j) { ....  i = j; }
    // In this case, j = {1,+,1}  and BEValue is j.
    // Because the other in-value of i (0) fits the evolution of BEValue
    // i really is an addrec evolution.
    //
    // We can generalize this saying that i is the shifted value of BEValue
    // by one iteration:
    //   PHI(f(0), f({1,+,1})) --> f({0,+,1})
    const SCEV *Shifted = SCEVShiftRewriter::rewrite(BEValue, L, *this);
    const SCEV *Start = SCEVInitRewriter::rewrite(Shifted, L, *this, false);
    if (Shifted != getCouldNotCompute() &&
        Start != getCouldNotCompute()) {
      const SCEV *StartVal = getSCEV(StartValueV);
      if (Start == StartVal) {
        // Okay, for the entire analysis of this edge we assumed the PHI
        // to be symbolic.  We now need to go back and purge all of the
        // entries for the scalars that use the symbolic expression.
        forgetMemoizedResults(SymbolicName);
        insertValueToMap(PN, Shifted);
        return Shifted;
      }
    }
  }

  // Remove the temporary PHI node SCEV that has been inserted while intending
  // to create an AddRecExpr for this PHI node. We can not keep this temporary
  // as it will prevent later (possibly simpler) SCEV expressions to be added
  // to the ValueExprMap.
  eraseValueFromMap(PN);

  return nullptr;
}

// Try to match a control flow sequence that branches out at BI and merges back
// at Merge into a "C ? LHS : RHS" select pattern.  Return true on a successful
// match.
static bool BrPHIToSelect(DominatorTree &DT, BranchInst *BI, PHINode *Merge,
                          Value *&C, Value *&LHS, Value *&RHS) {
  C = BI->getCondition();

  BasicBlockEdge LeftEdge(BI->getParent(), BI->getSuccessor(0));
  BasicBlockEdge RightEdge(BI->getParent(), BI->getSuccessor(1));

  if (!LeftEdge.isSingleEdge())
    return false;

  assert(RightEdge.isSingleEdge() && "Follows from LeftEdge.isSingleEdge()");

  Use &LeftUse = Merge->getOperandUse(0);
  Use &RightUse = Merge->getOperandUse(1);

  if (DT.dominates(LeftEdge, LeftUse) && DT.dominates(RightEdge, RightUse)) {
    LHS = LeftUse;
    RHS = RightUse;
    return true;
  }

  if (DT.dominates(LeftEdge, RightUse) && DT.dominates(RightEdge, LeftUse)) {
    LHS = RightUse;
    RHS = LeftUse;
    return true;
  }

  return false;
}

const SCEV *ScalarEvolution::createNodeFromSelectLikePHI(PHINode *PN) {
  auto IsReachable =
      [&](BasicBlock *BB) { return DT.isReachableFromEntry(BB); };
  if (PN->getNumIncomingValues() == 2 && all_of(PN->blocks(), IsReachable)) {
    // Try to match
    //
    //  br %cond, label %left, label %right
    // left:
    //  br label %merge
    // right:
    //  br label %merge
    // merge:
    //  V = phi [ %x, %left ], [ %y, %right ]
    //
    // as "select %cond, %x, %y"

    BasicBlock *IDom = DT[PN->getParent()]->getIDom()->getBlock();
    assert(IDom && "At least the entry block should dominate PN");

    auto *BI = dyn_cast<BranchInst>(IDom->getTerminator());
    Value *Cond = nullptr, *LHS = nullptr, *RHS = nullptr;

    if (BI && BI->isConditional() &&
        BrPHIToSelect(DT, BI, PN, Cond, LHS, RHS) &&
        properlyDominates(getSCEV(LHS), PN->getParent()) &&
        properlyDominates(getSCEV(RHS), PN->getParent()))
      return createNodeForSelectOrPHI(PN, Cond, LHS, RHS);
  }

  return nullptr;
}

const SCEV *ScalarEvolution::createNodeForPHI(PHINode *PN) {
  if (const SCEV *S = createAddRecFromPHI(PN))
    return S;

  if (Value *V = simplifyInstruction(PN, {getDataLayout(), &TLI, &DT, &AC}))
    return getSCEV(V);

  if (const SCEV *S = createNodeFromSelectLikePHI(PN))
    return S;

  // If it's not a loop phi, we can't handle it yet.
  return getUnknown(PN);
}

bool SCEVMinMaxExprContains(const SCEV *Root, const SCEV *OperandToFind,
                            SCEVTypes RootKind) {
  struct FindClosure {
    const SCEV *OperandToFind;
    const SCEVTypes RootKind; // Must be a sequential min/max expression.
    const SCEVTypes NonSequentialRootKind; // Non-seq variant of RootKind.

    bool Found = false;

    bool canRecurseInto(SCEVTypes Kind) const {
      // We can only recurse into the SCEV expression of the same effective type
      // as the type of our root SCEV expression, and into zero-extensions.
      return RootKind == Kind || NonSequentialRootKind == Kind ||
             scZeroExtend == Kind;
    };

    FindClosure(const SCEV *OperandToFind, SCEVTypes RootKind)
        : OperandToFind(OperandToFind), RootKind(RootKind),
          NonSequentialRootKind(
              SCEVSequentialMinMaxExpr::getEquivalentNonSequentialSCEVType(
                  RootKind)) {}

    bool follow(const SCEV *S) {
      Found = S == OperandToFind;

      return !isDone() && canRecurseInto(S->getSCEVType());
    }

    bool isDone() const { return Found; }
  };

  FindClosure FC(OperandToFind, RootKind);
  visitAll(Root, FC);
  return FC.Found;
}

std::optional<const SCEV *>
ScalarEvolution::createNodeForSelectOrPHIInstWithICmpInstCond(Type *Ty,
                                                              ICmpInst *Cond,
                                                              Value *TrueVal,
                                                              Value *FalseVal) {
  // Try to match some simple smax or umax patterns.
  auto *ICI = Cond;

  Value *LHS = ICI->getOperand(0);
  Value *RHS = ICI->getOperand(1);

  switch (ICI->getPredicate()) {
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_SLE:
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_ULE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_SGE:
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_UGE:
    // a > b ? a+x : b+x  ->  max(a, b)+x
    // a > b ? b+x : a+x  ->  min(a, b)+x
    if (getTypeSizeInBits(LHS->getType()) <= getTypeSizeInBits(Ty)) {
      bool Signed = ICI->isSigned();
      const SCEV *LA = getSCEV(TrueVal);
      const SCEV *RA = getSCEV(FalseVal);
      const SCEV *LS = getSCEV(LHS);
      const SCEV *RS = getSCEV(RHS);
      if (LA->getType()->isPointerTy()) {
        // FIXME: Handle cases where LS/RS are pointers not equal to LA/RA.
        // Need to make sure we can't produce weird expressions involving
        // negated pointers.
        if (LA == LS && RA == RS)
          return Signed ? getSMaxExpr(LS, RS) : getUMaxExpr(LS, RS);
        if (LA == RS && RA == LS)
          return Signed ? getSMinExpr(LS, RS) : getUMinExpr(LS, RS);
      }
      auto CoerceOperand = [&](const SCEV *Op) -> const SCEV * {
        if (Op->getType()->isPointerTy()) {
          Op = getLosslessPtrToIntExpr(Op);
          if (isa<SCEVCouldNotCompute>(Op))
            return Op;
        }
        if (Signed)
          Op = getNoopOrSignExtend(Op, Ty);
        else
          Op = getNoopOrZeroExtend(Op, Ty);
        return Op;
      };
      LS = CoerceOperand(LS);
      RS = CoerceOperand(RS);
      if (isa<SCEVCouldNotCompute>(LS) || isa<SCEVCouldNotCompute>(RS))
        break;
      const SCEV *LDiff = getMinusSCEV(LA, LS);
      const SCEV *RDiff = getMinusSCEV(RA, RS);
      if (LDiff == RDiff)
        return getAddExpr(Signed ? getSMaxExpr(LS, RS) : getUMaxExpr(LS, RS),
                          LDiff);
      LDiff = getMinusSCEV(LA, RS);
      RDiff = getMinusSCEV(RA, LS);
      if (LDiff == RDiff)
        return getAddExpr(Signed ? getSMinExpr(LS, RS) : getUMinExpr(LS, RS),
                          LDiff);
    }
    break;
  case ICmpInst::ICMP_NE:
    // x != 0 ? x+y : C+y  ->  x == 0 ? C+y : x+y
    std::swap(TrueVal, FalseVal);
    [[fallthrough]];
  case ICmpInst::ICMP_EQ:
    // x == 0 ? C+y : x+y  ->  umax(x, C)+y   iff C u<= 1
    if (getTypeSizeInBits(LHS->getType()) <= getTypeSizeInBits(Ty) &&
        isa<ConstantInt>(RHS) && cast<ConstantInt>(RHS)->isZero()) {
      const SCEV *X = getNoopOrZeroExtend(getSCEV(LHS), Ty);
      const SCEV *TrueValExpr = getSCEV(TrueVal);    // C+y
      const SCEV *FalseValExpr = getSCEV(FalseVal);  // x+y
      const SCEV *Y = getMinusSCEV(FalseValExpr, X); // y = (x+y)-x
      const SCEV *C = getMinusSCEV(TrueValExpr, Y);  // C = (C+y)-y
      if (isa<SCEVConstant>(C) && cast<SCEVConstant>(C)->getAPInt().ule(1))
        return getAddExpr(getUMaxExpr(X, C), Y);
    }
    // x == 0 ? 0 : umin    (..., x, ...)  ->  umin_seq(x, umin    (...))
    // x == 0 ? 0 : umin_seq(..., x, ...)  ->  umin_seq(x, umin_seq(...))
    // x == 0 ? 0 : umin    (..., umin_seq(..., x, ...), ...)
    //                    ->  umin_seq(x, umin (..., umin_seq(...), ...))
    if (isa<ConstantInt>(RHS) && cast<ConstantInt>(RHS)->isZero() &&
        isa<ConstantInt>(TrueVal) && cast<ConstantInt>(TrueVal)->isZero()) {
      const SCEV *X = getSCEV(LHS);
      while (auto *ZExt = dyn_cast<SCEVZeroExtendExpr>(X))
        X = ZExt->getOperand();
      if (getTypeSizeInBits(X->getType()) <= getTypeSizeInBits(Ty)) {
        const SCEV *FalseValExpr = getSCEV(FalseVal);
        if (SCEVMinMaxExprContains(FalseValExpr, X, scSequentialUMinExpr))
          return getUMinExpr(getNoopOrZeroExtend(X, Ty), FalseValExpr,
                             /*Sequential=*/true);
      }
    }
    break;
  default:
    break;
  }

  return std::nullopt;
}

static std::optional<const SCEV *>
createNodeForSelectViaUMinSeq(ScalarEvolution *SE, const SCEV *CondExpr,
                              const SCEV *TrueExpr, const SCEV *FalseExpr) {
  assert(CondExpr->getType()->isIntegerTy(1) &&
         TrueExpr->getType() == FalseExpr->getType() &&
         TrueExpr->getType()->isIntegerTy(1) &&
         "Unexpected operands of a select.");

  // i1 cond ? i1 x : i1 C  -->  C + (i1  cond ? (i1 x - i1 C) : i1 0)
  //                        -->  C + (umin_seq  cond, x - C)
  //
  // i1 cond ? i1 C : i1 x  -->  C + (i1  cond ? i1 0 : (i1 x - i1 C))
  //                        -->  C + (i1 ~cond ? (i1 x - i1 C) : i1 0)
  //                        -->  C + (umin_seq ~cond, x - C)

  // FIXME: while we can't legally model the case where both of the hands
  // are fully variable, we only require that the *difference* is constant.
  if (!isa<SCEVConstant>(TrueExpr) && !isa<SCEVConstant>(FalseExpr))
    return std::nullopt;

  const SCEV *X, *C;
  if (isa<SCEVConstant>(TrueExpr)) {
    CondExpr = SE->getNotSCEV(CondExpr);
    X = FalseExpr;
    C = TrueExpr;
  } else {
    X = TrueExpr;
    C = FalseExpr;
  }
  return SE->getAddExpr(C, SE->getUMinExpr(CondExpr, SE->getMinusSCEV(X, C),
                                           /*Sequential=*/true));
}

static std::optional<const SCEV *>
createNodeForSelectViaUMinSeq(ScalarEvolution *SE, Value *Cond, Value *TrueVal,
                              Value *FalseVal) {
  if (!isa<ConstantInt>(TrueVal) && !isa<ConstantInt>(FalseVal))
    return std::nullopt;

  const auto *SECond = SE->getSCEV(Cond);
  const auto *SETrue = SE->getSCEV(TrueVal);
  const auto *SEFalse = SE->getSCEV(FalseVal);
  return createNodeForSelectViaUMinSeq(SE, SECond, SETrue, SEFalse);
}

const SCEV *ScalarEvolution::createNodeForSelectOrPHIViaUMinSeq(
    Value *V, Value *Cond, Value *TrueVal, Value *FalseVal) {
  assert(Cond->getType()->isIntegerTy(1) && "Select condition is not an i1?");
  assert(TrueVal->getType() == FalseVal->getType() &&
         V->getType() == TrueVal->getType() &&
         "Types of select hands and of the result must match.");

  // For now, only deal with i1-typed `select`s.
  if (!V->getType()->isIntegerTy(1))
    return getUnknown(V);

  if (std::optional<const SCEV *> S =
          createNodeForSelectViaUMinSeq(this, Cond, TrueVal, FalseVal))
    return *S;

  return getUnknown(V);
}

const SCEV *ScalarEvolution::createNodeForSelectOrPHI(Value *V, Value *Cond,
                                                      Value *TrueVal,
                                                      Value *FalseVal) {
  // Handle "constant" branch or select. This can occur for instance when a
  // loop pass transforms an inner loop and moves on to process the outer loop.
  if (auto *CI = dyn_cast<ConstantInt>(Cond))
    return getSCEV(CI->isOne() ? TrueVal : FalseVal);

  if (auto *I = dyn_cast<Instruction>(V)) {
    if (auto *ICI = dyn_cast<ICmpInst>(Cond)) {
      if (std::optional<const SCEV *> S =
              createNodeForSelectOrPHIInstWithICmpInstCond(I->getType(), ICI,
                                                           TrueVal, FalseVal))
        return *S;
    }
  }

  return createNodeForSelectOrPHIViaUMinSeq(V, Cond, TrueVal, FalseVal);
}

/// Expand GEP instructions into add and multiply operations. This allows them
/// to be analyzed by regular SCEV code.
const SCEV *ScalarEvolution::createNodeForGEP(GEPOperator *GEP) {
  assert(GEP->getSourceElementType()->isSized() &&
         "GEP source element type must be sized");

  SmallVector<const SCEV *, 4> IndexExprs;
  for (Value *Index : GEP->indices())
    IndexExprs.push_back(getSCEV(Index));
  return getGEPExpr(GEP, IndexExprs);
}

APInt ScalarEvolution::getConstantMultipleImpl(const SCEV *S) {
  uint64_t BitWidth = getTypeSizeInBits(S->getType());
  auto GetShiftedByZeros = [BitWidth](uint32_t TrailingZeros) {
    return TrailingZeros >= BitWidth
               ? APInt::getZero(BitWidth)
               : APInt::getOneBitSet(BitWidth, TrailingZeros);
  };
  auto GetGCDMultiple = [this](const SCEVNAryExpr *N) {
    // The result is GCD of all operands results.
    APInt Res = getConstantMultiple(N->getOperand(0));
    for (unsigned I = 1, E = N->getNumOperands(); I < E && Res != 1; ++I)
      Res = APIntOps::GreatestCommonDivisor(
          Res, getConstantMultiple(N->getOperand(I)));
    return Res;
  };

  switch (S->getSCEVType()) {
  case scConstant:
    return cast<SCEVConstant>(S)->getAPInt();
  case scPtrToInt:
    return getConstantMultiple(cast<SCEVPtrToIntExpr>(S)->getOperand());
  case scUDivExpr:
  case scVScale:
    return APInt(BitWidth, 1);
  case scTruncate: {
    // Only multiples that are a power of 2 will hold after truncation.
    const SCEVTruncateExpr *T = cast<SCEVTruncateExpr>(S);
    uint32_t TZ = getMinTrailingZeros(T->getOperand());
    return GetShiftedByZeros(TZ);
  }
  case scZeroExtend: {
    const SCEVZeroExtendExpr *Z = cast<SCEVZeroExtendExpr>(S);
    return getConstantMultiple(Z->getOperand()).zext(BitWidth);
  }
  case scSignExtend: {
    // Only multiples that are a power of 2 will hold after sext.
    const SCEVSignExtendExpr *E = cast<SCEVSignExtendExpr>(S);
    uint32_t TZ = getMinTrailingZeros(E->getOperand());
    return GetShiftedByZeros(TZ);
  }
  case scMulExpr: {
    const SCEVMulExpr *M = cast<SCEVMulExpr>(S);
    if (M->hasNoUnsignedWrap()) {
      // The result is the product of all operand results.
      APInt Res = getConstantMultiple(M->getOperand(0));
      for (const SCEV *Operand : M->operands().drop_front())
        Res = Res * getConstantMultiple(Operand);
      return Res;
    }

    // If there are no wrap guarentees, find the trailing zeros, which is the
    // sum of trailing zeros for all its operands.
    uint32_t TZ = 0;
    for (const SCEV *Operand : M->operands())
      TZ += getMinTrailingZeros(Operand);
    return GetShiftedByZeros(TZ);
  }
  case scAddExpr:
  case scAddRecExpr: {
    const SCEVNAryExpr *N = cast<SCEVNAryExpr>(S);
    if (N->hasNoUnsignedWrap())
        return GetGCDMultiple(N);
    // Find the trailing bits, which is the minimum of its operands.
    uint32_t TZ = getMinTrailingZeros(N->getOperand(0));
    for (const SCEV *Operand : N->operands().drop_front())
      TZ = std::min(TZ, getMinTrailingZeros(Operand));
    return GetShiftedByZeros(TZ);
  }
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr:
    return GetGCDMultiple(cast<SCEVNAryExpr>(S));
  case scUnknown: {
    // ask ValueTracking for known bits
    const SCEVUnknown *U = cast<SCEVUnknown>(S);
    unsigned Known =
        computeKnownBits(U->getValue(), getDataLayout(), 0, &AC, nullptr, &DT)
            .countMinTrailingZeros();
    return GetShiftedByZeros(Known);
  }
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

APInt ScalarEvolution::getConstantMultiple(const SCEV *S) {
  auto I = ConstantMultipleCache.find(S);
  if (I != ConstantMultipleCache.end())
    return I->second;

  APInt Result = getConstantMultipleImpl(S);
  auto InsertPair = ConstantMultipleCache.insert({S, Result});
  assert(InsertPair.second && "Should insert a new key");
  return InsertPair.first->second;
}

APInt ScalarEvolution::getNonZeroConstantMultiple(const SCEV *S) {
  APInt Multiple = getConstantMultiple(S);
  return Multiple == 0 ? APInt(Multiple.getBitWidth(), 1) : Multiple;
}

uint32_t ScalarEvolution::getMinTrailingZeros(const SCEV *S) {
  return std::min(getConstantMultiple(S).countTrailingZeros(),
                  (unsigned)getTypeSizeInBits(S->getType()));
}

/// Helper method to assign a range to V from metadata present in the IR.
static std::optional<ConstantRange> GetRangeFromMetadata(Value *V) {
  if (Instruction *I = dyn_cast<Instruction>(V)) {
    if (MDNode *MD = I->getMetadata(LLVMContext::MD_range))
      return getConstantRangeFromMetadata(*MD);
    if (const auto *CB = dyn_cast<CallBase>(V))
      if (std::optional<ConstantRange> Range = CB->getRange())
        return Range;
  }
  if (auto *A = dyn_cast<Argument>(V))
    if (std::optional<ConstantRange> Range = A->getRange())
      return Range;

  return std::nullopt;
}

void ScalarEvolution::setNoWrapFlags(SCEVAddRecExpr *AddRec,
                                     SCEV::NoWrapFlags Flags) {
  if (AddRec->getNoWrapFlags(Flags) != Flags) {
    AddRec->setNoWrapFlags(Flags);
    UnsignedRanges.erase(AddRec);
    SignedRanges.erase(AddRec);
    ConstantMultipleCache.erase(AddRec);
  }
}

ConstantRange ScalarEvolution::
getRangeForUnknownRecurrence(const SCEVUnknown *U) {
  const DataLayout &DL = getDataLayout();

  unsigned BitWidth = getTypeSizeInBits(U->getType());
  const ConstantRange FullSet(BitWidth, /*isFullSet=*/true);

  // Match a simple recurrence of the form: <start, ShiftOp, Step>, and then
  // use information about the trip count to improve our available range.  Note
  // that the trip count independent cases are already handled by known bits.
  // WARNING: The definition of recurrence used here is subtly different than
  // the one used by AddRec (and thus most of this file).  Step is allowed to
  // be arbitrarily loop varying here, where AddRec allows only loop invariant
  // and other addrecs in the same loop (for non-affine addrecs).  The code
  // below intentionally handles the case where step is not loop invariant.
  auto *P = dyn_cast<PHINode>(U->getValue());
  if (!P)
    return FullSet;

  // Make sure that no Phi input comes from an unreachable block. Otherwise,
  // even the values that are not available in these blocks may come from them,
  // and this leads to false-positive recurrence test.
  for (auto *Pred : predecessors(P->getParent()))
    if (!DT.isReachableFromEntry(Pred))
      return FullSet;

  BinaryOperator *BO;
  Value *Start, *Step;
  if (!matchSimpleRecurrence(P, BO, Start, Step))
    return FullSet;

  // If we found a recurrence in reachable code, we must be in a loop. Note
  // that BO might be in some subloop of L, and that's completely okay.
  auto *L = LI.getLoopFor(P->getParent());
  assert(L && L->getHeader() == P->getParent());
  if (!L->contains(BO->getParent()))
    // NOTE: This bailout should be an assert instead.  However, asserting
    // the condition here exposes a case where LoopFusion is querying SCEV
    // with malformed loop information during the midst of the transform.
    // There doesn't appear to be an obvious fix, so for the moment bailout
    // until the caller issue can be fixed.  PR49566 tracks the bug.
    return FullSet;

  // TODO: Extend to other opcodes such as mul, and div
  switch (BO->getOpcode()) {
  default:
    return FullSet;
  case Instruction::AShr:
  case Instruction::LShr:
  case Instruction::Shl:
    break;
  };

  if (BO->getOperand(0) != P)
    // TODO: Handle the power function forms some day.
    return FullSet;

  unsigned TC = getSmallConstantMaxTripCount(L);
  if (!TC || TC >= BitWidth)
    return FullSet;

  auto KnownStart = computeKnownBits(Start, DL, 0, &AC, nullptr, &DT);
  auto KnownStep = computeKnownBits(Step, DL, 0, &AC, nullptr, &DT);
  assert(KnownStart.getBitWidth() == BitWidth &&
         KnownStep.getBitWidth() == BitWidth);

  // Compute total shift amount, being careful of overflow and bitwidths.
  auto MaxShiftAmt = KnownStep.getMaxValue();
  APInt TCAP(BitWidth, TC-1);
  bool Overflow = false;
  auto TotalShift = MaxShiftAmt.umul_ov(TCAP, Overflow);
  if (Overflow)
    return FullSet;

  switch (BO->getOpcode()) {
  default:
    llvm_unreachable("filtered out above");
  case Instruction::AShr: {
    // For each ashr, three cases:
    //   shift = 0 => unchanged value
    //   saturation => 0 or -1
    //   other => a value closer to zero (of the same sign)
    // Thus, the end value is closer to zero than the start.
    auto KnownEnd = KnownBits::ashr(KnownStart,
                                    KnownBits::makeConstant(TotalShift));
    if (KnownStart.isNonNegative())
      // Analogous to lshr (simply not yet canonicalized)
      return ConstantRange::getNonEmpty(KnownEnd.getMinValue(),
                                        KnownStart.getMaxValue() + 1);
    if (KnownStart.isNegative())
      // End >=u Start && End <=s Start
      return ConstantRange::getNonEmpty(KnownStart.getMinValue(),
                                        KnownEnd.getMaxValue() + 1);
    break;
  }
  case Instruction::LShr: {
    // For each lshr, three cases:
    //   shift = 0 => unchanged value
    //   saturation => 0
    //   other => a smaller positive number
    // Thus, the low end of the unsigned range is the last value produced.
    auto KnownEnd = KnownBits::lshr(KnownStart,
                                    KnownBits::makeConstant(TotalShift));
    return ConstantRange::getNonEmpty(KnownEnd.getMinValue(),
                                      KnownStart.getMaxValue() + 1);
  }
  case Instruction::Shl: {
    // Iff no bits are shifted out, value increases on every shift.
    auto KnownEnd = KnownBits::shl(KnownStart,
                                   KnownBits::makeConstant(TotalShift));
    if (TotalShift.ult(KnownStart.countMinLeadingZeros()))
      return ConstantRange(KnownStart.getMinValue(),
                           KnownEnd.getMaxValue() + 1);
    break;
  }
  };
  return FullSet;
}

const ConstantRange &
ScalarEvolution::getRangeRefIter(const SCEV *S,
                                 ScalarEvolution::RangeSignHint SignHint) {
  DenseMap<const SCEV *, ConstantRange> &Cache =
      SignHint == ScalarEvolution::HINT_RANGE_UNSIGNED ? UnsignedRanges
                                                       : SignedRanges;
  SmallVector<const SCEV *> WorkList;
  SmallPtrSet<const SCEV *, 8> Seen;

  // Add Expr to the worklist, if Expr is either an N-ary expression or a
  // SCEVUnknown PHI node.
  auto AddToWorklist = [&WorkList, &Seen, &Cache](const SCEV *Expr) {
    if (!Seen.insert(Expr).second)
      return;
    if (Cache.contains(Expr))
      return;
    switch (Expr->getSCEVType()) {
    case scUnknown:
      if (!isa<PHINode>(cast<SCEVUnknown>(Expr)->getValue()))
        break;
      [[fallthrough]];
    case scConstant:
    case scVScale:
    case scTruncate:
    case scZeroExtend:
    case scSignExtend:
    case scPtrToInt:
    case scAddExpr:
    case scMulExpr:
    case scUDivExpr:
    case scAddRecExpr:
    case scUMaxExpr:
    case scSMaxExpr:
    case scUMinExpr:
    case scSMinExpr:
    case scSequentialUMinExpr:
      WorkList.push_back(Expr);
      break;
    case scCouldNotCompute:
      llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
    }
  };
  AddToWorklist(S);

  // Build worklist by queuing operands of N-ary expressions and phi nodes.
  for (unsigned I = 0; I != WorkList.size(); ++I) {
    const SCEV *P = WorkList[I];
    auto *UnknownS = dyn_cast<SCEVUnknown>(P);
    // If it is not a `SCEVUnknown`, just recurse into operands.
    if (!UnknownS) {
      for (const SCEV *Op : P->operands())
        AddToWorklist(Op);
      continue;
    }
    // `SCEVUnknown`'s require special treatment.
    if (const PHINode *P = dyn_cast<PHINode>(UnknownS->getValue())) {
      if (!PendingPhiRangesIter.insert(P).second)
        continue;
      for (auto &Op : reverse(P->operands()))
        AddToWorklist(getSCEV(Op));
    }
  }

  if (!WorkList.empty()) {
    // Use getRangeRef to compute ranges for items in the worklist in reverse
    // order. This will force ranges for earlier operands to be computed before
    // their users in most cases.
    for (const SCEV *P : reverse(drop_begin(WorkList))) {
      getRangeRef(P, SignHint);

      if (auto *UnknownS = dyn_cast<SCEVUnknown>(P))
        if (const PHINode *P = dyn_cast<PHINode>(UnknownS->getValue()))
          PendingPhiRangesIter.erase(P);
    }
  }

  return getRangeRef(S, SignHint, 0);
}

/// Determine the range for a particular SCEV.  If SignHint is
/// HINT_RANGE_UNSIGNED (resp. HINT_RANGE_SIGNED) then getRange prefers ranges
/// with a "cleaner" unsigned (resp. signed) representation.
const ConstantRange &ScalarEvolution::getRangeRef(
    const SCEV *S, ScalarEvolution::RangeSignHint SignHint, unsigned Depth) {
  DenseMap<const SCEV *, ConstantRange> &Cache =
      SignHint == ScalarEvolution::HINT_RANGE_UNSIGNED ? UnsignedRanges
                                                       : SignedRanges;
  ConstantRange::PreferredRangeType RangeType =
      SignHint == ScalarEvolution::HINT_RANGE_UNSIGNED ? ConstantRange::Unsigned
                                                       : ConstantRange::Signed;

  // See if we've computed this range already.
  DenseMap<const SCEV *, ConstantRange>::iterator I = Cache.find(S);
  if (I != Cache.end())
    return I->second;

  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(S))
    return setRange(C, SignHint, ConstantRange(C->getAPInt()));

  // Switch to iteratively computing the range for S, if it is part of a deeply
  // nested expression.
  if (Depth > RangeIterThreshold)
    return getRangeRefIter(S, SignHint);

  unsigned BitWidth = getTypeSizeInBits(S->getType());
  ConstantRange ConservativeResult(BitWidth, /*isFullSet=*/true);
  using OBO = OverflowingBinaryOperator;

  // If the value has known zeros, the maximum value will have those known zeros
  // as well.
  if (SignHint == ScalarEvolution::HINT_RANGE_UNSIGNED) {
    APInt Multiple = getNonZeroConstantMultiple(S);
    APInt Remainder = APInt::getMaxValue(BitWidth).urem(Multiple);
    if (!Remainder.isZero())
      ConservativeResult =
          ConstantRange(APInt::getMinValue(BitWidth),
                        APInt::getMaxValue(BitWidth) - Remainder + 1);
  }
  else {
    uint32_t TZ = getMinTrailingZeros(S);
    if (TZ != 0) {
      ConservativeResult = ConstantRange(
          APInt::getSignedMinValue(BitWidth),
          APInt::getSignedMaxValue(BitWidth).ashr(TZ).shl(TZ) + 1);
    }
  }

  switch (S->getSCEVType()) {
  case scConstant:
    llvm_unreachable("Already handled above.");
  case scVScale:
    return setRange(S, SignHint, getVScaleRange(&F, BitWidth));
  case scTruncate: {
    const SCEVTruncateExpr *Trunc = cast<SCEVTruncateExpr>(S);
    ConstantRange X = getRangeRef(Trunc->getOperand(), SignHint, Depth + 1);
    return setRange(
        Trunc, SignHint,
        ConservativeResult.intersectWith(X.truncate(BitWidth), RangeType));
  }
  case scZeroExtend: {
    const SCEVZeroExtendExpr *ZExt = cast<SCEVZeroExtendExpr>(S);
    ConstantRange X = getRangeRef(ZExt->getOperand(), SignHint, Depth + 1);
    return setRange(
        ZExt, SignHint,
        ConservativeResult.intersectWith(X.zeroExtend(BitWidth), RangeType));
  }
  case scSignExtend: {
    const SCEVSignExtendExpr *SExt = cast<SCEVSignExtendExpr>(S);
    ConstantRange X = getRangeRef(SExt->getOperand(), SignHint, Depth + 1);
    return setRange(
        SExt, SignHint,
        ConservativeResult.intersectWith(X.signExtend(BitWidth), RangeType));
  }
  case scPtrToInt: {
    const SCEVPtrToIntExpr *PtrToInt = cast<SCEVPtrToIntExpr>(S);
    ConstantRange X = getRangeRef(PtrToInt->getOperand(), SignHint, Depth + 1);
    return setRange(PtrToInt, SignHint, X);
  }
  case scAddExpr: {
    const SCEVAddExpr *Add = cast<SCEVAddExpr>(S);
    ConstantRange X = getRangeRef(Add->getOperand(0), SignHint, Depth + 1);
    unsigned WrapType = OBO::AnyWrap;
    if (Add->hasNoSignedWrap())
      WrapType |= OBO::NoSignedWrap;
    if (Add->hasNoUnsignedWrap())
      WrapType |= OBO::NoUnsignedWrap;
    for (unsigned i = 1, e = Add->getNumOperands(); i != e; ++i)
      X = X.addWithNoWrap(getRangeRef(Add->getOperand(i), SignHint, Depth + 1),
                          WrapType, RangeType);
    return setRange(Add, SignHint,
                    ConservativeResult.intersectWith(X, RangeType));
  }
  case scMulExpr: {
    const SCEVMulExpr *Mul = cast<SCEVMulExpr>(S);
    ConstantRange X = getRangeRef(Mul->getOperand(0), SignHint, Depth + 1);
    for (unsigned i = 1, e = Mul->getNumOperands(); i != e; ++i)
      X = X.multiply(getRangeRef(Mul->getOperand(i), SignHint, Depth + 1));
    return setRange(Mul, SignHint,
                    ConservativeResult.intersectWith(X, RangeType));
  }
  case scUDivExpr: {
    const SCEVUDivExpr *UDiv = cast<SCEVUDivExpr>(S);
    ConstantRange X = getRangeRef(UDiv->getLHS(), SignHint, Depth + 1);
    ConstantRange Y = getRangeRef(UDiv->getRHS(), SignHint, Depth + 1);
    return setRange(UDiv, SignHint,
                    ConservativeResult.intersectWith(X.udiv(Y), RangeType));
  }
  case scAddRecExpr: {
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(S);
    // If there's no unsigned wrap, the value will never be less than its
    // initial value.
    if (AddRec->hasNoUnsignedWrap()) {
      APInt UnsignedMinValue = getUnsignedRangeMin(AddRec->getStart());
      if (!UnsignedMinValue.isZero())
        ConservativeResult = ConservativeResult.intersectWith(
            ConstantRange(UnsignedMinValue, APInt(BitWidth, 0)), RangeType);
    }

    // If there's no signed wrap, and all the operands except initial value have
    // the same sign or zero, the value won't ever be:
    // 1: smaller than initial value if operands are non negative,
    // 2: bigger than initial value if operands are non positive.
    // For both cases, value can not cross signed min/max boundary.
    if (AddRec->hasNoSignedWrap()) {
      bool AllNonNeg = true;
      bool AllNonPos = true;
      for (unsigned i = 1, e = AddRec->getNumOperands(); i != e; ++i) {
        if (!isKnownNonNegative(AddRec->getOperand(i)))
          AllNonNeg = false;
        if (!isKnownNonPositive(AddRec->getOperand(i)))
          AllNonPos = false;
      }
      if (AllNonNeg)
        ConservativeResult = ConservativeResult.intersectWith(
            ConstantRange::getNonEmpty(getSignedRangeMin(AddRec->getStart()),
                                       APInt::getSignedMinValue(BitWidth)),
            RangeType);
      else if (AllNonPos)
        ConservativeResult = ConservativeResult.intersectWith(
            ConstantRange::getNonEmpty(APInt::getSignedMinValue(BitWidth),
                                       getSignedRangeMax(AddRec->getStart()) +
                                           1),
            RangeType);
    }

    // TODO: non-affine addrec
    if (AddRec->isAffine()) {
      const SCEV *MaxBEScev =
          getConstantMaxBackedgeTakenCount(AddRec->getLoop());
      if (!isa<SCEVCouldNotCompute>(MaxBEScev)) {
        APInt MaxBECount = cast<SCEVConstant>(MaxBEScev)->getAPInt();

        // Adjust MaxBECount to the same bitwidth as AddRec. We can truncate if
        // MaxBECount's active bits are all <= AddRec's bit width.
        if (MaxBECount.getBitWidth() > BitWidth &&
            MaxBECount.getActiveBits() <= BitWidth)
          MaxBECount = MaxBECount.trunc(BitWidth);
        else if (MaxBECount.getBitWidth() < BitWidth)
          MaxBECount = MaxBECount.zext(BitWidth);

        if (MaxBECount.getBitWidth() == BitWidth) {
          auto RangeFromAffine = getRangeForAffineAR(
              AddRec->getStart(), AddRec->getStepRecurrence(*this), MaxBECount);
          ConservativeResult =
              ConservativeResult.intersectWith(RangeFromAffine, RangeType);

          auto RangeFromFactoring = getRangeViaFactoring(
              AddRec->getStart(), AddRec->getStepRecurrence(*this), MaxBECount);
          ConservativeResult =
              ConservativeResult.intersectWith(RangeFromFactoring, RangeType);
        }
      }

      // Now try symbolic BE count and more powerful methods.
      if (UseExpensiveRangeSharpening) {
        const SCEV *SymbolicMaxBECount =
            getSymbolicMaxBackedgeTakenCount(AddRec->getLoop());
        if (!isa<SCEVCouldNotCompute>(SymbolicMaxBECount) &&
            getTypeSizeInBits(MaxBEScev->getType()) <= BitWidth &&
            AddRec->hasNoSelfWrap()) {
          auto RangeFromAffineNew = getRangeForAffineNoSelfWrappingAR(
              AddRec, SymbolicMaxBECount, BitWidth, SignHint);
          ConservativeResult =
              ConservativeResult.intersectWith(RangeFromAffineNew, RangeType);
        }
      }
    }

    return setRange(AddRec, SignHint, std::move(ConservativeResult));
  }
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr: {
    Intrinsic::ID ID;
    switch (S->getSCEVType()) {
    case scUMaxExpr:
      ID = Intrinsic::umax;
      break;
    case scSMaxExpr:
      ID = Intrinsic::smax;
      break;
    case scUMinExpr:
    case scSequentialUMinExpr:
      ID = Intrinsic::umin;
      break;
    case scSMinExpr:
      ID = Intrinsic::smin;
      break;
    default:
      llvm_unreachable("Unknown SCEVMinMaxExpr/SCEVSequentialMinMaxExpr.");
    }

    const auto *NAry = cast<SCEVNAryExpr>(S);
    ConstantRange X = getRangeRef(NAry->getOperand(0), SignHint, Depth + 1);
    for (unsigned i = 1, e = NAry->getNumOperands(); i != e; ++i)
      X = X.intrinsic(
          ID, {X, getRangeRef(NAry->getOperand(i), SignHint, Depth + 1)});
    return setRange(S, SignHint,
                    ConservativeResult.intersectWith(X, RangeType));
  }
  case scUnknown: {
    const SCEVUnknown *U = cast<SCEVUnknown>(S);
    Value *V = U->getValue();

    // Check if the IR explicitly contains !range metadata.
    std::optional<ConstantRange> MDRange = GetRangeFromMetadata(V);
    if (MDRange)
      ConservativeResult =
          ConservativeResult.intersectWith(*MDRange, RangeType);

    // Use facts about recurrences in the underlying IR.  Note that add
    // recurrences are AddRecExprs and thus don't hit this path.  This
    // primarily handles shift recurrences.
    auto CR = getRangeForUnknownRecurrence(U);
    ConservativeResult = ConservativeResult.intersectWith(CR);

    // See if ValueTracking can give us a useful range.
    const DataLayout &DL = getDataLayout();
    KnownBits Known = computeKnownBits(V, DL, 0, &AC, nullptr, &DT);
    if (Known.getBitWidth() != BitWidth)
      Known = Known.zextOrTrunc(BitWidth);

    // ValueTracking may be able to compute a tighter result for the number of
    // sign bits than for the value of those sign bits.
    unsigned NS = ComputeNumSignBits(V, DL, 0, &AC, nullptr, &DT);
    if (U->getType()->isPointerTy()) {
      // If the pointer size is larger than the index size type, this can cause
      // NS to be larger than BitWidth. So compensate for this.
      unsigned ptrSize = DL.getPointerTypeSizeInBits(U->getType());
      int ptrIdxDiff = ptrSize - BitWidth;
      if (ptrIdxDiff > 0 && ptrSize > BitWidth && NS > (unsigned)ptrIdxDiff)
        NS -= ptrIdxDiff;
    }

    if (NS > 1) {
      // If we know any of the sign bits, we know all of the sign bits.
      if (!Known.Zero.getHiBits(NS).isZero())
        Known.Zero.setHighBits(NS);
      if (!Known.One.getHiBits(NS).isZero())
        Known.One.setHighBits(NS);
    }

    if (Known.getMinValue() != Known.getMaxValue() + 1)
      ConservativeResult = ConservativeResult.intersectWith(
          ConstantRange(Known.getMinValue(), Known.getMaxValue() + 1),
          RangeType);
    if (NS > 1)
      ConservativeResult = ConservativeResult.intersectWith(
          ConstantRange(APInt::getSignedMinValue(BitWidth).ashr(NS - 1),
                        APInt::getSignedMaxValue(BitWidth).ashr(NS - 1) + 1),
          RangeType);

    if (U->getType()->isPointerTy() && SignHint == HINT_RANGE_UNSIGNED) {
      // Strengthen the range if the underlying IR value is a
      // global/alloca/heap allocation using the size of the object.
      ObjectSizeOpts Opts;
      Opts.RoundToAlign = false;
      Opts.NullIsUnknownSize = true;
      uint64_t ObjSize;
      if ((isa<GlobalVariable>(V) || isa<AllocaInst>(V) ||
           isAllocationFn(V, &TLI)) &&
          getObjectSize(V, ObjSize, DL, &TLI, Opts) && ObjSize > 1) {
        // The highest address the object can start is ObjSize bytes before the
        // end (unsigned max value). If this value is not a multiple of the
        // alignment, the last possible start value is the next lowest multiple
        // of the alignment. Note: The computations below cannot overflow,
        // because if they would there's no possible start address for the
        // object.
        APInt MaxVal = APInt::getMaxValue(BitWidth) - APInt(BitWidth, ObjSize);
        uint64_t Align = U->getValue()->getPointerAlignment(DL).value();
        uint64_t Rem = MaxVal.urem(Align);
        MaxVal -= APInt(BitWidth, Rem);
        APInt MinVal = APInt::getZero(BitWidth);
        if (llvm::isKnownNonZero(V, DL))
          MinVal = Align;
        ConservativeResult = ConservativeResult.intersectWith(
            ConstantRange::getNonEmpty(MinVal, MaxVal + 1), RangeType);
      }
    }

    // A range of Phi is a subset of union of all ranges of its input.
    if (PHINode *Phi = dyn_cast<PHINode>(V)) {
      // Make sure that we do not run over cycled Phis.
      if (PendingPhiRanges.insert(Phi).second) {
        ConstantRange RangeFromOps(BitWidth, /*isFullSet=*/false);

        for (const auto &Op : Phi->operands()) {
          auto OpRange = getRangeRef(getSCEV(Op), SignHint, Depth + 1);
          RangeFromOps = RangeFromOps.unionWith(OpRange);
          // No point to continue if we already have a full set.
          if (RangeFromOps.isFullSet())
            break;
        }
        ConservativeResult =
            ConservativeResult.intersectWith(RangeFromOps, RangeType);
        bool Erased = PendingPhiRanges.erase(Phi);
        assert(Erased && "Failed to erase Phi properly?");
        (void)Erased;
      }
    }

    // vscale can't be equal to zero
    if (const auto *II = dyn_cast<IntrinsicInst>(V))
      if (II->getIntrinsicID() == Intrinsic::vscale) {
        ConstantRange Disallowed = APInt::getZero(BitWidth);
        ConservativeResult = ConservativeResult.difference(Disallowed);
      }

    return setRange(U, SignHint, std::move(ConservativeResult));
  }
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }

  return setRange(S, SignHint, std::move(ConservativeResult));
}

// Given a StartRange, Step and MaxBECount for an expression compute a range of
// values that the expression can take. Initially, the expression has a value
// from StartRange and then is changed by Step up to MaxBECount times. Signed
// argument defines if we treat Step as signed or unsigned.
static ConstantRange getRangeForAffineARHelper(APInt Step,
                                               const ConstantRange &StartRange,
                                               const APInt &MaxBECount,
                                               bool Signed) {
  unsigned BitWidth = Step.getBitWidth();
  assert(BitWidth == StartRange.getBitWidth() &&
         BitWidth == MaxBECount.getBitWidth() && "mismatched bit widths");
  // If either Step or MaxBECount is 0, then the expression won't change, and we
  // just need to return the initial range.
  if (Step == 0 || MaxBECount == 0)
    return StartRange;

  // If we don't know anything about the initial value (i.e. StartRange is
  // FullRange), then we don't know anything about the final range either.
  // Return FullRange.
  if (StartRange.isFullSet())
    return ConstantRange::getFull(BitWidth);

  // If Step is signed and negative, then we use its absolute value, but we also
  // note that we're moving in the opposite direction.
  bool Descending = Signed && Step.isNegative();

  if (Signed)
    // This is correct even for INT_SMIN. Let's look at i8 to illustrate this:
    // abs(INT_SMIN) = abs(-128) = abs(0x80) = -0x80 = 0x80 = 128.
    // This equations hold true due to the well-defined wrap-around behavior of
    // APInt.
    Step = Step.abs();

  // Check if Offset is more than full span of BitWidth. If it is, the
  // expression is guaranteed to overflow.
  if (APInt::getMaxValue(StartRange.getBitWidth()).udiv(Step).ult(MaxBECount))
    return ConstantRange::getFull(BitWidth);

  // Offset is by how much the expression can change. Checks above guarantee no
  // overflow here.
  APInt Offset = Step * MaxBECount;

  // Minimum value of the final range will match the minimal value of StartRange
  // if the expression is increasing and will be decreased by Offset otherwise.
  // Maximum value of the final range will match the maximal value of StartRange
  // if the expression is decreasing and will be increased by Offset otherwise.
  APInt StartLower = StartRange.getLower();
  APInt StartUpper = StartRange.getUpper() - 1;
  APInt MovedBoundary = Descending ? (StartLower - std::move(Offset))
                                   : (StartUpper + std::move(Offset));

  // It's possible that the new minimum/maximum value will fall into the initial
  // range (due to wrap around). This means that the expression can take any
  // value in this bitwidth, and we have to return full range.
  if (StartRange.contains(MovedBoundary))
    return ConstantRange::getFull(BitWidth);

  APInt NewLower =
      Descending ? std::move(MovedBoundary) : std::move(StartLower);
  APInt NewUpper =
      Descending ? std::move(StartUpper) : std::move(MovedBoundary);
  NewUpper += 1;

  // No overflow detected, return [StartLower, StartUpper + Offset + 1) range.
  return ConstantRange::getNonEmpty(std::move(NewLower), std::move(NewUpper));
}

ConstantRange ScalarEvolution::getRangeForAffineAR(const SCEV *Start,
                                                   const SCEV *Step,
                                                   const APInt &MaxBECount) {
  assert(getTypeSizeInBits(Start->getType()) ==
             getTypeSizeInBits(Step->getType()) &&
         getTypeSizeInBits(Start->getType()) == MaxBECount.getBitWidth() &&
         "mismatched bit widths");

  // First, consider step signed.
  ConstantRange StartSRange = getSignedRange(Start);
  ConstantRange StepSRange = getSignedRange(Step);

  // If Step can be both positive and negative, we need to find ranges for the
  // maximum absolute step values in both directions and union them.
  ConstantRange SR = getRangeForAffineARHelper(
      StepSRange.getSignedMin(), StartSRange, MaxBECount, /* Signed = */ true);
  SR = SR.unionWith(getRangeForAffineARHelper(StepSRange.getSignedMax(),
                                              StartSRange, MaxBECount,
                                              /* Signed = */ true));

  // Next, consider step unsigned.
  ConstantRange UR = getRangeForAffineARHelper(
      getUnsignedRangeMax(Step), getUnsignedRange(Start), MaxBECount,
      /* Signed = */ false);

  // Finally, intersect signed and unsigned ranges.
  return SR.intersectWith(UR, ConstantRange::Smallest);
}

ConstantRange ScalarEvolution::getRangeForAffineNoSelfWrappingAR(
    const SCEVAddRecExpr *AddRec, const SCEV *MaxBECount, unsigned BitWidth,
    ScalarEvolution::RangeSignHint SignHint) {
  assert(AddRec->isAffine() && "Non-affine AddRecs are not suppored!\n");
  assert(AddRec->hasNoSelfWrap() &&
         "This only works for non-self-wrapping AddRecs!");
  const bool IsSigned = SignHint == HINT_RANGE_SIGNED;
  const SCEV *Step = AddRec->getStepRecurrence(*this);
  // Only deal with constant step to save compile time.
  if (!isa<SCEVConstant>(Step))
    return ConstantRange::getFull(BitWidth);
  // Let's make sure that we can prove that we do not self-wrap during
  // MaxBECount iterations. We need this because MaxBECount is a maximum
  // iteration count estimate, and we might infer nw from some exit for which we
  // do not know max exit count (or any other side reasoning).
  // TODO: Turn into assert at some point.
  if (getTypeSizeInBits(MaxBECount->getType()) >
      getTypeSizeInBits(AddRec->getType()))
    return ConstantRange::getFull(BitWidth);
  MaxBECount = getNoopOrZeroExtend(MaxBECount, AddRec->getType());
  const SCEV *RangeWidth = getMinusOne(AddRec->getType());
  const SCEV *StepAbs = getUMinExpr(Step, getNegativeSCEV(Step));
  const SCEV *MaxItersWithoutWrap = getUDivExpr(RangeWidth, StepAbs);
  if (!isKnownPredicateViaConstantRanges(ICmpInst::ICMP_ULE, MaxBECount,
                                         MaxItersWithoutWrap))
    return ConstantRange::getFull(BitWidth);

  ICmpInst::Predicate LEPred =
      IsSigned ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE;
  ICmpInst::Predicate GEPred =
      IsSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE;
  const SCEV *End = AddRec->evaluateAtIteration(MaxBECount, *this);

  // We know that there is no self-wrap. Let's take Start and End values and
  // look at all intermediate values V1, V2, ..., Vn that IndVar takes during
  // the iteration. They either lie inside the range [Min(Start, End),
  // Max(Start, End)] or outside it:
  //
  // Case 1:   RangeMin    ...    Start V1 ... VN End ...           RangeMax;
  // Case 2:   RangeMin Vk ... V1 Start    ...    End Vn ... Vk + 1 RangeMax;
  //
  // No self wrap flag guarantees that the intermediate values cannot be BOTH
  // outside and inside the range [Min(Start, End), Max(Start, End)]. Using that
  // knowledge, let's try to prove that we are dealing with Case 1. It is so if
  // Start <= End and step is positive, or Start >= End and step is negative.
  const SCEV *Start = applyLoopGuards(AddRec->getStart(), AddRec->getLoop());
  ConstantRange StartRange = getRangeRef(Start, SignHint);
  ConstantRange EndRange = getRangeRef(End, SignHint);
  ConstantRange RangeBetween = StartRange.unionWith(EndRange);
  // If they already cover full iteration space, we will know nothing useful
  // even if we prove what we want to prove.
  if (RangeBetween.isFullSet())
    return RangeBetween;
  // Only deal with ranges that do not wrap (i.e. RangeMin < RangeMax).
  bool IsWrappedSet = IsSigned ? RangeBetween.isSignWrappedSet()
                               : RangeBetween.isWrappedSet();
  if (IsWrappedSet)
    return ConstantRange::getFull(BitWidth);

  if (isKnownPositive(Step) &&
      isKnownPredicateViaConstantRanges(LEPred, Start, End))
    return RangeBetween;
  if (isKnownNegative(Step) &&
           isKnownPredicateViaConstantRanges(GEPred, Start, End))
    return RangeBetween;
  return ConstantRange::getFull(BitWidth);
}

ConstantRange ScalarEvolution::getRangeViaFactoring(const SCEV *Start,
                                                    const SCEV *Step,
                                                    const APInt &MaxBECount) {
  //    RangeOf({C?A:B,+,C?P:Q}) == RangeOf(C?{A,+,P}:{B,+,Q})
  // == RangeOf({A,+,P}) union RangeOf({B,+,Q})

  unsigned BitWidth = MaxBECount.getBitWidth();
  assert(getTypeSizeInBits(Start->getType()) == BitWidth &&
         getTypeSizeInBits(Step->getType()) == BitWidth &&
         "mismatched bit widths");

  struct SelectPattern {
    Value *Condition = nullptr;
    APInt TrueValue;
    APInt FalseValue;

    explicit SelectPattern(ScalarEvolution &SE, unsigned BitWidth,
                           const SCEV *S) {
      std::optional<unsigned> CastOp;
      APInt Offset(BitWidth, 0);

      assert(SE.getTypeSizeInBits(S->getType()) == BitWidth &&
             "Should be!");

      // Peel off a constant offset:
      if (auto *SA = dyn_cast<SCEVAddExpr>(S)) {
        // In the future we could consider being smarter here and handle
        // {Start+Step,+,Step} too.
        if (SA->getNumOperands() != 2 || !isa<SCEVConstant>(SA->getOperand(0)))
          return;

        Offset = cast<SCEVConstant>(SA->getOperand(0))->getAPInt();
        S = SA->getOperand(1);
      }

      // Peel off a cast operation
      if (auto *SCast = dyn_cast<SCEVIntegralCastExpr>(S)) {
        CastOp = SCast->getSCEVType();
        S = SCast->getOperand();
      }

      using namespace llvm::PatternMatch;

      auto *SU = dyn_cast<SCEVUnknown>(S);
      const APInt *TrueVal, *FalseVal;
      if (!SU ||
          !match(SU->getValue(), m_Select(m_Value(Condition), m_APInt(TrueVal),
                                          m_APInt(FalseVal)))) {
        Condition = nullptr;
        return;
      }

      TrueValue = *TrueVal;
      FalseValue = *FalseVal;

      // Re-apply the cast we peeled off earlier
      if (CastOp)
        switch (*CastOp) {
        default:
          llvm_unreachable("Unknown SCEV cast type!");

        case scTruncate:
          TrueValue = TrueValue.trunc(BitWidth);
          FalseValue = FalseValue.trunc(BitWidth);
          break;
        case scZeroExtend:
          TrueValue = TrueValue.zext(BitWidth);
          FalseValue = FalseValue.zext(BitWidth);
          break;
        case scSignExtend:
          TrueValue = TrueValue.sext(BitWidth);
          FalseValue = FalseValue.sext(BitWidth);
          break;
        }

      // Re-apply the constant offset we peeled off earlier
      TrueValue += Offset;
      FalseValue += Offset;
    }

    bool isRecognized() { return Condition != nullptr; }
  };

  SelectPattern StartPattern(*this, BitWidth, Start);
  if (!StartPattern.isRecognized())
    return ConstantRange::getFull(BitWidth);

  SelectPattern StepPattern(*this, BitWidth, Step);
  if (!StepPattern.isRecognized())
    return ConstantRange::getFull(BitWidth);

  if (StartPattern.Condition != StepPattern.Condition) {
    // We don't handle this case today; but we could, by considering four
    // possibilities below instead of two. I'm not sure if there are cases where
    // that will help over what getRange already does, though.
    return ConstantRange::getFull(BitWidth);
  }

  // NB! Calling ScalarEvolution::getConstant is fine, but we should not try to
  // construct arbitrary general SCEV expressions here.  This function is called
  // from deep in the call stack, and calling getSCEV (on a sext instruction,
  // say) can end up caching a suboptimal value.

  // FIXME: without the explicit `this` receiver below, MSVC errors out with
  // C2352 and C2512 (otherwise it isn't needed).

  const SCEV *TrueStart = this->getConstant(StartPattern.TrueValue);
  const SCEV *TrueStep = this->getConstant(StepPattern.TrueValue);
  const SCEV *FalseStart = this->getConstant(StartPattern.FalseValue);
  const SCEV *FalseStep = this->getConstant(StepPattern.FalseValue);

  ConstantRange TrueRange =
      this->getRangeForAffineAR(TrueStart, TrueStep, MaxBECount);
  ConstantRange FalseRange =
      this->getRangeForAffineAR(FalseStart, FalseStep, MaxBECount);

  return TrueRange.unionWith(FalseRange);
}

SCEV::NoWrapFlags ScalarEvolution::getNoWrapFlagsFromUB(const Value *V) {
  if (isa<ConstantExpr>(V)) return SCEV::FlagAnyWrap;
  const BinaryOperator *BinOp = cast<BinaryOperator>(V);

  // Return early if there are no flags to propagate to the SCEV.
  SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;
  if (BinOp->hasNoUnsignedWrap())
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNUW);
  if (BinOp->hasNoSignedWrap())
    Flags = ScalarEvolution::setFlags(Flags, SCEV::FlagNSW);
  if (Flags == SCEV::FlagAnyWrap)
    return SCEV::FlagAnyWrap;

  return isSCEVExprNeverPoison(BinOp) ? Flags : SCEV::FlagAnyWrap;
}

const Instruction *
ScalarEvolution::getNonTrivialDefiningScopeBound(const SCEV *S) {
  if (auto *AddRec = dyn_cast<SCEVAddRecExpr>(S))
    return &*AddRec->getLoop()->getHeader()->begin();
  if (auto *U = dyn_cast<SCEVUnknown>(S))
    if (auto *I = dyn_cast<Instruction>(U->getValue()))
      return I;
  return nullptr;
}

const Instruction *
ScalarEvolution::getDefiningScopeBound(ArrayRef<const SCEV *> Ops,
                                       bool &Precise) {
  Precise = true;
  // Do a bounded search of the def relation of the requested SCEVs.
  SmallSet<const SCEV *, 16> Visited;
  SmallVector<const SCEV *> Worklist;
  auto pushOp = [&](const SCEV *S) {
    if (!Visited.insert(S).second)
      return;
    // Threshold of 30 here is arbitrary.
    if (Visited.size() > 30) {
      Precise = false;
      return;
    }
    Worklist.push_back(S);
  };

  for (const auto *S : Ops)
    pushOp(S);

  const Instruction *Bound = nullptr;
  while (!Worklist.empty()) {
    auto *S = Worklist.pop_back_val();
    if (auto *DefI = getNonTrivialDefiningScopeBound(S)) {
      if (!Bound || DT.dominates(Bound, DefI))
        Bound = DefI;
    } else {
      for (const auto *Op : S->operands())
        pushOp(Op);
    }
  }
  return Bound ? Bound : &*F.getEntryBlock().begin();
}

const Instruction *
ScalarEvolution::getDefiningScopeBound(ArrayRef<const SCEV *> Ops) {
  bool Discard;
  return getDefiningScopeBound(Ops, Discard);
}

bool ScalarEvolution::isGuaranteedToTransferExecutionTo(const Instruction *A,
                                                        const Instruction *B) {
  if (A->getParent() == B->getParent() &&
      isGuaranteedToTransferExecutionToSuccessor(A->getIterator(),
                                                 B->getIterator()))
    return true;

  auto *BLoop = LI.getLoopFor(B->getParent());
  if (BLoop && BLoop->getHeader() == B->getParent() &&
      BLoop->getLoopPreheader() == A->getParent() &&
      isGuaranteedToTransferExecutionToSuccessor(A->getIterator(),
                                                 A->getParent()->end()) &&
      isGuaranteedToTransferExecutionToSuccessor(B->getParent()->begin(),
                                                 B->getIterator()))
    return true;
  return false;
}


bool ScalarEvolution::isSCEVExprNeverPoison(const Instruction *I) {
  // Only proceed if we can prove that I does not yield poison.
  if (!programUndefinedIfPoison(I))
    return false;

  // At this point we know that if I is executed, then it does not wrap
  // according to at least one of NSW or NUW. If I is not executed, then we do
  // not know if the calculation that I represents would wrap. Multiple
  // instructions can map to the same SCEV. If we apply NSW or NUW from I to
  // the SCEV, we must guarantee no wrapping for that SCEV also when it is
  // derived from other instructions that map to the same SCEV. We cannot make
  // that guarantee for cases where I is not executed. So we need to find a
  // upper bound on the defining scope for the SCEV, and prove that I is
  // executed every time we enter that scope.  When the bounding scope is a
  // loop (the common case), this is equivalent to proving I executes on every
  // iteration of that loop.
  SmallVector<const SCEV *> SCEVOps;
  for (const Use &Op : I->operands()) {
    // I could be an extractvalue from a call to an overflow intrinsic.
    // TODO: We can do better here in some cases.
    if (isSCEVable(Op->getType()))
      SCEVOps.push_back(getSCEV(Op));
  }
  auto *DefI = getDefiningScopeBound(SCEVOps);
  return isGuaranteedToTransferExecutionTo(DefI, I);
}

bool ScalarEvolution::isAddRecNeverPoison(const Instruction *I, const Loop *L) {
  // If we know that \c I can never be poison period, then that's enough.
  if (isSCEVExprNeverPoison(I))
    return true;

  // If the loop only has one exit, then we know that, if the loop is entered,
  // any instruction dominating that exit will be executed. If any such
  // instruction would result in UB, the addrec cannot be poison.
  //
  // This is basically the same reasoning as in isSCEVExprNeverPoison(), but
  // also handles uses outside the loop header (they just need to dominate the
  // single exit).

  auto *ExitingBB = L->getExitingBlock();
  if (!ExitingBB || !loopHasNoAbnormalExits(L))
    return false;

  SmallPtrSet<const Value *, 16> KnownPoison;
  SmallVector<const Instruction *, 8> Worklist;

  // We start by assuming \c I, the post-inc add recurrence, is poison.  Only
  // things that are known to be poison under that assumption go on the
  // Worklist.
  KnownPoison.insert(I);
  Worklist.push_back(I);

  while (!Worklist.empty()) {
    const Instruction *Poison = Worklist.pop_back_val();

    for (const Use &U : Poison->uses()) {
      const Instruction *PoisonUser = cast<Instruction>(U.getUser());
      if (mustTriggerUB(PoisonUser, KnownPoison) &&
          DT.dominates(PoisonUser->getParent(), ExitingBB))
        return true;

      if (propagatesPoison(U) && L->contains(PoisonUser))
        if (KnownPoison.insert(PoisonUser).second)
          Worklist.push_back(PoisonUser);
    }
  }

  return false;
}

ScalarEvolution::LoopProperties
ScalarEvolution::getLoopProperties(const Loop *L) {
  using LoopProperties = ScalarEvolution::LoopProperties;

  auto Itr = LoopPropertiesCache.find(L);
  if (Itr == LoopPropertiesCache.end()) {
    auto HasSideEffects = [](Instruction *I) {
      if (auto *SI = dyn_cast<StoreInst>(I))
        return !SI->isSimple();

      return I->mayThrow() || I->mayWriteToMemory();
    };

    LoopProperties LP = {/* HasNoAbnormalExits */ true,
                         /*HasNoSideEffects*/ true};

    for (auto *BB : L->getBlocks())
      for (auto &I : *BB) {
        if (!isGuaranteedToTransferExecutionToSuccessor(&I))
          LP.HasNoAbnormalExits = false;
        if (HasSideEffects(&I))
          LP.HasNoSideEffects = false;
        if (!LP.HasNoAbnormalExits && !LP.HasNoSideEffects)
          break; // We're already as pessimistic as we can get.
      }

    auto InsertPair = LoopPropertiesCache.insert({L, LP});
    assert(InsertPair.second && "We just checked!");
    Itr = InsertPair.first;
  }

  return Itr->second;
}

bool ScalarEvolution::loopIsFiniteByAssumption(const Loop *L) {
  // A mustprogress loop without side effects must be finite.
  // TODO: The check used here is very conservative.  It's only *specific*
  // side effects which are well defined in infinite loops.
  return isFinite(L) || (isMustProgress(L) && loopHasNoSideEffects(L));
}

const SCEV *ScalarEvolution::createSCEVIter(Value *V) {
  // Worklist item with a Value and a bool indicating whether all operands have
  // been visited already.
  using PointerTy = PointerIntPair<Value *, 1, bool>;
  SmallVector<PointerTy> Stack;

  Stack.emplace_back(V, true);
  Stack.emplace_back(V, false);
  while (!Stack.empty()) {
    auto E = Stack.pop_back_val();
    Value *CurV = E.getPointer();

    if (getExistingSCEV(CurV))
      continue;

    SmallVector<Value *> Ops;
    const SCEV *CreatedSCEV = nullptr;
    // If all operands have been visited already, create the SCEV.
    if (E.getInt()) {
      CreatedSCEV = createSCEV(CurV);
    } else {
      // Otherwise get the operands we need to create SCEV's for before creating
      // the SCEV for CurV. If the SCEV for CurV can be constructed trivially,
      // just use it.
      CreatedSCEV = getOperandsToCreate(CurV, Ops);
    }

    if (CreatedSCEV) {
      insertValueToMap(CurV, CreatedSCEV);
    } else {
      // Queue CurV for SCEV creation, followed by its's operands which need to
      // be constructed first.
      Stack.emplace_back(CurV, true);
      for (Value *Op : Ops)
        Stack.emplace_back(Op, false);
    }
  }

  return getExistingSCEV(V);
}

const SCEV *
ScalarEvolution::getOperandsToCreate(Value *V, SmallVectorImpl<Value *> &Ops) {
  if (!isSCEVable(V->getType()))
    return getUnknown(V);

  if (Instruction *I = dyn_cast<Instruction>(V)) {
    // Don't attempt to analyze instructions in blocks that aren't
    // reachable. Such instructions don't matter, and they aren't required
    // to obey basic rules for definitions dominating uses which this
    // analysis depends on.
    if (!DT.isReachableFromEntry(I->getParent()))
      return getUnknown(PoisonValue::get(V->getType()));
  } else if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
    return getConstant(CI);
  else if (isa<GlobalAlias>(V))
    return getUnknown(V);
  else if (!isa<ConstantExpr>(V))
    return getUnknown(V);

  Operator *U = cast<Operator>(V);
  if (auto BO =
          MatchBinaryOp(U, getDataLayout(), AC, DT, dyn_cast<Instruction>(V))) {
    bool IsConstArg = isa<ConstantInt>(BO->RHS);
    switch (BO->Opcode) {
    case Instruction::Add:
    case Instruction::Mul: {
      // For additions and multiplications, traverse add/mul chains for which we
      // can potentially create a single SCEV, to reduce the number of
      // get{Add,Mul}Expr calls.
      do {
        if (BO->Op) {
          if (BO->Op != V && getExistingSCEV(BO->Op)) {
            Ops.push_back(BO->Op);
            break;
          }
        }
        Ops.push_back(BO->RHS);
        auto NewBO = MatchBinaryOp(BO->LHS, getDataLayout(), AC, DT,
                                   dyn_cast<Instruction>(V));
        if (!NewBO ||
            (BO->Opcode == Instruction::Add &&
             (NewBO->Opcode != Instruction::Add &&
              NewBO->Opcode != Instruction::Sub)) ||
            (BO->Opcode == Instruction::Mul &&
             NewBO->Opcode != Instruction::Mul)) {
          Ops.push_back(BO->LHS);
          break;
        }
        // CreateSCEV calls getNoWrapFlagsFromUB, which under certain conditions
        // requires a SCEV for the LHS.
        if (BO->Op && (BO->IsNSW || BO->IsNUW)) {
          auto *I = dyn_cast<Instruction>(BO->Op);
          if (I && programUndefinedIfPoison(I)) {
            Ops.push_back(BO->LHS);
            break;
          }
        }
        BO = NewBO;
      } while (true);
      return nullptr;
    }
    case Instruction::Sub:
    case Instruction::UDiv:
    case Instruction::URem:
      break;
    case Instruction::AShr:
    case Instruction::Shl:
    case Instruction::Xor:
      if (!IsConstArg)
        return nullptr;
      break;
    case Instruction::And:
    case Instruction::Or:
      if (!IsConstArg && !BO->LHS->getType()->isIntegerTy(1))
        return nullptr;
      break;
    case Instruction::LShr:
      return getUnknown(V);
    default:
      llvm_unreachable("Unhandled binop");
      break;
    }

    Ops.push_back(BO->LHS);
    Ops.push_back(BO->RHS);
    return nullptr;
  }

  switch (U->getOpcode()) {
  case Instruction::Trunc:
  case Instruction::ZExt:
  case Instruction::SExt:
  case Instruction::PtrToInt:
    Ops.push_back(U->getOperand(0));
    return nullptr;

  case Instruction::BitCast:
    if (isSCEVable(U->getType()) && isSCEVable(U->getOperand(0)->getType())) {
      Ops.push_back(U->getOperand(0));
      return nullptr;
    }
    return getUnknown(V);

  case Instruction::SDiv:
  case Instruction::SRem:
    Ops.push_back(U->getOperand(0));
    Ops.push_back(U->getOperand(1));
    return nullptr;

  case Instruction::GetElementPtr:
    assert(cast<GEPOperator>(U)->getSourceElementType()->isSized() &&
           "GEP source element type must be sized");
    for (Value *Index : U->operands())
      Ops.push_back(Index);
    return nullptr;

  case Instruction::IntToPtr:
    return getUnknown(V);

  case Instruction::PHI:
    // Keep constructing SCEVs' for phis recursively for now.
    return nullptr;

  case Instruction::Select: {
    // Check if U is a select that can be simplified to a SCEVUnknown.
    auto CanSimplifyToUnknown = [this, U]() {
      if (U->getType()->isIntegerTy(1) || isa<ConstantInt>(U->getOperand(0)))
        return false;

      auto *ICI = dyn_cast<ICmpInst>(U->getOperand(0));
      if (!ICI)
        return false;
      Value *LHS = ICI->getOperand(0);
      Value *RHS = ICI->getOperand(1);
      if (ICI->getPredicate() == CmpInst::ICMP_EQ ||
          ICI->getPredicate() == CmpInst::ICMP_NE) {
        if (!(isa<ConstantInt>(RHS) && cast<ConstantInt>(RHS)->isZero()))
          return true;
      } else if (getTypeSizeInBits(LHS->getType()) >
                 getTypeSizeInBits(U->getType()))
        return true;
      return false;
    };
    if (CanSimplifyToUnknown())
      return getUnknown(U);

    for (Value *Inc : U->operands())
      Ops.push_back(Inc);
    return nullptr;
    break;
  }
  case Instruction::Call:
  case Instruction::Invoke:
    if (Value *RV = cast<CallBase>(U)->getReturnedArgOperand()) {
      Ops.push_back(RV);
      return nullptr;
    }

    if (auto *II = dyn_cast<IntrinsicInst>(U)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::abs:
        Ops.push_back(II->getArgOperand(0));
        return nullptr;
      case Intrinsic::umax:
      case Intrinsic::umin:
      case Intrinsic::smax:
      case Intrinsic::smin:
      case Intrinsic::usub_sat:
      case Intrinsic::uadd_sat:
        Ops.push_back(II->getArgOperand(0));
        Ops.push_back(II->getArgOperand(1));
        return nullptr;
      case Intrinsic::start_loop_iterations:
      case Intrinsic::annotation:
      case Intrinsic::ptr_annotation:
        Ops.push_back(II->getArgOperand(0));
        return nullptr;
      default:
        break;
      }
    }
    break;
  }

  return nullptr;
}

const SCEV *ScalarEvolution::createSCEV(Value *V) {
  if (!isSCEVable(V->getType()))
    return getUnknown(V);

  if (Instruction *I = dyn_cast<Instruction>(V)) {
    // Don't attempt to analyze instructions in blocks that aren't
    // reachable. Such instructions don't matter, and they aren't required
    // to obey basic rules for definitions dominating uses which this
    // analysis depends on.
    if (!DT.isReachableFromEntry(I->getParent()))
      return getUnknown(PoisonValue::get(V->getType()));
  } else if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
    return getConstant(CI);
  else if (isa<GlobalAlias>(V))
    return getUnknown(V);
  else if (!isa<ConstantExpr>(V))
    return getUnknown(V);

  const SCEV *LHS;
  const SCEV *RHS;

  Operator *U = cast<Operator>(V);
  if (auto BO =
          MatchBinaryOp(U, getDataLayout(), AC, DT, dyn_cast<Instruction>(V))) {
    switch (BO->Opcode) {
    case Instruction::Add: {
      // The simple thing to do would be to just call getSCEV on both operands
      // and call getAddExpr with the result. However if we're looking at a
      // bunch of things all added together, this can be quite inefficient,
      // because it leads to N-1 getAddExpr calls for N ultimate operands.
      // Instead, gather up all the operands and make a single getAddExpr call.
      // LLVM IR canonical form means we need only traverse the left operands.
      SmallVector<const SCEV *, 4> AddOps;
      do {
        if (BO->Op) {
          if (auto *OpSCEV = getExistingSCEV(BO->Op)) {
            AddOps.push_back(OpSCEV);
            break;
          }

          // If a NUW or NSW flag can be applied to the SCEV for this
          // addition, then compute the SCEV for this addition by itself
          // with a separate call to getAddExpr. We need to do that
          // instead of pushing the operands of the addition onto AddOps,
          // since the flags are only known to apply to this particular
          // addition - they may not apply to other additions that can be
          // formed with operands from AddOps.
          const SCEV *RHS = getSCEV(BO->RHS);
          SCEV::NoWrapFlags Flags = getNoWrapFlagsFromUB(BO->Op);
          if (Flags != SCEV::FlagAnyWrap) {
            const SCEV *LHS = getSCEV(BO->LHS);
            if (BO->Opcode == Instruction::Sub)
              AddOps.push_back(getMinusSCEV(LHS, RHS, Flags));
            else
              AddOps.push_back(getAddExpr(LHS, RHS, Flags));
            break;
          }
        }

        if (BO->Opcode == Instruction::Sub)
          AddOps.push_back(getNegativeSCEV(getSCEV(BO->RHS)));
        else
          AddOps.push_back(getSCEV(BO->RHS));

        auto NewBO = MatchBinaryOp(BO->LHS, getDataLayout(), AC, DT,
                                   dyn_cast<Instruction>(V));
        if (!NewBO || (NewBO->Opcode != Instruction::Add &&
                       NewBO->Opcode != Instruction::Sub)) {
          AddOps.push_back(getSCEV(BO->LHS));
          break;
        }
        BO = NewBO;
      } while (true);

      return getAddExpr(AddOps);
    }

    case Instruction::Mul: {
      SmallVector<const SCEV *, 4> MulOps;
      do {
        if (BO->Op) {
          if (auto *OpSCEV = getExistingSCEV(BO->Op)) {
            MulOps.push_back(OpSCEV);
            break;
          }

          SCEV::NoWrapFlags Flags = getNoWrapFlagsFromUB(BO->Op);
          if (Flags != SCEV::FlagAnyWrap) {
            LHS = getSCEV(BO->LHS);
            RHS = getSCEV(BO->RHS);
            MulOps.push_back(getMulExpr(LHS, RHS, Flags));
            break;
          }
        }

        MulOps.push_back(getSCEV(BO->RHS));
        auto NewBO = MatchBinaryOp(BO->LHS, getDataLayout(), AC, DT,
                                   dyn_cast<Instruction>(V));
        if (!NewBO || NewBO->Opcode != Instruction::Mul) {
          MulOps.push_back(getSCEV(BO->LHS));
          break;
        }
        BO = NewBO;
      } while (true);

      return getMulExpr(MulOps);
    }
    case Instruction::UDiv:
      LHS = getSCEV(BO->LHS);
      RHS = getSCEV(BO->RHS);
      return getUDivExpr(LHS, RHS);
    case Instruction::URem:
      LHS = getSCEV(BO->LHS);
      RHS = getSCEV(BO->RHS);
      return getURemExpr(LHS, RHS);
    case Instruction::Sub: {
      SCEV::NoWrapFlags Flags = SCEV::FlagAnyWrap;
      if (BO->Op)
        Flags = getNoWrapFlagsFromUB(BO->Op);
      LHS = getSCEV(BO->LHS);
      RHS = getSCEV(BO->RHS);
      return getMinusSCEV(LHS, RHS, Flags);
    }
    case Instruction::And:
      // For an expression like x&255 that merely masks off the high bits,
      // use zext(trunc(x)) as the SCEV expression.
      if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->RHS)) {
        if (CI->isZero())
          return getSCEV(BO->RHS);
        if (CI->isMinusOne())
          return getSCEV(BO->LHS);
        const APInt &A = CI->getValue();

        // Instcombine's ShrinkDemandedConstant may strip bits out of
        // constants, obscuring what would otherwise be a low-bits mask.
        // Use computeKnownBits to compute what ShrinkDemandedConstant
        // knew about to reconstruct a low-bits mask value.
        unsigned LZ = A.countl_zero();
        unsigned TZ = A.countr_zero();
        unsigned BitWidth = A.getBitWidth();
        KnownBits Known(BitWidth);
        computeKnownBits(BO->LHS, Known, getDataLayout(),
                         0, &AC, nullptr, &DT);

        APInt EffectiveMask =
            APInt::getLowBitsSet(BitWidth, BitWidth - LZ - TZ).shl(TZ);
        if ((LZ != 0 || TZ != 0) && !((~A & ~Known.Zero) & EffectiveMask)) {
          const SCEV *MulCount = getConstant(APInt::getOneBitSet(BitWidth, TZ));
          const SCEV *LHS = getSCEV(BO->LHS);
          const SCEV *ShiftedLHS = nullptr;
          if (auto *LHSMul = dyn_cast<SCEVMulExpr>(LHS)) {
            if (auto *OpC = dyn_cast<SCEVConstant>(LHSMul->getOperand(0))) {
              // For an expression like (x * 8) & 8, simplify the multiply.
              unsigned MulZeros = OpC->getAPInt().countr_zero();
              unsigned GCD = std::min(MulZeros, TZ);
              APInt DivAmt = APInt::getOneBitSet(BitWidth, TZ - GCD);
              SmallVector<const SCEV*, 4> MulOps;
              MulOps.push_back(getConstant(OpC->getAPInt().lshr(GCD)));
              append_range(MulOps, LHSMul->operands().drop_front());
              auto *NewMul = getMulExpr(MulOps, LHSMul->getNoWrapFlags());
              ShiftedLHS = getUDivExpr(NewMul, getConstant(DivAmt));
            }
          }
          if (!ShiftedLHS)
            ShiftedLHS = getUDivExpr(LHS, MulCount);
          return getMulExpr(
              getZeroExtendExpr(
                  getTruncateExpr(ShiftedLHS,
                      IntegerType::get(getContext(), BitWidth - LZ - TZ)),
                  BO->LHS->getType()),
              MulCount);
        }
      }
      // Binary `and` is a bit-wise `umin`.
      if (BO->LHS->getType()->isIntegerTy(1)) {
        LHS = getSCEV(BO->LHS);
        RHS = getSCEV(BO->RHS);
        return getUMinExpr(LHS, RHS);
      }
      break;

    case Instruction::Or:
      // Binary `or` is a bit-wise `umax`.
      if (BO->LHS->getType()->isIntegerTy(1)) {
        LHS = getSCEV(BO->LHS);
        RHS = getSCEV(BO->RHS);
        return getUMaxExpr(LHS, RHS);
      }
      break;

    case Instruction::Xor:
      if (ConstantInt *CI = dyn_cast<ConstantInt>(BO->RHS)) {
        // If the RHS of xor is -1, then this is a not operation.
        if (CI->isMinusOne())
          return getNotSCEV(getSCEV(BO->LHS));

        // Model xor(and(x, C), C) as and(~x, C), if C is a low-bits mask.
        // This is a variant of the check for xor with -1, and it handles
        // the case where instcombine has trimmed non-demanded bits out
        // of an xor with -1.
        if (auto *LBO = dyn_cast<BinaryOperator>(BO->LHS))
          if (ConstantInt *LCI = dyn_cast<ConstantInt>(LBO->getOperand(1)))
            if (LBO->getOpcode() == Instruction::And &&
                LCI->getValue() == CI->getValue())
              if (const SCEVZeroExtendExpr *Z =
                      dyn_cast<SCEVZeroExtendExpr>(getSCEV(BO->LHS))) {
                Type *UTy = BO->LHS->getType();
                const SCEV *Z0 = Z->getOperand();
                Type *Z0Ty = Z0->getType();
                unsigned Z0TySize = getTypeSizeInBits(Z0Ty);

                // If C is a low-bits mask, the zero extend is serving to
                // mask off the high bits. Complement the operand and
                // re-apply the zext.
                if (CI->getValue().isMask(Z0TySize))
                  return getZeroExtendExpr(getNotSCEV(Z0), UTy);

                // If C is a single bit, it may be in the sign-bit position
                // before the zero-extend. In this case, represent the xor
                // using an add, which is equivalent, and re-apply the zext.
                APInt Trunc = CI->getValue().trunc(Z0TySize);
                if (Trunc.zext(getTypeSizeInBits(UTy)) == CI->getValue() &&
                    Trunc.isSignMask())
                  return getZeroExtendExpr(getAddExpr(Z0, getConstant(Trunc)),
                                           UTy);
              }
      }
      break;

    case Instruction::Shl:
      // Turn shift left of a constant amount into a multiply.
      if (ConstantInt *SA = dyn_cast<ConstantInt>(BO->RHS)) {
        uint32_t BitWidth = cast<IntegerType>(SA->getType())->getBitWidth();

        // If the shift count is not less than the bitwidth, the result of
        // the shift is undefined. Don't try to analyze it, because the
        // resolution chosen here may differ from the resolution chosen in
        // other parts of the compiler.
        if (SA->getValue().uge(BitWidth))
          break;

        // We can safely preserve the nuw flag in all cases. It's also safe to
        // turn a nuw nsw shl into a nuw nsw mul. However, nsw in isolation
        // requires special handling. It can be preserved as long as we're not
        // left shifting by bitwidth - 1.
        auto Flags = SCEV::FlagAnyWrap;
        if (BO->Op) {
          auto MulFlags = getNoWrapFlagsFromUB(BO->Op);
          if ((MulFlags & SCEV::FlagNSW) &&
              ((MulFlags & SCEV::FlagNUW) || SA->getValue().ult(BitWidth - 1)))
            Flags = (SCEV::NoWrapFlags)(Flags | SCEV::FlagNSW);
          if (MulFlags & SCEV::FlagNUW)
            Flags = (SCEV::NoWrapFlags)(Flags | SCEV::FlagNUW);
        }

        ConstantInt *X = ConstantInt::get(
            getContext(), APInt::getOneBitSet(BitWidth, SA->getZExtValue()));
        return getMulExpr(getSCEV(BO->LHS), getConstant(X), Flags);
      }
      break;

    case Instruction::AShr:
      // AShr X, C, where C is a constant.
      ConstantInt *CI = dyn_cast<ConstantInt>(BO->RHS);
      if (!CI)
        break;

      Type *OuterTy = BO->LHS->getType();
      uint64_t BitWidth = getTypeSizeInBits(OuterTy);
      // If the shift count is not less than the bitwidth, the result of
      // the shift is undefined. Don't try to analyze it, because the
      // resolution chosen here may differ from the resolution chosen in
      // other parts of the compiler.
      if (CI->getValue().uge(BitWidth))
        break;

      if (CI->isZero())
        return getSCEV(BO->LHS); // shift by zero --> noop

      uint64_t AShrAmt = CI->getZExtValue();
      Type *TruncTy = IntegerType::get(getContext(), BitWidth - AShrAmt);

      Operator *L = dyn_cast<Operator>(BO->LHS);
      const SCEV *AddTruncateExpr = nullptr;
      ConstantInt *ShlAmtCI = nullptr;
      const SCEV *AddConstant = nullptr;

      if (L && L->getOpcode() == Instruction::Add) {
        // X = Shl A, n
        // Y = Add X, c
        // Z = AShr Y, m
        // n, c and m are constants.

        Operator *LShift = dyn_cast<Operator>(L->getOperand(0));
        ConstantInt *AddOperandCI = dyn_cast<ConstantInt>(L->getOperand(1));
        if (LShift && LShift->getOpcode() == Instruction::Shl) {
          if (AddOperandCI) {
            const SCEV *ShlOp0SCEV = getSCEV(LShift->getOperand(0));
            ShlAmtCI = dyn_cast<ConstantInt>(LShift->getOperand(1));
            // since we truncate to TruncTy, the AddConstant should be of the
            // same type, so create a new Constant with type same as TruncTy.
            // Also, the Add constant should be shifted right by AShr amount.
            APInt AddOperand = AddOperandCI->getValue().ashr(AShrAmt);
            AddConstant = getConstant(AddOperand.trunc(BitWidth - AShrAmt));
            // we model the expression as sext(add(trunc(A), c << n)), since the
            // sext(trunc) part is already handled below, we create a
            // AddExpr(TruncExp) which will be used later.
            AddTruncateExpr = getTruncateExpr(ShlOp0SCEV, TruncTy);
          }
        }
      } else if (L && L->getOpcode() == Instruction::Shl) {
        // X = Shl A, n
        // Y = AShr X, m
        // Both n and m are constant.

        const SCEV *ShlOp0SCEV = getSCEV(L->getOperand(0));
        ShlAmtCI = dyn_cast<ConstantInt>(L->getOperand(1));
        AddTruncateExpr = getTruncateExpr(ShlOp0SCEV, TruncTy);
      }

      if (AddTruncateExpr && ShlAmtCI) {
        // We can merge the two given cases into a single SCEV statement,
        // incase n = m, the mul expression will be 2^0, so it gets resolved to
        // a simpler case. The following code handles the two cases:
        //
        // 1) For a two-shift sext-inreg, i.e. n = m,
        //    use sext(trunc(x)) as the SCEV expression.
        //
        // 2) When n > m, use sext(mul(trunc(x), 2^(n-m)))) as the SCEV
        //    expression. We already checked that ShlAmt < BitWidth, so
        //    the multiplier, 1 << (ShlAmt - AShrAmt), fits into TruncTy as
        //    ShlAmt - AShrAmt < Amt.
        const APInt &ShlAmt = ShlAmtCI->getValue();
        if (ShlAmt.ult(BitWidth) && ShlAmt.uge(AShrAmt)) {
          APInt Mul = APInt::getOneBitSet(BitWidth - AShrAmt,
                                          ShlAmtCI->getZExtValue() - AShrAmt);
          const SCEV *CompositeExpr =
              getMulExpr(AddTruncateExpr, getConstant(Mul));
          if (L->getOpcode() != Instruction::Shl)
            CompositeExpr = getAddExpr(CompositeExpr, AddConstant);

          return getSignExtendExpr(CompositeExpr, OuterTy);
        }
      }
      break;
    }
  }

  switch (U->getOpcode()) {
  case Instruction::Trunc:
    return getTruncateExpr(getSCEV(U->getOperand(0)), U->getType());

  case Instruction::ZExt:
    return getZeroExtendExpr(getSCEV(U->getOperand(0)), U->getType());

  case Instruction::SExt:
    if (auto BO = MatchBinaryOp(U->getOperand(0), getDataLayout(), AC, DT,
                                dyn_cast<Instruction>(V))) {
      // The NSW flag of a subtract does not always survive the conversion to
      // A + (-1)*B.  By pushing sign extension onto its operands we are much
      // more likely to preserve NSW and allow later AddRec optimisations.
      //
      // NOTE: This is effectively duplicating this logic from getSignExtend:
      //   sext((A + B + ...)<nsw>) --> (sext(A) + sext(B) + ...)<nsw>
      // but by that point the NSW information has potentially been lost.
      if (BO->Opcode == Instruction::Sub && BO->IsNSW) {
        Type *Ty = U->getType();
        auto *V1 = getSignExtendExpr(getSCEV(BO->LHS), Ty);
        auto *V2 = getSignExtendExpr(getSCEV(BO->RHS), Ty);
        return getMinusSCEV(V1, V2, SCEV::FlagNSW);
      }
    }
    return getSignExtendExpr(getSCEV(U->getOperand(0)), U->getType());

  case Instruction::BitCast:
    // BitCasts are no-op casts so we just eliminate the cast.
    if (isSCEVable(U->getType()) && isSCEVable(U->getOperand(0)->getType()))
      return getSCEV(U->getOperand(0));
    break;

  case Instruction::PtrToInt: {
    // Pointer to integer cast is straight-forward, so do model it.
    const SCEV *Op = getSCEV(U->getOperand(0));
    Type *DstIntTy = U->getType();
    // But only if effective SCEV (integer) type is wide enough to represent
    // all possible pointer values.
    const SCEV *IntOp = getPtrToIntExpr(Op, DstIntTy);
    if (isa<SCEVCouldNotCompute>(IntOp))
      return getUnknown(V);
    return IntOp;
  }
  case Instruction::IntToPtr:
    // Just don't deal with inttoptr casts.
    return getUnknown(V);

  case Instruction::SDiv:
    // If both operands are non-negative, this is just an udiv.
    if (isKnownNonNegative(getSCEV(U->getOperand(0))) &&
        isKnownNonNegative(getSCEV(U->getOperand(1))))
      return getUDivExpr(getSCEV(U->getOperand(0)), getSCEV(U->getOperand(1)));
    break;

  case Instruction::SRem:
    // If both operands are non-negative, this is just an urem.
    if (isKnownNonNegative(getSCEV(U->getOperand(0))) &&
        isKnownNonNegative(getSCEV(U->getOperand(1))))
      return getURemExpr(getSCEV(U->getOperand(0)), getSCEV(U->getOperand(1)));
    break;

  case Instruction::GetElementPtr:
    return createNodeForGEP(cast<GEPOperator>(U));

  case Instruction::PHI:
    return createNodeForPHI(cast<PHINode>(U));

  case Instruction::Select:
    return createNodeForSelectOrPHI(U, U->getOperand(0), U->getOperand(1),
                                    U->getOperand(2));

  case Instruction::Call:
  case Instruction::Invoke:
    if (Value *RV = cast<CallBase>(U)->getReturnedArgOperand())
      return getSCEV(RV);

    if (auto *II = dyn_cast<IntrinsicInst>(U)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::abs:
        return getAbsExpr(
            getSCEV(II->getArgOperand(0)),
            /*IsNSW=*/cast<ConstantInt>(II->getArgOperand(1))->isOne());
      case Intrinsic::umax:
        LHS = getSCEV(II->getArgOperand(0));
        RHS = getSCEV(II->getArgOperand(1));
        return getUMaxExpr(LHS, RHS);
      case Intrinsic::umin:
        LHS = getSCEV(II->getArgOperand(0));
        RHS = getSCEV(II->getArgOperand(1));
        return getUMinExpr(LHS, RHS);
      case Intrinsic::smax:
        LHS = getSCEV(II->getArgOperand(0));
        RHS = getSCEV(II->getArgOperand(1));
        return getSMaxExpr(LHS, RHS);
      case Intrinsic::smin:
        LHS = getSCEV(II->getArgOperand(0));
        RHS = getSCEV(II->getArgOperand(1));
        return getSMinExpr(LHS, RHS);
      case Intrinsic::usub_sat: {
        const SCEV *X = getSCEV(II->getArgOperand(0));
        const SCEV *Y = getSCEV(II->getArgOperand(1));
        const SCEV *ClampedY = getUMinExpr(X, Y);
        return getMinusSCEV(X, ClampedY, SCEV::FlagNUW);
      }
      case Intrinsic::uadd_sat: {
        const SCEV *X = getSCEV(II->getArgOperand(0));
        const SCEV *Y = getSCEV(II->getArgOperand(1));
        const SCEV *ClampedX = getUMinExpr(X, getNotSCEV(Y));
        return getAddExpr(ClampedX, Y, SCEV::FlagNUW);
      }
      case Intrinsic::start_loop_iterations:
      case Intrinsic::annotation:
      case Intrinsic::ptr_annotation:
        // A start_loop_iterations or llvm.annotation or llvm.prt.annotation is
        // just eqivalent to the first operand for SCEV purposes.
        return getSCEV(II->getArgOperand(0));
      case Intrinsic::vscale:
        return getVScale(II->getType());
      default:
        break;
      }
    }
    break;
  }

  return getUnknown(V);
}

//===----------------------------------------------------------------------===//
//                   Iteration Count Computation Code
//

const SCEV *ScalarEvolution::getTripCountFromExitCount(const SCEV *ExitCount) {
  if (isa<SCEVCouldNotCompute>(ExitCount))
    return getCouldNotCompute();

  auto *ExitCountType = ExitCount->getType();
  assert(ExitCountType->isIntegerTy());
  auto *EvalTy = Type::getIntNTy(ExitCountType->getContext(),
                                 1 + ExitCountType->getScalarSizeInBits());
  return getTripCountFromExitCount(ExitCount, EvalTy, nullptr);
}

const SCEV *ScalarEvolution::getTripCountFromExitCount(const SCEV *ExitCount,
                                                       Type *EvalTy,
                                                       const Loop *L) {
  if (isa<SCEVCouldNotCompute>(ExitCount))
    return getCouldNotCompute();

  unsigned ExitCountSize = getTypeSizeInBits(ExitCount->getType());
  unsigned EvalSize = EvalTy->getPrimitiveSizeInBits();

  auto CanAddOneWithoutOverflow = [&]() {
    ConstantRange ExitCountRange =
      getRangeRef(ExitCount, RangeSignHint::HINT_RANGE_UNSIGNED);
    if (!ExitCountRange.contains(APInt::getMaxValue(ExitCountSize)))
      return true;

    return L && isLoopEntryGuardedByCond(L, ICmpInst::ICMP_NE, ExitCount,
                                         getMinusOne(ExitCount->getType()));
  };

  // If we need to zero extend the backedge count, check if we can add one to
  // it prior to zero extending without overflow. Provided this is safe, it
  // allows better simplification of the +1.
  if (EvalSize > ExitCountSize && CanAddOneWithoutOverflow())
    return getZeroExtendExpr(
        getAddExpr(ExitCount, getOne(ExitCount->getType())), EvalTy);

  // Get the total trip count from the count by adding 1.  This may wrap.
  return getAddExpr(getTruncateOrZeroExtend(ExitCount, EvalTy), getOne(EvalTy));
}

static unsigned getConstantTripCount(const SCEVConstant *ExitCount) {
  if (!ExitCount)
    return 0;

  ConstantInt *ExitConst = ExitCount->getValue();

  // Guard against huge trip counts.
  if (ExitConst->getValue().getActiveBits() > 32)
    return 0;

  // In case of integer overflow, this returns 0, which is correct.
  return ((unsigned)ExitConst->getZExtValue()) + 1;
}

unsigned ScalarEvolution::getSmallConstantTripCount(const Loop *L) {
  auto *ExitCount = dyn_cast<SCEVConstant>(getBackedgeTakenCount(L, Exact));
  return getConstantTripCount(ExitCount);
}

unsigned
ScalarEvolution::getSmallConstantTripCount(const Loop *L,
                                           const BasicBlock *ExitingBlock) {
  assert(ExitingBlock && "Must pass a non-null exiting block!");
  assert(L->isLoopExiting(ExitingBlock) &&
         "Exiting block must actually branch out of the loop!");
  const SCEVConstant *ExitCount =
      dyn_cast<SCEVConstant>(getExitCount(L, ExitingBlock));
  return getConstantTripCount(ExitCount);
}

unsigned ScalarEvolution::getSmallConstantMaxTripCount(const Loop *L) {
  const auto *MaxExitCount =
      dyn_cast<SCEVConstant>(getConstantMaxBackedgeTakenCount(L));
  return getConstantTripCount(MaxExitCount);
}

unsigned ScalarEvolution::getSmallConstantTripMultiple(const Loop *L) {
  SmallVector<BasicBlock *, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  std::optional<unsigned> Res;
  for (auto *ExitingBB : ExitingBlocks) {
    unsigned Multiple = getSmallConstantTripMultiple(L, ExitingBB);
    if (!Res)
      Res = Multiple;
    Res = (unsigned)std::gcd(*Res, Multiple);
  }
  return Res.value_or(1);
}

unsigned ScalarEvolution::getSmallConstantTripMultiple(const Loop *L,
                                                       const SCEV *ExitCount) {
  if (ExitCount == getCouldNotCompute())
    return 1;

  // Get the trip count
  const SCEV *TCExpr = getTripCountFromExitCount(applyLoopGuards(ExitCount, L));

  APInt Multiple = getNonZeroConstantMultiple(TCExpr);
  // If a trip multiple is huge (>=2^32), the trip count is still divisible by
  // the greatest power of 2 divisor less than 2^32.
  return Multiple.getActiveBits() > 32
             ? 1U << std::min((unsigned)31, Multiple.countTrailingZeros())
             : (unsigned)Multiple.zextOrTrunc(32).getZExtValue();
}

/// Returns the largest constant divisor of the trip count of this loop as a
/// normal unsigned value, if possible. This means that the actual trip count is
/// always a multiple of the returned value (don't forget the trip count could
/// very well be zero as well!).
///
/// Returns 1 if the trip count is unknown or not guaranteed to be the
/// multiple of a constant (which is also the case if the trip count is simply
/// constant, use getSmallConstantTripCount for that case), Will also return 1
/// if the trip count is very large (>= 2^32).
///
/// As explained in the comments for getSmallConstantTripCount, this assumes
/// that control exits the loop via ExitingBlock.
unsigned
ScalarEvolution::getSmallConstantTripMultiple(const Loop *L,
                                              const BasicBlock *ExitingBlock) {
  assert(ExitingBlock && "Must pass a non-null exiting block!");
  assert(L->isLoopExiting(ExitingBlock) &&
         "Exiting block must actually branch out of the loop!");
  const SCEV *ExitCount = getExitCount(L, ExitingBlock);
  return getSmallConstantTripMultiple(L, ExitCount);
}

const SCEV *ScalarEvolution::getExitCount(const Loop *L,
                                          const BasicBlock *ExitingBlock,
                                          ExitCountKind Kind) {
  switch (Kind) {
  case Exact:
    return getBackedgeTakenInfo(L).getExact(ExitingBlock, this);
  case SymbolicMaximum:
    return getBackedgeTakenInfo(L).getSymbolicMax(ExitingBlock, this);
  case ConstantMaximum:
    return getBackedgeTakenInfo(L).getConstantMax(ExitingBlock, this);
  };
  llvm_unreachable("Invalid ExitCountKind!");
}

const SCEV *
ScalarEvolution::getPredicatedBackedgeTakenCount(const Loop *L,
                                                 SmallVector<const SCEVPredicate *, 4> &Preds) {
  return getPredicatedBackedgeTakenInfo(L).getExact(L, this, &Preds);
}

const SCEV *ScalarEvolution::getBackedgeTakenCount(const Loop *L,
                                                   ExitCountKind Kind) {
  switch (Kind) {
  case Exact:
    return getBackedgeTakenInfo(L).getExact(L, this);
  case ConstantMaximum:
    return getBackedgeTakenInfo(L).getConstantMax(this);
  case SymbolicMaximum:
    return getBackedgeTakenInfo(L).getSymbolicMax(L, this);
  };
  llvm_unreachable("Invalid ExitCountKind!");
}

const SCEV *ScalarEvolution::getPredicatedSymbolicMaxBackedgeTakenCount(
    const Loop *L, SmallVector<const SCEVPredicate *, 4> &Preds) {
  return getPredicatedBackedgeTakenInfo(L).getSymbolicMax(L, this, &Preds);
}

bool ScalarEvolution::isBackedgeTakenCountMaxOrZero(const Loop *L) {
  return getBackedgeTakenInfo(L).isConstantMaxOrZero(this);
}

/// Push PHI nodes in the header of the given loop onto the given Worklist.
static void PushLoopPHIs(const Loop *L,
                         SmallVectorImpl<Instruction *> &Worklist,
                         SmallPtrSetImpl<Instruction *> &Visited) {
  BasicBlock *Header = L->getHeader();

  // Push all Loop-header PHIs onto the Worklist stack.
  for (PHINode &PN : Header->phis())
    if (Visited.insert(&PN).second)
      Worklist.push_back(&PN);
}

ScalarEvolution::BackedgeTakenInfo &
ScalarEvolution::getPredicatedBackedgeTakenInfo(const Loop *L) {
  auto &BTI = getBackedgeTakenInfo(L);
  if (BTI.hasFullInfo())
    return BTI;

  auto Pair = PredicatedBackedgeTakenCounts.insert({L, BackedgeTakenInfo()});

  if (!Pair.second)
    return Pair.first->second;

  BackedgeTakenInfo Result =
      computeBackedgeTakenCount(L, /*AllowPredicates=*/true);

  return PredicatedBackedgeTakenCounts.find(L)->second = std::move(Result);
}

ScalarEvolution::BackedgeTakenInfo &
ScalarEvolution::getBackedgeTakenInfo(const Loop *L) {
  // Initially insert an invalid entry for this loop. If the insertion
  // succeeds, proceed to actually compute a backedge-taken count and
  // update the value. The temporary CouldNotCompute value tells SCEV
  // code elsewhere that it shouldn't attempt to request a new
  // backedge-taken count, which could result in infinite recursion.
  std::pair<DenseMap<const Loop *, BackedgeTakenInfo>::iterator, bool> Pair =
      BackedgeTakenCounts.insert({L, BackedgeTakenInfo()});
  if (!Pair.second)
    return Pair.first->second;

  // computeBackedgeTakenCount may allocate memory for its result. Inserting it
  // into the BackedgeTakenCounts map transfers ownership. Otherwise, the result
  // must be cleared in this scope.
  BackedgeTakenInfo Result = computeBackedgeTakenCount(L);

  // Now that we know more about the trip count for this loop, forget any
  // existing SCEV values for PHI nodes in this loop since they are only
  // conservative estimates made without the benefit of trip count
  // information. This invalidation is not necessary for correctness, and is
  // only done to produce more precise results.
  if (Result.hasAnyInfo()) {
    // Invalidate any expression using an addrec in this loop.
    SmallVector<const SCEV *, 8> ToForget;
    auto LoopUsersIt = LoopUsers.find(L);
    if (LoopUsersIt != LoopUsers.end())
      append_range(ToForget, LoopUsersIt->second);
    forgetMemoizedResults(ToForget);

    // Invalidate constant-evolved loop header phis.
    for (PHINode &PN : L->getHeader()->phis())
      ConstantEvolutionLoopExitValue.erase(&PN);
  }

  // Re-lookup the insert position, since the call to
  // computeBackedgeTakenCount above could result in a
  // recusive call to getBackedgeTakenInfo (on a different
  // loop), which would invalidate the iterator computed
  // earlier.
  return BackedgeTakenCounts.find(L)->second = std::move(Result);
}

void ScalarEvolution::forgetAllLoops() {
  // This method is intended to forget all info about loops. It should
  // invalidate caches as if the following happened:
  // - The trip counts of all loops have changed arbitrarily
  // - Every llvm::Value has been updated in place to produce a different
  // result.
  BackedgeTakenCounts.clear();
  PredicatedBackedgeTakenCounts.clear();
  BECountUsers.clear();
  LoopPropertiesCache.clear();
  ConstantEvolutionLoopExitValue.clear();
  ValueExprMap.clear();
  ValuesAtScopes.clear();
  ValuesAtScopesUsers.clear();
  LoopDispositions.clear();
  BlockDispositions.clear();
  UnsignedRanges.clear();
  SignedRanges.clear();
  ExprValueMap.clear();
  HasRecMap.clear();
  ConstantMultipleCache.clear();
  PredicatedSCEVRewrites.clear();
  FoldCache.clear();
  FoldCacheUser.clear();
}
void ScalarEvolution::visitAndClearUsers(
    SmallVectorImpl<Instruction *> &Worklist,
    SmallPtrSetImpl<Instruction *> &Visited,
    SmallVectorImpl<const SCEV *> &ToForget) {
  while (!Worklist.empty()) {
    Instruction *I = Worklist.pop_back_val();
    if (!isSCEVable(I->getType()) && !isa<WithOverflowInst>(I))
      continue;

    ValueExprMapType::iterator It =
        ValueExprMap.find_as(static_cast<Value *>(I));
    if (It != ValueExprMap.end()) {
      eraseValueFromMap(It->first);
      ToForget.push_back(It->second);
      if (PHINode *PN = dyn_cast<PHINode>(I))
        ConstantEvolutionLoopExitValue.erase(PN);
    }

    PushDefUseChildren(I, Worklist, Visited);
  }
}

void ScalarEvolution::forgetLoop(const Loop *L) {
  SmallVector<const Loop *, 16> LoopWorklist(1, L);
  SmallVector<Instruction *, 32> Worklist;
  SmallPtrSet<Instruction *, 16> Visited;
  SmallVector<const SCEV *, 16> ToForget;

  // Iterate over all the loops and sub-loops to drop SCEV information.
  while (!LoopWorklist.empty()) {
    auto *CurrL = LoopWorklist.pop_back_val();

    // Drop any stored trip count value.
    forgetBackedgeTakenCounts(CurrL, /* Predicated */ false);
    forgetBackedgeTakenCounts(CurrL, /* Predicated */ true);

    // Drop information about predicated SCEV rewrites for this loop.
    for (auto I = PredicatedSCEVRewrites.begin();
         I != PredicatedSCEVRewrites.end();) {
      std::pair<const SCEV *, const Loop *> Entry = I->first;
      if (Entry.second == CurrL)
        PredicatedSCEVRewrites.erase(I++);
      else
        ++I;
    }

    auto LoopUsersItr = LoopUsers.find(CurrL);
    if (LoopUsersItr != LoopUsers.end()) {
      ToForget.insert(ToForget.end(), LoopUsersItr->second.begin(),
                LoopUsersItr->second.end());
    }

    // Drop information about expressions based on loop-header PHIs.
    PushLoopPHIs(CurrL, Worklist, Visited);
    visitAndClearUsers(Worklist, Visited, ToForget);

    LoopPropertiesCache.erase(CurrL);
    // Forget all contained loops too, to avoid dangling entries in the
    // ValuesAtScopes map.
    LoopWorklist.append(CurrL->begin(), CurrL->end());
  }
  forgetMemoizedResults(ToForget);
}

void ScalarEvolution::forgetTopmostLoop(const Loop *L) {
  forgetLoop(L->getOutermostLoop());
}

void ScalarEvolution::forgetValue(Value *V) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return;

  // Drop information about expressions based on loop-header PHIs.
  SmallVector<Instruction *, 16> Worklist;
  SmallPtrSet<Instruction *, 8> Visited;
  SmallVector<const SCEV *, 8> ToForget;
  Worklist.push_back(I);
  Visited.insert(I);
  visitAndClearUsers(Worklist, Visited, ToForget);

  forgetMemoizedResults(ToForget);
}

void ScalarEvolution::forgetLcssaPhiWithNewPredecessor(Loop *L, PHINode *V) {
  if (!isSCEVable(V->getType()))
    return;

  // If SCEV looked through a trivial LCSSA phi node, we might have SCEV's
  // directly using a SCEVUnknown/SCEVAddRec defined in the loop. After an
  // extra predecessor is added, this is no longer valid. Find all Unknowns and
  // AddRecs defined in the loop and invalidate any SCEV's making use of them.
  if (const SCEV *S = getExistingSCEV(V)) {
    struct InvalidationRootCollector {
      Loop *L;
      SmallVector<const SCEV *, 8> Roots;

      InvalidationRootCollector(Loop *L) : L(L) {}

      bool follow(const SCEV *S) {
        if (auto *SU = dyn_cast<SCEVUnknown>(S)) {
          if (auto *I = dyn_cast<Instruction>(SU->getValue()))
            if (L->contains(I))
              Roots.push_back(S);
        } else if (auto *AddRec = dyn_cast<SCEVAddRecExpr>(S)) {
          if (L->contains(AddRec->getLoop()))
            Roots.push_back(S);
        }
        return true;
      }
      bool isDone() const { return false; }
    };

    InvalidationRootCollector C(L);
    visitAll(S, C);
    forgetMemoizedResults(C.Roots);
  }

  // Also perform the normal invalidation.
  forgetValue(V);
}

void ScalarEvolution::forgetLoopDispositions() { LoopDispositions.clear(); }

void ScalarEvolution::forgetBlockAndLoopDispositions(Value *V) {
  // Unless a specific value is passed to invalidation, completely clear both
  // caches.
  if (!V) {
    BlockDispositions.clear();
    LoopDispositions.clear();
    return;
  }

  if (!isSCEVable(V->getType()))
    return;

  const SCEV *S = getExistingSCEV(V);
  if (!S)
    return;

  // Invalidate the block and loop dispositions cached for S. Dispositions of
  // S's users may change if S's disposition changes (i.e. a user may change to
  // loop-invariant, if S changes to loop invariant), so also invalidate
  // dispositions of S's users recursively.
  SmallVector<const SCEV *, 8> Worklist = {S};
  SmallPtrSet<const SCEV *, 8> Seen = {S};
  while (!Worklist.empty()) {
    const SCEV *Curr = Worklist.pop_back_val();
    bool LoopDispoRemoved = LoopDispositions.erase(Curr);
    bool BlockDispoRemoved = BlockDispositions.erase(Curr);
    if (!LoopDispoRemoved && !BlockDispoRemoved)
      continue;
    auto Users = SCEVUsers.find(Curr);
    if (Users != SCEVUsers.end())
      for (const auto *User : Users->second)
        if (Seen.insert(User).second)
          Worklist.push_back(User);
  }
}

/// Get the exact loop backedge taken count considering all loop exits. A
/// computable result can only be returned for loops with all exiting blocks
/// dominating the latch. howFarToZero assumes that the limit of each loop test
/// is never skipped. This is a valid assumption as long as the loop exits via
/// that test. For precise results, it is the caller's responsibility to specify
/// the relevant loop exiting block using getExact(ExitingBlock, SE).
const SCEV *
ScalarEvolution::BackedgeTakenInfo::getExact(const Loop *L, ScalarEvolution *SE,
                                             SmallVector<const SCEVPredicate *, 4> *Preds) const {
  // If any exits were not computable, the loop is not computable.
  if (!isComplete() || ExitNotTaken.empty())
    return SE->getCouldNotCompute();

  const BasicBlock *Latch = L->getLoopLatch();
  // All exiting blocks we have collected must dominate the only backedge.
  if (!Latch)
    return SE->getCouldNotCompute();

  // All exiting blocks we have gathered dominate loop's latch, so exact trip
  // count is simply a minimum out of all these calculated exit counts.
  SmallVector<const SCEV *, 2> Ops;
  for (const auto &ENT : ExitNotTaken) {
    const SCEV *BECount = ENT.ExactNotTaken;
    assert(BECount != SE->getCouldNotCompute() && "Bad exit SCEV!");
    assert(SE->DT.dominates(ENT.ExitingBlock, Latch) &&
           "We should only have known counts for exiting blocks that dominate "
           "latch!");

    Ops.push_back(BECount);

    if (Preds)
      for (const auto *P : ENT.Predicates)
        Preds->push_back(P);

    assert((Preds || ENT.hasAlwaysTruePredicate()) &&
           "Predicate should be always true!");
  }

  // If an earlier exit exits on the first iteration (exit count zero), then
  // a later poison exit count should not propagate into the result. This are
  // exactly the semantics provided by umin_seq.
  return SE->getUMinFromMismatchedTypes(Ops, /* Sequential */ true);
}

/// Get the exact not taken count for this loop exit.
const SCEV *
ScalarEvolution::BackedgeTakenInfo::getExact(const BasicBlock *ExitingBlock,
                                             ScalarEvolution *SE) const {
  for (const auto &ENT : ExitNotTaken)
    if (ENT.ExitingBlock == ExitingBlock && ENT.hasAlwaysTruePredicate())
      return ENT.ExactNotTaken;

  return SE->getCouldNotCompute();
}

const SCEV *ScalarEvolution::BackedgeTakenInfo::getConstantMax(
    const BasicBlock *ExitingBlock, ScalarEvolution *SE) const {
  for (const auto &ENT : ExitNotTaken)
    if (ENT.ExitingBlock == ExitingBlock && ENT.hasAlwaysTruePredicate())
      return ENT.ConstantMaxNotTaken;

  return SE->getCouldNotCompute();
}

const SCEV *ScalarEvolution::BackedgeTakenInfo::getSymbolicMax(
    const BasicBlock *ExitingBlock, ScalarEvolution *SE) const {
  for (const auto &ENT : ExitNotTaken)
    if (ENT.ExitingBlock == ExitingBlock && ENT.hasAlwaysTruePredicate())
      return ENT.SymbolicMaxNotTaken;

  return SE->getCouldNotCompute();
}

/// getConstantMax - Get the constant max backedge taken count for the loop.
const SCEV *
ScalarEvolution::BackedgeTakenInfo::getConstantMax(ScalarEvolution *SE) const {
  auto PredicateNotAlwaysTrue = [](const ExitNotTakenInfo &ENT) {
    return !ENT.hasAlwaysTruePredicate();
  };

  if (!getConstantMax() || any_of(ExitNotTaken, PredicateNotAlwaysTrue))
    return SE->getCouldNotCompute();

  assert((isa<SCEVCouldNotCompute>(getConstantMax()) ||
          isa<SCEVConstant>(getConstantMax())) &&
         "No point in having a non-constant max backedge taken count!");
  return getConstantMax();
}

const SCEV *ScalarEvolution::BackedgeTakenInfo::getSymbolicMax(
    const Loop *L, ScalarEvolution *SE,
    SmallVector<const SCEVPredicate *, 4> *Predicates) {
  if (!SymbolicMax) {
    // Form an expression for the maximum exit count possible for this loop. We
    // merge the max and exact information to approximate a version of
    // getConstantMaxBackedgeTakenCount which isn't restricted to just
    // constants.
    SmallVector<const SCEV *, 4> ExitCounts;

    for (const auto &ENT : ExitNotTaken) {
      const SCEV *ExitCount = ENT.SymbolicMaxNotTaken;
      if (!isa<SCEVCouldNotCompute>(ExitCount)) {
        assert(SE->DT.dominates(ENT.ExitingBlock, L->getLoopLatch()) &&
               "We should only have known counts for exiting blocks that "
               "dominate latch!");
        ExitCounts.push_back(ExitCount);
        if (Predicates)
          for (const auto *P : ENT.Predicates)
            Predicates->push_back(P);

        assert((Predicates || ENT.hasAlwaysTruePredicate()) &&
               "Predicate should be always true!");
      }
    }
    if (ExitCounts.empty())
      SymbolicMax = SE->getCouldNotCompute();
    else
      SymbolicMax =
          SE->getUMinFromMismatchedTypes(ExitCounts, /*Sequential*/ true);
  }
  return SymbolicMax;
}

bool ScalarEvolution::BackedgeTakenInfo::isConstantMaxOrZero(
    ScalarEvolution *SE) const {
  auto PredicateNotAlwaysTrue = [](const ExitNotTakenInfo &ENT) {
    return !ENT.hasAlwaysTruePredicate();
  };
  return MaxOrZero && !any_of(ExitNotTaken, PredicateNotAlwaysTrue);
}

ScalarEvolution::ExitLimit::ExitLimit(const SCEV *E)
    : ExitLimit(E, E, E, false, std::nullopt) {}

ScalarEvolution::ExitLimit::ExitLimit(
    const SCEV *E, const SCEV *ConstantMaxNotTaken,
    const SCEV *SymbolicMaxNotTaken, bool MaxOrZero,
    ArrayRef<const SmallPtrSetImpl<const SCEVPredicate *> *> PredSetList)
    : ExactNotTaken(E), ConstantMaxNotTaken(ConstantMaxNotTaken),
      SymbolicMaxNotTaken(SymbolicMaxNotTaken), MaxOrZero(MaxOrZero) {
  // If we prove the max count is zero, so is the symbolic bound.  This happens
  // in practice due to differences in a) how context sensitive we've chosen
  // to be and b) how we reason about bounds implied by UB.
  if (ConstantMaxNotTaken->isZero()) {
    this->ExactNotTaken = E = ConstantMaxNotTaken;
    this->SymbolicMaxNotTaken = SymbolicMaxNotTaken = ConstantMaxNotTaken;
  }

  assert((isa<SCEVCouldNotCompute>(ExactNotTaken) ||
          !isa<SCEVCouldNotCompute>(ConstantMaxNotTaken)) &&
         "Exact is not allowed to be less precise than Constant Max");
  assert((isa<SCEVCouldNotCompute>(ExactNotTaken) ||
          !isa<SCEVCouldNotCompute>(SymbolicMaxNotTaken)) &&
         "Exact is not allowed to be less precise than Symbolic Max");
  assert((isa<SCEVCouldNotCompute>(SymbolicMaxNotTaken) ||
          !isa<SCEVCouldNotCompute>(ConstantMaxNotTaken)) &&
         "Symbolic Max is not allowed to be less precise than Constant Max");
  assert((isa<SCEVCouldNotCompute>(ConstantMaxNotTaken) ||
          isa<SCEVConstant>(ConstantMaxNotTaken)) &&
         "No point in having a non-constant max backedge taken count!");
  for (const auto *PredSet : PredSetList)
    for (const auto *P : *PredSet)
      addPredicate(P);
  assert((isa<SCEVCouldNotCompute>(E) || !E->getType()->isPointerTy()) &&
         "Backedge count should be int");
  assert((isa<SCEVCouldNotCompute>(ConstantMaxNotTaken) ||
          !ConstantMaxNotTaken->getType()->isPointerTy()) &&
         "Max backedge count should be int");
}

ScalarEvolution::ExitLimit::ExitLimit(
    const SCEV *E, const SCEV *ConstantMaxNotTaken,
    const SCEV *SymbolicMaxNotTaken, bool MaxOrZero,
    const SmallPtrSetImpl<const SCEVPredicate *> &PredSet)
    : ExitLimit(E, ConstantMaxNotTaken, SymbolicMaxNotTaken, MaxOrZero,
                { &PredSet }) {}

/// Allocate memory for BackedgeTakenInfo and copy the not-taken count of each
/// computable exit into a persistent ExitNotTakenInfo array.
ScalarEvolution::BackedgeTakenInfo::BackedgeTakenInfo(
    ArrayRef<ScalarEvolution::BackedgeTakenInfo::EdgeExitInfo> ExitCounts,
    bool IsComplete, const SCEV *ConstantMax, bool MaxOrZero)
    : ConstantMax(ConstantMax), IsComplete(IsComplete), MaxOrZero(MaxOrZero) {
  using EdgeExitInfo = ScalarEvolution::BackedgeTakenInfo::EdgeExitInfo;

  ExitNotTaken.reserve(ExitCounts.size());
  std::transform(ExitCounts.begin(), ExitCounts.end(),
                 std::back_inserter(ExitNotTaken),
                 [&](const EdgeExitInfo &EEI) {
        BasicBlock *ExitBB = EEI.first;
        const ExitLimit &EL = EEI.second;
        return ExitNotTakenInfo(ExitBB, EL.ExactNotTaken,
                                EL.ConstantMaxNotTaken, EL.SymbolicMaxNotTaken,
                                EL.Predicates);
  });
  assert((isa<SCEVCouldNotCompute>(ConstantMax) ||
          isa<SCEVConstant>(ConstantMax)) &&
         "No point in having a non-constant max backedge taken count!");
}

/// Compute the number of times the backedge of the specified loop will execute.
ScalarEvolution::BackedgeTakenInfo
ScalarEvolution::computeBackedgeTakenCount(const Loop *L,
                                           bool AllowPredicates) {
  SmallVector<BasicBlock *, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);

  using EdgeExitInfo = ScalarEvolution::BackedgeTakenInfo::EdgeExitInfo;

  SmallVector<EdgeExitInfo, 4> ExitCounts;
  bool CouldComputeBECount = true;
  BasicBlock *Latch = L->getLoopLatch(); // may be NULL.
  const SCEV *MustExitMaxBECount = nullptr;
  const SCEV *MayExitMaxBECount = nullptr;
  bool MustExitMaxOrZero = false;
  bool IsOnlyExit = ExitingBlocks.size() == 1;

  // Compute the ExitLimit for each loop exit. Use this to populate ExitCounts
  // and compute maxBECount.
  // Do a union of all the predicates here.
  for (BasicBlock *ExitBB : ExitingBlocks) {
    // We canonicalize untaken exits to br (constant), ignore them so that
    // proving an exit untaken doesn't negatively impact our ability to reason
    // about the loop as whole.
    if (auto *BI = dyn_cast<BranchInst>(ExitBB->getTerminator()))
      if (auto *CI = dyn_cast<ConstantInt>(BI->getCondition())) {
        bool ExitIfTrue = !L->contains(BI->getSuccessor(0));
        if (ExitIfTrue == CI->isZero())
          continue;
      }

    ExitLimit EL = computeExitLimit(L, ExitBB, IsOnlyExit, AllowPredicates);

    assert((AllowPredicates || EL.Predicates.empty()) &&
           "Predicated exit limit when predicates are not allowed!");

    // 1. For each exit that can be computed, add an entry to ExitCounts.
    // CouldComputeBECount is true only if all exits can be computed.
    if (EL.ExactNotTaken != getCouldNotCompute())
      ++NumExitCountsComputed;
    else
      // We couldn't compute an exact value for this exit, so
      // we won't be able to compute an exact value for the loop.
      CouldComputeBECount = false;
    // Remember exit count if either exact or symbolic is known. Because
    // Exact always implies symbolic, only check symbolic.
    if (EL.SymbolicMaxNotTaken != getCouldNotCompute())
      ExitCounts.emplace_back(ExitBB, EL);
    else {
      assert(EL.ExactNotTaken == getCouldNotCompute() &&
             "Exact is known but symbolic isn't?");
      ++NumExitCountsNotComputed;
    }

    // 2. Derive the loop's MaxBECount from each exit's max number of
    // non-exiting iterations. Partition the loop exits into two kinds:
    // LoopMustExits and LoopMayExits.
    //
    // If the exit dominates the loop latch, it is a LoopMustExit otherwise it
    // is a LoopMayExit.  If any computable LoopMustExit is found, then
    // MaxBECount is the minimum EL.ConstantMaxNotTaken of computable
    // LoopMustExits. Otherwise, MaxBECount is conservatively the maximum
    // EL.ConstantMaxNotTaken, where CouldNotCompute is considered greater than
    // any
    // computable EL.ConstantMaxNotTaken.
    if (EL.ConstantMaxNotTaken != getCouldNotCompute() && Latch &&
        DT.dominates(ExitBB, Latch)) {
      if (!MustExitMaxBECount) {
        MustExitMaxBECount = EL.ConstantMaxNotTaken;
        MustExitMaxOrZero = EL.MaxOrZero;
      } else {
        MustExitMaxBECount = getUMinFromMismatchedTypes(MustExitMaxBECount,
                                                        EL.ConstantMaxNotTaken);
      }
    } else if (MayExitMaxBECount != getCouldNotCompute()) {
      if (!MayExitMaxBECount || EL.ConstantMaxNotTaken == getCouldNotCompute())
        MayExitMaxBECount = EL.ConstantMaxNotTaken;
      else {
        MayExitMaxBECount = getUMaxFromMismatchedTypes(MayExitMaxBECount,
                                                       EL.ConstantMaxNotTaken);
      }
    }
  }
  const SCEV *MaxBECount = MustExitMaxBECount ? MustExitMaxBECount :
    (MayExitMaxBECount ? MayExitMaxBECount : getCouldNotCompute());
  // The loop backedge will be taken the maximum or zero times if there's
  // a single exit that must be taken the maximum or zero times.
  bool MaxOrZero = (MustExitMaxOrZero && ExitingBlocks.size() == 1);

  // Remember which SCEVs are used in exit limits for invalidation purposes.
  // We only care about non-constant SCEVs here, so we can ignore
  // EL.ConstantMaxNotTaken
  // and MaxBECount, which must be SCEVConstant.
  for (const auto &Pair : ExitCounts) {
    if (!isa<SCEVConstant>(Pair.second.ExactNotTaken))
      BECountUsers[Pair.second.ExactNotTaken].insert({L, AllowPredicates});
    if (!isa<SCEVConstant>(Pair.second.SymbolicMaxNotTaken))
      BECountUsers[Pair.second.SymbolicMaxNotTaken].insert(
          {L, AllowPredicates});
  }
  return BackedgeTakenInfo(std::move(ExitCounts), CouldComputeBECount,
                           MaxBECount, MaxOrZero);
}

ScalarEvolution::ExitLimit
ScalarEvolution::computeExitLimit(const Loop *L, BasicBlock *ExitingBlock,
                                  bool IsOnlyExit, bool AllowPredicates) {
  assert(L->contains(ExitingBlock) && "Exit count for non-loop block?");
  // If our exiting block does not dominate the latch, then its connection with
  // loop's exit limit may be far from trivial.
  const BasicBlock *Latch = L->getLoopLatch();
  if (!Latch || !DT.dominates(ExitingBlock, Latch))
    return getCouldNotCompute();

  Instruction *Term = ExitingBlock->getTerminator();
  if (BranchInst *BI = dyn_cast<BranchInst>(Term)) {
    assert(BI->isConditional() && "If unconditional, it can't be in loop!");
    bool ExitIfTrue = !L->contains(BI->getSuccessor(0));
    assert(ExitIfTrue == L->contains(BI->getSuccessor(1)) &&
           "It should have one successor in loop and one exit block!");
    // Proceed to the next level to examine the exit condition expression.
    return computeExitLimitFromCond(L, BI->getCondition(), ExitIfTrue,
                                    /*ControlsOnlyExit=*/IsOnlyExit,
                                    AllowPredicates);
  }

  if (SwitchInst *SI = dyn_cast<SwitchInst>(Term)) {
    // For switch, make sure that there is a single exit from the loop.
    BasicBlock *Exit = nullptr;
    for (auto *SBB : successors(ExitingBlock))
      if (!L->contains(SBB)) {
        if (Exit) // Multiple exit successors.
          return getCouldNotCompute();
        Exit = SBB;
      }
    assert(Exit && "Exiting block must have at least one exit");
    return computeExitLimitFromSingleExitSwitch(
        L, SI, Exit, /*ControlsOnlyExit=*/IsOnlyExit);
  }

  return getCouldNotCompute();
}

ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromCond(
    const Loop *L, Value *ExitCond, bool ExitIfTrue, bool ControlsOnlyExit,
    bool AllowPredicates) {
  ScalarEvolution::ExitLimitCacheTy Cache(L, ExitIfTrue, AllowPredicates);
  return computeExitLimitFromCondCached(Cache, L, ExitCond, ExitIfTrue,
                                        ControlsOnlyExit, AllowPredicates);
}

std::optional<ScalarEvolution::ExitLimit>
ScalarEvolution::ExitLimitCache::find(const Loop *L, Value *ExitCond,
                                      bool ExitIfTrue, bool ControlsOnlyExit,
                                      bool AllowPredicates) {
  (void)this->L;
  (void)this->ExitIfTrue;
  (void)this->AllowPredicates;

  assert(this->L == L && this->ExitIfTrue == ExitIfTrue &&
         this->AllowPredicates == AllowPredicates &&
         "Variance in assumed invariant key components!");
  auto Itr = TripCountMap.find({ExitCond, ControlsOnlyExit});
  if (Itr == TripCountMap.end())
    return std::nullopt;
  return Itr->second;
}

void ScalarEvolution::ExitLimitCache::insert(const Loop *L, Value *ExitCond,
                                             bool ExitIfTrue,
                                             bool ControlsOnlyExit,
                                             bool AllowPredicates,
                                             const ExitLimit &EL) {
  assert(this->L == L && this->ExitIfTrue == ExitIfTrue &&
         this->AllowPredicates == AllowPredicates &&
         "Variance in assumed invariant key components!");

  auto InsertResult = TripCountMap.insert({{ExitCond, ControlsOnlyExit}, EL});
  assert(InsertResult.second && "Expected successful insertion!");
  (void)InsertResult;
  (void)ExitIfTrue;
}

ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromCondCached(
    ExitLimitCacheTy &Cache, const Loop *L, Value *ExitCond, bool ExitIfTrue,
    bool ControlsOnlyExit, bool AllowPredicates) {

  if (auto MaybeEL = Cache.find(L, ExitCond, ExitIfTrue, ControlsOnlyExit,
                                AllowPredicates))
    return *MaybeEL;

  ExitLimit EL = computeExitLimitFromCondImpl(
      Cache, L, ExitCond, ExitIfTrue, ControlsOnlyExit, AllowPredicates);
  Cache.insert(L, ExitCond, ExitIfTrue, ControlsOnlyExit, AllowPredicates, EL);
  return EL;
}

ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromCondImpl(
    ExitLimitCacheTy &Cache, const Loop *L, Value *ExitCond, bool ExitIfTrue,
    bool ControlsOnlyExit, bool AllowPredicates) {
  // Handle BinOp conditions (And, Or).
  if (auto LimitFromBinOp = computeExitLimitFromCondFromBinOp(
          Cache, L, ExitCond, ExitIfTrue, ControlsOnlyExit, AllowPredicates))
    return *LimitFromBinOp;

  // With an icmp, it may be feasible to compute an exact backedge-taken count.
  // Proceed to the next level to examine the icmp.
  if (ICmpInst *ExitCondICmp = dyn_cast<ICmpInst>(ExitCond)) {
    ExitLimit EL =
        computeExitLimitFromICmp(L, ExitCondICmp, ExitIfTrue, ControlsOnlyExit);
    if (EL.hasFullInfo() || !AllowPredicates)
      return EL;

    // Try again, but use SCEV predicates this time.
    return computeExitLimitFromICmp(L, ExitCondICmp, ExitIfTrue,
                                    ControlsOnlyExit,
                                    /*AllowPredicates=*/true);
  }

  // Check for a constant condition. These are normally stripped out by
  // SimplifyCFG, but ScalarEvolution may be used by a pass which wishes to
  // preserve the CFG and is temporarily leaving constant conditions
  // in place.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(ExitCond)) {
    if (ExitIfTrue == !CI->getZExtValue())
      // The backedge is always taken.
      return getCouldNotCompute();
    // The backedge is never taken.
    return getZero(CI->getType());
  }

  // If we're exiting based on the overflow flag of an x.with.overflow intrinsic
  // with a constant step, we can form an equivalent icmp predicate and figure
  // out how many iterations will be taken before we exit.
  const WithOverflowInst *WO;
  const APInt *C;
  if (match(ExitCond, m_ExtractValue<1>(m_WithOverflowInst(WO))) &&
      match(WO->getRHS(), m_APInt(C))) {
    ConstantRange NWR =
      ConstantRange::makeExactNoWrapRegion(WO->getBinaryOp(), *C,
                                           WO->getNoWrapKind());
    CmpInst::Predicate Pred;
    APInt NewRHSC, Offset;
    NWR.getEquivalentICmp(Pred, NewRHSC, Offset);
    if (!ExitIfTrue)
      Pred = ICmpInst::getInversePredicate(Pred);
    auto *LHS = getSCEV(WO->getLHS());
    if (Offset != 0)
      LHS = getAddExpr(LHS, getConstant(Offset));
    auto EL = computeExitLimitFromICmp(L, Pred, LHS, getConstant(NewRHSC),
                                       ControlsOnlyExit, AllowPredicates);
    if (EL.hasAnyInfo())
      return EL;
  }

  // If it's not an integer or pointer comparison then compute it the hard way.
  return computeExitCountExhaustively(L, ExitCond, ExitIfTrue);
}

std::optional<ScalarEvolution::ExitLimit>
ScalarEvolution::computeExitLimitFromCondFromBinOp(
    ExitLimitCacheTy &Cache, const Loop *L, Value *ExitCond, bool ExitIfTrue,
    bool ControlsOnlyExit, bool AllowPredicates) {
  // Check if the controlling expression for this loop is an And or Or.
  Value *Op0, *Op1;
  bool IsAnd = false;
  if (match(ExitCond, m_LogicalAnd(m_Value(Op0), m_Value(Op1))))
    IsAnd = true;
  else if (match(ExitCond, m_LogicalOr(m_Value(Op0), m_Value(Op1))))
    IsAnd = false;
  else
    return std::nullopt;

  // EitherMayExit is true in these two cases:
  //   br (and Op0 Op1), loop, exit
  //   br (or  Op0 Op1), exit, loop
  bool EitherMayExit = IsAnd ^ ExitIfTrue;
  ExitLimit EL0 = computeExitLimitFromCondCached(
      Cache, L, Op0, ExitIfTrue, ControlsOnlyExit && !EitherMayExit,
      AllowPredicates);
  ExitLimit EL1 = computeExitLimitFromCondCached(
      Cache, L, Op1, ExitIfTrue, ControlsOnlyExit && !EitherMayExit,
      AllowPredicates);

  // Be robust against unsimplified IR for the form "op i1 X, NeutralElement"
  const Constant *NeutralElement = ConstantInt::get(ExitCond->getType(), IsAnd);
  if (isa<ConstantInt>(Op1))
    return Op1 == NeutralElement ? EL0 : EL1;
  if (isa<ConstantInt>(Op0))
    return Op0 == NeutralElement ? EL1 : EL0;

  const SCEV *BECount = getCouldNotCompute();
  const SCEV *ConstantMaxBECount = getCouldNotCompute();
  const SCEV *SymbolicMaxBECount = getCouldNotCompute();
  if (EitherMayExit) {
    bool UseSequentialUMin = !isa<BinaryOperator>(ExitCond);
    // Both conditions must be same for the loop to continue executing.
    // Choose the less conservative count.
    if (EL0.ExactNotTaken != getCouldNotCompute() &&
        EL1.ExactNotTaken != getCouldNotCompute()) {
      BECount = getUMinFromMismatchedTypes(EL0.ExactNotTaken, EL1.ExactNotTaken,
                                           UseSequentialUMin);
    }
    if (EL0.ConstantMaxNotTaken == getCouldNotCompute())
      ConstantMaxBECount = EL1.ConstantMaxNotTaken;
    else if (EL1.ConstantMaxNotTaken == getCouldNotCompute())
      ConstantMaxBECount = EL0.ConstantMaxNotTaken;
    else
      ConstantMaxBECount = getUMinFromMismatchedTypes(EL0.ConstantMaxNotTaken,
                                                      EL1.ConstantMaxNotTaken);
    if (EL0.SymbolicMaxNotTaken == getCouldNotCompute())
      SymbolicMaxBECount = EL1.SymbolicMaxNotTaken;
    else if (EL1.SymbolicMaxNotTaken == getCouldNotCompute())
      SymbolicMaxBECount = EL0.SymbolicMaxNotTaken;
    else
      SymbolicMaxBECount = getUMinFromMismatchedTypes(
          EL0.SymbolicMaxNotTaken, EL1.SymbolicMaxNotTaken, UseSequentialUMin);
  } else {
    // Both conditions must be same at the same time for the loop to exit.
    // For now, be conservative.
    if (EL0.ExactNotTaken == EL1.ExactNotTaken)
      BECount = EL0.ExactNotTaken;
  }

  // There are cases (e.g. PR26207) where computeExitLimitFromCond is able
  // to be more aggressive when computing BECount than when computing
  // ConstantMaxBECount.  In these cases it is possible for EL0.ExactNotTaken
  // and
  // EL1.ExactNotTaken to match, but for EL0.ConstantMaxNotTaken and
  // EL1.ConstantMaxNotTaken to not.
  if (isa<SCEVCouldNotCompute>(ConstantMaxBECount) &&
      !isa<SCEVCouldNotCompute>(BECount))
    ConstantMaxBECount = getConstant(getUnsignedRangeMax(BECount));
  if (isa<SCEVCouldNotCompute>(SymbolicMaxBECount))
    SymbolicMaxBECount =
        isa<SCEVCouldNotCompute>(BECount) ? ConstantMaxBECount : BECount;
  return ExitLimit(BECount, ConstantMaxBECount, SymbolicMaxBECount, false,
                   { &EL0.Predicates, &EL1.Predicates });
}

ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromICmp(
    const Loop *L, ICmpInst *ExitCond, bool ExitIfTrue, bool ControlsOnlyExit,
    bool AllowPredicates) {
  // If the condition was exit on true, convert the condition to exit on false
  ICmpInst::Predicate Pred;
  if (!ExitIfTrue)
    Pred = ExitCond->getPredicate();
  else
    Pred = ExitCond->getInversePredicate();
  const ICmpInst::Predicate OriginalPred = Pred;

  const SCEV *LHS = getSCEV(ExitCond->getOperand(0));
  const SCEV *RHS = getSCEV(ExitCond->getOperand(1));

  ExitLimit EL = computeExitLimitFromICmp(L, Pred, LHS, RHS, ControlsOnlyExit,
                                          AllowPredicates);
  if (EL.hasAnyInfo())
    return EL;

  auto *ExhaustiveCount =
      computeExitCountExhaustively(L, ExitCond, ExitIfTrue);

  if (!isa<SCEVCouldNotCompute>(ExhaustiveCount))
    return ExhaustiveCount;

  return computeShiftCompareExitLimit(ExitCond->getOperand(0),
                                      ExitCond->getOperand(1), L, OriginalPred);
}
ScalarEvolution::ExitLimit ScalarEvolution::computeExitLimitFromICmp(
    const Loop *L, ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
    bool ControlsOnlyExit, bool AllowPredicates) {

  // Try to evaluate any dependencies out of the loop.
  LHS = getSCEVAtScope(LHS, L);
  RHS = getSCEVAtScope(RHS, L);

  // At this point, we would like to compute how many iterations of the
  // loop the predicate will return true for these inputs.
  if (isLoopInvariant(LHS, L) && !isLoopInvariant(RHS, L)) {
    // If there is a loop-invariant, force it into the RHS.
    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  bool ControllingFiniteLoop = ControlsOnlyExit && loopHasNoAbnormalExits(L) &&
                               loopIsFiniteByAssumption(L);
  // Simplify the operands before analyzing them.
  (void)SimplifyICmpOperands(Pred, LHS, RHS, /*Depth=*/0);

  // If we have a comparison of a chrec against a constant, try to use value
  // ranges to answer this query.
  if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS))
    if (const SCEVAddRecExpr *AddRec = dyn_cast<SCEVAddRecExpr>(LHS))
      if (AddRec->getLoop() == L) {
        // Form the constant range.
        ConstantRange CompRange =
            ConstantRange::makeExactICmpRegion(Pred, RHSC->getAPInt());

        const SCEV *Ret = AddRec->getNumIterationsInRange(CompRange, *this);
        if (!isa<SCEVCouldNotCompute>(Ret)) return Ret;
      }

  // If this loop must exit based on this condition (or execute undefined
  // behaviour), and we can prove the test sequence produced must repeat
  // the same values on self-wrap of the IV, then we can infer that IV
  // doesn't self wrap because if it did, we'd have an infinite (undefined)
  // loop.
  if (ControllingFiniteLoop && isLoopInvariant(RHS, L)) {
    // TODO: We can peel off any functions which are invertible *in L*.  Loop
    // invariant terms are effectively constants for our purposes here.
    auto *InnerLHS = LHS;
    if (auto *ZExt = dyn_cast<SCEVZeroExtendExpr>(LHS))
      InnerLHS = ZExt->getOperand();
    if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(InnerLHS)) {
      auto *StrideC = dyn_cast<SCEVConstant>(AR->getStepRecurrence(*this));
      if (!AR->hasNoSelfWrap() && AR->getLoop() == L && AR->isAffine() &&
          StrideC && StrideC->getAPInt().isPowerOf2()) {
        auto Flags = AR->getNoWrapFlags();
        Flags = setFlags(Flags, SCEV::FlagNW);
        SmallVector<const SCEV*> Operands{AR->operands()};
        Flags = StrengthenNoWrapFlags(this, scAddRecExpr, Operands, Flags);
        setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), Flags);
      }
    }
  }

  switch (Pred) {
  case ICmpInst::ICMP_NE: {                     // while (X != Y)
    // Convert to: while (X-Y != 0)
    if (LHS->getType()->isPointerTy()) {
      LHS = getLosslessPtrToIntExpr(LHS);
      if (isa<SCEVCouldNotCompute>(LHS))
        return LHS;
    }
    if (RHS->getType()->isPointerTy()) {
      RHS = getLosslessPtrToIntExpr(RHS);
      if (isa<SCEVCouldNotCompute>(RHS))
        return RHS;
    }
    ExitLimit EL = howFarToZero(getMinusSCEV(LHS, RHS), L, ControlsOnlyExit,
                                AllowPredicates);
    if (EL.hasAnyInfo())
      return EL;
    break;
  }
  case ICmpInst::ICMP_EQ: {                     // while (X == Y)
    // Convert to: while (X-Y == 0)
    if (LHS->getType()->isPointerTy()) {
      LHS = getLosslessPtrToIntExpr(LHS);
      if (isa<SCEVCouldNotCompute>(LHS))
        return LHS;
    }
    if (RHS->getType()->isPointerTy()) {
      RHS = getLosslessPtrToIntExpr(RHS);
      if (isa<SCEVCouldNotCompute>(RHS))
        return RHS;
    }
    ExitLimit EL = howFarToNonZero(getMinusSCEV(LHS, RHS), L);
    if (EL.hasAnyInfo()) return EL;
    break;
  }
  case ICmpInst::ICMP_SLE:
  case ICmpInst::ICMP_ULE:
    // Since the loop is finite, an invariant RHS cannot include the boundary
    // value, otherwise it would loop forever.
    if (!EnableFiniteLoopControl || !ControllingFiniteLoop ||
        !isLoopInvariant(RHS, L)) {
      // Otherwise, perform the addition in a wider type, to avoid overflow.
      // If the LHS is an addrec with the appropriate nowrap flag, the
      // extension will be sunk into it and the exit count can be analyzed.
      auto *OldType = dyn_cast<IntegerType>(LHS->getType());
      if (!OldType)
        break;
      // Prefer doubling the bitwidth over adding a single bit to make it more
      // likely that we use a legal type.
      auto *NewType =
          Type::getIntNTy(OldType->getContext(), OldType->getBitWidth() * 2);
      if (ICmpInst::isSigned(Pred)) {
        LHS = getSignExtendExpr(LHS, NewType);
        RHS = getSignExtendExpr(RHS, NewType);
      } else {
        LHS = getZeroExtendExpr(LHS, NewType);
        RHS = getZeroExtendExpr(RHS, NewType);
      }
    }
    RHS = getAddExpr(getOne(RHS->getType()), RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_ULT: { // while (X < Y)
    bool IsSigned = ICmpInst::isSigned(Pred);
    ExitLimit EL = howManyLessThans(LHS, RHS, L, IsSigned, ControlsOnlyExit,
                                    AllowPredicates);
    if (EL.hasAnyInfo())
      return EL;
    break;
  }
  case ICmpInst::ICMP_SGE:
  case ICmpInst::ICMP_UGE:
    // Since the loop is finite, an invariant RHS cannot include the boundary
    // value, otherwise it would loop forever.
    if (!EnableFiniteLoopControl || !ControllingFiniteLoop ||
        !isLoopInvariant(RHS, L))
      break;
    RHS = getAddExpr(getMinusOne(RHS->getType()), RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_UGT: { // while (X > Y)
    bool IsSigned = ICmpInst::isSigned(Pred);
    ExitLimit EL = howManyGreaterThans(LHS, RHS, L, IsSigned, ControlsOnlyExit,
                                       AllowPredicates);
    if (EL.hasAnyInfo())
      return EL;
    break;
  }
  default:
    break;
  }

  return getCouldNotCompute();
}

ScalarEvolution::ExitLimit
ScalarEvolution::computeExitLimitFromSingleExitSwitch(const Loop *L,
                                                      SwitchInst *Switch,
                                                      BasicBlock *ExitingBlock,
                                                      bool ControlsOnlyExit) {
  assert(!L->contains(ExitingBlock) && "Not an exiting block!");

  // Give up if the exit is the default dest of a switch.
  if (Switch->getDefaultDest() == ExitingBlock)
    return getCouldNotCompute();

  assert(L->contains(Switch->getDefaultDest()) &&
         "Default case must not exit the loop!");
  const SCEV *LHS = getSCEVAtScope(Switch->getCondition(), L);
  const SCEV *RHS = getConstant(Switch->findCaseDest(ExitingBlock));

  // while (X != Y) --> while (X-Y != 0)
  ExitLimit EL = howFarToZero(getMinusSCEV(LHS, RHS), L, ControlsOnlyExit);
  if (EL.hasAnyInfo())
    return EL;

  return getCouldNotCompute();
}

static ConstantInt *
EvaluateConstantChrecAtConstant(const SCEVAddRecExpr *AddRec, ConstantInt *C,
                                ScalarEvolution &SE) {
  const SCEV *InVal = SE.getConstant(C);
  const SCEV *Val = AddRec->evaluateAtIteration(InVal, SE);
  assert(isa<SCEVConstant>(Val) &&
         "Evaluation of SCEV at constant didn't fold correctly?");
  return cast<SCEVConstant>(Val)->getValue();
}

ScalarEvolution::ExitLimit ScalarEvolution::computeShiftCompareExitLimit(
    Value *LHS, Value *RHSV, const Loop *L, ICmpInst::Predicate Pred) {
  ConstantInt *RHS = dyn_cast<ConstantInt>(RHSV);
  if (!RHS)
    return getCouldNotCompute();

  const BasicBlock *Latch = L->getLoopLatch();
  if (!Latch)
    return getCouldNotCompute();

  const BasicBlock *Predecessor = L->getLoopPredecessor();
  if (!Predecessor)
    return getCouldNotCompute();

  // Return true if V is of the form "LHS `shift_op` <positive constant>".
  // Return LHS in OutLHS and shift_opt in OutOpCode.
  auto MatchPositiveShift =
      [](Value *V, Value *&OutLHS, Instruction::BinaryOps &OutOpCode) {

    using namespace PatternMatch;

    ConstantInt *ShiftAmt;
    if (match(V, m_LShr(m_Value(OutLHS), m_ConstantInt(ShiftAmt))))
      OutOpCode = Instruction::LShr;
    else if (match(V, m_AShr(m_Value(OutLHS), m_ConstantInt(ShiftAmt))))
      OutOpCode = Instruction::AShr;
    else if (match(V, m_Shl(m_Value(OutLHS), m_ConstantInt(ShiftAmt))))
      OutOpCode = Instruction::Shl;
    else
      return false;

    return ShiftAmt->getValue().isStrictlyPositive();
  };

  // Recognize a "shift recurrence" either of the form %iv or of %iv.shifted in
  //
  // loop:
  //   %iv = phi i32 [ %iv.shifted, %loop ], [ %val, %preheader ]
  //   %iv.shifted = lshr i32 %iv, <positive constant>
  //
  // Return true on a successful match.  Return the corresponding PHI node (%iv
  // above) in PNOut and the opcode of the shift operation in OpCodeOut.
  auto MatchShiftRecurrence =
      [&](Value *V, PHINode *&PNOut, Instruction::BinaryOps &OpCodeOut) {
    std::optional<Instruction::BinaryOps> PostShiftOpCode;

    {
      Instruction::BinaryOps OpC;
      Value *V;

      // If we encounter a shift instruction, "peel off" the shift operation,
      // and remember that we did so.  Later when we inspect %iv's backedge
      // value, we will make sure that the backedge value uses the same
      // operation.
      //
      // Note: the peeled shift operation does not have to be the same
      // instruction as the one feeding into the PHI's backedge value.  We only
      // really care about it being the same *kind* of shift instruction --
      // that's all that is required for our later inferences to hold.
      if (MatchPositiveShift(LHS, V, OpC)) {
        PostShiftOpCode = OpC;
        LHS = V;
      }
    }

    PNOut = dyn_cast<PHINode>(LHS);
    if (!PNOut || PNOut->getParent() != L->getHeader())
      return false;

    Value *BEValue = PNOut->getIncomingValueForBlock(Latch);
    Value *OpLHS;

    return
        // The backedge value for the PHI node must be a shift by a positive
        // amount
        MatchPositiveShift(BEValue, OpLHS, OpCodeOut) &&

        // of the PHI node itself
        OpLHS == PNOut &&

        // and the kind of shift should be match the kind of shift we peeled
        // off, if any.
        (!PostShiftOpCode || *PostShiftOpCode == OpCodeOut);
  };

  PHINode *PN;
  Instruction::BinaryOps OpCode;
  if (!MatchShiftRecurrence(LHS, PN, OpCode))
    return getCouldNotCompute();

  const DataLayout &DL = getDataLayout();

  // The key rationale for this optimization is that for some kinds of shift
  // recurrences, the value of the recurrence "stabilizes" to either 0 or -1
  // within a finite number of iterations.  If the condition guarding the
  // backedge (in the sense that the backedge is taken if the condition is true)
  // is false for the value the shift recurrence stabilizes to, then we know
  // that the backedge is taken only a finite number of times.

  ConstantInt *StableValue = nullptr;
  switch (OpCode) {
  default:
    llvm_unreachable("Impossible case!");

  case Instruction::AShr: {
    // {K,ashr,<positive-constant>} stabilizes to signum(K) in at most
    // bitwidth(K) iterations.
    Value *FirstValue = PN->getIncomingValueForBlock(Predecessor);
    KnownBits Known = computeKnownBits(FirstValue, DL, 0, &AC,
                                       Predecessor->getTerminator(), &DT);
    auto *Ty = cast<IntegerType>(RHS->getType());
    if (Known.isNonNegative())
      StableValue = ConstantInt::get(Ty, 0);
    else if (Known.isNegative())
      StableValue = ConstantInt::get(Ty, -1, true);
    else
      return getCouldNotCompute();

    break;
  }
  case Instruction::LShr:
  case Instruction::Shl:
    // Both {K,lshr,<positive-constant>} and {K,shl,<positive-constant>}
    // stabilize to 0 in at most bitwidth(K) iterations.
    StableValue = ConstantInt::get(cast<IntegerType>(RHS->getType()), 0);
    break;
  }

  auto *Result =
      ConstantFoldCompareInstOperands(Pred, StableValue, RHS, DL, &TLI);
  assert(Result->getType()->isIntegerTy(1) &&
         "Otherwise cannot be an operand to a branch instruction");

  if (Result->isZeroValue()) {
    unsigned BitWidth = getTypeSizeInBits(RHS->getType());
    const SCEV *UpperBound =
        getConstant(getEffectiveSCEVType(RHS->getType()), BitWidth);
    return ExitLimit(getCouldNotCompute(), UpperBound, UpperBound, false);
  }

  return getCouldNotCompute();
}

/// Return true if we can constant fold an instruction of the specified type,
/// assuming that all operands were constants.
static bool CanConstantFold(const Instruction *I) {
  if (isa<BinaryOperator>(I) || isa<CmpInst>(I) ||
      isa<SelectInst>(I) || isa<CastInst>(I) || isa<GetElementPtrInst>(I) ||
      isa<LoadInst>(I) || isa<ExtractValueInst>(I))
    return true;

  if (const CallInst *CI = dyn_cast<CallInst>(I))
    if (const Function *F = CI->getCalledFunction())
      return canConstantFoldCallTo(CI, F);
  return false;
}

/// Determine whether this instruction can constant evolve within this loop
/// assuming its operands can all constant evolve.
static bool canConstantEvolve(Instruction *I, const Loop *L) {
  // An instruction outside of the loop can't be derived from a loop PHI.
  if (!L->contains(I)) return false;

  if (isa<PHINode>(I)) {
    // We don't currently keep track of the control flow needed to evaluate
    // PHIs, so we cannot handle PHIs inside of loops.
    return L->getHeader() == I->getParent();
  }

  // If we won't be able to constant fold this expression even if the operands
  // are constants, bail early.
  return CanConstantFold(I);
}

/// getConstantEvolvingPHIOperands - Implement getConstantEvolvingPHI by
/// recursing through each instruction operand until reaching a loop header phi.
static PHINode *
getConstantEvolvingPHIOperands(Instruction *UseInst, const Loop *L,
                               DenseMap<Instruction *, PHINode *> &PHIMap,
                               unsigned Depth) {
  if (Depth > MaxConstantEvolvingDepth)
    return nullptr;

  // Otherwise, we can evaluate this instruction if all of its operands are
  // constant or derived from a PHI node themselves.
  PHINode *PHI = nullptr;
  for (Value *Op : UseInst->operands()) {
    if (isa<Constant>(Op)) continue;

    Instruction *OpInst = dyn_cast<Instruction>(Op);
    if (!OpInst || !canConstantEvolve(OpInst, L)) return nullptr;

    PHINode *P = dyn_cast<PHINode>(OpInst);
    if (!P)
      // If this operand is already visited, reuse the prior result.
      // We may have P != PHI if this is the deepest point at which the
      // inconsistent paths meet.
      P = PHIMap.lookup(OpInst);
    if (!P) {
      // Recurse and memoize the results, whether a phi is found or not.
      // This recursive call invalidates pointers into PHIMap.
      P = getConstantEvolvingPHIOperands(OpInst, L, PHIMap, Depth + 1);
      PHIMap[OpInst] = P;
    }
    if (!P)
      return nullptr;  // Not evolving from PHI
    if (PHI && PHI != P)
      return nullptr;  // Evolving from multiple different PHIs.
    PHI = P;
  }
  // This is a expression evolving from a constant PHI!
  return PHI;
}

/// getConstantEvolvingPHI - Given an LLVM value and a loop, return a PHI node
/// in the loop that V is derived from.  We allow arbitrary operations along the
/// way, but the operands of an operation must either be constants or a value
/// derived from a constant PHI.  If this expression does not fit with these
/// constraints, return null.
static PHINode *getConstantEvolvingPHI(Value *V, const Loop *L) {
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I || !canConstantEvolve(I, L)) return nullptr;

  if (PHINode *PN = dyn_cast<PHINode>(I))
    return PN;

  // Record non-constant instructions contained by the loop.
  DenseMap<Instruction *, PHINode *> PHIMap;
  return getConstantEvolvingPHIOperands(I, L, PHIMap, 0);
}

/// EvaluateExpression - Given an expression that passes the
/// getConstantEvolvingPHI predicate, evaluate its value assuming the PHI node
/// in the loop has the value PHIVal.  If we can't fold this expression for some
/// reason, return null.
static Constant *EvaluateExpression(Value *V, const Loop *L,
                                    DenseMap<Instruction *, Constant *> &Vals,
                                    const DataLayout &DL,
                                    const TargetLibraryInfo *TLI) {
  // Convenient constant check, but redundant for recursive calls.
  if (Constant *C = dyn_cast<Constant>(V)) return C;
  Instruction *I = dyn_cast<Instruction>(V);
  if (!I) return nullptr;

  if (Constant *C = Vals.lookup(I)) return C;

  // An instruction inside the loop depends on a value outside the loop that we
  // weren't given a mapping for, or a value such as a call inside the loop.
  if (!canConstantEvolve(I, L)) return nullptr;

  // An unmapped PHI can be due to a branch or another loop inside this loop,
  // or due to this not being the initial iteration through a loop where we
  // couldn't compute the evolution of this particular PHI last time.
  if (isa<PHINode>(I)) return nullptr;

  std::vector<Constant*> Operands(I->getNumOperands());

  for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
    Instruction *Operand = dyn_cast<Instruction>(I->getOperand(i));
    if (!Operand) {
      Operands[i] = dyn_cast<Constant>(I->getOperand(i));
      if (!Operands[i]) return nullptr;
      continue;
    }
    Constant *C = EvaluateExpression(Operand, L, Vals, DL, TLI);
    Vals[Operand] = C;
    if (!C) return nullptr;
    Operands[i] = C;
  }

  return ConstantFoldInstOperands(I, Operands, DL, TLI,
                                  /*AllowNonDeterministic=*/false);
}


// If every incoming value to PN except the one for BB is a specific Constant,
// return that, else return nullptr.
static Constant *getOtherIncomingValue(PHINode *PN, BasicBlock *BB) {
  Constant *IncomingVal = nullptr;

  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    if (PN->getIncomingBlock(i) == BB)
      continue;

    auto *CurrentVal = dyn_cast<Constant>(PN->getIncomingValue(i));
    if (!CurrentVal)
      return nullptr;

    if (IncomingVal != CurrentVal) {
      if (IncomingVal)
        return nullptr;
      IncomingVal = CurrentVal;
    }
  }

  return IncomingVal;
}

/// getConstantEvolutionLoopExitValue - If we know that the specified Phi is
/// in the header of its containing loop, we know the loop executes a
/// constant number of times, and the PHI node is just a recurrence
/// involving constants, fold it.
Constant *
ScalarEvolution::getConstantEvolutionLoopExitValue(PHINode *PN,
                                                   const APInt &BEs,
                                                   const Loop *L) {
  auto I = ConstantEvolutionLoopExitValue.find(PN);
  if (I != ConstantEvolutionLoopExitValue.end())
    return I->second;

  if (BEs.ugt(MaxBruteForceIterations))
    return ConstantEvolutionLoopExitValue[PN] = nullptr;  // Not going to evaluate it.

  Constant *&RetVal = ConstantEvolutionLoopExitValue[PN];

  DenseMap<Instruction *, Constant *> CurrentIterVals;
  BasicBlock *Header = L->getHeader();
  assert(PN->getParent() == Header && "Can't evaluate PHI not in loop header!");

  BasicBlock *Latch = L->getLoopLatch();
  if (!Latch)
    return nullptr;

  for (PHINode &PHI : Header->phis()) {
    if (auto *StartCST = getOtherIncomingValue(&PHI, Latch))
      CurrentIterVals[&PHI] = StartCST;
  }
  if (!CurrentIterVals.count(PN))
    return RetVal = nullptr;

  Value *BEValue = PN->getIncomingValueForBlock(Latch);

  // Execute the loop symbolically to determine the exit value.
  assert(BEs.getActiveBits() < CHAR_BIT * sizeof(unsigned) &&
         "BEs is <= MaxBruteForceIterations which is an 'unsigned'!");

  unsigned NumIterations = BEs.getZExtValue(); // must be in range
  unsigned IterationNum = 0;
  const DataLayout &DL = getDataLayout();
  for (; ; ++IterationNum) {
    if (IterationNum == NumIterations)
      return RetVal = CurrentIterVals[PN];  // Got exit value!

    // Compute the value of the PHIs for the next iteration.
    // EvaluateExpression adds non-phi values to the CurrentIterVals map.
    DenseMap<Instruction *, Constant *> NextIterVals;
    Constant *NextPHI =
        EvaluateExpression(BEValue, L, CurrentIterVals, DL, &TLI);
    if (!NextPHI)
      return nullptr;        // Couldn't evaluate!
    NextIterVals[PN] = NextPHI;

    bool StoppedEvolving = NextPHI == CurrentIterVals[PN];

    // Also evaluate the other PHI nodes.  However, we don't get to stop if we
    // cease to be able to evaluate one of them or if they stop evolving,
    // because that doesn't necessarily prevent us from computing PN.
    SmallVector<std::pair<PHINode *, Constant *>, 8> PHIsToCompute;
    for (const auto &I : CurrentIterVals) {
      PHINode *PHI = dyn_cast<PHINode>(I.first);
      if (!PHI || PHI == PN || PHI->getParent() != Header) continue;
      PHIsToCompute.emplace_back(PHI, I.second);
    }
    // We use two distinct loops because EvaluateExpression may invalidate any
    // iterators into CurrentIterVals.
    for (const auto &I : PHIsToCompute) {
      PHINode *PHI = I.first;
      Constant *&NextPHI = NextIterVals[PHI];
      if (!NextPHI) {   // Not already computed.
        Value *BEValue = PHI->getIncomingValueForBlock(Latch);
        NextPHI = EvaluateExpression(BEValue, L, CurrentIterVals, DL, &TLI);
      }
      if (NextPHI != I.second)
        StoppedEvolving = false;
    }

    // If all entries in CurrentIterVals == NextIterVals then we can stop
    // iterating, the loop can't continue to change.
    if (StoppedEvolving)
      return RetVal = CurrentIterVals[PN];

    CurrentIterVals.swap(NextIterVals);
  }
}

const SCEV *ScalarEvolution::computeExitCountExhaustively(const Loop *L,
                                                          Value *Cond,
                                                          bool ExitWhen) {
  PHINode *PN = getConstantEvolvingPHI(Cond, L);
  if (!PN) return getCouldNotCompute();

  // If the loop is canonicalized, the PHI will have exactly two entries.
  // That's the only form we support here.
  if (PN->getNumIncomingValues() != 2) return getCouldNotCompute();

  DenseMap<Instruction *, Constant *> CurrentIterVals;
  BasicBlock *Header = L->getHeader();
  assert(PN->getParent() == Header && "Can't evaluate PHI not in loop header!");

  BasicBlock *Latch = L->getLoopLatch();
  assert(Latch && "Should follow from NumIncomingValues == 2!");

  for (PHINode &PHI : Header->phis()) {
    if (auto *StartCST = getOtherIncomingValue(&PHI, Latch))
      CurrentIterVals[&PHI] = StartCST;
  }
  if (!CurrentIterVals.count(PN))
    return getCouldNotCompute();

  // Okay, we find a PHI node that defines the trip count of this loop.  Execute
  // the loop symbolically to determine when the condition gets a value of
  // "ExitWhen".
  unsigned MaxIterations = MaxBruteForceIterations;   // Limit analysis.
  const DataLayout &DL = getDataLayout();
  for (unsigned IterationNum = 0; IterationNum != MaxIterations;++IterationNum){
    auto *CondVal = dyn_cast_or_null<ConstantInt>(
        EvaluateExpression(Cond, L, CurrentIterVals, DL, &TLI));

    // Couldn't symbolically evaluate.
    if (!CondVal) return getCouldNotCompute();

    if (CondVal->getValue() == uint64_t(ExitWhen)) {
      ++NumBruteForceTripCountsComputed;
      return getConstant(Type::getInt32Ty(getContext()), IterationNum);
    }

    // Update all the PHI nodes for the next iteration.
    DenseMap<Instruction *, Constant *> NextIterVals;

    // Create a list of which PHIs we need to compute. We want to do this before
    // calling EvaluateExpression on them because that may invalidate iterators
    // into CurrentIterVals.
    SmallVector<PHINode *, 8> PHIsToCompute;
    for (const auto &I : CurrentIterVals) {
      PHINode *PHI = dyn_cast<PHINode>(I.first);
      if (!PHI || PHI->getParent() != Header) continue;
      PHIsToCompute.push_back(PHI);
    }
    for (PHINode *PHI : PHIsToCompute) {
      Constant *&NextPHI = NextIterVals[PHI];
      if (NextPHI) continue;    // Already computed!

      Value *BEValue = PHI->getIncomingValueForBlock(Latch);
      NextPHI = EvaluateExpression(BEValue, L, CurrentIterVals, DL, &TLI);
    }
    CurrentIterVals.swap(NextIterVals);
  }

  // Too many iterations were needed to evaluate.
  return getCouldNotCompute();
}

const SCEV *ScalarEvolution::getSCEVAtScope(const SCEV *V, const Loop *L) {
  SmallVector<std::pair<const Loop *, const SCEV *>, 2> &Values =
      ValuesAtScopes[V];
  // Check to see if we've folded this expression at this loop before.
  for (auto &LS : Values)
    if (LS.first == L)
      return LS.second ? LS.second : V;

  Values.emplace_back(L, nullptr);

  // Otherwise compute it.
  const SCEV *C = computeSCEVAtScope(V, L);
  for (auto &LS : reverse(ValuesAtScopes[V]))
    if (LS.first == L) {
      LS.second = C;
      if (!isa<SCEVConstant>(C))
        ValuesAtScopesUsers[C].push_back({L, V});
      break;
    }
  return C;
}

/// This builds up a Constant using the ConstantExpr interface.  That way, we
/// will return Constants for objects which aren't represented by a
/// SCEVConstant, because SCEVConstant is restricted to ConstantInt.
/// Returns NULL if the SCEV isn't representable as a Constant.
static Constant *BuildConstantFromSCEV(const SCEV *V) {
  switch (V->getSCEVType()) {
  case scCouldNotCompute:
  case scAddRecExpr:
  case scVScale:
    return nullptr;
  case scConstant:
    return cast<SCEVConstant>(V)->getValue();
  case scUnknown:
    return dyn_cast<Constant>(cast<SCEVUnknown>(V)->getValue());
  case scPtrToInt: {
    const SCEVPtrToIntExpr *P2I = cast<SCEVPtrToIntExpr>(V);
    if (Constant *CastOp = BuildConstantFromSCEV(P2I->getOperand()))
      return ConstantExpr::getPtrToInt(CastOp, P2I->getType());

    return nullptr;
  }
  case scTruncate: {
    const SCEVTruncateExpr *ST = cast<SCEVTruncateExpr>(V);
    if (Constant *CastOp = BuildConstantFromSCEV(ST->getOperand()))
      return ConstantExpr::getTrunc(CastOp, ST->getType());
    return nullptr;
  }
  case scAddExpr: {
    const SCEVAddExpr *SA = cast<SCEVAddExpr>(V);
    Constant *C = nullptr;
    for (const SCEV *Op : SA->operands()) {
      Constant *OpC = BuildConstantFromSCEV(Op);
      if (!OpC)
        return nullptr;
      if (!C) {
        C = OpC;
        continue;
      }
      assert(!C->getType()->isPointerTy() &&
             "Can only have one pointer, and it must be last");
      if (OpC->getType()->isPointerTy()) {
        // The offsets have been converted to bytes.  We can add bytes using
        // an i8 GEP.
        C = ConstantExpr::getGetElementPtr(Type::getInt8Ty(C->getContext()),
                                           OpC, C);
      } else {
        C = ConstantExpr::getAdd(C, OpC);
      }
    }
    return C;
  }
  case scMulExpr:
  case scSignExtend:
  case scZeroExtend:
  case scUDivExpr:
  case scSMaxExpr:
  case scUMaxExpr:
  case scSMinExpr:
  case scUMinExpr:
  case scSequentialUMinExpr:
    return nullptr;
  }
  llvm_unreachable("Unknown SCEV kind!");
}

const SCEV *
ScalarEvolution::getWithOperands(const SCEV *S,
                                 SmallVectorImpl<const SCEV *> &NewOps) {
  switch (S->getSCEVType()) {
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
  case scPtrToInt:
    return getCastExpr(S->getSCEVType(), NewOps[0], S->getType());
  case scAddRecExpr: {
    auto *AddRec = cast<SCEVAddRecExpr>(S);
    return getAddRecExpr(NewOps, AddRec->getLoop(), AddRec->getNoWrapFlags());
  }
  case scAddExpr:
    return getAddExpr(NewOps, cast<SCEVAddExpr>(S)->getNoWrapFlags());
  case scMulExpr:
    return getMulExpr(NewOps, cast<SCEVMulExpr>(S)->getNoWrapFlags());
  case scUDivExpr:
    return getUDivExpr(NewOps[0], NewOps[1]);
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
    return getMinMaxExpr(S->getSCEVType(), NewOps);
  case scSequentialUMinExpr:
    return getSequentialMinMaxExpr(S->getSCEVType(), NewOps);
  case scConstant:
  case scVScale:
  case scUnknown:
    return S;
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

const SCEV *ScalarEvolution::computeSCEVAtScope(const SCEV *V, const Loop *L) {
  switch (V->getSCEVType()) {
  case scConstant:
  case scVScale:
    return V;
  case scAddRecExpr: {
    // If this is a loop recurrence for a loop that does not contain L, then we
    // are dealing with the final value computed by the loop.
    const SCEVAddRecExpr *AddRec = cast<SCEVAddRecExpr>(V);
    // First, attempt to evaluate each operand.
    // Avoid performing the look-up in the common case where the specified
    // expression has no loop-variant portions.
    for (unsigned i = 0, e = AddRec->getNumOperands(); i != e; ++i) {
      const SCEV *OpAtScope = getSCEVAtScope(AddRec->getOperand(i), L);
      if (OpAtScope == AddRec->getOperand(i))
        continue;

      // Okay, at least one of these operands is loop variant but might be
      // foldable.  Build a new instance of the folded commutative expression.
      SmallVector<const SCEV *, 8> NewOps;
      NewOps.reserve(AddRec->getNumOperands());
      append_range(NewOps, AddRec->operands().take_front(i));
      NewOps.push_back(OpAtScope);
      for (++i; i != e; ++i)
        NewOps.push_back(getSCEVAtScope(AddRec->getOperand(i), L));

      const SCEV *FoldedRec = getAddRecExpr(
          NewOps, AddRec->getLoop(), AddRec->getNoWrapFlags(SCEV::FlagNW));
      AddRec = dyn_cast<SCEVAddRecExpr>(FoldedRec);
      // The addrec may be folded to a nonrecurrence, for example, if the
      // induction variable is multiplied by zero after constant folding. Go
      // ahead and return the folded value.
      if (!AddRec)
        return FoldedRec;
      break;
    }

    // If the scope is outside the addrec's loop, evaluate it by using the
    // loop exit value of the addrec.
    if (!AddRec->getLoop()->contains(L)) {
      // To evaluate this recurrence, we need to know how many times the AddRec
      // loop iterates.  Compute this now.
      const SCEV *BackedgeTakenCount = getBackedgeTakenCount(AddRec->getLoop());
      if (BackedgeTakenCount == getCouldNotCompute())
        return AddRec;

      // Then, evaluate the AddRec.
      return AddRec->evaluateAtIteration(BackedgeTakenCount, *this);
    }

    return AddRec;
  }
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
  case scPtrToInt:
  case scAddExpr:
  case scMulExpr:
  case scUDivExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr: {
    ArrayRef<const SCEV *> Ops = V->operands();
    // Avoid performing the look-up in the common case where the specified
    // expression has no loop-variant portions.
    for (unsigned i = 0, e = Ops.size(); i != e; ++i) {
      const SCEV *OpAtScope = getSCEVAtScope(Ops[i], L);
      if (OpAtScope != Ops[i]) {
        // Okay, at least one of these operands is loop variant but might be
        // foldable.  Build a new instance of the folded commutative expression.
        SmallVector<const SCEV *, 8> NewOps;
        NewOps.reserve(Ops.size());
        append_range(NewOps, Ops.take_front(i));
        NewOps.push_back(OpAtScope);

        for (++i; i != e; ++i) {
          OpAtScope = getSCEVAtScope(Ops[i], L);
          NewOps.push_back(OpAtScope);
        }

        return getWithOperands(V, NewOps);
      }
    }
    // If we got here, all operands are loop invariant.
    return V;
  }
  case scUnknown: {
    // If this instruction is evolved from a constant-evolving PHI, compute the
    // exit value from the loop without using SCEVs.
    const SCEVUnknown *SU = cast<SCEVUnknown>(V);
    Instruction *I = dyn_cast<Instruction>(SU->getValue());
    if (!I)
      return V; // This is some other type of SCEVUnknown, just return it.

    if (PHINode *PN = dyn_cast<PHINode>(I)) {
      const Loop *CurrLoop = this->LI[I->getParent()];
      // Looking for loop exit value.
      if (CurrLoop && CurrLoop->getParentLoop() == L &&
          PN->getParent() == CurrLoop->getHeader()) {
        // Okay, there is no closed form solution for the PHI node.  Check
        // to see if the loop that contains it has a known backedge-taken
        // count.  If so, we may be able to force computation of the exit
        // value.
        const SCEV *BackedgeTakenCount = getBackedgeTakenCount(CurrLoop);
        // This trivial case can show up in some degenerate cases where
        // the incoming IR has not yet been fully simplified.
        if (BackedgeTakenCount->isZero()) {
          Value *InitValue = nullptr;
          bool MultipleInitValues = false;
          for (unsigned i = 0; i < PN->getNumIncomingValues(); i++) {
            if (!CurrLoop->contains(PN->getIncomingBlock(i))) {
              if (!InitValue)
                InitValue = PN->getIncomingValue(i);
              else if (InitValue != PN->getIncomingValue(i)) {
                MultipleInitValues = true;
                break;
              }
            }
          }
          if (!MultipleInitValues && InitValue)
            return getSCEV(InitValue);
        }
        // Do we have a loop invariant value flowing around the backedge
        // for a loop which must execute the backedge?
        if (!isa<SCEVCouldNotCompute>(BackedgeTakenCount) &&
            isKnownNonZero(BackedgeTakenCount) &&
            PN->getNumIncomingValues() == 2) {

          unsigned InLoopPred =
              CurrLoop->contains(PN->getIncomingBlock(0)) ? 0 : 1;
          Value *BackedgeVal = PN->getIncomingValue(InLoopPred);
          if (CurrLoop->isLoopInvariant(BackedgeVal))
            return getSCEV(BackedgeVal);
        }
        if (auto *BTCC = dyn_cast<SCEVConstant>(BackedgeTakenCount)) {
          // Okay, we know how many times the containing loop executes.  If
          // this is a constant evolving PHI node, get the final value at
          // the specified iteration number.
          Constant *RV =
              getConstantEvolutionLoopExitValue(PN, BTCC->getAPInt(), CurrLoop);
          if (RV)
            return getSCEV(RV);
        }
      }
    }

    // Okay, this is an expression that we cannot symbolically evaluate
    // into a SCEV.  Check to see if it's possible to symbolically evaluate
    // the arguments into constants, and if so, try to constant propagate the
    // result.  This is particularly useful for computing loop exit values.
    if (!CanConstantFold(I))
      return V; // This is some other type of SCEVUnknown, just return it.

    SmallVector<Constant *, 4> Operands;
    Operands.reserve(I->getNumOperands());
    bool MadeImprovement = false;
    for (Value *Op : I->operands()) {
      if (Constant *C = dyn_cast<Constant>(Op)) {
        Operands.push_back(C);
        continue;
      }

      // If any of the operands is non-constant and if they are
      // non-integer and non-pointer, don't even try to analyze them
      // with scev techniques.
      if (!isSCEVable(Op->getType()))
        return V;

      const SCEV *OrigV = getSCEV(Op);
      const SCEV *OpV = getSCEVAtScope(OrigV, L);
      MadeImprovement |= OrigV != OpV;

      Constant *C = BuildConstantFromSCEV(OpV);
      if (!C)
        return V;
      assert(C->getType() == Op->getType() && "Type mismatch");
      Operands.push_back(C);
    }

    // Check to see if getSCEVAtScope actually made an improvement.
    if (!MadeImprovement)
      return V; // This is some other type of SCEVUnknown, just return it.

    Constant *C = nullptr;
    const DataLayout &DL = getDataLayout();
    C = ConstantFoldInstOperands(I, Operands, DL, &TLI,
                                 /*AllowNonDeterministic=*/false);
    if (!C)
      return V;
    return getSCEV(C);
  }
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV type!");
}

const SCEV *ScalarEvolution::getSCEVAtScope(Value *V, const Loop *L) {
  return getSCEVAtScope(getSCEV(V), L);
}

const SCEV *ScalarEvolution::stripInjectiveFunctions(const SCEV *S) const {
  if (const SCEVZeroExtendExpr *ZExt = dyn_cast<SCEVZeroExtendExpr>(S))
    return stripInjectiveFunctions(ZExt->getOperand());
  if (const SCEVSignExtendExpr *SExt = dyn_cast<SCEVSignExtendExpr>(S))
    return stripInjectiveFunctions(SExt->getOperand());
  return S;
}

/// Finds the minimum unsigned root of the following equation:
///
///     A * X = B (mod N)
///
/// where N = 2^BW and BW is the common bit width of A and B. The signedness of
/// A and B isn't important.
///
/// If the equation does not have a solution, SCEVCouldNotCompute is returned.
static const SCEV *SolveLinEquationWithOverflow(const APInt &A, const SCEV *B,
                                               ScalarEvolution &SE) {
  uint32_t BW = A.getBitWidth();
  assert(BW == SE.getTypeSizeInBits(B->getType()));
  assert(A != 0 && "A must be non-zero.");

  // 1. D = gcd(A, N)
  //
  // The gcd of A and N may have only one prime factor: 2. The number of
  // trailing zeros in A is its multiplicity
  uint32_t Mult2 = A.countr_zero();
  // D = 2^Mult2

  // 2. Check if B is divisible by D.
  //
  // B is divisible by D if and only if the multiplicity of prime factor 2 for B
  // is not less than multiplicity of this prime factor for D.
  if (SE.getMinTrailingZeros(B) < Mult2)
    return SE.getCouldNotCompute();

  // 3. Compute I: the multiplicative inverse of (A / D) in arithmetic
  // modulo (N / D).
  //
  // If D == 1, (N / D) == N == 2^BW, so we need one extra bit to represent
  // (N / D) in general. The inverse itself always fits into BW bits, though,
  // so we immediately truncate it.
  APInt AD = A.lshr(Mult2).trunc(BW - Mult2); // AD = A / D
  APInt I = AD.multiplicativeInverse().zext(BW);

  // 4. Compute the minimum unsigned root of the equation:
  // I * (B / D) mod (N / D)
  // To simplify the computation, we factor out the divide by D:
  // (I * B mod N) / D
  const SCEV *D = SE.getConstant(APInt::getOneBitSet(BW, Mult2));
  return SE.getUDivExactExpr(SE.getMulExpr(B, SE.getConstant(I)), D);
}

/// For a given quadratic addrec, generate coefficients of the corresponding
/// quadratic equation, multiplied by a common value to ensure that they are
/// integers.
/// The returned value is a tuple { A, B, C, M, BitWidth }, where
/// Ax^2 + Bx + C is the quadratic function, M is the value that A, B and C
/// were multiplied by, and BitWidth is the bit width of the original addrec
/// coefficients.
/// This function returns std::nullopt if the addrec coefficients are not
/// compile- time constants.
static std::optional<std::tuple<APInt, APInt, APInt, APInt, unsigned>>
GetQuadraticEquation(const SCEVAddRecExpr *AddRec) {
  assert(AddRec->getNumOperands() == 3 && "This is not a quadratic chrec!");
  const SCEVConstant *LC = dyn_cast<SCEVConstant>(AddRec->getOperand(0));
  const SCEVConstant *MC = dyn_cast<SCEVConstant>(AddRec->getOperand(1));
  const SCEVConstant *NC = dyn_cast<SCEVConstant>(AddRec->getOperand(2));
  LLVM_DEBUG(dbgs() << __func__ << ": analyzing quadratic addrec: "
                    << *AddRec << '\n');

  // We currently can only solve this if the coefficients are constants.
  if (!LC || !MC || !NC) {
    LLVM_DEBUG(dbgs() << __func__ << ": coefficients are not constant\n");
    return std::nullopt;
  }

  APInt L = LC->getAPInt();
  APInt M = MC->getAPInt();
  APInt N = NC->getAPInt();
  assert(!N.isZero() && "This is not a quadratic addrec");

  unsigned BitWidth = LC->getAPInt().getBitWidth();
  unsigned NewWidth = BitWidth + 1;
  LLVM_DEBUG(dbgs() << __func__ << ": addrec coeff bw: "
                    << BitWidth << '\n');
  // The sign-extension (as opposed to a zero-extension) here matches the
  // extension used in SolveQuadraticEquationWrap (with the same motivation).
  N = N.sext(NewWidth);
  M = M.sext(NewWidth);
  L = L.sext(NewWidth);

  // The increments are M, M+N, M+2N, ..., so the accumulated values are
  //   L+M, (L+M)+(M+N), (L+M)+(M+N)+(M+2N), ..., that is,
  //   L+M, L+2M+N, L+3M+3N, ...
  // After n iterations the accumulated value Acc is L + nM + n(n-1)/2 N.
  //
  // The equation Acc = 0 is then
  //   L + nM + n(n-1)/2 N = 0,  or  2L + 2M n + n(n-1) N = 0.
  // In a quadratic form it becomes:
  //   N n^2 + (2M-N) n + 2L = 0.

  APInt A = N;
  APInt B = 2 * M - A;
  APInt C = 2 * L;
  APInt T = APInt(NewWidth, 2);
  LLVM_DEBUG(dbgs() << __func__ << ": equation " << A << "x^2 + " << B
                    << "x + " << C << ", coeff bw: " << NewWidth
                    << ", multiplied by " << T << '\n');
  return std::make_tuple(A, B, C, T, BitWidth);
}

/// Helper function to compare optional APInts:
/// (a) if X and Y both exist, return min(X, Y),
/// (b) if neither X nor Y exist, return std::nullopt,
/// (c) if exactly one of X and Y exists, return that value.
static std::optional<APInt> MinOptional(std::optional<APInt> X,
                                        std::optional<APInt> Y) {
  if (X && Y) {
    unsigned W = std::max(X->getBitWidth(), Y->getBitWidth());
    APInt XW = X->sext(W);
    APInt YW = Y->sext(W);
    return XW.slt(YW) ? *X : *Y;
  }
  if (!X && !Y)
    return std::nullopt;
  return X ? *X : *Y;
}

/// Helper function to truncate an optional APInt to a given BitWidth.
/// When solving addrec-related equations, it is preferable to return a value
/// that has the same bit width as the original addrec's coefficients. If the
/// solution fits in the original bit width, truncate it (except for i1).
/// Returning a value of a different bit width may inhibit some optimizations.
///
/// In general, a solution to a quadratic equation generated from an addrec
/// may require BW+1 bits, where BW is the bit width of the addrec's
/// coefficients. The reason is that the coefficients of the quadratic
/// equation are BW+1 bits wide (to avoid truncation when converting from
/// the addrec to the equation).
static std::optional<APInt> TruncIfPossible(std::optional<APInt> X,
                                            unsigned BitWidth) {
  if (!X)
    return std::nullopt;
  unsigned W = X->getBitWidth();
  if (BitWidth > 1 && BitWidth < W && X->isIntN(BitWidth))
    return X->trunc(BitWidth);
  return X;
}

/// Let c(n) be the value of the quadratic chrec {L,+,M,+,N} after n
/// iterations. The values L, M, N are assumed to be signed, and they
/// should all have the same bit widths.
/// Find the least n >= 0 such that c(n) = 0 in the arithmetic modulo 2^BW,
/// where BW is the bit width of the addrec's coefficients.
/// If the calculated value is a BW-bit integer (for BW > 1), it will be
/// returned as such, otherwise the bit width of the returned value may
/// be greater than BW.
///
/// This function returns std::nullopt if
/// (a) the addrec coefficients are not constant, or
/// (b) SolveQuadraticEquationWrap was unable to find a solution. For cases
///     like x^2 = 5, no integer solutions exist, in other cases an integer
///     solution may exist, but SolveQuadraticEquationWrap may fail to find it.
static std::optional<APInt>
SolveQuadraticAddRecExact(const SCEVAddRecExpr *AddRec, ScalarEvolution &SE) {
  APInt A, B, C, M;
  unsigned BitWidth;
  auto T = GetQuadraticEquation(AddRec);
  if (!T)
    return std::nullopt;

  std::tie(A, B, C, M, BitWidth) = *T;
  LLVM_DEBUG(dbgs() << __func__ << ": solving for unsigned overflow\n");
  std::optional<APInt> X =
      APIntOps::SolveQuadraticEquationWrap(A, B, C, BitWidth + 1);
  if (!X)
    return std::nullopt;

  ConstantInt *CX = ConstantInt::get(SE.getContext(), *X);
  ConstantInt *V = EvaluateConstantChrecAtConstant(AddRec, CX, SE);
  if (!V->isZero())
    return std::nullopt;

  return TruncIfPossible(X, BitWidth);
}

/// Let c(n) be the value of the quadratic chrec {0,+,M,+,N} after n
/// iterations. The values M, N are assumed to be signed, and they
/// should all have the same bit widths.
/// Find the least n such that c(n) does not belong to the given range,
/// while c(n-1) does.
///
/// This function returns std::nullopt if
/// (a) the addrec coefficients are not constant, or
/// (b) SolveQuadraticEquationWrap was unable to find a solution for the
///     bounds of the range.
static std::optional<APInt>
SolveQuadraticAddRecRange(const SCEVAddRecExpr *AddRec,
                          const ConstantRange &Range, ScalarEvolution &SE) {
  assert(AddRec->getOperand(0)->isZero() &&
         "Starting value of addrec should be 0");
  LLVM_DEBUG(dbgs() << __func__ << ": solving boundary crossing for range "
                    << Range << ", addrec " << *AddRec << '\n');
  // This case is handled in getNumIterationsInRange. Here we can assume that
  // we start in the range.
  assert(Range.contains(APInt(SE.getTypeSizeInBits(AddRec->getType()), 0)) &&
         "Addrec's initial value should be in range");

  APInt A, B, C, M;
  unsigned BitWidth;
  auto T = GetQuadraticEquation(AddRec);
  if (!T)
    return std::nullopt;

  // Be careful about the return value: there can be two reasons for not
  // returning an actual number. First, if no solutions to the equations
  // were found, and second, if the solutions don't leave the given range.
  // The first case means that the actual solution is "unknown", the second
  // means that it's known, but not valid. If the solution is unknown, we
  // cannot make any conclusions.
  // Return a pair: the optional solution and a flag indicating if the
  // solution was found.
  auto SolveForBoundary =
      [&](APInt Bound) -> std::pair<std::optional<APInt>, bool> {
    // Solve for signed overflow and unsigned overflow, pick the lower
    // solution.
    LLVM_DEBUG(dbgs() << "SolveQuadraticAddRecRange: checking boundary "
                      << Bound << " (before multiplying by " << M << ")\n");
    Bound *= M; // The quadratic equation multiplier.

    std::optional<APInt> SO;
    if (BitWidth > 1) {
      LLVM_DEBUG(dbgs() << "SolveQuadraticAddRecRange: solving for "
                           "signed overflow\n");
      SO = APIntOps::SolveQuadraticEquationWrap(A, B, -Bound, BitWidth);
    }
    LLVM_DEBUG(dbgs() << "SolveQuadraticAddRecRange: solving for "
                         "unsigned overflow\n");
    std::optional<APInt> UO =
        APIntOps::SolveQuadraticEquationWrap(A, B, -Bound, BitWidth + 1);

    auto LeavesRange = [&] (const APInt &X) {
      ConstantInt *C0 = ConstantInt::get(SE.getContext(), X);
      ConstantInt *V0 = EvaluateConstantChrecAtConstant(AddRec, C0, SE);
      if (Range.contains(V0->getValue()))
        return false;
      // X should be at least 1, so X-1 is non-negative.
      ConstantInt *C1 = ConstantInt::get(SE.getContext(), X-1);
      ConstantInt *V1 = EvaluateConstantChrecAtConstant(AddRec, C1, SE);
      if (Range.contains(V1->getValue()))
        return true;
      return false;
    };

    // If SolveQuadraticEquationWrap returns std::nullopt, it means that there
    // can be a solution, but the function failed to find it. We cannot treat it
    // as "no solution".
    if (!SO || !UO)
      return {std::nullopt, false};

    // Check the smaller value first to see if it leaves the range.
    // At this point, both SO and UO must have values.
    std::optional<APInt> Min = MinOptional(SO, UO);
    if (LeavesRange(*Min))
      return { Min, true };
    std::optional<APInt> Max = Min == SO ? UO : SO;
    if (LeavesRange(*Max))
      return { Max, true };

    // Solutions were found, but were eliminated, hence the "true".
    return {std::nullopt, true};
  };

  std::tie(A, B, C, M, BitWidth) = *T;
  // Lower bound is inclusive, subtract 1 to represent the exiting value.
  APInt Lower = Range.getLower().sext(A.getBitWidth()) - 1;
  APInt Upper = Range.getUpper().sext(A.getBitWidth());
  auto SL = SolveForBoundary(Lower);
  auto SU = SolveForBoundary(Upper);
  // If any of the solutions was unknown, no meaninigful conclusions can
  // be made.
  if (!SL.second || !SU.second)
    return std::nullopt;

  // Claim: The correct solution is not some value between Min and Max.
  //
  // Justification: Assuming that Min and Max are different values, one of
  // them is when the first signed overflow happens, the other is when the
  // first unsigned overflow happens. Crossing the range boundary is only
  // possible via an overflow (treating 0 as a special case of it, modeling
  // an overflow as crossing k*2^W for some k).
  //
  // The interesting case here is when Min was eliminated as an invalid
  // solution, but Max was not. The argument is that if there was another
  // overflow between Min and Max, it would also have been eliminated if
  // it was considered.
  //
  // For a given boundary, it is possible to have two overflows of the same
  // type (signed/unsigned) without having the other type in between: this
  // can happen when the vertex of the parabola is between the iterations
  // corresponding to the overflows. This is only possible when the two
  // overflows cross k*2^W for the same k. In such case, if the second one
  // left the range (and was the first one to do so), the first overflow
  // would have to enter the range, which would mean that either we had left
  // the range before or that we started outside of it. Both of these cases
  // are contradictions.
  //
  // Claim: In the case where SolveForBoundary returns std::nullopt, the correct
  // solution is not some value between the Max for this boundary and the
  // Min of the other boundary.
  //
  // Justification: Assume that we had such Max_A and Min_B corresponding
  // to range boundaries A and B and such that Max_A < Min_B. If there was
  // a solution between Max_A and Min_B, it would have to be caused by an
  // overflow corresponding to either A or B. It cannot correspond to B,
  // since Min_B is the first occurrence of such an overflow. If it
  // corresponded to A, it would have to be either a signed or an unsigned
  // overflow that is larger than both eliminated overflows for A. But
  // between the eliminated overflows and this overflow, the values would
  // cover the entire value space, thus crossing the other boundary, which
  // is a contradiction.

  return TruncIfPossible(MinOptional(SL.first, SU.first), BitWidth);
}

ScalarEvolution::ExitLimit ScalarEvolution::howFarToZero(const SCEV *V,
                                                         const Loop *L,
                                                         bool ControlsOnlyExit,
                                                         bool AllowPredicates) {

  // This is only used for loops with a "x != y" exit test. The exit condition
  // is now expressed as a single expression, V = x-y. So the exit test is
  // effectively V != 0.  We know and take advantage of the fact that this
  // expression only being used in a comparison by zero context.

  SmallPtrSet<const SCEVPredicate *, 4> Predicates;
  // If the value is a constant
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(V)) {
    // If the value is already zero, the branch will execute zero times.
    if (C->getValue()->isZero()) return C;
    return getCouldNotCompute();  // Otherwise it will loop infinitely.
  }

  const SCEVAddRecExpr *AddRec =
      dyn_cast<SCEVAddRecExpr>(stripInjectiveFunctions(V));

  if (!AddRec && AllowPredicates)
    // Try to make this an AddRec using runtime tests, in the first X
    // iterations of this loop, where X is the SCEV expression found by the
    // algorithm below.
    AddRec = convertSCEVToAddRecWithPredicates(V, L, Predicates);

  if (!AddRec || AddRec->getLoop() != L)
    return getCouldNotCompute();

  // If this is a quadratic (3-term) AddRec {L,+,M,+,N}, find the roots of
  // the quadratic equation to solve it.
  if (AddRec->isQuadratic() && AddRec->getType()->isIntegerTy()) {
    // We can only use this value if the chrec ends up with an exact zero
    // value at this index.  When solving for "X*X != 5", for example, we
    // should not accept a root of 2.
    if (auto S = SolveQuadraticAddRecExact(AddRec, *this)) {
      const auto *R = cast<SCEVConstant>(getConstant(*S));
      return ExitLimit(R, R, R, false, Predicates);
    }
    return getCouldNotCompute();
  }

  // Otherwise we can only handle this if it is affine.
  if (!AddRec->isAffine())
    return getCouldNotCompute();

  // If this is an affine expression, the execution count of this branch is
  // the minimum unsigned root of the following equation:
  //
  //     Start + Step*N = 0 (mod 2^BW)
  //
  // equivalent to:
  //
  //             Step*N = -Start (mod 2^BW)
  //
  // where BW is the common bit width of Start and Step.

  // Get the initial value for the loop.
  const SCEV *Start = getSCEVAtScope(AddRec->getStart(), L->getParentLoop());
  const SCEV *Step = getSCEVAtScope(AddRec->getOperand(1), L->getParentLoop());
  const SCEVConstant *StepC = dyn_cast<SCEVConstant>(Step);

  if (!isLoopInvariant(Step, L))
    return getCouldNotCompute();

  LoopGuards Guards = LoopGuards::collect(L, *this);
  // Specialize step for this loop so we get context sensitive facts below.
  const SCEV *StepWLG = applyLoopGuards(Step, Guards);

  // For positive steps (counting up until unsigned overflow):
  //   N = -Start/Step (as unsigned)
  // For negative steps (counting down to zero):
  //   N = Start/-Step
  // First compute the unsigned distance from zero in the direction of Step.
  bool CountDown = isKnownNegative(StepWLG);
  if (!CountDown && !isKnownNonNegative(StepWLG))
    return getCouldNotCompute();

  const SCEV *Distance = CountDown ? Start : getNegativeSCEV(Start);
  // Handle unitary steps, which cannot wraparound.
  // 1*N = -Start; -1*N = Start (mod 2^BW), so:
  //   N = Distance (as unsigned)
  if (StepC &&
      (StepC->getValue()->isOne() || StepC->getValue()->isMinusOne())) {
    APInt MaxBECount = getUnsignedRangeMax(applyLoopGuards(Distance, Guards));
    MaxBECount = APIntOps::umin(MaxBECount, getUnsignedRangeMax(Distance));

    // When a loop like "for (int i = 0; i != n; ++i) { /* body */ }" is rotated,
    // we end up with a loop whose backedge-taken count is n - 1.  Detect this
    // case, and see if we can improve the bound.
    //
    // Explicitly handling this here is necessary because getUnsignedRange
    // isn't context-sensitive; it doesn't know that we only care about the
    // range inside the loop.
    const SCEV *Zero = getZero(Distance->getType());
    const SCEV *One = getOne(Distance->getType());
    const SCEV *DistancePlusOne = getAddExpr(Distance, One);
    if (isLoopEntryGuardedByCond(L, ICmpInst::ICMP_NE, DistancePlusOne, Zero)) {
      // If Distance + 1 doesn't overflow, we can compute the maximum distance
      // as "unsigned_max(Distance + 1) - 1".
      ConstantRange CR = getUnsignedRange(DistancePlusOne);
      MaxBECount = APIntOps::umin(MaxBECount, CR.getUnsignedMax() - 1);
    }
    return ExitLimit(Distance, getConstant(MaxBECount), Distance, false,
                     Predicates);
  }

  // If the condition controls loop exit (the loop exits only if the expression
  // is true) and the addition is no-wrap we can use unsigned divide to
  // compute the backedge count.  In this case, the step may not divide the
  // distance, but we don't care because if the condition is "missed" the loop
  // will have undefined behavior due to wrapping.
  if (ControlsOnlyExit && AddRec->hasNoSelfWrap() &&
      loopHasNoAbnormalExits(AddRec->getLoop())) {

    // If the stride is zero, the loop must be infinite.  In C++, most loops
    // are finite by assumption, in which case the step being zero implies
    // UB must execute if the loop is entered.
    if (!loopIsFiniteByAssumption(L) && !isKnownNonZero(StepWLG))
      return getCouldNotCompute();

    const SCEV *Exact =
        getUDivExpr(Distance, CountDown ? getNegativeSCEV(Step) : Step);
    const SCEV *ConstantMax = getCouldNotCompute();
    if (Exact != getCouldNotCompute()) {
      APInt MaxInt = getUnsignedRangeMax(applyLoopGuards(Exact, Guards));
      ConstantMax =
          getConstant(APIntOps::umin(MaxInt, getUnsignedRangeMax(Exact)));
    }
    const SCEV *SymbolicMax =
        isa<SCEVCouldNotCompute>(Exact) ? ConstantMax : Exact;
    return ExitLimit(Exact, ConstantMax, SymbolicMax, false, Predicates);
  }

  // Solve the general equation.
  if (!StepC || StepC->getValue()->isZero())
    return getCouldNotCompute();
  const SCEV *E = SolveLinEquationWithOverflow(StepC->getAPInt(),
                                               getNegativeSCEV(Start), *this);

  const SCEV *M = E;
  if (E != getCouldNotCompute()) {
    APInt MaxWithGuards = getUnsignedRangeMax(applyLoopGuards(E, Guards));
    M = getConstant(APIntOps::umin(MaxWithGuards, getUnsignedRangeMax(E)));
  }
  auto *S = isa<SCEVCouldNotCompute>(E) ? M : E;
  return ExitLimit(E, M, S, false, Predicates);
}

ScalarEvolution::ExitLimit
ScalarEvolution::howFarToNonZero(const SCEV *V, const Loop *L) {
  // Loops that look like: while (X == 0) are very strange indeed.  We don't
  // handle them yet except for the trivial case.  This could be expanded in the
  // future as needed.

  // If the value is a constant, check to see if it is known to be non-zero
  // already.  If so, the backedge will execute zero times.
  if (const SCEVConstant *C = dyn_cast<SCEVConstant>(V)) {
    if (!C->getValue()->isZero())
      return getZero(C->getType());
    return getCouldNotCompute();  // Otherwise it will loop infinitely.
  }

  // We could implement others, but I really doubt anyone writes loops like
  // this, and if they did, they would already be constant folded.
  return getCouldNotCompute();
}

std::pair<const BasicBlock *, const BasicBlock *>
ScalarEvolution::getPredecessorWithUniqueSuccessorForBB(const BasicBlock *BB)
    const {
  // If the block has a unique predecessor, then there is no path from the
  // predecessor to the block that does not go through the direct edge
  // from the predecessor to the block.
  if (const BasicBlock *Pred = BB->getSinglePredecessor())
    return {Pred, BB};

  // A loop's header is defined to be a block that dominates the loop.
  // If the header has a unique predecessor outside the loop, it must be
  // a block that has exactly one successor that can reach the loop.
  if (const Loop *L = LI.getLoopFor(BB))
    return {L->getLoopPredecessor(), L->getHeader()};

  return {nullptr, nullptr};
}

/// SCEV structural equivalence is usually sufficient for testing whether two
/// expressions are equal, however for the purposes of looking for a condition
/// guarding a loop, it can be useful to be a little more general, since a
/// front-end may have replicated the controlling expression.
static bool HasSameValue(const SCEV *A, const SCEV *B) {
  // Quick check to see if they are the same SCEV.
  if (A == B) return true;

  auto ComputesEqualValues = [](const Instruction *A, const Instruction *B) {
    // Not all instructions that are "identical" compute the same value.  For
    // instance, two distinct alloca instructions allocating the same type are
    // identical and do not read memory; but compute distinct values.
    return A->isIdenticalTo(B) && (isa<BinaryOperator>(A) || isa<GetElementPtrInst>(A));
  };

  // Otherwise, if they're both SCEVUnknown, it's possible that they hold
  // two different instructions with the same value. Check for this case.
  if (const SCEVUnknown *AU = dyn_cast<SCEVUnknown>(A))
    if (const SCEVUnknown *BU = dyn_cast<SCEVUnknown>(B))
      if (const Instruction *AI = dyn_cast<Instruction>(AU->getValue()))
        if (const Instruction *BI = dyn_cast<Instruction>(BU->getValue()))
          if (ComputesEqualValues(AI, BI))
            return true;

  // Otherwise assume they may have a different value.
  return false;
}

static bool MatchBinarySub(const SCEV *S, const SCEV *&LHS, const SCEV *&RHS) {
  const SCEVAddExpr *Add = dyn_cast<SCEVAddExpr>(S);
  if (!Add || Add->getNumOperands() != 2)
    return false;
  if (auto *ME = dyn_cast<SCEVMulExpr>(Add->getOperand(0));
      ME && ME->getNumOperands() == 2 && ME->getOperand(0)->isAllOnesValue()) {
    LHS = Add->getOperand(1);
    RHS = ME->getOperand(1);
    return true;
  }
  if (auto *ME = dyn_cast<SCEVMulExpr>(Add->getOperand(1));
      ME && ME->getNumOperands() == 2 && ME->getOperand(0)->isAllOnesValue()) {
    LHS = Add->getOperand(0);
    RHS = ME->getOperand(1);
    return true;
  }
  return false;
}

bool ScalarEvolution::SimplifyICmpOperands(ICmpInst::Predicate &Pred,
                                           const SCEV *&LHS, const SCEV *&RHS,
                                           unsigned Depth) {
  bool Changed = false;
  // Simplifies ICMP to trivial true or false by turning it into '0 == 0' or
  // '0 != 0'.
  auto TrivialCase = [&](bool TriviallyTrue) {
    LHS = RHS = getConstant(ConstantInt::getFalse(getContext()));
    Pred = TriviallyTrue ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;
    return true;
  };
  // If we hit the max recursion limit bail out.
  if (Depth >= 3)
    return false;

  // Canonicalize a constant to the right side.
  if (const SCEVConstant *LHSC = dyn_cast<SCEVConstant>(LHS)) {
    // Check for both operands constant.
    if (const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS)) {
      if (!ICmpInst::compare(LHSC->getAPInt(), RHSC->getAPInt(), Pred))
        return TrivialCase(false);
      return TrivialCase(true);
    }
    // Otherwise swap the operands to put the constant on the right.
    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
    Changed = true;
  }

  // If we're comparing an addrec with a value which is loop-invariant in the
  // addrec's loop, put the addrec on the left. Also make a dominance check,
  // as both operands could be addrecs loop-invariant in each other's loop.
  if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(RHS)) {
    const Loop *L = AR->getLoop();
    if (isLoopInvariant(LHS, L) && properlyDominates(LHS, L->getHeader())) {
      std::swap(LHS, RHS);
      Pred = ICmpInst::getSwappedPredicate(Pred);
      Changed = true;
    }
  }

  // If there's a constant operand, canonicalize comparisons with boundary
  // cases, and canonicalize *-or-equal comparisons to regular comparisons.
  if (const SCEVConstant *RC = dyn_cast<SCEVConstant>(RHS)) {
    const APInt &RA = RC->getAPInt();

    bool SimplifiedByConstantRange = false;

    if (!ICmpInst::isEquality(Pred)) {
      ConstantRange ExactCR = ConstantRange::makeExactICmpRegion(Pred, RA);
      if (ExactCR.isFullSet())
        return TrivialCase(true);
      if (ExactCR.isEmptySet())
        return TrivialCase(false);

      APInt NewRHS;
      CmpInst::Predicate NewPred;
      if (ExactCR.getEquivalentICmp(NewPred, NewRHS) &&
          ICmpInst::isEquality(NewPred)) {
        // We were able to convert an inequality to an equality.
        Pred = NewPred;
        RHS = getConstant(NewRHS);
        Changed = SimplifiedByConstantRange = true;
      }
    }

    if (!SimplifiedByConstantRange) {
      switch (Pred) {
      default:
        break;
      case ICmpInst::ICMP_EQ:
      case ICmpInst::ICMP_NE:
        // Fold ((-1) * %a) + %b == 0 (equivalent to %b-%a == 0) into %a == %b.
        if (RA.isZero() && MatchBinarySub(LHS, LHS, RHS))
          Changed = true;
        break;

        // The "Should have been caught earlier!" messages refer to the fact
        // that the ExactCR.isFullSet() or ExactCR.isEmptySet() check above
        // should have fired on the corresponding cases, and canonicalized the
        // check to trivial case.

      case ICmpInst::ICMP_UGE:
        assert(!RA.isMinValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_UGT;
        RHS = getConstant(RA - 1);
        Changed = true;
        break;
      case ICmpInst::ICMP_ULE:
        assert(!RA.isMaxValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_ULT;
        RHS = getConstant(RA + 1);
        Changed = true;
        break;
      case ICmpInst::ICMP_SGE:
        assert(!RA.isMinSignedValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_SGT;
        RHS = getConstant(RA - 1);
        Changed = true;
        break;
      case ICmpInst::ICMP_SLE:
        assert(!RA.isMaxSignedValue() && "Should have been caught earlier!");
        Pred = ICmpInst::ICMP_SLT;
        RHS = getConstant(RA + 1);
        Changed = true;
        break;
      }
    }
  }

  // Check for obvious equality.
  if (HasSameValue(LHS, RHS)) {
    if (ICmpInst::isTrueWhenEqual(Pred))
      return TrivialCase(true);
    if (ICmpInst::isFalseWhenEqual(Pred))
      return TrivialCase(false);
  }

  // If possible, canonicalize GE/LE comparisons to GT/LT comparisons, by
  // adding or subtracting 1 from one of the operands.
  switch (Pred) {
  case ICmpInst::ICMP_SLE:
    if (!getSignedRangeMax(RHS).isMaxSignedValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), 1, true), RHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SLT;
      Changed = true;
    } else if (!getSignedRangeMin(LHS).isMinSignedValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), LHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SLT;
      Changed = true;
    }
    break;
  case ICmpInst::ICMP_SGE:
    if (!getSignedRangeMin(RHS).isMinSignedValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), RHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SGT;
      Changed = true;
    } else if (!getSignedRangeMax(LHS).isMaxSignedValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), 1, true), LHS,
                       SCEV::FlagNSW);
      Pred = ICmpInst::ICMP_SGT;
      Changed = true;
    }
    break;
  case ICmpInst::ICMP_ULE:
    if (!getUnsignedRangeMax(RHS).isMaxValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), 1, true), RHS,
                       SCEV::FlagNUW);
      Pred = ICmpInst::ICMP_ULT;
      Changed = true;
    } else if (!getUnsignedRangeMin(LHS).isMinValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), LHS);
      Pred = ICmpInst::ICMP_ULT;
      Changed = true;
    }
    break;
  case ICmpInst::ICMP_UGE:
    if (!getUnsignedRangeMin(RHS).isMinValue()) {
      RHS = getAddExpr(getConstant(RHS->getType(), (uint64_t)-1, true), RHS);
      Pred = ICmpInst::ICMP_UGT;
      Changed = true;
    } else if (!getUnsignedRangeMax(LHS).isMaxValue()) {
      LHS = getAddExpr(getConstant(RHS->getType(), 1, true), LHS,
                       SCEV::FlagNUW);
      Pred = ICmpInst::ICMP_UGT;
      Changed = true;
    }
    break;
  default:
    break;
  }

  // TODO: More simplifications are possible here.

  // Recursively simplify until we either hit a recursion limit or nothing
  // changes.
  if (Changed)
    return SimplifyICmpOperands(Pred, LHS, RHS, Depth + 1);

  return Changed;
}

bool ScalarEvolution::isKnownNegative(const SCEV *S) {
  return getSignedRangeMax(S).isNegative();
}

bool ScalarEvolution::isKnownPositive(const SCEV *S) {
  return getSignedRangeMin(S).isStrictlyPositive();
}

bool ScalarEvolution::isKnownNonNegative(const SCEV *S) {
  return !getSignedRangeMin(S).isNegative();
}

bool ScalarEvolution::isKnownNonPositive(const SCEV *S) {
  return !getSignedRangeMax(S).isStrictlyPositive();
}

bool ScalarEvolution::isKnownNonZero(const SCEV *S) {
  // Query push down for cases where the unsigned range is
  // less than sufficient.
  if (const auto *SExt = dyn_cast<SCEVSignExtendExpr>(S))
    return isKnownNonZero(SExt->getOperand(0));
  return getUnsignedRangeMin(S) != 0;
}

std::pair<const SCEV *, const SCEV *>
ScalarEvolution::SplitIntoInitAndPostInc(const Loop *L, const SCEV *S) {
  // Compute SCEV on entry of loop L.
  const SCEV *Start = SCEVInitRewriter::rewrite(S, L, *this);
  if (Start == getCouldNotCompute())
    return { Start, Start };
  // Compute post increment SCEV for loop L.
  const SCEV *PostInc = SCEVPostIncRewriter::rewrite(S, L, *this);
  assert(PostInc != getCouldNotCompute() && "Unexpected could not compute");
  return { Start, PostInc };
}

bool ScalarEvolution::isKnownViaInduction(ICmpInst::Predicate Pred,
                                          const SCEV *LHS, const SCEV *RHS) {
  // First collect all loops.
  SmallPtrSet<const Loop *, 8> LoopsUsed;
  getUsedLoops(LHS, LoopsUsed);
  getUsedLoops(RHS, LoopsUsed);

  if (LoopsUsed.empty())
    return false;

  // Domination relationship must be a linear order on collected loops.
#ifndef NDEBUG
  for (const auto *L1 : LoopsUsed)
    for (const auto *L2 : LoopsUsed)
      assert((DT.dominates(L1->getHeader(), L2->getHeader()) ||
              DT.dominates(L2->getHeader(), L1->getHeader())) &&
             "Domination relationship is not a linear order");
#endif

  const Loop *MDL =
      *llvm::max_element(LoopsUsed, [&](const Loop *L1, const Loop *L2) {
        return DT.properlyDominates(L1->getHeader(), L2->getHeader());
      });

  // Get init and post increment value for LHS.
  auto SplitLHS = SplitIntoInitAndPostInc(MDL, LHS);
  // if LHS contains unknown non-invariant SCEV then bail out.
  if (SplitLHS.first == getCouldNotCompute())
    return false;
  assert (SplitLHS.second != getCouldNotCompute() && "Unexpected CNC");
  // Get init and post increment value for RHS.
  auto SplitRHS = SplitIntoInitAndPostInc(MDL, RHS);
  // if RHS contains unknown non-invariant SCEV then bail out.
  if (SplitRHS.first == getCouldNotCompute())
    return false;
  assert (SplitRHS.second != getCouldNotCompute() && "Unexpected CNC");
  // It is possible that init SCEV contains an invariant load but it does
  // not dominate MDL and is not available at MDL loop entry, so we should
  // check it here.
  if (!isAvailableAtLoopEntry(SplitLHS.first, MDL) ||
      !isAvailableAtLoopEntry(SplitRHS.first, MDL))
    return false;

  // It seems backedge guard check is faster than entry one so in some cases
  // it can speed up whole estimation by short circuit
  return isLoopBackedgeGuardedByCond(MDL, Pred, SplitLHS.second,
                                     SplitRHS.second) &&
         isLoopEntryGuardedByCond(MDL, Pred, SplitLHS.first, SplitRHS.first);
}

bool ScalarEvolution::isKnownPredicate(ICmpInst::Predicate Pred,
                                       const SCEV *LHS, const SCEV *RHS) {
  // Canonicalize the inputs first.
  (void)SimplifyICmpOperands(Pred, LHS, RHS);

  if (isKnownViaInduction(Pred, LHS, RHS))
    return true;

  if (isKnownPredicateViaSplitting(Pred, LHS, RHS))
    return true;

  // Otherwise see what can be done with some simple reasoning.
  return isKnownViaNonRecursiveReasoning(Pred, LHS, RHS);
}

std::optional<bool> ScalarEvolution::evaluatePredicate(ICmpInst::Predicate Pred,
                                                       const SCEV *LHS,
                                                       const SCEV *RHS) {
  if (isKnownPredicate(Pred, LHS, RHS))
    return true;
  if (isKnownPredicate(ICmpInst::getInversePredicate(Pred), LHS, RHS))
    return false;
  return std::nullopt;
}

bool ScalarEvolution::isKnownPredicateAt(ICmpInst::Predicate Pred,
                                         const SCEV *LHS, const SCEV *RHS,
                                         const Instruction *CtxI) {
  // TODO: Analyze guards and assumes from Context's block.
  return isKnownPredicate(Pred, LHS, RHS) ||
         isBasicBlockEntryGuardedByCond(CtxI->getParent(), Pred, LHS, RHS);
}

std::optional<bool>
ScalarEvolution::evaluatePredicateAt(ICmpInst::Predicate Pred, const SCEV *LHS,
                                     const SCEV *RHS, const Instruction *CtxI) {
  std::optional<bool> KnownWithoutContext = evaluatePredicate(Pred, LHS, RHS);
  if (KnownWithoutContext)
    return KnownWithoutContext;

  if (isBasicBlockEntryGuardedByCond(CtxI->getParent(), Pred, LHS, RHS))
    return true;
  if (isBasicBlockEntryGuardedByCond(CtxI->getParent(),
                                          ICmpInst::getInversePredicate(Pred),
                                          LHS, RHS))
    return false;
  return std::nullopt;
}

bool ScalarEvolution::isKnownOnEveryIteration(ICmpInst::Predicate Pred,
                                              const SCEVAddRecExpr *LHS,
                                              const SCEV *RHS) {
  const Loop *L = LHS->getLoop();
  return isLoopEntryGuardedByCond(L, Pred, LHS->getStart(), RHS) &&
         isLoopBackedgeGuardedByCond(L, Pred, LHS->getPostIncExpr(*this), RHS);
}

std::optional<ScalarEvolution::MonotonicPredicateType>
ScalarEvolution::getMonotonicPredicateType(const SCEVAddRecExpr *LHS,
                                           ICmpInst::Predicate Pred) {
  auto Result = getMonotonicPredicateTypeImpl(LHS, Pred);

#ifndef NDEBUG
  // Verify an invariant: inverting the predicate should turn a monotonically
  // increasing change to a monotonically decreasing one, and vice versa.
  if (Result) {
    auto ResultSwapped =
        getMonotonicPredicateTypeImpl(LHS, ICmpInst::getSwappedPredicate(Pred));

    assert(*ResultSwapped != *Result &&
           "monotonicity should flip as we flip the predicate");
  }
#endif

  return Result;
}

std::optional<ScalarEvolution::MonotonicPredicateType>
ScalarEvolution::getMonotonicPredicateTypeImpl(const SCEVAddRecExpr *LHS,
                                               ICmpInst::Predicate Pred) {
  // A zero step value for LHS means the induction variable is essentially a
  // loop invariant value. We don't really depend on the predicate actually
  // flipping from false to true (for increasing predicates, and the other way
  // around for decreasing predicates), all we care about is that *if* the
  // predicate changes then it only changes from false to true.
  //
  // A zero step value in itself is not very useful, but there may be places
  // where SCEV can prove X >= 0 but not prove X > 0, so it is helpful to be
  // as general as possible.

  // Only handle LE/LT/GE/GT predicates.
  if (!ICmpInst::isRelational(Pred))
    return std::nullopt;

  bool IsGreater = ICmpInst::isGE(Pred) || ICmpInst::isGT(Pred);
  assert((IsGreater || ICmpInst::isLE(Pred) || ICmpInst::isLT(Pred)) &&
         "Should be greater or less!");

  // Check that AR does not wrap.
  if (ICmpInst::isUnsigned(Pred)) {
    if (!LHS->hasNoUnsignedWrap())
      return std::nullopt;
    return IsGreater ? MonotonicallyIncreasing : MonotonicallyDecreasing;
  }
  assert(ICmpInst::isSigned(Pred) &&
         "Relational predicate is either signed or unsigned!");
  if (!LHS->hasNoSignedWrap())
    return std::nullopt;

  const SCEV *Step = LHS->getStepRecurrence(*this);

  if (isKnownNonNegative(Step))
    return IsGreater ? MonotonicallyIncreasing : MonotonicallyDecreasing;

  if (isKnownNonPositive(Step))
    return !IsGreater ? MonotonicallyIncreasing : MonotonicallyDecreasing;

  return std::nullopt;
}

std::optional<ScalarEvolution::LoopInvariantPredicate>
ScalarEvolution::getLoopInvariantPredicate(ICmpInst::Predicate Pred,
                                           const SCEV *LHS, const SCEV *RHS,
                                           const Loop *L,
                                           const Instruction *CtxI) {
  // If there is a loop-invariant, force it into the RHS, otherwise bail out.
  if (!isLoopInvariant(RHS, L)) {
    if (!isLoopInvariant(LHS, L))
      return std::nullopt;

    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  const SCEVAddRecExpr *ArLHS = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!ArLHS || ArLHS->getLoop() != L)
    return std::nullopt;

  auto MonotonicType = getMonotonicPredicateType(ArLHS, Pred);
  if (!MonotonicType)
    return std::nullopt;
  // If the predicate "ArLHS `Pred` RHS" monotonically increases from false to
  // true as the loop iterates, and the backedge is control dependent on
  // "ArLHS `Pred` RHS" == true then we can reason as follows:
  //
  //   * if the predicate was false in the first iteration then the predicate
  //     is never evaluated again, since the loop exits without taking the
  //     backedge.
  //   * if the predicate was true in the first iteration then it will
  //     continue to be true for all future iterations since it is
  //     monotonically increasing.
  //
  // For both the above possibilities, we can replace the loop varying
  // predicate with its value on the first iteration of the loop (which is
  // loop invariant).
  //
  // A similar reasoning applies for a monotonically decreasing predicate, by
  // replacing true with false and false with true in the above two bullets.
  bool Increasing = *MonotonicType == ScalarEvolution::MonotonicallyIncreasing;
  auto P = Increasing ? Pred : ICmpInst::getInversePredicate(Pred);

  if (isLoopBackedgeGuardedByCond(L, P, LHS, RHS))
    return ScalarEvolution::LoopInvariantPredicate(Pred, ArLHS->getStart(),
                                                   RHS);

  if (!CtxI)
    return std::nullopt;
  // Try to prove via context.
  // TODO: Support other cases.
  switch (Pred) {
  default:
    break;
  case ICmpInst::ICMP_ULE:
  case ICmpInst::ICMP_ULT: {
    assert(ArLHS->hasNoUnsignedWrap() && "Is a requirement of monotonicity!");
    // Given preconditions
    // (1) ArLHS does not cross the border of positive and negative parts of
    //     range because of:
    //     - Positive step; (TODO: lift this limitation)
    //     - nuw - does not cross zero boundary;
    //     - nsw - does not cross SINT_MAX boundary;
    // (2) ArLHS <s RHS
    // (3) RHS >=s 0
    // we can replace the loop variant ArLHS <u RHS condition with loop
    // invariant Start(ArLHS) <u RHS.
    //
    // Because of (1) there are two options:
    // - ArLHS is always negative. It means that ArLHS <u RHS is always false;
    // - ArLHS is always non-negative. Because of (3) RHS is also non-negative.
    //   It means that ArLHS <s RHS <=> ArLHS <u RHS.
    //   Because of (2) ArLHS <u RHS is trivially true.
    // All together it means that ArLHS <u RHS <=> Start(ArLHS) >=s 0.
    // We can strengthen this to Start(ArLHS) <u RHS.
    auto SignFlippedPred = ICmpInst::getFlippedSignednessPredicate(Pred);
    if (ArLHS->hasNoSignedWrap() && ArLHS->isAffine() &&
        isKnownPositive(ArLHS->getStepRecurrence(*this)) &&
        isKnownNonNegative(RHS) &&
        isKnownPredicateAt(SignFlippedPred, ArLHS, RHS, CtxI))
      return ScalarEvolution::LoopInvariantPredicate(Pred, ArLHS->getStart(),
                                                     RHS);
  }
  }

  return std::nullopt;
}

std::optional<ScalarEvolution::LoopInvariantPredicate>
ScalarEvolution::getLoopInvariantExitCondDuringFirstIterations(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS, const Loop *L,
    const Instruction *CtxI, const SCEV *MaxIter) {
  if (auto LIP = getLoopInvariantExitCondDuringFirstIterationsImpl(
          Pred, LHS, RHS, L, CtxI, MaxIter))
    return LIP;
  if (auto *UMin = dyn_cast<SCEVUMinExpr>(MaxIter))
    // Number of iterations expressed as UMIN isn't always great for expressing
    // the value on the last iteration. If the straightforward approach didn't
    // work, try the following trick: if the a predicate is invariant for X, it
    // is also invariant for umin(X, ...). So try to find something that works
    // among subexpressions of MaxIter expressed as umin.
    for (auto *Op : UMin->operands())
      if (auto LIP = getLoopInvariantExitCondDuringFirstIterationsImpl(
              Pred, LHS, RHS, L, CtxI, Op))
        return LIP;
  return std::nullopt;
}

std::optional<ScalarEvolution::LoopInvariantPredicate>
ScalarEvolution::getLoopInvariantExitCondDuringFirstIterationsImpl(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS, const Loop *L,
    const Instruction *CtxI, const SCEV *MaxIter) {
  // Try to prove the following set of facts:
  // - The predicate is monotonic in the iteration space.
  // - If the check does not fail on the 1st iteration:
  //   - No overflow will happen during first MaxIter iterations;
  //   - It will not fail on the MaxIter'th iteration.
  // If the check does fail on the 1st iteration, we leave the loop and no
  // other checks matter.

  // If there is a loop-invariant, force it into the RHS, otherwise bail out.
  if (!isLoopInvariant(RHS, L)) {
    if (!isLoopInvariant(LHS, L))
      return std::nullopt;

    std::swap(LHS, RHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  auto *AR = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!AR || AR->getLoop() != L)
    return std::nullopt;

  // The predicate must be relational (i.e. <, <=, >=, >).
  if (!ICmpInst::isRelational(Pred))
    return std::nullopt;

  // TODO: Support steps other than +/- 1.
  const SCEV *Step = AR->getStepRecurrence(*this);
  auto *One = getOne(Step->getType());
  auto *MinusOne = getNegativeSCEV(One);
  if (Step != One && Step != MinusOne)
    return std::nullopt;

  // Type mismatch here means that MaxIter is potentially larger than max
  // unsigned value in start type, which mean we cannot prove no wrap for the
  // indvar.
  if (AR->getType() != MaxIter->getType())
    return std::nullopt;

  // Value of IV on suggested last iteration.
  const SCEV *Last = AR->evaluateAtIteration(MaxIter, *this);
  // Does it still meet the requirement?
  if (!isLoopBackedgeGuardedByCond(L, Pred, Last, RHS))
    return std::nullopt;
  // Because step is +/- 1 and MaxIter has same type as Start (i.e. it does
  // not exceed max unsigned value of this type), this effectively proves
  // that there is no wrap during the iteration. To prove that there is no
  // signed/unsigned wrap, we need to check that
  // Start <= Last for step = 1 or Start >= Last for step = -1.
  ICmpInst::Predicate NoOverflowPred =
      CmpInst::isSigned(Pred) ? ICmpInst::ICMP_SLE : ICmpInst::ICMP_ULE;
  if (Step == MinusOne)
    NoOverflowPred = CmpInst::getSwappedPredicate(NoOverflowPred);
  const SCEV *Start = AR->getStart();
  if (!isKnownPredicateAt(NoOverflowPred, Start, Last, CtxI))
    return std::nullopt;

  // Everything is fine.
  return ScalarEvolution::LoopInvariantPredicate(Pred, Start, RHS);
}

bool ScalarEvolution::isKnownPredicateViaConstantRanges(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS) {
  if (HasSameValue(LHS, RHS))
    return ICmpInst::isTrueWhenEqual(Pred);

  // This code is split out from isKnownPredicate because it is called from
  // within isLoopEntryGuardedByCond.

  auto CheckRanges = [&](const ConstantRange &RangeLHS,
                         const ConstantRange &RangeRHS) {
    return RangeLHS.icmp(Pred, RangeRHS);
  };

  // The check at the top of the function catches the case where the values are
  // known to be equal.
  if (Pred == CmpInst::ICMP_EQ)
    return false;

  if (Pred == CmpInst::ICMP_NE) {
    auto SL = getSignedRange(LHS);
    auto SR = getSignedRange(RHS);
    if (CheckRanges(SL, SR))
      return true;
    auto UL = getUnsignedRange(LHS);
    auto UR = getUnsignedRange(RHS);
    if (CheckRanges(UL, UR))
      return true;
    auto *Diff = getMinusSCEV(LHS, RHS);
    return !isa<SCEVCouldNotCompute>(Diff) && isKnownNonZero(Diff);
  }

  if (CmpInst::isSigned(Pred)) {
    auto SL = getSignedRange(LHS);
    auto SR = getSignedRange(RHS);
    return CheckRanges(SL, SR);
  }

  auto UL = getUnsignedRange(LHS);
  auto UR = getUnsignedRange(RHS);
  return CheckRanges(UL, UR);
}

bool ScalarEvolution::isKnownPredicateViaNoOverflow(ICmpInst::Predicate Pred,
                                                    const SCEV *LHS,
                                                    const SCEV *RHS) {
  // Match X to (A + C1)<ExpectedFlags> and Y to (A + C2)<ExpectedFlags>, where
  // C1 and C2 are constant integers. If either X or Y are not add expressions,
  // consider them as X + 0 and Y + 0 respectively. C1 and C2 are returned via
  // OutC1 and OutC2.
  auto MatchBinaryAddToConst = [this](const SCEV *X, const SCEV *Y,
                                      APInt &OutC1, APInt &OutC2,
                                      SCEV::NoWrapFlags ExpectedFlags) {
    const SCEV *XNonConstOp, *XConstOp;
    const SCEV *YNonConstOp, *YConstOp;
    SCEV::NoWrapFlags XFlagsPresent;
    SCEV::NoWrapFlags YFlagsPresent;

    if (!splitBinaryAdd(X, XConstOp, XNonConstOp, XFlagsPresent)) {
      XConstOp = getZero(X->getType());
      XNonConstOp = X;
      XFlagsPresent = ExpectedFlags;
    }
    if (!isa<SCEVConstant>(XConstOp) ||
        (XFlagsPresent & ExpectedFlags) != ExpectedFlags)
      return false;

    if (!splitBinaryAdd(Y, YConstOp, YNonConstOp, YFlagsPresent)) {
      YConstOp = getZero(Y->getType());
      YNonConstOp = Y;
      YFlagsPresent = ExpectedFlags;
    }

    if (!isa<SCEVConstant>(YConstOp) ||
        (YFlagsPresent & ExpectedFlags) != ExpectedFlags)
      return false;

    if (YNonConstOp != XNonConstOp)
      return false;

    OutC1 = cast<SCEVConstant>(XConstOp)->getAPInt();
    OutC2 = cast<SCEVConstant>(YConstOp)->getAPInt();

    return true;
  };

  APInt C1;
  APInt C2;

  switch (Pred) {
  default:
    break;

  case ICmpInst::ICMP_SGE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_SLE:
    // (X + C1)<nsw> s<= (X + C2)<nsw> if C1 s<= C2.
    if (MatchBinaryAddToConst(LHS, RHS, C1, C2, SCEV::FlagNSW) && C1.sle(C2))
      return true;

    break;

  case ICmpInst::ICMP_SGT:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_SLT:
    // (X + C1)<nsw> s< (X + C2)<nsw> if C1 s< C2.
    if (MatchBinaryAddToConst(LHS, RHS, C1, C2, SCEV::FlagNSW) && C1.slt(C2))
      return true;

    break;

  case ICmpInst::ICMP_UGE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_ULE:
    // (X + C1)<nuw> u<= (X + C2)<nuw> for C1 u<= C2.
    if (MatchBinaryAddToConst(LHS, RHS, C1, C2, SCEV::FlagNUW) && C1.ule(C2))
      return true;

    break;

  case ICmpInst::ICMP_UGT:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_ULT:
    // (X + C1)<nuw> u< (X + C2)<nuw> if C1 u< C2.
    if (MatchBinaryAddToConst(LHS, RHS, C1, C2, SCEV::FlagNUW) && C1.ult(C2))
      return true;
    break;
  }

  return false;
}

bool ScalarEvolution::isKnownPredicateViaSplitting(ICmpInst::Predicate Pred,
                                                   const SCEV *LHS,
                                                   const SCEV *RHS) {
  if (Pred != ICmpInst::ICMP_ULT || ProvingSplitPredicate)
    return false;

  // Allowing arbitrary number of activations of isKnownPredicateViaSplitting on
  // the stack can result in exponential time complexity.
  SaveAndRestore Restore(ProvingSplitPredicate, true);

  // If L >= 0 then I `ult` L <=> I >= 0 && I `slt` L
  //
  // To prove L >= 0 we use isKnownNonNegative whereas to prove I >= 0 we use
  // isKnownPredicate.  isKnownPredicate is more powerful, but also more
  // expensive; and using isKnownNonNegative(RHS) is sufficient for most of the
  // interesting cases seen in practice.  We can consider "upgrading" L >= 0 to
  // use isKnownPredicate later if needed.
  return isKnownNonNegative(RHS) &&
         isKnownPredicate(CmpInst::ICMP_SGE, LHS, getZero(LHS->getType())) &&
         isKnownPredicate(CmpInst::ICMP_SLT, LHS, RHS);
}

bool ScalarEvolution::isImpliedViaGuard(const BasicBlock *BB,
                                        ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS) {
  // No need to even try if we know the module has no guards.
  if (!HasGuards)
    return false;

  return any_of(*BB, [&](const Instruction &I) {
    using namespace llvm::PatternMatch;

    Value *Condition;
    return match(&I, m_Intrinsic<Intrinsic::experimental_guard>(
                         m_Value(Condition))) &&
           isImpliedCond(Pred, LHS, RHS, Condition, false);
  });
}

/// isLoopBackedgeGuardedByCond - Test whether the backedge of the loop is
/// protected by a conditional between LHS and RHS.  This is used to
/// to eliminate casts.
bool
ScalarEvolution::isLoopBackedgeGuardedByCond(const Loop *L,
                                             ICmpInst::Predicate Pred,
                                             const SCEV *LHS, const SCEV *RHS) {
  // Interpret a null as meaning no loop, where there is obviously no guard
  // (interprocedural conditions notwithstanding). Do not bother about
  // unreachable loops.
  if (!L || !DT.isReachableFromEntry(L->getHeader()))
    return true;

  if (VerifyIR)
    assert(!verifyFunction(*L->getHeader()->getParent(), &dbgs()) &&
           "This cannot be done on broken IR!");


  if (isKnownViaNonRecursiveReasoning(Pred, LHS, RHS))
    return true;

  BasicBlock *Latch = L->getLoopLatch();
  if (!Latch)
    return false;

  BranchInst *LoopContinuePredicate =
    dyn_cast<BranchInst>(Latch->getTerminator());
  if (LoopContinuePredicate && LoopContinuePredicate->isConditional() &&
      isImpliedCond(Pred, LHS, RHS,
                    LoopContinuePredicate->getCondition(),
                    LoopContinuePredicate->getSuccessor(0) != L->getHeader()))
    return true;

  // We don't want more than one activation of the following loops on the stack
  // -- that can lead to O(n!) time complexity.
  if (WalkingBEDominatingConds)
    return false;

  SaveAndRestore ClearOnExit(WalkingBEDominatingConds, true);

  // See if we can exploit a trip count to prove the predicate.
  const auto &BETakenInfo = getBackedgeTakenInfo(L);
  const SCEV *LatchBECount = BETakenInfo.getExact(Latch, this);
  if (LatchBECount != getCouldNotCompute()) {
    // We know that Latch branches back to the loop header exactly
    // LatchBECount times.  This means the backdege condition at Latch is
    // equivalent to  "{0,+,1} u< LatchBECount".
    Type *Ty = LatchBECount->getType();
    auto NoWrapFlags = SCEV::NoWrapFlags(SCEV::FlagNUW | SCEV::FlagNW);
    const SCEV *LoopCounter =
      getAddRecExpr(getZero(Ty), getOne(Ty), L, NoWrapFlags);
    if (isImpliedCond(Pred, LHS, RHS, ICmpInst::ICMP_ULT, LoopCounter,
                      LatchBECount))
      return true;
  }

  // Check conditions due to any @llvm.assume intrinsics.
  for (auto &AssumeVH : AC.assumptions()) {
    if (!AssumeVH)
      continue;
    auto *CI = cast<CallInst>(AssumeVH);
    if (!DT.dominates(CI, Latch->getTerminator()))
      continue;

    if (isImpliedCond(Pred, LHS, RHS, CI->getArgOperand(0), false))
      return true;
  }

  if (isImpliedViaGuard(Latch, Pred, LHS, RHS))
    return true;

  for (DomTreeNode *DTN = DT[Latch], *HeaderDTN = DT[L->getHeader()];
       DTN != HeaderDTN; DTN = DTN->getIDom()) {
    assert(DTN && "should reach the loop header before reaching the root!");

    BasicBlock *BB = DTN->getBlock();
    if (isImpliedViaGuard(BB, Pred, LHS, RHS))
      return true;

    BasicBlock *PBB = BB->getSinglePredecessor();
    if (!PBB)
      continue;

    BranchInst *ContinuePredicate = dyn_cast<BranchInst>(PBB->getTerminator());
    if (!ContinuePredicate || !ContinuePredicate->isConditional())
      continue;

    Value *Condition = ContinuePredicate->getCondition();

    // If we have an edge `E` within the loop body that dominates the only
    // latch, the condition guarding `E` also guards the backedge.  This
    // reasoning works only for loops with a single latch.

    BasicBlockEdge DominatingEdge(PBB, BB);
    if (DominatingEdge.isSingleEdge()) {
      // We're constructively (and conservatively) enumerating edges within the
      // loop body that dominate the latch.  The dominator tree better agree
      // with us on this:
      assert(DT.dominates(DominatingEdge, Latch) && "should be!");

      if (isImpliedCond(Pred, LHS, RHS, Condition,
                        BB != ContinuePredicate->getSuccessor(0)))
        return true;
    }
  }

  return false;
}

bool ScalarEvolution::isBasicBlockEntryGuardedByCond(const BasicBlock *BB,
                                                     ICmpInst::Predicate Pred,
                                                     const SCEV *LHS,
                                                     const SCEV *RHS) {
  // Do not bother proving facts for unreachable code.
  if (!DT.isReachableFromEntry(BB))
    return true;
  if (VerifyIR)
    assert(!verifyFunction(*BB->getParent(), &dbgs()) &&
           "This cannot be done on broken IR!");

  // If we cannot prove strict comparison (e.g. a > b), maybe we can prove
  // the facts (a >= b && a != b) separately. A typical situation is when the
  // non-strict comparison is known from ranges and non-equality is known from
  // dominating predicates. If we are proving strict comparison, we always try
  // to prove non-equality and non-strict comparison separately.
  auto NonStrictPredicate = ICmpInst::getNonStrictPredicate(Pred);
  const bool ProvingStrictComparison = (Pred != NonStrictPredicate);
  bool ProvedNonStrictComparison = false;
  bool ProvedNonEquality = false;

  auto SplitAndProve =
    [&](std::function<bool(ICmpInst::Predicate)> Fn) -> bool {
    if (!ProvedNonStrictComparison)
      ProvedNonStrictComparison = Fn(NonStrictPredicate);
    if (!ProvedNonEquality)
      ProvedNonEquality = Fn(ICmpInst::ICMP_NE);
    if (ProvedNonStrictComparison && ProvedNonEquality)
      return true;
    return false;
  };

  if (ProvingStrictComparison) {
    auto ProofFn = [&](ICmpInst::Predicate P) {
      return isKnownViaNonRecursiveReasoning(P, LHS, RHS);
    };
    if (SplitAndProve(ProofFn))
      return true;
  }

  // Try to prove (Pred, LHS, RHS) using isImpliedCond.
  auto ProveViaCond = [&](const Value *Condition, bool Inverse) {
    const Instruction *CtxI = &BB->front();
    if (isImpliedCond(Pred, LHS, RHS, Condition, Inverse, CtxI))
      return true;
    if (ProvingStrictComparison) {
      auto ProofFn = [&](ICmpInst::Predicate P) {
        return isImpliedCond(P, LHS, RHS, Condition, Inverse, CtxI);
      };
      if (SplitAndProve(ProofFn))
        return true;
    }
    return false;
  };

  // Starting at the block's predecessor, climb up the predecessor chain, as long
  // as there are predecessors that can be found that have unique successors
  // leading to the original block.
  const Loop *ContainingLoop = LI.getLoopFor(BB);
  const BasicBlock *PredBB;
  if (ContainingLoop && ContainingLoop->getHeader() == BB)
    PredBB = ContainingLoop->getLoopPredecessor();
  else
    PredBB = BB->getSinglePredecessor();
  for (std::pair<const BasicBlock *, const BasicBlock *> Pair(PredBB, BB);
       Pair.first; Pair = getPredecessorWithUniqueSuccessorForBB(Pair.first)) {
    const BranchInst *BlockEntryPredicate =
        dyn_cast<BranchInst>(Pair.first->getTerminator());
    if (!BlockEntryPredicate || BlockEntryPredicate->isUnconditional())
      continue;

    if (ProveViaCond(BlockEntryPredicate->getCondition(),
                     BlockEntryPredicate->getSuccessor(0) != Pair.second))
      return true;
  }

  // Check conditions due to any @llvm.assume intrinsics.
  for (auto &AssumeVH : AC.assumptions()) {
    if (!AssumeVH)
      continue;
    auto *CI = cast<CallInst>(AssumeVH);
    if (!DT.dominates(CI, BB))
      continue;

    if (ProveViaCond(CI->getArgOperand(0), false))
      return true;
  }

  // Check conditions due to any @llvm.experimental.guard intrinsics.
  auto *GuardDecl = F.getParent()->getFunction(
      Intrinsic::getName(Intrinsic::experimental_guard));
  if (GuardDecl)
    for (const auto *GU : GuardDecl->users())
      if (const auto *Guard = dyn_cast<IntrinsicInst>(GU))
        if (Guard->getFunction() == BB->getParent() && DT.dominates(Guard, BB))
          if (ProveViaCond(Guard->getArgOperand(0), false))
            return true;
  return false;
}

bool ScalarEvolution::isLoopEntryGuardedByCond(const Loop *L,
                                               ICmpInst::Predicate Pred,
                                               const SCEV *LHS,
                                               const SCEV *RHS) {
  // Interpret a null as meaning no loop, where there is obviously no guard
  // (interprocedural conditions notwithstanding).
  if (!L)
    return false;

  // Both LHS and RHS must be available at loop entry.
  assert(isAvailableAtLoopEntry(LHS, L) &&
         "LHS is not available at Loop Entry");
  assert(isAvailableAtLoopEntry(RHS, L) &&
         "RHS is not available at Loop Entry");

  if (isKnownViaNonRecursiveReasoning(Pred, LHS, RHS))
    return true;

  return isBasicBlockEntryGuardedByCond(L->getHeader(), Pred, LHS, RHS);
}

bool ScalarEvolution::isImpliedCond(ICmpInst::Predicate Pred, const SCEV *LHS,
                                    const SCEV *RHS,
                                    const Value *FoundCondValue, bool Inverse,
                                    const Instruction *CtxI) {
  // False conditions implies anything. Do not bother analyzing it further.
  if (FoundCondValue ==
      ConstantInt::getBool(FoundCondValue->getContext(), Inverse))
    return true;

  if (!PendingLoopPredicates.insert(FoundCondValue).second)
    return false;

  auto ClearOnExit =
      make_scope_exit([&]() { PendingLoopPredicates.erase(FoundCondValue); });

  // Recursively handle And and Or conditions.
  const Value *Op0, *Op1;
  if (match(FoundCondValue, m_LogicalAnd(m_Value(Op0), m_Value(Op1)))) {
    if (!Inverse)
      return isImpliedCond(Pred, LHS, RHS, Op0, Inverse, CtxI) ||
             isImpliedCond(Pred, LHS, RHS, Op1, Inverse, CtxI);
  } else if (match(FoundCondValue, m_LogicalOr(m_Value(Op0), m_Value(Op1)))) {
    if (Inverse)
      return isImpliedCond(Pred, LHS, RHS, Op0, Inverse, CtxI) ||
             isImpliedCond(Pred, LHS, RHS, Op1, Inverse, CtxI);
  }

  const ICmpInst *ICI = dyn_cast<ICmpInst>(FoundCondValue);
  if (!ICI) return false;

  // Now that we found a conditional branch that dominates the loop or controls
  // the loop latch. Check to see if it is the comparison we are looking for.
  ICmpInst::Predicate FoundPred;
  if (Inverse)
    FoundPred = ICI->getInversePredicate();
  else
    FoundPred = ICI->getPredicate();

  const SCEV *FoundLHS = getSCEV(ICI->getOperand(0));
  const SCEV *FoundRHS = getSCEV(ICI->getOperand(1));

  return isImpliedCond(Pred, LHS, RHS, FoundPred, FoundLHS, FoundRHS, CtxI);
}

bool ScalarEvolution::isImpliedCond(ICmpInst::Predicate Pred, const SCEV *LHS,
                                    const SCEV *RHS,
                                    ICmpInst::Predicate FoundPred,
                                    const SCEV *FoundLHS, const SCEV *FoundRHS,
                                    const Instruction *CtxI) {
  // Balance the types.
  if (getTypeSizeInBits(LHS->getType()) <
      getTypeSizeInBits(FoundLHS->getType())) {
    // For unsigned and equality predicates, try to prove that both found
    // operands fit into narrow unsigned range. If so, try to prove facts in
    // narrow types.
    if (!CmpInst::isSigned(FoundPred) && !FoundLHS->getType()->isPointerTy() &&
        !FoundRHS->getType()->isPointerTy()) {
      auto *NarrowType = LHS->getType();
      auto *WideType = FoundLHS->getType();
      auto BitWidth = getTypeSizeInBits(NarrowType);
      const SCEV *MaxValue = getZeroExtendExpr(
          getConstant(APInt::getMaxValue(BitWidth)), WideType);
      if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_ULE, FoundLHS,
                                          MaxValue) &&
          isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_ULE, FoundRHS,
                                          MaxValue)) {
        const SCEV *TruncFoundLHS = getTruncateExpr(FoundLHS, NarrowType);
        const SCEV *TruncFoundRHS = getTruncateExpr(FoundRHS, NarrowType);
        if (isImpliedCondBalancedTypes(Pred, LHS, RHS, FoundPred, TruncFoundLHS,
                                       TruncFoundRHS, CtxI))
          return true;
      }
    }

    if (LHS->getType()->isPointerTy() || RHS->getType()->isPointerTy())
      return false;
    if (CmpInst::isSigned(Pred)) {
      LHS = getSignExtendExpr(LHS, FoundLHS->getType());
      RHS = getSignExtendExpr(RHS, FoundLHS->getType());
    } else {
      LHS = getZeroExtendExpr(LHS, FoundLHS->getType());
      RHS = getZeroExtendExpr(RHS, FoundLHS->getType());
    }
  } else if (getTypeSizeInBits(LHS->getType()) >
      getTypeSizeInBits(FoundLHS->getType())) {
    if (FoundLHS->getType()->isPointerTy() || FoundRHS->getType()->isPointerTy())
      return false;
    if (CmpInst::isSigned(FoundPred)) {
      FoundLHS = getSignExtendExpr(FoundLHS, LHS->getType());
      FoundRHS = getSignExtendExpr(FoundRHS, LHS->getType());
    } else {
      FoundLHS = getZeroExtendExpr(FoundLHS, LHS->getType());
      FoundRHS = getZeroExtendExpr(FoundRHS, LHS->getType());
    }
  }
  return isImpliedCondBalancedTypes(Pred, LHS, RHS, FoundPred, FoundLHS,
                                    FoundRHS, CtxI);
}

bool ScalarEvolution::isImpliedCondBalancedTypes(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
    ICmpInst::Predicate FoundPred, const SCEV *FoundLHS, const SCEV *FoundRHS,
    const Instruction *CtxI) {
  assert(getTypeSizeInBits(LHS->getType()) ==
             getTypeSizeInBits(FoundLHS->getType()) &&
         "Types should be balanced!");
  // Canonicalize the query to match the way instcombine will have
  // canonicalized the comparison.
  if (SimplifyICmpOperands(Pred, LHS, RHS))
    if (LHS == RHS)
      return CmpInst::isTrueWhenEqual(Pred);
  if (SimplifyICmpOperands(FoundPred, FoundLHS, FoundRHS))
    if (FoundLHS == FoundRHS)
      return CmpInst::isFalseWhenEqual(FoundPred);

  // Check to see if we can make the LHS or RHS match.
  if (LHS == FoundRHS || RHS == FoundLHS) {
    if (isa<SCEVConstant>(RHS)) {
      std::swap(FoundLHS, FoundRHS);
      FoundPred = ICmpInst::getSwappedPredicate(FoundPred);
    } else {
      std::swap(LHS, RHS);
      Pred = ICmpInst::getSwappedPredicate(Pred);
    }
  }

  // Check whether the found predicate is the same as the desired predicate.
  if (FoundPred == Pred)
    return isImpliedCondOperands(Pred, LHS, RHS, FoundLHS, FoundRHS, CtxI);

  // Check whether swapping the found predicate makes it the same as the
  // desired predicate.
  if (ICmpInst::getSwappedPredicate(FoundPred) == Pred) {
    // We can write the implication
    // 0.  LHS Pred      RHS  <-   FoundLHS SwapPred  FoundRHS
    // using one of the following ways:
    // 1.  LHS Pred      RHS  <-   FoundRHS Pred      FoundLHS
    // 2.  RHS SwapPred  LHS  <-   FoundLHS SwapPred  FoundRHS
    // 3.  LHS Pred      RHS  <-  ~FoundLHS Pred     ~FoundRHS
    // 4. ~LHS SwapPred ~RHS  <-   FoundLHS SwapPred  FoundRHS
    // Forms 1. and 2. require swapping the operands of one condition. Don't
    // do this if it would break canonical constant/addrec ordering.
    if (!isa<SCEVConstant>(RHS) && !isa<SCEVAddRecExpr>(LHS))
      return isImpliedCondOperands(FoundPred, RHS, LHS, FoundLHS, FoundRHS,
                                   CtxI);
    if (!isa<SCEVConstant>(FoundRHS) && !isa<SCEVAddRecExpr>(FoundLHS))
      return isImpliedCondOperands(Pred, LHS, RHS, FoundRHS, FoundLHS, CtxI);

    // There's no clear preference between forms 3. and 4., try both.  Avoid
    // forming getNotSCEV of pointer values as the resulting subtract is
    // not legal.
    if (!LHS->getType()->isPointerTy() && !RHS->getType()->isPointerTy() &&
        isImpliedCondOperands(FoundPred, getNotSCEV(LHS), getNotSCEV(RHS),
                              FoundLHS, FoundRHS, CtxI))
      return true;

    if (!FoundLHS->getType()->isPointerTy() &&
        !FoundRHS->getType()->isPointerTy() &&
        isImpliedCondOperands(Pred, LHS, RHS, getNotSCEV(FoundLHS),
                              getNotSCEV(FoundRHS), CtxI))
      return true;

    return false;
  }

  auto IsSignFlippedPredicate = [](CmpInst::Predicate P1,
                                   CmpInst::Predicate P2) {
    assert(P1 != P2 && "Handled earlier!");
    return CmpInst::isRelational(P2) &&
           P1 == CmpInst::getFlippedSignednessPredicate(P2);
  };
  if (IsSignFlippedPredicate(Pred, FoundPred)) {
    // Unsigned comparison is the same as signed comparison when both the
    // operands are non-negative or negative.
    if ((isKnownNonNegative(FoundLHS) && isKnownNonNegative(FoundRHS)) ||
        (isKnownNegative(FoundLHS) && isKnownNegative(FoundRHS)))
      return isImpliedCondOperands(Pred, LHS, RHS, FoundLHS, FoundRHS, CtxI);
    // Create local copies that we can freely swap and canonicalize our
    // conditions to "le/lt".
    ICmpInst::Predicate CanonicalPred = Pred, CanonicalFoundPred = FoundPred;
    const SCEV *CanonicalLHS = LHS, *CanonicalRHS = RHS,
               *CanonicalFoundLHS = FoundLHS, *CanonicalFoundRHS = FoundRHS;
    if (ICmpInst::isGT(CanonicalPred) || ICmpInst::isGE(CanonicalPred)) {
      CanonicalPred = ICmpInst::getSwappedPredicate(CanonicalPred);
      CanonicalFoundPred = ICmpInst::getSwappedPredicate(CanonicalFoundPred);
      std::swap(CanonicalLHS, CanonicalRHS);
      std::swap(CanonicalFoundLHS, CanonicalFoundRHS);
    }
    assert((ICmpInst::isLT(CanonicalPred) || ICmpInst::isLE(CanonicalPred)) &&
           "Must be!");
    assert((ICmpInst::isLT(CanonicalFoundPred) ||
            ICmpInst::isLE(CanonicalFoundPred)) &&
           "Must be!");
    if (ICmpInst::isSigned(CanonicalPred) && isKnownNonNegative(CanonicalRHS))
      // Use implication:
      // x <u y && y >=s 0 --> x <s y.
      // If we can prove the left part, the right part is also proven.
      return isImpliedCondOperands(CanonicalFoundPred, CanonicalLHS,
                                   CanonicalRHS, CanonicalFoundLHS,
                                   CanonicalFoundRHS);
    if (ICmpInst::isUnsigned(CanonicalPred) && isKnownNegative(CanonicalRHS))
      // Use implication:
      // x <s y && y <s 0 --> x <u y.
      // If we can prove the left part, the right part is also proven.
      return isImpliedCondOperands(CanonicalFoundPred, CanonicalLHS,
                                   CanonicalRHS, CanonicalFoundLHS,
                                   CanonicalFoundRHS);
  }

  // Check if we can make progress by sharpening ranges.
  if (FoundPred == ICmpInst::ICMP_NE &&
      (isa<SCEVConstant>(FoundLHS) || isa<SCEVConstant>(FoundRHS))) {

    const SCEVConstant *C = nullptr;
    const SCEV *V = nullptr;

    if (isa<SCEVConstant>(FoundLHS)) {
      C = cast<SCEVConstant>(FoundLHS);
      V = FoundRHS;
    } else {
      C = cast<SCEVConstant>(FoundRHS);
      V = FoundLHS;
    }

    // The guarding predicate tells us that C != V. If the known range
    // of V is [C, t), we can sharpen the range to [C + 1, t).  The
    // range we consider has to correspond to same signedness as the
    // predicate we're interested in folding.

    APInt Min = ICmpInst::isSigned(Pred) ?
        getSignedRangeMin(V) : getUnsignedRangeMin(V);

    if (Min == C->getAPInt()) {
      // Given (V >= Min && V != Min) we conclude V >= (Min + 1).
      // This is true even if (Min + 1) wraps around -- in case of
      // wraparound, (Min + 1) < Min, so (V >= Min => V >= (Min + 1)).

      APInt SharperMin = Min + 1;

      switch (Pred) {
        case ICmpInst::ICMP_SGE:
        case ICmpInst::ICMP_UGE:
          // We know V `Pred` SharperMin.  If this implies LHS `Pred`
          // RHS, we're done.
          if (isImpliedCondOperands(Pred, LHS, RHS, V, getConstant(SharperMin),
                                    CtxI))
            return true;
          [[fallthrough]];

        case ICmpInst::ICMP_SGT:
        case ICmpInst::ICMP_UGT:
          // We know from the range information that (V `Pred` Min ||
          // V == Min).  We know from the guarding condition that !(V
          // == Min).  This gives us
          //
          //       V `Pred` Min || V == Min && !(V == Min)
          //   =>  V `Pred` Min
          //
          // If V `Pred` Min implies LHS `Pred` RHS, we're done.

          if (isImpliedCondOperands(Pred, LHS, RHS, V, getConstant(Min), CtxI))
            return true;
          break;

        // `LHS < RHS` and `LHS <= RHS` are handled in the same way as `RHS > LHS` and `RHS >= LHS` respectively.
        case ICmpInst::ICMP_SLE:
        case ICmpInst::ICMP_ULE:
          if (isImpliedCondOperands(CmpInst::getSwappedPredicate(Pred), RHS,
                                    LHS, V, getConstant(SharperMin), CtxI))
            return true;
          [[fallthrough]];

        case ICmpInst::ICMP_SLT:
        case ICmpInst::ICMP_ULT:
          if (isImpliedCondOperands(CmpInst::getSwappedPredicate(Pred), RHS,
                                    LHS, V, getConstant(Min), CtxI))
            return true;
          break;

        default:
          // No change
          break;
      }
    }
  }

  // Check whether the actual condition is beyond sufficient.
  if (FoundPred == ICmpInst::ICMP_EQ)
    if (ICmpInst::isTrueWhenEqual(Pred))
      if (isImpliedCondOperands(Pred, LHS, RHS, FoundLHS, FoundRHS, CtxI))
        return true;
  if (Pred == ICmpInst::ICMP_NE)
    if (!ICmpInst::isTrueWhenEqual(FoundPred))
      if (isImpliedCondOperands(FoundPred, LHS, RHS, FoundLHS, FoundRHS, CtxI))
        return true;

  if (isImpliedCondOperandsViaRanges(Pred, LHS, RHS, FoundPred, FoundLHS, FoundRHS))
    return true;

  // Otherwise assume the worst.
  return false;
}

bool ScalarEvolution::splitBinaryAdd(const SCEV *Expr,
                                     const SCEV *&L, const SCEV *&R,
                                     SCEV::NoWrapFlags &Flags) {
  const auto *AE = dyn_cast<SCEVAddExpr>(Expr);
  if (!AE || AE->getNumOperands() != 2)
    return false;

  L = AE->getOperand(0);
  R = AE->getOperand(1);
  Flags = AE->getNoWrapFlags();
  return true;
}

std::optional<APInt>
ScalarEvolution::computeConstantDifference(const SCEV *More, const SCEV *Less) {
  // We avoid subtracting expressions here because this function is usually
  // fairly deep in the call stack (i.e. is called many times).

  // X - X = 0.
  if (More == Less)
    return APInt(getTypeSizeInBits(More->getType()), 0);

  if (isa<SCEVAddRecExpr>(Less) && isa<SCEVAddRecExpr>(More)) {
    const auto *LAR = cast<SCEVAddRecExpr>(Less);
    const auto *MAR = cast<SCEVAddRecExpr>(More);

    if (LAR->getLoop() != MAR->getLoop())
      return std::nullopt;

    // We look at affine expressions only; not for correctness but to keep
    // getStepRecurrence cheap.
    if (!LAR->isAffine() || !MAR->isAffine())
      return std::nullopt;

    if (LAR->getStepRecurrence(*this) != MAR->getStepRecurrence(*this))
      return std::nullopt;

    Less = LAR->getStart();
    More = MAR->getStart();

    // fall through
  }

  if (isa<SCEVConstant>(Less) && isa<SCEVConstant>(More)) {
    const auto &M = cast<SCEVConstant>(More)->getAPInt();
    const auto &L = cast<SCEVConstant>(Less)->getAPInt();
    return M - L;
  }

  SCEV::NoWrapFlags Flags;
  const SCEV *LLess = nullptr, *RLess = nullptr;
  const SCEV *LMore = nullptr, *RMore = nullptr;
  const SCEVConstant *C1 = nullptr, *C2 = nullptr;
  // Compare (X + C1) vs X.
  if (splitBinaryAdd(Less, LLess, RLess, Flags))
    if ((C1 = dyn_cast<SCEVConstant>(LLess)))
      if (RLess == More)
        return -(C1->getAPInt());

  // Compare X vs (X + C2).
  if (splitBinaryAdd(More, LMore, RMore, Flags))
    if ((C2 = dyn_cast<SCEVConstant>(LMore)))
      if (RMore == Less)
        return C2->getAPInt();

  // Compare (X + C1) vs (X + C2).
  if (C1 && C2 && RLess == RMore)
    return C2->getAPInt() - C1->getAPInt();

  return std::nullopt;
}

bool ScalarEvolution::isImpliedCondOperandsViaAddRecStart(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
    const SCEV *FoundLHS, const SCEV *FoundRHS, const Instruction *CtxI) {
  // Try to recognize the following pattern:
  //
  //   FoundRHS = ...
  // ...
  // loop:
  //   FoundLHS = {Start,+,W}
  // context_bb: // Basic block from the same loop
  //   known(Pred, FoundLHS, FoundRHS)
  //
  // If some predicate is known in the context of a loop, it is also known on
  // each iteration of this loop, including the first iteration. Therefore, in
  // this case, `FoundLHS Pred FoundRHS` implies `Start Pred FoundRHS`. Try to
  // prove the original pred using this fact.
  if (!CtxI)
    return false;
  const BasicBlock *ContextBB = CtxI->getParent();
  // Make sure AR varies in the context block.
  if (auto *AR = dyn_cast<SCEVAddRecExpr>(FoundLHS)) {
    const Loop *L = AR->getLoop();
    // Make sure that context belongs to the loop and executes on 1st iteration
    // (if it ever executes at all).
    if (!L->contains(ContextBB) || !DT.dominates(ContextBB, L->getLoopLatch()))
      return false;
    if (!isAvailableAtLoopEntry(FoundRHS, AR->getLoop()))
      return false;
    return isImpliedCondOperands(Pred, LHS, RHS, AR->getStart(), FoundRHS);
  }

  if (auto *AR = dyn_cast<SCEVAddRecExpr>(FoundRHS)) {
    const Loop *L = AR->getLoop();
    // Make sure that context belongs to the loop and executes on 1st iteration
    // (if it ever executes at all).
    if (!L->contains(ContextBB) || !DT.dominates(ContextBB, L->getLoopLatch()))
      return false;
    if (!isAvailableAtLoopEntry(FoundLHS, AR->getLoop()))
      return false;
    return isImpliedCondOperands(Pred, LHS, RHS, FoundLHS, AR->getStart());
  }

  return false;
}

bool ScalarEvolution::isImpliedCondOperandsViaNoOverflow(
    ICmpInst::Predicate Pred, const SCEV *LHS, const SCEV *RHS,
    const SCEV *FoundLHS, const SCEV *FoundRHS) {
  if (Pred != CmpInst::ICMP_SLT && Pred != CmpInst::ICMP_ULT)
    return false;

  const auto *AddRecLHS = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!AddRecLHS)
    return false;

  const auto *AddRecFoundLHS = dyn_cast<SCEVAddRecExpr>(FoundLHS);
  if (!AddRecFoundLHS)
    return false;

  // We'd like to let SCEV reason about control dependencies, so we constrain
  // both the inequalities to be about add recurrences on the same loop.  This
  // way we can use isLoopEntryGuardedByCond later.

  const Loop *L = AddRecFoundLHS->getLoop();
  if (L != AddRecLHS->getLoop())
    return false;

  //  FoundLHS u< FoundRHS u< -C =>  (FoundLHS + C) u< (FoundRHS + C) ... (1)
  //
  //  FoundLHS s< FoundRHS s< INT_MIN - C => (FoundLHS + C) s< (FoundRHS + C)
  //                                                                  ... (2)
  //
  // Informal proof for (2), assuming (1) [*]:
  //
  // We'll also assume (A s< B) <=> ((A + INT_MIN) u< (B + INT_MIN)) ... (3)[**]
  //
  // Then
  //
  //       FoundLHS s< FoundRHS s< INT_MIN - C
  // <=>  (FoundLHS + INT_MIN) u< (FoundRHS + INT_MIN) u< -C   [ using (3) ]
  // <=>  (FoundLHS + INT_MIN + C) u< (FoundRHS + INT_MIN + C) [ using (1) ]
  // <=>  (FoundLHS + INT_MIN + C + INT_MIN) s<
  //                        (FoundRHS + INT_MIN + C + INT_MIN) [ using (3) ]
  // <=>  FoundLHS + C s< FoundRHS + C
  //
  // [*]: (1) can be proved by ruling out overflow.
  //
  // [**]: This can be proved by analyzing all the four possibilities:
  //    (A s< 0, B s< 0), (A s< 0, B s>= 0), (A s>= 0, B s< 0) and
  //    (A s>= 0, B s>= 0).
  //
  // Note:
  // Despite (2), "FoundRHS s< INT_MIN - C" does not mean that "FoundRHS + C"
  // will not sign underflow.  For instance, say FoundLHS = (i8 -128), FoundRHS
  // = (i8 -127) and C = (i8 -100).  Then INT_MIN - C = (i8 -28), and FoundRHS
  // s< (INT_MIN - C).  Lack of sign overflow / underflow in "FoundRHS + C" is
  // neither necessary nor sufficient to prove "(FoundLHS + C) s< (FoundRHS +
  // C)".

  std::optional<APInt> LDiff = computeConstantDifference(LHS, FoundLHS);
  std::optional<APInt> RDiff = computeConstantDifference(RHS, FoundRHS);
  if (!LDiff || !RDiff || *LDiff != *RDiff)
    return false;

  if (LDiff->isMinValue())
    return true;

  APInt FoundRHSLimit;

  if (Pred == CmpInst::ICMP_ULT) {
    FoundRHSLimit = -(*RDiff);
  } else {
    assert(Pred == CmpInst::ICMP_SLT && "Checked above!");
    FoundRHSLimit = APInt::getSignedMinValue(getTypeSizeInBits(RHS->getType())) - *RDiff;
  }

  // Try to prove (1) or (2), as needed.
  return isAvailableAtLoopEntry(FoundRHS, L) &&
         isLoopEntryGuardedByCond(L, Pred, FoundRHS,
                                  getConstant(FoundRHSLimit));
}

bool ScalarEvolution::isImpliedViaMerge(ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS,
                                        const SCEV *FoundLHS,
                                        const SCEV *FoundRHS, unsigned Depth) {
  const PHINode *LPhi = nullptr, *RPhi = nullptr;

  auto ClearOnExit = make_scope_exit([&]() {
    if (LPhi) {
      bool Erased = PendingMerges.erase(LPhi);
      assert(Erased && "Failed to erase LPhi!");
      (void)Erased;
    }
    if (RPhi) {
      bool Erased = PendingMerges.erase(RPhi);
      assert(Erased && "Failed to erase RPhi!");
      (void)Erased;
    }
  });

  // Find respective Phis and check that they are not being pending.
  if (const SCEVUnknown *LU = dyn_cast<SCEVUnknown>(LHS))
    if (auto *Phi = dyn_cast<PHINode>(LU->getValue())) {
      if (!PendingMerges.insert(Phi).second)
        return false;
      LPhi = Phi;
    }
  if (const SCEVUnknown *RU = dyn_cast<SCEVUnknown>(RHS))
    if (auto *Phi = dyn_cast<PHINode>(RU->getValue())) {
      // If we detect a loop of Phi nodes being processed by this method, for
      // example:
      //
      //   %a = phi i32 [ %some1, %preheader ], [ %b, %latch ]
      //   %b = phi i32 [ %some2, %preheader ], [ %a, %latch ]
      //
      // we don't want to deal with a case that complex, so return conservative
      // answer false.
      if (!PendingMerges.insert(Phi).second)
        return false;
      RPhi = Phi;
    }

  // If none of LHS, RHS is a Phi, nothing to do here.
  if (!LPhi && !RPhi)
    return false;

  // If there is a SCEVUnknown Phi we are interested in, make it left.
  if (!LPhi) {
    std::swap(LHS, RHS);
    std::swap(FoundLHS, FoundRHS);
    std::swap(LPhi, RPhi);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }

  assert(LPhi && "LPhi should definitely be a SCEVUnknown Phi!");
  const BasicBlock *LBB = LPhi->getParent();
  const SCEVAddRecExpr *RAR = dyn_cast<SCEVAddRecExpr>(RHS);

  auto ProvedEasily = [&](const SCEV *S1, const SCEV *S2) {
    return isKnownViaNonRecursiveReasoning(Pred, S1, S2) ||
           isImpliedCondOperandsViaRanges(Pred, S1, S2, Pred, FoundLHS, FoundRHS) ||
           isImpliedViaOperations(Pred, S1, S2, FoundLHS, FoundRHS, Depth);
  };

  if (RPhi && RPhi->getParent() == LBB) {
    // Case one: RHS is also a SCEVUnknown Phi from the same basic block.
    // If we compare two Phis from the same block, and for each entry block
    // the predicate is true for incoming values from this block, then the
    // predicate is also true for the Phis.
    for (const BasicBlock *IncBB : predecessors(LBB)) {
      const SCEV *L = getSCEV(LPhi->getIncomingValueForBlock(IncBB));
      const SCEV *R = getSCEV(RPhi->getIncomingValueForBlock(IncBB));
      if (!ProvedEasily(L, R))
        return false;
    }
  } else if (RAR && RAR->getLoop()->getHeader() == LBB) {
    // Case two: RHS is also a Phi from the same basic block, and it is an
    // AddRec. It means that there is a loop which has both AddRec and Unknown
    // PHIs, for it we can compare incoming values of AddRec from above the loop
    // and latch with their respective incoming values of LPhi.
    // TODO: Generalize to handle loops with many inputs in a header.
    if (LPhi->getNumIncomingValues() != 2) return false;

    auto *RLoop = RAR->getLoop();
    auto *Predecessor = RLoop->getLoopPredecessor();
    assert(Predecessor && "Loop with AddRec with no predecessor?");
    const SCEV *L1 = getSCEV(LPhi->getIncomingValueForBlock(Predecessor));
    if (!ProvedEasily(L1, RAR->getStart()))
      return false;
    auto *Latch = RLoop->getLoopLatch();
    assert(Latch && "Loop with AddRec with no latch?");
    const SCEV *L2 = getSCEV(LPhi->getIncomingValueForBlock(Latch));
    if (!ProvedEasily(L2, RAR->getPostIncExpr(*this)))
      return false;
  } else {
    // In all other cases go over inputs of LHS and compare each of them to RHS,
    // the predicate is true for (LHS, RHS) if it is true for all such pairs.
    // At this point RHS is either a non-Phi, or it is a Phi from some block
    // different from LBB.
    for (const BasicBlock *IncBB : predecessors(LBB)) {
      // Check that RHS is available in this block.
      if (!dominates(RHS, IncBB))
        return false;
      const SCEV *L = getSCEV(LPhi->getIncomingValueForBlock(IncBB));
      // Make sure L does not refer to a value from a potentially previous
      // iteration of a loop.
      if (!properlyDominates(L, LBB))
        return false;
      if (!ProvedEasily(L, RHS))
        return false;
    }
  }
  return true;
}

bool ScalarEvolution::isImpliedCondOperandsViaShift(ICmpInst::Predicate Pred,
                                                    const SCEV *LHS,
                                                    const SCEV *RHS,
                                                    const SCEV *FoundLHS,
                                                    const SCEV *FoundRHS) {
  // We want to imply LHS < RHS from LHS < (RHS >> shiftvalue).  First, make
  // sure that we are dealing with same LHS.
  if (RHS == FoundRHS) {
    std::swap(LHS, RHS);
    std::swap(FoundLHS, FoundRHS);
    Pred = ICmpInst::getSwappedPredicate(Pred);
  }
  if (LHS != FoundLHS)
    return false;

  auto *SUFoundRHS = dyn_cast<SCEVUnknown>(FoundRHS);
  if (!SUFoundRHS)
    return false;

  Value *Shiftee, *ShiftValue;

  using namespace PatternMatch;
  if (match(SUFoundRHS->getValue(),
            m_LShr(m_Value(Shiftee), m_Value(ShiftValue)))) {
    auto *ShifteeS = getSCEV(Shiftee);
    // Prove one of the following:
    // LHS <u (shiftee >> shiftvalue) && shiftee <=u RHS ---> LHS <u RHS
    // LHS <=u (shiftee >> shiftvalue) && shiftee <=u RHS ---> LHS <=u RHS
    // LHS <s (shiftee >> shiftvalue) && shiftee <=s RHS && shiftee >=s 0
    //   ---> LHS <s RHS
    // LHS <=s (shiftee >> shiftvalue) && shiftee <=s RHS && shiftee >=s 0
    //   ---> LHS <=s RHS
    if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE)
      return isKnownPredicate(ICmpInst::ICMP_ULE, ShifteeS, RHS);
    if (Pred == ICmpInst::ICMP_SLT || Pred == ICmpInst::ICMP_SLE)
      if (isKnownNonNegative(ShifteeS))
        return isKnownPredicate(ICmpInst::ICMP_SLE, ShifteeS, RHS);
  }

  return false;
}

bool ScalarEvolution::isImpliedCondOperands(ICmpInst::Predicate Pred,
                                            const SCEV *LHS, const SCEV *RHS,
                                            const SCEV *FoundLHS,
                                            const SCEV *FoundRHS,
                                            const Instruction *CtxI) {
  if (isImpliedCondOperandsViaRanges(Pred, LHS, RHS, Pred, FoundLHS, FoundRHS))
    return true;

  if (isImpliedCondOperandsViaNoOverflow(Pred, LHS, RHS, FoundLHS, FoundRHS))
    return true;

  if (isImpliedCondOperandsViaShift(Pred, LHS, RHS, FoundLHS, FoundRHS))
    return true;

  if (isImpliedCondOperandsViaAddRecStart(Pred, LHS, RHS, FoundLHS, FoundRHS,
                                          CtxI))
    return true;

  return isImpliedCondOperandsHelper(Pred, LHS, RHS,
                                     FoundLHS, FoundRHS);
}

/// Is MaybeMinMaxExpr an (U|S)(Min|Max) of Candidate and some other values?
template <typename MinMaxExprType>
static bool IsMinMaxConsistingOf(const SCEV *MaybeMinMaxExpr,
                                 const SCEV *Candidate) {
  const MinMaxExprType *MinMaxExpr = dyn_cast<MinMaxExprType>(MaybeMinMaxExpr);
  if (!MinMaxExpr)
    return false;

  return is_contained(MinMaxExpr->operands(), Candidate);
}

static bool IsKnownPredicateViaAddRecStart(ScalarEvolution &SE,
                                           ICmpInst::Predicate Pred,
                                           const SCEV *LHS, const SCEV *RHS) {
  // If both sides are affine addrecs for the same loop, with equal
  // steps, and we know the recurrences don't wrap, then we only
  // need to check the predicate on the starting values.

  if (!ICmpInst::isRelational(Pred))
    return false;

  const SCEVAddRecExpr *LAR = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!LAR)
    return false;
  const SCEVAddRecExpr *RAR = dyn_cast<SCEVAddRecExpr>(RHS);
  if (!RAR)
    return false;
  if (LAR->getLoop() != RAR->getLoop())
    return false;
  if (!LAR->isAffine() || !RAR->isAffine())
    return false;

  if (LAR->getStepRecurrence(SE) != RAR->getStepRecurrence(SE))
    return false;

  SCEV::NoWrapFlags NW = ICmpInst::isSigned(Pred) ?
                         SCEV::FlagNSW : SCEV::FlagNUW;
  if (!LAR->getNoWrapFlags(NW) || !RAR->getNoWrapFlags(NW))
    return false;

  return SE.isKnownPredicate(Pred, LAR->getStart(), RAR->getStart());
}

/// Is LHS `Pred` RHS true on the virtue of LHS or RHS being a Min or Max
/// expression?
static bool IsKnownPredicateViaMinOrMax(ScalarEvolution &SE,
                                        ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS) {
  switch (Pred) {
  default:
    return false;

  case ICmpInst::ICMP_SGE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_SLE:
    return
        // min(A, ...) <= A
        IsMinMaxConsistingOf<SCEVSMinExpr>(LHS, RHS) ||
        // A <= max(A, ...)
        IsMinMaxConsistingOf<SCEVSMaxExpr>(RHS, LHS);

  case ICmpInst::ICMP_UGE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_ULE:
    return
        // min(A, ...) <= A
        // FIXME: what about umin_seq?
        IsMinMaxConsistingOf<SCEVUMinExpr>(LHS, RHS) ||
        // A <= max(A, ...)
        IsMinMaxConsistingOf<SCEVUMaxExpr>(RHS, LHS);
  }

  llvm_unreachable("covered switch fell through?!");
}

bool ScalarEvolution::isImpliedViaOperations(ICmpInst::Predicate Pred,
                                             const SCEV *LHS, const SCEV *RHS,
                                             const SCEV *FoundLHS,
                                             const SCEV *FoundRHS,
                                             unsigned Depth) {
  assert(getTypeSizeInBits(LHS->getType()) ==
             getTypeSizeInBits(RHS->getType()) &&
         "LHS and RHS have different sizes?");
  assert(getTypeSizeInBits(FoundLHS->getType()) ==
             getTypeSizeInBits(FoundRHS->getType()) &&
         "FoundLHS and FoundRHS have different sizes?");
  // We want to avoid hurting the compile time with analysis of too big trees.
  if (Depth > MaxSCEVOperationsImplicationDepth)
    return false;

  // We only want to work with GT comparison so far.
  if (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_SLT) {
    Pred = CmpInst::getSwappedPredicate(Pred);
    std::swap(LHS, RHS);
    std::swap(FoundLHS, FoundRHS);
  }

  // For unsigned, try to reduce it to corresponding signed comparison.
  if (Pred == ICmpInst::ICMP_UGT)
    // We can replace unsigned predicate with its signed counterpart if all
    // involved values are non-negative.
    // TODO: We could have better support for unsigned.
    if (isKnownNonNegative(FoundLHS) && isKnownNonNegative(FoundRHS)) {
      // Knowing that both FoundLHS and FoundRHS are non-negative, and knowing
      // FoundLHS >u FoundRHS, we also know that FoundLHS >s FoundRHS. Let us
      // use this fact to prove that LHS and RHS are non-negative.
      const SCEV *MinusOne = getMinusOne(LHS->getType());
      if (isImpliedCondOperands(ICmpInst::ICMP_SGT, LHS, MinusOne, FoundLHS,
                                FoundRHS) &&
          isImpliedCondOperands(ICmpInst::ICMP_SGT, RHS, MinusOne, FoundLHS,
                                FoundRHS))
        Pred = ICmpInst::ICMP_SGT;
    }

  if (Pred != ICmpInst::ICMP_SGT)
    return false;

  auto GetOpFromSExt = [&](const SCEV *S) {
    if (auto *Ext = dyn_cast<SCEVSignExtendExpr>(S))
      return Ext->getOperand();
    // TODO: If S is a SCEVConstant then you can cheaply "strip" the sext off
    // the constant in some cases.
    return S;
  };

  // Acquire values from extensions.
  auto *OrigLHS = LHS;
  auto *OrigFoundLHS = FoundLHS;
  LHS = GetOpFromSExt(LHS);
  FoundLHS = GetOpFromSExt(FoundLHS);

  // Is the SGT predicate can be proved trivially or using the found context.
  auto IsSGTViaContext = [&](const SCEV *S1, const SCEV *S2) {
    return isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SGT, S1, S2) ||
           isImpliedViaOperations(ICmpInst::ICMP_SGT, S1, S2, OrigFoundLHS,
                                  FoundRHS, Depth + 1);
  };

  if (auto *LHSAddExpr = dyn_cast<SCEVAddExpr>(LHS)) {
    // We want to avoid creation of any new non-constant SCEV. Since we are
    // going to compare the operands to RHS, we should be certain that we don't
    // need any size extensions for this. So let's decline all cases when the
    // sizes of types of LHS and RHS do not match.
    // TODO: Maybe try to get RHS from sext to catch more cases?
    if (getTypeSizeInBits(LHS->getType()) != getTypeSizeInBits(RHS->getType()))
      return false;

    // Should not overflow.
    if (!LHSAddExpr->hasNoSignedWrap())
      return false;

    auto *LL = LHSAddExpr->getOperand(0);
    auto *LR = LHSAddExpr->getOperand(1);
    auto *MinusOne = getMinusOne(RHS->getType());

    // Checks that S1 >= 0 && S2 > RHS, trivially or using the found context.
    auto IsSumGreaterThanRHS = [&](const SCEV *S1, const SCEV *S2) {
      return IsSGTViaContext(S1, MinusOne) && IsSGTViaContext(S2, RHS);
    };
    // Try to prove the following rule:
    // (LHS = LL + LR) && (LL >= 0) && (LR > RHS) => (LHS > RHS).
    // (LHS = LL + LR) && (LR >= 0) && (LL > RHS) => (LHS > RHS).
    if (IsSumGreaterThanRHS(LL, LR) || IsSumGreaterThanRHS(LR, LL))
      return true;
  } else if (auto *LHSUnknownExpr = dyn_cast<SCEVUnknown>(LHS)) {
    Value *LL, *LR;
    // FIXME: Once we have SDiv implemented, we can get rid of this matching.

    using namespace llvm::PatternMatch;

    if (match(LHSUnknownExpr->getValue(), m_SDiv(m_Value(LL), m_Value(LR)))) {
      // Rules for division.
      // We are going to perform some comparisons with Denominator and its
      // derivative expressions. In general case, creating a SCEV for it may
      // lead to a complex analysis of the entire graph, and in particular it
      // can request trip count recalculation for the same loop. This would
      // cache as SCEVCouldNotCompute to avoid the infinite recursion. To avoid
      // this, we only want to create SCEVs that are constants in this section.
      // So we bail if Denominator is not a constant.
      if (!isa<ConstantInt>(LR))
        return false;

      auto *Denominator = cast<SCEVConstant>(getSCEV(LR));

      // We want to make sure that LHS = FoundLHS / Denominator. If it is so,
      // then a SCEV for the numerator already exists and matches with FoundLHS.
      auto *Numerator = getExistingSCEV(LL);
      if (!Numerator || Numerator->getType() != FoundLHS->getType())
        return false;

      // Make sure that the numerator matches with FoundLHS and the denominator
      // is positive.
      if (!HasSameValue(Numerator, FoundLHS) || !isKnownPositive(Denominator))
        return false;

      auto *DTy = Denominator->getType();
      auto *FRHSTy = FoundRHS->getType();
      if (DTy->isPointerTy() != FRHSTy->isPointerTy())
        // One of types is a pointer and another one is not. We cannot extend
        // them properly to a wider type, so let us just reject this case.
        // TODO: Usage of getEffectiveSCEVType for DTy, FRHSTy etc should help
        // to avoid this check.
        return false;

      // Given that:
      // FoundLHS > FoundRHS, LHS = FoundLHS / Denominator, Denominator > 0.
      auto *WTy = getWiderType(DTy, FRHSTy);
      auto *DenominatorExt = getNoopOrSignExtend(Denominator, WTy);
      auto *FoundRHSExt = getNoopOrSignExtend(FoundRHS, WTy);

      // Try to prove the following rule:
      // (FoundRHS > Denominator - 2) && (RHS <= 0) => (LHS > RHS).
      // For example, given that FoundLHS > 2. It means that FoundLHS is at
      // least 3. If we divide it by Denominator < 4, we will have at least 1.
      auto *DenomMinusTwo = getMinusSCEV(DenominatorExt, getConstant(WTy, 2));
      if (isKnownNonPositive(RHS) &&
          IsSGTViaContext(FoundRHSExt, DenomMinusTwo))
        return true;

      // Try to prove the following rule:
      // (FoundRHS > -1 - Denominator) && (RHS < 0) => (LHS > RHS).
      // For example, given that FoundLHS > -3. Then FoundLHS is at least -2.
      // If we divide it by Denominator > 2, then:
      // 1. If FoundLHS is negative, then the result is 0.
      // 2. If FoundLHS is non-negative, then the result is non-negative.
      // Anyways, the result is non-negative.
      auto *MinusOne = getMinusOne(WTy);
      auto *NegDenomMinusOne = getMinusSCEV(MinusOne, DenominatorExt);
      if (isKnownNegative(RHS) &&
          IsSGTViaContext(FoundRHSExt, NegDenomMinusOne))
        return true;
    }
  }

  // If our expression contained SCEVUnknown Phis, and we split it down and now
  // need to prove something for them, try to prove the predicate for every
  // possible incoming values of those Phis.
  if (isImpliedViaMerge(Pred, OrigLHS, RHS, OrigFoundLHS, FoundRHS, Depth + 1))
    return true;

  return false;
}

static bool isKnownPredicateExtendIdiom(ICmpInst::Predicate Pred,
                                        const SCEV *LHS, const SCEV *RHS) {
  // zext x u<= sext x, sext x s<= zext x
  switch (Pred) {
  case ICmpInst::ICMP_SGE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_SLE: {
    // If operand >=s 0 then ZExt == SExt.  If operand <s 0 then SExt <s ZExt.
    const SCEVSignExtendExpr *SExt = dyn_cast<SCEVSignExtendExpr>(LHS);
    const SCEVZeroExtendExpr *ZExt = dyn_cast<SCEVZeroExtendExpr>(RHS);
    if (SExt && ZExt && SExt->getOperand() == ZExt->getOperand())
      return true;
    break;
  }
  case ICmpInst::ICMP_UGE:
    std::swap(LHS, RHS);
    [[fallthrough]];
  case ICmpInst::ICMP_ULE: {
    // If operand >=s 0 then ZExt == SExt.  If operand <s 0 then ZExt <u SExt.
    const SCEVZeroExtendExpr *ZExt = dyn_cast<SCEVZeroExtendExpr>(LHS);
    const SCEVSignExtendExpr *SExt = dyn_cast<SCEVSignExtendExpr>(RHS);
    if (SExt && ZExt && SExt->getOperand() == ZExt->getOperand())
      return true;
    break;
  }
  default:
    break;
  };
  return false;
}

bool
ScalarEvolution::isKnownViaNonRecursiveReasoning(ICmpInst::Predicate Pred,
                                           const SCEV *LHS, const SCEV *RHS) {
  return isKnownPredicateExtendIdiom(Pred, LHS, RHS) ||
         isKnownPredicateViaConstantRanges(Pred, LHS, RHS) ||
         IsKnownPredicateViaMinOrMax(*this, Pred, LHS, RHS) ||
         IsKnownPredicateViaAddRecStart(*this, Pred, LHS, RHS) ||
         isKnownPredicateViaNoOverflow(Pred, LHS, RHS);
}

bool
ScalarEvolution::isImpliedCondOperandsHelper(ICmpInst::Predicate Pred,
                                             const SCEV *LHS, const SCEV *RHS,
                                             const SCEV *FoundLHS,
                                             const SCEV *FoundRHS) {
  switch (Pred) {
  default: llvm_unreachable("Unexpected ICmpInst::Predicate value!");
  case ICmpInst::ICMP_EQ:
  case ICmpInst::ICMP_NE:
    if (HasSameValue(LHS, FoundLHS) && HasSameValue(RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_SLT:
  case ICmpInst::ICMP_SLE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SLE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SGE, RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_SGT:
  case ICmpInst::ICMP_SGE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SGE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_SLE, RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_ULT:
  case ICmpInst::ICMP_ULE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_ULE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_UGE, RHS, FoundRHS))
      return true;
    break;
  case ICmpInst::ICMP_UGT:
  case ICmpInst::ICMP_UGE:
    if (isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_UGE, LHS, FoundLHS) &&
        isKnownViaNonRecursiveReasoning(ICmpInst::ICMP_ULE, RHS, FoundRHS))
      return true;
    break;
  }

  // Maybe it can be proved via operations?
  if (isImpliedViaOperations(Pred, LHS, RHS, FoundLHS, FoundRHS))
    return true;

  return false;
}

bool ScalarEvolution::isImpliedCondOperandsViaRanges(ICmpInst::Predicate Pred,
                                                     const SCEV *LHS,
                                                     const SCEV *RHS,
                                                     ICmpInst::Predicate FoundPred,
                                                     const SCEV *FoundLHS,
                                                     const SCEV *FoundRHS) {
  if (!isa<SCEVConstant>(RHS) || !isa<SCEVConstant>(FoundRHS))
    // The restriction on `FoundRHS` be lifted easily -- it exists only to
    // reduce the compile time impact of this optimization.
    return false;

  std::optional<APInt> Addend = computeConstantDifference(LHS, FoundLHS);
  if (!Addend)
    return false;

  const APInt &ConstFoundRHS = cast<SCEVConstant>(FoundRHS)->getAPInt();

  // `FoundLHSRange` is the range we know `FoundLHS` to be in by virtue of the
  // antecedent "`FoundLHS` `FoundPred` `FoundRHS`".
  ConstantRange FoundLHSRange =
      ConstantRange::makeExactICmpRegion(FoundPred, ConstFoundRHS);

  // Since `LHS` is `FoundLHS` + `Addend`, we can compute a range for `LHS`:
  ConstantRange LHSRange = FoundLHSRange.add(ConstantRange(*Addend));

  // We can also compute the range of values for `LHS` that satisfy the
  // consequent, "`LHS` `Pred` `RHS`":
  const APInt &ConstRHS = cast<SCEVConstant>(RHS)->getAPInt();
  // The antecedent implies the consequent if every value of `LHS` that
  // satisfies the antecedent also satisfies the consequent.
  return LHSRange.icmp(Pred, ConstRHS);
}

bool ScalarEvolution::canIVOverflowOnLT(const SCEV *RHS, const SCEV *Stride,
                                        bool IsSigned) {
  assert(isKnownPositive(Stride) && "Positive stride expected!");

  unsigned BitWidth = getTypeSizeInBits(RHS->getType());
  const SCEV *One = getOne(Stride->getType());

  if (IsSigned) {
    APInt MaxRHS = getSignedRangeMax(RHS);
    APInt MaxValue = APInt::getSignedMaxValue(BitWidth);
    APInt MaxStrideMinusOne = getSignedRangeMax(getMinusSCEV(Stride, One));

    // SMaxRHS + SMaxStrideMinusOne > SMaxValue => overflow!
    return (std::move(MaxValue) - MaxStrideMinusOne).slt(MaxRHS);
  }

  APInt MaxRHS = getUnsignedRangeMax(RHS);
  APInt MaxValue = APInt::getMaxValue(BitWidth);
  APInt MaxStrideMinusOne = getUnsignedRangeMax(getMinusSCEV(Stride, One));

  // UMaxRHS + UMaxStrideMinusOne > UMaxValue => overflow!
  return (std::move(MaxValue) - MaxStrideMinusOne).ult(MaxRHS);
}

bool ScalarEvolution::canIVOverflowOnGT(const SCEV *RHS, const SCEV *Stride,
                                        bool IsSigned) {

  unsigned BitWidth = getTypeSizeInBits(RHS->getType());
  const SCEV *One = getOne(Stride->getType());

  if (IsSigned) {
    APInt MinRHS = getSignedRangeMin(RHS);
    APInt MinValue = APInt::getSignedMinValue(BitWidth);
    APInt MaxStrideMinusOne = getSignedRangeMax(getMinusSCEV(Stride, One));

    // SMinRHS - SMaxStrideMinusOne < SMinValue => overflow!
    return (std::move(MinValue) + MaxStrideMinusOne).sgt(MinRHS);
  }

  APInt MinRHS = getUnsignedRangeMin(RHS);
  APInt MinValue = APInt::getMinValue(BitWidth);
  APInt MaxStrideMinusOne = getUnsignedRangeMax(getMinusSCEV(Stride, One));

  // UMinRHS - UMaxStrideMinusOne < UMinValue => overflow!
  return (std::move(MinValue) + MaxStrideMinusOne).ugt(MinRHS);
}

const SCEV *ScalarEvolution::getUDivCeilSCEV(const SCEV *N, const SCEV *D) {
  // umin(N, 1) + floor((N - umin(N, 1)) / D)
  // This is equivalent to "1 + floor((N - 1) / D)" for N != 0. The umin
  // expression fixes the case of N=0.
  const SCEV *MinNOne = getUMinExpr(N, getOne(N->getType()));
  const SCEV *NMinusOne = getMinusSCEV(N, MinNOne);
  return getAddExpr(MinNOne, getUDivExpr(NMinusOne, D));
}

const SCEV *ScalarEvolution::computeMaxBECountForLT(const SCEV *Start,
                                                    const SCEV *Stride,
                                                    const SCEV *End,
                                                    unsigned BitWidth,
                                                    bool IsSigned) {
  // The logic in this function assumes we can represent a positive stride.
  // If we can't, the backedge-taken count must be zero.
  if (IsSigned && BitWidth == 1)
    return getZero(Stride->getType());

  // This code below only been closely audited for negative strides in the
  // unsigned comparison case, it may be correct for signed comparison, but
  // that needs to be established.
  if (IsSigned && isKnownNegative(Stride))
    return getCouldNotCompute();

  // Calculate the maximum backedge count based on the range of values
  // permitted by Start, End, and Stride.
  APInt MinStart =
      IsSigned ? getSignedRangeMin(Start) : getUnsignedRangeMin(Start);

  APInt MinStride =
      IsSigned ? getSignedRangeMin(Stride) : getUnsignedRangeMin(Stride);

  // We assume either the stride is positive, or the backedge-taken count
  // is zero. So force StrideForMaxBECount to be at least one.
  APInt One(BitWidth, 1);
  APInt StrideForMaxBECount = IsSigned ? APIntOps::smax(One, MinStride)
                                       : APIntOps::umax(One, MinStride);

  APInt MaxValue = IsSigned ? APInt::getSignedMaxValue(BitWidth)
                            : APInt::getMaxValue(BitWidth);
  APInt Limit = MaxValue - (StrideForMaxBECount - 1);

  // Although End can be a MAX expression we estimate MaxEnd considering only
  // the case End = RHS of the loop termination condition. This is safe because
  // in the other case (End - Start) is zero, leading to a zero maximum backedge
  // taken count.
  APInt MaxEnd = IsSigned ? APIntOps::smin(getSignedRangeMax(End), Limit)
                          : APIntOps::umin(getUnsignedRangeMax(End), Limit);

  // MaxBECount = ceil((max(MaxEnd, MinStart) - MinStart) / Stride)
  MaxEnd = IsSigned ? APIntOps::smax(MaxEnd, MinStart)
                    : APIntOps::umax(MaxEnd, MinStart);

  return getUDivCeilSCEV(getConstant(MaxEnd - MinStart) /* Delta */,
                         getConstant(StrideForMaxBECount) /* Step */);
}

ScalarEvolution::ExitLimit
ScalarEvolution::howManyLessThans(const SCEV *LHS, const SCEV *RHS,
                                  const Loop *L, bool IsSigned,
                                  bool ControlsOnlyExit, bool AllowPredicates) {
  SmallPtrSet<const SCEVPredicate *, 4> Predicates;

  const SCEVAddRecExpr *IV = dyn_cast<SCEVAddRecExpr>(LHS);
  bool PredicatedIV = false;

  auto canAssumeNoSelfWrap = [&](const SCEVAddRecExpr *AR) {
    // Can we prove this loop *must* be UB if overflow of IV occurs?
    // Reasoning goes as follows:
    // * Suppose the IV did self wrap.
    // * If Stride evenly divides the iteration space, then once wrap
    //   occurs, the loop must revisit the same values.
    // * We know that RHS is invariant, and that none of those values
    //   caused this exit to be taken previously.  Thus, this exit is
    //   dynamically dead.
    // * If this is the sole exit, then a dead exit implies the loop
    //   must be infinite if there are no abnormal exits.
    // * If the loop were infinite, then it must either not be mustprogress
    //   or have side effects. Otherwise, it must be UB.
    // * It can't (by assumption), be UB so we have contradicted our
    //   premise and can conclude the IV did not in fact self-wrap.
    if (!isLoopInvariant(RHS, L))
      return false;

    auto *StrideC = dyn_cast<SCEVConstant>(AR->getStepRecurrence(*this));
    if (!StrideC || !StrideC->getAPInt().isPowerOf2())
      return false;

    if (!ControlsOnlyExit || !loopHasNoAbnormalExits(L))
      return false;

    return loopIsFiniteByAssumption(L);
  };

  if (!IV) {
    if (auto *ZExt = dyn_cast<SCEVZeroExtendExpr>(LHS)) {
      const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(ZExt->getOperand());
      if (AR && AR->getLoop() == L && AR->isAffine()) {
        auto canProveNUW = [&]() {
          // We can use the comparison to infer no-wrap flags only if it fully
          // controls the loop exit.
          if (!ControlsOnlyExit)
            return false;

          if (!isLoopInvariant(RHS, L))
            return false;

          if (!isKnownNonZero(AR->getStepRecurrence(*this)))
            // We need the sequence defined by AR to strictly increase in the
            // unsigned integer domain for the logic below to hold.
            return false;

          const unsigned InnerBitWidth = getTypeSizeInBits(AR->getType());
          const unsigned OuterBitWidth = getTypeSizeInBits(RHS->getType());
          // If RHS <=u Limit, then there must exist a value V in the sequence
          // defined by AR (e.g. {Start,+,Step}) such that V >u RHS, and
          // V <=u UINT_MAX.  Thus, we must exit the loop before unsigned
          // overflow occurs.  This limit also implies that a signed comparison
          // (in the wide bitwidth) is equivalent to an unsigned comparison as
          // the high bits on both sides must be zero.
          APInt StrideMax = getUnsignedRangeMax(AR->getStepRecurrence(*this));
          APInt Limit = APInt::getMaxValue(InnerBitWidth) - (StrideMax - 1);
          Limit = Limit.zext(OuterBitWidth);
          return getUnsignedRangeMax(applyLoopGuards(RHS, L)).ule(Limit);
        };
        auto Flags = AR->getNoWrapFlags();
        if (!hasFlags(Flags, SCEV::FlagNUW) && canProveNUW())
          Flags = setFlags(Flags, SCEV::FlagNUW);

        setNoWrapFlags(const_cast<SCEVAddRecExpr *>(AR), Flags);
        if (AR->hasNoUnsignedWrap()) {
          // Emulate what getZeroExtendExpr would have done during construction
          // if we'd been able to infer the fact just above at that time.
          const SCEV *Step = AR->getStepRecurrence(*this);
          Type *Ty = ZExt->getType();
          auto *S = getAddRecExpr(
            getExtendAddRecStart<SCEVZeroExtendExpr>(AR, Ty, this, 0),
            getZeroExtendExpr(Step, Ty, 0), L, AR->getNoWrapFlags());
          IV = dyn_cast<SCEVAddRecExpr>(S);
        }
      }
    }
  }


  if (!IV && AllowPredicates) {
    // Try to make this an AddRec using runtime tests, in the first X
    // iterations of this loop, where X is the SCEV expression found by the
    // algorithm below.
    IV = convertSCEVToAddRecWithPredicates(LHS, L, Predicates);
    PredicatedIV = true;
  }

  // Avoid weird loops
  if (!IV || IV->getLoop() != L || !IV->isAffine())
    return getCouldNotCompute();

  // A precondition of this method is that the condition being analyzed
  // reaches an exiting branch which dominates the latch.  Given that, we can
  // assume that an increment which violates the nowrap specification and
  // produces poison must cause undefined behavior when the resulting poison
  // value is branched upon and thus we can conclude that the backedge is
  // taken no more often than would be required to produce that poison value.
  // Note that a well defined loop can exit on the iteration which violates
  // the nowrap specification if there is another exit (either explicit or
  // implicit/exceptional) which causes the loop to execute before the
  // exiting instruction we're analyzing would trigger UB.
  auto WrapType = IsSigned ? SCEV::FlagNSW : SCEV::FlagNUW;
  bool NoWrap = ControlsOnlyExit && IV->getNoWrapFlags(WrapType);
  ICmpInst::Predicate Cond = IsSigned ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;

  const SCEV *Stride = IV->getStepRecurrence(*this);

  bool PositiveStride = isKnownPositive(Stride);

  // Avoid negative or zero stride values.
  if (!PositiveStride) {
    // We can compute the correct backedge taken count for loops with unknown
    // strides if we can prove that the loop is not an infinite loop with side
    // effects. Here's the loop structure we are trying to handle -
    //
    // i = start
    // do {
    //   A[i] = i;
    //   i += s;
    // } while (i < end);
    //
    // The backedge taken count for such loops is evaluated as -
    // (max(end, start + stride) - start - 1) /u stride
    //
    // The additional preconditions that we need to check to prove correctness
    // of the above formula is as follows -
    //
    // a) IV is either nuw or nsw depending upon signedness (indicated by the
    //    NoWrap flag).
    // b) the loop is guaranteed to be finite (e.g. is mustprogress and has
    //    no side effects within the loop)
    // c) loop has a single static exit (with no abnormal exits)
    //
    // Precondition a) implies that if the stride is negative, this is a single
    // trip loop. The backedge taken count formula reduces to zero in this case.
    //
    // Precondition b) and c) combine to imply that if rhs is invariant in L,
    // then a zero stride means the backedge can't be taken without executing
    // undefined behavior.
    //
    // The positive stride case is the same as isKnownPositive(Stride) returning
    // true (original behavior of the function).
    //
    if (PredicatedIV || !NoWrap || !loopIsFiniteByAssumption(L) ||
        !loopHasNoAbnormalExits(L))
      return getCouldNotCompute();

    if (!isKnownNonZero(Stride)) {
      // If we have a step of zero, and RHS isn't invariant in L, we don't know
      // if it might eventually be greater than start and if so, on which
      // iteration.  We can't even produce a useful upper bound.
      if (!isLoopInvariant(RHS, L))
        return getCouldNotCompute();

      // We allow a potentially zero stride, but we need to divide by stride
      // below.  Since the loop can't be infinite and this check must control
      // the sole exit, we can infer the exit must be taken on the first
      // iteration (e.g. backedge count = 0) if the stride is zero.  Given that,
      // we know the numerator in the divides below must be zero, so we can
      // pick an arbitrary non-zero value for the denominator (e.g. stride)
      // and produce the right result.
      // FIXME: Handle the case where Stride is poison?
      auto wouldZeroStrideBeUB = [&]() {
        // Proof by contradiction.  Suppose the stride were zero.  If we can
        // prove that the backedge *is* taken on the first iteration, then since
        // we know this condition controls the sole exit, we must have an
        // infinite loop.  We can't have a (well defined) infinite loop per
        // check just above.
        // Note: The (Start - Stride) term is used to get the start' term from
        // (start' + stride,+,stride). Remember that we only care about the
        // result of this expression when stride == 0 at runtime.
        auto *StartIfZero = getMinusSCEV(IV->getStart(), Stride);
        return isLoopEntryGuardedByCond(L, Cond, StartIfZero, RHS);
      };
      if (!wouldZeroStrideBeUB()) {
        Stride = getUMaxExpr(Stride, getOne(Stride->getType()));
      }
    }
  } else if (!Stride->isOne() && !NoWrap) {
    auto isUBOnWrap = [&]() {
      // From no-self-wrap, we need to then prove no-(un)signed-wrap.  This
      // follows trivially from the fact that every (un)signed-wrapped, but
      // not self-wrapped value must be LT than the last value before
      // (un)signed wrap.  Since we know that last value didn't exit, nor
      // will any smaller one.
      return canAssumeNoSelfWrap(IV);
    };

    // Avoid proven overflow cases: this will ensure that the backedge taken
    // count will not generate any unsigned overflow. Relaxed no-overflow
    // conditions exploit NoWrapFlags, allowing to optimize in presence of
    // undefined behaviors like the case of C language.
    if (canIVOverflowOnLT(RHS, Stride, IsSigned) && !isUBOnWrap())
      return getCouldNotCompute();
  }

  // On all paths just preceeding, we established the following invariant:
  //   IV can be assumed not to overflow up to and including the exiting
  //   iteration.  We proved this in one of two ways:
  //   1) We can show overflow doesn't occur before the exiting iteration
  //      1a) canIVOverflowOnLT, and b) step of one
  //   2) We can show that if overflow occurs, the loop must execute UB
  //      before any possible exit.
  // Note that we have not yet proved RHS invariant (in general).

  const SCEV *Start = IV->getStart();

  // Preserve pointer-typed Start/RHS to pass to isLoopEntryGuardedByCond.
  // If we convert to integers, isLoopEntryGuardedByCond will miss some cases.
  // Use integer-typed versions for actual computation; we can't subtract
  // pointers in general.
  const SCEV *OrigStart = Start;
  const SCEV *OrigRHS = RHS;
  if (Start->getType()->isPointerTy()) {
    Start = getLosslessPtrToIntExpr(Start);
    if (isa<SCEVCouldNotCompute>(Start))
      return Start;
  }
  if (RHS->getType()->isPointerTy()) {
    RHS = getLosslessPtrToIntExpr(RHS);
    if (isa<SCEVCouldNotCompute>(RHS))
      return RHS;
  }

  const SCEV *End = nullptr, *BECount = nullptr,
             *BECountIfBackedgeTaken = nullptr;
  if (!isLoopInvariant(RHS, L)) {
    const auto *RHSAddRec = dyn_cast<SCEVAddRecExpr>(RHS);
    if (PositiveStride && RHSAddRec != nullptr && RHSAddRec->getLoop() == L &&
        RHSAddRec->getNoWrapFlags()) {
      // The structure of loop we are trying to calculate backedge count of:
      //
      //  left = left_start
      //  right = right_start
      //
      //  while(left < right){
      //    ... do something here ...
      //    left += s1; // stride of left is s1 (s1 > 0)
      //    right += s2; // stride of right is s2 (s2 < 0)
      //  }
      //

      const SCEV *RHSStart = RHSAddRec->getStart();
      const SCEV *RHSStride = RHSAddRec->getStepRecurrence(*this);

      // If Stride - RHSStride is positive and does not overflow, we can write
      // backedge count as ->
      //    ceil((End - Start) /u (Stride - RHSStride))
      //    Where, End = max(RHSStart, Start)

      // Check if RHSStride < 0 and Stride - RHSStride will not overflow.
      if (isKnownNegative(RHSStride) &&
          willNotOverflow(Instruction::Sub, /*Signed=*/true, Stride,
                          RHSStride)) {

        const SCEV *Denominator = getMinusSCEV(Stride, RHSStride);
        if (isKnownPositive(Denominator)) {
          End = IsSigned ? getSMaxExpr(RHSStart, Start)
                         : getUMaxExpr(RHSStart, Start);

          // We can do this because End >= Start, as End = max(RHSStart, Start)
          const SCEV *Delta = getMinusSCEV(End, Start);

          BECount = getUDivCeilSCEV(Delta, Denominator);
          BECountIfBackedgeTaken =
              getUDivCeilSCEV(getMinusSCEV(RHSStart, Start), Denominator);
        }
      }
    }
    if (BECount == nullptr) {
      // If we cannot calculate ExactBECount, we can calculate the MaxBECount,
      // given the start, stride and max value for the end bound of the
      // loop (RHS), and the fact that IV does not overflow (which is
      // checked above).
      const SCEV *MaxBECount = computeMaxBECountForLT(
          Start, Stride, RHS, getTypeSizeInBits(LHS->getType()), IsSigned);
      return ExitLimit(getCouldNotCompute() /* ExactNotTaken */, MaxBECount,
                       MaxBECount, false /*MaxOrZero*/, Predicates);
    }
  } else {
    // We use the expression (max(End,Start)-Start)/Stride to describe the
    // backedge count, as if the backedge is taken at least once
    // max(End,Start) is End and so the result is as above, and if not
    // max(End,Start) is Start so we get a backedge count of zero.
    auto *OrigStartMinusStride = getMinusSCEV(OrigStart, Stride);
    assert(isAvailableAtLoopEntry(OrigStartMinusStride, L) && "Must be!");
    assert(isAvailableAtLoopEntry(OrigStart, L) && "Must be!");
    assert(isAvailableAtLoopEntry(OrigRHS, L) && "Must be!");
    // Can we prove (max(RHS,Start) > Start - Stride?
    if (isLoopEntryGuardedByCond(L, Cond, OrigStartMinusStride, OrigStart) &&
        isLoopEntryGuardedByCond(L, Cond, OrigStartMinusStride, OrigRHS)) {
      // In this case, we can use a refined formula for computing backedge
      // taken count.  The general formula remains:
      //   "End-Start /uceiling Stride" where "End = max(RHS,Start)"
      // We want to use the alternate formula:
      //   "((End - 1) - (Start - Stride)) /u Stride"
      // Let's do a quick case analysis to show these are equivalent under
      // our precondition that max(RHS,Start) > Start - Stride.
      // * For RHS <= Start, the backedge-taken count must be zero.
      //   "((End - 1) - (Start - Stride)) /u Stride" reduces to
      //   "((Start - 1) - (Start - Stride)) /u Stride" which simplies to
      //   "Stride - 1 /u Stride" which is indeed zero for all non-zero values
      //     of Stride.  For 0 stride, we've use umin(1,Stride) above,
      //     reducing this to the stride of 1 case.
      // * For RHS >= Start, the backedge count must be "RHS-Start /uceil
      // Stride".
      //   "((End - 1) - (Start - Stride)) /u Stride" reduces to
      //   "((RHS - 1) - (Start - Stride)) /u Stride" reassociates to
      //   "((RHS - (Start - Stride) - 1) /u Stride".
      //   Our preconditions trivially imply no overflow in that form.
      const SCEV *MinusOne = getMinusOne(Stride->getType());
      const SCEV *Numerator =
          getMinusSCEV(getAddExpr(RHS, MinusOne), getMinusSCEV(Start, Stride));
      BECount = getUDivExpr(Numerator, Stride);
    }

    if (!BECount) {
      auto canProveRHSGreaterThanEqualStart = [&]() {
        auto CondGE = IsSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE;
        const SCEV *GuardedRHS = applyLoopGuards(OrigRHS, L);
        const SCEV *GuardedStart = applyLoopGuards(OrigStart, L);

        if (isLoopEntryGuardedByCond(L, CondGE, OrigRHS, OrigStart) ||
            isKnownPredicate(CondGE, GuardedRHS, GuardedStart))
          return true;

        // (RHS > Start - 1) implies RHS >= Start.
        // * "RHS >= Start" is trivially equivalent to "RHS > Start - 1" if
        //   "Start - 1" doesn't overflow.
        // * For signed comparison, if Start - 1 does overflow, it's equal
        //   to INT_MAX, and "RHS >s INT_MAX" is trivially false.
        // * For unsigned comparison, if Start - 1 does overflow, it's equal
        //   to UINT_MAX, and "RHS >u UINT_MAX" is trivially false.
        //
        // FIXME: Should isLoopEntryGuardedByCond do this for us?
        auto CondGT = IsSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
        auto *StartMinusOne =
            getAddExpr(OrigStart, getMinusOne(OrigStart->getType()));
        return isLoopEntryGuardedByCond(L, CondGT, OrigRHS, StartMinusOne);
      };

      // If we know that RHS >= Start in the context of loop, then we know
      // that max(RHS, Start) = RHS at this point.
      if (canProveRHSGreaterThanEqualStart()) {
        End = RHS;
      } else {
        // If RHS < Start, the backedge will be taken zero times.  So in
        // general, we can write the backedge-taken count as:
        //
        //     RHS >= Start ? ceil(RHS - Start) / Stride : 0
        //
        // We convert it to the following to make it more convenient for SCEV:
        //
        //     ceil(max(RHS, Start) - Start) / Stride
        End = IsSigned ? getSMaxExpr(RHS, Start) : getUMaxExpr(RHS, Start);

        // See what would happen if we assume the backedge is taken. This is
        // used to compute MaxBECount.
        BECountIfBackedgeTaken =
            getUDivCeilSCEV(getMinusSCEV(RHS, Start), Stride);
      }

      // At this point, we know:
      //
      // 1. If IsSigned, Start <=s End; otherwise, Start <=u End
      // 2. The index variable doesn't overflow.
      //
      // Therefore, we know N exists such that
      // (Start + Stride * N) >= End, and computing "(Start + Stride * N)"
      // doesn't overflow.
      //
      // Using this information, try to prove whether the addition in
      // "(Start - End) + (Stride - 1)" has unsigned overflow.
      const SCEV *One = getOne(Stride->getType());
      bool MayAddOverflow = [&] {
        if (auto *StrideC = dyn_cast<SCEVConstant>(Stride)) {
          if (StrideC->getAPInt().isPowerOf2()) {
            // Suppose Stride is a power of two, and Start/End are unsigned
            // integers.  Let UMAX be the largest representable unsigned
            // integer.
            //
            // By the preconditions of this function, we know
            // "(Start + Stride * N) >= End", and this doesn't overflow.
            // As a formula:
            //
            //   End <= (Start + Stride * N) <= UMAX
            //
            // Subtracting Start from all the terms:
            //
            //   End - Start <= Stride * N <= UMAX - Start
            //
            // Since Start is unsigned, UMAX - Start <= UMAX.  Therefore:
            //
            //   End - Start <= Stride * N <= UMAX
            //
            // Stride * N is a multiple of Stride. Therefore,
            //
            //   End - Start <= Stride * N <= UMAX - (UMAX mod Stride)
            //
            // Since Stride is a power of two, UMAX + 1 is divisible by
            // Stride. Therefore, UMAX mod Stride == Stride - 1.  So we can
            // write:
            //
            //   End - Start <= Stride * N <= UMAX - Stride - 1
            //
            // Dropping the middle term:
            //
            //   End - Start <= UMAX - Stride - 1
            //
            // Adding Stride - 1 to both sides:
            //
            //   (End - Start) + (Stride - 1) <= UMAX
            //
            // In other words, the addition doesn't have unsigned overflow.
            //
            // A similar proof works if we treat Start/End as signed values.
            // Just rewrite steps before "End - Start <= Stride * N <= UMAX"
            // to use signed max instead of unsigned max. Note that we're
            // trying to prove a lack of unsigned overflow in either case.
            return false;
          }
        }
        if (Start == Stride || Start == getMinusSCEV(Stride, One)) {
          // If Start is equal to Stride, (End - Start) + (Stride - 1) == End
          // - 1. If !IsSigned, 0 <u Stride == Start <=u End; so 0 <u End - 1
          // <u End. If IsSigned, 0 <s Stride == Start <=s End; so 0 <s End -
          // 1 <s End.
          //
          // If Start is equal to Stride - 1, (End - Start) + Stride - 1 ==
          // End.
          return false;
        }
        return true;
      }();

      const SCEV *Delta = getMinusSCEV(End, Start);
      if (!MayAddOverflow) {
        // floor((D + (S - 1)) / S)
        // We prefer this formulation if it's legal because it's fewer
        // operations.
        BECount =
            getUDivExpr(getAddExpr(Delta, getMinusSCEV(Stride, One)), Stride);
      } else {
        BECount = getUDivCeilSCEV(Delta, Stride);
      }
    }
  }

  const SCEV *ConstantMaxBECount;
  bool MaxOrZero = false;
  if (isa<SCEVConstant>(BECount)) {
    ConstantMaxBECount = BECount;
  } else if (BECountIfBackedgeTaken &&
             isa<SCEVConstant>(BECountIfBackedgeTaken)) {
    // If we know exactly how many times the backedge will be taken if it's
    // taken at least once, then the backedge count will either be that or
    // zero.
    ConstantMaxBECount = BECountIfBackedgeTaken;
    MaxOrZero = true;
  } else {
    ConstantMaxBECount = computeMaxBECountForLT(
        Start, Stride, RHS, getTypeSizeInBits(LHS->getType()), IsSigned);
  }

  if (isa<SCEVCouldNotCompute>(ConstantMaxBECount) &&
      !isa<SCEVCouldNotCompute>(BECount))
    ConstantMaxBECount = getConstant(getUnsignedRangeMax(BECount));

  const SCEV *SymbolicMaxBECount =
      isa<SCEVCouldNotCompute>(BECount) ? ConstantMaxBECount : BECount;
  return ExitLimit(BECount, ConstantMaxBECount, SymbolicMaxBECount, MaxOrZero,
                   Predicates);
}

ScalarEvolution::ExitLimit ScalarEvolution::howManyGreaterThans(
    const SCEV *LHS, const SCEV *RHS, const Loop *L, bool IsSigned,
    bool ControlsOnlyExit, bool AllowPredicates) {
  SmallPtrSet<const SCEVPredicate *, 4> Predicates;
  // We handle only IV > Invariant
  if (!isLoopInvariant(RHS, L))
    return getCouldNotCompute();

  const SCEVAddRecExpr *IV = dyn_cast<SCEVAddRecExpr>(LHS);
  if (!IV && AllowPredicates)
    // Try to make this an AddRec using runtime tests, in the first X
    // iterations of this loop, where X is the SCEV expression found by the
    // algorithm below.
    IV = convertSCEVToAddRecWithPredicates(LHS, L, Predicates);

  // Avoid weird loops
  if (!IV || IV->getLoop() != L || !IV->isAffine())
    return getCouldNotCompute();

  auto WrapType = IsSigned ? SCEV::FlagNSW : SCEV::FlagNUW;
  bool NoWrap = ControlsOnlyExit && IV->getNoWrapFlags(WrapType);
  ICmpInst::Predicate Cond = IsSigned ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;

  const SCEV *Stride = getNegativeSCEV(IV->getStepRecurrence(*this));

  // Avoid negative or zero stride values
  if (!isKnownPositive(Stride))
    return getCouldNotCompute();

  // Avoid proven overflow cases: this will ensure that the backedge taken count
  // will not generate any unsigned overflow. Relaxed no-overflow conditions
  // exploit NoWrapFlags, allowing to optimize in presence of undefined
  // behaviors like the case of C language.
  if (!Stride->isOne() && !NoWrap)
    if (canIVOverflowOnGT(RHS, Stride, IsSigned))
      return getCouldNotCompute();

  const SCEV *Start = IV->getStart();
  const SCEV *End = RHS;
  if (!isLoopEntryGuardedByCond(L, Cond, getAddExpr(Start, Stride), RHS)) {
    // If we know that Start >= RHS in the context of loop, then we know that
    // min(RHS, Start) = RHS at this point.
    if (isLoopEntryGuardedByCond(
            L, IsSigned ? ICmpInst::ICMP_SGE : ICmpInst::ICMP_UGE, Start, RHS))
      End = RHS;
    else
      End = IsSigned ? getSMinExpr(RHS, Start) : getUMinExpr(RHS, Start);
  }

  if (Start->getType()->isPointerTy()) {
    Start = getLosslessPtrToIntExpr(Start);
    if (isa<SCEVCouldNotCompute>(Start))
      return Start;
  }
  if (End->getType()->isPointerTy()) {
    End = getLosslessPtrToIntExpr(End);
    if (isa<SCEVCouldNotCompute>(End))
      return End;
  }

  // Compute ((Start - End) + (Stride - 1)) / Stride.
  // FIXME: This can overflow. Holding off on fixing this for now;
  // howManyGreaterThans will hopefully be gone soon.
  const SCEV *One = getOne(Stride->getType());
  const SCEV *BECount = getUDivExpr(
      getAddExpr(getMinusSCEV(Start, End), getMinusSCEV(Stride, One)), Stride);

  APInt MaxStart = IsSigned ? getSignedRangeMax(Start)
                            : getUnsignedRangeMax(Start);

  APInt MinStride = IsSigned ? getSignedRangeMin(Stride)
                             : getUnsignedRangeMin(Stride);

  unsigned BitWidth = getTypeSizeInBits(LHS->getType());
  APInt Limit = IsSigned ? APInt::getSignedMinValue(BitWidth) + (MinStride - 1)
                         : APInt::getMinValue(BitWidth) + (MinStride - 1);

  // Although End can be a MIN expression we estimate MinEnd considering only
  // the case End = RHS. This is safe because in the other case (Start - End)
  // is zero, leading to a zero maximum backedge taken count.
  APInt MinEnd =
    IsSigned ? APIntOps::smax(getSignedRangeMin(RHS), Limit)
             : APIntOps::umax(getUnsignedRangeMin(RHS), Limit);

  const SCEV *ConstantMaxBECount =
      isa<SCEVConstant>(BECount)
          ? BECount
          : getUDivCeilSCEV(getConstant(MaxStart - MinEnd),
                            getConstant(MinStride));

  if (isa<SCEVCouldNotCompute>(ConstantMaxBECount))
    ConstantMaxBECount = BECount;
  const SCEV *SymbolicMaxBECount =
      isa<SCEVCouldNotCompute>(BECount) ? ConstantMaxBECount : BECount;

  return ExitLimit(BECount, ConstantMaxBECount, SymbolicMaxBECount, false,
                   Predicates);
}

const SCEV *SCEVAddRecExpr::getNumIterationsInRange(const ConstantRange &Range,
                                                    ScalarEvolution &SE) const {
  if (Range.isFullSet())  // Infinite loop.
    return SE.getCouldNotCompute();

  // If the start is a non-zero constant, shift the range to simplify things.
  if (const SCEVConstant *SC = dyn_cast<SCEVConstant>(getStart()))
    if (!SC->getValue()->isZero()) {
      SmallVector<const SCEV *, 4> Operands(operands());
      Operands[0] = SE.getZero(SC->getType());
      const SCEV *Shifted = SE.getAddRecExpr(Operands, getLoop(),
                                             getNoWrapFlags(FlagNW));
      if (const auto *ShiftedAddRec = dyn_cast<SCEVAddRecExpr>(Shifted))
        return ShiftedAddRec->getNumIterationsInRange(
            Range.subtract(SC->getAPInt()), SE);
      // This is strange and shouldn't happen.
      return SE.getCouldNotCompute();
    }

  // The only time we can solve this is when we have all constant indices.
  // Otherwise, we cannot determine the overflow conditions.
  if (any_of(operands(), [](const SCEV *Op) { return !isa<SCEVConstant>(Op); }))
    return SE.getCouldNotCompute();

  // Okay at this point we know that all elements of the chrec are constants and
  // that the start element is zero.

  // First check to see if the range contains zero.  If not, the first
  // iteration exits.
  unsigned BitWidth = SE.getTypeSizeInBits(getType());
  if (!Range.contains(APInt(BitWidth, 0)))
    return SE.getZero(getType());

  if (isAffine()) {
    // If this is an affine expression then we have this situation:
    //   Solve {0,+,A} in Range  ===  Ax in Range

    // We know that zero is in the range.  If A is positive then we know that
    // the upper value of the range must be the first possible exit value.
    // If A is negative then the lower of the range is the last possible loop
    // value.  Also note that we already checked for a full range.
    APInt A = cast<SCEVConstant>(getOperand(1))->getAPInt();
    APInt End = A.sge(1) ? (Range.getUpper() - 1) : Range.getLower();

    // The exit value should be (End+A)/A.
    APInt ExitVal = (End + A).udiv(A);
    ConstantInt *ExitValue = ConstantInt::get(SE.getContext(), ExitVal);

    // Evaluate at the exit value.  If we really did fall out of the valid
    // range, then we computed our trip count, otherwise wrap around or other
    // things must have happened.
    ConstantInt *Val = EvaluateConstantChrecAtConstant(this, ExitValue, SE);
    if (Range.contains(Val->getValue()))
      return SE.getCouldNotCompute();  // Something strange happened

    // Ensure that the previous value is in the range.
    assert(Range.contains(
           EvaluateConstantChrecAtConstant(this,
           ConstantInt::get(SE.getContext(), ExitVal - 1), SE)->getValue()) &&
           "Linear scev computation is off in a bad way!");
    return SE.getConstant(ExitValue);
  }

  if (isQuadratic()) {
    if (auto S = SolveQuadraticAddRecRange(this, Range, SE))
      return SE.getConstant(*S);
  }

  return SE.getCouldNotCompute();
}

const SCEVAddRecExpr *
SCEVAddRecExpr::getPostIncExpr(ScalarEvolution &SE) const {
  assert(getNumOperands() > 1 && "AddRec with zero step?");
  // There is a temptation to just call getAddExpr(this, getStepRecurrence(SE)),
  // but in this case we cannot guarantee that the value returned will be an
  // AddRec because SCEV does not have a fixed point where it stops
  // simplification: it is legal to return ({rec1} + {rec2}). For example, it
  // may happen if we reach arithmetic depth limit while simplifying. So we
  // construct the returned value explicitly.
  SmallVector<const SCEV *, 3> Ops;
  // If this is {A,+,B,+,C,...,+,N}, then its step is {B,+,C,+,...,+,N}, and
  // (this + Step) is {A+B,+,B+C,+...,+,N}.
  for (unsigned i = 0, e = getNumOperands() - 1; i < e; ++i)
    Ops.push_back(SE.getAddExpr(getOperand(i), getOperand(i + 1)));
  // We know that the last operand is not a constant zero (otherwise it would
  // have been popped out earlier). This guarantees us that if the result has
  // the same last operand, then it will also not be popped out, meaning that
  // the returned value will be an AddRec.
  const SCEV *Last = getOperand(getNumOperands() - 1);
  assert(!Last->isZero() && "Recurrency with zero step?");
  Ops.push_back(Last);
  return cast<SCEVAddRecExpr>(SE.getAddRecExpr(Ops, getLoop(),
                                               SCEV::FlagAnyWrap));
}

// Return true when S contains at least an undef value.
bool ScalarEvolution::containsUndefs(const SCEV *S) const {
  return SCEVExprContains(S, [](const SCEV *S) {
    if (const auto *SU = dyn_cast<SCEVUnknown>(S))
      return isa<UndefValue>(SU->getValue());
    return false;
  });
}

// Return true when S contains a value that is a nullptr.
bool ScalarEvolution::containsErasedValue(const SCEV *S) const {
  return SCEVExprContains(S, [](const SCEV *S) {
    if (const auto *SU = dyn_cast<SCEVUnknown>(S))
      return SU->getValue() == nullptr;
    return false;
  });
}

/// Return the size of an element read or written by Inst.
const SCEV *ScalarEvolution::getElementSize(Instruction *Inst) {
  Type *Ty;
  if (StoreInst *Store = dyn_cast<StoreInst>(Inst))
    Ty = Store->getValueOperand()->getType();
  else if (LoadInst *Load = dyn_cast<LoadInst>(Inst))
    Ty = Load->getType();
  else
    return nullptr;

  Type *ETy = getEffectiveSCEVType(PointerType::getUnqual(Ty));
  return getSizeOfExpr(ETy, Ty);
}

//===----------------------------------------------------------------------===//
//                   SCEVCallbackVH Class Implementation
//===----------------------------------------------------------------------===//

void ScalarEvolution::SCEVCallbackVH::deleted() {
  assert(SE && "SCEVCallbackVH called with a null ScalarEvolution!");
  if (PHINode *PN = dyn_cast<PHINode>(getValPtr()))
    SE->ConstantEvolutionLoopExitValue.erase(PN);
  SE->eraseValueFromMap(getValPtr());
  // this now dangles!
}

void ScalarEvolution::SCEVCallbackVH::allUsesReplacedWith(Value *V) {
  assert(SE && "SCEVCallbackVH called with a null ScalarEvolution!");

  // Forget all the expressions associated with users of the old value,
  // so that future queries will recompute the expressions using the new
  // value.
  SE->forgetValue(getValPtr());
  // this now dangles!
}

ScalarEvolution::SCEVCallbackVH::SCEVCallbackVH(Value *V, ScalarEvolution *se)
  : CallbackVH(V), SE(se) {}

//===----------------------------------------------------------------------===//
//                   ScalarEvolution Class Implementation
//===----------------------------------------------------------------------===//

ScalarEvolution::ScalarEvolution(Function &F, TargetLibraryInfo &TLI,
                                 AssumptionCache &AC, DominatorTree &DT,
                                 LoopInfo &LI)
    : F(F), DL(F.getDataLayout()), TLI(TLI), AC(AC), DT(DT), LI(LI),
      CouldNotCompute(new SCEVCouldNotCompute()), ValuesAtScopes(64),
      LoopDispositions(64), BlockDispositions(64) {
  // To use guards for proving predicates, we need to scan every instruction in
  // relevant basic blocks, and not just terminators.  Doing this is a waste of
  // time if the IR does not actually contain any calls to
  // @llvm.experimental.guard, so do a quick check and remember this beforehand.
  //
  // This pessimizes the case where a pass that preserves ScalarEvolution wants
  // to _add_ guards to the module when there weren't any before, and wants
  // ScalarEvolution to optimize based on those guards.  For now we prefer to be
  // efficient in lieu of being smart in that rather obscure case.

  auto *GuardDecl = F.getParent()->getFunction(
      Intrinsic::getName(Intrinsic::experimental_guard));
  HasGuards = GuardDecl && !GuardDecl->use_empty();
}

ScalarEvolution::ScalarEvolution(ScalarEvolution &&Arg)
    : F(Arg.F), DL(Arg.DL), HasGuards(Arg.HasGuards), TLI(Arg.TLI), AC(Arg.AC),
      DT(Arg.DT), LI(Arg.LI), CouldNotCompute(std::move(Arg.CouldNotCompute)),
      ValueExprMap(std::move(Arg.ValueExprMap)),
      PendingLoopPredicates(std::move(Arg.PendingLoopPredicates)),
      PendingPhiRanges(std::move(Arg.PendingPhiRanges)),
      PendingMerges(std::move(Arg.PendingMerges)),
      ConstantMultipleCache(std::move(Arg.ConstantMultipleCache)),
      BackedgeTakenCounts(std::move(Arg.BackedgeTakenCounts)),
      PredicatedBackedgeTakenCounts(
          std::move(Arg.PredicatedBackedgeTakenCounts)),
      BECountUsers(std::move(Arg.BECountUsers)),
      ConstantEvolutionLoopExitValue(
          std::move(Arg.ConstantEvolutionLoopExitValue)),
      ValuesAtScopes(std::move(Arg.ValuesAtScopes)),
      ValuesAtScopesUsers(std::move(Arg.ValuesAtScopesUsers)),
      LoopDispositions(std::move(Arg.LoopDispositions)),
      LoopPropertiesCache(std::move(Arg.LoopPropertiesCache)),
      BlockDispositions(std::move(Arg.BlockDispositions)),
      SCEVUsers(std::move(Arg.SCEVUsers)),
      UnsignedRanges(std::move(Arg.UnsignedRanges)),
      SignedRanges(std::move(Arg.SignedRanges)),
      UniqueSCEVs(std::move(Arg.UniqueSCEVs)),
      UniquePreds(std::move(Arg.UniquePreds)),
      SCEVAllocator(std::move(Arg.SCEVAllocator)),
      LoopUsers(std::move(Arg.LoopUsers)),
      PredicatedSCEVRewrites(std::move(Arg.PredicatedSCEVRewrites)),
      FirstUnknown(Arg.FirstUnknown) {
  Arg.FirstUnknown = nullptr;
}

ScalarEvolution::~ScalarEvolution() {
  // Iterate through all the SCEVUnknown instances and call their
  // destructors, so that they release their references to their values.
  for (SCEVUnknown *U = FirstUnknown; U;) {
    SCEVUnknown *Tmp = U;
    U = U->Next;
    Tmp->~SCEVUnknown();
  }
  FirstUnknown = nullptr;

  ExprValueMap.clear();
  ValueExprMap.clear();
  HasRecMap.clear();
  BackedgeTakenCounts.clear();
  PredicatedBackedgeTakenCounts.clear();

  assert(PendingLoopPredicates.empty() && "isImpliedCond garbage");
  assert(PendingPhiRanges.empty() && "getRangeRef garbage");
  assert(PendingMerges.empty() && "isImpliedViaMerge garbage");
  assert(!WalkingBEDominatingConds && "isLoopBackedgeGuardedByCond garbage!");
  assert(!ProvingSplitPredicate && "ProvingSplitPredicate garbage!");
}

bool ScalarEvolution::hasLoopInvariantBackedgeTakenCount(const Loop *L) {
  return !isa<SCEVCouldNotCompute>(getBackedgeTakenCount(L));
}

/// When printing a top-level SCEV for trip counts, it's helpful to include
/// a type for constants which are otherwise hard to disambiguate.
static void PrintSCEVWithTypeHint(raw_ostream &OS, const SCEV* S) {
  if (isa<SCEVConstant>(S))
    OS << *S->getType() << " ";
  OS << *S;
}

static void PrintLoopInfo(raw_ostream &OS, ScalarEvolution *SE,
                          const Loop *L) {
  // Print all inner loops first
  for (Loop *I : *L)
    PrintLoopInfo(OS, SE, I);

  OS << "Loop ";
  L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
  OS << ": ";

  SmallVector<BasicBlock *, 8> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  if (ExitingBlocks.size() != 1)
    OS << "<multiple exits> ";

  auto *BTC = SE->getBackedgeTakenCount(L);
  if (!isa<SCEVCouldNotCompute>(BTC)) {
    OS << "backedge-taken count is ";
    PrintSCEVWithTypeHint(OS, BTC);
  } else
    OS << "Unpredictable backedge-taken count.";
  OS << "\n";

  if (ExitingBlocks.size() > 1)
    for (BasicBlock *ExitingBlock : ExitingBlocks) {
      OS << "  exit count for " << ExitingBlock->getName() << ": ";
      PrintSCEVWithTypeHint(OS, SE->getExitCount(L, ExitingBlock));
      OS << "\n";
    }

  OS << "Loop ";
  L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
  OS << ": ";

  auto *ConstantBTC = SE->getConstantMaxBackedgeTakenCount(L);
  if (!isa<SCEVCouldNotCompute>(ConstantBTC)) {
    OS << "constant max backedge-taken count is ";
    PrintSCEVWithTypeHint(OS, ConstantBTC);
    if (SE->isBackedgeTakenCountMaxOrZero(L))
      OS << ", actual taken count either this or zero.";
  } else {
    OS << "Unpredictable constant max backedge-taken count. ";
  }

  OS << "\n"
        "Loop ";
  L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
  OS << ": ";

  auto *SymbolicBTC = SE->getSymbolicMaxBackedgeTakenCount(L);
  if (!isa<SCEVCouldNotCompute>(SymbolicBTC)) {
    OS << "symbolic max backedge-taken count is ";
    PrintSCEVWithTypeHint(OS, SymbolicBTC);
    if (SE->isBackedgeTakenCountMaxOrZero(L))
      OS << ", actual taken count either this or zero.";
  } else {
    OS << "Unpredictable symbolic max backedge-taken count. ";
  }
  OS << "\n";

  if (ExitingBlocks.size() > 1)
    for (BasicBlock *ExitingBlock : ExitingBlocks) {
      OS << "  symbolic max exit count for " << ExitingBlock->getName() << ": ";
      auto *ExitBTC = SE->getExitCount(L, ExitingBlock,
                                       ScalarEvolution::SymbolicMaximum);
      PrintSCEVWithTypeHint(OS, ExitBTC);
      OS << "\n";
    }

  SmallVector<const SCEVPredicate *, 4> Preds;
  auto *PBT = SE->getPredicatedBackedgeTakenCount(L, Preds);
  if (PBT != BTC || !Preds.empty()) {
    OS << "Loop ";
    L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
    OS << ": ";
    if (!isa<SCEVCouldNotCompute>(PBT)) {
      OS << "Predicated backedge-taken count is ";
      PrintSCEVWithTypeHint(OS, PBT);
    } else
      OS << "Unpredictable predicated backedge-taken count.";
    OS << "\n";
    OS << " Predicates:\n";
    for (const auto *P : Preds)
      P->print(OS, 4);
  }

  Preds.clear();
  auto *PredSymbolicMax =
      SE->getPredicatedSymbolicMaxBackedgeTakenCount(L, Preds);
  if (SymbolicBTC != PredSymbolicMax) {
    OS << "Loop ";
    L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
    OS << ": ";
    if (!isa<SCEVCouldNotCompute>(PredSymbolicMax)) {
      OS << "Predicated symbolic max backedge-taken count is ";
      PrintSCEVWithTypeHint(OS, PredSymbolicMax);
    } else
      OS << "Unpredictable predicated symbolic max backedge-taken count.";
    OS << "\n";
    OS << " Predicates:\n";
    for (const auto *P : Preds)
      P->print(OS, 4);
  }

  if (SE->hasLoopInvariantBackedgeTakenCount(L)) {
    OS << "Loop ";
    L->getHeader()->printAsOperand(OS, /*PrintType=*/false);
    OS << ": ";
    OS << "Trip multiple is " << SE->getSmallConstantTripMultiple(L) << "\n";
  }
}

namespace llvm {
raw_ostream &operator<<(raw_ostream &OS, ScalarEvolution::LoopDisposition LD) {
  switch (LD) {
  case ScalarEvolution::LoopVariant:
    OS << "Variant";
    break;
  case ScalarEvolution::LoopInvariant:
    OS << "Invariant";
    break;
  case ScalarEvolution::LoopComputable:
    OS << "Computable";
    break;
  }
  return OS;
}

raw_ostream &operator<<(raw_ostream &OS, ScalarEvolution::BlockDisposition BD) {
  switch (BD) {
  case ScalarEvolution::DoesNotDominateBlock:
    OS << "DoesNotDominate";
    break;
  case ScalarEvolution::DominatesBlock:
    OS << "Dominates";
    break;
  case ScalarEvolution::ProperlyDominatesBlock:
    OS << "ProperlyDominates";
    break;
  }
  return OS;
}
} // namespace llvm

void ScalarEvolution::print(raw_ostream &OS) const {
  // ScalarEvolution's implementation of the print method is to print
  // out SCEV values of all instructions that are interesting. Doing
  // this potentially causes it to create new SCEV objects though,
  // which technically conflicts with the const qualifier. This isn't
  // observable from outside the class though, so casting away the
  // const isn't dangerous.
  ScalarEvolution &SE = *const_cast<ScalarEvolution *>(this);

  if (ClassifyExpressions) {
    OS << "Classifying expressions for: ";
    F.printAsOperand(OS, /*PrintType=*/false);
    OS << "\n";
    for (Instruction &I : instructions(F))
      if (isSCEVable(I.getType()) && !isa<CmpInst>(I)) {
        OS << I << '\n';
        OS << "  -->  ";
        const SCEV *SV = SE.getSCEV(&I);
        SV->print(OS);
        if (!isa<SCEVCouldNotCompute>(SV)) {
          OS << " U: ";
          SE.getUnsignedRange(SV).print(OS);
          OS << " S: ";
          SE.getSignedRange(SV).print(OS);
        }

        const Loop *L = LI.getLoopFor(I.getParent());

        const SCEV *AtUse = SE.getSCEVAtScope(SV, L);
        if (AtUse != SV) {
          OS << "  -->  ";
          AtUse->print(OS);
          if (!isa<SCEVCouldNotCompute>(AtUse)) {
            OS << " U: ";
            SE.getUnsignedRange(AtUse).print(OS);
            OS << " S: ";
            SE.getSignedRange(AtUse).print(OS);
          }
        }

        if (L) {
          OS << "\t\t" "Exits: ";
          const SCEV *ExitValue = SE.getSCEVAtScope(SV, L->getParentLoop());
          if (!SE.isLoopInvariant(ExitValue, L)) {
            OS << "<<Unknown>>";
          } else {
            OS << *ExitValue;
          }

          bool First = true;
          for (const auto *Iter = L; Iter; Iter = Iter->getParentLoop()) {
            if (First) {
              OS << "\t\t" "LoopDispositions: { ";
              First = false;
            } else {
              OS << ", ";
            }

            Iter->getHeader()->printAsOperand(OS, /*PrintType=*/false);
            OS << ": " << SE.getLoopDisposition(SV, Iter);
          }

          for (const auto *InnerL : depth_first(L)) {
            if (InnerL == L)
              continue;
            if (First) {
              OS << "\t\t" "LoopDispositions: { ";
              First = false;
            } else {
              OS << ", ";
            }

            InnerL->getHeader()->printAsOperand(OS, /*PrintType=*/false);
            OS << ": " << SE.getLoopDisposition(SV, InnerL);
          }

          OS << " }";
        }

        OS << "\n";
      }
  }

  OS << "Determining loop execution counts for: ";
  F.printAsOperand(OS, /*PrintType=*/false);
  OS << "\n";
  for (Loop *I : LI)
    PrintLoopInfo(OS, &SE, I);
}

ScalarEvolution::LoopDisposition
ScalarEvolution::getLoopDisposition(const SCEV *S, const Loop *L) {
  auto &Values = LoopDispositions[S];
  for (auto &V : Values) {
    if (V.getPointer() == L)
      return V.getInt();
  }
  Values.emplace_back(L, LoopVariant);
  LoopDisposition D = computeLoopDisposition(S, L);
  auto &Values2 = LoopDispositions[S];
  for (auto &V : llvm::reverse(Values2)) {
    if (V.getPointer() == L) {
      V.setInt(D);
      break;
    }
  }
  return D;
}

ScalarEvolution::LoopDisposition
ScalarEvolution::computeLoopDisposition(const SCEV *S, const Loop *L) {
  switch (S->getSCEVType()) {
  case scConstant:
  case scVScale:
    return LoopInvariant;
  case scAddRecExpr: {
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(S);

    // If L is the addrec's loop, it's computable.
    if (AR->getLoop() == L)
      return LoopComputable;

    // Add recurrences are never invariant in the function-body (null loop).
    if (!L)
      return LoopVariant;

    // Everything that is not defined at loop entry is variant.
    if (DT.dominates(L->getHeader(), AR->getLoop()->getHeader()))
      return LoopVariant;
    assert(!L->contains(AR->getLoop()) && "Containing loop's header does not"
           " dominate the contained loop's header?");

    // This recurrence is invariant w.r.t. L if AR's loop contains L.
    if (AR->getLoop()->contains(L))
      return LoopInvariant;

    // This recurrence is variant w.r.t. L if any of its operands
    // are variant.
    for (const auto *Op : AR->operands())
      if (!isLoopInvariant(Op, L))
        return LoopVariant;

    // Otherwise it's loop-invariant.
    return LoopInvariant;
  }
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
  case scPtrToInt:
  case scAddExpr:
  case scMulExpr:
  case scUDivExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr: {
    bool HasVarying = false;
    for (const auto *Op : S->operands()) {
      LoopDisposition D = getLoopDisposition(Op, L);
      if (D == LoopVariant)
        return LoopVariant;
      if (D == LoopComputable)
        HasVarying = true;
    }
    return HasVarying ? LoopComputable : LoopInvariant;
  }
  case scUnknown:
    // All non-instruction values are loop invariant.  All instructions are loop
    // invariant if they are not contained in the specified loop.
    // Instructions are never considered invariant in the function body
    // (null loop) because they are defined within the "loop".
    if (auto *I = dyn_cast<Instruction>(cast<SCEVUnknown>(S)->getValue()))
      return (L && !L->contains(I)) ? LoopInvariant : LoopVariant;
    return LoopInvariant;
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

bool ScalarEvolution::isLoopInvariant(const SCEV *S, const Loop *L) {
  return getLoopDisposition(S, L) == LoopInvariant;
}

bool ScalarEvolution::hasComputableLoopEvolution(const SCEV *S, const Loop *L) {
  return getLoopDisposition(S, L) == LoopComputable;
}

ScalarEvolution::BlockDisposition
ScalarEvolution::getBlockDisposition(const SCEV *S, const BasicBlock *BB) {
  auto &Values = BlockDispositions[S];
  for (auto &V : Values) {
    if (V.getPointer() == BB)
      return V.getInt();
  }
  Values.emplace_back(BB, DoesNotDominateBlock);
  BlockDisposition D = computeBlockDisposition(S, BB);
  auto &Values2 = BlockDispositions[S];
  for (auto &V : llvm::reverse(Values2)) {
    if (V.getPointer() == BB) {
      V.setInt(D);
      break;
    }
  }
  return D;
}

ScalarEvolution::BlockDisposition
ScalarEvolution::computeBlockDisposition(const SCEV *S, const BasicBlock *BB) {
  switch (S->getSCEVType()) {
  case scConstant:
  case scVScale:
    return ProperlyDominatesBlock;
  case scAddRecExpr: {
    // This uses a "dominates" query instead of "properly dominates" query
    // to test for proper dominance too, because the instruction which
    // produces the addrec's value is a PHI, and a PHI effectively properly
    // dominates its entire containing block.
    const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(S);
    if (!DT.dominates(AR->getLoop()->getHeader(), BB))
      return DoesNotDominateBlock;

    // Fall through into SCEVNAryExpr handling.
    [[fallthrough]];
  }
  case scTruncate:
  case scZeroExtend:
  case scSignExtend:
  case scPtrToInt:
  case scAddExpr:
  case scMulExpr:
  case scUDivExpr:
  case scUMaxExpr:
  case scSMaxExpr:
  case scUMinExpr:
  case scSMinExpr:
  case scSequentialUMinExpr: {
    bool Proper = true;
    for (const SCEV *NAryOp : S->operands()) {
      BlockDisposition D = getBlockDisposition(NAryOp, BB);
      if (D == DoesNotDominateBlock)
        return DoesNotDominateBlock;
      if (D == DominatesBlock)
        Proper = false;
    }
    return Proper ? ProperlyDominatesBlock : DominatesBlock;
  }
  case scUnknown:
    if (Instruction *I =
          dyn_cast<Instruction>(cast<SCEVUnknown>(S)->getValue())) {
      if (I->getParent() == BB)
        return DominatesBlock;
      if (DT.properlyDominates(I->getParent(), BB))
        return ProperlyDominatesBlock;
      return DoesNotDominateBlock;
    }
    return ProperlyDominatesBlock;
  case scCouldNotCompute:
    llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
  }
  llvm_unreachable("Unknown SCEV kind!");
}

bool ScalarEvolution::dominates(const SCEV *S, const BasicBlock *BB) {
  return getBlockDisposition(S, BB) >= DominatesBlock;
}

bool ScalarEvolution::properlyDominates(const SCEV *S, const BasicBlock *BB) {
  return getBlockDisposition(S, BB) == ProperlyDominatesBlock;
}

bool ScalarEvolution::hasOperand(const SCEV *S, const SCEV *Op) const {
  return SCEVExprContains(S, [&](const SCEV *Expr) { return Expr == Op; });
}

void ScalarEvolution::forgetBackedgeTakenCounts(const Loop *L,
                                                bool Predicated) {
  auto &BECounts =
      Predicated ? PredicatedBackedgeTakenCounts : BackedgeTakenCounts;
  auto It = BECounts.find(L);
  if (It != BECounts.end()) {
    for (const ExitNotTakenInfo &ENT : It->second.ExitNotTaken) {
      for (const SCEV *S : {ENT.ExactNotTaken, ENT.SymbolicMaxNotTaken}) {
        if (!isa<SCEVConstant>(S)) {
          auto UserIt = BECountUsers.find(S);
          assert(UserIt != BECountUsers.end());
          UserIt->second.erase({L, Predicated});
        }
      }
    }
    BECounts.erase(It);
  }
}

void ScalarEvolution::forgetMemoizedResults(ArrayRef<const SCEV *> SCEVs) {
  SmallPtrSet<const SCEV *, 8> ToForget(SCEVs.begin(), SCEVs.end());
  SmallVector<const SCEV *, 8> Worklist(ToForget.begin(), ToForget.end());

  while (!Worklist.empty()) {
    const SCEV *Curr = Worklist.pop_back_val();
    auto Users = SCEVUsers.find(Curr);
    if (Users != SCEVUsers.end())
      for (const auto *User : Users->second)
        if (ToForget.insert(User).second)
          Worklist.push_back(User);
  }

  for (const auto *S : ToForget)
    forgetMemoizedResultsImpl(S);

  for (auto I = PredicatedSCEVRewrites.begin();
       I != PredicatedSCEVRewrites.end();) {
    std::pair<const SCEV *, const Loop *> Entry = I->first;
    if (ToForget.count(Entry.first))
      PredicatedSCEVRewrites.erase(I++);
    else
      ++I;
  }
}

void ScalarEvolution::forgetMemoizedResultsImpl(const SCEV *S) {
  LoopDispositions.erase(S);
  BlockDispositions.erase(S);
  UnsignedRanges.erase(S);
  SignedRanges.erase(S);
  HasRecMap.erase(S);
  ConstantMultipleCache.erase(S);

  if (auto *AR = dyn_cast<SCEVAddRecExpr>(S)) {
    UnsignedWrapViaInductionTried.erase(AR);
    SignedWrapViaInductionTried.erase(AR);
  }

  auto ExprIt = ExprValueMap.find(S);
  if (ExprIt != ExprValueMap.end()) {
    for (Value *V : ExprIt->second) {
      auto ValueIt = ValueExprMap.find_as(V);
      if (ValueIt != ValueExprMap.end())
        ValueExprMap.erase(ValueIt);
    }
    ExprValueMap.erase(ExprIt);
  }

  auto ScopeIt = ValuesAtScopes.find(S);
  if (ScopeIt != ValuesAtScopes.end()) {
    for (const auto &Pair : ScopeIt->second)
      if (!isa_and_nonnull<SCEVConstant>(Pair.second))
        llvm::erase(ValuesAtScopesUsers[Pair.second],
                    std::make_pair(Pair.first, S));
    ValuesAtScopes.erase(ScopeIt);
  }

  auto ScopeUserIt = ValuesAtScopesUsers.find(S);
  if (ScopeUserIt != ValuesAtScopesUsers.end()) {
    for (const auto &Pair : ScopeUserIt->second)
      llvm::erase(ValuesAtScopes[Pair.second], std::make_pair(Pair.first, S));
    ValuesAtScopesUsers.erase(ScopeUserIt);
  }

  auto BEUsersIt = BECountUsers.find(S);
  if (BEUsersIt != BECountUsers.end()) {
    // Work on a copy, as forgetBackedgeTakenCounts() will modify the original.
    auto Copy = BEUsersIt->second;
    for (const auto &Pair : Copy)
      forgetBackedgeTakenCounts(Pair.getPointer(), Pair.getInt());
    BECountUsers.erase(BEUsersIt);
  }

  auto FoldUser = FoldCacheUser.find(S);
  if (FoldUser != FoldCacheUser.end())
    for (auto &KV : FoldUser->second)
      FoldCache.erase(KV);
  FoldCacheUser.erase(S);
}

void
ScalarEvolution::getUsedLoops(const SCEV *S,
                              SmallPtrSetImpl<const Loop *> &LoopsUsed) {
  struct FindUsedLoops {
    FindUsedLoops(SmallPtrSetImpl<const Loop *> &LoopsUsed)
        : LoopsUsed(LoopsUsed) {}
    SmallPtrSetImpl<const Loop *> &LoopsUsed;
    bool follow(const SCEV *S) {
      if (auto *AR = dyn_cast<SCEVAddRecExpr>(S))
        LoopsUsed.insert(AR->getLoop());
      return true;
    }

    bool isDone() const { return false; }
  };

  FindUsedLoops F(LoopsUsed);
  SCEVTraversal<FindUsedLoops>(F).visitAll(S);
}

void ScalarEvolution::getReachableBlocks(
    SmallPtrSetImpl<BasicBlock *> &Reachable, Function &F) {
  SmallVector<BasicBlock *> Worklist;
  Worklist.push_back(&F.getEntryBlock());
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    if (!Reachable.insert(BB).second)
      continue;

    Value *Cond;
    BasicBlock *TrueBB, *FalseBB;
    if (match(BB->getTerminator(), m_Br(m_Value(Cond), m_BasicBlock(TrueBB),
                                        m_BasicBlock(FalseBB)))) {
      if (auto *C = dyn_cast<ConstantInt>(Cond)) {
        Worklist.push_back(C->isOne() ? TrueBB : FalseBB);
        continue;
      }

      if (auto *Cmp = dyn_cast<ICmpInst>(Cond)) {
        const SCEV *L = getSCEV(Cmp->getOperand(0));
        const SCEV *R = getSCEV(Cmp->getOperand(1));
        if (isKnownPredicateViaConstantRanges(Cmp->getPredicate(), L, R)) {
          Worklist.push_back(TrueBB);
          continue;
        }
        if (isKnownPredicateViaConstantRanges(Cmp->getInversePredicate(), L,
                                              R)) {
          Worklist.push_back(FalseBB);
          continue;
        }
      }
    }

    append_range(Worklist, successors(BB));
  }
}

void ScalarEvolution::verify() const {
  ScalarEvolution &SE = *const_cast<ScalarEvolution *>(this);
  ScalarEvolution SE2(F, TLI, AC, DT, LI);

  SmallVector<Loop *, 8> LoopStack(LI.begin(), LI.end());

  // Map's SCEV expressions from one ScalarEvolution "universe" to another.
  struct SCEVMapper : public SCEVRewriteVisitor<SCEVMapper> {
    SCEVMapper(ScalarEvolution &SE) : SCEVRewriteVisitor<SCEVMapper>(SE) {}

    const SCEV *visitConstant(const SCEVConstant *Constant) {
      return SE.getConstant(Constant->getAPInt());
    }

    const SCEV *visitUnknown(const SCEVUnknown *Expr) {
      return SE.getUnknown(Expr->getValue());
    }

    const SCEV *visitCouldNotCompute(const SCEVCouldNotCompute *Expr) {
      return SE.getCouldNotCompute();
    }
  };

  SCEVMapper SCM(SE2);
  SmallPtrSet<BasicBlock *, 16> ReachableBlocks;
  SE2.getReachableBlocks(ReachableBlocks, F);

  auto GetDelta = [&](const SCEV *Old, const SCEV *New) -> const SCEV * {
    if (containsUndefs(Old) || containsUndefs(New)) {
      // SCEV treats "undef" as an unknown but consistent value (i.e. it does
      // not propagate undef aggressively).  This means we can (and do) fail
      // verification in cases where a transform makes a value go from "undef"
      // to "undef+1" (say).  The transform is fine, since in both cases the
      // result is "undef", but SCEV thinks the value increased by 1.
      return nullptr;
    }

    // Unless VerifySCEVStrict is set, we only compare constant deltas.
    const SCEV *Delta = SE2.getMinusSCEV(Old, New);
    if (!VerifySCEVStrict && !isa<SCEVConstant>(Delta))
      return nullptr;

    return Delta;
  };

  while (!LoopStack.empty()) {
    auto *L = LoopStack.pop_back_val();
    llvm::append_range(LoopStack, *L);

    // Only verify BECounts in reachable loops. For an unreachable loop,
    // any BECount is legal.
    if (!ReachableBlocks.contains(L->getHeader()))
      continue;

    // Only verify cached BECounts. Computing new BECounts may change the
    // results of subsequent SCEV uses.
    auto It = BackedgeTakenCounts.find(L);
    if (It == BackedgeTakenCounts.end())
      continue;

    auto *CurBECount =
        SCM.visit(It->second.getExact(L, const_cast<ScalarEvolution *>(this)));
    auto *NewBECount = SE2.getBackedgeTakenCount(L);

    if (CurBECount == SE2.getCouldNotCompute() ||
        NewBECount == SE2.getCouldNotCompute()) {
      // NB! This situation is legal, but is very suspicious -- whatever pass
      // change the loop to make a trip count go from could not compute to
      // computable or vice-versa *should have* invalidated SCEV.  However, we
      // choose not to assert here (for now) since we don't want false
      // positives.
      continue;
    }

    if (SE.getTypeSizeInBits(CurBECount->getType()) >
        SE.getTypeSizeInBits(NewBECount->getType()))
      NewBECount = SE2.getZeroExtendExpr(NewBECount, CurBECount->getType());
    else if (SE.getTypeSizeInBits(CurBECount->getType()) <
             SE.getTypeSizeInBits(NewBECount->getType()))
      CurBECount = SE2.getZeroExtendExpr(CurBECount, NewBECount->getType());

    const SCEV *Delta = GetDelta(CurBECount, NewBECount);
    if (Delta && !Delta->isZero()) {
      dbgs() << "Trip Count for " << *L << " Changed!\n";
      dbgs() << "Old: " << *CurBECount << "\n";
      dbgs() << "New: " << *NewBECount << "\n";
      dbgs() << "Delta: " << *Delta << "\n";
      std::abort();
    }
  }

  // Collect all valid loops currently in LoopInfo.
  SmallPtrSet<Loop *, 32> ValidLoops;
  SmallVector<Loop *, 32> Worklist(LI.begin(), LI.end());
  while (!Worklist.empty()) {
    Loop *L = Worklist.pop_back_val();
    if (ValidLoops.insert(L).second)
      Worklist.append(L->begin(), L->end());
  }
  for (const auto &KV : ValueExprMap) {
#ifndef NDEBUG
    // Check for SCEV expressions referencing invalid/deleted loops.
    if (auto *AR = dyn_cast<SCEVAddRecExpr>(KV.second)) {
      assert(ValidLoops.contains(AR->getLoop()) &&
             "AddRec references invalid loop");
    }
#endif

    // Check that the value is also part of the reverse map.
    auto It = ExprValueMap.find(KV.second);
    if (It == ExprValueMap.end() || !It->second.contains(KV.first)) {
      dbgs() << "Value " << *KV.first
             << " is in ValueExprMap but not in ExprValueMap\n";
      std::abort();
    }

    if (auto *I = dyn_cast<Instruction>(&*KV.first)) {
      if (!ReachableBlocks.contains(I->getParent()))
        continue;
      const SCEV *OldSCEV = SCM.visit(KV.second);
      const SCEV *NewSCEV = SE2.getSCEV(I);
      const SCEV *Delta = GetDelta(OldSCEV, NewSCEV);
      if (Delta && !Delta->isZero()) {
        dbgs() << "SCEV for value " << *I << " changed!\n"
               << "Old: " << *OldSCEV << "\n"
               << "New: " << *NewSCEV << "\n"
               << "Delta: " << *Delta << "\n";
        std::abort();
      }
    }
  }

  for (const auto &KV : ExprValueMap) {
    for (Value *V : KV.second) {
      auto It = ValueExprMap.find_as(V);
      if (It == ValueExprMap.end()) {
        dbgs() << "Value " << *V
               << " is in ExprValueMap but not in ValueExprMap\n";
        std::abort();
      }
      if (It->second != KV.first) {
        dbgs() << "Value " << *V << " mapped to " << *It->second
               << " rather than " << *KV.first << "\n";
        std::abort();
      }
    }
  }

  // Verify integrity of SCEV users.
  for (const auto &S : UniqueSCEVs) {
    for (const auto *Op : S.operands()) {
      // We do not store dependencies of constants.
      if (isa<SCEVConstant>(Op))
        continue;
      auto It = SCEVUsers.find(Op);
      if (It != SCEVUsers.end() && It->second.count(&S))
        continue;
      dbgs() << "Use of operand  " << *Op << " by user " << S
             << " is not being tracked!\n";
      std::abort();
    }
  }

  // Verify integrity of ValuesAtScopes users.
  for (const auto &ValueAndVec : ValuesAtScopes) {
    const SCEV *Value = ValueAndVec.first;
    for (const auto &LoopAndValueAtScope : ValueAndVec.second) {
      const Loop *L = LoopAndValueAtScope.first;
      const SCEV *ValueAtScope = LoopAndValueAtScope.second;
      if (!isa<SCEVConstant>(ValueAtScope)) {
        auto It = ValuesAtScopesUsers.find(ValueAtScope);
        if (It != ValuesAtScopesUsers.end() &&
            is_contained(It->second, std::make_pair(L, Value)))
          continue;
        dbgs() << "Value: " << *Value << ", Loop: " << *L << ", ValueAtScope: "
               << *ValueAtScope << " missing in ValuesAtScopesUsers\n";
        std::abort();
      }
    }
  }

  for (const auto &ValueAtScopeAndVec : ValuesAtScopesUsers) {
    const SCEV *ValueAtScope = ValueAtScopeAndVec.first;
    for (const auto &LoopAndValue : ValueAtScopeAndVec.second) {
      const Loop *L = LoopAndValue.first;
      const SCEV *Value = LoopAndValue.second;
      assert(!isa<SCEVConstant>(Value));
      auto It = ValuesAtScopes.find(Value);
      if (It != ValuesAtScopes.end() &&
          is_contained(It->second, std::make_pair(L, ValueAtScope)))
        continue;
      dbgs() << "Value: " << *Value << ", Loop: " << *L << ", ValueAtScope: "
             << *ValueAtScope << " missing in ValuesAtScopes\n";
      std::abort();
    }
  }

  // Verify integrity of BECountUsers.
  auto VerifyBECountUsers = [&](bool Predicated) {
    auto &BECounts =
        Predicated ? PredicatedBackedgeTakenCounts : BackedgeTakenCounts;
    for (const auto &LoopAndBEInfo : BECounts) {
      for (const ExitNotTakenInfo &ENT : LoopAndBEInfo.second.ExitNotTaken) {
        for (const SCEV *S : {ENT.ExactNotTaken, ENT.SymbolicMaxNotTaken}) {
          if (!isa<SCEVConstant>(S)) {
            auto UserIt = BECountUsers.find(S);
            if (UserIt != BECountUsers.end() &&
                UserIt->second.contains({ LoopAndBEInfo.first, Predicated }))
              continue;
            dbgs() << "Value " << *S << " for loop " << *LoopAndBEInfo.first
                   << " missing from BECountUsers\n";
            std::abort();
          }
        }
      }
    }
  };
  VerifyBECountUsers(/* Predicated */ false);
  VerifyBECountUsers(/* Predicated */ true);

  // Verify intergity of loop disposition cache.
  for (auto &[S, Values] : LoopDispositions) {
    for (auto [Loop, CachedDisposition] : Values) {
      const auto RecomputedDisposition = SE2.getLoopDisposition(S, Loop);
      if (CachedDisposition != RecomputedDisposition) {
        dbgs() << "Cached disposition of " << *S << " for loop " << *Loop
               << " is incorrect: cached " << CachedDisposition << ", actual "
               << RecomputedDisposition << "\n";
        std::abort();
      }
    }
  }

  // Verify integrity of the block disposition cache.
  for (auto &[S, Values] : BlockDispositions) {
    for (auto [BB, CachedDisposition] : Values) {
      const auto RecomputedDisposition = SE2.getBlockDisposition(S, BB);
      if (CachedDisposition != RecomputedDisposition) {
        dbgs() << "Cached disposition of " << *S << " for block %"
               << BB->getName() << " is incorrect: cached " << CachedDisposition
               << ", actual " << RecomputedDisposition << "\n";
        std::abort();
      }
    }
  }

  // Verify FoldCache/FoldCacheUser caches.
  for (auto [FoldID, Expr] : FoldCache) {
    auto I = FoldCacheUser.find(Expr);
    if (I == FoldCacheUser.end()) {
      dbgs() << "Missing entry in FoldCacheUser for cached expression " << *Expr
             << "!\n";
      std::abort();
    }
    if (!is_contained(I->second, FoldID)) {
      dbgs() << "Missing FoldID in cached users of " << *Expr << "!\n";
      std::abort();
    }
  }
  for (auto [Expr, IDs] : FoldCacheUser) {
    for (auto &FoldID : IDs) {
      auto I = FoldCache.find(FoldID);
      if (I == FoldCache.end()) {
        dbgs() << "Missing entry in FoldCache for expression " << *Expr
               << "!\n";
        std::abort();
      }
      if (I->second != Expr) {
        dbgs() << "Entry in FoldCache doesn't match FoldCacheUser: "
               << *I->second << " != " << *Expr << "!\n";
        std::abort();
      }
    }
  }

  // Verify that ConstantMultipleCache computations are correct. We check that
  // cached multiples and recomputed multiples are multiples of each other to
  // verify correctness. It is possible that a recomputed multiple is different
  // from the cached multiple due to strengthened no wrap flags or changes in
  // KnownBits computations.
  for (auto [S, Multiple] : ConstantMultipleCache) {
    APInt RecomputedMultiple = SE2.getConstantMultiple(S);
    if ((Multiple != 0 && RecomputedMultiple != 0 &&
         Multiple.urem(RecomputedMultiple) != 0 &&
         RecomputedMultiple.urem(Multiple) != 0)) {
      dbgs() << "Incorrect cached computation in ConstantMultipleCache for "
             << *S << " : Computed " << RecomputedMultiple
             << " but cache contains " << Multiple << "!\n";
      std::abort();
    }
  }
}

bool ScalarEvolution::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  // Invalidate the ScalarEvolution object whenever it isn't preserved or one
  // of its dependencies is invalidated.
  auto PAC = PA.getChecker<ScalarEvolutionAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>()) ||
         Inv.invalidate<AssumptionAnalysis>(F, PA) ||
         Inv.invalidate<DominatorTreeAnalysis>(F, PA) ||
         Inv.invalidate<LoopAnalysis>(F, PA);
}

AnalysisKey ScalarEvolutionAnalysis::Key;

ScalarEvolution ScalarEvolutionAnalysis::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &LI = AM.getResult<LoopAnalysis>(F);
  return ScalarEvolution(F, TLI, AC, DT, LI);
}

PreservedAnalyses
ScalarEvolutionVerifierPass::run(Function &F, FunctionAnalysisManager &AM) {
  AM.getResult<ScalarEvolutionAnalysis>(F).verify();
  return PreservedAnalyses::all();
}

PreservedAnalyses
ScalarEvolutionPrinterPass::run(Function &F, FunctionAnalysisManager &AM) {
  // For compatibility with opt's -analyze feature under legacy pass manager
  // which was not ported to NPM. This keeps tests using
  // update_analyze_test_checks.py working.
  OS << "Printing analysis 'Scalar Evolution Analysis' for function '"
     << F.getName() << "':\n";
  AM.getResult<ScalarEvolutionAnalysis>(F).print(OS);
  return PreservedAnalyses::all();
}

INITIALIZE_PASS_BEGIN(ScalarEvolutionWrapperPass, "scalar-evolution",
                      "Scalar Evolution Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(ScalarEvolutionWrapperPass, "scalar-evolution",
                    "Scalar Evolution Analysis", false, true)

char ScalarEvolutionWrapperPass::ID = 0;

ScalarEvolutionWrapperPass::ScalarEvolutionWrapperPass() : FunctionPass(ID) {
  initializeScalarEvolutionWrapperPassPass(*PassRegistry::getPassRegistry());
}

bool ScalarEvolutionWrapperPass::runOnFunction(Function &F) {
  SE.reset(new ScalarEvolution(
      F, getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F),
      getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F),
      getAnalysis<DominatorTreeWrapperPass>().getDomTree(),
      getAnalysis<LoopInfoWrapperPass>().getLoopInfo()));
  return false;
}

void ScalarEvolutionWrapperPass::releaseMemory() { SE.reset(); }

void ScalarEvolutionWrapperPass::print(raw_ostream &OS, const Module *) const {
  SE->print(OS);
}

void ScalarEvolutionWrapperPass::verifyAnalysis() const {
  if (!VerifySCEV)
    return;

  SE->verify();
}

void ScalarEvolutionWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<AssumptionCacheTracker>();
  AU.addRequiredTransitive<LoopInfoWrapperPass>();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<TargetLibraryInfoWrapperPass>();
}

const SCEVPredicate *ScalarEvolution::getEqualPredicate(const SCEV *LHS,
                                                        const SCEV *RHS) {
  return getComparePredicate(ICmpInst::ICMP_EQ, LHS, RHS);
}

const SCEVPredicate *
ScalarEvolution::getComparePredicate(const ICmpInst::Predicate Pred,
                                     const SCEV *LHS, const SCEV *RHS) {
  FoldingSetNodeID ID;
  assert(LHS->getType() == RHS->getType() &&
         "Type mismatch between LHS and RHS");
  // Unique this node based on the arguments
  ID.AddInteger(SCEVPredicate::P_Compare);
  ID.AddInteger(Pred);
  ID.AddPointer(LHS);
  ID.AddPointer(RHS);
  void *IP = nullptr;
  if (const auto *S = UniquePreds.FindNodeOrInsertPos(ID, IP))
    return S;
  SCEVComparePredicate *Eq = new (SCEVAllocator)
    SCEVComparePredicate(ID.Intern(SCEVAllocator), Pred, LHS, RHS);
  UniquePreds.InsertNode(Eq, IP);
  return Eq;
}

const SCEVPredicate *ScalarEvolution::getWrapPredicate(
    const SCEVAddRecExpr *AR,
    SCEVWrapPredicate::IncrementWrapFlags AddedFlags) {
  FoldingSetNodeID ID;
  // Unique this node based on the arguments
  ID.AddInteger(SCEVPredicate::P_Wrap);
  ID.AddPointer(AR);
  ID.AddInteger(AddedFlags);
  void *IP = nullptr;
  if (const auto *S = UniquePreds.FindNodeOrInsertPos(ID, IP))
    return S;
  auto *OF = new (SCEVAllocator)
      SCEVWrapPredicate(ID.Intern(SCEVAllocator), AR, AddedFlags);
  UniquePreds.InsertNode(OF, IP);
  return OF;
}

namespace {

class SCEVPredicateRewriter : public SCEVRewriteVisitor<SCEVPredicateRewriter> {
public:

  /// Rewrites \p S in the context of a loop L and the SCEV predication
  /// infrastructure.
  ///
  /// If \p Pred is non-null, the SCEV expression is rewritten to respect the
  /// equivalences present in \p Pred.
  ///
  /// If \p NewPreds is non-null, rewrite is free to add further predicates to
  /// \p NewPreds such that the result will be an AddRecExpr.
  static const SCEV *rewrite(const SCEV *S, const Loop *L, ScalarEvolution &SE,
                             SmallPtrSetImpl<const SCEVPredicate *> *NewPreds,
                             const SCEVPredicate *Pred) {
    SCEVPredicateRewriter Rewriter(L, SE, NewPreds, Pred);
    return Rewriter.visit(S);
  }

  const SCEV *visitUnknown(const SCEVUnknown *Expr) {
    if (Pred) {
      if (auto *U = dyn_cast<SCEVUnionPredicate>(Pred)) {
        for (const auto *Pred : U->getPredicates())
          if (const auto *IPred = dyn_cast<SCEVComparePredicate>(Pred))
            if (IPred->getLHS() == Expr &&
                IPred->getPredicate() == ICmpInst::ICMP_EQ)
              return IPred->getRHS();
      } else if (const auto *IPred = dyn_cast<SCEVComparePredicate>(Pred)) {
        if (IPred->getLHS() == Expr &&
            IPred->getPredicate() == ICmpInst::ICMP_EQ)
          return IPred->getRHS();
      }
    }
    return convertToAddRecWithPreds(Expr);
  }

  const SCEV *visitZeroExtendExpr(const SCEVZeroExtendExpr *Expr) {
    const SCEV *Operand = visit(Expr->getOperand());
    const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Operand);
    if (AR && AR->getLoop() == L && AR->isAffine()) {
      // This couldn't be folded because the operand didn't have the nuw
      // flag. Add the nusw flag as an assumption that we could make.
      const SCEV *Step = AR->getStepRecurrence(SE);
      Type *Ty = Expr->getType();
      if (addOverflowAssumption(AR, SCEVWrapPredicate::IncrementNUSW))
        return SE.getAddRecExpr(SE.getZeroExtendExpr(AR->getStart(), Ty),
                                SE.getSignExtendExpr(Step, Ty), L,
                                AR->getNoWrapFlags());
    }
    return SE.getZeroExtendExpr(Operand, Expr->getType());
  }

  const SCEV *visitSignExtendExpr(const SCEVSignExtendExpr *Expr) {
    const SCEV *Operand = visit(Expr->getOperand());
    const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(Operand);
    if (AR && AR->getLoop() == L && AR->isAffine()) {
      // This couldn't be folded because the operand didn't have the nsw
      // flag. Add the nssw flag as an assumption that we could make.
      const SCEV *Step = AR->getStepRecurrence(SE);
      Type *Ty = Expr->getType();
      if (addOverflowAssumption(AR, SCEVWrapPredicate::IncrementNSSW))
        return SE.getAddRecExpr(SE.getSignExtendExpr(AR->getStart(), Ty),
                                SE.getSignExtendExpr(Step, Ty), L,
                                AR->getNoWrapFlags());
    }
    return SE.getSignExtendExpr(Operand, Expr->getType());
  }

private:
  explicit SCEVPredicateRewriter(const Loop *L, ScalarEvolution &SE,
                        SmallPtrSetImpl<const SCEVPredicate *> *NewPreds,
                        const SCEVPredicate *Pred)
      : SCEVRewriteVisitor(SE), NewPreds(NewPreds), Pred(Pred), L(L) {}

  bool addOverflowAssumption(const SCEVPredicate *P) {
    if (!NewPreds) {
      // Check if we've already made this assumption.
      return Pred && Pred->implies(P);
    }
    NewPreds->insert(P);
    return true;
  }

  bool addOverflowAssumption(const SCEVAddRecExpr *AR,
                             SCEVWrapPredicate::IncrementWrapFlags AddedFlags) {
    auto *A = SE.getWrapPredicate(AR, AddedFlags);
    return addOverflowAssumption(A);
  }

  // If \p Expr represents a PHINode, we try to see if it can be represented
  // as an AddRec, possibly under a predicate (PHISCEVPred). If it is possible
  // to add this predicate as a runtime overflow check, we return the AddRec.
  // If \p Expr does not meet these conditions (is not a PHI node, or we
  // couldn't create an AddRec for it, or couldn't add the predicate), we just
  // return \p Expr.
  const SCEV *convertToAddRecWithPreds(const SCEVUnknown *Expr) {
    if (!isa<PHINode>(Expr->getValue()))
      return Expr;
    std::optional<
        std::pair<const SCEV *, SmallVector<const SCEVPredicate *, 3>>>
        PredicatedRewrite = SE.createAddRecFromPHIWithCasts(Expr);
    if (!PredicatedRewrite)
      return Expr;
    for (const auto *P : PredicatedRewrite->second){
      // Wrap predicates from outer loops are not supported.
      if (auto *WP = dyn_cast<const SCEVWrapPredicate>(P)) {
        if (L != WP->getExpr()->getLoop())
          return Expr;
      }
      if (!addOverflowAssumption(P))
        return Expr;
    }
    return PredicatedRewrite->first;
  }

  SmallPtrSetImpl<const SCEVPredicate *> *NewPreds;
  const SCEVPredicate *Pred;
  const Loop *L;
};

} // end anonymous namespace

const SCEV *
ScalarEvolution::rewriteUsingPredicate(const SCEV *S, const Loop *L,
                                       const SCEVPredicate &Preds) {
  return SCEVPredicateRewriter::rewrite(S, L, *this, nullptr, &Preds);
}

const SCEVAddRecExpr *ScalarEvolution::convertSCEVToAddRecWithPredicates(
    const SCEV *S, const Loop *L,
    SmallPtrSetImpl<const SCEVPredicate *> &Preds) {
  SmallPtrSet<const SCEVPredicate *, 4> TransformPreds;
  S = SCEVPredicateRewriter::rewrite(S, L, *this, &TransformPreds, nullptr);
  auto *AddRec = dyn_cast<SCEVAddRecExpr>(S);

  if (!AddRec)
    return nullptr;

  // Since the transformation was successful, we can now transfer the SCEV
  // predicates.
  for (const auto *P : TransformPreds)
    Preds.insert(P);

  return AddRec;
}

/// SCEV predicates
SCEVPredicate::SCEVPredicate(const FoldingSetNodeIDRef ID,
                             SCEVPredicateKind Kind)
    : FastID(ID), Kind(Kind) {}

SCEVComparePredicate::SCEVComparePredicate(const FoldingSetNodeIDRef ID,
                                   const ICmpInst::Predicate Pred,
                                   const SCEV *LHS, const SCEV *RHS)
  : SCEVPredicate(ID, P_Compare), Pred(Pred), LHS(LHS), RHS(RHS) {
  assert(LHS->getType() == RHS->getType() && "LHS and RHS types don't match");
  assert(LHS != RHS && "LHS and RHS are the same SCEV");
}

bool SCEVComparePredicate::implies(const SCEVPredicate *N) const {
  const auto *Op = dyn_cast<SCEVComparePredicate>(N);

  if (!Op)
    return false;

  if (Pred != ICmpInst::ICMP_EQ)
    return false;

  return Op->LHS == LHS && Op->RHS == RHS;
}

bool SCEVComparePredicate::isAlwaysTrue() const { return false; }

void SCEVComparePredicate::print(raw_ostream &OS, unsigned Depth) const {
  if (Pred == ICmpInst::ICMP_EQ)
    OS.indent(Depth) << "Equal predicate: " << *LHS << " == " << *RHS << "\n";
  else
    OS.indent(Depth) << "Compare predicate: " << *LHS << " " << Pred << ") "
                     << *RHS << "\n";

}

SCEVWrapPredicate::SCEVWrapPredicate(const FoldingSetNodeIDRef ID,
                                     const SCEVAddRecExpr *AR,
                                     IncrementWrapFlags Flags)
    : SCEVPredicate(ID, P_Wrap), AR(AR), Flags(Flags) {}

const SCEVAddRecExpr *SCEVWrapPredicate::getExpr() const { return AR; }

bool SCEVWrapPredicate::implies(const SCEVPredicate *N) const {
  const auto *Op = dyn_cast<SCEVWrapPredicate>(N);

  return Op && Op->AR == AR && setFlags(Flags, Op->Flags) == Flags;
}

bool SCEVWrapPredicate::isAlwaysTrue() const {
  SCEV::NoWrapFlags ScevFlags = AR->getNoWrapFlags();
  IncrementWrapFlags IFlags = Flags;

  if (ScalarEvolution::setFlags(ScevFlags, SCEV::FlagNSW) == ScevFlags)
    IFlags = clearFlags(IFlags, IncrementNSSW);

  return IFlags == IncrementAnyWrap;
}

void SCEVWrapPredicate::print(raw_ostream &OS, unsigned Depth) const {
  OS.indent(Depth) << *getExpr() << " Added Flags: ";
  if (SCEVWrapPredicate::IncrementNUSW & getFlags())
    OS << "<nusw>";
  if (SCEVWrapPredicate::IncrementNSSW & getFlags())
    OS << "<nssw>";
  OS << "\n";
}

SCEVWrapPredicate::IncrementWrapFlags
SCEVWrapPredicate::getImpliedFlags(const SCEVAddRecExpr *AR,
                                   ScalarEvolution &SE) {
  IncrementWrapFlags ImpliedFlags = IncrementAnyWrap;
  SCEV::NoWrapFlags StaticFlags = AR->getNoWrapFlags();

  // We can safely transfer the NSW flag as NSSW.
  if (ScalarEvolution::setFlags(StaticFlags, SCEV::FlagNSW) == StaticFlags)
    ImpliedFlags = IncrementNSSW;

  if (ScalarEvolution::setFlags(StaticFlags, SCEV::FlagNUW) == StaticFlags) {
    // If the increment is positive, the SCEV NUW flag will also imply the
    // WrapPredicate NUSW flag.
    if (const auto *Step = dyn_cast<SCEVConstant>(AR->getStepRecurrence(SE)))
      if (Step->getValue()->getValue().isNonNegative())
        ImpliedFlags = setFlags(ImpliedFlags, IncrementNUSW);
  }

  return ImpliedFlags;
}

/// Union predicates don't get cached so create a dummy set ID for it.
SCEVUnionPredicate::SCEVUnionPredicate(ArrayRef<const SCEVPredicate *> Preds)
  : SCEVPredicate(FoldingSetNodeIDRef(nullptr, 0), P_Union) {
  for (const auto *P : Preds)
    add(P);
}

bool SCEVUnionPredicate::isAlwaysTrue() const {
  return all_of(Preds,
                [](const SCEVPredicate *I) { return I->isAlwaysTrue(); });
}

bool SCEVUnionPredicate::implies(const SCEVPredicate *N) const {
  if (const auto *Set = dyn_cast<SCEVUnionPredicate>(N))
    return all_of(Set->Preds,
                  [this](const SCEVPredicate *I) { return this->implies(I); });

  return any_of(Preds,
                [N](const SCEVPredicate *I) { return I->implies(N); });
}

void SCEVUnionPredicate::print(raw_ostream &OS, unsigned Depth) const {
  for (const auto *Pred : Preds)
    Pred->print(OS, Depth);
}

void SCEVUnionPredicate::add(const SCEVPredicate *N) {
  if (const auto *Set = dyn_cast<SCEVUnionPredicate>(N)) {
    for (const auto *Pred : Set->Preds)
      add(Pred);
    return;
  }

  // Only add predicate if it is not already implied by this union predicate.
  if (!implies(N))
    Preds.push_back(N);
}

PredicatedScalarEvolution::PredicatedScalarEvolution(ScalarEvolution &SE,
                                                     Loop &L)
    : SE(SE), L(L) {
  SmallVector<const SCEVPredicate*, 4> Empty;
  Preds = std::make_unique<SCEVUnionPredicate>(Empty);
}

void ScalarEvolution::registerUser(const SCEV *User,
                                   ArrayRef<const SCEV *> Ops) {
  for (const auto *Op : Ops)
    // We do not expect that forgetting cached data for SCEVConstants will ever
    // open any prospects for sharpening or introduce any correctness issues,
    // so we don't bother storing their dependencies.
    if (!isa<SCEVConstant>(Op))
      SCEVUsers[Op].insert(User);
}

const SCEV *PredicatedScalarEvolution::getSCEV(Value *V) {
  const SCEV *Expr = SE.getSCEV(V);
  RewriteEntry &Entry = RewriteMap[Expr];

  // If we already have an entry and the version matches, return it.
  if (Entry.second && Generation == Entry.first)
    return Entry.second;

  // We found an entry but it's stale. Rewrite the stale entry
  // according to the current predicate.
  if (Entry.second)
    Expr = Entry.second;

  const SCEV *NewSCEV = SE.rewriteUsingPredicate(Expr, &L, *Preds);
  Entry = {Generation, NewSCEV};

  return NewSCEV;
}

const SCEV *PredicatedScalarEvolution::getBackedgeTakenCount() {
  if (!BackedgeCount) {
    SmallVector<const SCEVPredicate *, 4> Preds;
    BackedgeCount = SE.getPredicatedBackedgeTakenCount(&L, Preds);
    for (const auto *P : Preds)
      addPredicate(*P);
  }
  return BackedgeCount;
}

const SCEV *PredicatedScalarEvolution::getSymbolicMaxBackedgeTakenCount() {
  if (!SymbolicMaxBackedgeCount) {
    SmallVector<const SCEVPredicate *, 4> Preds;
    SymbolicMaxBackedgeCount =
        SE.getPredicatedSymbolicMaxBackedgeTakenCount(&L, Preds);
    for (const auto *P : Preds)
      addPredicate(*P);
  }
  return SymbolicMaxBackedgeCount;
}

void PredicatedScalarEvolution::addPredicate(const SCEVPredicate &Pred) {
  if (Preds->implies(&Pred))
    return;

  auto &OldPreds = Preds->getPredicates();
  SmallVector<const SCEVPredicate*, 4> NewPreds(OldPreds.begin(), OldPreds.end());
  NewPreds.push_back(&Pred);
  Preds = std::make_unique<SCEVUnionPredicate>(NewPreds);
  updateGeneration();
}

const SCEVPredicate &PredicatedScalarEvolution::getPredicate() const {
  return *Preds;
}

void PredicatedScalarEvolution::updateGeneration() {
  // If the generation number wrapped recompute everything.
  if (++Generation == 0) {
    for (auto &II : RewriteMap) {
      const SCEV *Rewritten = II.second.second;
      II.second = {Generation, SE.rewriteUsingPredicate(Rewritten, &L, *Preds)};
    }
  }
}

void PredicatedScalarEvolution::setNoOverflow(
    Value *V, SCEVWrapPredicate::IncrementWrapFlags Flags) {
  const SCEV *Expr = getSCEV(V);
  const auto *AR = cast<SCEVAddRecExpr>(Expr);

  auto ImpliedFlags = SCEVWrapPredicate::getImpliedFlags(AR, SE);

  // Clear the statically implied flags.
  Flags = SCEVWrapPredicate::clearFlags(Flags, ImpliedFlags);
  addPredicate(*SE.getWrapPredicate(AR, Flags));

  auto II = FlagsMap.insert({V, Flags});
  if (!II.second)
    II.first->second = SCEVWrapPredicate::setFlags(Flags, II.first->second);
}

bool PredicatedScalarEvolution::hasNoOverflow(
    Value *V, SCEVWrapPredicate::IncrementWrapFlags Flags) {
  const SCEV *Expr = getSCEV(V);
  const auto *AR = cast<SCEVAddRecExpr>(Expr);

  Flags = SCEVWrapPredicate::clearFlags(
      Flags, SCEVWrapPredicate::getImpliedFlags(AR, SE));

  auto II = FlagsMap.find(V);

  if (II != FlagsMap.end())
    Flags = SCEVWrapPredicate::clearFlags(Flags, II->second);

  return Flags == SCEVWrapPredicate::IncrementAnyWrap;
}

const SCEVAddRecExpr *PredicatedScalarEvolution::getAsAddRec(Value *V) {
  const SCEV *Expr = this->getSCEV(V);
  SmallPtrSet<const SCEVPredicate *, 4> NewPreds;
  auto *New = SE.convertSCEVToAddRecWithPredicates(Expr, &L, NewPreds);

  if (!New)
    return nullptr;

  for (const auto *P : NewPreds)
    addPredicate(*P);

  RewriteMap[SE.getSCEV(V)] = {Generation, New};
  return New;
}

PredicatedScalarEvolution::PredicatedScalarEvolution(
    const PredicatedScalarEvolution &Init)
  : RewriteMap(Init.RewriteMap), SE(Init.SE), L(Init.L),
    Preds(std::make_unique<SCEVUnionPredicate>(Init.Preds->getPredicates())),
    Generation(Init.Generation), BackedgeCount(Init.BackedgeCount) {
  for (auto I : Init.FlagsMap)
    FlagsMap.insert(I);
}

void PredicatedScalarEvolution::print(raw_ostream &OS, unsigned Depth) const {
  // For each block.
  for (auto *BB : L.getBlocks())
    for (auto &I : *BB) {
      if (!SE.isSCEVable(I.getType()))
        continue;

      auto *Expr = SE.getSCEV(&I);
      auto II = RewriteMap.find(Expr);

      if (II == RewriteMap.end())
        continue;

      // Don't print things that are not interesting.
      if (II->second.second == Expr)
        continue;

      OS.indent(Depth) << "[PSE]" << I << ":\n";
      OS.indent(Depth + 2) << *Expr << "\n";
      OS.indent(Depth + 2) << "--> " << *II->second.second << "\n";
    }
}

// Match the mathematical pattern A - (A / B) * B, where A and B can be
// arbitrary expressions. Also match zext (trunc A to iB) to iY, which is used
// for URem with constant power-of-2 second operands.
// It's not always easy, as A and B can be folded (imagine A is X / 2, and B is
// 4, A / B becomes X / 8).
bool ScalarEvolution::matchURem(const SCEV *Expr, const SCEV *&LHS,
                                const SCEV *&RHS) {
  if (Expr->getType()->isPointerTy())
    return false;

  // Try to match 'zext (trunc A to iB) to iY', which is used
  // for URem with constant power-of-2 second operands. Make sure the size of
  // the operand A matches the size of the whole expressions.
  if (const auto *ZExt = dyn_cast<SCEVZeroExtendExpr>(Expr))
    if (const auto *Trunc = dyn_cast<SCEVTruncateExpr>(ZExt->getOperand(0))) {
      LHS = Trunc->getOperand();
      // Bail out if the type of the LHS is larger than the type of the
      // expression for now.
      if (getTypeSizeInBits(LHS->getType()) >
          getTypeSizeInBits(Expr->getType()))
        return false;
      if (LHS->getType() != Expr->getType())
        LHS = getZeroExtendExpr(LHS, Expr->getType());
      RHS = getConstant(APInt(getTypeSizeInBits(Expr->getType()), 1)
                        << getTypeSizeInBits(Trunc->getType()));
      return true;
    }
  const auto *Add = dyn_cast<SCEVAddExpr>(Expr);
  if (Add == nullptr || Add->getNumOperands() != 2)
    return false;

  const SCEV *A = Add->getOperand(1);
  const auto *Mul = dyn_cast<SCEVMulExpr>(Add->getOperand(0));

  if (Mul == nullptr)
    return false;

  const auto MatchURemWithDivisor = [&](const SCEV *B) {
    // (SomeExpr + (-(SomeExpr / B) * B)).
    if (Expr == getURemExpr(A, B)) {
      LHS = A;
      RHS = B;
      return true;
    }
    return false;
  };

  // (SomeExpr + (-1 * (SomeExpr / B) * B)).
  if (Mul->getNumOperands() == 3 && isa<SCEVConstant>(Mul->getOperand(0)))
    return MatchURemWithDivisor(Mul->getOperand(1)) ||
           MatchURemWithDivisor(Mul->getOperand(2));

  // (SomeExpr + ((-SomeExpr / B) * B)) or (SomeExpr + ((SomeExpr / B) * -B)).
  if (Mul->getNumOperands() == 2)
    return MatchURemWithDivisor(Mul->getOperand(1)) ||
           MatchURemWithDivisor(Mul->getOperand(0)) ||
           MatchURemWithDivisor(getNegativeSCEV(Mul->getOperand(1))) ||
           MatchURemWithDivisor(getNegativeSCEV(Mul->getOperand(0)));
  return false;
}

ScalarEvolution::LoopGuards
ScalarEvolution::LoopGuards::collect(const Loop *L, ScalarEvolution &SE) {
  LoopGuards Guards(SE);
  SmallVector<const SCEV *> ExprsToRewrite;
  auto CollectCondition = [&](ICmpInst::Predicate Predicate, const SCEV *LHS,
                              const SCEV *RHS,
                              DenseMap<const SCEV *, const SCEV *>
                                  &RewriteMap) {
    // WARNING: It is generally unsound to apply any wrap flags to the proposed
    // replacement SCEV which isn't directly implied by the structure of that
    // SCEV.  In particular, using contextual facts to imply flags is *NOT*
    // legal.  See the scoping rules for flags in the header to understand why.

    // If LHS is a constant, apply information to the other expression.
    if (isa<SCEVConstant>(LHS)) {
      std::swap(LHS, RHS);
      Predicate = CmpInst::getSwappedPredicate(Predicate);
    }

    // Check for a condition of the form (-C1 + X < C2).  InstCombine will
    // create this form when combining two checks of the form (X u< C2 + C1) and
    // (X >=u C1).
    auto MatchRangeCheckIdiom = [&SE, Predicate, LHS, RHS, &RewriteMap,
                                 &ExprsToRewrite]() {
      auto *AddExpr = dyn_cast<SCEVAddExpr>(LHS);
      if (!AddExpr || AddExpr->getNumOperands() != 2)
        return false;

      auto *C1 = dyn_cast<SCEVConstant>(AddExpr->getOperand(0));
      auto *LHSUnknown = dyn_cast<SCEVUnknown>(AddExpr->getOperand(1));
      auto *C2 = dyn_cast<SCEVConstant>(RHS);
      if (!C1 || !C2 || !LHSUnknown)
        return false;

      auto ExactRegion =
          ConstantRange::makeExactICmpRegion(Predicate, C2->getAPInt())
              .sub(C1->getAPInt());

      // Bail out, unless we have a non-wrapping, monotonic range.
      if (ExactRegion.isWrappedSet() || ExactRegion.isFullSet())
        return false;
      auto I = RewriteMap.find(LHSUnknown);
      const SCEV *RewrittenLHS = I != RewriteMap.end() ? I->second : LHSUnknown;
      RewriteMap[LHSUnknown] = SE.getUMaxExpr(
          SE.getConstant(ExactRegion.getUnsignedMin()),
          SE.getUMinExpr(RewrittenLHS,
                         SE.getConstant(ExactRegion.getUnsignedMax())));
      ExprsToRewrite.push_back(LHSUnknown);
      return true;
    };
    if (MatchRangeCheckIdiom())
      return;

    // Return true if \p Expr is a MinMax SCEV expression with a non-negative
    // constant operand. If so, return in \p SCTy the SCEV type and in \p RHS
    // the non-constant operand and in \p LHS the constant operand.
    auto IsMinMaxSCEVWithNonNegativeConstant =
        [&](const SCEV *Expr, SCEVTypes &SCTy, const SCEV *&LHS,
            const SCEV *&RHS) {
          if (auto *MinMax = dyn_cast<SCEVMinMaxExpr>(Expr)) {
            if (MinMax->getNumOperands() != 2)
              return false;
            if (auto *C = dyn_cast<SCEVConstant>(MinMax->getOperand(0))) {
              if (C->getAPInt().isNegative())
                return false;
              SCTy = MinMax->getSCEVType();
              LHS = MinMax->getOperand(0);
              RHS = MinMax->getOperand(1);
              return true;
            }
          }
          return false;
        };

    // Checks whether Expr is a non-negative constant, and Divisor is a positive
    // constant, and returns their APInt in ExprVal and in DivisorVal.
    auto GetNonNegExprAndPosDivisor = [&](const SCEV *Expr, const SCEV *Divisor,
                                          APInt &ExprVal, APInt &DivisorVal) {
      auto *ConstExpr = dyn_cast<SCEVConstant>(Expr);
      auto *ConstDivisor = dyn_cast<SCEVConstant>(Divisor);
      if (!ConstExpr || !ConstDivisor)
        return false;
      ExprVal = ConstExpr->getAPInt();
      DivisorVal = ConstDivisor->getAPInt();
      return ExprVal.isNonNegative() && !DivisorVal.isNonPositive();
    };

    // Return a new SCEV that modifies \p Expr to the closest number divides by
    // \p Divisor and greater or equal than Expr.
    // For now, only handle constant Expr and Divisor.
    auto GetNextSCEVDividesByDivisor = [&](const SCEV *Expr,
                                           const SCEV *Divisor) {
      APInt ExprVal;
      APInt DivisorVal;
      if (!GetNonNegExprAndPosDivisor(Expr, Divisor, ExprVal, DivisorVal))
        return Expr;
      APInt Rem = ExprVal.urem(DivisorVal);
      if (!Rem.isZero())
        // return the SCEV: Expr + Divisor - Expr % Divisor
        return SE.getConstant(ExprVal + DivisorVal - Rem);
      return Expr;
    };

    // Return a new SCEV that modifies \p Expr to the closest number divides by
    // \p Divisor and less or equal than Expr.
    // For now, only handle constant Expr and Divisor.
    auto GetPreviousSCEVDividesByDivisor = [&](const SCEV *Expr,
                                               const SCEV *Divisor) {
      APInt ExprVal;
      APInt DivisorVal;
      if (!GetNonNegExprAndPosDivisor(Expr, Divisor, ExprVal, DivisorVal))
        return Expr;
      APInt Rem = ExprVal.urem(DivisorVal);
      // return the SCEV: Expr - Expr % Divisor
      return SE.getConstant(ExprVal - Rem);
    };

    // Apply divisibilty by \p Divisor on MinMaxExpr with constant values,
    // recursively. This is done by aligning up/down the constant value to the
    // Divisor.
    std::function<const SCEV *(const SCEV *, const SCEV *)>
        ApplyDivisibiltyOnMinMaxExpr = [&](const SCEV *MinMaxExpr,
                                           const SCEV *Divisor) {
          const SCEV *MinMaxLHS = nullptr, *MinMaxRHS = nullptr;
          SCEVTypes SCTy;
          if (!IsMinMaxSCEVWithNonNegativeConstant(MinMaxExpr, SCTy, MinMaxLHS,
                                                   MinMaxRHS))
            return MinMaxExpr;
          auto IsMin =
              isa<SCEVSMinExpr>(MinMaxExpr) || isa<SCEVUMinExpr>(MinMaxExpr);
          assert(SE.isKnownNonNegative(MinMaxLHS) &&
                 "Expected non-negative operand!");
          auto *DivisibleExpr =
              IsMin ? GetPreviousSCEVDividesByDivisor(MinMaxLHS, Divisor)
                    : GetNextSCEVDividesByDivisor(MinMaxLHS, Divisor);
          SmallVector<const SCEV *> Ops = {
              ApplyDivisibiltyOnMinMaxExpr(MinMaxRHS, Divisor), DivisibleExpr};
          return SE.getMinMaxExpr(SCTy, Ops);
        };

    // If we have LHS == 0, check if LHS is computing a property of some unknown
    // SCEV %v which we can rewrite %v to express explicitly.
    const SCEVConstant *RHSC = dyn_cast<SCEVConstant>(RHS);
    if (Predicate == CmpInst::ICMP_EQ && RHSC &&
        RHSC->getValue()->isNullValue()) {
      // If LHS is A % B, i.e. A % B == 0, rewrite A to (A /u B) * B to
      // explicitly express that.
      const SCEV *URemLHS = nullptr;
      const SCEV *URemRHS = nullptr;
      if (SE.matchURem(LHS, URemLHS, URemRHS)) {
        if (const SCEVUnknown *LHSUnknown = dyn_cast<SCEVUnknown>(URemLHS)) {
          auto I = RewriteMap.find(LHSUnknown);
          const SCEV *RewrittenLHS =
              I != RewriteMap.end() ? I->second : LHSUnknown;
          RewrittenLHS = ApplyDivisibiltyOnMinMaxExpr(RewrittenLHS, URemRHS);
          const auto *Multiple =
              SE.getMulExpr(SE.getUDivExpr(RewrittenLHS, URemRHS), URemRHS);
          RewriteMap[LHSUnknown] = Multiple;
          ExprsToRewrite.push_back(LHSUnknown);
          return;
        }
      }
    }

    // Do not apply information for constants or if RHS contains an AddRec.
    if (isa<SCEVConstant>(LHS) || SE.containsAddRecurrence(RHS))
      return;

    // If RHS is SCEVUnknown, make sure the information is applied to it.
    if (!isa<SCEVUnknown>(LHS) && isa<SCEVUnknown>(RHS)) {
      std::swap(LHS, RHS);
      Predicate = CmpInst::getSwappedPredicate(Predicate);
    }

    // Puts rewrite rule \p From -> \p To into the rewrite map. Also if \p From
    // and \p FromRewritten are the same (i.e. there has been no rewrite
    // registered for \p From), then puts this value in the list of rewritten
    // expressions.
    auto AddRewrite = [&](const SCEV *From, const SCEV *FromRewritten,
                          const SCEV *To) {
      if (From == FromRewritten)
        ExprsToRewrite.push_back(From);
      RewriteMap[From] = To;
    };

    // Checks whether \p S has already been rewritten. In that case returns the
    // existing rewrite because we want to chain further rewrites onto the
    // already rewritten value. Otherwise returns \p S.
    auto GetMaybeRewritten = [&](const SCEV *S) {
      auto I = RewriteMap.find(S);
      return I != RewriteMap.end() ? I->second : S;
    };

    // Check for the SCEV expression (A /u B) * B while B is a constant, inside
    // \p Expr. The check is done recuresively on \p Expr, which is assumed to
    // be a composition of Min/Max SCEVs. Return whether the SCEV expression (A
    // /u B) * B was found, and return the divisor B in \p DividesBy. For
    // example, if Expr = umin (umax ((A /u 8) * 8, 16), 64), return true since
    // (A /u 8) * 8 matched the pattern, and return the constant SCEV 8 in \p
    // DividesBy.
    std::function<bool(const SCEV *, const SCEV *&)> HasDivisibiltyInfo =
        [&](const SCEV *Expr, const SCEV *&DividesBy) {
          if (auto *Mul = dyn_cast<SCEVMulExpr>(Expr)) {
            if (Mul->getNumOperands() != 2)
              return false;
            auto *MulLHS = Mul->getOperand(0);
            auto *MulRHS = Mul->getOperand(1);
            if (isa<SCEVConstant>(MulLHS))
              std::swap(MulLHS, MulRHS);
            if (auto *Div = dyn_cast<SCEVUDivExpr>(MulLHS))
              if (Div->getOperand(1) == MulRHS) {
                DividesBy = MulRHS;
                return true;
              }
          }
          if (auto *MinMax = dyn_cast<SCEVMinMaxExpr>(Expr))
            return HasDivisibiltyInfo(MinMax->getOperand(0), DividesBy) ||
                   HasDivisibiltyInfo(MinMax->getOperand(1), DividesBy);
          return false;
        };

    // Return true if Expr known to divide by \p DividesBy.
    std::function<bool(const SCEV *, const SCEV *&)> IsKnownToDivideBy =
        [&](const SCEV *Expr, const SCEV *DividesBy) {
          if (SE.getURemExpr(Expr, DividesBy)->isZero())
            return true;
          if (auto *MinMax = dyn_cast<SCEVMinMaxExpr>(Expr))
            return IsKnownToDivideBy(MinMax->getOperand(0), DividesBy) &&
                   IsKnownToDivideBy(MinMax->getOperand(1), DividesBy);
          return false;
        };

    const SCEV *RewrittenLHS = GetMaybeRewritten(LHS);
    const SCEV *DividesBy = nullptr;
    if (HasDivisibiltyInfo(RewrittenLHS, DividesBy))
      // Check that the whole expression is divided by DividesBy
      DividesBy =
          IsKnownToDivideBy(RewrittenLHS, DividesBy) ? DividesBy : nullptr;

    // Collect rewrites for LHS and its transitive operands based on the
    // condition.
    // For min/max expressions, also apply the guard to its operands:
    //  'min(a, b) >= c'   ->   '(a >= c) and (b >= c)',
    //  'min(a, b) >  c'   ->   '(a >  c) and (b >  c)',
    //  'max(a, b) <= c'   ->   '(a <= c) and (b <= c)',
    //  'max(a, b) <  c'   ->   '(a <  c) and (b <  c)'.

    // We cannot express strict predicates in SCEV, so instead we replace them
    // with non-strict ones against plus or minus one of RHS depending on the
    // predicate.
    const SCEV *One = SE.getOne(RHS->getType());
    switch (Predicate) {
      case CmpInst::ICMP_ULT:
        if (RHS->getType()->isPointerTy())
          return;
        RHS = SE.getUMaxExpr(RHS, One);
        [[fallthrough]];
      case CmpInst::ICMP_SLT: {
        RHS = SE.getMinusSCEV(RHS, One);
        RHS = DividesBy ? GetPreviousSCEVDividesByDivisor(RHS, DividesBy) : RHS;
        break;
      }
      case CmpInst::ICMP_UGT:
      case CmpInst::ICMP_SGT:
        RHS = SE.getAddExpr(RHS, One);
        RHS = DividesBy ? GetNextSCEVDividesByDivisor(RHS, DividesBy) : RHS;
        break;
      case CmpInst::ICMP_ULE:
      case CmpInst::ICMP_SLE:
        RHS = DividesBy ? GetPreviousSCEVDividesByDivisor(RHS, DividesBy) : RHS;
        break;
      case CmpInst::ICMP_UGE:
      case CmpInst::ICMP_SGE:
        RHS = DividesBy ? GetNextSCEVDividesByDivisor(RHS, DividesBy) : RHS;
        break;
      default:
        break;
    }

    SmallVector<const SCEV *, 16> Worklist(1, LHS);
    SmallPtrSet<const SCEV *, 16> Visited;

    auto EnqueueOperands = [&Worklist](const SCEVNAryExpr *S) {
      append_range(Worklist, S->operands());
    };

    while (!Worklist.empty()) {
      const SCEV *From = Worklist.pop_back_val();
      if (isa<SCEVConstant>(From))
        continue;
      if (!Visited.insert(From).second)
        continue;
      const SCEV *FromRewritten = GetMaybeRewritten(From);
      const SCEV *To = nullptr;

      switch (Predicate) {
      case CmpInst::ICMP_ULT:
      case CmpInst::ICMP_ULE:
        To = SE.getUMinExpr(FromRewritten, RHS);
        if (auto *UMax = dyn_cast<SCEVUMaxExpr>(FromRewritten))
          EnqueueOperands(UMax);
        break;
      case CmpInst::ICMP_SLT:
      case CmpInst::ICMP_SLE:
        To = SE.getSMinExpr(FromRewritten, RHS);
        if (auto *SMax = dyn_cast<SCEVSMaxExpr>(FromRewritten))
          EnqueueOperands(SMax);
        break;
      case CmpInst::ICMP_UGT:
      case CmpInst::ICMP_UGE:
        To = SE.getUMaxExpr(FromRewritten, RHS);
        if (auto *UMin = dyn_cast<SCEVUMinExpr>(FromRewritten))
          EnqueueOperands(UMin);
        break;
      case CmpInst::ICMP_SGT:
      case CmpInst::ICMP_SGE:
        To = SE.getSMaxExpr(FromRewritten, RHS);
        if (auto *SMin = dyn_cast<SCEVSMinExpr>(FromRewritten))
          EnqueueOperands(SMin);
        break;
      case CmpInst::ICMP_EQ:
        if (isa<SCEVConstant>(RHS))
          To = RHS;
        break;
      case CmpInst::ICMP_NE:
        if (isa<SCEVConstant>(RHS) &&
            cast<SCEVConstant>(RHS)->getValue()->isNullValue()) {
          const SCEV *OneAlignedUp =
              DividesBy ? GetNextSCEVDividesByDivisor(One, DividesBy) : One;
          To = SE.getUMaxExpr(FromRewritten, OneAlignedUp);
        }
        break;
      default:
        break;
      }

      if (To)
        AddRewrite(From, FromRewritten, To);
    }
  };

  BasicBlock *Header = L->getHeader();
  SmallVector<PointerIntPair<Value *, 1, bool>> Terms;
  // First, collect information from assumptions dominating the loop.
  for (auto &AssumeVH : SE.AC.assumptions()) {
    if (!AssumeVH)
      continue;
    auto *AssumeI = cast<CallInst>(AssumeVH);
    if (!SE.DT.dominates(AssumeI, Header))
      continue;
    Terms.emplace_back(AssumeI->getOperand(0), true);
  }

  // Second, collect information from llvm.experimental.guards dominating the loop.
  auto *GuardDecl = SE.F.getParent()->getFunction(
      Intrinsic::getName(Intrinsic::experimental_guard));
  if (GuardDecl)
    for (const auto *GU : GuardDecl->users())
      if (const auto *Guard = dyn_cast<IntrinsicInst>(GU))
        if (Guard->getFunction() == Header->getParent() &&
            SE.DT.dominates(Guard, Header))
          Terms.emplace_back(Guard->getArgOperand(0), true);

  // Third, collect conditions from dominating branches. Starting at the loop
  // predecessor, climb up the predecessor chain, as long as there are
  // predecessors that can be found that have unique successors leading to the
  // original header.
  // TODO: share this logic with isLoopEntryGuardedByCond.
  for (std::pair<const BasicBlock *, const BasicBlock *> Pair(
           L->getLoopPredecessor(), Header);
       Pair.first;
       Pair = SE.getPredecessorWithUniqueSuccessorForBB(Pair.first)) {

    const BranchInst *LoopEntryPredicate =
        dyn_cast<BranchInst>(Pair.first->getTerminator());
    if (!LoopEntryPredicate || LoopEntryPredicate->isUnconditional())
      continue;

    Terms.emplace_back(LoopEntryPredicate->getCondition(),
                       LoopEntryPredicate->getSuccessor(0) == Pair.second);
  }

  // Now apply the information from the collected conditions to
  // Guards.RewriteMap. Conditions are processed in reverse order, so the
  // earliest conditions is processed first. This ensures the SCEVs with the
  // shortest dependency chains are constructed first.
  for (auto [Term, EnterIfTrue] : reverse(Terms)) {
    SmallVector<Value *, 8> Worklist;
    SmallPtrSet<Value *, 8> Visited;
    Worklist.push_back(Term);
    while (!Worklist.empty()) {
      Value *Cond = Worklist.pop_back_val();
      if (!Visited.insert(Cond).second)
        continue;

      if (auto *Cmp = dyn_cast<ICmpInst>(Cond)) {
        auto Predicate =
            EnterIfTrue ? Cmp->getPredicate() : Cmp->getInversePredicate();
        const auto *LHS = SE.getSCEV(Cmp->getOperand(0));
        const auto *RHS = SE.getSCEV(Cmp->getOperand(1));
        CollectCondition(Predicate, LHS, RHS, Guards.RewriteMap);
        continue;
      }

      Value *L, *R;
      if (EnterIfTrue ? match(Cond, m_LogicalAnd(m_Value(L), m_Value(R)))
                      : match(Cond, m_LogicalOr(m_Value(L), m_Value(R)))) {
        Worklist.push_back(L);
        Worklist.push_back(R);
      }
    }
  }

  // Let the rewriter preserve NUW/NSW flags if the unsigned/signed ranges of
  // the replacement expressions are contained in the ranges of the replaced
  // expressions.
  Guards.PreserveNUW = true;
  Guards.PreserveNSW = true;
  for (const SCEV *Expr : ExprsToRewrite) {
    const SCEV *RewriteTo = Guards.RewriteMap[Expr];
    Guards.PreserveNUW &=
        SE.getUnsignedRange(Expr).contains(SE.getUnsignedRange(RewriteTo));
    Guards.PreserveNSW &=
        SE.getSignedRange(Expr).contains(SE.getSignedRange(RewriteTo));
  }

  // Now that all rewrite information is collect, rewrite the collected
  // expressions with the information in the map. This applies information to
  // sub-expressions.
  if (ExprsToRewrite.size() > 1) {
    for (const SCEV *Expr : ExprsToRewrite) {
      const SCEV *RewriteTo = Guards.RewriteMap[Expr];
      Guards.RewriteMap.erase(Expr);
      Guards.RewriteMap.insert({Expr, Guards.rewrite(RewriteTo)});
    }
  }
  return Guards;
}

const SCEV *ScalarEvolution::LoopGuards::rewrite(const SCEV *Expr) const {
  /// A rewriter to replace SCEV expressions in Map with the corresponding entry
  /// in the map. It skips AddRecExpr because we cannot guarantee that the
  /// replacement is loop invariant in the loop of the AddRec.
  class SCEVLoopGuardRewriter
      : public SCEVRewriteVisitor<SCEVLoopGuardRewriter> {
    const DenseMap<const SCEV *, const SCEV *> &Map;

    SCEV::NoWrapFlags FlagMask = SCEV::FlagAnyWrap;

  public:
    SCEVLoopGuardRewriter(ScalarEvolution &SE,
                          const ScalarEvolution::LoopGuards &Guards)
        : SCEVRewriteVisitor(SE), Map(Guards.RewriteMap) {
      if (Guards.PreserveNUW)
        FlagMask = ScalarEvolution::setFlags(FlagMask, SCEV::FlagNUW);
      if (Guards.PreserveNSW)
        FlagMask = ScalarEvolution::setFlags(FlagMask, SCEV::FlagNSW);
    }

    const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr) { return Expr; }

    const SCEV *visitUnknown(const SCEVUnknown *Expr) {
      auto I = Map.find(Expr);
      if (I == Map.end())
        return Expr;
      return I->second;
    }

    const SCEV *visitZeroExtendExpr(const SCEVZeroExtendExpr *Expr) {
      auto I = Map.find(Expr);
      if (I == Map.end()) {
        // If we didn't find the extact ZExt expr in the map, check if there's
        // an entry for a smaller ZExt we can use instead.
        Type *Ty = Expr->getType();
        const SCEV *Op = Expr->getOperand(0);
        unsigned Bitwidth = Ty->getScalarSizeInBits() / 2;
        while (Bitwidth % 8 == 0 && Bitwidth >= 8 &&
               Bitwidth > Op->getType()->getScalarSizeInBits()) {
          Type *NarrowTy = IntegerType::get(SE.getContext(), Bitwidth);
          auto *NarrowExt = SE.getZeroExtendExpr(Op, NarrowTy);
          auto I = Map.find(NarrowExt);
          if (I != Map.end())
            return SE.getZeroExtendExpr(I->second, Ty);
          Bitwidth = Bitwidth / 2;
        }

        return SCEVRewriteVisitor<SCEVLoopGuardRewriter>::visitZeroExtendExpr(
            Expr);
      }
      return I->second;
    }

    const SCEV *visitSignExtendExpr(const SCEVSignExtendExpr *Expr) {
      auto I = Map.find(Expr);
      if (I == Map.end())
        return SCEVRewriteVisitor<SCEVLoopGuardRewriter>::visitSignExtendExpr(
            Expr);
      return I->second;
    }

    const SCEV *visitUMinExpr(const SCEVUMinExpr *Expr) {
      auto I = Map.find(Expr);
      if (I == Map.end())
        return SCEVRewriteVisitor<SCEVLoopGuardRewriter>::visitUMinExpr(Expr);
      return I->second;
    }

    const SCEV *visitSMinExpr(const SCEVSMinExpr *Expr) {
      auto I = Map.find(Expr);
      if (I == Map.end())
        return SCEVRewriteVisitor<SCEVLoopGuardRewriter>::visitSMinExpr(Expr);
      return I->second;
    }

    const SCEV *visitAddExpr(const SCEVAddExpr *Expr) {
      SmallVector<const SCEV *, 2> Operands;
      bool Changed = false;
      for (const auto *Op : Expr->operands()) {
        Operands.push_back(
            SCEVRewriteVisitor<SCEVLoopGuardRewriter>::visit(Op));
        Changed |= Op != Operands.back();
      }
      // We are only replacing operands with equivalent values, so transfer the
      // flags from the original expression.
      return !Changed ? Expr
                      : SE.getAddExpr(Operands,
                                      ScalarEvolution::maskFlags(
                                          Expr->getNoWrapFlags(), FlagMask));
    }

    const SCEV *visitMulExpr(const SCEVMulExpr *Expr) {
      SmallVector<const SCEV *, 2> Operands;
      bool Changed = false;
      for (const auto *Op : Expr->operands()) {
        Operands.push_back(
            SCEVRewriteVisitor<SCEVLoopGuardRewriter>::visit(Op));
        Changed |= Op != Operands.back();
      }
      // We are only replacing operands with equivalent values, so transfer the
      // flags from the original expression.
      return !Changed ? Expr
                      : SE.getMulExpr(Operands,
                                      ScalarEvolution::maskFlags(
                                          Expr->getNoWrapFlags(), FlagMask));
    }
  };

  if (RewriteMap.empty())
    return Expr;

  SCEVLoopGuardRewriter Rewriter(SE, *this);
  return Rewriter.visit(Expr);
}

const SCEV *ScalarEvolution::applyLoopGuards(const SCEV *Expr, const Loop *L) {
  return applyLoopGuards(Expr, LoopGuards::collect(L, *this));
}

const SCEV *ScalarEvolution::applyLoopGuards(const SCEV *Expr,
                                             const LoopGuards &Guards) {
  return Guards.rewrite(Expr);
}
