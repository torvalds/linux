//===-- LowerGlobalDtors.cpp - Lower @llvm.global_dtors -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Lower @llvm.global_dtors.
///
/// Implement @llvm.global_dtors by creating wrapper functions that are
/// registered in @llvm.global_ctors and which contain a call to
/// `__cxa_atexit` to register their destructor functions.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/LowerGlobalDtors.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include <map>

using namespace llvm;

#define DEBUG_TYPE "lower-global-dtors"

namespace {
class LowerGlobalDtorsLegacyPass final : public ModulePass {
  StringRef getPassName() const override {
    return "Lower @llvm.global_dtors via `__cxa_atexit`";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  LowerGlobalDtorsLegacyPass() : ModulePass(ID) {
    initializeLowerGlobalDtorsLegacyPassPass(*PassRegistry::getPassRegistry());
  }
};
} // End anonymous namespace

char LowerGlobalDtorsLegacyPass::ID = 0;
INITIALIZE_PASS(LowerGlobalDtorsLegacyPass, DEBUG_TYPE,
                "Lower @llvm.global_dtors via `__cxa_atexit`", false, false)

ModulePass *llvm::createLowerGlobalDtorsLegacyPass() {
  return new LowerGlobalDtorsLegacyPass();
}

static bool runImpl(Module &M);
bool LowerGlobalDtorsLegacyPass::runOnModule(Module &M) { return runImpl(M); }

PreservedAnalyses LowerGlobalDtorsPass::run(Module &M,
                                            ModuleAnalysisManager &AM) {
  bool Changed = runImpl(M);
  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

static bool runImpl(Module &M) {
  GlobalVariable *GV = M.getGlobalVariable("llvm.global_dtors");
  if (!GV || !GV->hasInitializer())
    return false;

  const ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!InitList)
    return false;

  // Validate @llvm.global_dtor's type.
  auto *ETy = dyn_cast<StructType>(InitList->getType()->getElementType());
  if (!ETy || ETy->getNumElements() != 3 ||
      !ETy->getTypeAtIndex(0U)->isIntegerTy() ||
      !ETy->getTypeAtIndex(1U)->isPointerTy() ||
      !ETy->getTypeAtIndex(2U)->isPointerTy())
    return false; // Not (int, ptr, ptr).

  // Collect the contents of @llvm.global_dtors, ordered by priority. Within a
  // priority, sequences of destructors with the same associated object are
  // recorded so that we can register them as a group.
  std::map<
      uint16_t,
      std::vector<std::pair<Constant *, std::vector<Constant *>>>
  > DtorFuncs;
  for (Value *O : InitList->operands()) {
    auto *CS = dyn_cast<ConstantStruct>(O);
    if (!CS)
      continue; // Malformed.

    auto *Priority = dyn_cast<ConstantInt>(CS->getOperand(0));
    if (!Priority)
      continue; // Malformed.
    uint16_t PriorityValue = Priority->getLimitedValue(UINT16_MAX);

    Constant *DtorFunc = CS->getOperand(1);
    if (DtorFunc->isNullValue())
      break; // Found a null terminator, skip the rest.

    Constant *Associated = CS->getOperand(2);
    Associated = cast<Constant>(Associated->stripPointerCasts());

    auto &AtThisPriority = DtorFuncs[PriorityValue];
    if (AtThisPriority.empty() || AtThisPriority.back().first != Associated) {
        std::vector<Constant *> NewList;
        NewList.push_back(DtorFunc);
        AtThisPriority.push_back(std::make_pair(Associated, NewList));
    } else {
        AtThisPriority.back().second.push_back(DtorFunc);
    }
  }
  if (DtorFuncs.empty())
    return false;

  // extern "C" int __cxa_atexit(void (*f)(void *), void *p, void *d);
  LLVMContext &C = M.getContext();
  PointerType *VoidStar = PointerType::getUnqual(C);
  Type *AtExitFuncArgs[] = {VoidStar};
  FunctionType *AtExitFuncTy =
      FunctionType::get(Type::getVoidTy(C), AtExitFuncArgs,
                        /*isVarArg=*/false);

  FunctionCallee AtExit = M.getOrInsertFunction(
      "__cxa_atexit",
      FunctionType::get(Type::getInt32Ty(C),
                        {PointerType::get(AtExitFuncTy, 0), VoidStar, VoidStar},
                        /*isVarArg=*/false));

  // If __cxa_atexit is defined (e.g. in the case of LTO) and arg0 is not
  // actually used (i.e. it's dummy/stub function as used in emscripten when
  // the program never exits) we can simply return early and clear out
  // @llvm.global_dtors.
  if (auto F = dyn_cast<Function>(AtExit.getCallee())) {
    if (F && F->hasExactDefinition() && F->getArg(0)->getNumUses() == 0) {
      GV->eraseFromParent();
      return true;
    }
  }

  // Declare __dso_local.
  Type *DsoHandleTy = Type::getInt8Ty(C);
  Constant *DsoHandle = M.getOrInsertGlobal("__dso_handle", DsoHandleTy, [&] {
    auto *GV = new GlobalVariable(M, DsoHandleTy, /*isConstant=*/true,
                                  GlobalVariable::ExternalWeakLinkage, nullptr,
                                  "__dso_handle");
    GV->setVisibility(GlobalVariable::HiddenVisibility);
    return GV;
  });

  // For each unique priority level and associated symbol, generate a function
  // to call all the destructors at that level, and a function to register the
  // first function with __cxa_atexit.
  for (auto &PriorityAndMore : DtorFuncs) {
    uint16_t Priority = PriorityAndMore.first;
    uint64_t Id = 0;
    auto &AtThisPriority = PriorityAndMore.second;
    for (auto &AssociatedAndMore : AtThisPriority) {
      Constant *Associated = AssociatedAndMore.first;
      auto ThisId = Id++;

      Function *CallDtors = Function::Create(
          AtExitFuncTy, Function::PrivateLinkage,
          "call_dtors" +
              (Priority != UINT16_MAX ? (Twine(".") + Twine(Priority))
                                      : Twine()) +
              (AtThisPriority.size() > 1 ? Twine("$") + Twine(ThisId)
                                         : Twine()) +
              (!Associated->isNullValue() ? (Twine(".") + Associated->getName())
                                          : Twine()),
          &M);
      BasicBlock *BB = BasicBlock::Create(C, "body", CallDtors);
      FunctionType *VoidVoid = FunctionType::get(Type::getVoidTy(C),
                                                 /*isVarArg=*/false);

      for (auto *Dtor : reverse(AssociatedAndMore.second))
        CallInst::Create(VoidVoid, Dtor, "", BB);
      ReturnInst::Create(C, BB);

      Function *RegisterCallDtors = Function::Create(
          VoidVoid, Function::PrivateLinkage,
          "register_call_dtors" +
              (Priority != UINT16_MAX ? (Twine(".") + Twine(Priority))
                                      : Twine()) +
              (AtThisPriority.size() > 1 ? Twine("$") + Twine(ThisId)
                                         : Twine()) +
              (!Associated->isNullValue() ? (Twine(".") + Associated->getName())
                                          : Twine()),
          &M);
      BasicBlock *EntryBB = BasicBlock::Create(C, "entry", RegisterCallDtors);
      BasicBlock *FailBB = BasicBlock::Create(C, "fail", RegisterCallDtors);
      BasicBlock *RetBB = BasicBlock::Create(C, "return", RegisterCallDtors);

      Value *Null = ConstantPointerNull::get(VoidStar);
      Value *Args[] = {CallDtors, Null, DsoHandle};
      Value *Res = CallInst::Create(AtExit, Args, "call", EntryBB);
      Value *Cmp = new ICmpInst(EntryBB, ICmpInst::ICMP_NE, Res,
                                Constant::getNullValue(Res->getType()));
      BranchInst::Create(FailBB, RetBB, Cmp, EntryBB);

      // If `__cxa_atexit` hits out-of-memory, trap, so that we don't misbehave.
      // This should be very rare, because if the process is running out of
      // memory before main has even started, something is wrong.
      CallInst::Create(Intrinsic::getDeclaration(&M, Intrinsic::trap), "",
                       FailBB);
      new UnreachableInst(C, FailBB);

      ReturnInst::Create(C, RetBB);

      // Now register the registration function with @llvm.global_ctors.
      appendToGlobalCtors(M, RegisterCallDtors, Priority, Associated);
    }
  }

  // Now that we've lowered everything, remove @llvm.global_dtors.
  GV->eraseFromParent();

  return true;
}
