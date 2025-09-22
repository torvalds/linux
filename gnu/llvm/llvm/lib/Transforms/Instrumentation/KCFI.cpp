//===-- KCFI.cpp - Generic KCFI operand bundle lowering ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass emits generic KCFI indirect call checks for targets that don't
// support lowering KCFI operand bundles in the back-end.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/KCFI.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "kcfi"

STATISTIC(NumKCFIChecks, "Number of kcfi operands transformed into checks");

namespace {
class DiagnosticInfoKCFI : public DiagnosticInfo {
  const Twine &Msg;

public:
  DiagnosticInfoKCFI(const Twine &DiagMsg,
                     DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_Linker, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
} // namespace

PreservedAnalyses KCFIPass::run(Function &F, FunctionAnalysisManager &AM) {
  Module &M = *F.getParent();
  if (!M.getModuleFlag("kcfi"))
    return PreservedAnalyses::all();

  // Find call instructions with KCFI operand bundles.
  SmallVector<CallInst *> KCFICalls;
  for (Instruction &I : instructions(F)) {
    if (auto *CI = dyn_cast<CallInst>(&I))
      if (CI->getOperandBundle(LLVMContext::OB_kcfi))
        KCFICalls.push_back(CI);
  }

  if (KCFICalls.empty())
    return PreservedAnalyses::all();

  LLVMContext &Ctx = M.getContext();
  // patchable-function-prefix emits nops between the KCFI type identifier
  // and the function start. As we don't know the size of the emitted nops,
  // don't allow this attribute with generic lowering.
  if (F.hasFnAttribute("patchable-function-prefix"))
    Ctx.diagnose(
        DiagnosticInfoKCFI("-fpatchable-function-entry=N,M, where M>0 is not "
                           "compatible with -fsanitize=kcfi on this target"));

  IntegerType *Int32Ty = Type::getInt32Ty(Ctx);
  MDNode *VeryUnlikelyWeights = MDBuilder(Ctx).createUnlikelyBranchWeights();
  Triple T(M.getTargetTriple());

  for (CallInst *CI : KCFICalls) {
    // Get the expected hash value.
    const uint32_t ExpectedHash =
        cast<ConstantInt>(CI->getOperandBundle(LLVMContext::OB_kcfi)->Inputs[0])
            ->getZExtValue();

    // Drop the KCFI operand bundle.
    CallBase *Call = CallBase::removeOperandBundle(CI, LLVMContext::OB_kcfi,
                                                   CI->getIterator());
    assert(Call != CI);
    Call->copyMetadata(*CI);
    CI->replaceAllUsesWith(Call);
    CI->eraseFromParent();

    if (!Call->isIndirectCall())
      continue;

    // Emit a check and trap if the target hash doesn't match.
    IRBuilder<> Builder(Call);
    Value *FuncPtr = Call->getCalledOperand();
    // ARM uses the least significant bit of the function pointer to select
    // between ARM and Thumb modes for the callee. Instructions are always
    // at least 16-bit aligned, so clear the LSB before we compute the hash
    // location.
    if (T.isARM() || T.isThumb()) {
      FuncPtr = Builder.CreateIntToPtr(
          Builder.CreateAnd(Builder.CreatePtrToInt(FuncPtr, Int32Ty),
                            ConstantInt::get(Int32Ty, -2)),
          FuncPtr->getType());
    }
    Value *HashPtr = Builder.CreateConstInBoundsGEP1_32(Int32Ty, FuncPtr, -1);
    Value *Test = Builder.CreateICmpNE(Builder.CreateLoad(Int32Ty, HashPtr),
                                       ConstantInt::get(Int32Ty, ExpectedHash));
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Test, Call, false, VeryUnlikelyWeights);
    Builder.SetInsertPoint(ThenTerm);
    Builder.CreateCall(Intrinsic::getDeclaration(&M, Intrinsic::debugtrap));
    ++NumKCFIChecks;
  }

  return PreservedAnalyses::none();
}
