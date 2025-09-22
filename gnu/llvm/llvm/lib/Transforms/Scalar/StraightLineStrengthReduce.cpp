//===- StraightLineStrengthReduce.cpp - -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements straight-line strength reduction (SLSR). Unlike loop
// strength reduction, this algorithm is designed to reduce arithmetic
// redundancy in straight-line code instead of loops. It has proven to be
// effective in simplifying arithmetic statements derived from an unrolled loop.
// It can also simplify the logic of SeparateConstOffsetFromGEP.
//
// There are many optimizations we can perform in the domain of SLSR. This file
// for now contains only an initial step. Specifically, we look for strength
// reduction candidates in the following forms:
//
// Form 1: B + i * S
// Form 2: (B + i) * S
// Form 3: &B[i * S]
//
// where S is an integer variable, and i is a constant integer. If we found two
// candidates S1 and S2 in the same form and S1 dominates S2, we may rewrite S2
// in a simpler way with respect to S1. For example,
//
// S1: X = B + i * S
// S2: Y = B + i' * S   => X + (i' - i) * S
//
// S1: X = (B + i) * S
// S2: Y = (B + i') * S => X + (i' - i) * S
//
// S1: X = &B[i * S]
// S2: Y = &B[i' * S]   => &X[(i' - i) * S]
//
// Note: (i' - i) * S is folded to the extent possible.
//
// This rewriting is in general a good idea. The code patterns we focus on
// usually come from loop unrolling, so (i' - i) * S is likely the same
// across iterations and can be reused. When that happens, the optimized form
// takes only one add starting from the second iteration.
//
// When such rewriting is possible, we call S1 a "basis" of S2. When S2 has
// multiple bases, we choose to rewrite S2 with respect to its "immediate"
// basis, the basis that is the closest ancestor in the dominator tree.
//
// TODO:
//
// - Floating point arithmetics when fast math is enabled.
//
// - SLSR may decrease ILP at the architecture level. Targets that are very
//   sensitive to ILP may want to disable it. Having SLSR to consider ILP is
//   left as future work.
//
// - When (i' - i) is constant but i and i' are not, we could still perform
//   SLSR.

#include "llvm/Transforms/Scalar/StraightLineStrengthReduce.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <list>
#include <vector>

using namespace llvm;
using namespace PatternMatch;

static const unsigned UnknownAddressSpace =
    std::numeric_limits<unsigned>::max();

namespace {

class StraightLineStrengthReduceLegacyPass : public FunctionPass {
  const DataLayout *DL = nullptr;

public:
  static char ID;

  StraightLineStrengthReduceLegacyPass() : FunctionPass(ID) {
    initializeStraightLineStrengthReduceLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<ScalarEvolutionWrapperPass>();
    AU.addRequired<TargetTransformInfoWrapperPass>();
    // We do not modify the shape of the CFG.
    AU.setPreservesCFG();
  }

  bool doInitialization(Module &M) override {
    DL = &M.getDataLayout();
    return false;
  }

  bool runOnFunction(Function &F) override;
};

class StraightLineStrengthReduce {
public:
  StraightLineStrengthReduce(const DataLayout *DL, DominatorTree *DT,
                             ScalarEvolution *SE, TargetTransformInfo *TTI)
      : DL(DL), DT(DT), SE(SE), TTI(TTI) {}

  // SLSR candidate. Such a candidate must be in one of the forms described in
  // the header comments.
  struct Candidate {
    enum Kind {
      Invalid, // reserved for the default constructor
      Add,     // B + i * S
      Mul,     // (B + i) * S
      GEP,     // &B[..][i * S][..]
    };

    Candidate() = default;
    Candidate(Kind CT, const SCEV *B, ConstantInt *Idx, Value *S,
              Instruction *I)
        : CandidateKind(CT), Base(B), Index(Idx), Stride(S), Ins(I) {}

    Kind CandidateKind = Invalid;

    const SCEV *Base = nullptr;

    // Note that Index and Stride of a GEP candidate do not necessarily have the
    // same integer type. In that case, during rewriting, Stride will be
    // sign-extended or truncated to Index's type.
    ConstantInt *Index = nullptr;

    Value *Stride = nullptr;

    // The instruction this candidate corresponds to. It helps us to rewrite a
    // candidate with respect to its immediate basis. Note that one instruction
    // can correspond to multiple candidates depending on how you associate the
    // expression. For instance,
    //
    // (a + 1) * (b + 2)
    //
    // can be treated as
    //
    // <Base: a, Index: 1, Stride: b + 2>
    //
    // or
    //
    // <Base: b, Index: 2, Stride: a + 1>
    Instruction *Ins = nullptr;

    // Points to the immediate basis of this candidate, or nullptr if we cannot
    // find any basis for this candidate.
    Candidate *Basis = nullptr;
  };

  bool runOnFunction(Function &F);

private:
  // Returns true if Basis is a basis for C, i.e., Basis dominates C and they
  // share the same base and stride.
  bool isBasisFor(const Candidate &Basis, const Candidate &C);

  // Returns whether the candidate can be folded into an addressing mode.
  bool isFoldable(const Candidate &C, TargetTransformInfo *TTI,
                  const DataLayout *DL);

  // Returns true if C is already in a simplest form and not worth being
  // rewritten.
  bool isSimplestForm(const Candidate &C);

  // Checks whether I is in a candidate form. If so, adds all the matching forms
  // to Candidates, and tries to find the immediate basis for each of them.
  void allocateCandidatesAndFindBasis(Instruction *I);

  // Allocate candidates and find bases for Add instructions.
  void allocateCandidatesAndFindBasisForAdd(Instruction *I);

  // Given I = LHS + RHS, factors RHS into i * S and makes (LHS + i * S) a
  // candidate.
  void allocateCandidatesAndFindBasisForAdd(Value *LHS, Value *RHS,
                                            Instruction *I);
  // Allocate candidates and find bases for Mul instructions.
  void allocateCandidatesAndFindBasisForMul(Instruction *I);

  // Splits LHS into Base + Index and, if succeeds, calls
  // allocateCandidatesAndFindBasis.
  void allocateCandidatesAndFindBasisForMul(Value *LHS, Value *RHS,
                                            Instruction *I);

  // Allocate candidates and find bases for GetElementPtr instructions.
  void allocateCandidatesAndFindBasisForGEP(GetElementPtrInst *GEP);

  // A helper function that scales Idx with ElementSize before invoking
  // allocateCandidatesAndFindBasis.
  void allocateCandidatesAndFindBasisForGEP(const SCEV *B, ConstantInt *Idx,
                                            Value *S, uint64_t ElementSize,
                                            Instruction *I);

  // Adds the given form <CT, B, Idx, S> to Candidates, and finds its immediate
  // basis.
  void allocateCandidatesAndFindBasis(Candidate::Kind CT, const SCEV *B,
                                      ConstantInt *Idx, Value *S,
                                      Instruction *I);

  // Rewrites candidate C with respect to Basis.
  void rewriteCandidateWithBasis(const Candidate &C, const Candidate &Basis);

  // A helper function that factors ArrayIdx to a product of a stride and a
  // constant index, and invokes allocateCandidatesAndFindBasis with the
  // factorings.
  void factorArrayIndex(Value *ArrayIdx, const SCEV *Base, uint64_t ElementSize,
                        GetElementPtrInst *GEP);

  // Emit code that computes the "bump" from Basis to C.
  static Value *emitBump(const Candidate &Basis, const Candidate &C,
                         IRBuilder<> &Builder, const DataLayout *DL);

  const DataLayout *DL = nullptr;
  DominatorTree *DT = nullptr;
  ScalarEvolution *SE;
  TargetTransformInfo *TTI = nullptr;
  std::list<Candidate> Candidates;

  // Temporarily holds all instructions that are unlinked (but not deleted) by
  // rewriteCandidateWithBasis. These instructions will be actually removed
  // after all rewriting finishes.
  std::vector<Instruction *> UnlinkedInstructions;
};

} // end anonymous namespace

char StraightLineStrengthReduceLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(StraightLineStrengthReduceLegacyPass, "slsr",
                      "Straight line strength reduction", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(StraightLineStrengthReduceLegacyPass, "slsr",
                    "Straight line strength reduction", false, false)

FunctionPass *llvm::createStraightLineStrengthReducePass() {
  return new StraightLineStrengthReduceLegacyPass();
}

bool StraightLineStrengthReduce::isBasisFor(const Candidate &Basis,
                                            const Candidate &C) {
  return (Basis.Ins != C.Ins && // skip the same instruction
          // They must have the same type too. Basis.Base == C.Base doesn't
          // guarantee their types are the same (PR23975).
          Basis.Ins->getType() == C.Ins->getType() &&
          // Basis must dominate C in order to rewrite C with respect to Basis.
          DT->dominates(Basis.Ins->getParent(), C.Ins->getParent()) &&
          // They share the same base, stride, and candidate kind.
          Basis.Base == C.Base && Basis.Stride == C.Stride &&
          Basis.CandidateKind == C.CandidateKind);
}

static bool isGEPFoldable(GetElementPtrInst *GEP,
                          const TargetTransformInfo *TTI) {
  SmallVector<const Value *, 4> Indices(GEP->indices());
  return TTI->getGEPCost(GEP->getSourceElementType(), GEP->getPointerOperand(),
                         Indices) == TargetTransformInfo::TCC_Free;
}

// Returns whether (Base + Index * Stride) can be folded to an addressing mode.
static bool isAddFoldable(const SCEV *Base, ConstantInt *Index, Value *Stride,
                          TargetTransformInfo *TTI) {
  // Index->getSExtValue() may crash if Index is wider than 64-bit.
  return Index->getBitWidth() <= 64 &&
         TTI->isLegalAddressingMode(Base->getType(), nullptr, 0, true,
                                    Index->getSExtValue(), UnknownAddressSpace);
}

bool StraightLineStrengthReduce::isFoldable(const Candidate &C,
                                            TargetTransformInfo *TTI,
                                            const DataLayout *DL) {
  if (C.CandidateKind == Candidate::Add)
    return isAddFoldable(C.Base, C.Index, C.Stride, TTI);
  if (C.CandidateKind == Candidate::GEP)
    return isGEPFoldable(cast<GetElementPtrInst>(C.Ins), TTI);
  return false;
}

// Returns true if GEP has zero or one non-zero index.
static bool hasOnlyOneNonZeroIndex(GetElementPtrInst *GEP) {
  unsigned NumNonZeroIndices = 0;
  for (Use &Idx : GEP->indices()) {
    ConstantInt *ConstIdx = dyn_cast<ConstantInt>(Idx);
    if (ConstIdx == nullptr || !ConstIdx->isZero())
      ++NumNonZeroIndices;
  }
  return NumNonZeroIndices <= 1;
}

bool StraightLineStrengthReduce::isSimplestForm(const Candidate &C) {
  if (C.CandidateKind == Candidate::Add) {
    // B + 1 * S or B + (-1) * S
    return C.Index->isOne() || C.Index->isMinusOne();
  }
  if (C.CandidateKind == Candidate::Mul) {
    // (B + 0) * S
    return C.Index->isZero();
  }
  if (C.CandidateKind == Candidate::GEP) {
    // (char*)B + S or (char*)B - S
    return ((C.Index->isOne() || C.Index->isMinusOne()) &&
            hasOnlyOneNonZeroIndex(cast<GetElementPtrInst>(C.Ins)));
  }
  return false;
}

// TODO: We currently implement an algorithm whose time complexity is linear in
// the number of existing candidates. However, we could do better by using
// ScopedHashTable. Specifically, while traversing the dominator tree, we could
// maintain all the candidates that dominate the basic block being traversed in
// a ScopedHashTable. This hash table is indexed by the base and the stride of
// a candidate. Therefore, finding the immediate basis of a candidate boils down
// to one hash-table look up.
void StraightLineStrengthReduce::allocateCandidatesAndFindBasis(
    Candidate::Kind CT, const SCEV *B, ConstantInt *Idx, Value *S,
    Instruction *I) {
  Candidate C(CT, B, Idx, S, I);
  // SLSR can complicate an instruction in two cases:
  //
  // 1. If we can fold I into an addressing mode, computing I is likely free or
  // takes only one instruction.
  //
  // 2. I is already in a simplest form. For example, when
  //      X = B + 8 * S
  //      Y = B + S,
  //    rewriting Y to X - 7 * S is probably a bad idea.
  //
  // In the above cases, we still add I to the candidate list so that I can be
  // the basis of other candidates, but we leave I's basis blank so that I
  // won't be rewritten.
  if (!isFoldable(C, TTI, DL) && !isSimplestForm(C)) {
    // Try to compute the immediate basis of C.
    unsigned NumIterations = 0;
    // Limit the scan radius to avoid running in quadratice time.
    static const unsigned MaxNumIterations = 50;
    for (auto Basis = Candidates.rbegin();
         Basis != Candidates.rend() && NumIterations < MaxNumIterations;
         ++Basis, ++NumIterations) {
      if (isBasisFor(*Basis, C)) {
        C.Basis = &(*Basis);
        break;
      }
    }
  }
  // Regardless of whether we find a basis for C, we need to push C to the
  // candidate list so that it can be the basis of other candidates.
  Candidates.push_back(C);
}

void StraightLineStrengthReduce::allocateCandidatesAndFindBasis(
    Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::Add:
    allocateCandidatesAndFindBasisForAdd(I);
    break;
  case Instruction::Mul:
    allocateCandidatesAndFindBasisForMul(I);
    break;
  case Instruction::GetElementPtr:
    allocateCandidatesAndFindBasisForGEP(cast<GetElementPtrInst>(I));
    break;
  }
}

void StraightLineStrengthReduce::allocateCandidatesAndFindBasisForAdd(
    Instruction *I) {
  // Try matching B + i * S.
  if (!isa<IntegerType>(I->getType()))
    return;

  assert(I->getNumOperands() == 2 && "isn't I an add?");
  Value *LHS = I->getOperand(0), *RHS = I->getOperand(1);
  allocateCandidatesAndFindBasisForAdd(LHS, RHS, I);
  if (LHS != RHS)
    allocateCandidatesAndFindBasisForAdd(RHS, LHS, I);
}

void StraightLineStrengthReduce::allocateCandidatesAndFindBasisForAdd(
    Value *LHS, Value *RHS, Instruction *I) {
  Value *S = nullptr;
  ConstantInt *Idx = nullptr;
  if (match(RHS, m_Mul(m_Value(S), m_ConstantInt(Idx)))) {
    // I = LHS + RHS = LHS + Idx * S
    allocateCandidatesAndFindBasis(Candidate::Add, SE->getSCEV(LHS), Idx, S, I);
  } else if (match(RHS, m_Shl(m_Value(S), m_ConstantInt(Idx)))) {
    // I = LHS + RHS = LHS + (S << Idx) = LHS + S * (1 << Idx)
    APInt One(Idx->getBitWidth(), 1);
    Idx = ConstantInt::get(Idx->getContext(), One << Idx->getValue());
    allocateCandidatesAndFindBasis(Candidate::Add, SE->getSCEV(LHS), Idx, S, I);
  } else {
    // At least, I = LHS + 1 * RHS
    ConstantInt *One = ConstantInt::get(cast<IntegerType>(I->getType()), 1);
    allocateCandidatesAndFindBasis(Candidate::Add, SE->getSCEV(LHS), One, RHS,
                                   I);
  }
}

// Returns true if A matches B + C where C is constant.
static bool matchesAdd(Value *A, Value *&B, ConstantInt *&C) {
  return match(A, m_c_Add(m_Value(B), m_ConstantInt(C)));
}

// Returns true if A matches B | C where C is constant.
static bool matchesOr(Value *A, Value *&B, ConstantInt *&C) {
  return match(A, m_c_Or(m_Value(B), m_ConstantInt(C)));
}

void StraightLineStrengthReduce::allocateCandidatesAndFindBasisForMul(
    Value *LHS, Value *RHS, Instruction *I) {
  Value *B = nullptr;
  ConstantInt *Idx = nullptr;
  if (matchesAdd(LHS, B, Idx)) {
    // If LHS is in the form of "Base + Index", then I is in the form of
    // "(Base + Index) * RHS".
    allocateCandidatesAndFindBasis(Candidate::Mul, SE->getSCEV(B), Idx, RHS, I);
  } else if (matchesOr(LHS, B, Idx) && haveNoCommonBitsSet(B, Idx, *DL)) {
    // If LHS is in the form of "Base | Index" and Base and Index have no common
    // bits set, then
    //   Base | Index = Base + Index
    // and I is thus in the form of "(Base + Index) * RHS".
    allocateCandidatesAndFindBasis(Candidate::Mul, SE->getSCEV(B), Idx, RHS, I);
  } else {
    // Otherwise, at least try the form (LHS + 0) * RHS.
    ConstantInt *Zero = ConstantInt::get(cast<IntegerType>(I->getType()), 0);
    allocateCandidatesAndFindBasis(Candidate::Mul, SE->getSCEV(LHS), Zero, RHS,
                                   I);
  }
}

void StraightLineStrengthReduce::allocateCandidatesAndFindBasisForMul(
    Instruction *I) {
  // Try matching (B + i) * S.
  // TODO: we could extend SLSR to float and vector types.
  if (!isa<IntegerType>(I->getType()))
    return;

  assert(I->getNumOperands() == 2 && "isn't I a mul?");
  Value *LHS = I->getOperand(0), *RHS = I->getOperand(1);
  allocateCandidatesAndFindBasisForMul(LHS, RHS, I);
  if (LHS != RHS) {
    // Symmetrically, try to split RHS to Base + Index.
    allocateCandidatesAndFindBasisForMul(RHS, LHS, I);
  }
}

void StraightLineStrengthReduce::allocateCandidatesAndFindBasisForGEP(
    const SCEV *B, ConstantInt *Idx, Value *S, uint64_t ElementSize,
    Instruction *I) {
  // I = B + sext(Idx *nsw S) * ElementSize
  //   = B + (sext(Idx) * sext(S)) * ElementSize
  //   = B + (sext(Idx) * ElementSize) * sext(S)
  // Casting to IntegerType is safe because we skipped vector GEPs.
  IntegerType *PtrIdxTy = cast<IntegerType>(DL->getIndexType(I->getType()));
  ConstantInt *ScaledIdx = ConstantInt::get(
      PtrIdxTy, Idx->getSExtValue() * (int64_t)ElementSize, true);
  allocateCandidatesAndFindBasis(Candidate::GEP, B, ScaledIdx, S, I);
}

void StraightLineStrengthReduce::factorArrayIndex(Value *ArrayIdx,
                                                  const SCEV *Base,
                                                  uint64_t ElementSize,
                                                  GetElementPtrInst *GEP) {
  // At least, ArrayIdx = ArrayIdx *nsw 1.
  allocateCandidatesAndFindBasisForGEP(
      Base, ConstantInt::get(cast<IntegerType>(ArrayIdx->getType()), 1),
      ArrayIdx, ElementSize, GEP);
  Value *LHS = nullptr;
  ConstantInt *RHS = nullptr;
  // One alternative is matching the SCEV of ArrayIdx instead of ArrayIdx
  // itself. This would allow us to handle the shl case for free. However,
  // matching SCEVs has two issues:
  //
  // 1. this would complicate rewriting because the rewriting procedure
  // would have to translate SCEVs back to IR instructions. This translation
  // is difficult when LHS is further evaluated to a composite SCEV.
  //
  // 2. ScalarEvolution is designed to be control-flow oblivious. It tends
  // to strip nsw/nuw flags which are critical for SLSR to trace into
  // sext'ed multiplication.
  if (match(ArrayIdx, m_NSWMul(m_Value(LHS), m_ConstantInt(RHS)))) {
    // SLSR is currently unsafe if i * S may overflow.
    // GEP = Base + sext(LHS *nsw RHS) * ElementSize
    allocateCandidatesAndFindBasisForGEP(Base, RHS, LHS, ElementSize, GEP);
  } else if (match(ArrayIdx, m_NSWShl(m_Value(LHS), m_ConstantInt(RHS)))) {
    // GEP = Base + sext(LHS <<nsw RHS) * ElementSize
    //     = Base + sext(LHS *nsw (1 << RHS)) * ElementSize
    APInt One(RHS->getBitWidth(), 1);
    ConstantInt *PowerOf2 =
        ConstantInt::get(RHS->getContext(), One << RHS->getValue());
    allocateCandidatesAndFindBasisForGEP(Base, PowerOf2, LHS, ElementSize, GEP);
  }
}

void StraightLineStrengthReduce::allocateCandidatesAndFindBasisForGEP(
    GetElementPtrInst *GEP) {
  // TODO: handle vector GEPs
  if (GEP->getType()->isVectorTy())
    return;

  SmallVector<const SCEV *, 4> IndexExprs;
  for (Use &Idx : GEP->indices())
    IndexExprs.push_back(SE->getSCEV(Idx));

  gep_type_iterator GTI = gep_type_begin(GEP);
  for (unsigned I = 1, E = GEP->getNumOperands(); I != E; ++I, ++GTI) {
    if (GTI.isStruct())
      continue;

    const SCEV *OrigIndexExpr = IndexExprs[I - 1];
    IndexExprs[I - 1] = SE->getZero(OrigIndexExpr->getType());

    // The base of this candidate is GEP's base plus the offsets of all
    // indices except this current one.
    const SCEV *BaseExpr = SE->getGEPExpr(cast<GEPOperator>(GEP), IndexExprs);
    Value *ArrayIdx = GEP->getOperand(I);
    uint64_t ElementSize = GTI.getSequentialElementStride(*DL);
    if (ArrayIdx->getType()->getIntegerBitWidth() <=
        DL->getIndexSizeInBits(GEP->getAddressSpace())) {
      // Skip factoring if ArrayIdx is wider than the index size, because
      // ArrayIdx is implicitly truncated to the index size.
      factorArrayIndex(ArrayIdx, BaseExpr, ElementSize, GEP);
    }
    // When ArrayIdx is the sext of a value, we try to factor that value as
    // well.  Handling this case is important because array indices are
    // typically sign-extended to the pointer index size.
    Value *TruncatedArrayIdx = nullptr;
    if (match(ArrayIdx, m_SExt(m_Value(TruncatedArrayIdx))) &&
        TruncatedArrayIdx->getType()->getIntegerBitWidth() <=
            DL->getIndexSizeInBits(GEP->getAddressSpace())) {
      // Skip factoring if TruncatedArrayIdx is wider than the pointer size,
      // because TruncatedArrayIdx is implicitly truncated to the pointer size.
      factorArrayIndex(TruncatedArrayIdx, BaseExpr, ElementSize, GEP);
    }

    IndexExprs[I - 1] = OrigIndexExpr;
  }
}

// A helper function that unifies the bitwidth of A and B.
static void unifyBitWidth(APInt &A, APInt &B) {
  if (A.getBitWidth() < B.getBitWidth())
    A = A.sext(B.getBitWidth());
  else if (A.getBitWidth() > B.getBitWidth())
    B = B.sext(A.getBitWidth());
}

Value *StraightLineStrengthReduce::emitBump(const Candidate &Basis,
                                            const Candidate &C,
                                            IRBuilder<> &Builder,
                                            const DataLayout *DL) {
  APInt Idx = C.Index->getValue(), BasisIdx = Basis.Index->getValue();
  unifyBitWidth(Idx, BasisIdx);
  APInt IndexOffset = Idx - BasisIdx;

  // Compute Bump = C - Basis = (i' - i) * S.
  // Common case 1: if (i' - i) is 1, Bump = S.
  if (IndexOffset == 1)
    return C.Stride;
  // Common case 2: if (i' - i) is -1, Bump = -S.
  if (IndexOffset.isAllOnes())
    return Builder.CreateNeg(C.Stride);

  // Otherwise, Bump = (i' - i) * sext/trunc(S). Note that (i' - i) and S may
  // have different bit widths.
  IntegerType *DeltaType =
      IntegerType::get(Basis.Ins->getContext(), IndexOffset.getBitWidth());
  Value *ExtendedStride = Builder.CreateSExtOrTrunc(C.Stride, DeltaType);
  if (IndexOffset.isPowerOf2()) {
    // If (i' - i) is a power of 2, Bump = sext/trunc(S) << log(i' - i).
    ConstantInt *Exponent = ConstantInt::get(DeltaType, IndexOffset.logBase2());
    return Builder.CreateShl(ExtendedStride, Exponent);
  }
  if (IndexOffset.isNegatedPowerOf2()) {
    // If (i - i') is a power of 2, Bump = -sext/trunc(S) << log(i' - i).
    ConstantInt *Exponent =
        ConstantInt::get(DeltaType, (-IndexOffset).logBase2());
    return Builder.CreateNeg(Builder.CreateShl(ExtendedStride, Exponent));
  }
  Constant *Delta = ConstantInt::get(DeltaType, IndexOffset);
  return Builder.CreateMul(ExtendedStride, Delta);
}

void StraightLineStrengthReduce::rewriteCandidateWithBasis(
    const Candidate &C, const Candidate &Basis) {
  assert(C.CandidateKind == Basis.CandidateKind && C.Base == Basis.Base &&
         C.Stride == Basis.Stride);
  // We run rewriteCandidateWithBasis on all candidates in a post-order, so the
  // basis of a candidate cannot be unlinked before the candidate.
  assert(Basis.Ins->getParent() != nullptr && "the basis is unlinked");

  // An instruction can correspond to multiple candidates. Therefore, instead of
  // simply deleting an instruction when we rewrite it, we mark its parent as
  // nullptr (i.e. unlink it) so that we can skip the candidates whose
  // instruction is already rewritten.
  if (!C.Ins->getParent())
    return;

  IRBuilder<> Builder(C.Ins);
  Value *Bump = emitBump(Basis, C, Builder, DL);
  Value *Reduced = nullptr; // equivalent to but weaker than C.Ins
  switch (C.CandidateKind) {
  case Candidate::Add:
  case Candidate::Mul: {
    // C = Basis + Bump
    Value *NegBump;
    if (match(Bump, m_Neg(m_Value(NegBump)))) {
      // If Bump is a neg instruction, emit C = Basis - (-Bump).
      Reduced = Builder.CreateSub(Basis.Ins, NegBump);
      // We only use the negative argument of Bump, and Bump itself may be
      // trivially dead.
      RecursivelyDeleteTriviallyDeadInstructions(Bump);
    } else {
      // It's tempting to preserve nsw on Bump and/or Reduced. However, it's
      // usually unsound, e.g.,
      //
      // X = (-2 +nsw 1) *nsw INT_MAX
      // Y = (-2 +nsw 3) *nsw INT_MAX
      //   =>
      // Y = X + 2 * INT_MAX
      //
      // Neither + and * in the resultant expression are nsw.
      Reduced = Builder.CreateAdd(Basis.Ins, Bump);
    }
    break;
  }
  case Candidate::GEP: {
    bool InBounds = cast<GetElementPtrInst>(C.Ins)->isInBounds();
    // C = (char *)Basis + Bump
    Reduced = Builder.CreatePtrAdd(Basis.Ins, Bump, "", InBounds);
    break;
  }
  default:
    llvm_unreachable("C.CandidateKind is invalid");
  };
  Reduced->takeName(C.Ins);
  C.Ins->replaceAllUsesWith(Reduced);
  // Unlink C.Ins so that we can skip other candidates also corresponding to
  // C.Ins. The actual deletion is postponed to the end of runOnFunction.
  C.Ins->removeFromParent();
  UnlinkedInstructions.push_back(C.Ins);
}

bool StraightLineStrengthReduceLegacyPass::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;

  auto *TTI = &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  auto *DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto *SE = &getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  return StraightLineStrengthReduce(DL, DT, SE, TTI).runOnFunction(F);
}

bool StraightLineStrengthReduce::runOnFunction(Function &F) {
  // Traverse the dominator tree in the depth-first order. This order makes sure
  // all bases of a candidate are in Candidates when we process it.
  for (const auto Node : depth_first(DT))
    for (auto &I : *(Node->getBlock()))
      allocateCandidatesAndFindBasis(&I);

  // Rewrite candidates in the reverse depth-first order. This order makes sure
  // a candidate being rewritten is not a basis for any other candidate.
  while (!Candidates.empty()) {
    const Candidate &C = Candidates.back();
    if (C.Basis != nullptr) {
      rewriteCandidateWithBasis(C, *C.Basis);
    }
    Candidates.pop_back();
  }

  // Delete all unlink instructions.
  for (auto *UnlinkedInst : UnlinkedInstructions) {
    for (unsigned I = 0, E = UnlinkedInst->getNumOperands(); I != E; ++I) {
      Value *Op = UnlinkedInst->getOperand(I);
      UnlinkedInst->setOperand(I, nullptr);
      RecursivelyDeleteTriviallyDeadInstructions(Op);
    }
    UnlinkedInst->deleteValue();
  }
  bool Ret = !UnlinkedInstructions.empty();
  UnlinkedInstructions.clear();
  return Ret;
}

namespace llvm {

PreservedAnalyses
StraightLineStrengthReducePass::run(Function &F, FunctionAnalysisManager &AM) {
  const DataLayout *DL = &F.getDataLayout();
  auto *DT = &AM.getResult<DominatorTreeAnalysis>(F);
  auto *SE = &AM.getResult<ScalarEvolutionAnalysis>(F);
  auto *TTI = &AM.getResult<TargetIRAnalysis>(F);

  if (!StraightLineStrengthReduce(DL, DT, SE, TTI).runOnFunction(F))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<DominatorTreeAnalysis>();
  PA.preserve<ScalarEvolutionAnalysis>();
  PA.preserve<TargetIRAnalysis>();
  return PA;
}

} // namespace llvm
