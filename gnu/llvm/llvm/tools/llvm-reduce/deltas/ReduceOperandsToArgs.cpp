//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ReduceOperandsToArgs.h"
#include "Delta.h"
#include "Utils.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

using namespace llvm;

static bool canReplaceFunction(Function *F) {
  return all_of(F->uses(), [](Use &Op) {
    if (auto *CI = dyn_cast<CallBase>(Op.getUser()))
      return &CI->getCalledOperandUse() == &Op;
    return false;
  });
}

static bool canReduceUse(Use &Op) {
  Value *Val = Op.get();
  Type *Ty = Val->getType();

  // Only replace operands that can be passed-by-value.
  if (!Ty->isFirstClassType())
    return false;

  // Don't pass labels/metadata as arguments.
  if (Ty->isLabelTy() || Ty->isMetadataTy())
    return false;

  // No need to replace values that are already arguments.
  if (isa<Argument>(Val))
    return false;

  // Do not replace literals.
  if (isa<ConstantData>(Val))
    return false;

  // Do not convert direct function calls to indirect calls.
  if (auto *CI = dyn_cast<CallBase>(Op.getUser()))
    if (&CI->getCalledOperandUse() == &Op)
      return false;

  return true;
}

/// Goes over OldF calls and replaces them with a call to NewF.
static void replaceFunctionCalls(Function *OldF, Function *NewF) {
  SmallVector<CallBase *> Callers;
  for (Use &U : OldF->uses()) {
    auto *CI = cast<CallBase>(U.getUser());
    assert(&U == &CI->getCalledOperandUse());
    assert(CI->getCalledFunction() == OldF);
    Callers.push_back(CI);
  }

  // Call arguments for NewF.
  SmallVector<Value *> Args(NewF->arg_size(), nullptr);

  // Fill up the additional parameters with default values.
  for (auto ArgIdx : llvm::seq<size_t>(OldF->arg_size(), NewF->arg_size())) {
    Type *NewArgTy = NewF->getArg(ArgIdx)->getType();
    Args[ArgIdx] = getDefaultValue(NewArgTy);
  }

  for (CallBase *CI : Callers) {
    // Preserve the original function arguments.
    for (auto Z : zip_first(CI->args(), Args))
      std::get<1>(Z) = std::get<0>(Z);

    // Also preserve operand bundles.
    SmallVector<OperandBundleDef> OperandBundles;
    CI->getOperandBundlesAsDefs(OperandBundles);

    // Create the new function call.
    CallBase *NewCI;
    if (auto *II = dyn_cast<InvokeInst>(CI)) {
      NewCI = InvokeInst::Create(NewF, cast<InvokeInst>(II)->getNormalDest(),
                                 cast<InvokeInst>(II)->getUnwindDest(), Args,
                                 OperandBundles, CI->getName());
    } else {
      assert(isa<CallInst>(CI));
      NewCI = CallInst::Create(NewF, Args, OperandBundles, CI->getName());
    }
    NewCI->setCallingConv(NewF->getCallingConv());

    // Do the replacement for this use.
    if (!CI->use_empty())
      CI->replaceAllUsesWith(NewCI);
    ReplaceInstWithInst(CI, NewCI);
  }
}

/// Add a new function argument to @p F for each use in @OpsToReplace, and
/// replace those operand values with the new function argument.
static void substituteOperandWithArgument(Function *OldF,
                                          ArrayRef<Use *> OpsToReplace) {
  if (OpsToReplace.empty())
    return;

  SetVector<Value *> UniqueValues;
  for (Use *Op : OpsToReplace)
    UniqueValues.insert(Op->get());

  // Determine the new function's signature.
  SmallVector<Type *> NewArgTypes;
  llvm::append_range(NewArgTypes, OldF->getFunctionType()->params());
  size_t ArgOffset = NewArgTypes.size();
  for (Value *V : UniqueValues)
    NewArgTypes.push_back(V->getType());
  FunctionType *FTy =
      FunctionType::get(OldF->getFunctionType()->getReturnType(), NewArgTypes,
                        OldF->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF =
      Function::Create(FTy, OldF->getLinkage(), OldF->getAddressSpace(),
                       OldF->getName(), OldF->getParent());

  // In order to preserve function order, we move NewF behind OldF
  NewF->removeFromParent();
  OldF->getParent()->getFunctionList().insertAfter(OldF->getIterator(), NewF);

  // Preserve the parameters of OldF.
  ValueToValueMapTy VMap;
  for (auto Z : zip_first(OldF->args(), NewF->args())) {
    Argument &OldArg = std::get<0>(Z);
    Argument &NewArg = std::get<1>(Z);

    NewArg.setName(OldArg.getName()); // Copy the name over...
    VMap[&OldArg] = &NewArg;          // Add mapping to VMap
  }

  // Adjust the new parameters.
  ValueToValueMapTy OldValMap;
  for (auto Z : zip_first(UniqueValues, drop_begin(NewF->args(), ArgOffset))) {
    Value *OldVal = std::get<0>(Z);
    Argument &NewArg = std::get<1>(Z);

    NewArg.setName(OldVal->getName());
    OldValMap[OldVal] = &NewArg;
  }

  SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
  CloneFunctionInto(NewF, OldF, VMap, CloneFunctionChangeType::LocalChangesOnly,
                    Returns, "", /*CodeInfo=*/nullptr);

  // Replace the actual operands.
  for (Use *Op : OpsToReplace) {
    Value *NewArg = OldValMap.lookup(Op->get());
    auto *NewUser = cast<Instruction>(VMap.lookup(Op->getUser()));

    if (PHINode *NewPhi = dyn_cast<PHINode>(NewUser)) {
      PHINode *OldPhi = cast<PHINode>(Op->getUser());
      BasicBlock *OldBB = OldPhi->getIncomingBlock(*Op);
      NewPhi->setIncomingValueForBlock(cast<BasicBlock>(VMap.lookup(OldBB)),
                                       NewArg);
    } else
      NewUser->setOperand(Op->getOperandNo(), NewArg);
  }

  // Replace all OldF uses with NewF.
  replaceFunctionCalls(OldF, NewF);

  // Rename NewF to OldF's name.
  std::string FName = OldF->getName().str();
  OldF->replaceAllUsesWith(NewF);
  OldF->eraseFromParent();
  NewF->setName(FName);
}

static void reduceOperandsToArgs(Oracle &O, ReducerWorkItem &WorkItem) {
  Module &Program = WorkItem.getModule();

  SmallVector<Use *> OperandsToReduce;
  for (Function &F : make_early_inc_range(Program.functions())) {
    if (!canReplaceFunction(&F))
      continue;
    OperandsToReduce.clear();
    for (Instruction &I : instructions(&F)) {
      for (Use &Op : I.operands()) {
        if (!canReduceUse(Op))
          continue;
        if (O.shouldKeep())
          continue;

        OperandsToReduce.push_back(&Op);
      }
    }

    substituteOperandWithArgument(&F, OperandsToReduce);
  }
}

void llvm::reduceOperandsToArgsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceOperandsToArgs,
               "Converting operands to function arguments");
}
