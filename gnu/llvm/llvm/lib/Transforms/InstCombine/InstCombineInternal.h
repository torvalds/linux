//===- InstCombineInternal.h - InstCombine pass internals -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
///
/// This file provides internal interfaces used to implement the InstCombine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_INSTCOMBINE_INSTCOMBINEINTERNAL_H
#define LLVM_LIB_TRANSFORMS_INSTCOMBINE_INSTCOMBINEINTERNAL_H

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Utils/Local.h"
#include <cassert>

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

using namespace llvm::PatternMatch;

// As a default, let's assume that we want to be aggressive,
// and attempt to traverse with no limits in attempt to sink negation.
static constexpr unsigned NegatorDefaultMaxDepth = ~0U;

// Let's guesstimate that most often we will end up visiting/producing
// fairly small number of new instructions.
static constexpr unsigned NegatorMaxNodesSSO = 16;

namespace llvm {

class AAResults;
class APInt;
class AssumptionCache;
class BlockFrequencyInfo;
class DataLayout;
class DominatorTree;
class GEPOperator;
class GlobalVariable;
class LoopInfo;
class OptimizationRemarkEmitter;
class ProfileSummaryInfo;
class TargetLibraryInfo;
class User;

class LLVM_LIBRARY_VISIBILITY InstCombinerImpl final
    : public InstCombiner,
      public InstVisitor<InstCombinerImpl, Instruction *> {
public:
  InstCombinerImpl(InstructionWorklist &Worklist, BuilderTy &Builder,
                   bool MinimizeSize, AAResults *AA, AssumptionCache &AC,
                   TargetLibraryInfo &TLI, TargetTransformInfo &TTI,
                   DominatorTree &DT, OptimizationRemarkEmitter &ORE,
                   BlockFrequencyInfo *BFI, BranchProbabilityInfo *BPI,
                   ProfileSummaryInfo *PSI, const DataLayout &DL, LoopInfo *LI)
      : InstCombiner(Worklist, Builder, MinimizeSize, AA, AC, TLI, TTI, DT, ORE,
                     BFI, BPI, PSI, DL, LI) {}

  virtual ~InstCombinerImpl() = default;

  /// Perform early cleanup and prepare the InstCombine worklist.
  bool prepareWorklist(Function &F,
                       ReversePostOrderTraversal<BasicBlock *> &RPOT);

  /// Run the combiner over the entire worklist until it is empty.
  ///
  /// \returns true if the IR is changed.
  bool run();

  // Visitation implementation - Implement instruction combining for different
  // instruction types.  The semantics are as follows:
  // Return Value:
  //    null        - No change was made
  //     I          - Change was made, I is still valid, I may be dead though
  //   otherwise    - Change was made, replace I with returned instruction
  //
  Instruction *visitFNeg(UnaryOperator &I);
  Instruction *visitAdd(BinaryOperator &I);
  Instruction *visitFAdd(BinaryOperator &I);
  Value *OptimizePointerDifference(
      Value *LHS, Value *RHS, Type *Ty, bool isNUW);
  Instruction *visitSub(BinaryOperator &I);
  Instruction *visitFSub(BinaryOperator &I);
  Instruction *visitMul(BinaryOperator &I);
  Instruction *foldPowiReassoc(BinaryOperator &I);
  Instruction *foldFMulReassoc(BinaryOperator &I);
  Instruction *visitFMul(BinaryOperator &I);
  Instruction *visitURem(BinaryOperator &I);
  Instruction *visitSRem(BinaryOperator &I);
  Instruction *visitFRem(BinaryOperator &I);
  bool simplifyDivRemOfSelectWithZeroOp(BinaryOperator &I);
  Instruction *commonIRemTransforms(BinaryOperator &I);
  Instruction *commonIDivTransforms(BinaryOperator &I);
  Instruction *visitUDiv(BinaryOperator &I);
  Instruction *visitSDiv(BinaryOperator &I);
  Instruction *visitFDiv(BinaryOperator &I);
  Value *simplifyRangeCheck(ICmpInst *Cmp0, ICmpInst *Cmp1, bool Inverted);
  Instruction *visitAnd(BinaryOperator &I);
  Instruction *visitOr(BinaryOperator &I);
  bool sinkNotIntoLogicalOp(Instruction &I);
  bool sinkNotIntoOtherHandOfLogicalOp(Instruction &I);
  Instruction *visitXor(BinaryOperator &I);
  Instruction *visitShl(BinaryOperator &I);
  Value *reassociateShiftAmtsOfTwoSameDirectionShifts(
      BinaryOperator *Sh0, const SimplifyQuery &SQ,
      bool AnalyzeForSignBitExtraction = false);
  Instruction *canonicalizeCondSignextOfHighBitExtractToSignextHighBitExtract(
      BinaryOperator &I);
  Instruction *foldVariableSignZeroExtensionOfVariableHighBitExtract(
      BinaryOperator &OldAShr);
  Instruction *visitAShr(BinaryOperator &I);
  Instruction *visitLShr(BinaryOperator &I);
  Instruction *commonShiftTransforms(BinaryOperator &I);
  Instruction *visitFCmpInst(FCmpInst &I);
  CmpInst *canonicalizeICmpPredicate(CmpInst &I);
  Instruction *visitICmpInst(ICmpInst &I);
  Instruction *FoldShiftByConstant(Value *Op0, Constant *Op1,
                                   BinaryOperator &I);
  Instruction *commonCastTransforms(CastInst &CI);
  Instruction *visitTrunc(TruncInst &CI);
  Instruction *visitZExt(ZExtInst &Zext);
  Instruction *visitSExt(SExtInst &Sext);
  Instruction *visitFPTrunc(FPTruncInst &CI);
  Instruction *visitFPExt(CastInst &CI);
  Instruction *visitFPToUI(FPToUIInst &FI);
  Instruction *visitFPToSI(FPToSIInst &FI);
  Instruction *visitUIToFP(CastInst &CI);
  Instruction *visitSIToFP(CastInst &CI);
  Instruction *visitPtrToInt(PtrToIntInst &CI);
  Instruction *visitIntToPtr(IntToPtrInst &CI);
  Instruction *visitBitCast(BitCastInst &CI);
  Instruction *visitAddrSpaceCast(AddrSpaceCastInst &CI);
  Instruction *foldItoFPtoI(CastInst &FI);
  Instruction *visitSelectInst(SelectInst &SI);
  Instruction *visitCallInst(CallInst &CI);
  Instruction *visitInvokeInst(InvokeInst &II);
  Instruction *visitCallBrInst(CallBrInst &CBI);

  Instruction *SliceUpIllegalIntegerPHI(PHINode &PN);
  Instruction *visitPHINode(PHINode &PN);
  Instruction *visitGetElementPtrInst(GetElementPtrInst &GEP);
  Instruction *visitGEPOfGEP(GetElementPtrInst &GEP, GEPOperator *Src);
  Instruction *visitAllocaInst(AllocaInst &AI);
  Instruction *visitAllocSite(Instruction &FI);
  Instruction *visitFree(CallInst &FI, Value *FreedOp);
  Instruction *visitLoadInst(LoadInst &LI);
  Instruction *visitStoreInst(StoreInst &SI);
  Instruction *visitAtomicRMWInst(AtomicRMWInst &SI);
  Instruction *visitUnconditionalBranchInst(BranchInst &BI);
  Instruction *visitBranchInst(BranchInst &BI);
  Instruction *visitFenceInst(FenceInst &FI);
  Instruction *visitSwitchInst(SwitchInst &SI);
  Instruction *visitReturnInst(ReturnInst &RI);
  Instruction *visitUnreachableInst(UnreachableInst &I);
  Instruction *
  foldAggregateConstructionIntoAggregateReuse(InsertValueInst &OrigIVI);
  Instruction *visitInsertValueInst(InsertValueInst &IV);
  Instruction *visitInsertElementInst(InsertElementInst &IE);
  Instruction *visitExtractElementInst(ExtractElementInst &EI);
  Instruction *simplifyBinOpSplats(ShuffleVectorInst &SVI);
  Instruction *visitShuffleVectorInst(ShuffleVectorInst &SVI);
  Instruction *visitExtractValueInst(ExtractValueInst &EV);
  Instruction *visitLandingPadInst(LandingPadInst &LI);
  Instruction *visitVAEndInst(VAEndInst &I);
  Value *pushFreezeToPreventPoisonFromPropagating(FreezeInst &FI);
  bool freezeOtherUses(FreezeInst &FI);
  Instruction *foldFreezeIntoRecurrence(FreezeInst &I, PHINode *PN);
  Instruction *visitFreeze(FreezeInst &I);

  /// Specify what to return for unhandled instructions.
  Instruction *visitInstruction(Instruction &I) { return nullptr; }

  /// True when DB dominates all uses of DI except UI.
  /// UI must be in the same block as DI.
  /// The routine checks that the DI parent and DB are different.
  bool dominatesAllUses(const Instruction *DI, const Instruction *UI,
                        const BasicBlock *DB) const;

  /// Try to replace select with select operand SIOpd in SI-ICmp sequence.
  bool replacedSelectWithOperand(SelectInst *SI, const ICmpInst *Icmp,
                                 const unsigned SIOpd);

  LoadInst *combineLoadToNewType(LoadInst &LI, Type *NewTy,
                                 const Twine &Suffix = "");

  KnownFPClass computeKnownFPClass(Value *Val, FastMathFlags FMF,
                                   FPClassTest Interested = fcAllFlags,
                                   const Instruction *CtxI = nullptr,
                                   unsigned Depth = 0) const {
    return llvm::computeKnownFPClass(
        Val, FMF, Interested, Depth,
        getSimplifyQuery().getWithInstruction(CtxI));
  }

  KnownFPClass computeKnownFPClass(Value *Val,
                                   FPClassTest Interested = fcAllFlags,
                                   const Instruction *CtxI = nullptr,
                                   unsigned Depth = 0) const {
    return llvm::computeKnownFPClass(
        Val, Interested, Depth, getSimplifyQuery().getWithInstruction(CtxI));
  }

  /// Check if fmul \p MulVal, +0.0 will yield +0.0 (or signed zero is
  /// ignorable).
  bool fmulByZeroIsZero(Value *MulVal, FastMathFlags FMF,
                        const Instruction *CtxI) const;

  Constant *getLosslessTrunc(Constant *C, Type *TruncTy, unsigned ExtOp) {
    Constant *TruncC = ConstantExpr::getTrunc(C, TruncTy);
    Constant *ExtTruncC =
        ConstantFoldCastOperand(ExtOp, TruncC, C->getType(), DL);
    if (ExtTruncC && ExtTruncC == C)
      return TruncC;
    return nullptr;
  }

  Constant *getLosslessUnsignedTrunc(Constant *C, Type *TruncTy) {
    return getLosslessTrunc(C, TruncTy, Instruction::ZExt);
  }

  Constant *getLosslessSignedTrunc(Constant *C, Type *TruncTy) {
    return getLosslessTrunc(C, TruncTy, Instruction::SExt);
  }

  std::optional<std::pair<Intrinsic::ID, SmallVector<Value *, 3>>>
  convertOrOfShiftsToFunnelShift(Instruction &Or);

private:
  bool annotateAnyAllocSite(CallBase &Call, const TargetLibraryInfo *TLI);
  bool isDesirableIntType(unsigned BitWidth) const;
  bool shouldChangeType(unsigned FromBitWidth, unsigned ToBitWidth) const;
  bool shouldChangeType(Type *From, Type *To) const;
  Value *dyn_castNegVal(Value *V) const;

  /// Classify whether a cast is worth optimizing.
  ///
  /// This is a helper to decide whether the simplification of
  /// logic(cast(A), cast(B)) to cast(logic(A, B)) should be performed.
  ///
  /// \param CI The cast we are interested in.
  ///
  /// \return true if this cast actually results in any code being generated and
  /// if it cannot already be eliminated by some other transformation.
  bool shouldOptimizeCast(CastInst *CI);

  /// Try to optimize a sequence of instructions checking if an operation
  /// on LHS and RHS overflows.
  ///
  /// If this overflow check is done via one of the overflow check intrinsics,
  /// then CtxI has to be the call instruction calling that intrinsic.  If this
  /// overflow check is done by arithmetic followed by a compare, then CtxI has
  /// to be the arithmetic instruction.
  ///
  /// If a simplification is possible, stores the simplified result of the
  /// operation in OperationResult and result of the overflow check in
  /// OverflowResult, and return true.  If no simplification is possible,
  /// returns false.
  bool OptimizeOverflowCheck(Instruction::BinaryOps BinaryOp, bool IsSigned,
                             Value *LHS, Value *RHS,
                             Instruction &CtxI, Value *&OperationResult,
                             Constant *&OverflowResult);

  Instruction *visitCallBase(CallBase &Call);
  Instruction *tryOptimizeCall(CallInst *CI);
  bool transformConstExprCastCall(CallBase &Call);
  Instruction *transformCallThroughTrampoline(CallBase &Call,
                                              IntrinsicInst &Tramp);

  // Return (a, b) if (LHS, RHS) is known to be (a, b) or (b, a).
  // Otherwise, return std::nullopt
  // Currently it matches:
  // - LHS = (select c, a, b), RHS = (select c, b, a)
  // - LHS = (phi [a, BB0], [b, BB1]), RHS = (phi [b, BB0], [a, BB1])
  // - LHS = min(a, b), RHS = max(a, b)
  std::optional<std::pair<Value *, Value *>> matchSymmetricPair(Value *LHS,
                                                                Value *RHS);

  Value *simplifyMaskedLoad(IntrinsicInst &II);
  Instruction *simplifyMaskedStore(IntrinsicInst &II);
  Instruction *simplifyMaskedGather(IntrinsicInst &II);
  Instruction *simplifyMaskedScatter(IntrinsicInst &II);

  /// Transform (zext icmp) to bitwise / integer operations in order to
  /// eliminate it.
  ///
  /// \param ICI The icmp of the (zext icmp) pair we are interested in.
  /// \parem CI The zext of the (zext icmp) pair we are interested in.
  ///
  /// \return null if the transformation cannot be performed. If the
  /// transformation can be performed the new instruction that replaces the
  /// (zext icmp) pair will be returned.
  Instruction *transformZExtICmp(ICmpInst *Cmp, ZExtInst &Zext);

  Instruction *transformSExtICmp(ICmpInst *Cmp, SExtInst &Sext);

  bool willNotOverflowSignedAdd(const WithCache<const Value *> &LHS,
                                const WithCache<const Value *> &RHS,
                                const Instruction &CxtI) const {
    return computeOverflowForSignedAdd(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowUnsignedAdd(const WithCache<const Value *> &LHS,
                                  const WithCache<const Value *> &RHS,
                                  const Instruction &CxtI) const {
    return computeOverflowForUnsignedAdd(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowAdd(const Value *LHS, const Value *RHS,
                          const Instruction &CxtI, bool IsSigned) const {
    return IsSigned ? willNotOverflowSignedAdd(LHS, RHS, CxtI)
                    : willNotOverflowUnsignedAdd(LHS, RHS, CxtI);
  }

  bool willNotOverflowSignedSub(const Value *LHS, const Value *RHS,
                                const Instruction &CxtI) const {
    return computeOverflowForSignedSub(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowUnsignedSub(const Value *LHS, const Value *RHS,
                                  const Instruction &CxtI) const {
    return computeOverflowForUnsignedSub(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowSub(const Value *LHS, const Value *RHS,
                          const Instruction &CxtI, bool IsSigned) const {
    return IsSigned ? willNotOverflowSignedSub(LHS, RHS, CxtI)
                    : willNotOverflowUnsignedSub(LHS, RHS, CxtI);
  }

  bool willNotOverflowSignedMul(const Value *LHS, const Value *RHS,
                                const Instruction &CxtI) const {
    return computeOverflowForSignedMul(LHS, RHS, &CxtI) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowUnsignedMul(const Value *LHS, const Value *RHS,
                                  const Instruction &CxtI,
                                  bool IsNSW = false) const {
    return computeOverflowForUnsignedMul(LHS, RHS, &CxtI, IsNSW) ==
           OverflowResult::NeverOverflows;
  }

  bool willNotOverflowMul(const Value *LHS, const Value *RHS,
                          const Instruction &CxtI, bool IsSigned) const {
    return IsSigned ? willNotOverflowSignedMul(LHS, RHS, CxtI)
                    : willNotOverflowUnsignedMul(LHS, RHS, CxtI);
  }

  bool willNotOverflow(BinaryOperator::BinaryOps Opcode, const Value *LHS,
                       const Value *RHS, const Instruction &CxtI,
                       bool IsSigned) const {
    switch (Opcode) {
    case Instruction::Add: return willNotOverflowAdd(LHS, RHS, CxtI, IsSigned);
    case Instruction::Sub: return willNotOverflowSub(LHS, RHS, CxtI, IsSigned);
    case Instruction::Mul: return willNotOverflowMul(LHS, RHS, CxtI, IsSigned);
    default: llvm_unreachable("Unexpected opcode for overflow query");
    }
  }

  Value *EmitGEPOffset(GEPOperator *GEP, bool RewriteGEP = false);
  Instruction *scalarizePHI(ExtractElementInst &EI, PHINode *PN);
  Instruction *foldBitcastExtElt(ExtractElementInst &ExtElt);
  Instruction *foldCastedBitwiseLogic(BinaryOperator &I);
  Instruction *foldFBinOpOfIntCasts(BinaryOperator &I);
  // Should only be called by `foldFBinOpOfIntCasts`.
  Instruction *foldFBinOpOfIntCastsFromSign(
      BinaryOperator &BO, bool OpsFromSigned, std::array<Value *, 2> IntOps,
      Constant *Op1FpC, SmallVectorImpl<WithCache<const Value *>> &OpsKnown);
  Instruction *foldBinopOfSextBoolToSelect(BinaryOperator &I);
  Instruction *narrowBinOp(TruncInst &Trunc);
  Instruction *narrowMaskedBinOp(BinaryOperator &And);
  Instruction *narrowMathIfNoOverflow(BinaryOperator &I);
  Instruction *narrowFunnelShift(TruncInst &Trunc);
  Instruction *optimizeBitCastFromPhi(CastInst &CI, PHINode *PN);
  Instruction *matchSAddSubSat(IntrinsicInst &MinMax1);
  Instruction *foldNot(BinaryOperator &I);
  Instruction *foldBinOpOfDisplacedShifts(BinaryOperator &I);

  /// Determine if a pair of casts can be replaced by a single cast.
  ///
  /// \param CI1 The first of a pair of casts.
  /// \param CI2 The second of a pair of casts.
  ///
  /// \return 0 if the cast pair cannot be eliminated, otherwise returns an
  /// Instruction::CastOps value for a cast that can replace the pair, casting
  /// CI1->getSrcTy() to CI2->getDstTy().
  ///
  /// \see CastInst::isEliminableCastPair
  Instruction::CastOps isEliminableCastPair(const CastInst *CI1,
                                            const CastInst *CI2);
  Value *simplifyIntToPtrRoundTripCast(Value *Val);

  Value *foldAndOrOfICmps(ICmpInst *LHS, ICmpInst *RHS, Instruction &I,
                          bool IsAnd, bool IsLogical = false);
  Value *foldXorOfICmps(ICmpInst *LHS, ICmpInst *RHS, BinaryOperator &Xor);

  Value *foldEqOfParts(ICmpInst *Cmp0, ICmpInst *Cmp1, bool IsAnd);

  Value *foldAndOrOfICmpsUsingRanges(ICmpInst *ICmp1, ICmpInst *ICmp2,
                                     bool IsAnd);

  /// Optimize (fcmp)&(fcmp) or (fcmp)|(fcmp).
  /// NOTE: Unlike most of instcombine, this returns a Value which should
  /// already be inserted into the function.
  Value *foldLogicOfFCmps(FCmpInst *LHS, FCmpInst *RHS, bool IsAnd,
                          bool IsLogicalSelect = false);

  Instruction *foldLogicOfIsFPClass(BinaryOperator &Operator, Value *LHS,
                                    Value *RHS);

  Instruction *
  canonicalizeConditionalNegationViaMathToSelect(BinaryOperator &i);

  Value *foldAndOrOfICmpsOfAndWithPow2(ICmpInst *LHS, ICmpInst *RHS,
                                       Instruction *CxtI, bool IsAnd,
                                       bool IsLogical = false);
  Value *matchSelectFromAndOr(Value *A, Value *B, Value *C, Value *D,
                              bool InvertFalseVal = false);
  Value *getSelectCondition(Value *A, Value *B, bool ABIsTheSame);

  Instruction *foldLShrOverflowBit(BinaryOperator &I);
  Instruction *foldExtractOfOverflowIntrinsic(ExtractValueInst &EV);
  Instruction *foldIntrinsicWithOverflowCommon(IntrinsicInst *II);
  Instruction *foldIntrinsicIsFPClass(IntrinsicInst &II);
  Instruction *foldFPSignBitOps(BinaryOperator &I);
  Instruction *foldFDivConstantDivisor(BinaryOperator &I);

  // Optimize one of these forms:
  //   and i1 Op, SI / select i1 Op, i1 SI, i1 false (if IsAnd = true)
  //   or i1 Op, SI  / select i1 Op, i1 true, i1 SI  (if IsAnd = false)
  // into simplier select instruction using isImpliedCondition.
  Instruction *foldAndOrOfSelectUsingImpliedCond(Value *Op, SelectInst &SI,
                                                 bool IsAnd);

  Instruction *hoistFNegAboveFMulFDiv(Value *FNegOp, Instruction &FMFSource);

public:
  /// Create and insert the idiom we use to indicate a block is unreachable
  /// without having to rewrite the CFG from within InstCombine.
  void CreateNonTerminatorUnreachable(Instruction *InsertAt) {
    auto &Ctx = InsertAt->getContext();
    auto *SI = new StoreInst(ConstantInt::getTrue(Ctx),
                             PoisonValue::get(PointerType::getUnqual(Ctx)),
                             /*isVolatile*/ false, Align(1));
    InsertNewInstWith(SI, InsertAt->getIterator());
  }

  /// Combiner aware instruction erasure.
  ///
  /// When dealing with an instruction that has side effects or produces a void
  /// value, we can't rely on DCE to delete the instruction. Instead, visit
  /// methods should return the value returned by this function.
  Instruction *eraseInstFromFunction(Instruction &I) override {
    LLVM_DEBUG(dbgs() << "IC: ERASE " << I << '\n');
    assert(I.use_empty() && "Cannot erase instruction that is used!");
    salvageDebugInfo(I);

    // Make sure that we reprocess all operands now that we reduced their
    // use counts.
    SmallVector<Value *> Ops(I.operands());
    Worklist.remove(&I);
    DC.removeValue(&I);
    I.eraseFromParent();
    for (Value *Op : Ops)
      Worklist.handleUseCountDecrement(Op);
    MadeIRChange = true;
    return nullptr; // Don't do anything with FI
  }

  OverflowResult computeOverflow(
      Instruction::BinaryOps BinaryOp, bool IsSigned,
      Value *LHS, Value *RHS, Instruction *CxtI) const;

  /// Performs a few simplifications for operators which are associative
  /// or commutative.
  bool SimplifyAssociativeOrCommutative(BinaryOperator &I);

  /// Tries to simplify binary operations which some other binary
  /// operation distributes over.
  ///
  /// It does this by either by factorizing out common terms (eg "(A*B)+(A*C)"
  /// -> "A*(B+C)") or expanding out if this results in simplifications (eg: "A
  /// & (B | C) -> (A&B) | (A&C)" if this is a win).  Returns the simplified
  /// value, or null if it didn't simplify.
  Value *foldUsingDistributiveLaws(BinaryOperator &I);

  /// Tries to simplify add operations using the definition of remainder.
  ///
  /// The definition of remainder is X % C = X - (X / C ) * C. The add
  /// expression X % C0 + (( X / C0 ) % C1) * C0 can be simplified to
  /// X % (C0 * C1)
  Value *SimplifyAddWithRemainder(BinaryOperator &I);

  // Binary Op helper for select operations where the expression can be
  // efficiently reorganized.
  Value *SimplifySelectsFeedingBinaryOp(BinaryOperator &I, Value *LHS,
                                        Value *RHS);

  // If `I` has operand `(ctpop (not x))`, fold `I` with `(sub nuw nsw
  // BitWidth(x), (ctpop x))`.
  Instruction *tryFoldInstWithCtpopWithNot(Instruction *I);

  // (Binop1 (Binop2 (logic_shift X, C), C1), (logic_shift Y, C))
  //    -> (logic_shift (Binop1 (Binop2 X, inv_logic_shift(C1, C)), Y), C)
  // (Binop1 (Binop2 (logic_shift X, Amt), Mask), (logic_shift Y, Amt))
  //    -> (BinOp (logic_shift (BinOp X, Y)), Mask)
  Instruction *foldBinOpShiftWithShift(BinaryOperator &I);

  /// Tries to simplify binops of select and cast of the select condition.
  ///
  /// (Binop (cast C), (select C, T, F))
  ///    -> (select C, C0, C1)
  Instruction *foldBinOpOfSelectAndCastOfSelectCondition(BinaryOperator &I);

  /// This tries to simplify binary operations by factorizing out common terms
  /// (e. g. "(A*B)+(A*C)" -> "A*(B+C)").
  Value *tryFactorizationFolds(BinaryOperator &I);

  /// Match a select chain which produces one of three values based on whether
  /// the LHS is less than, equal to, or greater than RHS respectively.
  /// Return true if we matched a three way compare idiom. The LHS, RHS, Less,
  /// Equal and Greater values are saved in the matching process and returned to
  /// the caller.
  bool matchThreeWayIntCompare(SelectInst *SI, Value *&LHS, Value *&RHS,
                               ConstantInt *&Less, ConstantInt *&Equal,
                               ConstantInt *&Greater);

  /// Attempts to replace I with a simpler value based on the demanded
  /// bits.
  Value *SimplifyDemandedUseBits(Instruction *I, const APInt &DemandedMask,
                                 KnownBits &Known, unsigned Depth,
                                 const SimplifyQuery &Q);
  using InstCombiner::SimplifyDemandedBits;
  bool SimplifyDemandedBits(Instruction *I, unsigned Op,
                            const APInt &DemandedMask, KnownBits &Known,
                            unsigned Depth, const SimplifyQuery &Q) override;

  /// Helper routine of SimplifyDemandedUseBits. It computes KnownZero/KnownOne
  /// bits. It also tries to handle simplifications that can be done based on
  /// DemandedMask, but without modifying the Instruction.
  Value *SimplifyMultipleUseDemandedBits(Instruction *I,
                                         const APInt &DemandedMask,
                                         KnownBits &Known, unsigned Depth,
                                         const SimplifyQuery &Q);

  /// Helper routine of SimplifyDemandedUseBits. It tries to simplify demanded
  /// bit for "r1 = shr x, c1; r2 = shl r1, c2" instruction sequence.
  Value *simplifyShrShlDemandedBits(
      Instruction *Shr, const APInt &ShrOp1, Instruction *Shl,
      const APInt &ShlOp1, const APInt &DemandedMask, KnownBits &Known);

  /// Tries to simplify operands to an integer instruction based on its
  /// demanded bits.
  bool SimplifyDemandedInstructionBits(Instruction &Inst);
  bool SimplifyDemandedInstructionBits(Instruction &Inst, KnownBits &Known);

  Value *SimplifyDemandedVectorElts(Value *V, APInt DemandedElts,
                                    APInt &PoisonElts, unsigned Depth = 0,
                                    bool AllowMultipleUsers = false) override;

  /// Attempts to replace V with a simpler value based on the demanded
  /// floating-point classes
  Value *SimplifyDemandedUseFPClass(Value *V, FPClassTest DemandedMask,
                                    KnownFPClass &Known, unsigned Depth,
                                    Instruction *CxtI);
  bool SimplifyDemandedFPClass(Instruction *I, unsigned Op,
                               FPClassTest DemandedMask, KnownFPClass &Known,
                               unsigned Depth = 0);

  /// Canonicalize the position of binops relative to shufflevector.
  Instruction *foldVectorBinop(BinaryOperator &Inst);
  Instruction *foldVectorSelect(SelectInst &Sel);
  Instruction *foldSelectShuffle(ShuffleVectorInst &Shuf);

  /// Given a binary operator, cast instruction, or select which has a PHI node
  /// as operand #0, see if we can fold the instruction into the PHI (which is
  /// only possible if all operands to the PHI are constants).
  Instruction *foldOpIntoPhi(Instruction &I, PHINode *PN);

  /// For a binary operator with 2 phi operands, try to hoist the binary
  /// operation before the phi. This can result in fewer instructions in
  /// patterns where at least one set of phi operands simplifies.
  /// Example:
  /// BB3: binop (phi [X, BB1], [C1, BB2]), (phi [Y, BB1], [C2, BB2])
  /// -->
  /// BB1: BO = binop X, Y
  /// BB3: phi [BO, BB1], [(binop C1, C2), BB2]
  Instruction *foldBinopWithPhiOperands(BinaryOperator &BO);

  /// Given an instruction with a select as one operand and a constant as the
  /// other operand, try to fold the binary operator into the select arguments.
  /// This also works for Cast instructions, which obviously do not have a
  /// second operand.
  Instruction *FoldOpIntoSelect(Instruction &Op, SelectInst *SI,
                                bool FoldWithMultiUse = false);

  /// This is a convenience wrapper function for the above two functions.
  Instruction *foldBinOpIntoSelectOrPhi(BinaryOperator &I);

  Instruction *foldAddWithConstant(BinaryOperator &Add);

  Instruction *foldSquareSumInt(BinaryOperator &I);
  Instruction *foldSquareSumFP(BinaryOperator &I);

  /// Try to rotate an operation below a PHI node, using PHI nodes for
  /// its operands.
  Instruction *foldPHIArgOpIntoPHI(PHINode &PN);
  Instruction *foldPHIArgBinOpIntoPHI(PHINode &PN);
  Instruction *foldPHIArgInsertValueInstructionIntoPHI(PHINode &PN);
  Instruction *foldPHIArgExtractValueInstructionIntoPHI(PHINode &PN);
  Instruction *foldPHIArgGEPIntoPHI(PHINode &PN);
  Instruction *foldPHIArgLoadIntoPHI(PHINode &PN);
  Instruction *foldPHIArgZextsIntoPHI(PHINode &PN);
  Instruction *foldPHIArgIntToPtrToPHI(PHINode &PN);

  /// If an integer typed PHI has only one use which is an IntToPtr operation,
  /// replace the PHI with an existing pointer typed PHI if it exists. Otherwise
  /// insert a new pointer typed PHI and replace the original one.
  bool foldIntegerTypedPHI(PHINode &PN);

  /// Helper function for FoldPHIArgXIntoPHI() to set debug location for the
  /// folded operation.
  void PHIArgMergedDebugLoc(Instruction *Inst, PHINode &PN);

  Instruction *foldGEPICmp(GEPOperator *GEPLHS, Value *RHS,
                           ICmpInst::Predicate Cond, Instruction &I);
  Instruction *foldSelectICmp(ICmpInst::Predicate Pred, SelectInst *SI,
                              Value *RHS, const ICmpInst &I);
  bool foldAllocaCmp(AllocaInst *Alloca);
  Instruction *foldCmpLoadFromIndexedGlobal(LoadInst *LI,
                                            GetElementPtrInst *GEP,
                                            GlobalVariable *GV, CmpInst &ICI,
                                            ConstantInt *AndCst = nullptr);
  Instruction *foldFCmpIntToFPConst(FCmpInst &I, Instruction *LHSI,
                                    Constant *RHSC);
  Instruction *foldICmpAddOpConst(Value *X, const APInt &C,
                                  ICmpInst::Predicate Pred);
  Instruction *foldICmpWithCastOp(ICmpInst &ICmp);
  Instruction *foldICmpWithZextOrSext(ICmpInst &ICmp);

  Instruction *foldICmpUsingKnownBits(ICmpInst &Cmp);
  Instruction *foldICmpWithDominatingICmp(ICmpInst &Cmp);
  Instruction *foldICmpWithConstant(ICmpInst &Cmp);
  Instruction *foldICmpUsingBoolRange(ICmpInst &I);
  Instruction *foldICmpInstWithConstant(ICmpInst &Cmp);
  Instruction *foldICmpInstWithConstantNotInt(ICmpInst &Cmp);
  Instruction *foldICmpInstWithConstantAllowPoison(ICmpInst &Cmp,
                                                   const APInt &C);
  Instruction *foldICmpBinOp(ICmpInst &Cmp, const SimplifyQuery &SQ);
  Instruction *foldICmpWithMinMax(Instruction &I, MinMaxIntrinsic *MinMax,
                                  Value *Z, ICmpInst::Predicate Pred);
  Instruction *foldICmpEquality(ICmpInst &Cmp);
  Instruction *foldIRemByPowerOfTwoToBitTest(ICmpInst &I);
  Instruction *foldSignBitTest(ICmpInst &I);
  Instruction *foldICmpWithZero(ICmpInst &Cmp);

  Value *foldMultiplicationOverflowCheck(ICmpInst &Cmp);

  Instruction *foldICmpBinOpWithConstant(ICmpInst &Cmp, BinaryOperator *BO,
                                         const APInt &C);
  Instruction *foldICmpSelectConstant(ICmpInst &Cmp, SelectInst *Select,
                                      ConstantInt *C);
  Instruction *foldICmpTruncConstant(ICmpInst &Cmp, TruncInst *Trunc,
                                     const APInt &C);
  Instruction *foldICmpTruncWithTruncOrExt(ICmpInst &Cmp,
                                           const SimplifyQuery &Q);
  Instruction *foldICmpAndConstant(ICmpInst &Cmp, BinaryOperator *And,
                                   const APInt &C);
  Instruction *foldICmpXorConstant(ICmpInst &Cmp, BinaryOperator *Xor,
                                   const APInt &C);
  Instruction *foldICmpOrConstant(ICmpInst &Cmp, BinaryOperator *Or,
                                  const APInt &C);
  Instruction *foldICmpMulConstant(ICmpInst &Cmp, BinaryOperator *Mul,
                                   const APInt &C);
  Instruction *foldICmpShlConstant(ICmpInst &Cmp, BinaryOperator *Shl,
                                   const APInt &C);
  Instruction *foldICmpShrConstant(ICmpInst &Cmp, BinaryOperator *Shr,
                                   const APInt &C);
  Instruction *foldICmpSRemConstant(ICmpInst &Cmp, BinaryOperator *UDiv,
                                    const APInt &C);
  Instruction *foldICmpUDivConstant(ICmpInst &Cmp, BinaryOperator *UDiv,
                                    const APInt &C);
  Instruction *foldICmpDivConstant(ICmpInst &Cmp, BinaryOperator *Div,
                                   const APInt &C);
  Instruction *foldICmpSubConstant(ICmpInst &Cmp, BinaryOperator *Sub,
                                   const APInt &C);
  Instruction *foldICmpAddConstant(ICmpInst &Cmp, BinaryOperator *Add,
                                   const APInt &C);
  Instruction *foldICmpAndConstConst(ICmpInst &Cmp, BinaryOperator *And,
                                     const APInt &C1);
  Instruction *foldICmpAndShift(ICmpInst &Cmp, BinaryOperator *And,
                                const APInt &C1, const APInt &C2);
  Instruction *foldICmpXorShiftConst(ICmpInst &Cmp, BinaryOperator *Xor,
                                     const APInt &C);
  Instruction *foldICmpShrConstConst(ICmpInst &I, Value *ShAmt, const APInt &C1,
                                     const APInt &C2);
  Instruction *foldICmpShlConstConst(ICmpInst &I, Value *ShAmt, const APInt &C1,
                                     const APInt &C2);

  Instruction *foldICmpBinOpEqualityWithConstant(ICmpInst &Cmp,
                                                 BinaryOperator *BO,
                                                 const APInt &C);
  Instruction *foldICmpIntrinsicWithConstant(ICmpInst &ICI, IntrinsicInst *II,
                                             const APInt &C);
  Instruction *foldICmpEqIntrinsicWithConstant(ICmpInst &ICI, IntrinsicInst *II,
                                               const APInt &C);
  Instruction *foldICmpBitCast(ICmpInst &Cmp);
  Instruction *foldICmpWithTrunc(ICmpInst &Cmp);
  Instruction *foldICmpCommutative(ICmpInst::Predicate Pred, Value *Op0,
                                   Value *Op1, ICmpInst &CxtI);

  // Helpers of visitSelectInst().
  Instruction *foldSelectOfBools(SelectInst &SI);
  Instruction *foldSelectExtConst(SelectInst &Sel);
  Instruction *foldSelectOpOp(SelectInst &SI, Instruction *TI, Instruction *FI);
  Instruction *foldSelectIntoOp(SelectInst &SI, Value *, Value *);
  Instruction *foldSPFofSPF(Instruction *Inner, SelectPatternFlavor SPF1,
                            Value *A, Value *B, Instruction &Outer,
                            SelectPatternFlavor SPF2, Value *C);
  Instruction *foldSelectInstWithICmp(SelectInst &SI, ICmpInst *ICI);
  Instruction *foldSelectValueEquivalence(SelectInst &SI, ICmpInst &ICI);
  bool replaceInInstruction(Value *V, Value *Old, Value *New,
                            unsigned Depth = 0);

  Value *insertRangeTest(Value *V, const APInt &Lo, const APInt &Hi,
                         bool isSigned, bool Inside);
  bool mergeStoreIntoSuccessor(StoreInst &SI);

  /// Given an initial instruction, check to see if it is the root of a
  /// bswap/bitreverse idiom. If so, return the equivalent bswap/bitreverse
  /// intrinsic.
  Instruction *matchBSwapOrBitReverse(Instruction &I, bool MatchBSwaps,
                                      bool MatchBitReversals);

  Instruction *SimplifyAnyMemTransfer(AnyMemTransferInst *MI);
  Instruction *SimplifyAnyMemSet(AnyMemSetInst *MI);

  Value *EvaluateInDifferentType(Value *V, Type *Ty, bool isSigned);

  bool tryToSinkInstruction(Instruction *I, BasicBlock *DestBlock);
  void tryToSinkInstructionDbgValues(
      Instruction *I, BasicBlock::iterator InsertPos, BasicBlock *SrcBlock,
      BasicBlock *DestBlock, SmallVectorImpl<DbgVariableIntrinsic *> &DbgUsers);
  void tryToSinkInstructionDbgVariableRecords(
      Instruction *I, BasicBlock::iterator InsertPos, BasicBlock *SrcBlock,
      BasicBlock *DestBlock, SmallVectorImpl<DbgVariableRecord *> &DPUsers);

  bool removeInstructionsBeforeUnreachable(Instruction &I);
  void addDeadEdge(BasicBlock *From, BasicBlock *To,
                   SmallVectorImpl<BasicBlock *> &Worklist);
  void handleUnreachableFrom(Instruction *I,
                             SmallVectorImpl<BasicBlock *> &Worklist);
  void handlePotentiallyDeadBlocks(SmallVectorImpl<BasicBlock *> &Worklist);
  void handlePotentiallyDeadSuccessors(BasicBlock *BB, BasicBlock *LiveSucc);
  void freelyInvertAllUsersOf(Value *V, Value *IgnoredUser = nullptr);
};

class Negator final {
  /// Top-to-bottom, def-to-use negated instruction tree we produced.
  SmallVector<Instruction *, NegatorMaxNodesSSO> NewInstructions;

  using BuilderTy = IRBuilder<TargetFolder, IRBuilderCallbackInserter>;
  BuilderTy Builder;

  const bool IsTrulyNegation;

  SmallDenseMap<Value *, Value *> NegationsCache;

  Negator(LLVMContext &C, const DataLayout &DL, bool IsTrulyNegation);

#if LLVM_ENABLE_STATS
  unsigned NumValuesVisitedInThisNegator = 0;
  ~Negator();
#endif

  using Result = std::pair<ArrayRef<Instruction *> /*NewInstructions*/,
                           Value * /*NegatedRoot*/>;

  std::array<Value *, 2> getSortedOperandsOfBinOp(Instruction *I);

  [[nodiscard]] Value *visitImpl(Value *V, bool IsNSW, unsigned Depth);

  [[nodiscard]] Value *negate(Value *V, bool IsNSW, unsigned Depth);

  /// Recurse depth-first and attempt to sink the negation.
  /// FIXME: use worklist?
  [[nodiscard]] std::optional<Result> run(Value *Root, bool IsNSW);

  Negator(const Negator &) = delete;
  Negator(Negator &&) = delete;
  Negator &operator=(const Negator &) = delete;
  Negator &operator=(Negator &&) = delete;

public:
  /// Attempt to negate \p Root. Retuns nullptr if negation can't be performed,
  /// otherwise returns negated value.
  [[nodiscard]] static Value *Negate(bool LHSIsZero, bool IsNSW, Value *Root,
                                     InstCombinerImpl &IC);
};

} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_LIB_TRANSFORMS_INSTCOMBINE_INSTCOMBINEINTERNAL_H
