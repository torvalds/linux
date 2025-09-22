//===- ReduceOpcodes.cpp - Specialized Delta Pass -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Try to replace instructions that are likely to codegen to simpler or smaller
// sequences. This is a fuzzy and target specific concept.
//
//===----------------------------------------------------------------------===//

#include "ReduceOpcodes.h"
#include "Delta.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"

using namespace llvm;

// Assume outgoing undef arguments aren't relevant.
// TODO: Maybe skip any trivial constant arguments.
static bool shouldIgnoreArgument(const Value *V) {
  return isa<UndefValue>(V);
}

static Value *replaceIntrinsic(Module &M, IntrinsicInst *II,
                               Intrinsic::ID NewIID,
                               ArrayRef<Type *> Tys = std::nullopt) {
  Function *NewFunc = Intrinsic::getDeclaration(&M, NewIID, Tys);
  II->setCalledFunction(NewFunc);
  return II;
}

static Value *reduceIntrinsic(Oracle &O, Module &M, IntrinsicInst *II) {
  IRBuilder<> B(II);
  switch (II->getIntrinsicID()) {
  case Intrinsic::sqrt:
    if (O.shouldKeep())
      return nullptr;

    return B.CreateFMul(II->getArgOperand(0),
                        ConstantFP::get(II->getType(), 2.0));
  case Intrinsic::minnum:
  case Intrinsic::maxnum:
  case Intrinsic::minimum:
  case Intrinsic::maximum:
  case Intrinsic::amdgcn_fmul_legacy:
    if (O.shouldKeep())
      return nullptr;
    return B.CreateFMul(II->getArgOperand(0), II->getArgOperand(1));
  case Intrinsic::amdgcn_workitem_id_y:
  case Intrinsic::amdgcn_workitem_id_z:
    if (O.shouldKeep())
      return nullptr;
    return replaceIntrinsic(M, II, Intrinsic::amdgcn_workitem_id_x);
  case Intrinsic::amdgcn_workgroup_id_y:
  case Intrinsic::amdgcn_workgroup_id_z:
    if (O.shouldKeep())
      return nullptr;
    return replaceIntrinsic(M, II, Intrinsic::amdgcn_workgroup_id_x);
  case Intrinsic::amdgcn_div_fixup:
  case Intrinsic::amdgcn_fma_legacy:
    if (O.shouldKeep())
      return nullptr;
    return replaceIntrinsic(M, II, Intrinsic::fma, {II->getType()});
  default:
    return nullptr;
  }
}

/// Look for calls that look like they could be replaced with a load or store.
static bool callLooksLikeLoadStore(CallBase *CB, Value *&DataArg,
                                   Value *&PtrArg) {
  const bool IsStore = CB->getType()->isVoidTy();

  PtrArg = nullptr;
  DataArg = nullptr;
  for (Value *Arg : CB->args()) {
    if (shouldIgnoreArgument(Arg))
      continue;

    if (!Arg->getType()->isSized())
      return false;

    if (!PtrArg && Arg->getType()->isPointerTy()) {
      PtrArg = Arg;
      continue;
    }

    if (!IsStore || DataArg)
      return false;

    DataArg = Arg;
  }

  if (IsStore && !DataArg) {
    // FIXME: For typed pointers, use element type?
    DataArg = ConstantInt::get(IntegerType::getInt32Ty(CB->getContext()), 0);
  }

  // If we didn't find any arguments, we can fill in the pointer.
  if (!PtrArg) {
    unsigned AS = CB->getDataLayout().getAllocaAddrSpace();

    PointerType *PtrTy =
        PointerType::get(DataArg ? DataArg->getType()
                                 : IntegerType::getInt32Ty(CB->getContext()),
                         AS);

    PtrArg = ConstantPointerNull::get(PtrTy);
  }

  return true;
}

// TODO: Replace 2 pointer argument calls with memcpy
static Value *tryReplaceCallWithLoadStore(Oracle &O, Module &M, CallBase *CB) {
  Value *PtrArg = nullptr;
  Value *DataArg = nullptr;
  if (!callLooksLikeLoadStore(CB, DataArg, PtrArg) || O.shouldKeep())
    return nullptr;

  IRBuilder<> B(CB);
  if (DataArg)
    return B.CreateStore(DataArg, PtrArg, true);
  return B.CreateLoad(CB->getType(), PtrArg, true);
}

static bool callLooksLikeOperator(CallBase *CB,
                                  SmallVectorImpl<Value *> &OperatorArgs) {
  Type *ReturnTy = CB->getType();
  if (!ReturnTy->isFirstClassType())
    return false;

  for (Value *Arg : CB->args()) {
    if (shouldIgnoreArgument(Arg))
      continue;

    if (Arg->getType() != ReturnTy)
      return false;

    OperatorArgs.push_back(Arg);
  }

  return true;
}

static Value *tryReplaceCallWithOperator(Oracle &O, Module &M, CallBase *CB) {
  SmallVector<Value *, 4> Arguments;

  if (!callLooksLikeOperator(CB, Arguments) || Arguments.size() > 3)
    return nullptr;

  if (O.shouldKeep())
    return nullptr;

  IRBuilder<> B(CB);
  if (CB->getType()->isFPOrFPVectorTy()) {
    switch (Arguments.size()) {
    case 1:
      return B.CreateFNeg(Arguments[0]);
    case 2:
      return B.CreateFMul(Arguments[0], Arguments[1]);
    case 3:
      return B.CreateIntrinsic(Intrinsic::fma, {CB->getType()}, Arguments);
    default:
      return nullptr;
    }

    llvm_unreachable("all argument sizes handled");
  }

  if (CB->getType()->isIntOrIntVectorTy()) {
    switch (Arguments.size()) {
    case 1:
      return B.CreateUnaryIntrinsic(Intrinsic::bswap, Arguments[0]);
    case 2:
      return B.CreateAnd(Arguments[0], Arguments[1]);
    case 3:
      return B.CreateIntrinsic(Intrinsic::fshl, {CB->getType()}, Arguments);
    default:
      return nullptr;
    }

    llvm_unreachable("all argument sizes handled");
  }

  return nullptr;
}

static Value *reduceInstruction(Oracle &O, Module &M, Instruction &I) {
  IRBuilder<> B(&I);

  // TODO: fp binary operator with constant to fneg
  switch (I.getOpcode()) {
  case Instruction::FDiv:
  case Instruction::FRem:
    if (O.shouldKeep())
      return nullptr;

    // Divisions tends to codegen into a long sequence or a library call.
    return B.CreateFMul(I.getOperand(0), I.getOperand(1));
  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::URem:
  case Instruction::SRem:
    if (O.shouldKeep())
      return nullptr;

    // Divisions tends to codegen into a long sequence or a library call.
    return B.CreateMul(I.getOperand(0), I.getOperand(1));
  case Instruction::Add:
  case Instruction::Sub: {
    if (O.shouldKeep())
      return nullptr;

    // Add/sub are more likely codegen to instructions with carry out side
    // effects.
    return B.CreateOr(I.getOperand(0), I.getOperand(1));
  }
  case Instruction::Call: {
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I))
      return reduceIntrinsic(O, M, II);

    CallBase *CB = cast<CallBase>(&I);

    if (Value *NewOp = tryReplaceCallWithOperator(O, M, CB))
      return NewOp;

    if (Value *NewOp = tryReplaceCallWithLoadStore(O, M, CB))
      return NewOp;

    return nullptr;
  }
  default:
    return nullptr;
  }

  return nullptr;
}

static void replaceOpcodesInModule(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Mod = WorkItem.getModule();

  for (Function &F : Mod) {
    for (BasicBlock &BB : F)
      for (Instruction &I : make_early_inc_range(BB)) {
        Instruction *Replacement =
            dyn_cast_or_null<Instruction>(reduceInstruction(O, Mod, I));
        if (Replacement && Replacement != &I) {
          if (isa<FPMathOperator>(Replacement))
            Replacement->copyFastMathFlags(&I);

          Replacement->copyIRFlags(&I);
          Replacement->copyMetadata(I);
          Replacement->takeName(&I);
          I.replaceAllUsesWith(Replacement);
          I.eraseFromParent();
        }
      }
  }
}

void llvm::reduceOpcodesDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, replaceOpcodesInModule, "Reducing Opcodes");
}
