//===- CoroSplit.cpp - Converts a coroutine into a state machine ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This pass builds the coroutine frame and outlines resume and destroy parts
// of the coroutine into separate functions.
//
// We present a coroutine to an LLVM as an ordinary function with suspension
// points marked up with intrinsics. We let the optimizer party on the coroutine
// as a single function for as long as possible. Shortly before the coroutine is
// eligible to be inlined into its callers, we split up the coroutine into parts
// corresponding to an initial, resume and destroy invocations of the coroutine,
// add them to the current SCC and restart the IPO pipeline to optimize the
// coroutine subfunctions we extracted before proceeding to the caller of the
// coroutine.
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Coroutines/CoroSplit.h"
#include "CoroInstr.h"
#include "CoroInternal.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/CallGraphUpdater.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "coro-split"

namespace {

/// A little helper class for building
class CoroCloner {
public:
  enum class Kind {
    /// The shared resume function for a switch lowering.
    SwitchResume,

    /// The shared unwind function for a switch lowering.
    SwitchUnwind,

    /// The shared cleanup function for a switch lowering.
    SwitchCleanup,

    /// An individual continuation function.
    Continuation,

    /// An async resume function.
    Async,
  };

private:
  Function &OrigF;
  Function *NewF;
  const Twine &Suffix;
  coro::Shape &Shape;
  Kind FKind;
  ValueToValueMapTy VMap;
  IRBuilder<> Builder;
  Value *NewFramePtr = nullptr;

  /// The active suspend instruction; meaningful only for continuation and async
  /// ABIs.
  AnyCoroSuspendInst *ActiveSuspend = nullptr;

  TargetTransformInfo &TTI;

public:
  /// Create a cloner for a switch lowering.
  CoroCloner(Function &OrigF, const Twine &Suffix, coro::Shape &Shape,
             Kind FKind, TargetTransformInfo &TTI)
      : OrigF(OrigF), NewF(nullptr), Suffix(Suffix), Shape(Shape), FKind(FKind),
        Builder(OrigF.getContext()), TTI(TTI) {
    assert(Shape.ABI == coro::ABI::Switch);
  }

  /// Create a cloner for a continuation lowering.
  CoroCloner(Function &OrigF, const Twine &Suffix, coro::Shape &Shape,
             Function *NewF, AnyCoroSuspendInst *ActiveSuspend,
             TargetTransformInfo &TTI)
      : OrigF(OrigF), NewF(NewF), Suffix(Suffix), Shape(Shape),
        FKind(Shape.ABI == coro::ABI::Async ? Kind::Async : Kind::Continuation),
        Builder(OrigF.getContext()), ActiveSuspend(ActiveSuspend), TTI(TTI) {
    assert(Shape.ABI == coro::ABI::Retcon ||
           Shape.ABI == coro::ABI::RetconOnce || Shape.ABI == coro::ABI::Async);
    assert(NewF && "need existing function for continuation");
    assert(ActiveSuspend && "need active suspend point for continuation");
  }

  Function *getFunction() const {
    assert(NewF != nullptr && "declaration not yet set");
    return NewF;
  }

  void create();

private:
  bool isSwitchDestroyFunction() {
    switch (FKind) {
    case Kind::Async:
    case Kind::Continuation:
    case Kind::SwitchResume:
      return false;
    case Kind::SwitchUnwind:
    case Kind::SwitchCleanup:
      return true;
    }
    llvm_unreachable("Unknown CoroCloner::Kind enum");
  }

  void replaceEntryBlock();
  Value *deriveNewFramePointer();
  void replaceRetconOrAsyncSuspendUses();
  void replaceCoroSuspends();
  void replaceCoroEnds();
  void replaceSwiftErrorOps();
  void salvageDebugInfo();
  void handleFinalSuspend();
};

} // end anonymous namespace

// FIXME:
// Lower the intrinisc in CoroEarly phase if coroutine frame doesn't escape
// and it is known that other transformations, for example, sanitizers
// won't lead to incorrect code.
static void lowerAwaitSuspend(IRBuilder<> &Builder, CoroAwaitSuspendInst *CB,
                              coro::Shape &Shape) {
  auto Wrapper = CB->getWrapperFunction();
  auto Awaiter = CB->getAwaiter();
  auto FramePtr = CB->getFrame();

  Builder.SetInsertPoint(CB);

  CallBase *NewCall = nullptr;
  // await_suspend has only 2 parameters, awaiter and handle.
  // Copy parameter attributes from the intrinsic call, but remove the last,
  // because the last parameter now becomes the function that is being called.
  AttributeList NewAttributes =
      CB->getAttributes().removeParamAttributes(CB->getContext(), 2);

  if (auto Invoke = dyn_cast<InvokeInst>(CB)) {
    auto WrapperInvoke =
        Builder.CreateInvoke(Wrapper, Invoke->getNormalDest(),
                             Invoke->getUnwindDest(), {Awaiter, FramePtr});

    WrapperInvoke->setCallingConv(Invoke->getCallingConv());
    std::copy(Invoke->bundle_op_info_begin(), Invoke->bundle_op_info_end(),
              WrapperInvoke->bundle_op_info_begin());
    WrapperInvoke->setAttributes(NewAttributes);
    WrapperInvoke->setDebugLoc(Invoke->getDebugLoc());
    NewCall = WrapperInvoke;
  } else if (auto Call = dyn_cast<CallInst>(CB)) {
    auto WrapperCall = Builder.CreateCall(Wrapper, {Awaiter, FramePtr});

    WrapperCall->setAttributes(NewAttributes);
    WrapperCall->setDebugLoc(Call->getDebugLoc());
    NewCall = WrapperCall;
  } else {
    llvm_unreachable("Unexpected coro_await_suspend invocation method");
  }

  if (CB->getCalledFunction()->getIntrinsicID() ==
      Intrinsic::coro_await_suspend_handle) {
    // Follow the lowered await_suspend call above with a lowered resume call
    // to the returned coroutine.
    if (auto *Invoke = dyn_cast<InvokeInst>(CB)) {
      // If the await_suspend call is an invoke, we continue in the next block.
      Builder.SetInsertPoint(Invoke->getNormalDest()->getFirstInsertionPt());
    }

    coro::LowererBase LB(*Wrapper->getParent());
    auto *ResumeAddr = LB.makeSubFnCall(NewCall, CoroSubFnInst::ResumeIndex,
                                        &*Builder.GetInsertPoint());

    LLVMContext &Ctx = Builder.getContext();
    FunctionType *ResumeTy = FunctionType::get(
        Type::getVoidTy(Ctx), PointerType::getUnqual(Ctx), false);
    auto *ResumeCall = Builder.CreateCall(ResumeTy, ResumeAddr, {NewCall});
    ResumeCall->setCallingConv(CallingConv::Fast);

    // We can't insert the 'ret' instruction and adjust the cc until the
    // function has been split, so remember this for later.
    Shape.SymmetricTransfers.push_back(ResumeCall);

    NewCall = ResumeCall;
  }

  CB->replaceAllUsesWith(NewCall);
  CB->eraseFromParent();
}

static void lowerAwaitSuspends(Function &F, coro::Shape &Shape) {
  IRBuilder<> Builder(F.getContext());
  for (auto *AWS : Shape.CoroAwaitSuspends)
    lowerAwaitSuspend(Builder, AWS, Shape);
}

static void maybeFreeRetconStorage(IRBuilder<> &Builder,
                                   const coro::Shape &Shape, Value *FramePtr,
                                   CallGraph *CG) {
  assert(Shape.ABI == coro::ABI::Retcon || Shape.ABI == coro::ABI::RetconOnce);
  if (Shape.RetconLowering.IsFrameInlineInStorage)
    return;

  Shape.emitDealloc(Builder, FramePtr, CG);
}

/// Replace an llvm.coro.end.async.
/// Will inline the must tail call function call if there is one.
/// \returns true if cleanup of the coro.end block is needed, false otherwise.
static bool replaceCoroEndAsync(AnyCoroEndInst *End) {
  IRBuilder<> Builder(End);

  auto *EndAsync = dyn_cast<CoroAsyncEndInst>(End);
  if (!EndAsync) {
    Builder.CreateRetVoid();
    return true /*needs cleanup of coro.end block*/;
  }

  auto *MustTailCallFunc = EndAsync->getMustTailCallFunction();
  if (!MustTailCallFunc) {
    Builder.CreateRetVoid();
    return true /*needs cleanup of coro.end block*/;
  }

  // Move the must tail call from the predecessor block into the end block.
  auto *CoroEndBlock = End->getParent();
  auto *MustTailCallFuncBlock = CoroEndBlock->getSinglePredecessor();
  assert(MustTailCallFuncBlock && "Must have a single predecessor block");
  auto It = MustTailCallFuncBlock->getTerminator()->getIterator();
  auto *MustTailCall = cast<CallInst>(&*std::prev(It));
  CoroEndBlock->splice(End->getIterator(), MustTailCallFuncBlock,
                       MustTailCall->getIterator());

  // Insert the return instruction.
  Builder.SetInsertPoint(End);
  Builder.CreateRetVoid();
  InlineFunctionInfo FnInfo;

  // Remove the rest of the block, by splitting it into an unreachable block.
  auto *BB = End->getParent();
  BB->splitBasicBlock(End);
  BB->getTerminator()->eraseFromParent();

  auto InlineRes = InlineFunction(*MustTailCall, FnInfo);
  assert(InlineRes.isSuccess() && "Expected inlining to succeed");
  (void)InlineRes;

  // We have cleaned up the coro.end block above.
  return false;
}

/// Replace a non-unwind call to llvm.coro.end.
static void replaceFallthroughCoroEnd(AnyCoroEndInst *End,
                                      const coro::Shape &Shape, Value *FramePtr,
                                      bool InResume, CallGraph *CG) {
  // Start inserting right before the coro.end.
  IRBuilder<> Builder(End);

  // Create the return instruction.
  switch (Shape.ABI) {
  // The cloned functions in switch-lowering always return void.
  case coro::ABI::Switch:
    assert(!cast<CoroEndInst>(End)->hasResults() &&
           "switch coroutine should not return any values");
    // coro.end doesn't immediately end the coroutine in the main function
    // in this lowering, because we need to deallocate the coroutine.
    if (!InResume)
      return;
    Builder.CreateRetVoid();
    break;

  // In async lowering this returns.
  case coro::ABI::Async: {
    bool CoroEndBlockNeedsCleanup = replaceCoroEndAsync(End);
    if (!CoroEndBlockNeedsCleanup)
      return;
    break;
  }

  // In unique continuation lowering, the continuations always return void.
  // But we may have implicitly allocated storage.
  case coro::ABI::RetconOnce: {
    maybeFreeRetconStorage(Builder, Shape, FramePtr, CG);
    auto *CoroEnd = cast<CoroEndInst>(End);
    auto *RetTy = Shape.getResumeFunctionType()->getReturnType();

    if (!CoroEnd->hasResults()) {
      assert(RetTy->isVoidTy());
      Builder.CreateRetVoid();
      break;
    }

    auto *CoroResults = CoroEnd->getResults();
    unsigned NumReturns = CoroResults->numReturns();

    if (auto *RetStructTy = dyn_cast<StructType>(RetTy)) {
      assert(RetStructTy->getNumElements() == NumReturns &&
             "numbers of returns should match resume function singature");
      Value *ReturnValue = UndefValue::get(RetStructTy);
      unsigned Idx = 0;
      for (Value *RetValEl : CoroResults->return_values())
        ReturnValue = Builder.CreateInsertValue(ReturnValue, RetValEl, Idx++);
      Builder.CreateRet(ReturnValue);
    } else if (NumReturns == 0) {
      assert(RetTy->isVoidTy());
      Builder.CreateRetVoid();
    } else {
      assert(NumReturns == 1);
      Builder.CreateRet(*CoroResults->retval_begin());
    }
    CoroResults->replaceAllUsesWith(
        ConstantTokenNone::get(CoroResults->getContext()));
    CoroResults->eraseFromParent();
    break;
  }

  // In non-unique continuation lowering, we signal completion by returning
  // a null continuation.
  case coro::ABI::Retcon: {
    assert(!cast<CoroEndInst>(End)->hasResults() &&
           "retcon coroutine should not return any values");
    maybeFreeRetconStorage(Builder, Shape, FramePtr, CG);
    auto RetTy = Shape.getResumeFunctionType()->getReturnType();
    auto RetStructTy = dyn_cast<StructType>(RetTy);
    PointerType *ContinuationTy =
        cast<PointerType>(RetStructTy ? RetStructTy->getElementType(0) : RetTy);

    Value *ReturnValue = ConstantPointerNull::get(ContinuationTy);
    if (RetStructTy) {
      ReturnValue = Builder.CreateInsertValue(UndefValue::get(RetStructTy),
                                              ReturnValue, 0);
    }
    Builder.CreateRet(ReturnValue);
    break;
  }
  }

  // Remove the rest of the block, by splitting it into an unreachable block.
  auto *BB = End->getParent();
  BB->splitBasicBlock(End);
  BB->getTerminator()->eraseFromParent();
}

// Mark a coroutine as done, which implies that the coroutine is finished and
// never get resumed.
//
// In resume-switched ABI, the done state is represented by storing zero in
// ResumeFnAddr.
//
// NOTE: We couldn't omit the argument `FramePtr`. It is necessary because the
// pointer to the frame in splitted function is not stored in `Shape`.
static void markCoroutineAsDone(IRBuilder<> &Builder, const coro::Shape &Shape,
                                Value *FramePtr) {
  assert(
      Shape.ABI == coro::ABI::Switch &&
      "markCoroutineAsDone is only supported for Switch-Resumed ABI for now.");
  auto *GepIndex = Builder.CreateStructGEP(
      Shape.FrameTy, FramePtr, coro::Shape::SwitchFieldIndex::Resume,
      "ResumeFn.addr");
  auto *NullPtr = ConstantPointerNull::get(cast<PointerType>(
      Shape.FrameTy->getTypeAtIndex(coro::Shape::SwitchFieldIndex::Resume)));
  Builder.CreateStore(NullPtr, GepIndex);

  // If the coroutine don't have unwind coro end, we could omit the store to
  // the final suspend point since we could infer the coroutine is suspended
  // at the final suspend point by the nullness of ResumeFnAddr.
  // However, we can't skip it if the coroutine have unwind coro end. Since
  // the coroutine reaches unwind coro end is considered suspended at the
  // final suspend point (the ResumeFnAddr is null) but in fact the coroutine
  // didn't complete yet. We need the IndexVal for the final suspend point
  // to make the states clear.
  if (Shape.SwitchLowering.HasUnwindCoroEnd &&
      Shape.SwitchLowering.HasFinalSuspend) {
    assert(cast<CoroSuspendInst>(Shape.CoroSuspends.back())->isFinal() &&
           "The final suspend should only live in the last position of "
           "CoroSuspends.");
    ConstantInt *IndexVal = Shape.getIndex(Shape.CoroSuspends.size() - 1);
    auto *FinalIndex = Builder.CreateStructGEP(
        Shape.FrameTy, FramePtr, Shape.getSwitchIndexField(), "index.addr");

    Builder.CreateStore(IndexVal, FinalIndex);
  }
}

/// Replace an unwind call to llvm.coro.end.
static void replaceUnwindCoroEnd(AnyCoroEndInst *End, const coro::Shape &Shape,
                                 Value *FramePtr, bool InResume,
                                 CallGraph *CG) {
  IRBuilder<> Builder(End);

  switch (Shape.ABI) {
  // In switch-lowering, this does nothing in the main function.
  case coro::ABI::Switch: {
    // In C++'s specification, the coroutine should be marked as done
    // if promise.unhandled_exception() throws.  The frontend will
    // call coro.end(true) along this path.
    //
    // FIXME: We should refactor this once there is other language
    // which uses Switch-Resumed style other than C++.
    markCoroutineAsDone(Builder, Shape, FramePtr);
    if (!InResume)
      return;
    break;
  }
  // In async lowering this does nothing.
  case coro::ABI::Async:
    break;
  // In continuation-lowering, this frees the continuation storage.
  case coro::ABI::Retcon:
  case coro::ABI::RetconOnce:
    maybeFreeRetconStorage(Builder, Shape, FramePtr, CG);
    break;
  }

  // If coro.end has an associated bundle, add cleanupret instruction.
  if (auto Bundle = End->getOperandBundle(LLVMContext::OB_funclet)) {
    auto *FromPad = cast<CleanupPadInst>(Bundle->Inputs[0]);
    auto *CleanupRet = Builder.CreateCleanupRet(FromPad, nullptr);
    End->getParent()->splitBasicBlock(End);
    CleanupRet->getParent()->getTerminator()->eraseFromParent();
  }
}

static void replaceCoroEnd(AnyCoroEndInst *End, const coro::Shape &Shape,
                           Value *FramePtr, bool InResume, CallGraph *CG) {
  if (End->isUnwind())
    replaceUnwindCoroEnd(End, Shape, FramePtr, InResume, CG);
  else
    replaceFallthroughCoroEnd(End, Shape, FramePtr, InResume, CG);

  auto &Context = End->getContext();
  End->replaceAllUsesWith(InResume ? ConstantInt::getTrue(Context)
                                   : ConstantInt::getFalse(Context));
  End->eraseFromParent();
}

// In the resume function, we remove the last case  (when coro::Shape is built,
// the final suspend point (if present) is always the last element of
// CoroSuspends array) since it is an undefined behavior to resume a coroutine
// suspended at the final suspend point.
// In the destroy function, if it isn't possible that the ResumeFnAddr is NULL
// and the coroutine doesn't suspend at the final suspend point actually (this
// is possible since the coroutine is considered suspended at the final suspend
// point if promise.unhandled_exception() exits via an exception), we can
// remove the last case.
void CoroCloner::handleFinalSuspend() {
  assert(Shape.ABI == coro::ABI::Switch &&
         Shape.SwitchLowering.HasFinalSuspend);

  if (isSwitchDestroyFunction() && Shape.SwitchLowering.HasUnwindCoroEnd)
    return;

  auto *Switch = cast<SwitchInst>(VMap[Shape.SwitchLowering.ResumeSwitch]);
  auto FinalCaseIt = std::prev(Switch->case_end());
  BasicBlock *ResumeBB = FinalCaseIt->getCaseSuccessor();
  Switch->removeCase(FinalCaseIt);
  if (isSwitchDestroyFunction()) {
    BasicBlock *OldSwitchBB = Switch->getParent();
    auto *NewSwitchBB = OldSwitchBB->splitBasicBlock(Switch, "Switch");
    Builder.SetInsertPoint(OldSwitchBB->getTerminator());

    if (NewF->isCoroOnlyDestroyWhenComplete()) {
      // When the coroutine can only be destroyed when complete, we don't need
      // to generate code for other cases.
      Builder.CreateBr(ResumeBB);
    } else {
      auto *GepIndex = Builder.CreateStructGEP(
          Shape.FrameTy, NewFramePtr, coro::Shape::SwitchFieldIndex::Resume,
          "ResumeFn.addr");
      auto *Load =
          Builder.CreateLoad(Shape.getSwitchResumePointerType(), GepIndex);
      auto *Cond = Builder.CreateIsNull(Load);
      Builder.CreateCondBr(Cond, ResumeBB, NewSwitchBB);
    }
    OldSwitchBB->getTerminator()->eraseFromParent();
  }
}

static FunctionType *
getFunctionTypeFromAsyncSuspend(AnyCoroSuspendInst *Suspend) {
  auto *AsyncSuspend = cast<CoroSuspendAsyncInst>(Suspend);
  auto *StructTy = cast<StructType>(AsyncSuspend->getType());
  auto &Context = Suspend->getParent()->getParent()->getContext();
  auto *VoidTy = Type::getVoidTy(Context);
  return FunctionType::get(VoidTy, StructTy->elements(), false);
}

static Function *createCloneDeclaration(Function &OrigF, coro::Shape &Shape,
                                        const Twine &Suffix,
                                        Module::iterator InsertBefore,
                                        AnyCoroSuspendInst *ActiveSuspend) {
  Module *M = OrigF.getParent();
  auto *FnTy = (Shape.ABI != coro::ABI::Async)
                   ? Shape.getResumeFunctionType()
                   : getFunctionTypeFromAsyncSuspend(ActiveSuspend);

  Function *NewF =
      Function::Create(FnTy, GlobalValue::LinkageTypes::InternalLinkage,
                       OrigF.getName() + Suffix);

  M->getFunctionList().insert(InsertBefore, NewF);

  return NewF;
}

/// Replace uses of the active llvm.coro.suspend.retcon/async call with the
/// arguments to the continuation function.
///
/// This assumes that the builder has a meaningful insertion point.
void CoroCloner::replaceRetconOrAsyncSuspendUses() {
  assert(Shape.ABI == coro::ABI::Retcon || Shape.ABI == coro::ABI::RetconOnce ||
         Shape.ABI == coro::ABI::Async);

  auto NewS = VMap[ActiveSuspend];
  if (NewS->use_empty())
    return;

  // Copy out all the continuation arguments after the buffer pointer into
  // an easily-indexed data structure for convenience.
  SmallVector<Value *, 8> Args;
  // The async ABI includes all arguments -- including the first argument.
  bool IsAsyncABI = Shape.ABI == coro::ABI::Async;
  for (auto I = IsAsyncABI ? NewF->arg_begin() : std::next(NewF->arg_begin()),
            E = NewF->arg_end();
       I != E; ++I)
    Args.push_back(&*I);

  // If the suspend returns a single scalar value, we can just do a simple
  // replacement.
  if (!isa<StructType>(NewS->getType())) {
    assert(Args.size() == 1);
    NewS->replaceAllUsesWith(Args.front());
    return;
  }

  // Try to peephole extracts of an aggregate return.
  for (Use &U : llvm::make_early_inc_range(NewS->uses())) {
    auto *EVI = dyn_cast<ExtractValueInst>(U.getUser());
    if (!EVI || EVI->getNumIndices() != 1)
      continue;

    EVI->replaceAllUsesWith(Args[EVI->getIndices().front()]);
    EVI->eraseFromParent();
  }

  // If we have no remaining uses, we're done.
  if (NewS->use_empty())
    return;

  // Otherwise, we need to create an aggregate.
  Value *Agg = PoisonValue::get(NewS->getType());
  for (size_t I = 0, E = Args.size(); I != E; ++I)
    Agg = Builder.CreateInsertValue(Agg, Args[I], I);

  NewS->replaceAllUsesWith(Agg);
}

void CoroCloner::replaceCoroSuspends() {
  Value *SuspendResult;

  switch (Shape.ABI) {
  // In switch lowering, replace coro.suspend with the appropriate value
  // for the type of function we're extracting.
  // Replacing coro.suspend with (0) will result in control flow proceeding to
  // a resume label associated with a suspend point, replacing it with (1) will
  // result in control flow proceeding to a cleanup label associated with this
  // suspend point.
  case coro::ABI::Switch:
    SuspendResult = Builder.getInt8(isSwitchDestroyFunction() ? 1 : 0);
    break;

  // In async lowering there are no uses of the result.
  case coro::ABI::Async:
    return;

  // In returned-continuation lowering, the arguments from earlier
  // continuations are theoretically arbitrary, and they should have been
  // spilled.
  case coro::ABI::RetconOnce:
  case coro::ABI::Retcon:
    return;
  }

  for (AnyCoroSuspendInst *CS : Shape.CoroSuspends) {
    // The active suspend was handled earlier.
    if (CS == ActiveSuspend)
      continue;

    auto *MappedCS = cast<AnyCoroSuspendInst>(VMap[CS]);
    MappedCS->replaceAllUsesWith(SuspendResult);
    MappedCS->eraseFromParent();
  }
}

void CoroCloner::replaceCoroEnds() {
  for (AnyCoroEndInst *CE : Shape.CoroEnds) {
    // We use a null call graph because there's no call graph node for
    // the cloned function yet.  We'll just be rebuilding that later.
    auto *NewCE = cast<AnyCoroEndInst>(VMap[CE]);
    replaceCoroEnd(NewCE, Shape, NewFramePtr, /*in resume*/ true, nullptr);
  }
}

static void replaceSwiftErrorOps(Function &F, coro::Shape &Shape,
                                 ValueToValueMapTy *VMap) {
  if (Shape.ABI == coro::ABI::Async && Shape.CoroSuspends.empty())
    return;
  Value *CachedSlot = nullptr;
  auto getSwiftErrorSlot = [&](Type *ValueTy) -> Value * {
    if (CachedSlot)
      return CachedSlot;

    // Check if the function has a swifterror argument.
    for (auto &Arg : F.args()) {
      if (Arg.isSwiftError()) {
        CachedSlot = &Arg;
        return &Arg;
      }
    }

    // Create a swifterror alloca.
    IRBuilder<> Builder(F.getEntryBlock().getFirstNonPHIOrDbg());
    auto Alloca = Builder.CreateAlloca(ValueTy);
    Alloca->setSwiftError(true);

    CachedSlot = Alloca;
    return Alloca;
  };

  for (CallInst *Op : Shape.SwiftErrorOps) {
    auto MappedOp = VMap ? cast<CallInst>((*VMap)[Op]) : Op;
    IRBuilder<> Builder(MappedOp);

    // If there are no arguments, this is a 'get' operation.
    Value *MappedResult;
    if (Op->arg_empty()) {
      auto ValueTy = Op->getType();
      auto Slot = getSwiftErrorSlot(ValueTy);
      MappedResult = Builder.CreateLoad(ValueTy, Slot);
    } else {
      assert(Op->arg_size() == 1);
      auto Value = MappedOp->getArgOperand(0);
      auto ValueTy = Value->getType();
      auto Slot = getSwiftErrorSlot(ValueTy);
      Builder.CreateStore(Value, Slot);
      MappedResult = Slot;
    }

    MappedOp->replaceAllUsesWith(MappedResult);
    MappedOp->eraseFromParent();
  }

  // If we're updating the original function, we've invalidated SwiftErrorOps.
  if (VMap == nullptr) {
    Shape.SwiftErrorOps.clear();
  }
}

/// Returns all DbgVariableIntrinsic in F.
static std::pair<SmallVector<DbgVariableIntrinsic *, 8>,
                 SmallVector<DbgVariableRecord *>>
collectDbgVariableIntrinsics(Function &F) {
  SmallVector<DbgVariableIntrinsic *, 8> Intrinsics;
  SmallVector<DbgVariableRecord *> DbgVariableRecords;
  for (auto &I : instructions(F)) {
    for (DbgVariableRecord &DVR : filterDbgVars(I.getDbgRecordRange()))
      DbgVariableRecords.push_back(&DVR);
    if (auto *DVI = dyn_cast<DbgVariableIntrinsic>(&I))
      Intrinsics.push_back(DVI);
  }
  return {Intrinsics, DbgVariableRecords};
}

void CoroCloner::replaceSwiftErrorOps() {
  ::replaceSwiftErrorOps(*NewF, Shape, &VMap);
}

void CoroCloner::salvageDebugInfo() {
  auto [Worklist, DbgVariableRecords] = collectDbgVariableIntrinsics(*NewF);
  SmallDenseMap<Argument *, AllocaInst *, 4> ArgToAllocaMap;

  // Only 64-bit ABIs have a register we can refer to with the entry value.
  bool UseEntryValue =
      llvm::Triple(OrigF.getParent()->getTargetTriple()).isArch64Bit();
  for (DbgVariableIntrinsic *DVI : Worklist)
    coro::salvageDebugInfo(ArgToAllocaMap, *DVI, Shape.OptimizeFrame,
                           UseEntryValue);
  for (DbgVariableRecord *DVR : DbgVariableRecords)
    coro::salvageDebugInfo(ArgToAllocaMap, *DVR, Shape.OptimizeFrame,
                           UseEntryValue);

  // Remove all salvaged dbg.declare intrinsics that became
  // either unreachable or stale due to the CoroSplit transformation.
  DominatorTree DomTree(*NewF);
  auto IsUnreachableBlock = [&](BasicBlock *BB) {
    return !isPotentiallyReachable(&NewF->getEntryBlock(), BB, nullptr,
                                   &DomTree);
  };
  auto RemoveOne = [&](auto *DVI) {
    if (IsUnreachableBlock(DVI->getParent()))
      DVI->eraseFromParent();
    else if (isa_and_nonnull<AllocaInst>(DVI->getVariableLocationOp(0))) {
      // Count all non-debuginfo uses in reachable blocks.
      unsigned Uses = 0;
      for (auto *User : DVI->getVariableLocationOp(0)->users())
        if (auto *I = dyn_cast<Instruction>(User))
          if (!isa<AllocaInst>(I) && !IsUnreachableBlock(I->getParent()))
            ++Uses;
      if (!Uses)
        DVI->eraseFromParent();
    }
  };
  for_each(Worklist, RemoveOne);
  for_each(DbgVariableRecords, RemoveOne);
}

void CoroCloner::replaceEntryBlock() {
  // In the original function, the AllocaSpillBlock is a block immediately
  // following the allocation of the frame object which defines GEPs for
  // all the allocas that have been moved into the frame, and it ends by
  // branching to the original beginning of the coroutine.  Make this
  // the entry block of the cloned function.
  auto *Entry = cast<BasicBlock>(VMap[Shape.AllocaSpillBlock]);
  auto *OldEntry = &NewF->getEntryBlock();
  Entry->setName("entry" + Suffix);
  Entry->moveBefore(OldEntry);
  Entry->getTerminator()->eraseFromParent();

  // Clear all predecessors of the new entry block.  There should be
  // exactly one predecessor, which we created when splitting out
  // AllocaSpillBlock to begin with.
  assert(Entry->hasOneUse());
  auto BranchToEntry = cast<BranchInst>(Entry->user_back());
  assert(BranchToEntry->isUnconditional());
  Builder.SetInsertPoint(BranchToEntry);
  Builder.CreateUnreachable();
  BranchToEntry->eraseFromParent();

  // Branch from the entry to the appropriate place.
  Builder.SetInsertPoint(Entry);
  switch (Shape.ABI) {
  case coro::ABI::Switch: {
    // In switch-lowering, we built a resume-entry block in the original
    // function.  Make the entry block branch to this.
    auto *SwitchBB =
        cast<BasicBlock>(VMap[Shape.SwitchLowering.ResumeEntryBlock]);
    Builder.CreateBr(SwitchBB);
    break;
  }
  case coro::ABI::Async:
  case coro::ABI::Retcon:
  case coro::ABI::RetconOnce: {
    // In continuation ABIs, we want to branch to immediately after the
    // active suspend point.  Earlier phases will have put the suspend in its
    // own basic block, so just thread our jump directly to its successor.
    assert((Shape.ABI == coro::ABI::Async &&
            isa<CoroSuspendAsyncInst>(ActiveSuspend)) ||
           ((Shape.ABI == coro::ABI::Retcon ||
             Shape.ABI == coro::ABI::RetconOnce) &&
            isa<CoroSuspendRetconInst>(ActiveSuspend)));
    auto *MappedCS = cast<AnyCoroSuspendInst>(VMap[ActiveSuspend]);
    auto Branch = cast<BranchInst>(MappedCS->getNextNode());
    assert(Branch->isUnconditional());
    Builder.CreateBr(Branch->getSuccessor(0));
    break;
  }
  }

  // Any static alloca that's still being used but not reachable from the new
  // entry needs to be moved to the new entry.
  Function *F = OldEntry->getParent();
  DominatorTree DT{*F};
  for (Instruction &I : llvm::make_early_inc_range(instructions(F))) {
    auto *Alloca = dyn_cast<AllocaInst>(&I);
    if (!Alloca || I.use_empty())
      continue;
    if (DT.isReachableFromEntry(I.getParent()) ||
        !isa<ConstantInt>(Alloca->getArraySize()))
      continue;
    I.moveBefore(*Entry, Entry->getFirstInsertionPt());
  }
}

/// Derive the value of the new frame pointer.
Value *CoroCloner::deriveNewFramePointer() {
  // Builder should be inserting to the front of the new entry block.

  switch (Shape.ABI) {
  // In switch-lowering, the argument is the frame pointer.
  case coro::ABI::Switch:
    return &*NewF->arg_begin();
  // In async-lowering, one of the arguments is an async context as determined
  // by the `llvm.coro.id.async` intrinsic. We can retrieve the async context of
  // the resume function from the async context projection function associated
  // with the active suspend. The frame is located as a tail to the async
  // context header.
  case coro::ABI::Async: {
    auto *ActiveAsyncSuspend = cast<CoroSuspendAsyncInst>(ActiveSuspend);
    auto ContextIdx = ActiveAsyncSuspend->getStorageArgumentIndex() & 0xff;
    auto *CalleeContext = NewF->getArg(ContextIdx);
    auto *ProjectionFunc =
        ActiveAsyncSuspend->getAsyncContextProjectionFunction();
    auto DbgLoc =
        cast<CoroSuspendAsyncInst>(VMap[ActiveSuspend])->getDebugLoc();
    // Calling i8* (i8*)
    auto *CallerContext = Builder.CreateCall(ProjectionFunc->getFunctionType(),
                                             ProjectionFunc, CalleeContext);
    CallerContext->setCallingConv(ProjectionFunc->getCallingConv());
    CallerContext->setDebugLoc(DbgLoc);
    // The frame is located after the async_context header.
    auto &Context = Builder.getContext();
    auto *FramePtrAddr = Builder.CreateConstInBoundsGEP1_32(
        Type::getInt8Ty(Context), CallerContext,
        Shape.AsyncLowering.FrameOffset, "async.ctx.frameptr");
    // Inline the projection function.
    InlineFunctionInfo InlineInfo;
    auto InlineRes = InlineFunction(*CallerContext, InlineInfo);
    assert(InlineRes.isSuccess());
    (void)InlineRes;
    return FramePtrAddr;
  }
  // In continuation-lowering, the argument is the opaque storage.
  case coro::ABI::Retcon:
  case coro::ABI::RetconOnce: {
    Argument *NewStorage = &*NewF->arg_begin();
    auto FramePtrTy = PointerType::getUnqual(Shape.FrameTy->getContext());

    // If the storage is inline, just bitcast to the storage to the frame type.
    if (Shape.RetconLowering.IsFrameInlineInStorage)
      return NewStorage;

    // Otherwise, load the real frame from the opaque storage.
    return Builder.CreateLoad(FramePtrTy, NewStorage);
  }
  }
  llvm_unreachable("bad ABI");
}

static void addFramePointerAttrs(AttributeList &Attrs, LLVMContext &Context,
                                 unsigned ParamIndex, uint64_t Size,
                                 Align Alignment, bool NoAlias) {
  AttrBuilder ParamAttrs(Context);
  ParamAttrs.addAttribute(Attribute::NonNull);
  ParamAttrs.addAttribute(Attribute::NoUndef);

  if (NoAlias)
    ParamAttrs.addAttribute(Attribute::NoAlias);

  ParamAttrs.addAlignmentAttr(Alignment);
  ParamAttrs.addDereferenceableAttr(Size);
  Attrs = Attrs.addParamAttributes(Context, ParamIndex, ParamAttrs);
}

static void addAsyncContextAttrs(AttributeList &Attrs, LLVMContext &Context,
                                 unsigned ParamIndex) {
  AttrBuilder ParamAttrs(Context);
  ParamAttrs.addAttribute(Attribute::SwiftAsync);
  Attrs = Attrs.addParamAttributes(Context, ParamIndex, ParamAttrs);
}

static void addSwiftSelfAttrs(AttributeList &Attrs, LLVMContext &Context,
                              unsigned ParamIndex) {
  AttrBuilder ParamAttrs(Context);
  ParamAttrs.addAttribute(Attribute::SwiftSelf);
  Attrs = Attrs.addParamAttributes(Context, ParamIndex, ParamAttrs);
}

/// Clone the body of the original function into a resume function of
/// some sort.
void CoroCloner::create() {
  // Create the new function if we don't already have one.
  if (!NewF) {
    NewF = createCloneDeclaration(OrigF, Shape, Suffix,
                                  OrigF.getParent()->end(), ActiveSuspend);
  }

  // Replace all args with dummy instructions. If an argument is the old frame
  // pointer, the dummy will be replaced by the new frame pointer once it is
  // computed below. Uses of all other arguments should have already been
  // rewritten by buildCoroutineFrame() to use loads/stores on the coroutine
  // frame.
  SmallVector<Instruction *> DummyArgs;
  for (Argument &A : OrigF.args()) {
    DummyArgs.push_back(new FreezeInst(PoisonValue::get(A.getType())));
    VMap[&A] = DummyArgs.back();
  }

  SmallVector<ReturnInst *, 4> Returns;

  // Ignore attempts to change certain attributes of the function.
  // TODO: maybe there should be a way to suppress this during cloning?
  auto savedVisibility = NewF->getVisibility();
  auto savedUnnamedAddr = NewF->getUnnamedAddr();
  auto savedDLLStorageClass = NewF->getDLLStorageClass();

  // NewF's linkage (which CloneFunctionInto does *not* change) might not
  // be compatible with the visibility of OrigF (which it *does* change),
  // so protect against that.
  auto savedLinkage = NewF->getLinkage();
  NewF->setLinkage(llvm::GlobalValue::ExternalLinkage);

  CloneFunctionInto(NewF, &OrigF, VMap,
                    CloneFunctionChangeType::LocalChangesOnly, Returns);

  auto &Context = NewF->getContext();

  // For async functions / continuations, adjust the scope line of the
  // clone to the line number of the suspend point. However, only
  // adjust the scope line when the files are the same. This ensures
  // line number and file name belong together. The scope line is
  // associated with all pre-prologue instructions. This avoids a jump
  // in the linetable from the function declaration to the suspend point.
  if (DISubprogram *SP = NewF->getSubprogram()) {
    assert(SP != OrigF.getSubprogram() && SP->isDistinct());
    if (ActiveSuspend)
      if (auto DL = ActiveSuspend->getDebugLoc())
        if (SP->getFile() == DL->getFile())
          SP->setScopeLine(DL->getLine());
    // Update the linkage name to reflect the modified symbol name. It
    // is necessary to update the linkage name in Swift, since the
    // mangling changes for resume functions. It might also be the
    // right thing to do in C++, but due to a limitation in LLVM's
    // AsmPrinter we can only do this if the function doesn't have an
    // abstract specification, since the DWARF backend expects the
    // abstract specification to contain the linkage name and asserts
    // that they are identical.
    if (SP->getUnit() &&
        SP->getUnit()->getSourceLanguage() == dwarf::DW_LANG_Swift) {
      SP->replaceLinkageName(MDString::get(Context, NewF->getName()));
      if (auto *Decl = SP->getDeclaration()) {
        auto *NewDecl = DISubprogram::get(
            Decl->getContext(), Decl->getScope(), Decl->getName(),
            NewF->getName(), Decl->getFile(), Decl->getLine(), Decl->getType(),
            Decl->getScopeLine(), Decl->getContainingType(),
            Decl->getVirtualIndex(), Decl->getThisAdjustment(),
            Decl->getFlags(), Decl->getSPFlags(), Decl->getUnit(),
            Decl->getTemplateParams(), nullptr, Decl->getRetainedNodes(),
            Decl->getThrownTypes(), Decl->getAnnotations(),
            Decl->getTargetFuncName());
        SP->replaceDeclaration(NewDecl);
      }
    }
  }

  NewF->setLinkage(savedLinkage);
  NewF->setVisibility(savedVisibility);
  NewF->setUnnamedAddr(savedUnnamedAddr);
  NewF->setDLLStorageClass(savedDLLStorageClass);
  // The function sanitizer metadata needs to match the signature of the
  // function it is being attached to. However this does not hold for split
  // functions here. Thus remove the metadata for split functions.
  if (Shape.ABI == coro::ABI::Switch &&
      NewF->hasMetadata(LLVMContext::MD_func_sanitize))
    NewF->eraseMetadata(LLVMContext::MD_func_sanitize);

  // Replace the attributes of the new function:
  auto OrigAttrs = NewF->getAttributes();
  auto NewAttrs = AttributeList();

  switch (Shape.ABI) {
  case coro::ABI::Switch:
    // Bootstrap attributes by copying function attributes from the
    // original function.  This should include optimization settings and so on.
    NewAttrs = NewAttrs.addFnAttributes(
        Context, AttrBuilder(Context, OrigAttrs.getFnAttrs()));

    addFramePointerAttrs(NewAttrs, Context, 0, Shape.FrameSize,
                         Shape.FrameAlign, /*NoAlias=*/false);
    break;
  case coro::ABI::Async: {
    auto *ActiveAsyncSuspend = cast<CoroSuspendAsyncInst>(ActiveSuspend);
    if (OrigF.hasParamAttribute(Shape.AsyncLowering.ContextArgNo,
                                Attribute::SwiftAsync)) {
      uint32_t ArgAttributeIndices =
          ActiveAsyncSuspend->getStorageArgumentIndex();
      auto ContextArgIndex = ArgAttributeIndices & 0xff;
      addAsyncContextAttrs(NewAttrs, Context, ContextArgIndex);

      // `swiftasync` must preceed `swiftself` so 0 is not a valid index for
      // `swiftself`.
      auto SwiftSelfIndex = ArgAttributeIndices >> 8;
      if (SwiftSelfIndex)
        addSwiftSelfAttrs(NewAttrs, Context, SwiftSelfIndex);
    }

    // Transfer the original function's attributes.
    auto FnAttrs = OrigF.getAttributes().getFnAttrs();
    NewAttrs = NewAttrs.addFnAttributes(Context, AttrBuilder(Context, FnAttrs));
    break;
  }
  case coro::ABI::Retcon:
  case coro::ABI::RetconOnce:
    // If we have a continuation prototype, just use its attributes,
    // full-stop.
    NewAttrs = Shape.RetconLowering.ResumePrototype->getAttributes();

    /// FIXME: Is it really good to add the NoAlias attribute?
    addFramePointerAttrs(NewAttrs, Context, 0,
                         Shape.getRetconCoroId()->getStorageSize(),
                         Shape.getRetconCoroId()->getStorageAlignment(),
                         /*NoAlias=*/true);

    break;
  }

  switch (Shape.ABI) {
  // In these ABIs, the cloned functions always return 'void', and the
  // existing return sites are meaningless.  Note that for unique
  // continuations, this includes the returns associated with suspends;
  // this is fine because we can't suspend twice.
  case coro::ABI::Switch:
  case coro::ABI::RetconOnce:
    // Remove old returns.
    for (ReturnInst *Return : Returns)
      changeToUnreachable(Return);
    break;

  // With multi-suspend continuations, we'll already have eliminated the
  // original returns and inserted returns before all the suspend points,
  // so we want to leave any returns in place.
  case coro::ABI::Retcon:
    break;
  // Async lowering will insert musttail call functions at all suspend points
  // followed by a return.
  // Don't change returns to unreachable because that will trip up the verifier.
  // These returns should be unreachable from the clone.
  case coro::ABI::Async:
    break;
  }

  NewF->setAttributes(NewAttrs);
  NewF->setCallingConv(Shape.getResumeFunctionCC());

  // Set up the new entry block.
  replaceEntryBlock();

  // Turn symmetric transfers into musttail calls.
  for (CallInst *ResumeCall : Shape.SymmetricTransfers) {
    ResumeCall = cast<CallInst>(VMap[ResumeCall]);
    if (TTI.supportsTailCallFor(ResumeCall)) {
      // FIXME: Could we support symmetric transfer effectively without
      // musttail?
      ResumeCall->setTailCallKind(CallInst::TCK_MustTail);
    }

    // Put a 'ret void' after the call, and split any remaining instructions to
    // an unreachable block.
    BasicBlock *BB = ResumeCall->getParent();
    BB->splitBasicBlock(ResumeCall->getNextNode());
    Builder.SetInsertPoint(BB->getTerminator());
    Builder.CreateRetVoid();
    BB->getTerminator()->eraseFromParent();
  }

  Builder.SetInsertPoint(&NewF->getEntryBlock().front());
  NewFramePtr = deriveNewFramePointer();

  // Remap frame pointer.
  Value *OldFramePtr = VMap[Shape.FramePtr];
  NewFramePtr->takeName(OldFramePtr);
  OldFramePtr->replaceAllUsesWith(NewFramePtr);

  // Remap vFrame pointer.
  auto *NewVFrame = Builder.CreateBitCast(
      NewFramePtr, PointerType::getUnqual(Builder.getContext()), "vFrame");
  Value *OldVFrame = cast<Value>(VMap[Shape.CoroBegin]);
  if (OldVFrame != NewVFrame)
    OldVFrame->replaceAllUsesWith(NewVFrame);

  // All uses of the arguments should have been resolved by this point,
  // so we can safely remove the dummy values.
  for (Instruction *DummyArg : DummyArgs) {
    DummyArg->replaceAllUsesWith(PoisonValue::get(DummyArg->getType()));
    DummyArg->deleteValue();
  }

  switch (Shape.ABI) {
  case coro::ABI::Switch:
    // Rewrite final suspend handling as it is not done via switch (allows to
    // remove final case from the switch, since it is undefined behavior to
    // resume the coroutine suspended at the final suspend point.
    if (Shape.SwitchLowering.HasFinalSuspend)
      handleFinalSuspend();
    break;
  case coro::ABI::Async:
  case coro::ABI::Retcon:
  case coro::ABI::RetconOnce:
    // Replace uses of the active suspend with the corresponding
    // continuation-function arguments.
    assert(ActiveSuspend != nullptr &&
           "no active suspend when lowering a continuation-style coroutine");
    replaceRetconOrAsyncSuspendUses();
    break;
  }

  // Handle suspends.
  replaceCoroSuspends();

  // Handle swifterror.
  replaceSwiftErrorOps();

  // Remove coro.end intrinsics.
  replaceCoroEnds();

  // Salvage debug info that points into the coroutine frame.
  salvageDebugInfo();

  // Eliminate coro.free from the clones, replacing it with 'null' in cleanup,
  // to suppress deallocation code.
  if (Shape.ABI == coro::ABI::Switch)
    coro::replaceCoroFree(cast<CoroIdInst>(VMap[Shape.CoroBegin->getId()]),
                          /*Elide=*/FKind == CoroCloner::Kind::SwitchCleanup);
}

static void updateAsyncFuncPointerContextSize(coro::Shape &Shape) {
  assert(Shape.ABI == coro::ABI::Async);

  auto *FuncPtrStruct = cast<ConstantStruct>(
      Shape.AsyncLowering.AsyncFuncPointer->getInitializer());
  auto *OrigRelativeFunOffset = FuncPtrStruct->getOperand(0);
  auto *OrigContextSize = FuncPtrStruct->getOperand(1);
  auto *NewContextSize = ConstantInt::get(OrigContextSize->getType(),
                                          Shape.AsyncLowering.ContextSize);
  auto *NewFuncPtrStruct = ConstantStruct::get(
      FuncPtrStruct->getType(), OrigRelativeFunOffset, NewContextSize);

  Shape.AsyncLowering.AsyncFuncPointer->setInitializer(NewFuncPtrStruct);
}

static void replaceFrameSizeAndAlignment(coro::Shape &Shape) {
  if (Shape.ABI == coro::ABI::Async)
    updateAsyncFuncPointerContextSize(Shape);

  for (CoroAlignInst *CA : Shape.CoroAligns) {
    CA->replaceAllUsesWith(
        ConstantInt::get(CA->getType(), Shape.FrameAlign.value()));
    CA->eraseFromParent();
  }

  if (Shape.CoroSizes.empty())
    return;

  // In the same function all coro.sizes should have the same result type.
  auto *SizeIntrin = Shape.CoroSizes.back();
  Module *M = SizeIntrin->getModule();
  const DataLayout &DL = M->getDataLayout();
  auto Size = DL.getTypeAllocSize(Shape.FrameTy);
  auto *SizeConstant = ConstantInt::get(SizeIntrin->getType(), Size);

  for (CoroSizeInst *CS : Shape.CoroSizes) {
    CS->replaceAllUsesWith(SizeConstant);
    CS->eraseFromParent();
  }
}

static void postSplitCleanup(Function &F) {
  removeUnreachableBlocks(F);

#ifndef NDEBUG
  // For now, we do a mandatory verification step because we don't
  // entirely trust this pass.  Note that we don't want to add a verifier
  // pass to FPM below because it will also verify all the global data.
  if (verifyFunction(F, &errs()))
    report_fatal_error("Broken function");
#endif
}

// Coroutine has no suspend points. Remove heap allocation for the coroutine
// frame if possible.
static void handleNoSuspendCoroutine(coro::Shape &Shape) {
  auto *CoroBegin = Shape.CoroBegin;
  auto *CoroId = CoroBegin->getId();
  auto *AllocInst = CoroId->getCoroAlloc();
  switch (Shape.ABI) {
  case coro::ABI::Switch: {
    auto SwitchId = cast<CoroIdInst>(CoroId);
    coro::replaceCoroFree(SwitchId, /*Elide=*/AllocInst != nullptr);
    if (AllocInst) {
      IRBuilder<> Builder(AllocInst);
      auto *Frame = Builder.CreateAlloca(Shape.FrameTy);
      Frame->setAlignment(Shape.FrameAlign);
      AllocInst->replaceAllUsesWith(Builder.getFalse());
      AllocInst->eraseFromParent();
      CoroBegin->replaceAllUsesWith(Frame);
    } else {
      CoroBegin->replaceAllUsesWith(CoroBegin->getMem());
    }

    break;
  }
  case coro::ABI::Async:
  case coro::ABI::Retcon:
  case coro::ABI::RetconOnce:
    CoroBegin->replaceAllUsesWith(UndefValue::get(CoroBegin->getType()));
    break;
  }

  CoroBegin->eraseFromParent();
  Shape.CoroBegin = nullptr;
}

// SimplifySuspendPoint needs to check that there is no calls between
// coro_save and coro_suspend, since any of the calls may potentially resume
// the coroutine and if that is the case we cannot eliminate the suspend point.
static bool hasCallsInBlockBetween(Instruction *From, Instruction *To) {
  for (Instruction *I = From; I != To; I = I->getNextNode()) {
    // Assume that no intrinsic can resume the coroutine.
    if (isa<IntrinsicInst>(I))
      continue;

    if (isa<CallBase>(I))
      return true;
  }
  return false;
}

static bool hasCallsInBlocksBetween(BasicBlock *SaveBB, BasicBlock *ResDesBB) {
  SmallPtrSet<BasicBlock *, 8> Set;
  SmallVector<BasicBlock *, 8> Worklist;

  Set.insert(SaveBB);
  Worklist.push_back(ResDesBB);

  // Accumulate all blocks between SaveBB and ResDesBB. Because CoroSaveIntr
  // returns a token consumed by suspend instruction, all blocks in between
  // will have to eventually hit SaveBB when going backwards from ResDesBB.
  while (!Worklist.empty()) {
    auto *BB = Worklist.pop_back_val();
    Set.insert(BB);
    for (auto *Pred : predecessors(BB))
      if (!Set.contains(Pred))
        Worklist.push_back(Pred);
  }

  // SaveBB and ResDesBB are checked separately in hasCallsBetween.
  Set.erase(SaveBB);
  Set.erase(ResDesBB);

  for (auto *BB : Set)
    if (hasCallsInBlockBetween(BB->getFirstNonPHI(), nullptr))
      return true;

  return false;
}

static bool hasCallsBetween(Instruction *Save, Instruction *ResumeOrDestroy) {
  auto *SaveBB = Save->getParent();
  auto *ResumeOrDestroyBB = ResumeOrDestroy->getParent();

  if (SaveBB == ResumeOrDestroyBB)
    return hasCallsInBlockBetween(Save->getNextNode(), ResumeOrDestroy);

  // Any calls from Save to the end of the block?
  if (hasCallsInBlockBetween(Save->getNextNode(), nullptr))
    return true;

  // Any calls from begging of the block up to ResumeOrDestroy?
  if (hasCallsInBlockBetween(ResumeOrDestroyBB->getFirstNonPHI(),
                             ResumeOrDestroy))
    return true;

  // Any calls in all of the blocks between SaveBB and ResumeOrDestroyBB?
  if (hasCallsInBlocksBetween(SaveBB, ResumeOrDestroyBB))
    return true;

  return false;
}

// If a SuspendIntrin is preceded by Resume or Destroy, we can eliminate the
// suspend point and replace it with nornal control flow.
static bool simplifySuspendPoint(CoroSuspendInst *Suspend,
                                 CoroBeginInst *CoroBegin) {
  Instruction *Prev = Suspend->getPrevNode();
  if (!Prev) {
    auto *Pred = Suspend->getParent()->getSinglePredecessor();
    if (!Pred)
      return false;
    Prev = Pred->getTerminator();
  }

  CallBase *CB = dyn_cast<CallBase>(Prev);
  if (!CB)
    return false;

  auto *Callee = CB->getCalledOperand()->stripPointerCasts();

  // See if the callsite is for resumption or destruction of the coroutine.
  auto *SubFn = dyn_cast<CoroSubFnInst>(Callee);
  if (!SubFn)
    return false;

  // Does not refer to the current coroutine, we cannot do anything with it.
  if (SubFn->getFrame() != CoroBegin)
    return false;

  // See if the transformation is safe. Specifically, see if there are any
  // calls in between Save and CallInstr. They can potenitally resume the
  // coroutine rendering this optimization unsafe.
  auto *Save = Suspend->getCoroSave();
  if (hasCallsBetween(Save, CB))
    return false;

  // Replace llvm.coro.suspend with the value that results in resumption over
  // the resume or cleanup path.
  Suspend->replaceAllUsesWith(SubFn->getRawIndex());
  Suspend->eraseFromParent();
  Save->eraseFromParent();

  // No longer need a call to coro.resume or coro.destroy.
  if (auto *Invoke = dyn_cast<InvokeInst>(CB)) {
    BranchInst::Create(Invoke->getNormalDest(), Invoke->getIterator());
  }

  // Grab the CalledValue from CB before erasing the CallInstr.
  auto *CalledValue = CB->getCalledOperand();
  CB->eraseFromParent();

  // If no more users remove it. Usually it is a bitcast of SubFn.
  if (CalledValue != SubFn && CalledValue->user_empty())
    if (auto *I = dyn_cast<Instruction>(CalledValue))
      I->eraseFromParent();

  // Now we are good to remove SubFn.
  if (SubFn->user_empty())
    SubFn->eraseFromParent();

  return true;
}

// Remove suspend points that are simplified.
static void simplifySuspendPoints(coro::Shape &Shape) {
  // Currently, the only simplification we do is switch-lowering-specific.
  if (Shape.ABI != coro::ABI::Switch)
    return;

  auto &S = Shape.CoroSuspends;
  size_t I = 0, N = S.size();
  if (N == 0)
    return;

  size_t ChangedFinalIndex = std::numeric_limits<size_t>::max();
  while (true) {
    auto SI = cast<CoroSuspendInst>(S[I]);
    // Leave final.suspend to handleFinalSuspend since it is undefined behavior
    // to resume a coroutine suspended at the final suspend point.
    if (!SI->isFinal() && simplifySuspendPoint(SI, Shape.CoroBegin)) {
      if (--N == I)
        break;

      std::swap(S[I], S[N]);

      if (cast<CoroSuspendInst>(S[I])->isFinal()) {
        assert(Shape.SwitchLowering.HasFinalSuspend);
        ChangedFinalIndex = I;
      }

      continue;
    }
    if (++I == N)
      break;
  }
  S.resize(N);

  // Maintain final.suspend in case final suspend was swapped.
  // Due to we requrie the final suspend to be the last element of CoroSuspends.
  if (ChangedFinalIndex < N) {
    assert(cast<CoroSuspendInst>(S[ChangedFinalIndex])->isFinal());
    std::swap(S[ChangedFinalIndex], S.back());
  }
}

namespace {

struct SwitchCoroutineSplitter {
  static void split(Function &F, coro::Shape &Shape,
                    SmallVectorImpl<Function *> &Clones,
                    TargetTransformInfo &TTI) {
    assert(Shape.ABI == coro::ABI::Switch);

    createResumeEntryBlock(F, Shape);
    auto *ResumeClone =
        createClone(F, ".resume", Shape, CoroCloner::Kind::SwitchResume, TTI);
    auto *DestroyClone =
        createClone(F, ".destroy", Shape, CoroCloner::Kind::SwitchUnwind, TTI);
    auto *CleanupClone =
        createClone(F, ".cleanup", Shape, CoroCloner::Kind::SwitchCleanup, TTI);

    postSplitCleanup(*ResumeClone);
    postSplitCleanup(*DestroyClone);
    postSplitCleanup(*CleanupClone);

    // Store addresses resume/destroy/cleanup functions in the coroutine frame.
    updateCoroFrame(Shape, ResumeClone, DestroyClone, CleanupClone);

    assert(Clones.empty());
    Clones.push_back(ResumeClone);
    Clones.push_back(DestroyClone);
    Clones.push_back(CleanupClone);

    // Create a constant array referring to resume/destroy/clone functions
    // pointed by the last argument of @llvm.coro.info, so that CoroElide pass
    // can determined correct function to call.
    setCoroInfo(F, Shape, Clones);
  }

private:
  // Create a resume clone by cloning the body of the original function, setting
  // new entry block and replacing coro.suspend an appropriate value to force
  // resume or cleanup pass for every suspend point.
  static Function *createClone(Function &F, const Twine &Suffix,
                               coro::Shape &Shape, CoroCloner::Kind FKind,
                               TargetTransformInfo &TTI) {
    CoroCloner Cloner(F, Suffix, Shape, FKind, TTI);
    Cloner.create();
    return Cloner.getFunction();
  }

  // Create an entry block for a resume function with a switch that will jump to
  // suspend points.
  static void createResumeEntryBlock(Function &F, coro::Shape &Shape) {
    LLVMContext &C = F.getContext();

    // resume.entry:
    //  %index.addr = getelementptr inbounds %f.Frame, %f.Frame* %FramePtr, i32
    //  0, i32 2 % index = load i32, i32* %index.addr switch i32 %index, label
    //  %unreachable [
    //    i32 0, label %resume.0
    //    i32 1, label %resume.1
    //    ...
    //  ]

    auto *NewEntry = BasicBlock::Create(C, "resume.entry", &F);
    auto *UnreachBB = BasicBlock::Create(C, "unreachable", &F);

    IRBuilder<> Builder(NewEntry);
    auto *FramePtr = Shape.FramePtr;
    auto *FrameTy = Shape.FrameTy;
    auto *GepIndex = Builder.CreateStructGEP(
        FrameTy, FramePtr, Shape.getSwitchIndexField(), "index.addr");
    auto *Index = Builder.CreateLoad(Shape.getIndexType(), GepIndex, "index");
    auto *Switch =
        Builder.CreateSwitch(Index, UnreachBB, Shape.CoroSuspends.size());
    Shape.SwitchLowering.ResumeSwitch = Switch;

    size_t SuspendIndex = 0;
    for (auto *AnyS : Shape.CoroSuspends) {
      auto *S = cast<CoroSuspendInst>(AnyS);
      ConstantInt *IndexVal = Shape.getIndex(SuspendIndex);

      // Replace CoroSave with a store to Index:
      //    %index.addr = getelementptr %f.frame... (index field number)
      //    store i32 %IndexVal, i32* %index.addr1
      auto *Save = S->getCoroSave();
      Builder.SetInsertPoint(Save);
      if (S->isFinal()) {
        // The coroutine should be marked done if it reaches the final suspend
        // point.
        markCoroutineAsDone(Builder, Shape, FramePtr);
      } else {
        auto *GepIndex = Builder.CreateStructGEP(
            FrameTy, FramePtr, Shape.getSwitchIndexField(), "index.addr");
        Builder.CreateStore(IndexVal, GepIndex);
      }

      Save->replaceAllUsesWith(ConstantTokenNone::get(C));
      Save->eraseFromParent();

      // Split block before and after coro.suspend and add a jump from an entry
      // switch:
      //
      //  whateverBB:
      //    whatever
      //    %0 = call i8 @llvm.coro.suspend(token none, i1 false)
      //    switch i8 %0, label %suspend[i8 0, label %resume
      //                                 i8 1, label %cleanup]
      // becomes:
      //
      //  whateverBB:
      //     whatever
      //     br label %resume.0.landing
      //
      //  resume.0: ; <--- jump from the switch in the resume.entry
      //     %0 = tail call i8 @llvm.coro.suspend(token none, i1 false)
      //     br label %resume.0.landing
      //
      //  resume.0.landing:
      //     %1 = phi i8[-1, %whateverBB], [%0, %resume.0]
      //     switch i8 % 1, label %suspend [i8 0, label %resume
      //                                    i8 1, label %cleanup]

      auto *SuspendBB = S->getParent();
      auto *ResumeBB =
          SuspendBB->splitBasicBlock(S, "resume." + Twine(SuspendIndex));
      auto *LandingBB = ResumeBB->splitBasicBlock(
          S->getNextNode(), ResumeBB->getName() + Twine(".landing"));
      Switch->addCase(IndexVal, ResumeBB);

      cast<BranchInst>(SuspendBB->getTerminator())->setSuccessor(0, LandingBB);
      auto *PN = PHINode::Create(Builder.getInt8Ty(), 2, "");
      PN->insertBefore(LandingBB->begin());
      S->replaceAllUsesWith(PN);
      PN->addIncoming(Builder.getInt8(-1), SuspendBB);
      PN->addIncoming(S, ResumeBB);

      ++SuspendIndex;
    }

    Builder.SetInsertPoint(UnreachBB);
    Builder.CreateUnreachable();

    Shape.SwitchLowering.ResumeEntryBlock = NewEntry;
  }

  // Store addresses of Resume/Destroy/Cleanup functions in the coroutine frame.
  static void updateCoroFrame(coro::Shape &Shape, Function *ResumeFn,
                              Function *DestroyFn, Function *CleanupFn) {
    IRBuilder<> Builder(&*Shape.getInsertPtAfterFramePtr());

    auto *ResumeAddr = Builder.CreateStructGEP(
        Shape.FrameTy, Shape.FramePtr, coro::Shape::SwitchFieldIndex::Resume,
        "resume.addr");
    Builder.CreateStore(ResumeFn, ResumeAddr);

    Value *DestroyOrCleanupFn = DestroyFn;

    CoroIdInst *CoroId = Shape.getSwitchCoroId();
    if (CoroAllocInst *CA = CoroId->getCoroAlloc()) {
      // If there is a CoroAlloc and it returns false (meaning we elide the
      // allocation, use CleanupFn instead of DestroyFn).
      DestroyOrCleanupFn = Builder.CreateSelect(CA, DestroyFn, CleanupFn);
    }

    auto *DestroyAddr = Builder.CreateStructGEP(
        Shape.FrameTy, Shape.FramePtr, coro::Shape::SwitchFieldIndex::Destroy,
        "destroy.addr");
    Builder.CreateStore(DestroyOrCleanupFn, DestroyAddr);
  }

  // Create a global constant array containing pointers to functions provided
  // and set Info parameter of CoroBegin to point at this constant. Example:
  //
  //   @f.resumers = internal constant [2 x void(%f.frame*)*]
  //                    [void(%f.frame*)* @f.resume, void(%f.frame*)*
  //                    @f.destroy]
  //   define void @f() {
  //     ...
  //     call i8* @llvm.coro.begin(i8* null, i32 0, i8* null,
  //                    i8* bitcast([2 x void(%f.frame*)*] * @f.resumers to
  //                    i8*))
  //
  // Assumes that all the functions have the same signature.
  static void setCoroInfo(Function &F, coro::Shape &Shape,
                          ArrayRef<Function *> Fns) {
    // This only works under the switch-lowering ABI because coro elision
    // only works on the switch-lowering ABI.
    SmallVector<Constant *, 4> Args(Fns.begin(), Fns.end());
    assert(!Args.empty());
    Function *Part = *Fns.begin();
    Module *M = Part->getParent();
    auto *ArrTy = ArrayType::get(Part->getType(), Args.size());

    auto *ConstVal = ConstantArray::get(ArrTy, Args);
    auto *GV = new GlobalVariable(*M, ConstVal->getType(), /*isConstant=*/true,
                                  GlobalVariable::PrivateLinkage, ConstVal,
                                  F.getName() + Twine(".resumers"));

    // Update coro.begin instruction to refer to this constant.
    LLVMContext &C = F.getContext();
    auto *BC = ConstantExpr::getPointerCast(GV, PointerType::getUnqual(C));
    Shape.getSwitchCoroId()->setInfo(BC);
  }
};

} // namespace

static void replaceAsyncResumeFunction(CoroSuspendAsyncInst *Suspend,
                                       Value *Continuation) {
  auto *ResumeIntrinsic = Suspend->getResumeFunction();
  auto &Context = Suspend->getParent()->getParent()->getContext();
  auto *Int8PtrTy = PointerType::getUnqual(Context);

  IRBuilder<> Builder(ResumeIntrinsic);
  auto *Val = Builder.CreateBitOrPointerCast(Continuation, Int8PtrTy);
  ResumeIntrinsic->replaceAllUsesWith(Val);
  ResumeIntrinsic->eraseFromParent();
  Suspend->setOperand(CoroSuspendAsyncInst::ResumeFunctionArg,
                      UndefValue::get(Int8PtrTy));
}

/// Coerce the arguments in \p FnArgs according to \p FnTy in \p CallArgs.
static void coerceArguments(IRBuilder<> &Builder, FunctionType *FnTy,
                            ArrayRef<Value *> FnArgs,
                            SmallVectorImpl<Value *> &CallArgs) {
  size_t ArgIdx = 0;
  for (auto *paramTy : FnTy->params()) {
    assert(ArgIdx < FnArgs.size());
    if (paramTy != FnArgs[ArgIdx]->getType())
      CallArgs.push_back(
          Builder.CreateBitOrPointerCast(FnArgs[ArgIdx], paramTy));
    else
      CallArgs.push_back(FnArgs[ArgIdx]);
    ++ArgIdx;
  }
}

CallInst *coro::createMustTailCall(DebugLoc Loc, Function *MustTailCallFn,
                                   TargetTransformInfo &TTI,
                                   ArrayRef<Value *> Arguments,
                                   IRBuilder<> &Builder) {
  auto *FnTy = MustTailCallFn->getFunctionType();
  // Coerce the arguments, llvm optimizations seem to ignore the types in
  // vaarg functions and throws away casts in optimized mode.
  SmallVector<Value *, 8> CallArgs;
  coerceArguments(Builder, FnTy, Arguments, CallArgs);

  auto *TailCall = Builder.CreateCall(FnTy, MustTailCallFn, CallArgs);
  // Skip targets which don't support tail call.
  if (TTI.supportsTailCallFor(TailCall)) {
    TailCall->setTailCallKind(CallInst::TCK_MustTail);
  }
  TailCall->setDebugLoc(Loc);
  TailCall->setCallingConv(MustTailCallFn->getCallingConv());
  return TailCall;
}

static void splitAsyncCoroutine(Function &F, coro::Shape &Shape,
                                SmallVectorImpl<Function *> &Clones,
                                TargetTransformInfo &TTI) {
  assert(Shape.ABI == coro::ABI::Async);
  assert(Clones.empty());
  // Reset various things that the optimizer might have decided it
  // "knows" about the coroutine function due to not seeing a return.
  F.removeFnAttr(Attribute::NoReturn);
  F.removeRetAttr(Attribute::NoAlias);
  F.removeRetAttr(Attribute::NonNull);

  auto &Context = F.getContext();
  auto *Int8PtrTy = PointerType::getUnqual(Context);

  auto *Id = cast<CoroIdAsyncInst>(Shape.CoroBegin->getId());
  IRBuilder<> Builder(Id);

  auto *FramePtr = Id->getStorage();
  FramePtr = Builder.CreateBitOrPointerCast(FramePtr, Int8PtrTy);
  FramePtr = Builder.CreateConstInBoundsGEP1_32(
      Type::getInt8Ty(Context), FramePtr, Shape.AsyncLowering.FrameOffset,
      "async.ctx.frameptr");

  // Map all uses of llvm.coro.begin to the allocated frame pointer.
  {
    // Make sure we don't invalidate Shape.FramePtr.
    TrackingVH<Value> Handle(Shape.FramePtr);
    Shape.CoroBegin->replaceAllUsesWith(FramePtr);
    Shape.FramePtr = Handle.getValPtr();
  }

  // Create all the functions in order after the main function.
  auto NextF = std::next(F.getIterator());

  // Create a continuation function for each of the suspend points.
  Clones.reserve(Shape.CoroSuspends.size());
  for (size_t Idx = 0, End = Shape.CoroSuspends.size(); Idx != End; ++Idx) {
    auto *Suspend = cast<CoroSuspendAsyncInst>(Shape.CoroSuspends[Idx]);

    // Create the clone declaration.
    auto ResumeNameSuffix = ".resume.";
    auto ProjectionFunctionName =
        Suspend->getAsyncContextProjectionFunction()->getName();
    bool UseSwiftMangling = false;
    if (ProjectionFunctionName == "__swift_async_resume_project_context") {
      ResumeNameSuffix = "TQ";
      UseSwiftMangling = true;
    } else if (ProjectionFunctionName == "__swift_async_resume_get_context") {
      ResumeNameSuffix = "TY";
      UseSwiftMangling = true;
    }
    auto *Continuation = createCloneDeclaration(
        F, Shape,
        UseSwiftMangling ? ResumeNameSuffix + Twine(Idx) + "_"
                         : ResumeNameSuffix + Twine(Idx),
        NextF, Suspend);
    Clones.push_back(Continuation);

    // Insert a branch to a new return block immediately before the suspend
    // point.
    auto *SuspendBB = Suspend->getParent();
    auto *NewSuspendBB = SuspendBB->splitBasicBlock(Suspend);
    auto *Branch = cast<BranchInst>(SuspendBB->getTerminator());

    // Place it before the first suspend.
    auto *ReturnBB =
        BasicBlock::Create(F.getContext(), "coro.return", &F, NewSuspendBB);
    Branch->setSuccessor(0, ReturnBB);

    IRBuilder<> Builder(ReturnBB);

    // Insert the call to the tail call function and inline it.
    auto *Fn = Suspend->getMustTailCallFunction();
    SmallVector<Value *, 8> Args(Suspend->args());
    auto FnArgs = ArrayRef<Value *>(Args).drop_front(
        CoroSuspendAsyncInst::MustTailCallFuncArg + 1);
    auto *TailCall = coro::createMustTailCall(Suspend->getDebugLoc(), Fn, TTI,
                                              FnArgs, Builder);
    Builder.CreateRetVoid();
    InlineFunctionInfo FnInfo;
    (void)InlineFunction(*TailCall, FnInfo);

    // Replace the lvm.coro.async.resume intrisic call.
    replaceAsyncResumeFunction(Suspend, Continuation);
  }

  assert(Clones.size() == Shape.CoroSuspends.size());
  for (size_t Idx = 0, End = Shape.CoroSuspends.size(); Idx != End; ++Idx) {
    auto *Suspend = Shape.CoroSuspends[Idx];
    auto *Clone = Clones[Idx];

    CoroCloner(F, "resume." + Twine(Idx), Shape, Clone, Suspend, TTI).create();
  }
}

static void splitRetconCoroutine(Function &F, coro::Shape &Shape,
                                 SmallVectorImpl<Function *> &Clones,
                                 TargetTransformInfo &TTI) {
  assert(Shape.ABI == coro::ABI::Retcon || Shape.ABI == coro::ABI::RetconOnce);
  assert(Clones.empty());

  // Reset various things that the optimizer might have decided it
  // "knows" about the coroutine function due to not seeing a return.
  F.removeFnAttr(Attribute::NoReturn);
  F.removeRetAttr(Attribute::NoAlias);
  F.removeRetAttr(Attribute::NonNull);

  // Allocate the frame.
  auto *Id = cast<AnyCoroIdRetconInst>(Shape.CoroBegin->getId());
  Value *RawFramePtr;
  if (Shape.RetconLowering.IsFrameInlineInStorage) {
    RawFramePtr = Id->getStorage();
  } else {
    IRBuilder<> Builder(Id);

    // Determine the size of the frame.
    const DataLayout &DL = F.getDataLayout();
    auto Size = DL.getTypeAllocSize(Shape.FrameTy);

    // Allocate.  We don't need to update the call graph node because we're
    // going to recompute it from scratch after splitting.
    // FIXME: pass the required alignment
    RawFramePtr = Shape.emitAlloc(Builder, Builder.getInt64(Size), nullptr);
    RawFramePtr =
        Builder.CreateBitCast(RawFramePtr, Shape.CoroBegin->getType());

    // Stash the allocated frame pointer in the continuation storage.
    Builder.CreateStore(RawFramePtr, Id->getStorage());
  }

  // Map all uses of llvm.coro.begin to the allocated frame pointer.
  {
    // Make sure we don't invalidate Shape.FramePtr.
    TrackingVH<Value> Handle(Shape.FramePtr);
    Shape.CoroBegin->replaceAllUsesWith(RawFramePtr);
    Shape.FramePtr = Handle.getValPtr();
  }

  // Create a unique return block.
  BasicBlock *ReturnBB = nullptr;
  SmallVector<PHINode *, 4> ReturnPHIs;

  // Create all the functions in order after the main function.
  auto NextF = std::next(F.getIterator());

  // Create a continuation function for each of the suspend points.
  Clones.reserve(Shape.CoroSuspends.size());
  for (size_t i = 0, e = Shape.CoroSuspends.size(); i != e; ++i) {
    auto Suspend = cast<CoroSuspendRetconInst>(Shape.CoroSuspends[i]);

    // Create the clone declaration.
    auto Continuation =
        createCloneDeclaration(F, Shape, ".resume." + Twine(i), NextF, nullptr);
    Clones.push_back(Continuation);

    // Insert a branch to the unified return block immediately before
    // the suspend point.
    auto SuspendBB = Suspend->getParent();
    auto NewSuspendBB = SuspendBB->splitBasicBlock(Suspend);
    auto Branch = cast<BranchInst>(SuspendBB->getTerminator());

    // Create the unified return block.
    if (!ReturnBB) {
      // Place it before the first suspend.
      ReturnBB =
          BasicBlock::Create(F.getContext(), "coro.return", &F, NewSuspendBB);
      Shape.RetconLowering.ReturnBlock = ReturnBB;

      IRBuilder<> Builder(ReturnBB);

      // Create PHIs for all the return values.
      assert(ReturnPHIs.empty());

      // First, the continuation.
      ReturnPHIs.push_back(Builder.CreatePHI(Continuation->getType(),
                                             Shape.CoroSuspends.size()));

      // Next, all the directly-yielded values.
      for (auto *ResultTy : Shape.getRetconResultTypes())
        ReturnPHIs.push_back(
            Builder.CreatePHI(ResultTy, Shape.CoroSuspends.size()));

      // Build the return value.
      auto RetTy = F.getReturnType();

      // Cast the continuation value if necessary.
      // We can't rely on the types matching up because that type would
      // have to be infinite.
      auto CastedContinuationTy =
          (ReturnPHIs.size() == 1 ? RetTy : RetTy->getStructElementType(0));
      auto *CastedContinuation =
          Builder.CreateBitCast(ReturnPHIs[0], CastedContinuationTy);

      Value *RetV;
      if (ReturnPHIs.size() == 1) {
        RetV = CastedContinuation;
      } else {
        RetV = PoisonValue::get(RetTy);
        RetV = Builder.CreateInsertValue(RetV, CastedContinuation, 0);
        for (size_t I = 1, E = ReturnPHIs.size(); I != E; ++I)
          RetV = Builder.CreateInsertValue(RetV, ReturnPHIs[I], I);
      }

      Builder.CreateRet(RetV);
    }

    // Branch to the return block.
    Branch->setSuccessor(0, ReturnBB);
    ReturnPHIs[0]->addIncoming(Continuation, SuspendBB);
    size_t NextPHIIndex = 1;
    for (auto &VUse : Suspend->value_operands())
      ReturnPHIs[NextPHIIndex++]->addIncoming(&*VUse, SuspendBB);
    assert(NextPHIIndex == ReturnPHIs.size());
  }

  assert(Clones.size() == Shape.CoroSuspends.size());
  for (size_t i = 0, e = Shape.CoroSuspends.size(); i != e; ++i) {
    auto Suspend = Shape.CoroSuspends[i];
    auto Clone = Clones[i];

    CoroCloner(F, "resume." + Twine(i), Shape, Clone, Suspend, TTI).create();
  }
}

namespace {
class PrettyStackTraceFunction : public PrettyStackTraceEntry {
  Function &F;

public:
  PrettyStackTraceFunction(Function &F) : F(F) {}
  void print(raw_ostream &OS) const override {
    OS << "While splitting coroutine ";
    F.printAsOperand(OS, /*print type*/ false, F.getParent());
    OS << "\n";
  }
};
} // namespace

static coro::Shape
splitCoroutine(Function &F, SmallVectorImpl<Function *> &Clones,
               TargetTransformInfo &TTI, bool OptimizeFrame,
               std::function<bool(Instruction &)> MaterializableCallback) {
  PrettyStackTraceFunction prettyStackTrace(F);

  // The suspend-crossing algorithm in buildCoroutineFrame get tripped
  // up by uses in unreachable blocks, so remove them as a first pass.
  removeUnreachableBlocks(F);

  coro::Shape Shape(F, OptimizeFrame);
  if (!Shape.CoroBegin)
    return Shape;

  lowerAwaitSuspends(F, Shape);

  simplifySuspendPoints(Shape);
  buildCoroutineFrame(F, Shape, TTI, MaterializableCallback);
  replaceFrameSizeAndAlignment(Shape);

  // If there are no suspend points, no split required, just remove
  // the allocation and deallocation blocks, they are not needed.
  if (Shape.CoroSuspends.empty()) {
    handleNoSuspendCoroutine(Shape);
  } else {
    switch (Shape.ABI) {
    case coro::ABI::Switch:
      SwitchCoroutineSplitter::split(F, Shape, Clones, TTI);
      break;
    case coro::ABI::Async:
      splitAsyncCoroutine(F, Shape, Clones, TTI);
      break;
    case coro::ABI::Retcon:
    case coro::ABI::RetconOnce:
      splitRetconCoroutine(F, Shape, Clones, TTI);
      break;
    }
  }

  // Replace all the swifterror operations in the original function.
  // This invalidates SwiftErrorOps in the Shape.
  replaceSwiftErrorOps(F, Shape, nullptr);

  // Salvage debug intrinsics that point into the coroutine frame in the
  // original function. The Cloner has already salvaged debug info in the new
  // coroutine funclets.
  SmallDenseMap<Argument *, AllocaInst *, 4> ArgToAllocaMap;
  auto [DbgInsts, DbgVariableRecords] = collectDbgVariableIntrinsics(F);
  for (auto *DDI : DbgInsts)
    coro::salvageDebugInfo(ArgToAllocaMap, *DDI, Shape.OptimizeFrame,
                           false /*UseEntryValue*/);
  for (DbgVariableRecord *DVR : DbgVariableRecords)
    coro::salvageDebugInfo(ArgToAllocaMap, *DVR, Shape.OptimizeFrame,
                           false /*UseEntryValue*/);
  return Shape;
}

/// Remove calls to llvm.coro.end in the original function.
static void removeCoroEndsFromRampFunction(const coro::Shape &Shape) {
  if (Shape.ABI != coro::ABI::Switch) {
    for (auto *End : Shape.CoroEnds) {
      replaceCoroEnd(End, Shape, Shape.FramePtr, /*in resume*/ false, nullptr);
    }
  } else {
    for (llvm::AnyCoroEndInst *End : Shape.CoroEnds) {
      auto &Context = End->getContext();
      End->replaceAllUsesWith(ConstantInt::getFalse(Context));
      End->eraseFromParent();
    }
  }
}

static void updateCallGraphAfterCoroutineSplit(
    LazyCallGraph::Node &N, const coro::Shape &Shape,
    const SmallVectorImpl<Function *> &Clones, LazyCallGraph::SCC &C,
    LazyCallGraph &CG, CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR,
    FunctionAnalysisManager &FAM) {

  if (!Clones.empty()) {
    switch (Shape.ABI) {
    case coro::ABI::Switch:
      // Each clone in the Switch lowering is independent of the other clones.
      // Let the LazyCallGraph know about each one separately.
      for (Function *Clone : Clones)
        CG.addSplitFunction(N.getFunction(), *Clone);
      break;
    case coro::ABI::Async:
    case coro::ABI::Retcon:
    case coro::ABI::RetconOnce:
      // Each clone in the Async/Retcon lowering references of the other clones.
      // Let the LazyCallGraph know about all of them at once.
      if (!Clones.empty())
        CG.addSplitRefRecursiveFunctions(N.getFunction(), Clones);
      break;
    }

    // Let the CGSCC infra handle the changes to the original function.
    updateCGAndAnalysisManagerForCGSCCPass(CG, C, N, AM, UR, FAM);
  }

  // Do some cleanup and let the CGSCC infra see if we've cleaned up any edges
  // to the split functions.
  postSplitCleanup(N.getFunction());
  updateCGAndAnalysisManagerForFunctionPass(CG, C, N, AM, UR, FAM);
}

/// Replace a call to llvm.coro.prepare.retcon.
static void replacePrepare(CallInst *Prepare, LazyCallGraph &CG,
                           LazyCallGraph::SCC &C) {
  auto CastFn = Prepare->getArgOperand(0); // as an i8*
  auto Fn = CastFn->stripPointerCasts();   // as its original type

  // Attempt to peephole this pattern:
  //    %0 = bitcast [[TYPE]] @some_function to i8*
  //    %1 = call @llvm.coro.prepare.retcon(i8* %0)
  //    %2 = bitcast %1 to [[TYPE]]
  // ==>
  //    %2 = @some_function
  for (Use &U : llvm::make_early_inc_range(Prepare->uses())) {
    // Look for bitcasts back to the original function type.
    auto *Cast = dyn_cast<BitCastInst>(U.getUser());
    if (!Cast || Cast->getType() != Fn->getType())
      continue;

    // Replace and remove the cast.
    Cast->replaceAllUsesWith(Fn);
    Cast->eraseFromParent();
  }

  // Replace any remaining uses with the function as an i8*.
  // This can never directly be a callee, so we don't need to update CG.
  Prepare->replaceAllUsesWith(CastFn);
  Prepare->eraseFromParent();

  // Kill dead bitcasts.
  while (auto *Cast = dyn_cast<BitCastInst>(CastFn)) {
    if (!Cast->use_empty())
      break;
    CastFn = Cast->getOperand(0);
    Cast->eraseFromParent();
  }
}

static bool replaceAllPrepares(Function *PrepareFn, LazyCallGraph &CG,
                               LazyCallGraph::SCC &C) {
  bool Changed = false;
  for (Use &P : llvm::make_early_inc_range(PrepareFn->uses())) {
    // Intrinsics can only be used in calls.
    auto *Prepare = cast<CallInst>(P.getUser());
    replacePrepare(Prepare, CG, C);
    Changed = true;
  }

  return Changed;
}

static void addPrepareFunction(const Module &M,
                               SmallVectorImpl<Function *> &Fns,
                               StringRef Name) {
  auto *PrepareFn = M.getFunction(Name);
  if (PrepareFn && !PrepareFn->use_empty())
    Fns.push_back(PrepareFn);
}

CoroSplitPass::CoroSplitPass(bool OptimizeFrame)
    : MaterializableCallback(coro::defaultMaterializable),
      OptimizeFrame(OptimizeFrame) {}

PreservedAnalyses CoroSplitPass::run(LazyCallGraph::SCC &C,
                                     CGSCCAnalysisManager &AM,
                                     LazyCallGraph &CG, CGSCCUpdateResult &UR) {
  // NB: One invariant of a valid LazyCallGraph::SCC is that it must contain a
  //     non-zero number of nodes, so we assume that here and grab the first
  //     node's function's module.
  Module &M = *C.begin()->getFunction().getParent();
  auto &FAM =
      AM.getResult<FunctionAnalysisManagerCGSCCProxy>(C, CG).getManager();

  // Check for uses of llvm.coro.prepare.retcon/async.
  SmallVector<Function *, 2> PrepareFns;
  addPrepareFunction(M, PrepareFns, "llvm.coro.prepare.retcon");
  addPrepareFunction(M, PrepareFns, "llvm.coro.prepare.async");

  // Find coroutines for processing.
  SmallVector<LazyCallGraph::Node *> Coroutines;
  for (LazyCallGraph::Node &N : C)
    if (N.getFunction().isPresplitCoroutine())
      Coroutines.push_back(&N);

  if (Coroutines.empty() && PrepareFns.empty())
    return PreservedAnalyses::all();

  // Split all the coroutines.
  for (LazyCallGraph::Node *N : Coroutines) {
    Function &F = N->getFunction();
    LLVM_DEBUG(dbgs() << "CoroSplit: Processing coroutine '" << F.getName()
                      << "\n");
    F.setSplittedCoroutine();

    SmallVector<Function *, 4> Clones;
    auto &ORE = FAM.getResult<OptimizationRemarkEmitterAnalysis>(F);
    const coro::Shape Shape =
        splitCoroutine(F, Clones, FAM.getResult<TargetIRAnalysis>(F),
                       OptimizeFrame, MaterializableCallback);
    removeCoroEndsFromRampFunction(Shape);
    updateCallGraphAfterCoroutineSplit(*N, Shape, Clones, C, CG, AM, UR, FAM);

    ORE.emit([&]() {
      return OptimizationRemark(DEBUG_TYPE, "CoroSplit", &F)
             << "Split '" << ore::NV("function", F.getName())
             << "' (frame_size=" << ore::NV("frame_size", Shape.FrameSize)
             << ", align=" << ore::NV("align", Shape.FrameAlign.value()) << ")";
    });

    if (!Shape.CoroSuspends.empty()) {
      // Run the CGSCC pipeline on the original and newly split functions.
      UR.CWorklist.insert(&C);
      for (Function *Clone : Clones)
        UR.CWorklist.insert(CG.lookupSCC(CG.get(*Clone)));
    }
  }

    for (auto *PrepareFn : PrepareFns) {
      replaceAllPrepares(PrepareFn, CG, C);
    }

  return PreservedAnalyses::none();
}
