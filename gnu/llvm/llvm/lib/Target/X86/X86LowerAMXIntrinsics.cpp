//===-- X86LowerAMXIntrinsics.cpp -X86 Scalarize AMX Intrinsics------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Pass to transform amx intrinsics to scalar operations.
/// This pass is always enabled and it skips when it is not -O0 and has no
/// optnone attributes. With -O0 or optnone attribute, the def of shape to amx
/// intrinsics is near the amx intrinsics code. We are not able to find a
/// point which post-dominate all the shape and dominate all amx intrinsics.
/// To decouple the dependency of the shape, we transform amx intrinsics
/// to scalar operation, so that compiling doesn't fail. In long term, we
/// should improve fast register allocation to allocate amx register.
//===----------------------------------------------------------------------===//
//
#include "X86.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "lower-amx-intrinsics"

#ifndef NDEBUG
static bool isV256I32Ty(Type *Ty) {
  if (auto *FVT = dyn_cast<FixedVectorType>(Ty))
    return FVT->getNumElements() == 256 &&
           FVT->getElementType()->isIntegerTy(32);
  return false;
}
#endif

static cl::opt<bool>
    X86ScalarizeAMX("enable-x86-scalar-amx", cl::init(false), cl::Hidden,
                    cl::desc("X86: enable AMX scalarizition."));

namespace {
class X86LowerAMXIntrinsics {
  Function &Func;

public:
  X86LowerAMXIntrinsics(Function &F, DomTreeUpdater &DomTU, LoopInfo *LoopI)
      : Func(F), DTU(DomTU), LI(LoopI) {}
  bool visit();

private:
  DomTreeUpdater &DTU;
  LoopInfo *LI;
  BasicBlock *createLoop(BasicBlock *Preheader, BasicBlock *Exit, Value *Bound,
                         Value *Step, StringRef Name, IRBuilderBase &B,
                         Loop *L);
  template <bool IsTileLoad>
  Value *createTileLoadStoreLoops(BasicBlock *Start, BasicBlock *End,
                                  IRBuilderBase &B, Value *Row, Value *Col,
                                  Value *Ptr, Value *Stride, Value *Tile);
  template <Intrinsic::ID IntrID>
  std::enable_if_t<IntrID == Intrinsic::x86_tdpbssd_internal ||
                       IntrID == Intrinsic::x86_tdpbsud_internal ||
                       IntrID == Intrinsic::x86_tdpbusd_internal ||
                       IntrID == Intrinsic::x86_tdpbuud_internal ||
                       IntrID == Intrinsic::x86_tdpbf16ps_internal,
                   Value *>
  createTileDPLoops(BasicBlock *Start, BasicBlock *End, IRBuilderBase &B,
                    Value *Row, Value *Col, Value *K, Value *Acc, Value *LHS,
                    Value *RHS);
  template <bool IsTileLoad>
  bool lowerTileLoadStore(Instruction *TileLoadStore);
  template <Intrinsic::ID IntrID>
  std::enable_if_t<IntrID == Intrinsic::x86_tdpbssd_internal ||
                       IntrID == Intrinsic::x86_tdpbsud_internal ||
                       IntrID == Intrinsic::x86_tdpbusd_internal ||
                       IntrID == Intrinsic::x86_tdpbuud_internal ||
                       IntrID == Intrinsic::x86_tdpbf16ps_internal,
                   bool>
  lowerTileDP(Instruction *TileDP);
  bool lowerTileZero(Instruction *TileZero);
};
} // anonymous namespace

BasicBlock *X86LowerAMXIntrinsics::createLoop(BasicBlock *Preheader,
                                              BasicBlock *Exit, Value *Bound,
                                              Value *Step, StringRef Name,
                                              IRBuilderBase &B, Loop *L) {
  LLVMContext &Ctx = Preheader->getContext();
  BasicBlock *Header =
      BasicBlock::Create(Ctx, Name + ".header", Preheader->getParent(), Exit);
  BasicBlock *Body =
      BasicBlock::Create(Ctx, Name + ".body", Header->getParent(), Exit);
  BasicBlock *Latch =
      BasicBlock::Create(Ctx, Name + ".latch", Header->getParent(), Exit);

  Type *I16Ty = Type::getInt16Ty(Ctx);
  BranchInst::Create(Body, Header);
  BranchInst::Create(Latch, Body);
  PHINode *IV =
      PHINode::Create(I16Ty, 2, Name + ".iv", Header->getTerminator()->getIterator());
  IV->addIncoming(ConstantInt::get(I16Ty, 0), Preheader);

  B.SetInsertPoint(Latch);
  Value *Inc = B.CreateAdd(IV, Step, Name + ".step");
  Value *Cond = B.CreateICmpNE(Inc, Bound, Name + ".cond");
  BranchInst::Create(Header, Exit, Cond, Latch);
  IV->addIncoming(Inc, Latch);

  BranchInst *PreheaderBr = cast<BranchInst>(Preheader->getTerminator());
  BasicBlock *Tmp = PreheaderBr->getSuccessor(0);
  PreheaderBr->setSuccessor(0, Header);
  DTU.applyUpdatesPermissive({
      {DominatorTree::Delete, Preheader, Tmp},
      {DominatorTree::Insert, Header, Body},
      {DominatorTree::Insert, Body, Latch},
      {DominatorTree::Insert, Latch, Header},
      {DominatorTree::Insert, Latch, Exit},
      {DominatorTree::Insert, Preheader, Header},
  });
  if (LI) {
    L->addBasicBlockToLoop(Header, *LI);
    L->addBasicBlockToLoop(Body, *LI);
    L->addBasicBlockToLoop(Latch, *LI);
  }
  return Body;
}

template <bool IsTileLoad>
Value *X86LowerAMXIntrinsics::createTileLoadStoreLoops(
    BasicBlock *Start, BasicBlock *End, IRBuilderBase &B, Value *Row,
    Value *Col, Value *Ptr, Value *Stride, Value *Tile) {
  std::string IntrinName = IsTileLoad ? "tileload" : "tilestore";
  Loop *RowLoop = nullptr;
  Loop *ColLoop = nullptr;
  if (LI) {
    RowLoop = LI->AllocateLoop();
    ColLoop = LI->AllocateLoop();
    RowLoop->addChildLoop(ColLoop);
    if (Loop *ParentL = LI->getLoopFor(Start))
      ParentL->addChildLoop(RowLoop);
    else
      LI->addTopLevelLoop(RowLoop);
  }

  BasicBlock *RowBody = createLoop(Start, End, Row, B.getInt16(1),
                                   IntrinName + ".scalarize.rows", B, RowLoop);
  BasicBlock *RowLatch = RowBody->getSingleSuccessor();

  BasicBlock *ColBody = createLoop(RowBody, RowLatch, Col, B.getInt16(1),
                                   IntrinName + ".scalarize.cols", B, ColLoop);

  BasicBlock *ColLoopLatch = ColBody->getSingleSuccessor();
  BasicBlock *ColLoopHeader = ColBody->getSinglePredecessor();
  BasicBlock *RowLoopHeader = RowBody->getSinglePredecessor();
  Value *CurrentRow = &*RowLoopHeader->begin();
  Value *CurrentCol = &*ColLoopHeader->begin();
  Type *EltTy = B.getInt32Ty();
  FixedVectorType *V256I32Ty = FixedVectorType::get(EltTy, 256);

  // Common part for tileload and tilestore
  // *.scalarize.cols.body:
  // Calculate %idxmem and %idxvec
  B.SetInsertPoint(ColBody->getTerminator());
  Value *CurrentRowZExt = B.CreateZExt(CurrentRow, Stride->getType());
  Value *CurrentColZExt = B.CreateZExt(CurrentCol, Stride->getType());
  Value *Offset =
      B.CreateAdd(B.CreateMul(CurrentRowZExt, Stride), CurrentColZExt);
  Value *EltPtr = B.CreateGEP(EltTy, Ptr, Offset);
  Value *Idx = B.CreateAdd(B.CreateMul(CurrentRow, B.getInt16(16)), CurrentCol);
  if (IsTileLoad) {
    // tileload.scalarize.rows.header:
    // %vec.phi.row = phi <256 x i32> [ zeroinitializer, %entry ], [ %ResVec,
    // %tileload.scalarize.rows.latch ]
    B.SetInsertPoint(RowLoopHeader->getTerminator());
    Value *VecZero = Constant::getNullValue(V256I32Ty);
    PHINode *VecCPhiRowLoop = B.CreatePHI(V256I32Ty, 2, "vec.phi.row");
    VecCPhiRowLoop->addIncoming(VecZero, Start);

    // tileload.scalarize.cols.header:
    // %vec.phi = phi <256 x i32> [ %vec.phi.row, %tileload.scalarize.rows.body
    // ], [ %ResVec, %tileload.scalarize.cols.latch ]
    B.SetInsertPoint(ColLoopHeader->getTerminator());
    PHINode *VecPhi = B.CreatePHI(V256I32Ty, 2, "vec.phi");
    VecPhi->addIncoming(VecCPhiRowLoop, RowBody);

    // tileload.scalarize.cols.body:
    // Calculate %idxmem and %idxvec
    // %eltptr = getelementptr i32, i32* %base, i64 %idxmem
    // %elt = load i32, i32* %ptr
    // %ResVec = insertelement <256 x i32> %vec.phi, i32 %elt, i16 %idxvec
    B.SetInsertPoint(ColBody->getTerminator());
    Value *Elt = B.CreateLoad(EltTy, EltPtr);
    Value *ResVec = B.CreateInsertElement(VecPhi, Elt, Idx);
    VecPhi->addIncoming(ResVec, ColLoopLatch);
    VecCPhiRowLoop->addIncoming(ResVec, RowLatch);

    return ResVec;
  } else {
    auto *BitCast = cast<BitCastInst>(Tile);
    Value *Vec = BitCast->getOperand(0);
    assert(isV256I32Ty(Vec->getType()) && "bitcast from non-v256i32 to x86amx");
    // tilestore.scalarize.cols.body:
    // %mul = mul i16 %row.iv, i16 16
    // %idx = add i16 %mul, i16 %col.iv
    // %vec = extractelement <16 x i32> %vec, i16 %idx
    // store i32 %vec, i32* %ptr
    B.SetInsertPoint(ColBody->getTerminator());
    Value *Elt = B.CreateExtractElement(Vec, Idx);

    B.CreateStore(Elt, EltPtr);
    return nullptr;
  }
}

template <Intrinsic::ID IntrID>
std::enable_if_t<IntrID == Intrinsic::x86_tdpbssd_internal ||
                     IntrID == Intrinsic::x86_tdpbsud_internal ||
                     IntrID == Intrinsic::x86_tdpbusd_internal ||
                     IntrID == Intrinsic::x86_tdpbuud_internal ||
                     IntrID == Intrinsic::x86_tdpbf16ps_internal,
                 Value *>
X86LowerAMXIntrinsics::createTileDPLoops(BasicBlock *Start, BasicBlock *End,
                                         IRBuilderBase &B, Value *Row,
                                         Value *Col, Value *K, Value *Acc,
                                         Value *LHS, Value *RHS) {
  std::string IntrinName;
  switch (IntrID) {
  case Intrinsic::x86_tdpbssd_internal:
    IntrinName = "tiledpbssd";
    break;
  case Intrinsic::x86_tdpbsud_internal:
    IntrinName = "tiledpbsud";
    break;
  case Intrinsic::x86_tdpbusd_internal:
    IntrinName = "tiledpbusd";
    break;
  case Intrinsic::x86_tdpbuud_internal:
    IntrinName = "tiledpbuud";
    break;
  case Intrinsic::x86_tdpbf16ps_internal:
    IntrinName = "tiledpbf16ps";
    break;
  }
  Loop *RowLoop = nullptr;
  Loop *ColLoop = nullptr;
  Loop *InnerLoop = nullptr;
  if (LI) {
    RowLoop = LI->AllocateLoop();
    ColLoop = LI->AllocateLoop();
    InnerLoop = LI->AllocateLoop();
    ColLoop->addChildLoop(InnerLoop);
    RowLoop->addChildLoop(ColLoop);
    if (Loop *ParentL = LI->getLoopFor(Start))
      ParentL->addChildLoop(RowLoop);
    else
      LI->addTopLevelLoop(RowLoop);
  }

  BasicBlock *RowBody = createLoop(Start, End, Row, B.getInt16(1),
                                   IntrinName + ".scalarize.rows", B, RowLoop);
  BasicBlock *RowLatch = RowBody->getSingleSuccessor();

  BasicBlock *ColBody = createLoop(RowBody, RowLatch, Col, B.getInt16(1),
                                   IntrinName + ".scalarize.cols", B, ColLoop);

  BasicBlock *ColLoopLatch = ColBody->getSingleSuccessor();

  B.SetInsertPoint(ColBody->getTerminator());
  BasicBlock *InnerBody =
      createLoop(ColBody, ColLoopLatch, K, B.getInt16(1),
                 IntrinName + ".scalarize.inner", B, InnerLoop);

  BasicBlock *ColLoopHeader = ColBody->getSinglePredecessor();
  BasicBlock *RowLoopHeader = RowBody->getSinglePredecessor();
  BasicBlock *InnerLoopHeader = InnerBody->getSinglePredecessor();
  BasicBlock *InnerLoopLatch = InnerBody->getSingleSuccessor();
  Value *CurrentRow = &*RowLoopHeader->begin();
  Value *CurrentCol = &*ColLoopHeader->begin();
  Value *CurrentInner = &*InnerLoopHeader->begin();

  FixedVectorType *V256I32Ty = FixedVectorType::get(B.getInt32Ty(), 256);
  auto *BitCastAcc = cast<BitCastInst>(Acc);
  Value *VecC = BitCastAcc->getOperand(0);
  assert(isV256I32Ty(VecC->getType()) && "bitcast from non-v256i32 to x86amx");
  // TODO else create BitCast from x86amx to v256i32.
  // Store x86amx to memory, and reload from memory
  // to vector. However with -O0, it doesn't happen.
  auto *BitCastLHS = cast<BitCastInst>(LHS);
  Value *VecA = BitCastLHS->getOperand(0);
  assert(isV256I32Ty(VecA->getType()) && "bitcast from non-v256i32 to x86amx");
  auto *BitCastRHS = cast<BitCastInst>(RHS);
  Value *VecB = BitCastRHS->getOperand(0);
  assert(isV256I32Ty(VecB->getType()) && "bitcast from non-v256i32 to x86amx");

  // tiledpbssd.scalarize.rows.header:
  // %vec.c.phi.row = phi <256 x i32> [ %VecC, %continue ], [ %NewVecC,
  // %tiledpbssd.scalarize.rows.latch ]

  // %vec.d.phi.row = phi <256 x i32> [ zeroinitializer, %continue ], [
  // %NewVecD, %tiledpbssd.scalarize.rows.latch ]
  B.SetInsertPoint(RowLoopHeader->getTerminator());
  PHINode *VecCPhiRowLoop = B.CreatePHI(V256I32Ty, 2, "vec.c.phi.row");
  VecCPhiRowLoop->addIncoming(VecC, Start);
  Value *VecZero = Constant::getNullValue(V256I32Ty);
  PHINode *VecDPhiRowLoop = B.CreatePHI(V256I32Ty, 2, "vec.d.phi.row");
  VecDPhiRowLoop->addIncoming(VecZero, Start);

  // tiledpbssd.scalarize.cols.header:
  // %vec.c.phi.col = phi <256 x i32> [ %vec.c.phi.row,
  // %tiledpbssd.scalarize.rows.body ], [ %NewVecC,
  // %tiledpbssd.scalarize.cols.latch ]

  // %vec.d.phi.col = phi <256 x i32> [
  // %vec.d.phi.row, %tiledpbssd.scalarize.rows.body ], [ %NewVecD,
  // %tiledpbssd.scalarize.cols.latch ]

  // calculate idxc.
  B.SetInsertPoint(ColLoopHeader->getTerminator());
  PHINode *VecCPhiColLoop = B.CreatePHI(V256I32Ty, 2, "vec.c.phi.col");
  VecCPhiColLoop->addIncoming(VecCPhiRowLoop, RowBody);
  PHINode *VecDPhiColLoop = B.CreatePHI(V256I32Ty, 2, "vec.d.phi.col");
  VecDPhiColLoop->addIncoming(VecDPhiRowLoop, RowBody);
  Value *IdxC =
      B.CreateAdd(B.CreateMul(CurrentRow, B.getInt16(16)), CurrentCol);

  // tiledpbssd.scalarize.inner.header:
  // %vec.c.inner.phi = phi <256 x i32> [ %vec.c.phi.col,
  // %tiledpbssd.scalarize.cols.body ], [ %NewVecC,
  // %tiledpbssd.scalarize.inner.latch ]

  B.SetInsertPoint(InnerLoopHeader->getTerminator());
  PHINode *VecCPhi = B.CreatePHI(V256I32Ty, 2, "vec.c.inner.phi");
  VecCPhi->addIncoming(VecCPhiColLoop, ColBody);

  B.SetInsertPoint(InnerBody->getTerminator());
  Value *IdxA =
      B.CreateAdd(B.CreateMul(CurrentRow, B.getInt16(16)), CurrentInner);
  Value *IdxB =
      B.CreateAdd(B.CreateMul(CurrentInner, B.getInt16(16)), CurrentCol);
  Value *NewVecC = nullptr;

  if (IntrID != Intrinsic::x86_tdpbf16ps_internal) {
    // tiledpbssd.scalarize.inner.body:
    // calculate idxa, idxb
    // %eltc = extractelement <256 x i32> %vec.c.inner.phi, i16 %idxc
    // %elta = extractelement <256 x i32> %veca, i16 %idxa
    // %eltav4i8 = bitcast i32 %elta to <4 x i8>
    // %eltb = extractelement <256 x i32> %vecb, i16 %idxb
    // %eltbv4i8 = bitcast i32 %eltb to <4 x i8>
    // %eltav4i32 = sext <4 x i8> %eltav4i8 to <4 x i32>
    // %eltbv4i32 = sext <4 x i8> %eltbv4i8 to <4 x i32>
    // %mulab = mul <4 x i32> %eltbv4i32, %eltav4i32
    // %acc = call i32 @llvm.vector.reduce.add.v4i32(<4 x i32> %131)
    // %neweltc = add i32 %elt, %acc
    // %NewVecC = insertelement <256 x i32> %vec.c.inner.phi, i32 %neweltc,
    // i16 %idxc
    FixedVectorType *V4I8Ty = FixedVectorType::get(B.getInt8Ty(), 4);
    FixedVectorType *V4I32Ty = FixedVectorType::get(B.getInt32Ty(), 4);
    Value *EltC = B.CreateExtractElement(VecCPhi, IdxC);
    Value *EltA = B.CreateExtractElement(VecA, IdxA);
    Value *SubVecA = B.CreateBitCast(EltA, V4I8Ty);
    Value *EltB = B.CreateExtractElement(VecB, IdxB);
    Value *SubVecB = B.CreateBitCast(EltB, V4I8Ty);
    Value *SEXTSubVecB = nullptr;
    Value *SEXTSubVecA = nullptr;
    switch (IntrID) {
    case Intrinsic::x86_tdpbssd_internal:
      SEXTSubVecB = B.CreateSExt(SubVecB, V4I32Ty);
      SEXTSubVecA = B.CreateSExt(SubVecA, V4I32Ty);
      break;
    case Intrinsic::x86_tdpbsud_internal:
      SEXTSubVecB = B.CreateZExt(SubVecB, V4I32Ty);
      SEXTSubVecA = B.CreateSExt(SubVecA, V4I32Ty);
      break;
    case Intrinsic::x86_tdpbusd_internal:
      SEXTSubVecB = B.CreateSExt(SubVecB, V4I32Ty);
      SEXTSubVecA = B.CreateZExt(SubVecA, V4I32Ty);
      break;
    case Intrinsic::x86_tdpbuud_internal:
      SEXTSubVecB = B.CreateZExt(SubVecB, V4I32Ty);
      SEXTSubVecA = B.CreateZExt(SubVecA, V4I32Ty);
      break;
    default:
      llvm_unreachable("Invalid intrinsic ID!");
    }
    Value *SubVecR = B.CreateAddReduce(B.CreateMul(SEXTSubVecA, SEXTSubVecB));
    Value *ResElt = B.CreateAdd(EltC, SubVecR);
    NewVecC = B.CreateInsertElement(VecCPhi, ResElt, IdxC);
  } else {
    // tiledpbf16ps.scalarize.inner.body:
    // calculate idxa, idxb, idxc
    // %eltc = extractelement <256 x i32> %vec.c.inner.phi, i16 %idxc
    // %eltcf32 = bitcast i32 %eltc to float
    // %elta = extractelement <256 x i32> %veca, i16 %idxa
    // %eltav2i16 = bitcast i32 %elta to <2 x i16>
    // %eltb = extractelement <256 x i32> %vecb, i16 %idxb
    // %eltbv2i16 = bitcast i32 %eltb to <2 x i16>
    // %shufflea = shufflevector <2 x i16> %elta, <2 x i16> zeroinitializer, <4
    // x i32> <i32 2, i32 0, i32 3, i32 1>
    // %eltav2f32 = bitcast <4 x i16> %shufflea to <2 x float>
    // %shuffleb = shufflevector <2 x i16> %eltb, <2 xi16> zeroinitializer, <4 x
    // i32> <i32 2, i32 0, i32 3, i32 1>
    // %eltbv2f32 = bitcast <4 x i16> %shuffleb to <2 x float>
    // %mulab = fmul <2 x float> %eltav2f32, %eltbv2f32
    // %acc = call float
    // @llvm.vector.reduce.fadd.v2f32(float %eltcf32, <2 x float> %mulab)
    // %neweltc = bitcast float %acc to i32
    // %NewVecC = insertelement <256 x i32> %vec.c.inner.phi, i32 %neweltc,
    // i16 %idxc
    // %NewVecD = insertelement <256 x i32> %vec.d.inner.phi, i32 %neweltc,
    // i16 %idxc
    FixedVectorType *V2I16Ty = FixedVectorType::get(B.getInt16Ty(), 2);
    FixedVectorType *V2F32Ty = FixedVectorType::get(B.getFloatTy(), 2);
    Value *EltC = B.CreateExtractElement(VecCPhi, IdxC);
    Value *EltCF32 = B.CreateBitCast(EltC, B.getFloatTy());
    Value *EltA = B.CreateExtractElement(VecA, IdxA);
    Value *SubVecA = B.CreateBitCast(EltA, V2I16Ty);
    Value *EltB = B.CreateExtractElement(VecB, IdxB);
    Value *SubVecB = B.CreateBitCast(EltB, V2I16Ty);
    Value *ZeroV2I16 = Constant::getNullValue(V2I16Ty);
    int ShuffleMask[4] = {2, 0, 3, 1};
    auto ShuffleArray = ArrayRef(ShuffleMask);
    Value *AV2F32 = B.CreateBitCast(
        B.CreateShuffleVector(SubVecA, ZeroV2I16, ShuffleArray), V2F32Ty);
    Value *BV2F32 = B.CreateBitCast(
        B.CreateShuffleVector(SubVecB, ZeroV2I16, ShuffleArray), V2F32Ty);
    Value *SubVecR = B.CreateFAddReduce(EltCF32, B.CreateFMul(AV2F32, BV2F32));
    Value *ResElt = B.CreateBitCast(SubVecR, B.getInt32Ty());
    NewVecC = B.CreateInsertElement(VecCPhi, ResElt, IdxC);
  }

  // tiledpbssd.scalarize.cols.latch:
  // %NewEltC = extractelement <256 x i32> %vec.c.phi.col, i16 %idxc
  // %NewVecD = insertelement <256 x i32> %vec.d.phi.col, i32 %NewEltC,
  // i16 %idxc
  B.SetInsertPoint(ColLoopLatch->getTerminator());
  Value *NewEltC = B.CreateExtractElement(NewVecC, IdxC);
  Value *NewVecD = B.CreateInsertElement(VecDPhiColLoop, NewEltC, IdxC);

  VecCPhi->addIncoming(NewVecC, InnerLoopLatch);
  VecCPhiRowLoop->addIncoming(NewVecC, RowLatch);
  VecCPhiColLoop->addIncoming(NewVecC, ColLoopLatch);
  VecDPhiRowLoop->addIncoming(NewVecD, RowLatch);
  VecDPhiColLoop->addIncoming(NewVecD, ColLoopLatch);

  return NewVecD;
}

template <Intrinsic::ID IntrID>
std::enable_if_t<IntrID == Intrinsic::x86_tdpbssd_internal ||
                     IntrID == Intrinsic::x86_tdpbsud_internal ||
                     IntrID == Intrinsic::x86_tdpbusd_internal ||
                     IntrID == Intrinsic::x86_tdpbuud_internal ||
                     IntrID == Intrinsic::x86_tdpbf16ps_internal,
                 bool>
X86LowerAMXIntrinsics::lowerTileDP(Instruction *TileDP) {
  Value *M, *N, *K, *C, *A, *B;
  match(TileDP, m_Intrinsic<IntrID>(m_Value(M), m_Value(N), m_Value(K),
                                    m_Value(C), m_Value(A), m_Value(B)));
  Instruction *InsertI = TileDP;
  IRBuilder<> PreBuilder(TileDP);
  PreBuilder.SetInsertPoint(TileDP);
  // We visit the loop with (m, n/4, k/4):
  // %n_dword = lshr i16 %n, 2
  // %k_dword = lshr i16 %k, 2
  Value *NDWord = PreBuilder.CreateLShr(N, PreBuilder.getInt16(2));
  Value *KDWord = PreBuilder.CreateLShr(K, PreBuilder.getInt16(2));
  BasicBlock *Start = InsertI->getParent();
  BasicBlock *End =
      SplitBlock(InsertI->getParent(), InsertI, &DTU, LI, nullptr, "continue");
  IRBuilder<> Builder(TileDP);
  Value *ResVec = createTileDPLoops<IntrID>(Start, End, Builder, M, NDWord,
                                            KDWord, C, A, B);
  // we cannot assume there always be bitcast after tiledpbssd. So we need to
  // insert one bitcast as required
  Builder.SetInsertPoint(End, End->getFirstNonPHIIt());
  Value *ResAMX =
      Builder.CreateBitCast(ResVec, Type::getX86_AMXTy(Builder.getContext()));
  // Delete TileDP intrinsic and do some clean-up.
  for (Use &U : llvm::make_early_inc_range(TileDP->uses())) {
    Instruction *I = cast<Instruction>(U.getUser());
    Value *Vec;
    if (match(I, m_BitCast(m_Value(Vec)))) {
      I->replaceAllUsesWith(ResVec);
      I->eraseFromParent();
    }
  }
  TileDP->replaceAllUsesWith(ResAMX);
  TileDP->eraseFromParent();
  return true;
}

template <bool IsTileLoad>
bool X86LowerAMXIntrinsics::lowerTileLoadStore(Instruction *TileLoadStore) {
  Value *M, *N, *Ptr, *Stride, *Tile;
  if (IsTileLoad)
    match(TileLoadStore,
          m_Intrinsic<Intrinsic::x86_tileloadd64_internal>(
              m_Value(M), m_Value(N), m_Value(Ptr), m_Value(Stride)));
  else
    match(TileLoadStore, m_Intrinsic<Intrinsic::x86_tilestored64_internal>(
                             m_Value(M), m_Value(N), m_Value(Ptr),
                             m_Value(Stride), m_Value(Tile)));

  Instruction *InsertI = TileLoadStore;
  IRBuilder<> PreBuilder(TileLoadStore);
  PreBuilder.SetInsertPoint(TileLoadStore);
  Value *NDWord = PreBuilder.CreateLShr(N, PreBuilder.getInt16(2));
  Value *StrideDWord = PreBuilder.CreateLShr(Stride, PreBuilder.getInt64(2));
  BasicBlock *Start = InsertI->getParent();
  BasicBlock *End =
      SplitBlock(InsertI->getParent(), InsertI, &DTU, LI, nullptr, "continue");
  IRBuilder<> Builder(TileLoadStore);
  Value *ResVec = createTileLoadStoreLoops<IsTileLoad>(
      Start, End, Builder, M, NDWord, Ptr, StrideDWord,
      IsTileLoad ? nullptr : Tile);
  if (IsTileLoad) {
    // we cannot assume there always be bitcast after tileload. So we need to
    // insert one bitcast as required
    Builder.SetInsertPoint(End, End->getFirstNonPHIIt());
    Value *ResAMX =
        Builder.CreateBitCast(ResVec, Type::getX86_AMXTy(Builder.getContext()));
    // Delete tileloadd6 intrinsic and do some clean-up
    for (Use &U : llvm::make_early_inc_range(TileLoadStore->uses())) {
      Instruction *I = cast<Instruction>(U.getUser());
      Value *Vec;
      if (match(I, m_BitCast(m_Value(Vec)))) {
        I->replaceAllUsesWith(ResVec);
        I->eraseFromParent();
      }
    }
    TileLoadStore->replaceAllUsesWith(ResAMX);
  }
  TileLoadStore->eraseFromParent();
  return true;
}

bool X86LowerAMXIntrinsics::lowerTileZero(Instruction *TileZero) {
  IRBuilder<> Builder(TileZero);
  FixedVectorType *V256I32Ty = FixedVectorType::get(Builder.getInt32Ty(), 256);
  Value *VecZero = Constant::getNullValue(V256I32Ty);
  for (Use &U : llvm::make_early_inc_range(TileZero->uses())) {
    Instruction *I = cast<Instruction>(U.getUser());
    Value *Vec;
    if (match(I, m_BitCast(m_Value(Vec)))) {
      I->replaceAllUsesWith(VecZero);
      I->eraseFromParent();
    }
  }
  TileZero->eraseFromParent();
  return true;
}

bool X86LowerAMXIntrinsics::visit() {
  bool C = false;
  SmallVector<IntrinsicInst *, 8> WorkList;
  for (BasicBlock *BB : depth_first(&Func)) {
    for (BasicBlock::iterator II = BB->begin(), IE = BB->end(); II != IE;) {
      if (auto *Inst = dyn_cast<IntrinsicInst>(&*II++)) {
        switch (Inst->getIntrinsicID()) {
        case Intrinsic::x86_tdpbssd_internal:
        case Intrinsic::x86_tdpbsud_internal:
        case Intrinsic::x86_tdpbusd_internal:
        case Intrinsic::x86_tdpbuud_internal:
        case Intrinsic::x86_tileloadd64_internal:
        case Intrinsic::x86_tilestored64_internal:
        case Intrinsic::x86_tilezero_internal:
        case Intrinsic::x86_tdpbf16ps_internal:
          WorkList.push_back(Inst);
          break;
        default:
          break;
        }
      }
    }
  }

  for (auto *Inst : WorkList) {
    switch (Inst->getIntrinsicID()) {
    case Intrinsic::x86_tdpbssd_internal:
      C = lowerTileDP<Intrinsic::x86_tdpbssd_internal>(Inst) || C;
      break;
    case Intrinsic::x86_tdpbsud_internal:
      C = lowerTileDP<Intrinsic::x86_tdpbsud_internal>(Inst) || C;
      break;
    case Intrinsic::x86_tdpbusd_internal:
      C = lowerTileDP<Intrinsic::x86_tdpbusd_internal>(Inst) || C;
      break;
    case Intrinsic::x86_tdpbuud_internal:
      C = lowerTileDP<Intrinsic::x86_tdpbuud_internal>(Inst) || C;
      break;
    case Intrinsic::x86_tdpbf16ps_internal:
      C = lowerTileDP<Intrinsic::x86_tdpbf16ps_internal>(Inst) || C;
      break;
    case Intrinsic::x86_tileloadd64_internal:
      C = lowerTileLoadStore<true>(Inst) || C;
      break;
    case Intrinsic::x86_tilestored64_internal:
      C = lowerTileLoadStore<false>(Inst) || C;
      break;
    case Intrinsic::x86_tilezero_internal:
      C = lowerTileZero(Inst) || C;
      break;
    default:
      llvm_unreachable("invalid amx intrinsics!");
    }
  }

  return C;
}

namespace {
class X86LowerAMXIntrinsicsLegacyPass : public FunctionPass {
public:
  static char ID;

  X86LowerAMXIntrinsicsLegacyPass() : FunctionPass(ID) {
    initializeX86LowerAMXIntrinsicsLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (!X86ScalarizeAMX)
      return false;
    TargetMachine *TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
    if (!F.hasFnAttribute(Attribute::OptimizeNone) &&
        TM->getOptLevel() != CodeGenOptLevel::None)
      return false;

    auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>();
    auto *DT = DTWP ? &DTWP->getDomTree() : nullptr;
    auto *LIWP = getAnalysisIfAvailable<LoopInfoWrapperPass>();
    auto *LI = LIWP ? &LIWP->getLoopInfo() : nullptr;
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);

    X86LowerAMXIntrinsics LAT(F, DTU, LI);
    return LAT.visit();
  }
  StringRef getPassName() const override { return "Lower AMX intrinsics"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
  }
};
} // namespace

static const char PassName[] = "Lower AMX intrinsics";
char X86LowerAMXIntrinsicsLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(X86LowerAMXIntrinsicsLegacyPass, DEBUG_TYPE, PassName,
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_END(X86LowerAMXIntrinsicsLegacyPass, DEBUG_TYPE, PassName,
                    false, false)

FunctionPass *llvm::createX86LowerAMXIntrinsicsPass() {
  return new X86LowerAMXIntrinsicsLegacyPass();
}
