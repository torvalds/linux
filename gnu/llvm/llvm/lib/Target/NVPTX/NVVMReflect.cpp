//===- NVVMReflect.cpp - NVVM Emulate conditional compilation -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass replaces occurrences of __nvvm_reflect("foo") and llvm.nvvm.reflect
// with an integer.
//
// We choose the value we use by looking at metadata in the module itself.  Note
// that we intentionally only have one way to choose these values, because other
// parts of LLVM (particularly, InstCombineCall) rely on being able to predict
// the values chosen by this pass.
//
// If we see an unknown string, we replace its call with 0.
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <sstream>
#include <string>
#define NVVM_REFLECT_FUNCTION "__nvvm_reflect"
#define NVVM_REFLECT_OCL_FUNCTION "__nvvm_reflect_ocl"

using namespace llvm;

#define DEBUG_TYPE "nvptx-reflect"

namespace llvm { void initializeNVVMReflectPass(PassRegistry &); }

namespace {
class NVVMReflect : public FunctionPass {
public:
  static char ID;
  unsigned int SmVersion;
  NVVMReflect() : NVVMReflect(0) {}
  explicit NVVMReflect(unsigned int Sm) : FunctionPass(ID), SmVersion(Sm) {
    initializeNVVMReflectPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &) override;
};
}

FunctionPass *llvm::createNVVMReflectPass(unsigned int SmVersion) {
  return new NVVMReflect(SmVersion);
}

static cl::opt<bool>
NVVMReflectEnabled("nvvm-reflect-enable", cl::init(true), cl::Hidden,
                   cl::desc("NVVM reflection, enabled by default"));

char NVVMReflect::ID = 0;
INITIALIZE_PASS(NVVMReflect, "nvvm-reflect",
                "Replace occurrences of __nvvm_reflect() calls with 0/1", false,
                false)

static bool runNVVMReflect(Function &F, unsigned SmVersion) {
  if (!NVVMReflectEnabled)
    return false;

  if (F.getName() == NVVM_REFLECT_FUNCTION ||
      F.getName() == NVVM_REFLECT_OCL_FUNCTION) {
    assert(F.isDeclaration() && "_reflect function should not have a body");
    assert(F.getReturnType()->isIntegerTy() &&
           "_reflect's return type should be integer");
    return false;
  }

  SmallVector<Instruction *, 4> ToRemove;
  SmallVector<Instruction *, 4> ToSimplify;

  // Go through the calls in this function.  Each call to __nvvm_reflect or
  // llvm.nvvm.reflect should be a CallInst with a ConstantArray argument.
  // First validate that. If the c-string corresponding to the ConstantArray can
  // be found successfully, see if it can be found in VarMap. If so, replace the
  // uses of CallInst with the value found in VarMap. If not, replace the use
  // with value 0.

  // The IR for __nvvm_reflect calls differs between CUDA versions.
  //
  // CUDA 6.5 and earlier uses this sequence:
  //    %ptr = tail call i8* @llvm.nvvm.ptr.constant.to.gen.p0i8.p4i8
  //        (i8 addrspace(4)* getelementptr inbounds
  //           ([8 x i8], [8 x i8] addrspace(4)* @str, i32 0, i32 0))
  //    %reflect = tail call i32 @__nvvm_reflect(i8* %ptr)
  //
  // The value returned by Sym->getOperand(0) is a Constant with a
  // ConstantDataSequential operand which can be converted to string and used
  // for lookup.
  //
  // CUDA 7.0 does it slightly differently:
  //   %reflect = call i32 @__nvvm_reflect(i8* addrspacecast
  //        (i8 addrspace(1)* getelementptr inbounds
  //           ([8 x i8], [8 x i8] addrspace(1)* @str, i32 0, i32 0) to i8*))
  //
  // In this case, we get a Constant with a GlobalVariable operand and we need
  // to dig deeper to find its initializer with the string we'll use for lookup.
  for (Instruction &I : instructions(F)) {
    CallInst *Call = dyn_cast<CallInst>(&I);
    if (!Call)
      continue;
    Function *Callee = Call->getCalledFunction();
    if (!Callee || (Callee->getName() != NVVM_REFLECT_FUNCTION &&
                    Callee->getName() != NVVM_REFLECT_OCL_FUNCTION &&
                    Callee->getIntrinsicID() != Intrinsic::nvvm_reflect))
      continue;

    // FIXME: Improve error handling here and elsewhere in this pass.
    assert(Call->getNumOperands() == 2 &&
           "Wrong number of operands to __nvvm_reflect function");

    // In cuda 6.5 and earlier, we will have an extra constant-to-generic
    // conversion of the string.
    const Value *Str = Call->getArgOperand(0);
    if (const CallInst *ConvCall = dyn_cast<CallInst>(Str)) {
      // FIXME: Add assertions about ConvCall.
      Str = ConvCall->getArgOperand(0);
    }
    // Pre opaque pointers we have a constant expression wrapping the constant
    // string.
    Str = Str->stripPointerCasts();
    assert(isa<Constant>(Str) &&
           "Format of __nvvm_reflect function not recognized");

    const Value *Operand = cast<Constant>(Str)->getOperand(0);
    if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(Operand)) {
      // For CUDA-7.0 style __nvvm_reflect calls, we need to find the operand's
      // initializer.
      assert(GV->hasInitializer() &&
             "Format of _reflect function not recognized");
      const Constant *Initializer = GV->getInitializer();
      Operand = Initializer;
    }

    assert(isa<ConstantDataSequential>(Operand) &&
           "Format of _reflect function not recognized");
    assert(cast<ConstantDataSequential>(Operand)->isCString() &&
           "Format of _reflect function not recognized");

    StringRef ReflectArg = cast<ConstantDataSequential>(Operand)->getAsString();
    ReflectArg = ReflectArg.substr(0, ReflectArg.size() - 1);
    LLVM_DEBUG(dbgs() << "Arg of _reflect : " << ReflectArg << "\n");

    int ReflectVal = 0; // The default value is 0
    if (ReflectArg == "__CUDA_FTZ") {
      // Try to pull __CUDA_FTZ from the nvvm-reflect-ftz module flag.  Our
      // choice here must be kept in sync with AutoUpgrade, which uses the same
      // technique to detect whether ftz is enabled.
      if (auto *Flag = mdconst::extract_or_null<ConstantInt>(
              F.getParent()->getModuleFlag("nvvm-reflect-ftz")))
        ReflectVal = Flag->getSExtValue();
    } else if (ReflectArg == "__CUDA_ARCH") {
      ReflectVal = SmVersion * 10;
    }

    // If the immediate user is a simple comparison we want to simplify it.
    for (User *U : Call->users())
      if (Instruction *I = dyn_cast<Instruction>(U))
        ToSimplify.push_back(I);

    Call->replaceAllUsesWith(ConstantInt::get(Call->getType(), ReflectVal));
    ToRemove.push_back(Call);
  }

  // The code guarded by __nvvm_reflect may be invalid for the target machine.
  // Traverse the use-def chain, continually simplifying constant expressions
  // until we find a terminator that we can then remove.
  while (!ToSimplify.empty()) {
    Instruction *I = ToSimplify.pop_back_val();
    if (Constant *C =
            ConstantFoldInstruction(I, F.getDataLayout())) {
      for (User *U : I->users())
        if (Instruction *I = dyn_cast<Instruction>(U))
          ToSimplify.push_back(I);

      I->replaceAllUsesWith(C);
      if (isInstructionTriviallyDead(I)) {
        ToRemove.push_back(I);
      }
    } else if (I->isTerminator()) {
      ConstantFoldTerminator(I->getParent());
    }
  }

  // Removing via isInstructionTriviallyDead may add duplicates to the ToRemove
  // array. Filter out the duplicates before starting to erase from parent.
  std::sort(ToRemove.begin(), ToRemove.end());
  auto NewLastIter = llvm::unique(ToRemove);
  ToRemove.erase(NewLastIter, ToRemove.end());

  for (Instruction *I : ToRemove)
    I->eraseFromParent();

  return ToRemove.size() > 0;
}

bool NVVMReflect::runOnFunction(Function &F) {
  return runNVVMReflect(F, SmVersion);
}

NVVMReflectPass::NVVMReflectPass() : NVVMReflectPass(0) {}

PreservedAnalyses NVVMReflectPass::run(Function &F,
                                       FunctionAnalysisManager &AM) {
  return runNVVMReflect(F, SmVersion) ? PreservedAnalyses::none()
                                      : PreservedAnalyses::all();
}
