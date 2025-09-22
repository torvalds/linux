//===- BasicAliasAnalysis.cpp - Stateless Alias Analysis Impl -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the primary stateless implementation of the
// Alias Analysis interface that implements identities (two different
// globals cannot alias, etc), but does no stateful analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/SaveAndRestore.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>

#define DEBUG_TYPE "basicaa"

using namespace llvm;

/// Enable analysis of recursive PHI nodes.
static cl::opt<bool> EnableRecPhiAnalysis("basic-aa-recphi", cl::Hidden,
                                          cl::init(true));

static cl::opt<bool> EnableSeparateStorageAnalysis("basic-aa-separate-storage",
                                                   cl::Hidden, cl::init(true));

/// SearchLimitReached / SearchTimes shows how often the limit of
/// to decompose GEPs is reached. It will affect the precision
/// of basic alias analysis.
STATISTIC(SearchLimitReached, "Number of times the limit to "
                              "decompose GEPs is reached");
STATISTIC(SearchTimes, "Number of times a GEP is decomposed");

// The max limit of the search depth in DecomposeGEPExpression() and
// getUnderlyingObject().
static const unsigned MaxLookupSearchDepth = 6;

bool BasicAAResult::invalidate(Function &Fn, const PreservedAnalyses &PA,
                               FunctionAnalysisManager::Invalidator &Inv) {
  // We don't care if this analysis itself is preserved, it has no state. But
  // we need to check that the analyses it depends on have been. Note that we
  // may be created without handles to some analyses and in that case don't
  // depend on them.
  if (Inv.invalidate<AssumptionAnalysis>(Fn, PA) ||
      (DT_ && Inv.invalidate<DominatorTreeAnalysis>(Fn, PA)))
    return true;

  // Otherwise this analysis result remains valid.
  return false;
}

//===----------------------------------------------------------------------===//
// Useful predicates
//===----------------------------------------------------------------------===//

/// Returns the size of the object specified by V or UnknownSize if unknown.
static std::optional<TypeSize> getObjectSize(const Value *V,
                                             const DataLayout &DL,
                                             const TargetLibraryInfo &TLI,
                                             bool NullIsValidLoc,
                                             bool RoundToAlign = false) {
  uint64_t Size;
  ObjectSizeOpts Opts;
  Opts.RoundToAlign = RoundToAlign;
  Opts.NullIsUnknownSize = NullIsValidLoc;
  if (getObjectSize(V, Size, DL, &TLI, Opts))
    return TypeSize::getFixed(Size);
  return std::nullopt;
}

/// Returns true if we can prove that the object specified by V is smaller than
/// Size.
static bool isObjectSmallerThan(const Value *V, TypeSize Size,
                                const DataLayout &DL,
                                const TargetLibraryInfo &TLI,
                                bool NullIsValidLoc) {
  // Note that the meanings of the "object" are slightly different in the
  // following contexts:
  //    c1: llvm::getObjectSize()
  //    c2: llvm.objectsize() intrinsic
  //    c3: isObjectSmallerThan()
  // c1 and c2 share the same meaning; however, the meaning of "object" in c3
  // refers to the "entire object".
  //
  //  Consider this example:
  //     char *p = (char*)malloc(100)
  //     char *q = p+80;
  //
  //  In the context of c1 and c2, the "object" pointed by q refers to the
  // stretch of memory of q[0:19]. So, getObjectSize(q) should return 20.
  //
  //  However, in the context of c3, the "object" refers to the chunk of memory
  // being allocated. So, the "object" has 100 bytes, and q points to the middle
  // the "object". In case q is passed to isObjectSmallerThan() as the 1st
  // parameter, before the llvm::getObjectSize() is called to get the size of
  // entire object, we should:
  //    - either rewind the pointer q to the base-address of the object in
  //      question (in this case rewind to p), or
  //    - just give up. It is up to caller to make sure the pointer is pointing
  //      to the base address the object.
  //
  // We go for 2nd option for simplicity.
  if (!isIdentifiedObject(V))
    return false;

  // This function needs to use the aligned object size because we allow
  // reads a bit past the end given sufficient alignment.
  std::optional<TypeSize> ObjectSize = getObjectSize(V, DL, TLI, NullIsValidLoc,
                                                     /*RoundToAlign*/ true);

  return ObjectSize && TypeSize::isKnownLT(*ObjectSize, Size);
}

/// Return the minimal extent from \p V to the end of the underlying object,
/// assuming the result is used in an aliasing query. E.g., we do use the query
/// location size and the fact that null pointers cannot alias here.
static TypeSize getMinimalExtentFrom(const Value &V,
                                     const LocationSize &LocSize,
                                     const DataLayout &DL,
                                     bool NullIsValidLoc) {
  // If we have dereferenceability information we know a lower bound for the
  // extent as accesses for a lower offset would be valid. We need to exclude
  // the "or null" part if null is a valid pointer. We can ignore frees, as an
  // access after free would be undefined behavior.
  bool CanBeNull, CanBeFreed;
  uint64_t DerefBytes =
    V.getPointerDereferenceableBytes(DL, CanBeNull, CanBeFreed);
  DerefBytes = (CanBeNull && NullIsValidLoc) ? 0 : DerefBytes;
  // If queried with a precise location size, we assume that location size to be
  // accessed, thus valid.
  if (LocSize.isPrecise())
    DerefBytes = std::max(DerefBytes, LocSize.getValue().getKnownMinValue());
  return TypeSize::getFixed(DerefBytes);
}

/// Returns true if we can prove that the object specified by V has size Size.
static bool isObjectSize(const Value *V, TypeSize Size, const DataLayout &DL,
                         const TargetLibraryInfo &TLI, bool NullIsValidLoc) {
  std::optional<TypeSize> ObjectSize =
      getObjectSize(V, DL, TLI, NullIsValidLoc);
  return ObjectSize && *ObjectSize == Size;
}

/// Return true if both V1 and V2 are VScale
static bool areBothVScale(const Value *V1, const Value *V2) {
  return PatternMatch::match(V1, PatternMatch::m_VScale()) &&
         PatternMatch::match(V2, PatternMatch::m_VScale());
}

//===----------------------------------------------------------------------===//
// CaptureInfo implementations
//===----------------------------------------------------------------------===//

CaptureInfo::~CaptureInfo() = default;

bool SimpleCaptureInfo::isNotCapturedBefore(const Value *Object,
                                            const Instruction *I, bool OrAt) {
  return isNonEscapingLocalObject(Object, &IsCapturedCache);
}

static bool isNotInCycle(const Instruction *I, const DominatorTree *DT,
                         const LoopInfo *LI) {
  BasicBlock *BB = const_cast<BasicBlock *>(I->getParent());
  SmallVector<BasicBlock *> Succs(successors(BB));
  return Succs.empty() ||
         !isPotentiallyReachableFromMany(Succs, BB, nullptr, DT, LI);
}

bool EarliestEscapeInfo::isNotCapturedBefore(const Value *Object,
                                             const Instruction *I, bool OrAt) {
  if (!isIdentifiedFunctionLocal(Object))
    return false;

  auto Iter = EarliestEscapes.insert({Object, nullptr});
  if (Iter.second) {
    Instruction *EarliestCapture = FindEarliestCapture(
        Object, *const_cast<Function *>(DT.getRoot()->getParent()),
        /*ReturnCaptures=*/false, /*StoreCaptures=*/true, DT);
    if (EarliestCapture) {
      auto Ins = Inst2Obj.insert({EarliestCapture, {}});
      Ins.first->second.push_back(Object);
    }
    Iter.first->second = EarliestCapture;
  }

  // No capturing instruction.
  if (!Iter.first->second)
    return true;

  // No context instruction means any use is capturing.
  if (!I)
    return false;

  if (I == Iter.first->second) {
    if (OrAt)
      return false;
    return isNotInCycle(I, &DT, LI);
  }

  return !isPotentiallyReachable(Iter.first->second, I, nullptr, &DT, LI);
}

void EarliestEscapeInfo::removeInstruction(Instruction *I) {
  auto Iter = Inst2Obj.find(I);
  if (Iter != Inst2Obj.end()) {
    for (const Value *Obj : Iter->second)
      EarliestEscapes.erase(Obj);
    Inst2Obj.erase(I);
  }
}

//===----------------------------------------------------------------------===//
// GetElementPtr Instruction Decomposition and Analysis
//===----------------------------------------------------------------------===//

namespace {
/// Represents zext(sext(trunc(V))).
struct CastedValue {
  const Value *V;
  unsigned ZExtBits = 0;
  unsigned SExtBits = 0;
  unsigned TruncBits = 0;
  /// Whether trunc(V) is non-negative.
  bool IsNonNegative = false;

  explicit CastedValue(const Value *V) : V(V) {}
  explicit CastedValue(const Value *V, unsigned ZExtBits, unsigned SExtBits,
                       unsigned TruncBits, bool IsNonNegative)
      : V(V), ZExtBits(ZExtBits), SExtBits(SExtBits), TruncBits(TruncBits),
        IsNonNegative(IsNonNegative) {}

  unsigned getBitWidth() const {
    return V->getType()->getPrimitiveSizeInBits() - TruncBits + ZExtBits +
           SExtBits;
  }

  CastedValue withValue(const Value *NewV, bool PreserveNonNeg) const {
    return CastedValue(NewV, ZExtBits, SExtBits, TruncBits,
                       IsNonNegative && PreserveNonNeg);
  }

  /// Replace V with zext(NewV)
  CastedValue withZExtOfValue(const Value *NewV, bool ZExtNonNegative) const {
    unsigned ExtendBy = V->getType()->getPrimitiveSizeInBits() -
                        NewV->getType()->getPrimitiveSizeInBits();
    if (ExtendBy <= TruncBits)
      // zext<nneg>(trunc(zext(NewV))) == zext<nneg>(trunc(NewV))
      // The nneg can be preserved on the outer zext here.
      return CastedValue(NewV, ZExtBits, SExtBits, TruncBits - ExtendBy,
                         IsNonNegative);

    // zext(sext(zext(NewV))) == zext(zext(zext(NewV)))
    ExtendBy -= TruncBits;
    // zext<nneg>(zext(NewV)) == zext(NewV)
    // zext(zext<nneg>(NewV)) == zext<nneg>(NewV)
    // The nneg can be preserved from the inner zext here but must be dropped
    // from the outer.
    return CastedValue(NewV, ZExtBits + SExtBits + ExtendBy, 0, 0,
                       ZExtNonNegative);
  }

  /// Replace V with sext(NewV)
  CastedValue withSExtOfValue(const Value *NewV) const {
    unsigned ExtendBy = V->getType()->getPrimitiveSizeInBits() -
                        NewV->getType()->getPrimitiveSizeInBits();
    if (ExtendBy <= TruncBits)
      // zext<nneg>(trunc(sext(NewV))) == zext<nneg>(trunc(NewV))
      // The nneg can be preserved on the outer zext here
      return CastedValue(NewV, ZExtBits, SExtBits, TruncBits - ExtendBy,
                         IsNonNegative);

    // zext(sext(sext(NewV)))
    ExtendBy -= TruncBits;
    // zext<nneg>(sext(sext(NewV))) = zext<nneg>(sext(NewV))
    // The nneg can be preserved on the outer zext here
    return CastedValue(NewV, ZExtBits, SExtBits + ExtendBy, 0, IsNonNegative);
  }

  APInt evaluateWith(APInt N) const {
    assert(N.getBitWidth() == V->getType()->getPrimitiveSizeInBits() &&
           "Incompatible bit width");
    if (TruncBits) N = N.trunc(N.getBitWidth() - TruncBits);
    if (SExtBits) N = N.sext(N.getBitWidth() + SExtBits);
    if (ZExtBits) N = N.zext(N.getBitWidth() + ZExtBits);
    return N;
  }

  ConstantRange evaluateWith(ConstantRange N) const {
    assert(N.getBitWidth() == V->getType()->getPrimitiveSizeInBits() &&
           "Incompatible bit width");
    if (TruncBits) N = N.truncate(N.getBitWidth() - TruncBits);
    if (SExtBits) N = N.signExtend(N.getBitWidth() + SExtBits);
    if (ZExtBits) N = N.zeroExtend(N.getBitWidth() + ZExtBits);
    return N;
  }

  bool canDistributeOver(bool NUW, bool NSW) const {
    // zext(x op<nuw> y) == zext(x) op<nuw> zext(y)
    // sext(x op<nsw> y) == sext(x) op<nsw> sext(y)
    // trunc(x op y) == trunc(x) op trunc(y)
    return (!ZExtBits || NUW) && (!SExtBits || NSW);
  }

  bool hasSameCastsAs(const CastedValue &Other) const {
    if (ZExtBits == Other.ZExtBits && SExtBits == Other.SExtBits &&
        TruncBits == Other.TruncBits)
      return true;
    // If either CastedValue has a nneg zext then the sext/zext bits are
    // interchangable for that value.
    if (IsNonNegative || Other.IsNonNegative)
      return (ZExtBits + SExtBits == Other.ZExtBits + Other.SExtBits &&
              TruncBits == Other.TruncBits);
    return false;
  }
};

/// Represents zext(sext(trunc(V))) * Scale + Offset.
struct LinearExpression {
  CastedValue Val;
  APInt Scale;
  APInt Offset;

  /// True if all operations in this expression are NSW.
  bool IsNSW;

  LinearExpression(const CastedValue &Val, const APInt &Scale,
                   const APInt &Offset, bool IsNSW)
      : Val(Val), Scale(Scale), Offset(Offset), IsNSW(IsNSW) {}

  LinearExpression(const CastedValue &Val) : Val(Val), IsNSW(true) {
    unsigned BitWidth = Val.getBitWidth();
    Scale = APInt(BitWidth, 1);
    Offset = APInt(BitWidth, 0);
  }

  LinearExpression mul(const APInt &Other, bool MulIsNSW) const {
    // The check for zero offset is necessary, because generally
    // (X +nsw Y) *nsw Z does not imply (X *nsw Z) +nsw (Y *nsw Z).
    bool NSW = IsNSW && (Other.isOne() || (MulIsNSW && Offset.isZero()));
    return LinearExpression(Val, Scale * Other, Offset * Other, NSW);
  }
};
}

/// Analyzes the specified value as a linear expression: "A*V + B", where A and
/// B are constant integers.
static LinearExpression GetLinearExpression(
    const CastedValue &Val,  const DataLayout &DL, unsigned Depth,
    AssumptionCache *AC, DominatorTree *DT) {
  // Limit our recursion depth.
  if (Depth == 6)
    return Val;

  if (const ConstantInt *Const = dyn_cast<ConstantInt>(Val.V))
    return LinearExpression(Val, APInt(Val.getBitWidth(), 0),
                            Val.evaluateWith(Const->getValue()), true);

  if (const BinaryOperator *BOp = dyn_cast<BinaryOperator>(Val.V)) {
    if (ConstantInt *RHSC = dyn_cast<ConstantInt>(BOp->getOperand(1))) {
      APInt RHS = Val.evaluateWith(RHSC->getValue());
      // The only non-OBO case we deal with is or, and only limited to the
      // case where it is both nuw and nsw.
      bool NUW = true, NSW = true;
      if (isa<OverflowingBinaryOperator>(BOp)) {
        NUW &= BOp->hasNoUnsignedWrap();
        NSW &= BOp->hasNoSignedWrap();
      }
      if (!Val.canDistributeOver(NUW, NSW))
        return Val;

      // While we can distribute over trunc, we cannot preserve nowrap flags
      // in that case.
      if (Val.TruncBits)
        NUW = NSW = false;

      LinearExpression E(Val);
      switch (BOp->getOpcode()) {
      default:
        // We don't understand this instruction, so we can't decompose it any
        // further.
        return Val;
      case Instruction::Or:
        // X|C == X+C if it is disjoint.  Otherwise we can't analyze it.
        if (!cast<PossiblyDisjointInst>(BOp)->isDisjoint())
          return Val;

        [[fallthrough]];
      case Instruction::Add: {
        E = GetLinearExpression(Val.withValue(BOp->getOperand(0), false), DL,
                                Depth + 1, AC, DT);
        E.Offset += RHS;
        E.IsNSW &= NSW;
        break;
      }
      case Instruction::Sub: {
        E = GetLinearExpression(Val.withValue(BOp->getOperand(0), false), DL,
                                Depth + 1, AC, DT);
        E.Offset -= RHS;
        E.IsNSW &= NSW;
        break;
      }
      case Instruction::Mul:
        E = GetLinearExpression(Val.withValue(BOp->getOperand(0), false), DL,
                                Depth + 1, AC, DT)
                .mul(RHS, NSW);
        break;
      case Instruction::Shl:
        // We're trying to linearize an expression of the kind:
        //   shl i8 -128, 36
        // where the shift count exceeds the bitwidth of the type.
        // We can't decompose this further (the expression would return
        // a poison value).
        if (RHS.getLimitedValue() > Val.getBitWidth())
          return Val;

        E = GetLinearExpression(Val.withValue(BOp->getOperand(0), NSW), DL,
                                Depth + 1, AC, DT);
        E.Offset <<= RHS.getLimitedValue();
        E.Scale <<= RHS.getLimitedValue();
        E.IsNSW &= NSW;
        break;
      }
      return E;
    }
  }

  if (const auto *ZExt = dyn_cast<ZExtInst>(Val.V))
    return GetLinearExpression(
        Val.withZExtOfValue(ZExt->getOperand(0), ZExt->hasNonNeg()), DL,
        Depth + 1, AC, DT);

  if (isa<SExtInst>(Val.V))
    return GetLinearExpression(
        Val.withSExtOfValue(cast<CastInst>(Val.V)->getOperand(0)),
        DL, Depth + 1, AC, DT);

  return Val;
}

/// To ensure a pointer offset fits in an integer of size IndexSize
/// (in bits) when that size is smaller than the maximum index size. This is
/// an issue, for example, in particular for 32b pointers with negative indices
/// that rely on two's complement wrap-arounds for precise alias information
/// where the maximum index size is 64b.
static void adjustToIndexSize(APInt &Offset, unsigned IndexSize) {
  assert(IndexSize <= Offset.getBitWidth() && "Invalid IndexSize!");
  unsigned ShiftBits = Offset.getBitWidth() - IndexSize;
  if (ShiftBits != 0) {
    Offset <<= ShiftBits;
    Offset.ashrInPlace(ShiftBits);
  }
}

namespace {
// A linear transformation of a Value; this class represents
// ZExt(SExt(Trunc(V, TruncBits), SExtBits), ZExtBits) * Scale.
struct VariableGEPIndex {
  CastedValue Val;
  APInt Scale;

  // Context instruction to use when querying information about this index.
  const Instruction *CxtI;

  /// True if all operations in this expression are NSW.
  bool IsNSW;

  /// True if the index should be subtracted rather than added. We don't simply
  /// negate the Scale, to avoid losing the NSW flag: X - INT_MIN*1 may be
  /// non-wrapping, while X + INT_MIN*(-1) wraps.
  bool IsNegated;

  bool hasNegatedScaleOf(const VariableGEPIndex &Other) const {
    if (IsNegated == Other.IsNegated)
      return Scale == -Other.Scale;
    return Scale == Other.Scale;
  }

  void dump() const {
    print(dbgs());
    dbgs() << "\n";
  }
  void print(raw_ostream &OS) const {
    OS << "(V=" << Val.V->getName()
       << ", zextbits=" << Val.ZExtBits
       << ", sextbits=" << Val.SExtBits
       << ", truncbits=" << Val.TruncBits
       << ", scale=" << Scale
       << ", nsw=" << IsNSW
       << ", negated=" << IsNegated << ")";
  }
};
}

// Represents the internal structure of a GEP, decomposed into a base pointer,
// constant offsets, and variable scaled indices.
struct BasicAAResult::DecomposedGEP {
  // Base pointer of the GEP
  const Value *Base;
  // Total constant offset from base.
  APInt Offset;
  // Scaled variable (non-constant) indices.
  SmallVector<VariableGEPIndex, 4> VarIndices;
  // Are all operations inbounds GEPs or non-indexing operations?
  // (std::nullopt iff expression doesn't involve any geps)
  std::optional<bool> InBounds;

  void dump() const {
    print(dbgs());
    dbgs() << "\n";
  }
  void print(raw_ostream &OS) const {
    OS << "(DecomposedGEP Base=" << Base->getName()
       << ", Offset=" << Offset
       << ", VarIndices=[";
    for (size_t i = 0; i < VarIndices.size(); i++) {
      if (i != 0)
        OS << ", ";
      VarIndices[i].print(OS);
    }
    OS << "])";
  }
};


/// If V is a symbolic pointer expression, decompose it into a base pointer
/// with a constant offset and a number of scaled symbolic offsets.
///
/// The scaled symbolic offsets (represented by pairs of a Value* and a scale
/// in the VarIndices vector) are Value*'s that are known to be scaled by the
/// specified amount, but which may have other unrepresented high bits. As
/// such, the gep cannot necessarily be reconstructed from its decomposed form.
BasicAAResult::DecomposedGEP
BasicAAResult::DecomposeGEPExpression(const Value *V, const DataLayout &DL,
                                      AssumptionCache *AC, DominatorTree *DT) {
  // Limit recursion depth to limit compile time in crazy cases.
  unsigned MaxLookup = MaxLookupSearchDepth;
  SearchTimes++;
  const Instruction *CxtI = dyn_cast<Instruction>(V);

  unsigned MaxIndexSize = DL.getMaxIndexSizeInBits();
  DecomposedGEP Decomposed;
  Decomposed.Offset = APInt(MaxIndexSize, 0);
  do {
    // See if this is a bitcast or GEP.
    const Operator *Op = dyn_cast<Operator>(V);
    if (!Op) {
      // The only non-operator case we can handle are GlobalAliases.
      if (const GlobalAlias *GA = dyn_cast<GlobalAlias>(V)) {
        if (!GA->isInterposable()) {
          V = GA->getAliasee();
          continue;
        }
      }
      Decomposed.Base = V;
      return Decomposed;
    }

    if (Op->getOpcode() == Instruction::BitCast ||
        Op->getOpcode() == Instruction::AddrSpaceCast) {
      V = Op->getOperand(0);
      continue;
    }

    const GEPOperator *GEPOp = dyn_cast<GEPOperator>(Op);
    if (!GEPOp) {
      if (const auto *PHI = dyn_cast<PHINode>(V)) {
        // Look through single-arg phi nodes created by LCSSA.
        if (PHI->getNumIncomingValues() == 1) {
          V = PHI->getIncomingValue(0);
          continue;
        }
      } else if (const auto *Call = dyn_cast<CallBase>(V)) {
        // CaptureTracking can know about special capturing properties of some
        // intrinsics like launder.invariant.group, that can't be expressed with
        // the attributes, but have properties like returning aliasing pointer.
        // Because some analysis may assume that nocaptured pointer is not
        // returned from some special intrinsic (because function would have to
        // be marked with returns attribute), it is crucial to use this function
        // because it should be in sync with CaptureTracking. Not using it may
        // cause weird miscompilations where 2 aliasing pointers are assumed to
        // noalias.
        if (auto *RP = getArgumentAliasingToReturnedPointer(Call, false)) {
          V = RP;
          continue;
        }
      }

      Decomposed.Base = V;
      return Decomposed;
    }

    // Track whether we've seen at least one in bounds gep, and if so, whether
    // all geps parsed were in bounds.
    if (Decomposed.InBounds == std::nullopt)
      Decomposed.InBounds = GEPOp->isInBounds();
    else if (!GEPOp->isInBounds())
      Decomposed.InBounds = false;

    assert(GEPOp->getSourceElementType()->isSized() && "GEP must be sized");

    unsigned AS = GEPOp->getPointerAddressSpace();
    // Walk the indices of the GEP, accumulating them into BaseOff/VarIndices.
    gep_type_iterator GTI = gep_type_begin(GEPOp);
    unsigned IndexSize = DL.getIndexSizeInBits(AS);
    // Assume all GEP operands are constants until proven otherwise.
    bool GepHasConstantOffset = true;
    for (User::const_op_iterator I = GEPOp->op_begin() + 1, E = GEPOp->op_end();
         I != E; ++I, ++GTI) {
      const Value *Index = *I;
      // Compute the (potentially symbolic) offset in bytes for this index.
      if (StructType *STy = GTI.getStructTypeOrNull()) {
        // For a struct, add the member offset.
        unsigned FieldNo = cast<ConstantInt>(Index)->getZExtValue();
        if (FieldNo == 0)
          continue;

        Decomposed.Offset += DL.getStructLayout(STy)->getElementOffset(FieldNo);
        continue;
      }

      // For an array/pointer, add the element offset, explicitly scaled.
      if (const ConstantInt *CIdx = dyn_cast<ConstantInt>(Index)) {
        if (CIdx->isZero())
          continue;

        // Don't attempt to analyze GEPs if the scalable index is not zero.
        TypeSize AllocTypeSize = GTI.getSequentialElementStride(DL);
        if (AllocTypeSize.isScalable()) {
          Decomposed.Base = V;
          return Decomposed;
        }

        Decomposed.Offset += AllocTypeSize.getFixedValue() *
                             CIdx->getValue().sextOrTrunc(MaxIndexSize);
        continue;
      }

      TypeSize AllocTypeSize = GTI.getSequentialElementStride(DL);
      if (AllocTypeSize.isScalable()) {
        Decomposed.Base = V;
        return Decomposed;
      }

      GepHasConstantOffset = false;

      // If the integer type is smaller than the index size, it is implicitly
      // sign extended or truncated to index size.
      unsigned Width = Index->getType()->getIntegerBitWidth();
      unsigned SExtBits = IndexSize > Width ? IndexSize - Width : 0;
      unsigned TruncBits = IndexSize < Width ? Width - IndexSize : 0;
      LinearExpression LE = GetLinearExpression(
          CastedValue(Index, 0, SExtBits, TruncBits, false), DL, 0, AC, DT);

      // Scale by the type size.
      unsigned TypeSize = AllocTypeSize.getFixedValue();
      LE = LE.mul(APInt(IndexSize, TypeSize), GEPOp->isInBounds());
      Decomposed.Offset += LE.Offset.sext(MaxIndexSize);
      APInt Scale = LE.Scale.sext(MaxIndexSize);

      // If we already had an occurrence of this index variable, merge this
      // scale into it.  For example, we want to handle:
      //   A[x][x] -> x*16 + x*4 -> x*20
      // This also ensures that 'x' only appears in the index list once.
      for (unsigned i = 0, e = Decomposed.VarIndices.size(); i != e; ++i) {
        if ((Decomposed.VarIndices[i].Val.V == LE.Val.V ||
             areBothVScale(Decomposed.VarIndices[i].Val.V, LE.Val.V)) &&
            Decomposed.VarIndices[i].Val.hasSameCastsAs(LE.Val)) {
          Scale += Decomposed.VarIndices[i].Scale;
          LE.IsNSW = false; // We cannot guarantee nsw for the merge.
          Decomposed.VarIndices.erase(Decomposed.VarIndices.begin() + i);
          break;
        }
      }

      // Make sure that we have a scale that makes sense for this target's
      // index size.
      adjustToIndexSize(Scale, IndexSize);

      if (!!Scale) {
        VariableGEPIndex Entry = {LE.Val, Scale, CxtI, LE.IsNSW,
                                  /* IsNegated */ false};
        Decomposed.VarIndices.push_back(Entry);
      }
    }

    // Take care of wrap-arounds
    if (GepHasConstantOffset)
      adjustToIndexSize(Decomposed.Offset, IndexSize);

    // Analyze the base pointer next.
    V = GEPOp->getOperand(0);
  } while (--MaxLookup);

  // If the chain of expressions is too deep, just return early.
  Decomposed.Base = V;
  SearchLimitReached++;
  return Decomposed;
}

ModRefInfo BasicAAResult::getModRefInfoMask(const MemoryLocation &Loc,
                                            AAQueryInfo &AAQI,
                                            bool IgnoreLocals) {
  assert(Visited.empty() && "Visited must be cleared after use!");
  auto _ = make_scope_exit([&] { Visited.clear(); });

  unsigned MaxLookup = 8;
  SmallVector<const Value *, 16> Worklist;
  Worklist.push_back(Loc.Ptr);
  ModRefInfo Result = ModRefInfo::NoModRef;

  do {
    const Value *V = getUnderlyingObject(Worklist.pop_back_val());
    if (!Visited.insert(V).second)
      continue;

    // Ignore allocas if we were instructed to do so.
    if (IgnoreLocals && isa<AllocaInst>(V))
      continue;

    // If the location points to memory that is known to be invariant for
    // the life of the underlying SSA value, then we can exclude Mod from
    // the set of valid memory effects.
    //
    // An argument that is marked readonly and noalias is known to be
    // invariant while that function is executing.
    if (const Argument *Arg = dyn_cast<Argument>(V)) {
      if (Arg->hasNoAliasAttr() && Arg->onlyReadsMemory()) {
        Result |= ModRefInfo::Ref;
        continue;
      }
    }

    // A global constant can't be mutated.
    if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(V)) {
      // Note: this doesn't require GV to be "ODR" because it isn't legal for a
      // global to be marked constant in some modules and non-constant in
      // others.  GV may even be a declaration, not a definition.
      if (!GV->isConstant())
        return ModRefInfo::ModRef;
      continue;
    }

    // If both select values point to local memory, then so does the select.
    if (const SelectInst *SI = dyn_cast<SelectInst>(V)) {
      Worklist.push_back(SI->getTrueValue());
      Worklist.push_back(SI->getFalseValue());
      continue;
    }

    // If all values incoming to a phi node point to local memory, then so does
    // the phi.
    if (const PHINode *PN = dyn_cast<PHINode>(V)) {
      // Don't bother inspecting phi nodes with many operands.
      if (PN->getNumIncomingValues() > MaxLookup)
        return ModRefInfo::ModRef;
      append_range(Worklist, PN->incoming_values());
      continue;
    }

    // Otherwise be conservative.
    return ModRefInfo::ModRef;
  } while (!Worklist.empty() && --MaxLookup);

  // If we hit the maximum number of instructions to examine, be conservative.
  if (!Worklist.empty())
    return ModRefInfo::ModRef;

  return Result;
}

static bool isIntrinsicCall(const CallBase *Call, Intrinsic::ID IID) {
  const IntrinsicInst *II = dyn_cast<IntrinsicInst>(Call);
  return II && II->getIntrinsicID() == IID;
}

/// Returns the behavior when calling the given call site.
MemoryEffects BasicAAResult::getMemoryEffects(const CallBase *Call,
                                              AAQueryInfo &AAQI) {
  MemoryEffects Min = Call->getAttributes().getMemoryEffects();

  if (const Function *F = dyn_cast<Function>(Call->getCalledOperand())) {
    MemoryEffects FuncME = AAQI.AAR.getMemoryEffects(F);
    // Operand bundles on the call may also read or write memory, in addition
    // to the behavior of the called function.
    if (Call->hasReadingOperandBundles())
      FuncME |= MemoryEffects::readOnly();
    if (Call->hasClobberingOperandBundles())
      FuncME |= MemoryEffects::writeOnly();
    Min &= FuncME;
  }

  return Min;
}

/// Returns the behavior when calling the given function. For use when the call
/// site is not known.
MemoryEffects BasicAAResult::getMemoryEffects(const Function *F) {
  switch (F->getIntrinsicID()) {
  case Intrinsic::experimental_guard:
  case Intrinsic::experimental_deoptimize:
    // These intrinsics can read arbitrary memory, and additionally modref
    // inaccessible memory to model control dependence.
    return MemoryEffects::readOnly() |
           MemoryEffects::inaccessibleMemOnly(ModRefInfo::ModRef);
  }

  return F->getMemoryEffects();
}

ModRefInfo BasicAAResult::getArgModRefInfo(const CallBase *Call,
                                           unsigned ArgIdx) {
  if (Call->paramHasAttr(ArgIdx, Attribute::WriteOnly))
    return ModRefInfo::Mod;

  if (Call->paramHasAttr(ArgIdx, Attribute::ReadOnly))
    return ModRefInfo::Ref;

  if (Call->paramHasAttr(ArgIdx, Attribute::ReadNone))
    return ModRefInfo::NoModRef;

  return ModRefInfo::ModRef;
}

#ifndef NDEBUG
static const Function *getParent(const Value *V) {
  if (const Instruction *inst = dyn_cast<Instruction>(V)) {
    if (!inst->getParent())
      return nullptr;
    return inst->getParent()->getParent();
  }

  if (const Argument *arg = dyn_cast<Argument>(V))
    return arg->getParent();

  return nullptr;
}

static bool notDifferentParent(const Value *O1, const Value *O2) {

  const Function *F1 = getParent(O1);
  const Function *F2 = getParent(O2);

  return !F1 || !F2 || F1 == F2;
}
#endif

AliasResult BasicAAResult::alias(const MemoryLocation &LocA,
                                 const MemoryLocation &LocB, AAQueryInfo &AAQI,
                                 const Instruction *CtxI) {
  assert(notDifferentParent(LocA.Ptr, LocB.Ptr) &&
         "BasicAliasAnalysis doesn't support interprocedural queries.");
  return aliasCheck(LocA.Ptr, LocA.Size, LocB.Ptr, LocB.Size, AAQI, CtxI);
}

/// Checks to see if the specified callsite can clobber the specified memory
/// object.
///
/// Since we only look at local properties of this function, we really can't
/// say much about this query.  We do, however, use simple "address taken"
/// analysis on local objects.
ModRefInfo BasicAAResult::getModRefInfo(const CallBase *Call,
                                        const MemoryLocation &Loc,
                                        AAQueryInfo &AAQI) {
  assert(notDifferentParent(Call, Loc.Ptr) &&
         "AliasAnalysis query involving multiple functions!");

  const Value *Object = getUnderlyingObject(Loc.Ptr);

  // Calls marked 'tail' cannot read or write allocas from the current frame
  // because the current frame might be destroyed by the time they run. However,
  // a tail call may use an alloca with byval. Calling with byval copies the
  // contents of the alloca into argument registers or stack slots, so there is
  // no lifetime issue.
  if (isa<AllocaInst>(Object))
    if (const CallInst *CI = dyn_cast<CallInst>(Call))
      if (CI->isTailCall() &&
          !CI->getAttributes().hasAttrSomewhere(Attribute::ByVal))
        return ModRefInfo::NoModRef;

  // Stack restore is able to modify unescaped dynamic allocas. Assume it may
  // modify them even though the alloca is not escaped.
  if (auto *AI = dyn_cast<AllocaInst>(Object))
    if (!AI->isStaticAlloca() && isIntrinsicCall(Call, Intrinsic::stackrestore))
      return ModRefInfo::Mod;

  // A call can access a locally allocated object either because it is passed as
  // an argument to the call, or because it has escaped prior to the call.
  //
  // Make sure the object has not escaped here, and then check that none of the
  // call arguments alias the object below.
  if (!isa<Constant>(Object) && Call != Object &&
      AAQI.CI->isNotCapturedBefore(Object, Call, /*OrAt*/ false)) {

    // Optimistically assume that call doesn't touch Object and check this
    // assumption in the following loop.
    ModRefInfo Result = ModRefInfo::NoModRef;

    unsigned OperandNo = 0;
    for (auto CI = Call->data_operands_begin(), CE = Call->data_operands_end();
         CI != CE; ++CI, ++OperandNo) {
      if (!(*CI)->getType()->isPointerTy())
        continue;

      // Call doesn't access memory through this operand, so we don't care
      // if it aliases with Object.
      if (Call->doesNotAccessMemory(OperandNo))
        continue;

      // If this is a no-capture pointer argument, see if we can tell that it
      // is impossible to alias the pointer we're checking.
      AliasResult AR =
          AAQI.AAR.alias(MemoryLocation::getBeforeOrAfter(*CI),
                         MemoryLocation::getBeforeOrAfter(Object), AAQI);
      // Operand doesn't alias 'Object', continue looking for other aliases
      if (AR == AliasResult::NoAlias)
        continue;
      // Operand aliases 'Object', but call doesn't modify it. Strengthen
      // initial assumption and keep looking in case if there are more aliases.
      if (Call->onlyReadsMemory(OperandNo)) {
        Result |= ModRefInfo::Ref;
        continue;
      }
      // Operand aliases 'Object' but call only writes into it.
      if (Call->onlyWritesMemory(OperandNo)) {
        Result |= ModRefInfo::Mod;
        continue;
      }
      // This operand aliases 'Object' and call reads and writes into it.
      // Setting ModRef will not yield an early return below, MustAlias is not
      // used further.
      Result = ModRefInfo::ModRef;
      break;
    }

    // Early return if we improved mod ref information
    if (!isModAndRefSet(Result))
      return Result;
  }

  // If the call is malloc/calloc like, we can assume that it doesn't
  // modify any IR visible value.  This is only valid because we assume these
  // routines do not read values visible in the IR.  TODO: Consider special
  // casing realloc and strdup routines which access only their arguments as
  // well.  Or alternatively, replace all of this with inaccessiblememonly once
  // that's implemented fully.
  if (isMallocOrCallocLikeFn(Call, &TLI)) {
    // Be conservative if the accessed pointer may alias the allocation -
    // fallback to the generic handling below.
    if (AAQI.AAR.alias(MemoryLocation::getBeforeOrAfter(Call), Loc, AAQI) ==
        AliasResult::NoAlias)
      return ModRefInfo::NoModRef;
  }

  // Like assumes, invariant.start intrinsics were also marked as arbitrarily
  // writing so that proper control dependencies are maintained but they never
  // mod any particular memory location visible to the IR.
  // *Unlike* assumes (which are now modeled as NoModRef), invariant.start
  // intrinsic is now modeled as reading memory. This prevents hoisting the
  // invariant.start intrinsic over stores. Consider:
  // *ptr = 40;
  // *ptr = 50;
  // invariant_start(ptr)
  // int val = *ptr;
  // print(val);
  //
  // This cannot be transformed to:
  //
  // *ptr = 40;
  // invariant_start(ptr)
  // *ptr = 50;
  // int val = *ptr;
  // print(val);
  //
  // The transformation will cause the second store to be ignored (based on
  // rules of invariant.start)  and print 40, while the first program always
  // prints 50.
  if (isIntrinsicCall(Call, Intrinsic::invariant_start))
    return ModRefInfo::Ref;

  // Be conservative.
  return ModRefInfo::ModRef;
}

ModRefInfo BasicAAResult::getModRefInfo(const CallBase *Call1,
                                        const CallBase *Call2,
                                        AAQueryInfo &AAQI) {
  // Guard intrinsics are marked as arbitrarily writing so that proper control
  // dependencies are maintained but they never mods any particular memory
  // location.
  //
  // *Unlike* assumes, guard intrinsics are modeled as reading memory since the
  // heap state at the point the guard is issued needs to be consistent in case
  // the guard invokes the "deopt" continuation.

  // NB! This function is *not* commutative, so we special case two
  // possibilities for guard intrinsics.

  if (isIntrinsicCall(Call1, Intrinsic::experimental_guard))
    return isModSet(getMemoryEffects(Call2, AAQI).getModRef())
               ? ModRefInfo::Ref
               : ModRefInfo::NoModRef;

  if (isIntrinsicCall(Call2, Intrinsic::experimental_guard))
    return isModSet(getMemoryEffects(Call1, AAQI).getModRef())
               ? ModRefInfo::Mod
               : ModRefInfo::NoModRef;

  // Be conservative.
  return ModRefInfo::ModRef;
}

/// Return true if we know V to the base address of the corresponding memory
/// object.  This implies that any address less than V must be out of bounds
/// for the underlying object.  Note that just being isIdentifiedObject() is
/// not enough - For example, a negative offset from a noalias argument or call
/// can be inbounds w.r.t the actual underlying object.
static bool isBaseOfObject(const Value *V) {
  // TODO: We can handle other cases here
  // 1) For GC languages, arguments to functions are often required to be
  //    base pointers.
  // 2) Result of allocation routines are often base pointers.  Leverage TLI.
  return (isa<AllocaInst>(V) || isa<GlobalVariable>(V));
}

/// Provides a bunch of ad-hoc rules to disambiguate a GEP instruction against
/// another pointer.
///
/// We know that V1 is a GEP, but we don't know anything about V2.
/// UnderlyingV1 is getUnderlyingObject(GEP1), UnderlyingV2 is the same for
/// V2.
AliasResult BasicAAResult::aliasGEP(
    const GEPOperator *GEP1, LocationSize V1Size,
    const Value *V2, LocationSize V2Size,
    const Value *UnderlyingV1, const Value *UnderlyingV2, AAQueryInfo &AAQI) {
  if (!V1Size.hasValue() && !V2Size.hasValue()) {
    // TODO: This limitation exists for compile-time reasons. Relax it if we
    // can avoid exponential pathological cases.
    if (!isa<GEPOperator>(V2))
      return AliasResult::MayAlias;

    // If both accesses have unknown size, we can only check whether the base
    // objects don't alias.
    AliasResult BaseAlias =
        AAQI.AAR.alias(MemoryLocation::getBeforeOrAfter(UnderlyingV1),
                       MemoryLocation::getBeforeOrAfter(UnderlyingV2), AAQI);
    return BaseAlias == AliasResult::NoAlias ? AliasResult::NoAlias
                                             : AliasResult::MayAlias;
  }

  DominatorTree *DT = getDT(AAQI);
  DecomposedGEP DecompGEP1 = DecomposeGEPExpression(GEP1, DL, &AC, DT);
  DecomposedGEP DecompGEP2 = DecomposeGEPExpression(V2, DL, &AC, DT);

  // Bail if we were not able to decompose anything.
  if (DecompGEP1.Base == GEP1 && DecompGEP2.Base == V2)
    return AliasResult::MayAlias;

  // Subtract the GEP2 pointer from the GEP1 pointer to find out their
  // symbolic difference.
  subtractDecomposedGEPs(DecompGEP1, DecompGEP2, AAQI);

  // If an inbounds GEP would have to start from an out of bounds address
  // for the two to alias, then we can assume noalias.
  // TODO: Remove !isScalable() once BasicAA fully support scalable location
  // size
  if (*DecompGEP1.InBounds && DecompGEP1.VarIndices.empty() &&
      V2Size.hasValue() && !V2Size.isScalable() &&
      DecompGEP1.Offset.sge(V2Size.getValue()) &&
      isBaseOfObject(DecompGEP2.Base))
    return AliasResult::NoAlias;

  if (isa<GEPOperator>(V2)) {
    // Symmetric case to above.
    if (*DecompGEP2.InBounds && DecompGEP1.VarIndices.empty() &&
        V1Size.hasValue() && !V1Size.isScalable() &&
        DecompGEP1.Offset.sle(-V1Size.getValue()) &&
        isBaseOfObject(DecompGEP1.Base))
      return AliasResult::NoAlias;
  }

  // For GEPs with identical offsets, we can preserve the size and AAInfo
  // when performing the alias check on the underlying objects.
  if (DecompGEP1.Offset == 0 && DecompGEP1.VarIndices.empty())
    return AAQI.AAR.alias(MemoryLocation(DecompGEP1.Base, V1Size),
                          MemoryLocation(DecompGEP2.Base, V2Size), AAQI);

  // Do the base pointers alias?
  AliasResult BaseAlias =
      AAQI.AAR.alias(MemoryLocation::getBeforeOrAfter(DecompGEP1.Base),
                     MemoryLocation::getBeforeOrAfter(DecompGEP2.Base), AAQI);

  // If we get a No or May, then return it immediately, no amount of analysis
  // will improve this situation.
  if (BaseAlias != AliasResult::MustAlias) {
    assert(BaseAlias == AliasResult::NoAlias ||
           BaseAlias == AliasResult::MayAlias);
    return BaseAlias;
  }

  // If there is a constant difference between the pointers, but the difference
  // is less than the size of the associated memory object, then we know
  // that the objects are partially overlapping.  If the difference is
  // greater, we know they do not overlap.
  if (DecompGEP1.VarIndices.empty()) {
    APInt &Off = DecompGEP1.Offset;

    // Initialize for Off >= 0 (V2 <= GEP1) case.
    const Value *LeftPtr = V2;
    const Value *RightPtr = GEP1;
    LocationSize VLeftSize = V2Size;
    LocationSize VRightSize = V1Size;
    const bool Swapped = Off.isNegative();

    if (Swapped) {
      // Swap if we have the situation where:
      // +                +
      // | BaseOffset     |
      // ---------------->|
      // |-->V1Size       |-------> V2Size
      // GEP1             V2
      std::swap(LeftPtr, RightPtr);
      std::swap(VLeftSize, VRightSize);
      Off = -Off;
    }

    if (!VLeftSize.hasValue())
      return AliasResult::MayAlias;

    const TypeSize LSize = VLeftSize.getValue();
    if (!LSize.isScalable()) {
      if (Off.ult(LSize)) {
        // Conservatively drop processing if a phi was visited and/or offset is
        // too big.
        AliasResult AR = AliasResult::PartialAlias;
        if (VRightSize.hasValue() && !VRightSize.isScalable() &&
            Off.ule(INT32_MAX) && (Off + VRightSize.getValue()).ule(LSize)) {
          // Memory referenced by right pointer is nested. Save the offset in
          // cache. Note that originally offset estimated as GEP1-V2, but
          // AliasResult contains the shift that represents GEP1+Offset=V2.
          AR.setOffset(-Off.getSExtValue());
          AR.swap(Swapped);
        }
        return AR;
      }
      return AliasResult::NoAlias;
    } else {
      // We can use the getVScaleRange to prove that Off >= (CR.upper * LSize).
      ConstantRange CR = getVScaleRange(&F, Off.getBitWidth());
      bool Overflow;
      APInt UpperRange = CR.getUnsignedMax().umul_ov(
          APInt(Off.getBitWidth(), LSize.getKnownMinValue()), Overflow);
      if (!Overflow && Off.uge(UpperRange))
        return AliasResult::NoAlias;
    }
  }

  // VScale Alias Analysis - Given one scalable offset between accesses and a
  // scalable typesize, we can divide each side by vscale, treating both values
  // as a constant. We prove that Offset/vscale >= TypeSize/vscale.
  if (DecompGEP1.VarIndices.size() == 1 &&
      DecompGEP1.VarIndices[0].Val.TruncBits == 0 &&
      DecompGEP1.Offset.isZero() &&
      PatternMatch::match(DecompGEP1.VarIndices[0].Val.V,
                          PatternMatch::m_VScale())) {
    const VariableGEPIndex &ScalableVar = DecompGEP1.VarIndices[0];
    APInt Scale =
        ScalableVar.IsNegated ? -ScalableVar.Scale : ScalableVar.Scale;
    LocationSize VLeftSize = Scale.isNegative() ? V1Size : V2Size;

    // Check if the offset is known to not overflow, if it does then attempt to
    // prove it with the known values of vscale_range.
    bool Overflows = !DecompGEP1.VarIndices[0].IsNSW;
    if (Overflows) {
      ConstantRange CR = getVScaleRange(&F, Scale.getBitWidth());
      (void)CR.getSignedMax().smul_ov(Scale, Overflows);
    }

    if (!Overflows) {
      // Note that we do not check that the typesize is scalable, as vscale >= 1
      // so noalias still holds so long as the dependency distance is at least
      // as big as the typesize.
      if (VLeftSize.hasValue() &&
          Scale.abs().uge(VLeftSize.getValue().getKnownMinValue()))
        return AliasResult::NoAlias;
    }
  }

  // Bail on analysing scalable LocationSize
  if (V1Size.isScalable() || V2Size.isScalable())
    return AliasResult::MayAlias;

  // We need to know both acess sizes for all the following heuristics.
  if (!V1Size.hasValue() || !V2Size.hasValue())
    return AliasResult::MayAlias;

  APInt GCD;
  ConstantRange OffsetRange = ConstantRange(DecompGEP1.Offset);
  for (unsigned i = 0, e = DecompGEP1.VarIndices.size(); i != e; ++i) {
    const VariableGEPIndex &Index = DecompGEP1.VarIndices[i];
    const APInt &Scale = Index.Scale;
    APInt ScaleForGCD = Scale;
    if (!Index.IsNSW)
      ScaleForGCD =
          APInt::getOneBitSet(Scale.getBitWidth(), Scale.countr_zero());

    if (i == 0)
      GCD = ScaleForGCD.abs();
    else
      GCD = APIntOps::GreatestCommonDivisor(GCD, ScaleForGCD.abs());

    ConstantRange CR = computeConstantRange(Index.Val.V, /* ForSigned */ false,
                                            true, &AC, Index.CxtI);
    KnownBits Known =
        computeKnownBits(Index.Val.V, DL, 0, &AC, Index.CxtI, DT);
    CR = CR.intersectWith(
        ConstantRange::fromKnownBits(Known, /* Signed */ true),
        ConstantRange::Signed);
    CR = Index.Val.evaluateWith(CR).sextOrTrunc(OffsetRange.getBitWidth());

    assert(OffsetRange.getBitWidth() == Scale.getBitWidth() &&
           "Bit widths are normalized to MaxIndexSize");
    if (Index.IsNSW)
      CR = CR.smul_sat(ConstantRange(Scale));
    else
      CR = CR.smul_fast(ConstantRange(Scale));

    if (Index.IsNegated)
      OffsetRange = OffsetRange.sub(CR);
    else
      OffsetRange = OffsetRange.add(CR);
  }

  // We now have accesses at two offsets from the same base:
  //  1. (...)*GCD + DecompGEP1.Offset with size V1Size
  //  2. 0 with size V2Size
  // Using arithmetic modulo GCD, the accesses are at
  // [ModOffset..ModOffset+V1Size) and [0..V2Size). If the first access fits
  // into the range [V2Size..GCD), then we know they cannot overlap.
  APInt ModOffset = DecompGEP1.Offset.srem(GCD);
  if (ModOffset.isNegative())
    ModOffset += GCD; // We want mod, not rem.
  if (ModOffset.uge(V2Size.getValue()) &&
      (GCD - ModOffset).uge(V1Size.getValue()))
    return AliasResult::NoAlias;

  // Compute ranges of potentially accessed bytes for both accesses. If the
  // interseciton is empty, there can be no overlap.
  unsigned BW = OffsetRange.getBitWidth();
  ConstantRange Range1 = OffsetRange.add(
      ConstantRange(APInt(BW, 0), APInt(BW, V1Size.getValue())));
  ConstantRange Range2 =
      ConstantRange(APInt(BW, 0), APInt(BW, V2Size.getValue()));
  if (Range1.intersectWith(Range2).isEmptySet())
    return AliasResult::NoAlias;

  // Try to determine the range of values for VarIndex such that
  // VarIndex <= -MinAbsVarIndex || MinAbsVarIndex <= VarIndex.
  std::optional<APInt> MinAbsVarIndex;
  if (DecompGEP1.VarIndices.size() == 1) {
    // VarIndex = Scale*V.
    const VariableGEPIndex &Var = DecompGEP1.VarIndices[0];
    if (Var.Val.TruncBits == 0 &&
        isKnownNonZero(Var.Val.V, SimplifyQuery(DL, DT, &AC, Var.CxtI))) {
      // Check if abs(V*Scale) >= abs(Scale) holds in the presence of
      // potentially wrapping math.
      auto MultiplyByScaleNoWrap = [](const VariableGEPIndex &Var) {
        if (Var.IsNSW)
          return true;

        int ValOrigBW = Var.Val.V->getType()->getPrimitiveSizeInBits();
        // If Scale is small enough so that abs(V*Scale) >= abs(Scale) holds.
        // The max value of abs(V) is 2^ValOrigBW - 1. Multiplying with a
        // constant smaller than 2^(bitwidth(Val) - ValOrigBW) won't wrap.
        int MaxScaleValueBW = Var.Val.getBitWidth() - ValOrigBW;
        if (MaxScaleValueBW <= 0)
          return false;
        return Var.Scale.ule(
            APInt::getMaxValue(MaxScaleValueBW).zext(Var.Scale.getBitWidth()));
      };
      // Refine MinAbsVarIndex, if abs(Scale*V) >= abs(Scale) holds in the
      // presence of potentially wrapping math.
      if (MultiplyByScaleNoWrap(Var)) {
        // If V != 0 then abs(VarIndex) >= abs(Scale).
        MinAbsVarIndex = Var.Scale.abs();
      }
    }
  } else if (DecompGEP1.VarIndices.size() == 2) {
    // VarIndex = Scale*V0 + (-Scale)*V1.
    // If V0 != V1 then abs(VarIndex) >= abs(Scale).
    // Check that MayBeCrossIteration is false, to avoid reasoning about
    // inequality of values across loop iterations.
    const VariableGEPIndex &Var0 = DecompGEP1.VarIndices[0];
    const VariableGEPIndex &Var1 = DecompGEP1.VarIndices[1];
    if (Var0.hasNegatedScaleOf(Var1) && Var0.Val.TruncBits == 0 &&
        Var0.Val.hasSameCastsAs(Var1.Val) && !AAQI.MayBeCrossIteration &&
        isKnownNonEqual(Var0.Val.V, Var1.Val.V, DL, &AC, /* CxtI */ nullptr,
                        DT))
      MinAbsVarIndex = Var0.Scale.abs();
  }

  if (MinAbsVarIndex) {
    // The constant offset will have added at least +/-MinAbsVarIndex to it.
    APInt OffsetLo = DecompGEP1.Offset - *MinAbsVarIndex;
    APInt OffsetHi = DecompGEP1.Offset + *MinAbsVarIndex;
    // We know that Offset <= OffsetLo || Offset >= OffsetHi
    if (OffsetLo.isNegative() && (-OffsetLo).uge(V1Size.getValue()) &&
        OffsetHi.isNonNegative() && OffsetHi.uge(V2Size.getValue()))
      return AliasResult::NoAlias;
  }

  if (constantOffsetHeuristic(DecompGEP1, V1Size, V2Size, &AC, DT, AAQI))
    return AliasResult::NoAlias;

  // Statically, we can see that the base objects are the same, but the
  // pointers have dynamic offsets which we can't resolve. And none of our
  // little tricks above worked.
  return AliasResult::MayAlias;
}

static AliasResult MergeAliasResults(AliasResult A, AliasResult B) {
  // If the results agree, take it.
  if (A == B)
    return A;
  // A mix of PartialAlias and MustAlias is PartialAlias.
  if ((A == AliasResult::PartialAlias && B == AliasResult::MustAlias) ||
      (B == AliasResult::PartialAlias && A == AliasResult::MustAlias))
    return AliasResult::PartialAlias;
  // Otherwise, we don't know anything.
  return AliasResult::MayAlias;
}

/// Provides a bunch of ad-hoc rules to disambiguate a Select instruction
/// against another.
AliasResult
BasicAAResult::aliasSelect(const SelectInst *SI, LocationSize SISize,
                           const Value *V2, LocationSize V2Size,
                           AAQueryInfo &AAQI) {
  // If the values are Selects with the same condition, we can do a more precise
  // check: just check for aliases between the values on corresponding arms.
  if (const SelectInst *SI2 = dyn_cast<SelectInst>(V2))
    if (isValueEqualInPotentialCycles(SI->getCondition(), SI2->getCondition(),
                                      AAQI)) {
      AliasResult Alias =
          AAQI.AAR.alias(MemoryLocation(SI->getTrueValue(), SISize),
                         MemoryLocation(SI2->getTrueValue(), V2Size), AAQI);
      if (Alias == AliasResult::MayAlias)
        return AliasResult::MayAlias;
      AliasResult ThisAlias =
          AAQI.AAR.alias(MemoryLocation(SI->getFalseValue(), SISize),
                         MemoryLocation(SI2->getFalseValue(), V2Size), AAQI);
      return MergeAliasResults(ThisAlias, Alias);
    }

  // If both arms of the Select node NoAlias or MustAlias V2, then returns
  // NoAlias / MustAlias. Otherwise, returns MayAlias.
  AliasResult Alias = AAQI.AAR.alias(MemoryLocation(SI->getTrueValue(), SISize),
                                     MemoryLocation(V2, V2Size), AAQI);
  if (Alias == AliasResult::MayAlias)
    return AliasResult::MayAlias;

  AliasResult ThisAlias =
      AAQI.AAR.alias(MemoryLocation(SI->getFalseValue(), SISize),
                     MemoryLocation(V2, V2Size), AAQI);
  return MergeAliasResults(ThisAlias, Alias);
}

/// Provide a bunch of ad-hoc rules to disambiguate a PHI instruction against
/// another.
AliasResult BasicAAResult::aliasPHI(const PHINode *PN, LocationSize PNSize,
                                    const Value *V2, LocationSize V2Size,
                                    AAQueryInfo &AAQI) {
  if (!PN->getNumIncomingValues())
    return AliasResult::NoAlias;
  // If the values are PHIs in the same block, we can do a more precise
  // as well as efficient check: just check for aliases between the values
  // on corresponding edges.
  if (const PHINode *PN2 = dyn_cast<PHINode>(V2))
    if (PN2->getParent() == PN->getParent()) {
      std::optional<AliasResult> Alias;
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
        AliasResult ThisAlias = AAQI.AAR.alias(
            MemoryLocation(PN->getIncomingValue(i), PNSize),
            MemoryLocation(
                PN2->getIncomingValueForBlock(PN->getIncomingBlock(i)), V2Size),
            AAQI);
        if (Alias)
          *Alias = MergeAliasResults(*Alias, ThisAlias);
        else
          Alias = ThisAlias;
        if (*Alias == AliasResult::MayAlias)
          break;
      }
      return *Alias;
    }

  SmallVector<Value *, 4> V1Srcs;
  // If a phi operand recurses back to the phi, we can still determine NoAlias
  // if we don't alias the underlying objects of the other phi operands, as we
  // know that the recursive phi needs to be based on them in some way.
  bool isRecursive = false;
  auto CheckForRecPhi = [&](Value *PV) {
    if (!EnableRecPhiAnalysis)
      return false;
    if (getUnderlyingObject(PV) == PN) {
      isRecursive = true;
      return true;
    }
    return false;
  };

  SmallPtrSet<Value *, 4> UniqueSrc;
  Value *OnePhi = nullptr;
  for (Value *PV1 : PN->incoming_values()) {
    // Skip the phi itself being the incoming value.
    if (PV1 == PN)
      continue;

    if (isa<PHINode>(PV1)) {
      if (OnePhi && OnePhi != PV1) {
        // To control potential compile time explosion, we choose to be
        // conserviate when we have more than one Phi input.  It is important
        // that we handle the single phi case as that lets us handle LCSSA
        // phi nodes and (combined with the recursive phi handling) simple
        // pointer induction variable patterns.
        return AliasResult::MayAlias;
      }
      OnePhi = PV1;
    }

    if (CheckForRecPhi(PV1))
      continue;

    if (UniqueSrc.insert(PV1).second)
      V1Srcs.push_back(PV1);
  }

  if (OnePhi && UniqueSrc.size() > 1)
    // Out of an abundance of caution, allow only the trivial lcssa and
    // recursive phi cases.
    return AliasResult::MayAlias;

  // If V1Srcs is empty then that means that the phi has no underlying non-phi
  // value. This should only be possible in blocks unreachable from the entry
  // block, but return MayAlias just in case.
  if (V1Srcs.empty())
    return AliasResult::MayAlias;

  // If this PHI node is recursive, indicate that the pointer may be moved
  // across iterations. We can only prove NoAlias if different underlying
  // objects are involved.
  if (isRecursive)
    PNSize = LocationSize::beforeOrAfterPointer();

  // In the recursive alias queries below, we may compare values from two
  // different loop iterations.
  SaveAndRestore SavedMayBeCrossIteration(AAQI.MayBeCrossIteration, true);

  AliasResult Alias = AAQI.AAR.alias(MemoryLocation(V1Srcs[0], PNSize),
                                     MemoryLocation(V2, V2Size), AAQI);

  // Early exit if the check of the first PHI source against V2 is MayAlias.
  // Other results are not possible.
  if (Alias == AliasResult::MayAlias)
    return AliasResult::MayAlias;
  // With recursive phis we cannot guarantee that MustAlias/PartialAlias will
  // remain valid to all elements and needs to conservatively return MayAlias.
  if (isRecursive && Alias != AliasResult::NoAlias)
    return AliasResult::MayAlias;

  // If all sources of the PHI node NoAlias or MustAlias V2, then returns
  // NoAlias / MustAlias. Otherwise, returns MayAlias.
  for (unsigned i = 1, e = V1Srcs.size(); i != e; ++i) {
    Value *V = V1Srcs[i];

    AliasResult ThisAlias = AAQI.AAR.alias(
        MemoryLocation(V, PNSize), MemoryLocation(V2, V2Size), AAQI);
    Alias = MergeAliasResults(ThisAlias, Alias);
    if (Alias == AliasResult::MayAlias)
      break;
  }

  return Alias;
}

/// Provides a bunch of ad-hoc rules to disambiguate in common cases, such as
/// array references.
AliasResult BasicAAResult::aliasCheck(const Value *V1, LocationSize V1Size,
                                      const Value *V2, LocationSize V2Size,
                                      AAQueryInfo &AAQI,
                                      const Instruction *CtxI) {
  // If either of the memory references is empty, it doesn't matter what the
  // pointer values are.
  if (V1Size.isZero() || V2Size.isZero())
    return AliasResult::NoAlias;

  // Strip off any casts if they exist.
  V1 = V1->stripPointerCastsForAliasAnalysis();
  V2 = V2->stripPointerCastsForAliasAnalysis();

  // If V1 or V2 is undef, the result is NoAlias because we can always pick a
  // value for undef that aliases nothing in the program.
  if (isa<UndefValue>(V1) || isa<UndefValue>(V2))
    return AliasResult::NoAlias;

  // Are we checking for alias of the same value?
  // Because we look 'through' phi nodes, we could look at "Value" pointers from
  // different iterations. We must therefore make sure that this is not the
  // case. The function isValueEqualInPotentialCycles ensures that this cannot
  // happen by looking at the visited phi nodes and making sure they cannot
  // reach the value.
  if (isValueEqualInPotentialCycles(V1, V2, AAQI))
    return AliasResult::MustAlias;

  if (!V1->getType()->isPointerTy() || !V2->getType()->isPointerTy())
    return AliasResult::NoAlias; // Scalars cannot alias each other

  // Figure out what objects these things are pointing to if we can.
  const Value *O1 = getUnderlyingObject(V1, MaxLookupSearchDepth);
  const Value *O2 = getUnderlyingObject(V2, MaxLookupSearchDepth);

  // Null values in the default address space don't point to any object, so they
  // don't alias any other pointer.
  if (const ConstantPointerNull *CPN = dyn_cast<ConstantPointerNull>(O1))
    if (!NullPointerIsDefined(&F, CPN->getType()->getAddressSpace()))
      return AliasResult::NoAlias;
  if (const ConstantPointerNull *CPN = dyn_cast<ConstantPointerNull>(O2))
    if (!NullPointerIsDefined(&F, CPN->getType()->getAddressSpace()))
      return AliasResult::NoAlias;

  if (O1 != O2) {
    // If V1/V2 point to two different objects, we know that we have no alias.
    if (isIdentifiedObject(O1) && isIdentifiedObject(O2))
      return AliasResult::NoAlias;

    // Function arguments can't alias with things that are known to be
    // unambigously identified at the function level.
    if ((isa<Argument>(O1) && isIdentifiedFunctionLocal(O2)) ||
        (isa<Argument>(O2) && isIdentifiedFunctionLocal(O1)))
      return AliasResult::NoAlias;

    // If one pointer is the result of a call/invoke or load and the other is a
    // non-escaping local object within the same function, then we know the
    // object couldn't escape to a point where the call could return it.
    //
    // Note that if the pointers are in different functions, there are a
    // variety of complications. A call with a nocapture argument may still
    // temporary store the nocapture argument's value in a temporary memory
    // location if that memory location doesn't escape. Or it may pass a
    // nocapture value to other functions as long as they don't capture it.
    if (isEscapeSource(O1) && AAQI.CI->isNotCapturedBefore(
                                  O2, dyn_cast<Instruction>(O1), /*OrAt*/ true))
      return AliasResult::NoAlias;
    if (isEscapeSource(O2) && AAQI.CI->isNotCapturedBefore(
                                  O1, dyn_cast<Instruction>(O2), /*OrAt*/ true))
      return AliasResult::NoAlias;
  }

  // If the size of one access is larger than the entire object on the other
  // side, then we know such behavior is undefined and can assume no alias.
  bool NullIsValidLocation = NullPointerIsDefined(&F);
  if ((isObjectSmallerThan(
          O2, getMinimalExtentFrom(*V1, V1Size, DL, NullIsValidLocation), DL,
          TLI, NullIsValidLocation)) ||
      (isObjectSmallerThan(
          O1, getMinimalExtentFrom(*V2, V2Size, DL, NullIsValidLocation), DL,
          TLI, NullIsValidLocation)))
    return AliasResult::NoAlias;

  if (EnableSeparateStorageAnalysis) {
    for (AssumptionCache::ResultElem &Elem : AC.assumptionsFor(O1)) {
      if (!Elem || Elem.Index == AssumptionCache::ExprResultIdx)
        continue;

      AssumeInst *Assume = cast<AssumeInst>(Elem);
      OperandBundleUse OBU = Assume->getOperandBundleAt(Elem.Index);
      if (OBU.getTagName() == "separate_storage") {
        assert(OBU.Inputs.size() == 2);
        const Value *Hint1 = OBU.Inputs[0].get();
        const Value *Hint2 = OBU.Inputs[1].get();
        // This is often a no-op; instcombine rewrites this for us. No-op
        // getUnderlyingObject calls are fast, though.
        const Value *HintO1 = getUnderlyingObject(Hint1);
        const Value *HintO2 = getUnderlyingObject(Hint2);

        DominatorTree *DT = getDT(AAQI);
        auto ValidAssumeForPtrContext = [&](const Value *Ptr) {
          if (const Instruction *PtrI = dyn_cast<Instruction>(Ptr)) {
            return isValidAssumeForContext(Assume, PtrI, DT,
                                           /* AllowEphemerals */ true);
          }
          if (const Argument *PtrA = dyn_cast<Argument>(Ptr)) {
            const Instruction *FirstI =
                &*PtrA->getParent()->getEntryBlock().begin();
            return isValidAssumeForContext(Assume, FirstI, DT,
                                           /* AllowEphemerals */ true);
          }
          return false;
        };

        if ((O1 == HintO1 && O2 == HintO2) || (O1 == HintO2 && O2 == HintO1)) {
          // Note that we go back to V1 and V2 for the
          // ValidAssumeForPtrContext checks; they're dominated by O1 and O2,
          // so strictly more assumptions are valid for them.
          if ((CtxI && isValidAssumeForContext(Assume, CtxI, DT,
                                               /* AllowEphemerals */ true)) ||
              ValidAssumeForPtrContext(V1) || ValidAssumeForPtrContext(V2)) {
            return AliasResult::NoAlias;
          }
        }
      }
    }
  }

  // If one the accesses may be before the accessed pointer, canonicalize this
  // by using unknown after-pointer sizes for both accesses. This is
  // equivalent, because regardless of which pointer is lower, one of them
  // will always came after the other, as long as the underlying objects aren't
  // disjoint. We do this so that the rest of BasicAA does not have to deal
  // with accesses before the base pointer, and to improve cache utilization by
  // merging equivalent states.
  if (V1Size.mayBeBeforePointer() || V2Size.mayBeBeforePointer()) {
    V1Size = LocationSize::afterPointer();
    V2Size = LocationSize::afterPointer();
  }

  // FIXME: If this depth limit is hit, then we may cache sub-optimal results
  // for recursive queries. For this reason, this limit is chosen to be large
  // enough to be very rarely hit, while still being small enough to avoid
  // stack overflows.
  if (AAQI.Depth >= 512)
    return AliasResult::MayAlias;

  // Check the cache before climbing up use-def chains. This also terminates
  // otherwise infinitely recursive queries. Include MayBeCrossIteration in the
  // cache key, because some cases where MayBeCrossIteration==false returns
  // MustAlias or NoAlias may become MayAlias under MayBeCrossIteration==true.
  AAQueryInfo::LocPair Locs({V1, V1Size, AAQI.MayBeCrossIteration},
                            {V2, V2Size, AAQI.MayBeCrossIteration});
  const bool Swapped = V1 > V2;
  if (Swapped)
    std::swap(Locs.first, Locs.second);
  const auto &Pair = AAQI.AliasCache.try_emplace(
      Locs, AAQueryInfo::CacheEntry{AliasResult::NoAlias, 0});
  if (!Pair.second) {
    auto &Entry = Pair.first->second;
    if (!Entry.isDefinitive()) {
      // Remember that we used an assumption. This may either be a direct use
      // of an assumption, or a use of an entry that may itself be based on an
      // assumption.
      ++AAQI.NumAssumptionUses;
      if (Entry.isAssumption())
        ++Entry.NumAssumptionUses;
    }
    // Cache contains sorted {V1,V2} pairs but we should return original order.
    auto Result = Entry.Result;
    Result.swap(Swapped);
    return Result;
  }

  int OrigNumAssumptionUses = AAQI.NumAssumptionUses;
  unsigned OrigNumAssumptionBasedResults = AAQI.AssumptionBasedResults.size();
  AliasResult Result =
      aliasCheckRecursive(V1, V1Size, V2, V2Size, AAQI, O1, O2);

  auto It = AAQI.AliasCache.find(Locs);
  assert(It != AAQI.AliasCache.end() && "Must be in cache");
  auto &Entry = It->second;

  // Check whether a NoAlias assumption has been used, but disproven.
  bool AssumptionDisproven =
      Entry.NumAssumptionUses > 0 && Result != AliasResult::NoAlias;
  if (AssumptionDisproven)
    Result = AliasResult::MayAlias;

  // This is a definitive result now, when considered as a root query.
  AAQI.NumAssumptionUses -= Entry.NumAssumptionUses;
  Entry.Result = Result;
  // Cache contains sorted {V1,V2} pairs.
  Entry.Result.swap(Swapped);

  // If the assumption has been disproven, remove any results that may have
  // been based on this assumption. Do this after the Entry updates above to
  // avoid iterator invalidation.
  if (AssumptionDisproven)
    while (AAQI.AssumptionBasedResults.size() > OrigNumAssumptionBasedResults)
      AAQI.AliasCache.erase(AAQI.AssumptionBasedResults.pop_back_val());

  // The result may still be based on assumptions higher up in the chain.
  // Remember it, so it can be purged from the cache later.
  if (OrigNumAssumptionUses != AAQI.NumAssumptionUses &&
      Result != AliasResult::MayAlias) {
    AAQI.AssumptionBasedResults.push_back(Locs);
    Entry.NumAssumptionUses = AAQueryInfo::CacheEntry::AssumptionBased;
  } else {
    Entry.NumAssumptionUses = AAQueryInfo::CacheEntry::Definitive;
  }

  // Depth is incremented before this function is called, so Depth==1 indicates
  // a root query.
  if (AAQI.Depth == 1) {
    // Any remaining assumption based results must be based on proven
    // assumptions, so convert them to definitive results.
    for (const auto &Loc : AAQI.AssumptionBasedResults) {
      auto It = AAQI.AliasCache.find(Loc);
      if (It != AAQI.AliasCache.end())
        It->second.NumAssumptionUses = AAQueryInfo::CacheEntry::Definitive;
    }
    AAQI.AssumptionBasedResults.clear();
    AAQI.NumAssumptionUses = 0;
  }
  return Result;
}

AliasResult BasicAAResult::aliasCheckRecursive(
    const Value *V1, LocationSize V1Size,
    const Value *V2, LocationSize V2Size,
    AAQueryInfo &AAQI, const Value *O1, const Value *O2) {
  if (const GEPOperator *GV1 = dyn_cast<GEPOperator>(V1)) {
    AliasResult Result = aliasGEP(GV1, V1Size, V2, V2Size, O1, O2, AAQI);
    if (Result != AliasResult::MayAlias)
      return Result;
  } else if (const GEPOperator *GV2 = dyn_cast<GEPOperator>(V2)) {
    AliasResult Result = aliasGEP(GV2, V2Size, V1, V1Size, O2, O1, AAQI);
    Result.swap();
    if (Result != AliasResult::MayAlias)
      return Result;
  }

  if (const PHINode *PN = dyn_cast<PHINode>(V1)) {
    AliasResult Result = aliasPHI(PN, V1Size, V2, V2Size, AAQI);
    if (Result != AliasResult::MayAlias)
      return Result;
  } else if (const PHINode *PN = dyn_cast<PHINode>(V2)) {
    AliasResult Result = aliasPHI(PN, V2Size, V1, V1Size, AAQI);
    Result.swap();
    if (Result != AliasResult::MayAlias)
      return Result;
  }

  if (const SelectInst *S1 = dyn_cast<SelectInst>(V1)) {
    AliasResult Result = aliasSelect(S1, V1Size, V2, V2Size, AAQI);
    if (Result != AliasResult::MayAlias)
      return Result;
  } else if (const SelectInst *S2 = dyn_cast<SelectInst>(V2)) {
    AliasResult Result = aliasSelect(S2, V2Size, V1, V1Size, AAQI);
    Result.swap();
    if (Result != AliasResult::MayAlias)
      return Result;
  }

  // If both pointers are pointing into the same object and one of them
  // accesses the entire object, then the accesses must overlap in some way.
  if (O1 == O2) {
    bool NullIsValidLocation = NullPointerIsDefined(&F);
    if (V1Size.isPrecise() && V2Size.isPrecise() &&
        (isObjectSize(O1, V1Size.getValue(), DL, TLI, NullIsValidLocation) ||
         isObjectSize(O2, V2Size.getValue(), DL, TLI, NullIsValidLocation)))
      return AliasResult::PartialAlias;
  }

  return AliasResult::MayAlias;
}

/// Check whether two Values can be considered equivalent.
///
/// If the values may come from different cycle iterations, this will also
/// check that the values are not part of cycle. We have to do this because we
/// are looking through phi nodes, that is we say
/// noalias(V, phi(VA, VB)) if noalias(V, VA) and noalias(V, VB).
bool BasicAAResult::isValueEqualInPotentialCycles(const Value *V,
                                                  const Value *V2,
                                                  const AAQueryInfo &AAQI) {
  if (V != V2)
    return false;

  if (!AAQI.MayBeCrossIteration)
    return true;

  // Non-instructions and instructions in the entry block cannot be part of
  // a loop.
  const Instruction *Inst = dyn_cast<Instruction>(V);
  if (!Inst || Inst->getParent()->isEntryBlock())
    return true;

  return isNotInCycle(Inst, getDT(AAQI), /*LI*/ nullptr);
}

/// Computes the symbolic difference between two de-composed GEPs.
void BasicAAResult::subtractDecomposedGEPs(DecomposedGEP &DestGEP,
                                           const DecomposedGEP &SrcGEP,
                                           const AAQueryInfo &AAQI) {
  DestGEP.Offset -= SrcGEP.Offset;
  for (const VariableGEPIndex &Src : SrcGEP.VarIndices) {
    // Find V in Dest.  This is N^2, but pointer indices almost never have more
    // than a few variable indexes.
    bool Found = false;
    for (auto I : enumerate(DestGEP.VarIndices)) {
      VariableGEPIndex &Dest = I.value();
      if ((!isValueEqualInPotentialCycles(Dest.Val.V, Src.Val.V, AAQI) &&
           !areBothVScale(Dest.Val.V, Src.Val.V)) ||
          !Dest.Val.hasSameCastsAs(Src.Val))
        continue;

      // Normalize IsNegated if we're going to lose the NSW flag anyway.
      if (Dest.IsNegated) {
        Dest.Scale = -Dest.Scale;
        Dest.IsNegated = false;
        Dest.IsNSW = false;
      }

      // If we found it, subtract off Scale V's from the entry in Dest.  If it
      // goes to zero, remove the entry.
      if (Dest.Scale != Src.Scale) {
        Dest.Scale -= Src.Scale;
        Dest.IsNSW = false;
      } else {
        DestGEP.VarIndices.erase(DestGEP.VarIndices.begin() + I.index());
      }
      Found = true;
      break;
    }

    // If we didn't consume this entry, add it to the end of the Dest list.
    if (!Found) {
      VariableGEPIndex Entry = {Src.Val, Src.Scale, Src.CxtI, Src.IsNSW,
                                /* IsNegated */ true};
      DestGEP.VarIndices.push_back(Entry);
    }
  }
}

bool BasicAAResult::constantOffsetHeuristic(const DecomposedGEP &GEP,
                                            LocationSize MaybeV1Size,
                                            LocationSize MaybeV2Size,
                                            AssumptionCache *AC,
                                            DominatorTree *DT,
                                            const AAQueryInfo &AAQI) {
  if (GEP.VarIndices.size() != 2 || !MaybeV1Size.hasValue() ||
      !MaybeV2Size.hasValue())
    return false;

  const uint64_t V1Size = MaybeV1Size.getValue();
  const uint64_t V2Size = MaybeV2Size.getValue();

  const VariableGEPIndex &Var0 = GEP.VarIndices[0], &Var1 = GEP.VarIndices[1];

  if (Var0.Val.TruncBits != 0 || !Var0.Val.hasSameCastsAs(Var1.Val) ||
      !Var0.hasNegatedScaleOf(Var1) ||
      Var0.Val.V->getType() != Var1.Val.V->getType())
    return false;

  // We'll strip off the Extensions of Var0 and Var1 and do another round
  // of GetLinearExpression decomposition. In the example above, if Var0
  // is zext(%x + 1) we should get V1 == %x and V1Offset == 1.

  LinearExpression E0 =
      GetLinearExpression(CastedValue(Var0.Val.V), DL, 0, AC, DT);
  LinearExpression E1 =
      GetLinearExpression(CastedValue(Var1.Val.V), DL, 0, AC, DT);
  if (E0.Scale != E1.Scale || !E0.Val.hasSameCastsAs(E1.Val) ||
      !isValueEqualInPotentialCycles(E0.Val.V, E1.Val.V, AAQI))
    return false;

  // We have a hit - Var0 and Var1 only differ by a constant offset!

  // If we've been sext'ed then zext'd the maximum difference between Var0 and
  // Var1 is possible to calculate, but we're just interested in the absolute
  // minimum difference between the two. The minimum distance may occur due to
  // wrapping; consider "add i3 %i, 5": if %i == 7 then 7 + 5 mod 8 == 4, and so
  // the minimum distance between %i and %i + 5 is 3.
  APInt MinDiff = E0.Offset - E1.Offset, Wrapped = -MinDiff;
  MinDiff = APIntOps::umin(MinDiff, Wrapped);
  APInt MinDiffBytes =
    MinDiff.zextOrTrunc(Var0.Scale.getBitWidth()) * Var0.Scale.abs();

  // We can't definitely say whether GEP1 is before or after V2 due to wrapping
  // arithmetic (i.e. for some values of GEP1 and V2 GEP1 < V2, and for other
  // values GEP1 > V2). We'll therefore only declare NoAlias if both V1Size and
  // V2Size can fit in the MinDiffBytes gap.
  return MinDiffBytes.uge(V1Size + GEP.Offset.abs()) &&
         MinDiffBytes.uge(V2Size + GEP.Offset.abs());
}

//===----------------------------------------------------------------------===//
// BasicAliasAnalysis Pass
//===----------------------------------------------------------------------===//

AnalysisKey BasicAA::Key;

BasicAAResult BasicAA::run(Function &F, FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &AC = AM.getResult<AssumptionAnalysis>(F);
  auto *DT = &AM.getResult<DominatorTreeAnalysis>(F);
  return BasicAAResult(F.getDataLayout(), F, TLI, AC, DT);
}

BasicAAWrapperPass::BasicAAWrapperPass() : FunctionPass(ID) {
  initializeBasicAAWrapperPassPass(*PassRegistry::getPassRegistry());
}

char BasicAAWrapperPass::ID = 0;

void BasicAAWrapperPass::anchor() {}

INITIALIZE_PASS_BEGIN(BasicAAWrapperPass, "basic-aa",
                      "Basic Alias Analysis (stateless AA impl)", true, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(BasicAAWrapperPass, "basic-aa",
                    "Basic Alias Analysis (stateless AA impl)", true, true)

FunctionPass *llvm::createBasicAAWrapperPass() {
  return new BasicAAWrapperPass();
}

bool BasicAAWrapperPass::runOnFunction(Function &F) {
  auto &ACT = getAnalysis<AssumptionCacheTracker>();
  auto &TLIWP = getAnalysis<TargetLibraryInfoWrapperPass>();
  auto &DTWP = getAnalysis<DominatorTreeWrapperPass>();

  Result.reset(new BasicAAResult(F.getDataLayout(), F,
                                 TLIWP.getTLI(F), ACT.getAssumptionCache(F),
                                 &DTWP.getDomTree()));

  return false;
}

void BasicAAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<AssumptionCacheTracker>();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<TargetLibraryInfoWrapperPass>();
}
