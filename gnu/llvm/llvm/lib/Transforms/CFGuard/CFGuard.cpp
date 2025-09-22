//===-- CFGuard.cpp - Control Flow Guard checks -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the IR transform to add Microsoft's Control Flow Guard
/// checks on Windows targets.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/CFGuard.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

using OperandBundleDef = OperandBundleDefT<Value *>;

#define DEBUG_TYPE "cfguard"

STATISTIC(CFGuardCounter, "Number of Control Flow Guard checks added");

namespace {

/// Adds Control Flow Guard (CFG) checks on indirect function calls/invokes.
/// These checks ensure that the target address corresponds to the start of an
/// address-taken function. X86_64 targets use the Mechanism::Dispatch
/// mechanism. X86, ARM, and AArch64 targets use the Mechanism::Check machanism.
class CFGuardImpl {
public:
  using Mechanism = CFGuardPass::Mechanism;

  CFGuardImpl(Mechanism M) : GuardMechanism(M) {
    // Get or insert the guard check or dispatch global symbols.
    switch (GuardMechanism) {
    case Mechanism::Check:
      GuardFnName = "__guard_check_icall_fptr";
      break;
    case Mechanism::Dispatch:
      GuardFnName = "__guard_dispatch_icall_fptr";
      break;
    }
  }

  /// Inserts a Control Flow Guard (CFG) check on an indirect call using the CFG
  /// check mechanism. When the image is loaded, the loader puts the appropriate
  /// guard check function pointer in the __guard_check_icall_fptr global
  /// symbol. This checks that the target address is a valid address-taken
  /// function. The address of the target function is passed to the guard check
  /// function in an architecture-specific register (e.g. ECX on 32-bit X86,
  /// X15 on Aarch64, and R0 on ARM). The guard check function has no return
  /// value (if the target is invalid, the guard check funtion will raise an
  /// error).
  ///
  /// For example, the following LLVM IR:
  /// \code
  ///   %func_ptr = alloca i32 ()*, align 8
  ///   store i32 ()* @target_func, i32 ()** %func_ptr, align 8
  ///   %0 = load i32 ()*, i32 ()** %func_ptr, align 8
  ///   %1 = call i32 %0()
  /// \endcode
  ///
  /// is transformed to:
  /// \code
  ///   %func_ptr = alloca i32 ()*, align 8
  ///   store i32 ()* @target_func, i32 ()** %func_ptr, align 8
  ///   %0 = load i32 ()*, i32 ()** %func_ptr, align 8
  ///   %1 = load void (i8*)*, void (i8*)** @__guard_check_icall_fptr
  ///   %2 = bitcast i32 ()* %0 to i8*
  ///   call cfguard_checkcc void %1(i8* %2)
  ///   %3 = call i32 %0()
  /// \endcode
  ///
  /// For example, the following X86 assembly code:
  /// \code
  ///   movl  $_target_func, %eax
  ///   calll *%eax
  /// \endcode
  ///
  /// is transformed to:
  /// \code
  /// 	movl	$_target_func, %ecx
  /// 	calll	*___guard_check_icall_fptr
  /// 	calll	*%ecx
  /// \endcode
  ///
  /// \param CB indirect call to instrument.
  void insertCFGuardCheck(CallBase *CB);

  /// Inserts a Control Flow Guard (CFG) check on an indirect call using the CFG
  /// dispatch mechanism. When the image is loaded, the loader puts the
  /// appropriate guard check function pointer in the
  /// __guard_dispatch_icall_fptr global symbol. This checks that the target
  /// address is a valid address-taken function and, if so, tail calls the
  /// target. The target address is passed in an architecture-specific register
  /// (e.g. RAX on X86_64), with all other arguments for the target function
  /// passed as usual.
  ///
  /// For example, the following LLVM IR:
  /// \code
  ///   %func_ptr = alloca i32 ()*, align 8
  ///   store i32 ()* @target_func, i32 ()** %func_ptr, align 8
  ///   %0 = load i32 ()*, i32 ()** %func_ptr, align 8
  ///   %1 = call i32 %0()
  /// \endcode
  ///
  /// is transformed to:
  /// \code
  ///   %func_ptr = alloca i32 ()*, align 8
  ///   store i32 ()* @target_func, i32 ()** %func_ptr, align 8
  ///   %0 = load i32 ()*, i32 ()** %func_ptr, align 8
  ///   %1 = load i32 ()*, i32 ()** @__guard_dispatch_icall_fptr
  ///   %2 = call i32 %1() [ "cfguardtarget"(i32 ()* %0) ]
  /// \endcode
  ///
  /// For example, the following X86_64 assembly code:
  /// \code
  ///   leaq   target_func(%rip), %rax
  ///	  callq  *%rax
  /// \endcode
  ///
  /// is transformed to:
  /// \code
  ///   leaq   target_func(%rip), %rax
  ///   callq  *__guard_dispatch_icall_fptr(%rip)
  /// \endcode
  ///
  /// \param CB indirect call to instrument.
  void insertCFGuardDispatch(CallBase *CB);

  bool doInitialization(Module &M);
  bool runOnFunction(Function &F);

private:
  // Only add checks if the module has the cfguard=2 flag.
  int cfguard_module_flag = 0;
  StringRef GuardFnName;
  Mechanism GuardMechanism = Mechanism::Check;
  FunctionType *GuardFnType = nullptr;
  PointerType *GuardFnPtrType = nullptr;
  Constant *GuardFnGlobal = nullptr;
};

class CFGuard : public FunctionPass {
  CFGuardImpl Impl;

public:
  static char ID;

  // Default constructor required for the INITIALIZE_PASS macro.
  CFGuard(CFGuardImpl::Mechanism M) : FunctionPass(ID), Impl(M) {
    initializeCFGuardPass(*PassRegistry::getPassRegistry());
  }

  bool doInitialization(Module &M) override { return Impl.doInitialization(M); }
  bool runOnFunction(Function &F) override { return Impl.runOnFunction(F); }
};

} // end anonymous namespace

void CFGuardImpl::insertCFGuardCheck(CallBase *CB) {

  assert(Triple(CB->getModule()->getTargetTriple()).isOSWindows() &&
         "Only applicable for Windows targets");
  assert(CB->isIndirectCall() &&
         "Control Flow Guard checks can only be added to indirect calls");

  IRBuilder<> B(CB);
  Value *CalledOperand = CB->getCalledOperand();

  // If the indirect call is called within catchpad or cleanuppad,
  // we need to copy "funclet" bundle of the call.
  SmallVector<llvm::OperandBundleDef, 1> Bundles;
  if (auto Bundle = CB->getOperandBundle(LLVMContext::OB_funclet))
    Bundles.push_back(OperandBundleDef(*Bundle));

  // Load the global symbol as a pointer to the check function.
  LoadInst *GuardCheckLoad = B.CreateLoad(GuardFnPtrType, GuardFnGlobal);

  // Create new call instruction. The CFGuard check should always be a call,
  // even if the original CallBase is an Invoke or CallBr instruction.
  CallInst *GuardCheck =
      B.CreateCall(GuardFnType, GuardCheckLoad, {CalledOperand}, Bundles);

  // Ensure that the first argument is passed in the correct register
  // (e.g. ECX on 32-bit X86 targets).
  GuardCheck->setCallingConv(CallingConv::CFGuard_Check);
}

void CFGuardImpl::insertCFGuardDispatch(CallBase *CB) {

  assert(Triple(CB->getModule()->getTargetTriple()).isOSWindows() &&
         "Only applicable for Windows targets");
  assert(CB->isIndirectCall() &&
         "Control Flow Guard checks can only be added to indirect calls");

  IRBuilder<> B(CB);
  Value *CalledOperand = CB->getCalledOperand();
  Type *CalledOperandType = CalledOperand->getType();

  // Load the global as a pointer to a function of the same type.
  LoadInst *GuardDispatchLoad = B.CreateLoad(CalledOperandType, GuardFnGlobal);

  // Add the original call target as a cfguardtarget operand bundle.
  SmallVector<llvm::OperandBundleDef, 1> Bundles;
  CB->getOperandBundlesAsDefs(Bundles);
  Bundles.emplace_back("cfguardtarget", CalledOperand);

  // Create a copy of the call/invoke instruction and add the new bundle.
  assert((isa<CallInst>(CB) || isa<InvokeInst>(CB)) &&
         "Unknown indirect call type");
  CallBase *NewCB = CallBase::Create(CB, Bundles, CB->getIterator());

  // Change the target of the call to be the guard dispatch function.
  NewCB->setCalledOperand(GuardDispatchLoad);

  // Replace the original call/invoke with the new instruction.
  CB->replaceAllUsesWith(NewCB);

  // Delete the original call/invoke.
  CB->eraseFromParent();
}

bool CFGuardImpl::doInitialization(Module &M) {

  // Check if this module has the cfguard flag and read its value.
  if (auto *MD =
          mdconst::extract_or_null<ConstantInt>(M.getModuleFlag("cfguard")))
    cfguard_module_flag = MD->getZExtValue();

  // Skip modules for which CFGuard checks have been disabled.
  if (cfguard_module_flag != 2)
    return false;

  // Set up prototypes for the guard check and dispatch functions.
  GuardFnType =
      FunctionType::get(Type::getVoidTy(M.getContext()),
                        {PointerType::getUnqual(M.getContext())}, false);
  GuardFnPtrType = PointerType::get(GuardFnType, 0);

  GuardFnGlobal = M.getOrInsertGlobal(GuardFnName, GuardFnPtrType, [&] {
    auto *Var = new GlobalVariable(M, GuardFnPtrType, false,
                                   GlobalVariable::ExternalLinkage, nullptr,
                                   GuardFnName);
    Var->setDSOLocal(true);
    return Var;
  });

  return true;
}

bool CFGuardImpl::runOnFunction(Function &F) {

  // Skip modules for which CFGuard checks have been disabled.
  if (cfguard_module_flag != 2)
    return false;

  SmallVector<CallBase *, 8> IndirectCalls;

  // Iterate over the instructions to find all indirect call/invoke/callbr
  // instructions. Make a separate list of pointers to indirect
  // call/invoke/callbr instructions because the original instructions will be
  // deleted as the checks are added.
  for (BasicBlock &BB : F) {
    for (Instruction &I : BB) {
      auto *CB = dyn_cast<CallBase>(&I);
      if (CB && CB->isIndirectCall() && !CB->hasFnAttr("guard_nocf")) {
        IndirectCalls.push_back(CB);
        CFGuardCounter++;
      }
    }
  }

  // If no checks are needed, return early.
  if (IndirectCalls.empty()) {
    return false;
  }

  // For each indirect call/invoke, add the appropriate dispatch or check.
  if (GuardMechanism == Mechanism::Dispatch) {
    for (CallBase *CB : IndirectCalls) {
      insertCFGuardDispatch(CB);
    }
  } else {
    for (CallBase *CB : IndirectCalls) {
      insertCFGuardCheck(CB);
    }
  }

  return true;
}

PreservedAnalyses CFGuardPass::run(Function &F, FunctionAnalysisManager &FAM) {
  CFGuardImpl Impl(GuardMechanism);
  bool Changed = Impl.doInitialization(*F.getParent());
  Changed |= Impl.runOnFunction(F);
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

char CFGuard::ID = 0;
INITIALIZE_PASS(CFGuard, "CFGuard", "CFGuard", false, false)

FunctionPass *llvm::createCFGuardCheckPass() {
  return new CFGuard(CFGuardPass::Mechanism::Check);
}

FunctionPass *llvm::createCFGuardDispatchPass() {
  return new CFGuard(CFGuardPass::Mechanism::Dispatch);
}
