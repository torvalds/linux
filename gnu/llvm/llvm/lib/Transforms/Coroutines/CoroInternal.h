//===- CoroInternal.h - Internal Coroutine interfaces ---------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Common definitions/declarations used internally by coroutine lowering passes.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_COROUTINES_COROINTERNAL_H
#define LLVM_LIB_TRANSFORMS_COROUTINES_COROINTERNAL_H

#include "CoroInstr.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {

class CallGraph;

namespace coro {

bool declaresAnyIntrinsic(const Module &M);
bool declaresIntrinsics(const Module &M,
                        const std::initializer_list<StringRef>);
void replaceCoroFree(CoroIdInst *CoroId, bool Elide);

/// Attempts to rewrite the location operand of debug intrinsics in terms of
/// the coroutine frame pointer, folding pointer offsets into the DIExpression
/// of the intrinsic.
/// If the frame pointer is an Argument, store it into an alloca if
/// OptimizeFrame is false.
void salvageDebugInfo(
    SmallDenseMap<Argument *, AllocaInst *, 4> &ArgToAllocaMap,
    DbgVariableIntrinsic &DVI, bool OptimizeFrame, bool IsEntryPoint);
void salvageDebugInfo(
    SmallDenseMap<Argument *, AllocaInst *, 4> &ArgToAllocaMap,
    DbgVariableRecord &DVR, bool OptimizeFrame, bool UseEntryValue);

// Keeps data and helper functions for lowering coroutine intrinsics.
struct LowererBase {
  Module &TheModule;
  LLVMContext &Context;
  PointerType *const Int8Ptr;
  FunctionType *const ResumeFnType;
  ConstantPointerNull *const NullPtr;

  LowererBase(Module &M);
  CallInst *makeSubFnCall(Value *Arg, int Index, Instruction *InsertPt);
};

enum class ABI {
  /// The "resume-switch" lowering, where there are separate resume and
  /// destroy functions that are shared between all suspend points.  The
  /// coroutine frame implicitly stores the resume and destroy functions,
  /// the current index, and any promise value.
  Switch,

  /// The "returned-continuation" lowering, where each suspend point creates a
  /// single continuation function that is used for both resuming and
  /// destroying.  Does not support promises.
  Retcon,

  /// The "unique returned-continuation" lowering, where each suspend point
  /// creates a single continuation function that is used for both resuming
  /// and destroying.  Does not support promises.  The function is known to
  /// suspend at most once during its execution, and the return value of
  /// the continuation is void.
  RetconOnce,

  /// The "async continuation" lowering, where each suspend point creates a
  /// single continuation function. The continuation function is available as an
  /// intrinsic.
  Async,
};

// Holds structural Coroutine Intrinsics for a particular function and other
// values used during CoroSplit pass.
struct LLVM_LIBRARY_VISIBILITY Shape {
  CoroBeginInst *CoroBegin;
  SmallVector<AnyCoroEndInst *, 4> CoroEnds;
  SmallVector<CoroSizeInst *, 2> CoroSizes;
  SmallVector<CoroAlignInst *, 2> CoroAligns;
  SmallVector<AnyCoroSuspendInst *, 4> CoroSuspends;
  SmallVector<CallInst*, 2> SwiftErrorOps;
  SmallVector<CoroAwaitSuspendInst *, 4> CoroAwaitSuspends;
  SmallVector<CallInst *, 2> SymmetricTransfers;

  // Field indexes for special fields in the switch lowering.
  struct SwitchFieldIndex {
    enum {
      Resume,
      Destroy

      // The promise field is always at a fixed offset from the start of
      // frame given its type, but the index isn't a constant for all
      // possible frames.

      // The switch-index field isn't at a fixed offset or index, either;
      // we just work it in where it fits best.
    };
  };

  coro::ABI ABI;

  StructType *FrameTy;
  Align FrameAlign;
  uint64_t FrameSize;
  Value *FramePtr;
  BasicBlock *AllocaSpillBlock;

  /// This would only be true if optimization are enabled.
  bool OptimizeFrame;

  struct SwitchLoweringStorage {
    SwitchInst *ResumeSwitch;
    AllocaInst *PromiseAlloca;
    BasicBlock *ResumeEntryBlock;
    unsigned IndexField;
    unsigned IndexAlign;
    unsigned IndexOffset;
    bool HasFinalSuspend;
    bool HasUnwindCoroEnd;
  };

  struct RetconLoweringStorage {
    Function *ResumePrototype;
    Function *Alloc;
    Function *Dealloc;
    BasicBlock *ReturnBlock;
    bool IsFrameInlineInStorage;
  };

  struct AsyncLoweringStorage {
    Value *Context;
    CallingConv::ID AsyncCC;
    unsigned ContextArgNo;
    uint64_t ContextHeaderSize;
    uint64_t ContextAlignment;
    uint64_t FrameOffset; // Start of the frame.
    uint64_t ContextSize; // Includes frame size.
    GlobalVariable *AsyncFuncPointer;

    Align getContextAlignment() const { return Align(ContextAlignment); }
  };

  union {
    SwitchLoweringStorage SwitchLowering;
    RetconLoweringStorage RetconLowering;
    AsyncLoweringStorage AsyncLowering;
  };

  CoroIdInst *getSwitchCoroId() const {
    assert(ABI == coro::ABI::Switch);
    return cast<CoroIdInst>(CoroBegin->getId());
  }

  AnyCoroIdRetconInst *getRetconCoroId() const {
    assert(ABI == coro::ABI::Retcon ||
           ABI == coro::ABI::RetconOnce);
    return cast<AnyCoroIdRetconInst>(CoroBegin->getId());
  }

  CoroIdAsyncInst *getAsyncCoroId() const {
    assert(ABI == coro::ABI::Async);
    return cast<CoroIdAsyncInst>(CoroBegin->getId());
  }

  unsigned getSwitchIndexField() const {
    assert(ABI == coro::ABI::Switch);
    assert(FrameTy && "frame type not assigned");
    return SwitchLowering.IndexField;
  }
  IntegerType *getIndexType() const {
    assert(ABI == coro::ABI::Switch);
    assert(FrameTy && "frame type not assigned");
    return cast<IntegerType>(FrameTy->getElementType(getSwitchIndexField()));
  }
  ConstantInt *getIndex(uint64_t Value) const {
    return ConstantInt::get(getIndexType(), Value);
  }

  PointerType *getSwitchResumePointerType() const {
    assert(ABI == coro::ABI::Switch);
  assert(FrameTy && "frame type not assigned");
  return cast<PointerType>(FrameTy->getElementType(SwitchFieldIndex::Resume));
  }

  FunctionType *getResumeFunctionType() const {
    switch (ABI) {
    case coro::ABI::Switch:
      return FunctionType::get(Type::getVoidTy(FrameTy->getContext()),
                               PointerType::getUnqual(FrameTy->getContext()),
                               /*IsVarArg=*/false);
    case coro::ABI::Retcon:
    case coro::ABI::RetconOnce:
      return RetconLowering.ResumePrototype->getFunctionType();
    case coro::ABI::Async:
      // Not used. The function type depends on the active suspend.
      return nullptr;
    }

    llvm_unreachable("Unknown coro::ABI enum");
  }

  ArrayRef<Type*> getRetconResultTypes() const {
    assert(ABI == coro::ABI::Retcon ||
           ABI == coro::ABI::RetconOnce);
    auto FTy = CoroBegin->getFunction()->getFunctionType();

    // The safety of all this is checked by checkWFRetconPrototype.
    if (auto STy = dyn_cast<StructType>(FTy->getReturnType())) {
      return STy->elements().slice(1);
    } else {
      return ArrayRef<Type*>();
    }
  }

  ArrayRef<Type*> getRetconResumeTypes() const {
    assert(ABI == coro::ABI::Retcon ||
           ABI == coro::ABI::RetconOnce);

    // The safety of all this is checked by checkWFRetconPrototype.
    auto FTy = RetconLowering.ResumePrototype->getFunctionType();
    return FTy->params().slice(1);
  }

  CallingConv::ID getResumeFunctionCC() const {
    switch (ABI) {
    case coro::ABI::Switch:
      return CallingConv::Fast;

    case coro::ABI::Retcon:
    case coro::ABI::RetconOnce:
      return RetconLowering.ResumePrototype->getCallingConv();
    case coro::ABI::Async:
      return AsyncLowering.AsyncCC;
    }
    llvm_unreachable("Unknown coro::ABI enum");
  }

  AllocaInst *getPromiseAlloca() const {
    if (ABI == coro::ABI::Switch)
      return SwitchLowering.PromiseAlloca;
    return nullptr;
  }

  BasicBlock::iterator getInsertPtAfterFramePtr() const {
    if (auto *I = dyn_cast<Instruction>(FramePtr)) {
      BasicBlock::iterator It = std::next(I->getIterator());
      It.setHeadBit(true); // Copy pre-RemoveDIs behaviour.
      return It;
    }
    return cast<Argument>(FramePtr)->getParent()->getEntryBlock().begin();
  }

  /// Allocate memory according to the rules of the active lowering.
  ///
  /// \param CG - if non-null, will be updated for the new call
  Value *emitAlloc(IRBuilder<> &Builder, Value *Size, CallGraph *CG) const;

  /// Deallocate memory according to the rules of the active lowering.
  ///
  /// \param CG - if non-null, will be updated for the new call
  void emitDealloc(IRBuilder<> &Builder, Value *Ptr, CallGraph *CG) const;

  Shape() = default;
  explicit Shape(Function &F, bool OptimizeFrame = false)
      : OptimizeFrame(OptimizeFrame) {
    buildFrom(F);
  }
  void buildFrom(Function &F);
};

bool defaultMaterializable(Instruction &V);
void buildCoroutineFrame(
    Function &F, Shape &Shape, TargetTransformInfo &TTI,
    const std::function<bool(Instruction &)> &MaterializableCallback);
CallInst *createMustTailCall(DebugLoc Loc, Function *MustTailCallFn,
                             TargetTransformInfo &TTI,
                             ArrayRef<Value *> Arguments, IRBuilder<> &);
} // End namespace coro.
} // End namespace llvm

#endif
