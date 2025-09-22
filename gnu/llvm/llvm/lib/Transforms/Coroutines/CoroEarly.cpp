//===- CoroEarly.cpp - Coroutine Early Function Pass ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Coroutines/CoroEarly.h"
#include "CoroInternal.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"

using namespace llvm;

#define DEBUG_TYPE "coro-early"

namespace {
// Created on demand if the coro-early pass has work to do.
class Lowerer : public coro::LowererBase {
  IRBuilder<> Builder;
  PointerType *const AnyResumeFnPtrTy;
  Constant *NoopCoro = nullptr;

  void lowerResumeOrDestroy(CallBase &CB, CoroSubFnInst::ResumeKind);
  void lowerCoroPromise(CoroPromiseInst *Intrin);
  void lowerCoroDone(IntrinsicInst *II);
  void lowerCoroNoop(IntrinsicInst *II);

public:
  Lowerer(Module &M)
      : LowererBase(M), Builder(Context),
        AnyResumeFnPtrTy(FunctionType::get(Type::getVoidTy(Context), Int8Ptr,
                                           /*isVarArg=*/false)
                             ->getPointerTo()) {}
  void lowerEarlyIntrinsics(Function &F);
};
}

// Replace a direct call to coro.resume or coro.destroy with an indirect call to
// an address returned by coro.subfn.addr intrinsic. This is done so that
// CGPassManager recognizes devirtualization when CoroElide pass replaces a call
// to coro.subfn.addr with an appropriate function address.
void Lowerer::lowerResumeOrDestroy(CallBase &CB,
                                   CoroSubFnInst::ResumeKind Index) {
  Value *ResumeAddr = makeSubFnCall(CB.getArgOperand(0), Index, &CB);
  CB.setCalledOperand(ResumeAddr);
  CB.setCallingConv(CallingConv::Fast);
}

// Coroutine promise field is always at the fixed offset from the beginning of
// the coroutine frame. i8* coro.promise(i8*, i1 from) intrinsic adds an offset
// to a passed pointer to move from coroutine frame to coroutine promise and
// vice versa. Since we don't know exactly which coroutine frame it is, we build
// a coroutine frame mock up starting with two function pointers, followed by a
// properly aligned coroutine promise field.
// TODO: Handle the case when coroutine promise alloca has align override.
void Lowerer::lowerCoroPromise(CoroPromiseInst *Intrin) {
  Value *Operand = Intrin->getArgOperand(0);
  Align Alignment = Intrin->getAlignment();
  Type *Int8Ty = Builder.getInt8Ty();

  auto *SampleStruct =
      StructType::get(Context, {AnyResumeFnPtrTy, AnyResumeFnPtrTy, Int8Ty});
  const DataLayout &DL = TheModule.getDataLayout();
  int64_t Offset = alignTo(
      DL.getStructLayout(SampleStruct)->getElementOffset(2), Alignment);
  if (Intrin->isFromPromise())
    Offset = -Offset;

  Builder.SetInsertPoint(Intrin);
  Value *Replacement =
      Builder.CreateConstInBoundsGEP1_32(Int8Ty, Operand, Offset);

  Intrin->replaceAllUsesWith(Replacement);
  Intrin->eraseFromParent();
}

// When a coroutine reaches final suspend point, it zeros out ResumeFnAddr in
// the coroutine frame (it is UB to resume from a final suspend point).
// The llvm.coro.done intrinsic is used to check whether a coroutine is
// suspended at the final suspend point or not.
void Lowerer::lowerCoroDone(IntrinsicInst *II) {
  Value *Operand = II->getArgOperand(0);

  // ResumeFnAddr is the first pointer sized element of the coroutine frame.
  static_assert(coro::Shape::SwitchFieldIndex::Resume == 0,
                "resume function not at offset zero");
  auto *FrameTy = Int8Ptr;
  PointerType *FramePtrTy = FrameTy->getPointerTo();

  Builder.SetInsertPoint(II);
  auto *BCI = Builder.CreateBitCast(Operand, FramePtrTy);
  auto *Load = Builder.CreateLoad(FrameTy, BCI);
  auto *Cond = Builder.CreateICmpEQ(Load, NullPtr);

  II->replaceAllUsesWith(Cond);
  II->eraseFromParent();
}

static void buildDebugInfoForNoopResumeDestroyFunc(Function *NoopFn) {
  Module &M = *NoopFn->getParent();
  if (M.debug_compile_units().empty())
     return;

  DICompileUnit *CU = *M.debug_compile_units_begin();
  DIBuilder DB(M, /*AllowUnresolved*/ false, CU);
  std::array<Metadata *, 2> Params{nullptr, nullptr};
  auto *SubroutineType =
      DB.createSubroutineType(DB.getOrCreateTypeArray(Params));
  StringRef Name = NoopFn->getName();
  auto *SP = DB.createFunction(
      CU, /*Name=*/Name, /*LinkageName=*/Name, /*File=*/ CU->getFile(),
      /*LineNo=*/0, SubroutineType, /*ScopeLine=*/0, DINode::FlagArtificial,
      DISubprogram::SPFlagDefinition);
  NoopFn->setSubprogram(SP);
  DB.finalize();
}

void Lowerer::lowerCoroNoop(IntrinsicInst *II) {
  if (!NoopCoro) {
    LLVMContext &C = Builder.getContext();
    Module &M = *II->getModule();

    // Create a noop.frame struct type.
    StructType *FrameTy = StructType::create(C, "NoopCoro.Frame");
    auto *FramePtrTy = FrameTy->getPointerTo();
    auto *FnTy = FunctionType::get(Type::getVoidTy(C), FramePtrTy,
                                   /*isVarArg=*/false);
    auto *FnPtrTy = FnTy->getPointerTo();
    FrameTy->setBody({FnPtrTy, FnPtrTy});

    // Create a Noop function that does nothing.
    Function *NoopFn =
        Function::Create(FnTy, GlobalValue::LinkageTypes::PrivateLinkage,
                         "__NoopCoro_ResumeDestroy", &M);
    NoopFn->setCallingConv(CallingConv::Fast);
    buildDebugInfoForNoopResumeDestroyFunc(NoopFn);
    auto *Entry = BasicBlock::Create(C, "entry", NoopFn);
    ReturnInst::Create(C, Entry);

    // Create a constant struct for the frame.
    Constant* Values[] = {NoopFn, NoopFn};
    Constant* NoopCoroConst = ConstantStruct::get(FrameTy, Values);
    NoopCoro = new GlobalVariable(M, NoopCoroConst->getType(), /*isConstant=*/true,
                                GlobalVariable::PrivateLinkage, NoopCoroConst,
                                "NoopCoro.Frame.Const");
    cast<GlobalVariable>(NoopCoro)->setNoSanitizeMetadata();
  }

  Builder.SetInsertPoint(II);
  auto *NoopCoroVoidPtr = Builder.CreateBitCast(NoopCoro, Int8Ptr);
  II->replaceAllUsesWith(NoopCoroVoidPtr);
  II->eraseFromParent();
}

// Prior to CoroSplit, calls to coro.begin needs to be marked as NoDuplicate,
// as CoroSplit assumes there is exactly one coro.begin. After CoroSplit,
// NoDuplicate attribute will be removed from coro.begin otherwise, it will
// interfere with inlining.
static void setCannotDuplicate(CoroIdInst *CoroId) {
  for (User *U : CoroId->users())
    if (auto *CB = dyn_cast<CoroBeginInst>(U))
      CB->setCannotDuplicate();
}

void Lowerer::lowerEarlyIntrinsics(Function &F) {
  CoroIdInst *CoroId = nullptr;
  SmallVector<CoroFreeInst *, 4> CoroFrees;
  bool HasCoroSuspend = false;
  for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
    auto *CB = dyn_cast<CallBase>(&I);
    if (!CB)
      continue;

    switch (CB->getIntrinsicID()) {
      default:
        continue;
      case Intrinsic::coro_free:
        CoroFrees.push_back(cast<CoroFreeInst>(&I));
        break;
      case Intrinsic::coro_suspend:
        // Make sure that final suspend point is not duplicated as CoroSplit
        // pass expects that there is at most one final suspend point.
        if (cast<CoroSuspendInst>(&I)->isFinal())
          CB->setCannotDuplicate();
        HasCoroSuspend = true;
        break;
      case Intrinsic::coro_end_async:
      case Intrinsic::coro_end:
        // Make sure that fallthrough coro.end is not duplicated as CoroSplit
        // pass expects that there is at most one fallthrough coro.end.
        if (cast<AnyCoroEndInst>(&I)->isFallthrough())
          CB->setCannotDuplicate();
        break;
      case Intrinsic::coro_noop:
        lowerCoroNoop(cast<IntrinsicInst>(&I));
        break;
      case Intrinsic::coro_id:
        if (auto *CII = cast<CoroIdInst>(&I)) {
          if (CII->getInfo().isPreSplit()) {
            assert(F.isPresplitCoroutine() &&
                   "The frontend uses Swtich-Resumed ABI should emit "
                   "\"presplitcoroutine\" attribute for the coroutine.");
            setCannotDuplicate(CII);
            CII->setCoroutineSelf();
            CoroId = cast<CoroIdInst>(&I);
          }
        }
        break;
      case Intrinsic::coro_id_retcon:
      case Intrinsic::coro_id_retcon_once:
      case Intrinsic::coro_id_async:
        F.setPresplitCoroutine();
        break;
      case Intrinsic::coro_resume:
        lowerResumeOrDestroy(*CB, CoroSubFnInst::ResumeIndex);
        break;
      case Intrinsic::coro_destroy:
        lowerResumeOrDestroy(*CB, CoroSubFnInst::DestroyIndex);
        break;
      case Intrinsic::coro_promise:
        lowerCoroPromise(cast<CoroPromiseInst>(&I));
        break;
      case Intrinsic::coro_done:
        lowerCoroDone(cast<IntrinsicInst>(&I));
        break;
    }
  }

  // Make sure that all CoroFree reference the coro.id intrinsic.
  // Token type is not exposed through coroutine C/C++ builtins to plain C, so
  // we allow specifying none and fixing it up here.
  if (CoroId)
    for (CoroFreeInst *CF : CoroFrees)
      CF->setArgOperand(0, CoroId);

  // Coroutine suspention could potentially lead to any argument modified
  // outside of the function, hence arguments should not have noalias
  // attributes.
  if (HasCoroSuspend)
    for (Argument &A : F.args())
      if (A.hasNoAliasAttr())
        A.removeAttr(Attribute::NoAlias);
}

static bool declaresCoroEarlyIntrinsics(const Module &M) {
  return coro::declaresIntrinsics(
      M, {"llvm.coro.id", "llvm.coro.id.retcon", "llvm.coro.id.retcon.once",
          "llvm.coro.id.async", "llvm.coro.destroy", "llvm.coro.done",
          "llvm.coro.end", "llvm.coro.end.async", "llvm.coro.noop",
          "llvm.coro.free", "llvm.coro.promise", "llvm.coro.resume",
          "llvm.coro.suspend"});
}

PreservedAnalyses CoroEarlyPass::run(Module &M, ModuleAnalysisManager &) {
  if (!declaresCoroEarlyIntrinsics(M))
    return PreservedAnalyses::all();

  Lowerer L(M);
  for (auto &F : M)
    L.lowerEarlyIntrinsics(F);

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
