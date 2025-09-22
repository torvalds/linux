//===-------- LoopIdiomVectorize.cpp - Loop idiom vectorization -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements a pass that recognizes certain loop idioms and
// transforms them into more optimized versions of the same loop. In cases
// where this happens, it can be a significant performance win.
//
// We currently only recognize one loop that finds the first mismatched byte
// in an array and returns the index, i.e. something like:
//
//  while (++i != n) {
//    if (a[i] != b[i])
//      break;
//  }
//
// In this example we can actually vectorize the loop despite the early exit,
// although the loop vectorizer does not support it. It requires some extra
// checks to deal with the possibility of faulting loads when crossing page
// boundaries. However, even with these checks it is still profitable to do the
// transformation.
//
//===----------------------------------------------------------------------===//
//
// NOTE: This Pass matches a really specific loop pattern because it's only
// supposed to be a temporary solution until our LoopVectorizer is powerful
// enought to vectorize it automatically.
//
// TODO List:
//
// * Add support for the inverse case where we scan for a matching element.
// * Permit 64-bit induction variable types.
// * Recognize loops that increment the IV *after* comparing bytes.
// * Allow 32-bit sign-extends of the IV used by the GEP.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Vectorize/LoopIdiomVectorize.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "loop-idiom-vectorize"

static cl::opt<bool> DisableAll("disable-loop-idiom-vectorize-all", cl::Hidden,
                                cl::init(false),
                                cl::desc("Disable Loop Idiom Vectorize Pass."));

static cl::opt<LoopIdiomVectorizeStyle>
    LITVecStyle("loop-idiom-vectorize-style", cl::Hidden,
                cl::desc("The vectorization style for loop idiom transform."),
                cl::values(clEnumValN(LoopIdiomVectorizeStyle::Masked, "masked",
                                      "Use masked vector intrinsics"),
                           clEnumValN(LoopIdiomVectorizeStyle::Predicated,
                                      "predicated", "Use VP intrinsics")),
                cl::init(LoopIdiomVectorizeStyle::Masked));

static cl::opt<bool>
    DisableByteCmp("disable-loop-idiom-vectorize-bytecmp", cl::Hidden,
                   cl::init(false),
                   cl::desc("Proceed with Loop Idiom Vectorize Pass, but do "
                            "not convert byte-compare loop(s)."));

static cl::opt<unsigned>
    ByteCmpVF("loop-idiom-vectorize-bytecmp-vf", cl::Hidden,
              cl::desc("The vectorization factor for byte-compare patterns."),
              cl::init(16));

static cl::opt<bool>
    VerifyLoops("loop-idiom-vectorize-verify", cl::Hidden, cl::init(false),
                cl::desc("Verify loops generated Loop Idiom Vectorize Pass."));

namespace {
class LoopIdiomVectorize {
  LoopIdiomVectorizeStyle VectorizeStyle;
  unsigned ByteCompareVF;
  Loop *CurLoop = nullptr;
  DominatorTree *DT;
  LoopInfo *LI;
  const TargetTransformInfo *TTI;
  const DataLayout *DL;

  // Blocks that will be used for inserting vectorized code.
  BasicBlock *EndBlock = nullptr;
  BasicBlock *VectorLoopPreheaderBlock = nullptr;
  BasicBlock *VectorLoopStartBlock = nullptr;
  BasicBlock *VectorLoopMismatchBlock = nullptr;
  BasicBlock *VectorLoopIncBlock = nullptr;

public:
  LoopIdiomVectorize(LoopIdiomVectorizeStyle S, unsigned VF, DominatorTree *DT,
                     LoopInfo *LI, const TargetTransformInfo *TTI,
                     const DataLayout *DL)
      : VectorizeStyle(S), ByteCompareVF(VF), DT(DT), LI(LI), TTI(TTI), DL(DL) {
  }

  bool run(Loop *L);

private:
  /// \name Countable Loop Idiom Handling
  /// @{

  bool runOnCountableLoop();
  bool runOnLoopBlock(BasicBlock *BB, const SCEV *BECount,
                      SmallVectorImpl<BasicBlock *> &ExitBlocks);

  bool recognizeByteCompare();

  Value *expandFindMismatch(IRBuilder<> &Builder, DomTreeUpdater &DTU,
                            GetElementPtrInst *GEPA, GetElementPtrInst *GEPB,
                            Instruction *Index, Value *Start, Value *MaxLen);

  Value *createMaskedFindMismatch(IRBuilder<> &Builder, DomTreeUpdater &DTU,
                                  GetElementPtrInst *GEPA,
                                  GetElementPtrInst *GEPB, Value *ExtStart,
                                  Value *ExtEnd);
  Value *createPredicatedFindMismatch(IRBuilder<> &Builder, DomTreeUpdater &DTU,
                                      GetElementPtrInst *GEPA,
                                      GetElementPtrInst *GEPB, Value *ExtStart,
                                      Value *ExtEnd);

  void transformByteCompare(GetElementPtrInst *GEPA, GetElementPtrInst *GEPB,
                            PHINode *IndPhi, Value *MaxLen, Instruction *Index,
                            Value *Start, bool IncIdx, BasicBlock *FoundBB,
                            BasicBlock *EndBB);
  /// @}
};
} // anonymous namespace

PreservedAnalyses LoopIdiomVectorizePass::run(Loop &L, LoopAnalysisManager &AM,
                                              LoopStandardAnalysisResults &AR,
                                              LPMUpdater &) {
  if (DisableAll)
    return PreservedAnalyses::all();

  const auto *DL = &L.getHeader()->getDataLayout();

  LoopIdiomVectorizeStyle VecStyle = VectorizeStyle;
  if (LITVecStyle.getNumOccurrences())
    VecStyle = LITVecStyle;

  unsigned BCVF = ByteCompareVF;
  if (ByteCmpVF.getNumOccurrences())
    BCVF = ByteCmpVF;

  LoopIdiomVectorize LIV(VecStyle, BCVF, &AR.DT, &AR.LI, &AR.TTI, DL);
  if (!LIV.run(&L))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

//===----------------------------------------------------------------------===//
//
//          Implementation of LoopIdiomVectorize
//
//===----------------------------------------------------------------------===//

bool LoopIdiomVectorize::run(Loop *L) {
  CurLoop = L;

  Function &F = *L->getHeader()->getParent();
  if (DisableAll || F.hasOptSize())
    return false;

  if (F.hasFnAttribute(Attribute::NoImplicitFloat)) {
    LLVM_DEBUG(dbgs() << DEBUG_TYPE << " is disabled on " << F.getName()
                      << " due to its NoImplicitFloat attribute");
    return false;
  }

  // If the loop could not be converted to canonical form, it must have an
  // indirectbr in it, just give up.
  if (!L->getLoopPreheader())
    return false;

  LLVM_DEBUG(dbgs() << DEBUG_TYPE " Scanning: F[" << F.getName() << "] Loop %"
                    << CurLoop->getHeader()->getName() << "\n");

  return recognizeByteCompare();
}

bool LoopIdiomVectorize::recognizeByteCompare() {
  // Currently the transformation only works on scalable vector types, although
  // there is no fundamental reason why it cannot be made to work for fixed
  // width too.

  // We also need to know the minimum page size for the target in order to
  // generate runtime memory checks to ensure the vector version won't fault.
  if (!TTI->supportsScalableVectors() || !TTI->getMinPageSize().has_value() ||
      DisableByteCmp)
    return false;

  BasicBlock *Header = CurLoop->getHeader();

  // In LoopIdiomVectorize::run we have already checked that the loop
  // has a preheader so we can assume it's in a canonical form.
  if (CurLoop->getNumBackEdges() != 1 || CurLoop->getNumBlocks() != 2)
    return false;

  PHINode *PN = dyn_cast<PHINode>(&Header->front());
  if (!PN || PN->getNumIncomingValues() != 2)
    return false;

  auto LoopBlocks = CurLoop->getBlocks();
  // The first block in the loop should contain only 4 instructions, e.g.
  //
  //  while.cond:
  //   %res.phi = phi i32 [ %start, %ph ], [ %inc, %while.body ]
  //   %inc = add i32 %res.phi, 1
  //   %cmp.not = icmp eq i32 %inc, %n
  //   br i1 %cmp.not, label %while.end, label %while.body
  //
  if (LoopBlocks[0]->sizeWithoutDebug() > 4)
    return false;

  // The second block should contain 7 instructions, e.g.
  //
  // while.body:
  //   %idx = zext i32 %inc to i64
  //   %idx.a = getelementptr inbounds i8, ptr %a, i64 %idx
  //   %load.a = load i8, ptr %idx.a
  //   %idx.b = getelementptr inbounds i8, ptr %b, i64 %idx
  //   %load.b = load i8, ptr %idx.b
  //   %cmp.not.ld = icmp eq i8 %load.a, %load.b
  //   br i1 %cmp.not.ld, label %while.cond, label %while.end
  //
  if (LoopBlocks[1]->sizeWithoutDebug() > 7)
    return false;

  // The incoming value to the PHI node from the loop should be an add of 1.
  Value *StartIdx = nullptr;
  Instruction *Index = nullptr;
  if (!CurLoop->contains(PN->getIncomingBlock(0))) {
    StartIdx = PN->getIncomingValue(0);
    Index = dyn_cast<Instruction>(PN->getIncomingValue(1));
  } else {
    StartIdx = PN->getIncomingValue(1);
    Index = dyn_cast<Instruction>(PN->getIncomingValue(0));
  }

  // Limit to 32-bit types for now
  if (!Index || !Index->getType()->isIntegerTy(32) ||
      !match(Index, m_c_Add(m_Specific(PN), m_One())))
    return false;

  // If we match the pattern, PN and Index will be replaced with the result of
  // the cttz.elts intrinsic. If any other instructions are used outside of
  // the loop, we cannot replace it.
  for (BasicBlock *BB : LoopBlocks)
    for (Instruction &I : *BB)
      if (&I != PN && &I != Index)
        for (User *U : I.users())
          if (!CurLoop->contains(cast<Instruction>(U)))
            return false;

  // Match the branch instruction for the header
  ICmpInst::Predicate Pred;
  Value *MaxLen;
  BasicBlock *EndBB, *WhileBB;
  if (!match(Header->getTerminator(),
             m_Br(m_ICmp(Pred, m_Specific(Index), m_Value(MaxLen)),
                  m_BasicBlock(EndBB), m_BasicBlock(WhileBB))) ||
      Pred != ICmpInst::Predicate::ICMP_EQ || !CurLoop->contains(WhileBB))
    return false;

  // WhileBB should contain the pattern of load & compare instructions. Match
  // the pattern and find the GEP instructions used by the loads.
  ICmpInst::Predicate WhilePred;
  BasicBlock *FoundBB;
  BasicBlock *TrueBB;
  Value *LoadA, *LoadB;
  if (!match(WhileBB->getTerminator(),
             m_Br(m_ICmp(WhilePred, m_Value(LoadA), m_Value(LoadB)),
                  m_BasicBlock(TrueBB), m_BasicBlock(FoundBB))) ||
      WhilePred != ICmpInst::Predicate::ICMP_EQ || !CurLoop->contains(TrueBB))
    return false;

  Value *A, *B;
  if (!match(LoadA, m_Load(m_Value(A))) || !match(LoadB, m_Load(m_Value(B))))
    return false;

  LoadInst *LoadAI = cast<LoadInst>(LoadA);
  LoadInst *LoadBI = cast<LoadInst>(LoadB);
  if (!LoadAI->isSimple() || !LoadBI->isSimple())
    return false;

  GetElementPtrInst *GEPA = dyn_cast<GetElementPtrInst>(A);
  GetElementPtrInst *GEPB = dyn_cast<GetElementPtrInst>(B);

  if (!GEPA || !GEPB)
    return false;

  Value *PtrA = GEPA->getPointerOperand();
  Value *PtrB = GEPB->getPointerOperand();

  // Check we are loading i8 values from two loop invariant pointers
  if (!CurLoop->isLoopInvariant(PtrA) || !CurLoop->isLoopInvariant(PtrB) ||
      !GEPA->getResultElementType()->isIntegerTy(8) ||
      !GEPB->getResultElementType()->isIntegerTy(8) ||
      !LoadAI->getType()->isIntegerTy(8) ||
      !LoadBI->getType()->isIntegerTy(8) || PtrA == PtrB)
    return false;

  // Check that the index to the GEPs is the index we found earlier
  if (GEPA->getNumIndices() > 1 || GEPB->getNumIndices() > 1)
    return false;

  Value *IdxA = GEPA->getOperand(GEPA->getNumIndices());
  Value *IdxB = GEPB->getOperand(GEPB->getNumIndices());
  if (IdxA != IdxB || !match(IdxA, m_ZExt(m_Specific(Index))))
    return false;

  // We only ever expect the pre-incremented index value to be used inside the
  // loop.
  if (!PN->hasOneUse())
    return false;

  // Ensure that when the Found and End blocks are identical the PHIs have the
  // supported format. We don't currently allow cases like this:
  // while.cond:
  //   ...
  //   br i1 %cmp.not, label %while.end, label %while.body
  //
  // while.body:
  //   ...
  //   br i1 %cmp.not2, label %while.cond, label %while.end
  //
  // while.end:
  //   %final_ptr = phi ptr [ %c, %while.body ], [ %d, %while.cond ]
  //
  // Where the incoming values for %final_ptr are unique and from each of the
  // loop blocks, but not actually defined in the loop. This requires extra
  // work setting up the byte.compare block, i.e. by introducing a select to
  // choose the correct value.
  // TODO: We could add support for this in future.
  if (FoundBB == EndBB) {
    for (PHINode &EndPN : EndBB->phis()) {
      Value *WhileCondVal = EndPN.getIncomingValueForBlock(Header);
      Value *WhileBodyVal = EndPN.getIncomingValueForBlock(WhileBB);

      // The value of the index when leaving the while.cond block is always the
      // same as the end value (MaxLen) so we permit either. The value when
      // leaving the while.body block should only be the index. Otherwise for
      // any other values we only allow ones that are same for both blocks.
      if (WhileCondVal != WhileBodyVal &&
          ((WhileCondVal != Index && WhileCondVal != MaxLen) ||
           (WhileBodyVal != Index)))
        return false;
    }
  }

  LLVM_DEBUG(dbgs() << "FOUND IDIOM IN LOOP: \n"
                    << *(EndBB->getParent()) << "\n\n");

  // The index is incremented before the GEP/Load pair so we need to
  // add 1 to the start value.
  transformByteCompare(GEPA, GEPB, PN, MaxLen, Index, StartIdx, /*IncIdx=*/true,
                       FoundBB, EndBB);
  return true;
}

Value *LoopIdiomVectorize::createMaskedFindMismatch(
    IRBuilder<> &Builder, DomTreeUpdater &DTU, GetElementPtrInst *GEPA,
    GetElementPtrInst *GEPB, Value *ExtStart, Value *ExtEnd) {
  Type *I64Type = Builder.getInt64Ty();
  Type *ResType = Builder.getInt32Ty();
  Type *LoadType = Builder.getInt8Ty();
  Value *PtrA = GEPA->getPointerOperand();
  Value *PtrB = GEPB->getPointerOperand();

  ScalableVectorType *PredVTy =
      ScalableVectorType::get(Builder.getInt1Ty(), ByteCompareVF);

  Value *InitialPred = Builder.CreateIntrinsic(
      Intrinsic::get_active_lane_mask, {PredVTy, I64Type}, {ExtStart, ExtEnd});

  Value *VecLen = Builder.CreateIntrinsic(Intrinsic::vscale, {I64Type}, {});
  VecLen =
      Builder.CreateMul(VecLen, ConstantInt::get(I64Type, ByteCompareVF), "",
                        /*HasNUW=*/true, /*HasNSW=*/true);

  Value *PFalse = Builder.CreateVectorSplat(PredVTy->getElementCount(),
                                            Builder.getInt1(false));

  BranchInst *JumpToVectorLoop = BranchInst::Create(VectorLoopStartBlock);
  Builder.Insert(JumpToVectorLoop);

  DTU.applyUpdates({{DominatorTree::Insert, VectorLoopPreheaderBlock,
                     VectorLoopStartBlock}});

  // Set up the first vector loop block by creating the PHIs, doing the vector
  // loads and comparing the vectors.
  Builder.SetInsertPoint(VectorLoopStartBlock);
  PHINode *LoopPred = Builder.CreatePHI(PredVTy, 2, "mismatch_vec_loop_pred");
  LoopPred->addIncoming(InitialPred, VectorLoopPreheaderBlock);
  PHINode *VectorIndexPhi = Builder.CreatePHI(I64Type, 2, "mismatch_vec_index");
  VectorIndexPhi->addIncoming(ExtStart, VectorLoopPreheaderBlock);
  Type *VectorLoadType =
      ScalableVectorType::get(Builder.getInt8Ty(), ByteCompareVF);
  Value *Passthru = ConstantInt::getNullValue(VectorLoadType);

  Value *VectorLhsGep =
      Builder.CreateGEP(LoadType, PtrA, VectorIndexPhi, "", GEPA->isInBounds());
  Value *VectorLhsLoad = Builder.CreateMaskedLoad(VectorLoadType, VectorLhsGep,
                                                  Align(1), LoopPred, Passthru);

  Value *VectorRhsGep =
      Builder.CreateGEP(LoadType, PtrB, VectorIndexPhi, "", GEPB->isInBounds());
  Value *VectorRhsLoad = Builder.CreateMaskedLoad(VectorLoadType, VectorRhsGep,
                                                  Align(1), LoopPred, Passthru);

  Value *VectorMatchCmp = Builder.CreateICmpNE(VectorLhsLoad, VectorRhsLoad);
  VectorMatchCmp = Builder.CreateSelect(LoopPred, VectorMatchCmp, PFalse);
  Value *VectorMatchHasActiveLanes = Builder.CreateOrReduce(VectorMatchCmp);
  BranchInst *VectorEarlyExit = BranchInst::Create(
      VectorLoopMismatchBlock, VectorLoopIncBlock, VectorMatchHasActiveLanes);
  Builder.Insert(VectorEarlyExit);

  DTU.applyUpdates(
      {{DominatorTree::Insert, VectorLoopStartBlock, VectorLoopMismatchBlock},
       {DominatorTree::Insert, VectorLoopStartBlock, VectorLoopIncBlock}});

  // Increment the index counter and calculate the predicate for the next
  // iteration of the loop. We branch back to the start of the loop if there
  // is at least one active lane.
  Builder.SetInsertPoint(VectorLoopIncBlock);
  Value *NewVectorIndexPhi =
      Builder.CreateAdd(VectorIndexPhi, VecLen, "",
                        /*HasNUW=*/true, /*HasNSW=*/true);
  VectorIndexPhi->addIncoming(NewVectorIndexPhi, VectorLoopIncBlock);
  Value *NewPred =
      Builder.CreateIntrinsic(Intrinsic::get_active_lane_mask,
                              {PredVTy, I64Type}, {NewVectorIndexPhi, ExtEnd});
  LoopPred->addIncoming(NewPred, VectorLoopIncBlock);

  Value *PredHasActiveLanes =
      Builder.CreateExtractElement(NewPred, uint64_t(0));
  BranchInst *VectorLoopBranchBack =
      BranchInst::Create(VectorLoopStartBlock, EndBlock, PredHasActiveLanes);
  Builder.Insert(VectorLoopBranchBack);

  DTU.applyUpdates(
      {{DominatorTree::Insert, VectorLoopIncBlock, VectorLoopStartBlock},
       {DominatorTree::Insert, VectorLoopIncBlock, EndBlock}});

  // If we found a mismatch then we need to calculate which lane in the vector
  // had a mismatch and add that on to the current loop index.
  Builder.SetInsertPoint(VectorLoopMismatchBlock);
  PHINode *FoundPred = Builder.CreatePHI(PredVTy, 1, "mismatch_vec_found_pred");
  FoundPred->addIncoming(VectorMatchCmp, VectorLoopStartBlock);
  PHINode *LastLoopPred =
      Builder.CreatePHI(PredVTy, 1, "mismatch_vec_last_loop_pred");
  LastLoopPred->addIncoming(LoopPred, VectorLoopStartBlock);
  PHINode *VectorFoundIndex =
      Builder.CreatePHI(I64Type, 1, "mismatch_vec_found_index");
  VectorFoundIndex->addIncoming(VectorIndexPhi, VectorLoopStartBlock);

  Value *PredMatchCmp = Builder.CreateAnd(LastLoopPred, FoundPred);
  Value *Ctz = Builder.CreateIntrinsic(
      Intrinsic::experimental_cttz_elts, {ResType, PredMatchCmp->getType()},
      {PredMatchCmp, /*ZeroIsPoison=*/Builder.getInt1(true)});
  Ctz = Builder.CreateZExt(Ctz, I64Type);
  Value *VectorLoopRes64 = Builder.CreateAdd(VectorFoundIndex, Ctz, "",
                                             /*HasNUW=*/true, /*HasNSW=*/true);
  return Builder.CreateTrunc(VectorLoopRes64, ResType);
}

Value *LoopIdiomVectorize::createPredicatedFindMismatch(
    IRBuilder<> &Builder, DomTreeUpdater &DTU, GetElementPtrInst *GEPA,
    GetElementPtrInst *GEPB, Value *ExtStart, Value *ExtEnd) {
  Type *I64Type = Builder.getInt64Ty();
  Type *I32Type = Builder.getInt32Ty();
  Type *ResType = I32Type;
  Type *LoadType = Builder.getInt8Ty();
  Value *PtrA = GEPA->getPointerOperand();
  Value *PtrB = GEPB->getPointerOperand();

  auto *JumpToVectorLoop = BranchInst::Create(VectorLoopStartBlock);
  Builder.Insert(JumpToVectorLoop);

  DTU.applyUpdates({{DominatorTree::Insert, VectorLoopPreheaderBlock,
                     VectorLoopStartBlock}});

  // Set up the first Vector loop block by creating the PHIs, doing the vector
  // loads and comparing the vectors.
  Builder.SetInsertPoint(VectorLoopStartBlock);
  auto *VectorIndexPhi = Builder.CreatePHI(I64Type, 2, "mismatch_vector_index");
  VectorIndexPhi->addIncoming(ExtStart, VectorLoopPreheaderBlock);

  // Calculate AVL by subtracting the vector loop index from the trip count
  Value *AVL = Builder.CreateSub(ExtEnd, VectorIndexPhi, "avl", /*HasNUW=*/true,
                                 /*HasNSW=*/true);

  auto *VectorLoadType = ScalableVectorType::get(LoadType, ByteCompareVF);
  auto *VF = ConstantInt::get(I32Type, ByteCompareVF);

  Value *VL = Builder.CreateIntrinsic(Intrinsic::experimental_get_vector_length,
                                      {I64Type}, {AVL, VF, Builder.getTrue()});
  Value *GepOffset = VectorIndexPhi;

  Value *VectorLhsGep =
      Builder.CreateGEP(LoadType, PtrA, GepOffset, "", GEPA->isInBounds());
  VectorType *TrueMaskTy =
      VectorType::get(Builder.getInt1Ty(), VectorLoadType->getElementCount());
  Value *AllTrueMask = Constant::getAllOnesValue(TrueMaskTy);
  Value *VectorLhsLoad = Builder.CreateIntrinsic(
      Intrinsic::vp_load, {VectorLoadType, VectorLhsGep->getType()},
      {VectorLhsGep, AllTrueMask, VL}, nullptr, "lhs.load");

  Value *VectorRhsGep =
      Builder.CreateGEP(LoadType, PtrB, GepOffset, "", GEPB->isInBounds());
  Value *VectorRhsLoad = Builder.CreateIntrinsic(
      Intrinsic::vp_load, {VectorLoadType, VectorLhsGep->getType()},
      {VectorRhsGep, AllTrueMask, VL}, nullptr, "rhs.load");

  StringRef PredicateStr = CmpInst::getPredicateName(CmpInst::ICMP_NE);
  auto *PredicateMDS = MDString::get(VectorLhsLoad->getContext(), PredicateStr);
  Value *Pred = MetadataAsValue::get(VectorLhsLoad->getContext(), PredicateMDS);
  Value *VectorMatchCmp = Builder.CreateIntrinsic(
      Intrinsic::vp_icmp, {VectorLhsLoad->getType()},
      {VectorLhsLoad, VectorRhsLoad, Pred, AllTrueMask, VL}, nullptr,
      "mismatch.cmp");
  Value *CTZ = Builder.CreateIntrinsic(
      Intrinsic::vp_cttz_elts, {ResType, VectorMatchCmp->getType()},
      {VectorMatchCmp, /*ZeroIsPoison=*/Builder.getInt1(false), AllTrueMask,
       VL});
  Value *MismatchFound = Builder.CreateICmpNE(CTZ, VL);
  auto *VectorEarlyExit = BranchInst::Create(VectorLoopMismatchBlock,
                                             VectorLoopIncBlock, MismatchFound);
  Builder.Insert(VectorEarlyExit);

  DTU.applyUpdates(
      {{DominatorTree::Insert, VectorLoopStartBlock, VectorLoopMismatchBlock},
       {DominatorTree::Insert, VectorLoopStartBlock, VectorLoopIncBlock}});

  // Increment the index counter and calculate the predicate for the next
  // iteration of the loop. We branch back to the start of the loop if there
  // is at least one active lane.
  Builder.SetInsertPoint(VectorLoopIncBlock);
  Value *VL64 = Builder.CreateZExt(VL, I64Type);
  Value *NewVectorIndexPhi =
      Builder.CreateAdd(VectorIndexPhi, VL64, "",
                        /*HasNUW=*/true, /*HasNSW=*/true);
  VectorIndexPhi->addIncoming(NewVectorIndexPhi, VectorLoopIncBlock);
  Value *ExitCond = Builder.CreateICmpNE(NewVectorIndexPhi, ExtEnd);
  auto *VectorLoopBranchBack =
      BranchInst::Create(VectorLoopStartBlock, EndBlock, ExitCond);
  Builder.Insert(VectorLoopBranchBack);

  DTU.applyUpdates(
      {{DominatorTree::Insert, VectorLoopIncBlock, VectorLoopStartBlock},
       {DominatorTree::Insert, VectorLoopIncBlock, EndBlock}});

  // If we found a mismatch then we need to calculate which lane in the vector
  // had a mismatch and add that on to the current loop index.
  Builder.SetInsertPoint(VectorLoopMismatchBlock);

  // Add LCSSA phis for CTZ and VectorIndexPhi.
  auto *CTZLCSSAPhi = Builder.CreatePHI(CTZ->getType(), 1, "ctz");
  CTZLCSSAPhi->addIncoming(CTZ, VectorLoopStartBlock);
  auto *VectorIndexLCSSAPhi =
      Builder.CreatePHI(VectorIndexPhi->getType(), 1, "mismatch_vector_index");
  VectorIndexLCSSAPhi->addIncoming(VectorIndexPhi, VectorLoopStartBlock);

  Value *CTZI64 = Builder.CreateZExt(CTZLCSSAPhi, I64Type);
  Value *VectorLoopRes64 = Builder.CreateAdd(VectorIndexLCSSAPhi, CTZI64, "",
                                             /*HasNUW=*/true, /*HasNSW=*/true);
  return Builder.CreateTrunc(VectorLoopRes64, ResType);
}

Value *LoopIdiomVectorize::expandFindMismatch(
    IRBuilder<> &Builder, DomTreeUpdater &DTU, GetElementPtrInst *GEPA,
    GetElementPtrInst *GEPB, Instruction *Index, Value *Start, Value *MaxLen) {
  Value *PtrA = GEPA->getPointerOperand();
  Value *PtrB = GEPB->getPointerOperand();

  // Get the arguments and types for the intrinsic.
  BasicBlock *Preheader = CurLoop->getLoopPreheader();
  BranchInst *PHBranch = cast<BranchInst>(Preheader->getTerminator());
  LLVMContext &Ctx = PHBranch->getContext();
  Type *LoadType = Type::getInt8Ty(Ctx);
  Type *ResType = Builder.getInt32Ty();

  // Split block in the original loop preheader.
  EndBlock = SplitBlock(Preheader, PHBranch, DT, LI, nullptr, "mismatch_end");

  // Create the blocks that we're going to need:
  //  1. A block for checking the zero-extended length exceeds 0
  //  2. A block to check that the start and end addresses of a given array
  //     lie on the same page.
  //  3. The vector loop preheader.
  //  4. The first vector loop block.
  //  5. The vector loop increment block.
  //  6. A block we can jump to from the vector loop when a mismatch is found.
  //  7. The first block of the scalar loop itself, containing PHIs , loads
  //  and cmp.
  //  8. A scalar loop increment block to increment the PHIs and go back
  //  around the loop.

  BasicBlock *MinItCheckBlock = BasicBlock::Create(
      Ctx, "mismatch_min_it_check", EndBlock->getParent(), EndBlock);

  // Update the terminator added by SplitBlock to branch to the first block
  Preheader->getTerminator()->setSuccessor(0, MinItCheckBlock);

  BasicBlock *MemCheckBlock = BasicBlock::Create(
      Ctx, "mismatch_mem_check", EndBlock->getParent(), EndBlock);

  VectorLoopPreheaderBlock = BasicBlock::Create(
      Ctx, "mismatch_vec_loop_preheader", EndBlock->getParent(), EndBlock);

  VectorLoopStartBlock = BasicBlock::Create(Ctx, "mismatch_vec_loop",
                                            EndBlock->getParent(), EndBlock);

  VectorLoopIncBlock = BasicBlock::Create(Ctx, "mismatch_vec_loop_inc",
                                          EndBlock->getParent(), EndBlock);

  VectorLoopMismatchBlock = BasicBlock::Create(Ctx, "mismatch_vec_loop_found",
                                               EndBlock->getParent(), EndBlock);

  BasicBlock *LoopPreHeaderBlock = BasicBlock::Create(
      Ctx, "mismatch_loop_pre", EndBlock->getParent(), EndBlock);

  BasicBlock *LoopStartBlock =
      BasicBlock::Create(Ctx, "mismatch_loop", EndBlock->getParent(), EndBlock);

  BasicBlock *LoopIncBlock = BasicBlock::Create(
      Ctx, "mismatch_loop_inc", EndBlock->getParent(), EndBlock);

  DTU.applyUpdates({{DominatorTree::Insert, Preheader, MinItCheckBlock},
                    {DominatorTree::Delete, Preheader, EndBlock}});

  // Update LoopInfo with the new vector & scalar loops.
  auto VectorLoop = LI->AllocateLoop();
  auto ScalarLoop = LI->AllocateLoop();

  if (CurLoop->getParentLoop()) {
    CurLoop->getParentLoop()->addBasicBlockToLoop(MinItCheckBlock, *LI);
    CurLoop->getParentLoop()->addBasicBlockToLoop(MemCheckBlock, *LI);
    CurLoop->getParentLoop()->addBasicBlockToLoop(VectorLoopPreheaderBlock,
                                                  *LI);
    CurLoop->getParentLoop()->addChildLoop(VectorLoop);
    CurLoop->getParentLoop()->addBasicBlockToLoop(VectorLoopMismatchBlock, *LI);
    CurLoop->getParentLoop()->addBasicBlockToLoop(LoopPreHeaderBlock, *LI);
    CurLoop->getParentLoop()->addChildLoop(ScalarLoop);
  } else {
    LI->addTopLevelLoop(VectorLoop);
    LI->addTopLevelLoop(ScalarLoop);
  }

  // Add the new basic blocks to their associated loops.
  VectorLoop->addBasicBlockToLoop(VectorLoopStartBlock, *LI);
  VectorLoop->addBasicBlockToLoop(VectorLoopIncBlock, *LI);

  ScalarLoop->addBasicBlockToLoop(LoopStartBlock, *LI);
  ScalarLoop->addBasicBlockToLoop(LoopIncBlock, *LI);

  // Set up some types and constants that we intend to reuse.
  Type *I64Type = Builder.getInt64Ty();

  // Check the zero-extended iteration count > 0
  Builder.SetInsertPoint(MinItCheckBlock);
  Value *ExtStart = Builder.CreateZExt(Start, I64Type);
  Value *ExtEnd = Builder.CreateZExt(MaxLen, I64Type);
  // This check doesn't really cost us very much.

  Value *LimitCheck = Builder.CreateICmpULE(Start, MaxLen);
  BranchInst *MinItCheckBr =
      BranchInst::Create(MemCheckBlock, LoopPreHeaderBlock, LimitCheck);
  MinItCheckBr->setMetadata(
      LLVMContext::MD_prof,
      MDBuilder(MinItCheckBr->getContext()).createBranchWeights(99, 1));
  Builder.Insert(MinItCheckBr);

  DTU.applyUpdates(
      {{DominatorTree::Insert, MinItCheckBlock, MemCheckBlock},
       {DominatorTree::Insert, MinItCheckBlock, LoopPreHeaderBlock}});

  // For each of the arrays, check the start/end addresses are on the same
  // page.
  Builder.SetInsertPoint(MemCheckBlock);

  // The early exit in the original loop means that when performing vector
  // loads we are potentially reading ahead of the early exit. So we could
  // fault if crossing a page boundary. Therefore, we create runtime memory
  // checks based on the minimum page size as follows:
  //   1. Calculate the addresses of the first memory accesses in the loop,
  //      i.e. LhsStart and RhsStart.
  //   2. Get the last accessed addresses in the loop, i.e. LhsEnd and RhsEnd.
  //   3. Determine which pages correspond to all the memory accesses, i.e
  //      LhsStartPage, LhsEndPage, RhsStartPage, RhsEndPage.
  //   4. If LhsStartPage == LhsEndPage and RhsStartPage == RhsEndPage, then
  //      we know we won't cross any page boundaries in the loop so we can
  //      enter the vector loop! Otherwise we fall back on the scalar loop.
  Value *LhsStartGEP = Builder.CreateGEP(LoadType, PtrA, ExtStart);
  Value *RhsStartGEP = Builder.CreateGEP(LoadType, PtrB, ExtStart);
  Value *RhsStart = Builder.CreatePtrToInt(RhsStartGEP, I64Type);
  Value *LhsStart = Builder.CreatePtrToInt(LhsStartGEP, I64Type);
  Value *LhsEndGEP = Builder.CreateGEP(LoadType, PtrA, ExtEnd);
  Value *RhsEndGEP = Builder.CreateGEP(LoadType, PtrB, ExtEnd);
  Value *LhsEnd = Builder.CreatePtrToInt(LhsEndGEP, I64Type);
  Value *RhsEnd = Builder.CreatePtrToInt(RhsEndGEP, I64Type);

  const uint64_t MinPageSize = TTI->getMinPageSize().value();
  const uint64_t AddrShiftAmt = llvm::Log2_64(MinPageSize);
  Value *LhsStartPage = Builder.CreateLShr(LhsStart, AddrShiftAmt);
  Value *LhsEndPage = Builder.CreateLShr(LhsEnd, AddrShiftAmt);
  Value *RhsStartPage = Builder.CreateLShr(RhsStart, AddrShiftAmt);
  Value *RhsEndPage = Builder.CreateLShr(RhsEnd, AddrShiftAmt);
  Value *LhsPageCmp = Builder.CreateICmpNE(LhsStartPage, LhsEndPage);
  Value *RhsPageCmp = Builder.CreateICmpNE(RhsStartPage, RhsEndPage);

  Value *CombinedPageCmp = Builder.CreateOr(LhsPageCmp, RhsPageCmp);
  BranchInst *CombinedPageCmpCmpBr = BranchInst::Create(
      LoopPreHeaderBlock, VectorLoopPreheaderBlock, CombinedPageCmp);
  CombinedPageCmpCmpBr->setMetadata(
      LLVMContext::MD_prof, MDBuilder(CombinedPageCmpCmpBr->getContext())
                                .createBranchWeights(10, 90));
  Builder.Insert(CombinedPageCmpCmpBr);

  DTU.applyUpdates(
      {{DominatorTree::Insert, MemCheckBlock, LoopPreHeaderBlock},
       {DominatorTree::Insert, MemCheckBlock, VectorLoopPreheaderBlock}});

  // Set up the vector loop preheader, i.e. calculate initial loop predicate,
  // zero-extend MaxLen to 64-bits, determine the number of vector elements
  // processed in each iteration, etc.
  Builder.SetInsertPoint(VectorLoopPreheaderBlock);

  // At this point we know two things must be true:
  //  1. Start <= End
  //  2. ExtMaxLen <= MinPageSize due to the page checks.
  // Therefore, we know that we can use a 64-bit induction variable that
  // starts from 0 -> ExtMaxLen and it will not overflow.
  Value *VectorLoopRes = nullptr;
  switch (VectorizeStyle) {
  case LoopIdiomVectorizeStyle::Masked:
    VectorLoopRes =
        createMaskedFindMismatch(Builder, DTU, GEPA, GEPB, ExtStart, ExtEnd);
    break;
  case LoopIdiomVectorizeStyle::Predicated:
    VectorLoopRes = createPredicatedFindMismatch(Builder, DTU, GEPA, GEPB,
                                                 ExtStart, ExtEnd);
    break;
  }

  Builder.Insert(BranchInst::Create(EndBlock));

  DTU.applyUpdates(
      {{DominatorTree::Insert, VectorLoopMismatchBlock, EndBlock}});

  // Generate code for scalar loop.
  Builder.SetInsertPoint(LoopPreHeaderBlock);
  Builder.Insert(BranchInst::Create(LoopStartBlock));

  DTU.applyUpdates(
      {{DominatorTree::Insert, LoopPreHeaderBlock, LoopStartBlock}});

  Builder.SetInsertPoint(LoopStartBlock);
  PHINode *IndexPhi = Builder.CreatePHI(ResType, 2, "mismatch_index");
  IndexPhi->addIncoming(Start, LoopPreHeaderBlock);

  // Otherwise compare the values
  // Load bytes from each array and compare them.
  Value *GepOffset = Builder.CreateZExt(IndexPhi, I64Type);

  Value *LhsGep =
      Builder.CreateGEP(LoadType, PtrA, GepOffset, "", GEPA->isInBounds());
  Value *LhsLoad = Builder.CreateLoad(LoadType, LhsGep);

  Value *RhsGep =
      Builder.CreateGEP(LoadType, PtrB, GepOffset, "", GEPB->isInBounds());
  Value *RhsLoad = Builder.CreateLoad(LoadType, RhsGep);

  Value *MatchCmp = Builder.CreateICmpEQ(LhsLoad, RhsLoad);
  // If we have a mismatch then exit the loop ...
  BranchInst *MatchCmpBr = BranchInst::Create(LoopIncBlock, EndBlock, MatchCmp);
  Builder.Insert(MatchCmpBr);

  DTU.applyUpdates({{DominatorTree::Insert, LoopStartBlock, LoopIncBlock},
                    {DominatorTree::Insert, LoopStartBlock, EndBlock}});

  // Have we reached the maximum permitted length for the loop?
  Builder.SetInsertPoint(LoopIncBlock);
  Value *PhiInc = Builder.CreateAdd(IndexPhi, ConstantInt::get(ResType, 1), "",
                                    /*HasNUW=*/Index->hasNoUnsignedWrap(),
                                    /*HasNSW=*/Index->hasNoSignedWrap());
  IndexPhi->addIncoming(PhiInc, LoopIncBlock);
  Value *IVCmp = Builder.CreateICmpEQ(PhiInc, MaxLen);
  BranchInst *IVCmpBr = BranchInst::Create(EndBlock, LoopStartBlock, IVCmp);
  Builder.Insert(IVCmpBr);

  DTU.applyUpdates({{DominatorTree::Insert, LoopIncBlock, EndBlock},
                    {DominatorTree::Insert, LoopIncBlock, LoopStartBlock}});

  // In the end block we need to insert a PHI node to deal with three cases:
  //  1. We didn't find a mismatch in the scalar loop, so we return MaxLen.
  //  2. We exitted the scalar loop early due to a mismatch and need to return
  //  the index that we found.
  //  3. We didn't find a mismatch in the vector loop, so we return MaxLen.
  //  4. We exitted the vector loop early due to a mismatch and need to return
  //  the index that we found.
  Builder.SetInsertPoint(EndBlock, EndBlock->getFirstInsertionPt());
  PHINode *ResPhi = Builder.CreatePHI(ResType, 4, "mismatch_result");
  ResPhi->addIncoming(MaxLen, LoopIncBlock);
  ResPhi->addIncoming(IndexPhi, LoopStartBlock);
  ResPhi->addIncoming(MaxLen, VectorLoopIncBlock);
  ResPhi->addIncoming(VectorLoopRes, VectorLoopMismatchBlock);

  Value *FinalRes = Builder.CreateTrunc(ResPhi, ResType);

  if (VerifyLoops) {
    ScalarLoop->verifyLoop();
    VectorLoop->verifyLoop();
    if (!VectorLoop->isRecursivelyLCSSAForm(*DT, *LI))
      report_fatal_error("Loops must remain in LCSSA form!");
    if (!ScalarLoop->isRecursivelyLCSSAForm(*DT, *LI))
      report_fatal_error("Loops must remain in LCSSA form!");
  }

  return FinalRes;
}

void LoopIdiomVectorize::transformByteCompare(GetElementPtrInst *GEPA,
                                              GetElementPtrInst *GEPB,
                                              PHINode *IndPhi, Value *MaxLen,
                                              Instruction *Index, Value *Start,
                                              bool IncIdx, BasicBlock *FoundBB,
                                              BasicBlock *EndBB) {

  // Insert the byte compare code at the end of the preheader block
  BasicBlock *Preheader = CurLoop->getLoopPreheader();
  BasicBlock *Header = CurLoop->getHeader();
  BranchInst *PHBranch = cast<BranchInst>(Preheader->getTerminator());
  IRBuilder<> Builder(PHBranch);
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  Builder.SetCurrentDebugLocation(PHBranch->getDebugLoc());

  // Increment the pointer if this was done before the loads in the loop.
  if (IncIdx)
    Start = Builder.CreateAdd(Start, ConstantInt::get(Start->getType(), 1));

  Value *ByteCmpRes =
      expandFindMismatch(Builder, DTU, GEPA, GEPB, Index, Start, MaxLen);

  // Replaces uses of index & induction Phi with intrinsic (we already
  // checked that the the first instruction of Header is the Phi above).
  assert(IndPhi->hasOneUse() && "Index phi node has more than one use!");
  Index->replaceAllUsesWith(ByteCmpRes);

  assert(PHBranch->isUnconditional() &&
         "Expected preheader to terminate with an unconditional branch.");

  // If no mismatch was found, we can jump to the end block. Create a
  // new basic block for the compare instruction.
  auto *CmpBB = BasicBlock::Create(Preheader->getContext(), "byte.compare",
                                   Preheader->getParent());
  CmpBB->moveBefore(EndBB);

  // Replace the branch in the preheader with an always-true conditional branch.
  // This ensures there is still a reference to the original loop.
  Builder.CreateCondBr(Builder.getTrue(), CmpBB, Header);
  PHBranch->eraseFromParent();

  BasicBlock *MismatchEnd = cast<Instruction>(ByteCmpRes)->getParent();
  DTU.applyUpdates({{DominatorTree::Insert, MismatchEnd, CmpBB}});

  // Create the branch to either the end or found block depending on the value
  // returned by the intrinsic.
  Builder.SetInsertPoint(CmpBB);
  if (FoundBB != EndBB) {
    Value *FoundCmp = Builder.CreateICmpEQ(ByteCmpRes, MaxLen);
    Builder.CreateCondBr(FoundCmp, EndBB, FoundBB);
    DTU.applyUpdates({{DominatorTree::Insert, CmpBB, FoundBB},
                      {DominatorTree::Insert, CmpBB, EndBB}});

  } else {
    Builder.CreateBr(FoundBB);
    DTU.applyUpdates({{DominatorTree::Insert, CmpBB, FoundBB}});
  }

  auto fixSuccessorPhis = [&](BasicBlock *SuccBB) {
    for (PHINode &PN : SuccBB->phis()) {
      // At this point we've already replaced all uses of the result from the
      // loop with ByteCmp. Look through the incoming values to find ByteCmp,
      // meaning this is a Phi collecting the results of the byte compare.
      bool ResPhi = false;
      for (Value *Op : PN.incoming_values())
        if (Op == ByteCmpRes) {
          ResPhi = true;
          break;
        }

      // Any PHI that depended upon the result of the byte compare needs a new
      // incoming value from CmpBB. This is because the original loop will get
      // deleted.
      if (ResPhi)
        PN.addIncoming(ByteCmpRes, CmpBB);
      else {
        // There should be no other outside uses of other values in the
        // original loop. Any incoming values should either:
        //   1. Be for blocks outside the loop, which aren't interesting. Or ..
        //   2. These are from blocks in the loop with values defined outside
        //      the loop. We should a similar incoming value from CmpBB.
        for (BasicBlock *BB : PN.blocks())
          if (CurLoop->contains(BB)) {
            PN.addIncoming(PN.getIncomingValueForBlock(BB), CmpBB);
            break;
          }
      }
    }
  };

  // Ensure all Phis in the successors of CmpBB have an incoming value from it.
  fixSuccessorPhis(EndBB);
  if (EndBB != FoundBB)
    fixSuccessorPhis(FoundBB);

  // The new CmpBB block isn't part of the loop, but will need to be added to
  // the outer loop if there is one.
  if (!CurLoop->isOutermost())
    CurLoop->getParentLoop()->addBasicBlockToLoop(CmpBB, *LI);

  if (VerifyLoops && CurLoop->getParentLoop()) {
    CurLoop->getParentLoop()->verifyLoop();
    if (!CurLoop->getParentLoop()->isRecursivelyLCSSAForm(*DT, *LI))
      report_fatal_error("Loops must remain in LCSSA form!");
  }
}
