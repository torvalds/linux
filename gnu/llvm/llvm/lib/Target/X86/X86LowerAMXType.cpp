//===- Target/X86/X86LowerAMXType.cpp - -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Pass to transform <256 x i32> load/store
/// <256 x i32> is bitcasted to x86_amx on X86, and AMX instruction set only
/// provides simple operation on x86_amx. The basic elementwise operation
/// is not supported by AMX. Since x86_amx is bitcasted from vector <256 x i32>
/// and only AMX intrinsics can operate on the type, we need transform
/// load/store <256 x i32> instruction to AMX load/store. If the bitcast can
/// not be combined with load/store, we transform the bitcast to amx load/store
/// and <256 x i32> store/load.
///
/// If Front End not use O0 but the Mid/Back end use O0, (e.g. "Clang -O2 -S
/// -emit-llvm t.c" + "llc t.ll") we should make sure the amx data is volatile,
/// because that is necessary for AMX fast register allocation. (In Fast
/// registera allocation, register will be allocated before spill/reload, so
/// there is no additional register for amx to identify the step in spill.)
/// The volatileTileData() will handle this case.
/// e.g.
/// ----------------------------------------------------------
/// | def %td = ...                                          |
/// | ...                                                    |
/// | "use %td"                                              |
/// ----------------------------------------------------------
/// will transfer to -->
/// ----------------------------------------------------------
/// | def %td = ...                                          |
/// | call void @llvm.x86.tilestored64.internal(mem, %td)    |
/// | ...                                                    |
/// | %td2 = call x86_amx @llvm.x86.tileloadd64.internal(mem)|
/// | "use %td2"                                             |
/// ----------------------------------------------------------
//
//===----------------------------------------------------------------------===//
//
#include "X86.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
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
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Local.h"

#include <map>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "lower-amx-type"

static bool isAMXCast(Instruction *II) {
  return match(II,
               m_Intrinsic<Intrinsic::x86_cast_vector_to_tile>(m_Value())) ||
         match(II, m_Intrinsic<Intrinsic::x86_cast_tile_to_vector>(m_Value()));
}

static bool isAMXIntrinsic(Value *I) {
  auto *II = dyn_cast<IntrinsicInst>(I);
  if (!II)
    return false;
  if (isAMXCast(II))
    return false;
  // Check if return type or parameter is x86_amx. If it is x86_amx
  // the intrinsic must be x86 amx intrinsics.
  if (II->getType()->isX86_AMXTy())
    return true;
  for (Value *V : II->args()) {
    if (V->getType()->isX86_AMXTy())
      return true;
  }

  return false;
}

static bool containsAMXCode(Function &F) {
  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (I.getType()->isX86_AMXTy())
        return true;
  return false;
}

static AllocaInst *createAllocaInstAtEntry(IRBuilder<> &Builder, BasicBlock *BB,
                                           Type *Ty) {
  Function &F = *BB->getParent();
  const DataLayout &DL = F.getDataLayout();

  LLVMContext &Ctx = Builder.getContext();
  auto AllocaAlignment = DL.getPrefTypeAlign(Type::getX86_AMXTy(Ctx));
  unsigned AllocaAS = DL.getAllocaAddrSpace();
  AllocaInst *AllocaRes =
      new AllocaInst(Ty, AllocaAS, "", F.getEntryBlock().begin());
  AllocaRes->setAlignment(AllocaAlignment);
  return AllocaRes;
}

static Instruction *getFirstNonAllocaInTheEntryBlock(Function &F) {
  for (Instruction &I : F.getEntryBlock())
    if (!isa<AllocaInst>(&I))
      return &I;
  llvm_unreachable("No terminator in the entry block!");
}

static std::pair<Value *, Value *> getShape(IntrinsicInst *II, unsigned OpNo) {
  IRBuilder<> Builder(II);
  Value *Row = nullptr, *Col = nullptr;
  switch (II->getIntrinsicID()) {
  default:
    llvm_unreachable("Expect amx intrinsics");
  case Intrinsic::x86_tileloadd64_internal:
  case Intrinsic::x86_tileloaddt164_internal:
  case Intrinsic::x86_tilestored64_internal: {
    Row = II->getArgOperand(0);
    Col = II->getArgOperand(1);
    break;
  }
  // a * b + c
  // The shape depends on which operand.
  case Intrinsic::x86_tcmmimfp16ps_internal:
  case Intrinsic::x86_tcmmrlfp16ps_internal:
  case Intrinsic::x86_tdpbssd_internal:
  case Intrinsic::x86_tdpbsud_internal:
  case Intrinsic::x86_tdpbusd_internal:
  case Intrinsic::x86_tdpbuud_internal:
  case Intrinsic::x86_tdpbf16ps_internal:
  case Intrinsic::x86_tdpfp16ps_internal: {
    switch (OpNo) {
    case 3:
      Row = II->getArgOperand(0);
      Col = II->getArgOperand(1);
      break;
    case 4:
      Row = II->getArgOperand(0);
      Col = II->getArgOperand(2);
      break;
    case 5:
      if (isa<ConstantInt>(II->getArgOperand(2)))
        Row = Builder.getInt16(
            (cast<ConstantInt>(II->getOperand(2))->getSExtValue()) / 4);
      else if (isa<Instruction>(II->getArgOperand(2))) {
        // When it is not a const value and it is not a function argument, we
        // create Row after the definition of II->getOperand(2) instead of
        // before II. For example, II is %118, we try to getshape for %117:
        //   %117 = call x86_amx @llvm.x86.cast.vector.to.tile.v256i32(<256 x
        //   i32> %115).
        //   %118 = call x86_amx @llvm.x86.tdpbf16ps.internal(i16
        //   %104, i16 %105, i16 %106, x86_amx %110, x86_amx %114, x86_amx
        //   %117).
        // If we create %row = udiv i16 %106, 4 before %118(aka. II), then its
        // definition is after its user(new tileload for %117).
        // So, the best choice is to create %row right after the definition of
        // %106.
        Builder.SetInsertPoint(cast<Instruction>(II->getOperand(2)));
        Row = Builder.CreateUDiv(II->getOperand(2), Builder.getInt16(4));
        cast<Instruction>(Row)->moveAfter(cast<Instruction>(II->getOperand(2)));
      } else {
        // When it is not a const value and it is a function argument, we create
        // Row at the entry bb.
        IRBuilder<> NewBuilder(
            getFirstNonAllocaInTheEntryBlock(*II->getFunction()));
        Row = NewBuilder.CreateUDiv(II->getOperand(2), NewBuilder.getInt16(4));
      }
      Col = II->getArgOperand(1);
      break;
    }
    break;
  }
  }

  return std::make_pair(Row, Col);
}

static std::pair<Value *, Value *> getShape(PHINode *Phi) {
  Use &U = *(Phi->use_begin());
  unsigned OpNo = U.getOperandNo();
  User *V = U.getUser();
  // TODO We don't traverse all users. To make the algorithm simple, here we
  // just traverse the first user. If we can find shape, then return the shape,
  // otherwise just return nullptr and the optimization for undef/zero will be
  // abandoned.
  while (V) {
    if (isAMXCast(dyn_cast<Instruction>(V))) {
      if (V->use_empty())
        break;
      Use &U = *(V->use_begin());
      OpNo = U.getOperandNo();
      V = U.getUser();
    } else if (isAMXIntrinsic(V)) {
      return getShape(cast<IntrinsicInst>(V), OpNo);
    } else if (isa<PHINode>(V)) {
      if (V->use_empty())
        break;
      Use &U = *(V->use_begin());
      V = U.getUser();
    } else {
      break;
    }
  }

  return std::make_pair(nullptr, nullptr);
}

namespace {
class X86LowerAMXType {
  Function &Func;

  // In AMX intrinsics we let Shape = {Row, Col}, but the
  // RealCol = Col / ElementSize. We may use the RealCol
  // as a new Row for other new created AMX intrinsics.
  std::map<Value *, Value *> Col2Row;

public:
  X86LowerAMXType(Function &F) : Func(F) {}
  bool visit();
  void combineLoadBitcast(LoadInst *LD, BitCastInst *Bitcast);
  void combineBitcastStore(BitCastInst *Bitcast, StoreInst *ST);
  bool transformBitcast(BitCastInst *Bitcast);
};

// %src = load <256 x i32>, <256 x i32>* %addr, align 64
// %2 = bitcast <256 x i32> %src to x86_amx
// -->
// %2 = call x86_amx @llvm.x86.tileloadd64.internal(i16 %row, i16 %col,
// i8* %addr, i64 %stride64)
void X86LowerAMXType::combineLoadBitcast(LoadInst *LD, BitCastInst *Bitcast) {
  Value *Row = nullptr, *Col = nullptr;
  Use &U = *(Bitcast->use_begin());
  unsigned OpNo = U.getOperandNo();
  auto *II = cast<IntrinsicInst>(U.getUser());
  std::tie(Row, Col) = getShape(II, OpNo);
  IRBuilder<> Builder(Bitcast);
  // Use the maximun column as stride.
  Value *Stride = Builder.getInt64(64);
  Value *I8Ptr = LD->getOperand(0);
  std::array<Value *, 4> Args = {Row, Col, I8Ptr, Stride};

  Value *NewInst = Builder.CreateIntrinsic(Intrinsic::x86_tileloadd64_internal,
                                           std::nullopt, Args);
  Bitcast->replaceAllUsesWith(NewInst);
}

// %src = call x86_amx @llvm.x86.tileloadd64.internal(%row, %col, %addr,
//                                                    %stride);
// %13 = bitcast x86_amx %src to <256 x i32>
// store <256 x i32> %13, <256 x i32>* %addr, align 64
// -->
// call void @llvm.x86.tilestored64.internal(%row, %col, %addr,
//                                           %stride64, %13)
void X86LowerAMXType::combineBitcastStore(BitCastInst *Bitcast, StoreInst *ST) {

  Value *Tile = Bitcast->getOperand(0);
  auto *II = cast<IntrinsicInst>(Tile);
  // Tile is output from AMX intrinsic. The first operand of the
  // intrinsic is row, the second operand of the intrinsic is column.
  Value *Row = II->getOperand(0);
  Value *Col = II->getOperand(1);
  IRBuilder<> Builder(ST);
  // Use the maximum column as stride. It must be the same with load
  // stride.
  Value *Stride = Builder.getInt64(64);
  Value *I8Ptr = ST->getOperand(1);
  std::array<Value *, 5> Args = {Row, Col, I8Ptr, Stride, Tile};
  Builder.CreateIntrinsic(Intrinsic::x86_tilestored64_internal, std::nullopt,
                          Args);
  if (Bitcast->hasOneUse())
    return;
  // %13 = bitcast x86_amx %src to <256 x i32>
  // store <256 x i32> %13, <256 x i32>* %addr, align 64
  // %add = <256 x i32> %13, <256 x i32> %src2
  // -->
  // %13 = bitcast x86_amx %src to <256 x i32>
  // call void @llvm.x86.tilestored64.internal(%row, %col, %addr,
  //                                           %stride64, %13)
  // %14 = load <256 x i32>, %addr
  // %add = <256 x i32> %14, <256 x i32> %src2
  Value *Vec = Builder.CreateLoad(Bitcast->getType(), ST->getOperand(1));
  Bitcast->replaceAllUsesWith(Vec);
}

// transform bitcast to <store, load> instructions.
bool X86LowerAMXType::transformBitcast(BitCastInst *Bitcast) {
  IRBuilder<> Builder(Bitcast);
  AllocaInst *AllocaAddr;
  Value *I8Ptr, *Stride;
  auto *Src = Bitcast->getOperand(0);

  auto Prepare = [&](Type *MemTy) {
    AllocaAddr = createAllocaInstAtEntry(Builder, Bitcast->getParent(), MemTy);
    I8Ptr = AllocaAddr;
    Stride = Builder.getInt64(64);
  };

  if (Bitcast->getType()->isX86_AMXTy()) {
    // %2 = bitcast <256 x i32> %src to x86_amx
    // -->
    // %addr = alloca <256 x i32>, align 64
    // store <256 x i32> %src, <256 x i32>* %addr, align 64
    // %addr2 = bitcast <256 x i32>* to i8*
    // %2 = call x86_amx @llvm.x86.tileloadd64.internal(i16 %row, i16 %col,
    //                                                  i8* %addr2,
    //                                                  i64 64)
    Use &U = *(Bitcast->use_begin());
    unsigned OpNo = U.getOperandNo();
    auto *II = dyn_cast<IntrinsicInst>(U.getUser());
    if (!II)
      return false; // May be bitcast from x86amx to <256 x i32>.
    Prepare(Bitcast->getOperand(0)->getType());
    Builder.CreateStore(Src, AllocaAddr);
    // TODO we can pick an constant operand for the shape.
    Value *Row = nullptr, *Col = nullptr;
    std::tie(Row, Col) = getShape(II, OpNo);
    std::array<Value *, 4> Args = {Row, Col, I8Ptr, Stride};
    Value *NewInst = Builder.CreateIntrinsic(
        Intrinsic::x86_tileloadd64_internal, std::nullopt, Args);
    Bitcast->replaceAllUsesWith(NewInst);
  } else {
    // %2 = bitcast x86_amx %src to <256 x i32>
    // -->
    // %addr = alloca <256 x i32>, align 64
    // %addr2 = bitcast <256 x i32>* to i8*
    // call void @llvm.x86.tilestored64.internal(i16 %row, i16 %col,
    //                                           i8* %addr2, i64 %stride)
    // %2 = load <256 x i32>, <256 x i32>* %addr, align 64
    auto *II = dyn_cast<IntrinsicInst>(Src);
    if (!II)
      return false; // May be bitcast from <256 x i32> to x86amx.
    Prepare(Bitcast->getType());
    Value *Row = II->getOperand(0);
    Value *Col = II->getOperand(1);
    std::array<Value *, 5> Args = {Row, Col, I8Ptr, Stride, Src};
    Builder.CreateIntrinsic(Intrinsic::x86_tilestored64_internal, std::nullopt,
                            Args);
    Value *NewInst = Builder.CreateLoad(Bitcast->getType(), AllocaAddr);
    Bitcast->replaceAllUsesWith(NewInst);
  }

  return true;
}

bool X86LowerAMXType::visit() {
  SmallVector<Instruction *, 8> DeadInsts;
  Col2Row.clear();

  for (BasicBlock *BB : post_order(&Func)) {
    for (Instruction &Inst : llvm::make_early_inc_range(llvm::reverse(*BB))) {
      auto *Bitcast = dyn_cast<BitCastInst>(&Inst);
      if (!Bitcast)
        continue;

      Value *Src = Bitcast->getOperand(0);
      if (Bitcast->getType()->isX86_AMXTy()) {
        if (Bitcast->user_empty()) {
          DeadInsts.push_back(Bitcast);
          continue;
        }
        LoadInst *LD = dyn_cast<LoadInst>(Src);
        if (!LD) {
          if (transformBitcast(Bitcast))
            DeadInsts.push_back(Bitcast);
          continue;
        }
        // If load has mutli-user, duplicate a vector load.
        // %src = load <256 x i32>, <256 x i32>* %addr, align 64
        // %2 = bitcast <256 x i32> %src to x86_amx
        // %add = add <256 x i32> %src, <256 x i32> %src2
        // -->
        // %src = load <256 x i32>, <256 x i32>* %addr, align 64
        // %2 = call x86_amx @llvm.x86.tileloadd64.internal(i16 %row, i16 %col,
        //                                            i8* %addr, i64 %stride64)
        // %add = add <256 x i32> %src, <256 x i32> %src2

        // If load has one user, the load will be eliminated in DAG ISel.
        // %src = load <256 x i32>, <256 x i32>* %addr, align 64
        // %2 = bitcast <256 x i32> %src to x86_amx
        // -->
        // %2 = call x86_amx @llvm.x86.tileloadd64.internal(i16 %row, i16 %col,
        //                                            i8* %addr, i64 %stride64)
        combineLoadBitcast(LD, Bitcast);
        DeadInsts.push_back(Bitcast);
        if (LD->hasOneUse())
          DeadInsts.push_back(LD);
      } else if (Src->getType()->isX86_AMXTy()) {
        if (Bitcast->user_empty()) {
          DeadInsts.push_back(Bitcast);
          continue;
        }
        StoreInst *ST = nullptr;
        for (Use &U : Bitcast->uses()) {
          ST = dyn_cast<StoreInst>(U.getUser());
          if (ST)
            break;
        }
        if (!ST) {
          if (transformBitcast(Bitcast))
            DeadInsts.push_back(Bitcast);
          continue;
        }
        // If bitcast (%13) has one use, combine bitcast and store to amx store.
        // %src = call x86_amx @llvm.x86.tileloadd64.internal(%row, %col, %addr,
        //                                                    %stride);
        // %13 = bitcast x86_amx %src to <256 x i32>
        // store <256 x i32> %13, <256 x i32>* %addr, align 64
        // -->
        // call void @llvm.x86.tilestored64.internal(%row, %col, %addr,
        //                                           %stride64, %13)
        //
        // If bitcast (%13) has multi-use, transform as below.
        // %13 = bitcast x86_amx %src to <256 x i32>
        // store <256 x i32> %13, <256 x i32>* %addr, align 64
        // %add = <256 x i32> %13, <256 x i32> %src2
        // -->
        // %13 = bitcast x86_amx %src to <256 x i32>
        // call void @llvm.x86.tilestored64.internal(%row, %col, %addr,
        //                                           %stride64, %13)
        // %14 = load <256 x i32>, %addr
        // %add = <256 x i32> %14, <256 x i32> %src2
        //
        combineBitcastStore(Bitcast, ST);
        // Delete user first.
        DeadInsts.push_back(ST);
        DeadInsts.push_back(Bitcast);
      }
    }
  }

  bool C = !DeadInsts.empty();

  for (auto *Inst : DeadInsts)
    Inst->eraseFromParent();

  return C;
}
} // anonymous namespace

static Value *getAllocaPos(BasicBlock *BB) {
  Function *F = BB->getParent();
  IRBuilder<> Builder(&F->getEntryBlock().front());
  const DataLayout &DL = F->getDataLayout();
  unsigned AllocaAS = DL.getAllocaAddrSpace();
  Type *V256I32Ty = VectorType::get(Builder.getInt32Ty(), 256, false);
  AllocaInst *AllocaRes =
      new AllocaInst(V256I32Ty, AllocaAS, "", F->getEntryBlock().begin());
  BasicBlock::iterator Iter = AllocaRes->getIterator();
  ++Iter;
  Builder.SetInsertPoint(&*Iter);
  Value *I8Ptr = Builder.CreateBitCast(AllocaRes, Builder.getPtrTy());
  return I8Ptr;
}

static Instruction *createTileStore(Instruction *TileDef, Value *Ptr) {
  assert(TileDef->getType()->isX86_AMXTy() && "Not define tile!");
  auto *II = cast<IntrinsicInst>(TileDef);
  assert(II && "Not tile intrinsic!");
  Value *Row = II->getOperand(0);
  Value *Col = II->getOperand(1);

  BasicBlock *BB = TileDef->getParent();
  BasicBlock::iterator Iter = TileDef->getIterator();
  IRBuilder<> Builder(BB, ++Iter);
  Value *Stride = Builder.getInt64(64);
  std::array<Value *, 5> Args = {Row, Col, Ptr, Stride, TileDef};

  Instruction *TileStore = Builder.CreateIntrinsic(
      Intrinsic::x86_tilestored64_internal, std::nullopt, Args);
  return TileStore;
}

static void replaceWithTileLoad(Use &U, Value *Ptr, bool IsPHI = false) {
  Value *V = U.get();
  assert(V->getType()->isX86_AMXTy() && "Not define tile!");

  // Get tile shape.
  IntrinsicInst *II = nullptr;
  if (IsPHI) {
    Value *PhiOp = cast<PHINode>(V)->getIncomingValue(0);
    II = cast<IntrinsicInst>(PhiOp);
  } else {
    II = cast<IntrinsicInst>(V);
  }
  Value *Row = II->getOperand(0);
  Value *Col = II->getOperand(1);

  Instruction *UserI = cast<Instruction>(U.getUser());
  IRBuilder<> Builder(UserI);
  Value *Stride = Builder.getInt64(64);
  std::array<Value *, 4> Args = {Row, Col, Ptr, Stride};

  Value *TileLoad = Builder.CreateIntrinsic(Intrinsic::x86_tileloadd64_internal,
                                            std::nullopt, Args);
  UserI->replaceUsesOfWith(V, TileLoad);
}

static bool isIncomingOfPHI(Instruction *I) {
  for (Use &U : I->uses()) {
    User *V = U.getUser();
    if (isa<PHINode>(V))
      return true;
  }
  return false;
}

// Let all AMX tile data become volatile data, shorten the life range
// of each tile register before fast register allocation.
namespace {
class X86VolatileTileData {
  Function &F;

public:
  X86VolatileTileData(Function &Func) : F(Func) {}
  Value *updatePhiIncomings(BasicBlock *BB,
                            SmallVector<Instruction *, 2> &Incomings);
  void replacePhiDefWithLoad(Instruction *PHI, Value *StorePtr);
  bool volatileTileData();
  void volatileTilePHI(PHINode *PHI);
  void volatileTileNonPHI(Instruction *I);
};

Value *X86VolatileTileData::updatePhiIncomings(
    BasicBlock *BB, SmallVector<Instruction *, 2> &Incomings) {
  Value *I8Ptr = getAllocaPos(BB);

  for (auto *I : Incomings) {
    User *Store = createTileStore(I, I8Ptr);

    // All its uses (except phi) should load from stored mem.
    for (Use &U : I->uses()) {
      User *V = U.getUser();
      if (isa<PHINode>(V) || V == Store)
        continue;
      replaceWithTileLoad(U, I8Ptr);
    }
  }
  return I8Ptr;
}

void X86VolatileTileData::replacePhiDefWithLoad(Instruction *PHI,
                                                Value *StorePtr) {
  for (Use &U : PHI->uses())
    replaceWithTileLoad(U, StorePtr, true);
  PHI->eraseFromParent();
}

// Smilar with volatileTileNonPHI, this function only handle PHI Nodes
// and their related AMX intrinsics.
// 1) PHI Def should change to tileload.
// 2) PHI Incoming Values should tilestored in just after their def.
// 3) The mem of these tileload and tilestores should be same.
// e.g.
// ------------------------------------------------------
// bb_dom:
//   ...
//   br i1 %bool.cond, label %if.else, label %if.then
//
// if.then:
//   def %t0 = ...
//   ...
//   use %t0
//   ...
//   br label %if.end
//
// if.else:
//   def %t1 = ...
//   br label %if.end
//
// if.end:
//   %td = phi x86_amx [ %t1, %if.else ], [ %t0, %if.then ]
//   ...
//   use %td
// ------------------------------------------------------
// -->
// ------------------------------------------------------
// bb_entry:
//   %mem = alloca <256 x i32>, align 1024                  *
//   ...
// bb_dom:
//   ...
//   br i1 %bool.cond, label %if.else, label %if.then
//
// if.then:
//   def %t0 = ...
//   call void @llvm.x86.tilestored64.internal(mem, %t0)    *
//   ...
//   %t0` = call x86_amx @llvm.x86.tileloadd64.internal(mem)*
//   use %t0`                                               *
//   ...
//   br label %if.end
//
// if.else:
//   def %t1 = ...
//   call void @llvm.x86.tilestored64.internal(mem, %t1)    *
//   br label %if.end
//
// if.end:
//   ...
//   %td = call x86_amx @llvm.x86.tileloadd64.internal(mem) *
//   use %td
// ------------------------------------------------------
void X86VolatileTileData::volatileTilePHI(PHINode *PHI) {
  BasicBlock *BB = PHI->getParent();
  SmallVector<Instruction *, 2> Incomings;

  for (unsigned I = 0, E = PHI->getNumIncomingValues(); I != E; ++I) {
    Value *Op = PHI->getIncomingValue(I);
    Instruction *Inst = dyn_cast<Instruction>(Op);
    assert(Inst && "We shouldn't fold AMX instrution!");
    Incomings.push_back(Inst);
  }

  Value *StorePtr = updatePhiIncomings(BB, Incomings);
  replacePhiDefWithLoad(PHI, StorePtr);
}

// Store the defined tile and load it before use.
// All its users are not PHI.
// e.g.
// ------------------------------------------------------
// def %td = ...
// ...
// "use %td"
// ------------------------------------------------------
// -->
// ------------------------------------------------------
// def %td = ...
// call void @llvm.x86.tilestored64.internal(mem, %td)
// ...
// %td2 = call x86_amx @llvm.x86.tileloadd64.internal(mem)
// "use %td2"
// ------------------------------------------------------
void X86VolatileTileData::volatileTileNonPHI(Instruction *I) {
  BasicBlock *BB = I->getParent();
  Value *I8Ptr = getAllocaPos(BB);
  User *Store = createTileStore(I, I8Ptr);

  // All its uses should load from stored mem.
  for (Use &U : I->uses()) {
    User *V = U.getUser();
    assert(!isa<PHINode>(V) && "PHI Nodes should be excluded!");
    if (V != Store)
      replaceWithTileLoad(U, I8Ptr);
  }
}

// Volatile Tile Model:
// 1) All the uses of tile data comes from tileload in time.
// 2) All the defs of tile data tilestore into mem immediately.
// For example:
// --------------------------------------------------------------------------
// %t1 = call x86_amx @llvm.x86.tileloadd64.internal(m, k, ...)          key
// %t2 = call x86_amx @llvm.x86.tileloadd64.internal(k, n, ...)
// %t3 = call x86_amx @llvm.x86.tileloadd64.internal(m, n, ...)          amx
// %td = tail call x86_amx @llvm.x86.tdpbssd.internal(m, n, k, t1, t2, t3)
// call void @llvm.x86.tilestored64.internal(... td)                     area
// --------------------------------------------------------------------------
// 3) No terminator, call or other amx instructions in the key amx area.
bool X86VolatileTileData::volatileTileData() {
  bool Changed = false;
  for (BasicBlock &BB : F) {
    SmallVector<Instruction *, 2> PHIInsts;
    SmallVector<Instruction *, 8> AMXDefInsts;

    for (Instruction &I : BB) {
      if (!I.getType()->isX86_AMXTy())
        continue;
      if (isa<PHINode>(&I))
        PHIInsts.push_back(&I);
      else
        AMXDefInsts.push_back(&I);
    }

    // First we "volatile" the non-phi related amx intrinsics.
    for (Instruction *I : AMXDefInsts) {
      if (isIncomingOfPHI(I))
        continue;
      volatileTileNonPHI(I);
      Changed = true;
    }

    for (Instruction *I : PHIInsts) {
      volatileTilePHI(dyn_cast<PHINode>(I));
      Changed = true;
    }
  }
  return Changed;
}

} // anonymous namespace

namespace {

class X86LowerAMXCast {
  Function &Func;
  std::unique_ptr<DominatorTree> DT;

public:
  X86LowerAMXCast(Function &F) : Func(F), DT(nullptr) {}
  bool combineCastStore(IntrinsicInst *Cast, StoreInst *ST);
  bool combineLoadCast(IntrinsicInst *Cast, LoadInst *LD);
  bool combineLdSt(SmallVectorImpl<Instruction *> &Casts);
  bool combineAMXcast(TargetLibraryInfo *TLI);
  bool transformAMXCast(IntrinsicInst *AMXCast);
  bool transformAllAMXCast();
  bool optimizeAMXCastFromPhi(IntrinsicInst *CI, PHINode *PN,
                              SmallSetVector<Instruction *, 16> &DeadInst);
};

static bool DCEInstruction(Instruction *I,
                           SmallSetVector<Instruction *, 16> &WorkList,
                           const TargetLibraryInfo *TLI) {
  if (isInstructionTriviallyDead(I, TLI)) {
    salvageDebugInfo(*I);
    salvageKnowledge(I);

    // Null out all of the instruction's operands to see if any operand becomes
    // dead as we go.
    for (unsigned i = 0, e = I->getNumOperands(); i != e; ++i) {
      Value *OpV = I->getOperand(i);
      I->setOperand(i, nullptr);

      if (!OpV->use_empty() || I == OpV)
        continue;

      // If the operand is an instruction that became dead as we nulled out the
      // operand, and if it is 'trivially' dead, delete it in a future loop
      // iteration.
      if (Instruction *OpI = dyn_cast<Instruction>(OpV)) {
        if (isInstructionTriviallyDead(OpI, TLI)) {
          WorkList.insert(OpI);
        }
      }
    }
    I->eraseFromParent();
    return true;
  }
  return false;
}

/// This function handles following case
///
///     A  ->  B    amxcast
///     PHI
///     B  ->  A    amxcast
///
/// All the related PHI nodes can be replaced by new PHI nodes with type A.
/// The uses of \p CI can be changed to the new PHI node corresponding to \p PN.
bool X86LowerAMXCast::optimizeAMXCastFromPhi(
    IntrinsicInst *CI, PHINode *PN,
    SmallSetVector<Instruction *, 16> &DeadInst) {
  IRBuilder<> Builder(CI);
  Value *Src = CI->getOperand(0);
  Type *SrcTy = Src->getType(); // Type B
  Type *DestTy = CI->getType(); // Type A

  SmallVector<PHINode *, 4> PhiWorklist;
  SmallSetVector<PHINode *, 4> OldPhiNodes;

  // Find all of the A->B casts and PHI nodes.
  // We need to inspect all related PHI nodes, but PHIs can be cyclic, so
  // OldPhiNodes is used to track all known PHI nodes, before adding a new
  // PHI to PhiWorklist, it is checked against and added to OldPhiNodes first.
  PhiWorklist.push_back(PN);
  OldPhiNodes.insert(PN);
  while (!PhiWorklist.empty()) {
    auto *OldPN = PhiWorklist.pop_back_val();
    for (unsigned I = 0; I < OldPN->getNumOperands(); ++I) {
      Value *IncValue = OldPN->getIncomingValue(I);
      // TODO: currently, We ignore cases where it is a const. In the future, we
      // might support const.
      if (isa<Constant>(IncValue)) {
        auto *IncConst = dyn_cast<Constant>(IncValue);
        if (!isa<UndefValue>(IncValue) && !IncConst->isZeroValue())
          return false;
        Value *Row = nullptr, *Col = nullptr;
        std::tie(Row, Col) = getShape(OldPN);
        // TODO: If it is not constant the Row and Col must domoniate tilezero
        // that we are going to create.
        if (!Row || !Col || !isa<Constant>(Row) || !isa<Constant>(Col))
          return false;
        // Create tilezero at the end of incoming block.
        auto *Block = OldPN->getIncomingBlock(I);
        BasicBlock::iterator Iter = Block->getTerminator()->getIterator();
        Instruction *NewInst = Builder.CreateIntrinsic(
            Intrinsic::x86_tilezero_internal, std::nullopt, {Row, Col});
        NewInst->moveBefore(&*Iter);
        NewInst = Builder.CreateIntrinsic(Intrinsic::x86_cast_tile_to_vector,
                                          {IncValue->getType()}, {NewInst});
        NewInst->moveBefore(&*Iter);
        // Replace InValue with new Value.
        OldPN->setIncomingValue(I, NewInst);
        IncValue = NewInst;
      }

      if (auto *PNode = dyn_cast<PHINode>(IncValue)) {
        if (OldPhiNodes.insert(PNode))
          PhiWorklist.push_back(PNode);
        continue;
      }
      Instruction *ACI = dyn_cast<Instruction>(IncValue);
      if (ACI && isAMXCast(ACI)) {
        // Verify it's a A->B cast.
        Type *TyA = ACI->getOperand(0)->getType();
        Type *TyB = ACI->getType();
        if (TyA != DestTy || TyB != SrcTy)
          return false;
        continue;
      }
      return false;
    }
  }

  // Check that each user of each old PHI node is something that we can
  // rewrite, so that all of the old PHI nodes can be cleaned up afterwards.
  for (auto *OldPN : OldPhiNodes) {
    for (User *V : OldPN->users()) {
      Instruction *ACI = dyn_cast<Instruction>(V);
      if (ACI && isAMXCast(ACI)) {
        // Verify it's a B->A cast.
        Type *TyB = ACI->getOperand(0)->getType();
        Type *TyA = ACI->getType();
        if (TyA != DestTy || TyB != SrcTy)
          return false;
      } else if (auto *PHI = dyn_cast<PHINode>(V)) {
        // As long as the user is another old PHI node, then even if we don't
        // rewrite it, the PHI web we're considering won't have any users
        // outside itself, so it'll be dead.
        // example:
        //   bb.0:
        //      %0 = amxcast ...
        //   bb.1:
        //      %1 = amxcast ...
        //   bb.2:
        //      %goodphi = phi %0, %1
        //      %3 = amxcast %goodphi
        //   bb.3:
        //      %goodphi2 = phi %0, %goodphi
        //      %4 = amxcast %goodphi2
        // When optimizeAMXCastFromPhi process %3 and %goodphi, %goodphi2 is
        // outside the phi-web, so the combination stop When
        // optimizeAMXCastFromPhi process %4 and %goodphi2, the optimization
        // will be done.
        if (OldPhiNodes.count(PHI) == 0)
          return false;
      } else
        return false;
    }
  }

  // For each old PHI node, create a corresponding new PHI node with a type A.
  SmallDenseMap<PHINode *, PHINode *> NewPNodes;
  for (auto *OldPN : OldPhiNodes) {
    Builder.SetInsertPoint(OldPN);
    PHINode *NewPN = Builder.CreatePHI(DestTy, OldPN->getNumOperands());
    NewPNodes[OldPN] = NewPN;
  }

  // Fill in the operands of new PHI nodes.
  for (auto *OldPN : OldPhiNodes) {
    PHINode *NewPN = NewPNodes[OldPN];
    for (unsigned j = 0, e = OldPN->getNumOperands(); j != e; ++j) {
      Value *V = OldPN->getOperand(j);
      Value *NewV = nullptr;
      Instruction *ACI = dyn_cast<Instruction>(V);
      // There should not be a AMXcast from a const.
      if (ACI && isAMXCast(ACI))
        NewV = ACI->getOperand(0);
      else if (auto *PrevPN = dyn_cast<PHINode>(V))
        NewV = NewPNodes[PrevPN];
      assert(NewV);
      NewPN->addIncoming(NewV, OldPN->getIncomingBlock(j));
    }
  }

  // Traverse all accumulated PHI nodes and process its users,
  // which are Stores and BitcCasts. Without this processing
  // NewPHI nodes could be replicated and could lead to extra
  // moves generated after DeSSA.
  // If there is a store with type B, change it to type A.

  // Replace users of BitCast B->A with NewPHI. These will help
  // later to get rid of a closure formed by OldPHI nodes.
  for (auto *OldPN : OldPhiNodes) {
    PHINode *NewPN = NewPNodes[OldPN];
    for (User *V : make_early_inc_range(OldPN->users())) {
      Instruction *ACI = dyn_cast<Instruction>(V);
      if (ACI && isAMXCast(ACI)) {
        Type *TyB = ACI->getOperand(0)->getType();
        Type *TyA = ACI->getType();
        assert(TyA == DestTy && TyB == SrcTy);
        (void)TyA;
        (void)TyB;
        ACI->replaceAllUsesWith(NewPN);
        DeadInst.insert(ACI);
      } else if (auto *PHI = dyn_cast<PHINode>(V)) {
        // We don't need to push PHINode into DeadInst since they are operands
        // of rootPN DCE can safely delete rootPN's operands if rootPN is dead.
        assert(OldPhiNodes.contains(PHI));
        (void)PHI;
      } else
        llvm_unreachable("all uses should be handled");
    }
  }
  return true;
}

// %43 = call <256 x i32> @llvm.x86.cast.tile.to.vector.v256i32(x86_amx %42)
// store <256 x i32> %43, <256 x i32>* %p, align 64
// -->
// call void @llvm.x86.tilestored64.internal(i16 %row, i16 %col, i8* %p,
//                                           i64 64, x86_amx %42)
bool X86LowerAMXCast::combineCastStore(IntrinsicInst *Cast, StoreInst *ST) {
  Value *Tile = Cast->getOperand(0);
  // TODO: If it is cast intrinsic or phi node, we can propagate the
  // shape information through def-use chain.
  if (!isAMXIntrinsic(Tile))
    return false;
  auto *II = cast<IntrinsicInst>(Tile);
  // Tile is output from AMX intrinsic. The first operand of the
  // intrinsic is row, the second operand of the intrinsic is column.
  Value *Row = II->getOperand(0);
  Value *Col = II->getOperand(1);
  IRBuilder<> Builder(ST);
  // Stride should be equal to col(measured by bytes)
  Value *Stride = Builder.CreateSExt(Col, Builder.getInt64Ty());
  Value *I8Ptr = Builder.CreateBitCast(ST->getOperand(1), Builder.getPtrTy());
  std::array<Value *, 5> Args = {Row, Col, I8Ptr, Stride, Tile};
  Builder.CreateIntrinsic(Intrinsic::x86_tilestored64_internal, std::nullopt,
                          Args);
  return true;
}

// %65 = load <256 x i32>, <256 x i32>* %p, align 64
// %66 = call x86_amx @llvm.x86.cast.vector.to.tile(<256 x i32> %65)
// -->
// %66 = call x86_amx @llvm.x86.tileloadd64.internal(i16 %row, i16 %col,
//                                                   i8* %p, i64 64)
bool X86LowerAMXCast::combineLoadCast(IntrinsicInst *Cast, LoadInst *LD) {
  bool EraseLoad = true;
  Value *Row = nullptr, *Col = nullptr;
  Use &U = *(Cast->use_begin());
  unsigned OpNo = U.getOperandNo();
  auto *II = cast<IntrinsicInst>(U.getUser());
  // TODO: If it is cast intrinsic or phi node, we can propagate the
  // shape information through def-use chain.
  if (!isAMXIntrinsic(II))
    return false;
  std::tie(Row, Col) = getShape(II, OpNo);
  IRBuilder<> Builder(LD);
  // Stride should be equal to col(measured by bytes)
  Value *Stride = Builder.CreateSExt(Col, Builder.getInt64Ty());
  Value *I8Ptr;

  // To save compiling time, we create doninator tree when it is really
  // needed.
  if (!DT)
    DT.reset(new DominatorTree(Func));
  if (!DT->dominates(Row, LD) || !DT->dominates(Col, LD)) {
    // store the value to stack and reload it from stack before cast.
    auto *AllocaAddr =
        createAllocaInstAtEntry(Builder, Cast->getParent(), LD->getType());
    Builder.SetInsertPoint(&*std::next(LD->getIterator()));
    Builder.CreateStore(LD, AllocaAddr);

    Builder.SetInsertPoint(Cast);
    I8Ptr = Builder.CreateBitCast(AllocaAddr, Builder.getPtrTy());
    EraseLoad = false;
  } else {
    I8Ptr = Builder.CreateBitCast(LD->getOperand(0), Builder.getPtrTy());
  }
  std::array<Value *, 4> Args = {Row, Col, I8Ptr, Stride};

  Value *NewInst = Builder.CreateIntrinsic(Intrinsic::x86_tileloadd64_internal,
                                           std::nullopt, Args);
  Cast->replaceAllUsesWith(NewInst);

  return EraseLoad;
}

bool X86LowerAMXCast::combineLdSt(SmallVectorImpl<Instruction *> &Casts) {
  bool Change = false;
  for (auto *Cast : Casts) {
    auto *II = cast<IntrinsicInst>(Cast);
    // %43 = call <256 x i32> @llvm.x86.cast.tile.to.vector(x86_amx %42)
    // store <256 x i32> %43, <256 x i32>* %p, align 64
    // -->
    // call void @llvm.x86.tilestored64.internal(i16 %row, i16 %col, i8* %p,
    //                                           i64 64, x86_amx %42)
    if (II->getIntrinsicID() == Intrinsic::x86_cast_tile_to_vector) {
      SmallVector<Instruction *, 2> DeadStores;
      for (User *U : Cast->users()) {
        StoreInst *Store = dyn_cast<StoreInst>(U);
        if (!Store)
          continue;
        if (combineCastStore(cast<IntrinsicInst>(Cast), Store)) {
          DeadStores.push_back(Store);
          Change = true;
        }
      }
      for (auto *Store : DeadStores)
        Store->eraseFromParent();
    } else { // x86_cast_vector_to_tile
      SmallVector<Instruction *, 2> DeadLoads;
      auto *Load = dyn_cast<LoadInst>(Cast->getOperand(0));
      if (!Load || !Load->hasOneUse())
        continue;
      // %65 = load <256 x i32>, <256 x i32>* %p, align 64
      // %66 = call x86_amx @llvm.x86.cast.vector.to.tile(<256 x i32> %65)
      // -->
      // %66 = call x86_amx @llvm.x86.tileloadd64.internal(i16 %row, i16 %col,
      //                                                   i8* %p, i64 64)
      if (combineLoadCast(cast<IntrinsicInst>(Cast), Load)) {
        // Set the operand is null so that load instruction can be erased.
        Cast->setOperand(0, nullptr);
        Load->eraseFromParent();
      }
    }
  }
  return Change;
}

bool X86LowerAMXCast::combineAMXcast(TargetLibraryInfo *TLI) {
  bool Change = false;
  // Collect tile cast instruction.
  SmallVector<Instruction *, 8> Vec2TileInsts;
  SmallVector<Instruction *, 8> Tile2VecInsts;
  SmallVector<Instruction *, 8> PhiCastWorkList;
  SmallSetVector<Instruction *, 16> DeadInst;
  for (BasicBlock &BB : Func) {
    for (Instruction &I : BB) {
      Value *Vec;
      if (match(&I,
                m_Intrinsic<Intrinsic::x86_cast_vector_to_tile>(m_Value(Vec))))
        Vec2TileInsts.push_back(&I);
      else if (match(&I, m_Intrinsic<Intrinsic::x86_cast_tile_to_vector>(
                             m_Value(Vec))))
        Tile2VecInsts.push_back(&I);
    }
  }

  auto Convert = [&](SmallVectorImpl<Instruction *> &Insts, Intrinsic::ID IID) {
    for (auto *Inst : Insts) {
      for (User *U : Inst->users()) {
        IntrinsicInst *II = dyn_cast<IntrinsicInst>(U);
        if (!II || II->getIntrinsicID() != IID)
          continue;
        // T1 = vec2tile V0
        // V2 = tile2vec T1
        // V3 = OP V2
        // -->
        // T1 = vec2tile V0
        // V2 = tile2vec T1
        // V3 = OP V0
        II->replaceAllUsesWith(Inst->getOperand(0));
        Change = true;
      }
    }
  };

  Convert(Vec2TileInsts, Intrinsic::x86_cast_tile_to_vector);
  Convert(Tile2VecInsts, Intrinsic::x86_cast_vector_to_tile);

  SmallVector<Instruction *, 8> LiveCasts;
  auto EraseInst = [&](SmallVectorImpl<Instruction *> &Insts) {
    for (auto *Inst : Insts) {
      if (Inst->use_empty()) {
        Inst->eraseFromParent();
        Change = true;
      } else {
        LiveCasts.push_back(Inst);
      }
    }
  };

  EraseInst(Vec2TileInsts);
  EraseInst(Tile2VecInsts);
  LLVM_DEBUG(dbgs() << "[LowerAMXTYpe][combineAMXcast] IR dump after combine "
                       "Vec2Tile and Tile2Vec:\n";
             Func.dump());
  Change |= combineLdSt(LiveCasts);
  EraseInst(LiveCasts);
  LLVM_DEBUG(dbgs() << "[LowerAMXTYpe][combineAMXcast] IR dump after combine "
                       "AMXCast and load/store:\n";
             Func.dump());

  // Handle the A->B->A cast, and there is an intervening PHI node.
  for (BasicBlock &BB : Func) {
    for (Instruction &I : BB) {
      if (isAMXCast(&I)) {
        if (isa<PHINode>(I.getOperand(0)))
          PhiCastWorkList.push_back(&I);
      }
    }
  }
  for (auto *I : PhiCastWorkList) {
    // We skip the dead Amxcast.
    if (DeadInst.contains(I))
      continue;
    PHINode *PN = cast<PHINode>(I->getOperand(0));
    if (optimizeAMXCastFromPhi(cast<IntrinsicInst>(I), PN, DeadInst)) {
      DeadInst.insert(PN);
      Change = true;
    }
  }

  // Since we create new phi and merge AMXCast, some old phis and AMXCast might
  // have no uses. We do some DeadCodeElimination for them.
  while (!DeadInst.empty()) {
    Instruction *I = DeadInst.pop_back_val();
    Change |= DCEInstruction(I, DeadInst, TLI);
  }
  LLVM_DEBUG(dbgs() << "[LowerAMXTYpe][combineAMXcast] IR dump after "
                       "optimizeAMXCastFromPhi:\n";
             Func.dump());
  return Change;
}

// There might be remaining AMXcast after combineAMXcast and they should be
// handled elegantly.
bool X86LowerAMXCast::transformAMXCast(IntrinsicInst *AMXCast) {
  IRBuilder<> Builder(AMXCast);
  AllocaInst *AllocaAddr;
  Value *I8Ptr, *Stride;
  auto *Src = AMXCast->getOperand(0);

  auto Prepare = [&](Type *MemTy) {
    AllocaAddr = createAllocaInstAtEntry(Builder, AMXCast->getParent(), MemTy);
    I8Ptr = Builder.CreateBitCast(AllocaAddr, Builder.getPtrTy());
    Stride = Builder.getInt64(64);
  };

  if (AMXCast->getType()->isX86_AMXTy()) {
    // %2 = amxcast <225 x i32> %src to x86_amx
    // call void @llvm.x86.tilestored64.internal(i16 15, i16 60,
    //                                           i8* %addr3, i64 60, x86_amx %2)
    // -->
    // %addr = alloca <225 x i32>, align 64
    // store <225 x i32> %src, <225 x i32>* %addr, align 64
    // %addr2 = bitcast <225 x i32>* %addr to i8*
    // %2 = call x86_amx @llvm.x86.tileloadd64.internal(i16 15, i16 60,
    //                                                  i8* %addr2,
    //                                                  i64 60)
    // call void @llvm.x86.tilestored64.internal(i16 15, i16 60,
    //                                           i8* %addr3, i64 60, x86_amx %2)
    if (AMXCast->use_empty()) {
      AMXCast->eraseFromParent();
      return true;
    }
    Use &U = *(AMXCast->use_begin());
    unsigned OpNo = U.getOperandNo();
    auto *II = dyn_cast<IntrinsicInst>(U.getUser());
    if (!II)
      return false; // May be bitcast from x86amx to <256 x i32>.
    Prepare(AMXCast->getOperand(0)->getType());
    Builder.CreateStore(Src, AllocaAddr);
    // TODO we can pick an constant operand for the shape.
    Value *Row = nullptr, *Col = nullptr;
    std::tie(Row, Col) = getShape(II, OpNo);
    std::array<Value *, 4> Args = {
        Row, Col, I8Ptr, Builder.CreateSExt(Col, Builder.getInt64Ty())};
    Value *NewInst = Builder.CreateIntrinsic(
        Intrinsic::x86_tileloadd64_internal, std::nullopt, Args);
    AMXCast->replaceAllUsesWith(NewInst);
    AMXCast->eraseFromParent();
  } else {
    // %2 = amxcast x86_amx %src to <225 x i32>
    // -->
    // %addr = alloca <225 x i32>, align 64
    // %addr2 = bitcast <225 x i32>* to i8*
    // call void @llvm.x86.tilestored64.internal(i16 %row, i16 %col,
    //                                           i8* %addr2, i64 %stride)
    // %2 = load <225 x i32>, <225 x i32>* %addr, align 64
    auto *II = dyn_cast<IntrinsicInst>(Src);
    if (!II)
      return false; // May be bitcast from <256 x i32> to x86amx.
    Prepare(AMXCast->getType());
    Value *Row = II->getOperand(0);
    Value *Col = II->getOperand(1);
    std::array<Value *, 5> Args = {
        Row, Col, I8Ptr, Builder.CreateSExt(Col, Builder.getInt64Ty()), Src};
    Builder.CreateIntrinsic(Intrinsic::x86_tilestored64_internal, std::nullopt,
                            Args);
    Value *NewInst = Builder.CreateLoad(AMXCast->getType(), AllocaAddr);
    AMXCast->replaceAllUsesWith(NewInst);
    AMXCast->eraseFromParent();
  }

  return true;
}

bool X86LowerAMXCast::transformAllAMXCast() {
  bool Change = false;
  // Collect tile cast instruction.
  SmallVector<Instruction *, 8> WorkLists;
  for (BasicBlock &BB : Func) {
    for (Instruction &I : BB) {
      if (isAMXCast(&I))
        WorkLists.push_back(&I);
    }
  }

  for (auto *Inst : WorkLists) {
    Change |= transformAMXCast(cast<IntrinsicInst>(Inst));
  }

  return Change;
}

} // anonymous namespace

namespace {

class X86LowerAMXTypeLegacyPass : public FunctionPass {
public:
  static char ID;

  X86LowerAMXTypeLegacyPass() : FunctionPass(ID) {
    initializeX86LowerAMXTypeLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    // Performance optimization: most code doesn't use AMX, so return early if
    // there are no instructions that produce AMX values. This is sufficient, as
    // AMX arguments and constants are not allowed -- so any producer of an AMX
    // value must be an instruction.
    // TODO: find a cheaper way for this, without looking at all instructions.
    if (!containsAMXCode(F))
      return false;

    bool C = false;
    TargetMachine *TM = &getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
    TargetLibraryInfo *TLI =
        &getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);

    X86LowerAMXCast LAC(F);
    C |= LAC.combineAMXcast(TLI);
    // There might be remaining AMXcast after combineAMXcast and they should be
    // handled elegantly.
    C |= LAC.transformAllAMXCast();

    X86LowerAMXType LAT(F);
    C |= LAT.visit();

    // Prepare for fast register allocation at O0.
    // Todo: May better check the volatile model of AMX code, not just
    // by checking Attribute::OptimizeNone and CodeGenOptLevel::None.
    if (TM->getOptLevel() == CodeGenOptLevel::None) {
      // If Front End not use O0 but the Mid/Back end use O0, (e.g.
      // "Clang -O2 -S -emit-llvm t.c" + "llc t.ll") we should make
      // sure the amx data is volatile, that is nessary for AMX fast
      // register allocation.
      if (!F.hasFnAttribute(Attribute::OptimizeNone)) {
        X86VolatileTileData VTD(F);
        C = VTD.volatileTileData() || C;
      }
    }

    return C;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetPassConfig>();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
  }
};

} // anonymous namespace

static const char PassName[] = "Lower AMX type for load/store";
char X86LowerAMXTypeLegacyPass::ID = 0;
INITIALIZE_PASS_BEGIN(X86LowerAMXTypeLegacyPass, DEBUG_TYPE, PassName, false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(X86LowerAMXTypeLegacyPass, DEBUG_TYPE, PassName, false,
                    false)

FunctionPass *llvm::createX86LowerAMXTypePass() {
  return new X86LowerAMXTypeLegacyPass();
}
