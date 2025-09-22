//===- PreISelIntrinsicLowering.cpp - Pre-ISel intrinsic lowering pass ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements IR lowering for the llvm.memcpy, llvm.memmove,
// llvm.memset, llvm.load.relative and llvm.objc.* intrinsics.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/PreISelIntrinsicLowering.h"
#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/Analysis/ObjCARCUtil.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/LowerMemIntrinsics.h"

using namespace llvm;

/// Threshold to leave statically sized memory intrinsic calls. Calls of known
/// size larger than this will be expanded by the pass. Calls of unknown or
/// lower size will be left for expansion in codegen.
static cl::opt<int64_t> MemIntrinsicExpandSizeThresholdOpt(
    "mem-intrinsic-expand-size",
    cl::desc("Set minimum mem intrinsic size to expand in IR"), cl::init(-1),
    cl::Hidden);

namespace {

struct PreISelIntrinsicLowering {
  const TargetMachine &TM;
  const function_ref<TargetTransformInfo &(Function &)> LookupTTI;

  /// If this is true, assume it's preferably to leave memory intrinsic calls
  /// for replacement with a library call later. Otherwise this depends on
  /// TargetLoweringInfo availability of the corresponding function.
  const bool UseMemIntrinsicLibFunc;

  explicit PreISelIntrinsicLowering(
      const TargetMachine &TM_,
      function_ref<TargetTransformInfo &(Function &)> LookupTTI_,
      bool UseMemIntrinsicLibFunc_ = true)
      : TM(TM_), LookupTTI(LookupTTI_),
        UseMemIntrinsicLibFunc(UseMemIntrinsicLibFunc_) {}

  static bool shouldExpandMemIntrinsicWithSize(Value *Size,
                                               const TargetTransformInfo &TTI);
  bool expandMemIntrinsicUses(Function &F) const;
  bool lowerIntrinsics(Module &M) const;
};

} // namespace

static bool lowerLoadRelative(Function &F) {
  if (F.use_empty())
    return false;

  bool Changed = false;
  Type *Int32Ty = Type::getInt32Ty(F.getContext());

  for (Use &U : llvm::make_early_inc_range(F.uses())) {
    auto CI = dyn_cast<CallInst>(U.getUser());
    if (!CI || CI->getCalledOperand() != &F)
      continue;

    IRBuilder<> B(CI);
    Value *OffsetPtr =
        B.CreatePtrAdd(CI->getArgOperand(0), CI->getArgOperand(1));
    Value *OffsetI32 = B.CreateAlignedLoad(Int32Ty, OffsetPtr, Align(4));

    Value *ResultPtr = B.CreatePtrAdd(CI->getArgOperand(0), OffsetI32);

    CI->replaceAllUsesWith(ResultPtr);
    CI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

// ObjCARC has knowledge about whether an obj-c runtime function needs to be
// always tail-called or never tail-called.
static CallInst::TailCallKind getOverridingTailCallKind(const Function &F) {
  objcarc::ARCInstKind Kind = objcarc::GetFunctionClass(&F);
  if (objcarc::IsAlwaysTail(Kind))
    return CallInst::TCK_Tail;
  else if (objcarc::IsNeverTail(Kind))
    return CallInst::TCK_NoTail;
  return CallInst::TCK_None;
}

static bool lowerObjCCall(Function &F, const char *NewFn,
                          bool setNonLazyBind = false) {
  assert(IntrinsicInst::mayLowerToFunctionCall(F.getIntrinsicID()) &&
         "Pre-ISel intrinsics do lower into regular function calls");
  if (F.use_empty())
    return false;

  // If we haven't already looked up this function, check to see if the
  // program already contains a function with this name.
  Module *M = F.getParent();
  FunctionCallee FCache = M->getOrInsertFunction(NewFn, F.getFunctionType());

  if (Function *Fn = dyn_cast<Function>(FCache.getCallee())) {
    Fn->setLinkage(F.getLinkage());
    if (setNonLazyBind && !Fn->isWeakForLinker()) {
      // If we have Native ARC, set nonlazybind attribute for these APIs for
      // performance.
      Fn->addFnAttr(Attribute::NonLazyBind);
    }
  }

  CallInst::TailCallKind OverridingTCK = getOverridingTailCallKind(F);

  for (Use &U : llvm::make_early_inc_range(F.uses())) {
    auto *CB = cast<CallBase>(U.getUser());

    if (CB->getCalledFunction() != &F) {
      objcarc::ARCInstKind Kind = objcarc::getAttachedARCFunctionKind(CB);
      (void)Kind;
      assert((Kind == objcarc::ARCInstKind::RetainRV ||
              Kind == objcarc::ARCInstKind::UnsafeClaimRV) &&
             "use expected to be the argument of operand bundle "
             "\"clang.arc.attachedcall\"");
      U.set(FCache.getCallee());
      continue;
    }

    auto *CI = cast<CallInst>(CB);
    assert(CI->getCalledFunction() && "Cannot lower an indirect call!");

    IRBuilder<> Builder(CI->getParent(), CI->getIterator());
    SmallVector<Value *, 8> Args(CI->args());
    SmallVector<llvm::OperandBundleDef, 1> BundleList;
    CI->getOperandBundlesAsDefs(BundleList);
    CallInst *NewCI = Builder.CreateCall(FCache, Args, BundleList);
    NewCI->setName(CI->getName());

    // Try to set the most appropriate TailCallKind based on both the current
    // attributes and the ones that we could get from ObjCARC's special
    // knowledge of the runtime functions.
    //
    // std::max respects both requirements of notail and tail here:
    // * notail on either the call or from ObjCARC becomes notail
    // * tail on either side is stronger than none, but not notail
    CallInst::TailCallKind TCK = CI->getTailCallKind();
    NewCI->setTailCallKind(std::max(TCK, OverridingTCK));

    // Transfer the 'returned' attribute from the intrinsic to the call site.
    // By applying this only to intrinsic call sites, we avoid applying it to
    // non-ARC explicit calls to things like objc_retain which have not been
    // auto-upgraded to use the intrinsics.
    unsigned Index;
    if (F.getAttributes().hasAttrSomewhere(Attribute::Returned, &Index) &&
        Index)
      NewCI->addParamAttr(Index - AttributeList::FirstArgIndex,
                          Attribute::Returned);

    if (!CI->use_empty())
      CI->replaceAllUsesWith(NewCI);
    CI->eraseFromParent();
  }

  return true;
}

// TODO: Should refine based on estimated number of accesses (e.g. does it
// require splitting based on alignment)
bool PreISelIntrinsicLowering::shouldExpandMemIntrinsicWithSize(
    Value *Size, const TargetTransformInfo &TTI) {
  ConstantInt *CI = dyn_cast<ConstantInt>(Size);
  if (!CI)
    return true;
  uint64_t Threshold = MemIntrinsicExpandSizeThresholdOpt.getNumOccurrences()
                           ? MemIntrinsicExpandSizeThresholdOpt
                           : TTI.getMaxMemIntrinsicInlineSizeThreshold();
  uint64_t SizeVal = CI->getZExtValue();

  // Treat a threshold of 0 as a special case to force expansion of all
  // intrinsics, including size 0.
  return SizeVal > Threshold || Threshold == 0;
}

static bool canEmitLibcall(const TargetMachine &TM, Function *F,
                           RTLIB::Libcall LC) {
  // TODO: Should this consider the address space of the memcpy?
  const TargetLowering *TLI = TM.getSubtargetImpl(*F)->getTargetLowering();
  return TLI->getLibcallName(LC) != nullptr;
}

// TODO: Handle atomic memcpy and memcpy.inline
// TODO: Pass ScalarEvolution
bool PreISelIntrinsicLowering::expandMemIntrinsicUses(Function &F) const {
  Intrinsic::ID ID = F.getIntrinsicID();
  bool Changed = false;

  for (User *U : llvm::make_early_inc_range(F.users())) {
    Instruction *Inst = cast<Instruction>(U);

    switch (ID) {
    case Intrinsic::memcpy: {
      auto *Memcpy = cast<MemCpyInst>(Inst);
      Function *ParentFunc = Memcpy->getFunction();
      const TargetTransformInfo &TTI = LookupTTI(*ParentFunc);
      if (shouldExpandMemIntrinsicWithSize(Memcpy->getLength(), TTI)) {
        if (UseMemIntrinsicLibFunc &&
            canEmitLibcall(TM, ParentFunc, RTLIB::MEMCPY))
          break;

        // TODO: For optsize, emit the loop into a separate function
        expandMemCpyAsLoop(Memcpy, TTI);
        Changed = true;
        Memcpy->eraseFromParent();
      }

      break;
    }
    case Intrinsic::memcpy_inline: {
      // Only expand llvm.memcpy.inline with non-constant length in this
      // codepath, leaving the current SelectionDAG expansion for constant
      // length memcpy intrinsics undisturbed.
      auto *Memcpy = cast<MemCpyInlineInst>(Inst);
      if (isa<ConstantInt>(Memcpy->getLength()))
        break;

      Function *ParentFunc = Memcpy->getFunction();
      const TargetTransformInfo &TTI = LookupTTI(*ParentFunc);
      expandMemCpyAsLoop(Memcpy, TTI);
      Changed = true;
      Memcpy->eraseFromParent();
      break;
    }
    case Intrinsic::memmove: {
      auto *Memmove = cast<MemMoveInst>(Inst);
      Function *ParentFunc = Memmove->getFunction();
      const TargetTransformInfo &TTI = LookupTTI(*ParentFunc);
      if (shouldExpandMemIntrinsicWithSize(Memmove->getLength(), TTI)) {
        if (UseMemIntrinsicLibFunc &&
            canEmitLibcall(TM, ParentFunc, RTLIB::MEMMOVE))
          break;

        if (expandMemMoveAsLoop(Memmove, TTI)) {
          Changed = true;
          Memmove->eraseFromParent();
        }
      }

      break;
    }
    case Intrinsic::memset: {
      auto *Memset = cast<MemSetInst>(Inst);
      Function *ParentFunc = Memset->getFunction();
      const TargetTransformInfo &TTI = LookupTTI(*ParentFunc);
      if (shouldExpandMemIntrinsicWithSize(Memset->getLength(), TTI)) {
        if (UseMemIntrinsicLibFunc &&
            canEmitLibcall(TM, ParentFunc, RTLIB::MEMSET))
          break;

        expandMemSetAsLoop(Memset);
        Changed = true;
        Memset->eraseFromParent();
      }

      break;
    }
    case Intrinsic::memset_inline: {
      // Only expand llvm.memset.inline with non-constant length in this
      // codepath, leaving the current SelectionDAG expansion for constant
      // length memset intrinsics undisturbed.
      auto *Memset = cast<MemSetInlineInst>(Inst);
      if (isa<ConstantInt>(Memset->getLength()))
        break;

      expandMemSetAsLoop(Memset);
      Changed = true;
      Memset->eraseFromParent();
      break;
    }
    default:
      llvm_unreachable("unhandled intrinsic");
    }
  }

  return Changed;
}

bool PreISelIntrinsicLowering::lowerIntrinsics(Module &M) const {
  bool Changed = false;
  for (Function &F : M) {
    switch (F.getIntrinsicID()) {
    default:
      break;
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memmove:
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
      Changed |= expandMemIntrinsicUses(F);
      break;
    case Intrinsic::load_relative:
      Changed |= lowerLoadRelative(F);
      break;
    case Intrinsic::objc_autorelease:
      Changed |= lowerObjCCall(F, "objc_autorelease");
      break;
    case Intrinsic::objc_autoreleasePoolPop:
      Changed |= lowerObjCCall(F, "objc_autoreleasePoolPop");
      break;
    case Intrinsic::objc_autoreleasePoolPush:
      Changed |= lowerObjCCall(F, "objc_autoreleasePoolPush");
      break;
    case Intrinsic::objc_autoreleaseReturnValue:
      Changed |= lowerObjCCall(F, "objc_autoreleaseReturnValue");
      break;
    case Intrinsic::objc_copyWeak:
      Changed |= lowerObjCCall(F, "objc_copyWeak");
      break;
    case Intrinsic::objc_destroyWeak:
      Changed |= lowerObjCCall(F, "objc_destroyWeak");
      break;
    case Intrinsic::objc_initWeak:
      Changed |= lowerObjCCall(F, "objc_initWeak");
      break;
    case Intrinsic::objc_loadWeak:
      Changed |= lowerObjCCall(F, "objc_loadWeak");
      break;
    case Intrinsic::objc_loadWeakRetained:
      Changed |= lowerObjCCall(F, "objc_loadWeakRetained");
      break;
    case Intrinsic::objc_moveWeak:
      Changed |= lowerObjCCall(F, "objc_moveWeak");
      break;
    case Intrinsic::objc_release:
      Changed |= lowerObjCCall(F, "objc_release", true);
      break;
    case Intrinsic::objc_retain:
      Changed |= lowerObjCCall(F, "objc_retain", true);
      break;
    case Intrinsic::objc_retainAutorelease:
      Changed |= lowerObjCCall(F, "objc_retainAutorelease");
      break;
    case Intrinsic::objc_retainAutoreleaseReturnValue:
      Changed |= lowerObjCCall(F, "objc_retainAutoreleaseReturnValue");
      break;
    case Intrinsic::objc_retainAutoreleasedReturnValue:
      Changed |= lowerObjCCall(F, "objc_retainAutoreleasedReturnValue");
      break;
    case Intrinsic::objc_retainBlock:
      Changed |= lowerObjCCall(F, "objc_retainBlock");
      break;
    case Intrinsic::objc_storeStrong:
      Changed |= lowerObjCCall(F, "objc_storeStrong");
      break;
    case Intrinsic::objc_storeWeak:
      Changed |= lowerObjCCall(F, "objc_storeWeak");
      break;
    case Intrinsic::objc_unsafeClaimAutoreleasedReturnValue:
      Changed |= lowerObjCCall(F, "objc_unsafeClaimAutoreleasedReturnValue");
      break;
    case Intrinsic::objc_retainedObject:
      Changed |= lowerObjCCall(F, "objc_retainedObject");
      break;
    case Intrinsic::objc_unretainedObject:
      Changed |= lowerObjCCall(F, "objc_unretainedObject");
      break;
    case Intrinsic::objc_unretainedPointer:
      Changed |= lowerObjCCall(F, "objc_unretainedPointer");
      break;
    case Intrinsic::objc_retain_autorelease:
      Changed |= lowerObjCCall(F, "objc_retain_autorelease");
      break;
    case Intrinsic::objc_sync_enter:
      Changed |= lowerObjCCall(F, "objc_sync_enter");
      break;
    case Intrinsic::objc_sync_exit:
      Changed |= lowerObjCCall(F, "objc_sync_exit");
      break;
    }
  }
  return Changed;
}

namespace {

class PreISelIntrinsicLoweringLegacyPass : public ModulePass {
public:
  static char ID;

  PreISelIntrinsicLoweringLegacyPass() : ModulePass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
    AU.addRequired<TargetPassConfig>();
  }

  bool runOnModule(Module &M) override {
    auto LookupTTI = [this](Function &F) -> TargetTransformInfo & {
      return this->getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
    };

    const auto &TM = getAnalysis<TargetPassConfig>().getTM<TargetMachine>();
    PreISelIntrinsicLowering Lowering(TM, LookupTTI);
    return Lowering.lowerIntrinsics(M);
  }
};

} // end anonymous namespace

char PreISelIntrinsicLoweringLegacyPass::ID;

INITIALIZE_PASS_BEGIN(PreISelIntrinsicLoweringLegacyPass,
                      "pre-isel-intrinsic-lowering",
                      "Pre-ISel Intrinsic Lowering", false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(TargetTransformInfoWrapperPass)
INITIALIZE_PASS_END(PreISelIntrinsicLoweringLegacyPass,
                    "pre-isel-intrinsic-lowering",
                    "Pre-ISel Intrinsic Lowering", false, false)

ModulePass *llvm::createPreISelIntrinsicLoweringPass() {
  return new PreISelIntrinsicLoweringLegacyPass();
}

PreservedAnalyses PreISelIntrinsicLoweringPass::run(Module &M,
                                                    ModuleAnalysisManager &AM) {
  auto &FAM = AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  auto LookupTTI = [&FAM](Function &F) -> TargetTransformInfo & {
    return FAM.getResult<TargetIRAnalysis>(F);
  };

  PreISelIntrinsicLowering Lowering(TM, LookupTTI);
  if (!Lowering.lowerIntrinsics(M))
    return PreservedAnalyses::all();
  else
    return PreservedAnalyses::none();
}
