//===- ObjCARCAPElim.cpp - ObjC ARC Optimization --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines ObjC ARC optimizations. ARC stands for Automatic
/// Reference Counting and is a system for managing reference counts for objects
/// in Objective C.
///
/// This specific file implements optimizations which remove extraneous
/// autorelease pools.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/Analysis/ObjCARCInstKind.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/ObjCARC.h"

using namespace llvm;
using namespace llvm::objcarc;

#define DEBUG_TYPE "objc-arc-ap-elim"

namespace {

/// Interprocedurally determine if calls made by the given call site can
/// possibly produce autoreleases.
bool MayAutorelease(const CallBase &CB, unsigned Depth = 0) {
  if (const Function *Callee = CB.getCalledFunction()) {
    if (!Callee->hasExactDefinition())
      return true;
    for (const BasicBlock &BB : *Callee) {
      for (const Instruction &I : BB)
        if (const CallBase *JCB = dyn_cast<CallBase>(&I))
          // This recursion depth limit is arbitrary. It's just great
          // enough to cover known interesting testcases.
          if (Depth < 3 && !JCB->onlyReadsMemory() &&
              MayAutorelease(*JCB, Depth + 1))
            return true;
    }
    return false;
  }

  return true;
}

bool OptimizeBB(BasicBlock *BB) {
  bool Changed = false;

  Instruction *Push = nullptr;
  for (Instruction &Inst : llvm::make_early_inc_range(*BB)) {
    switch (GetBasicARCInstKind(&Inst)) {
    case ARCInstKind::AutoreleasepoolPush:
      Push = &Inst;
      break;
    case ARCInstKind::AutoreleasepoolPop:
      // If this pop matches a push and nothing in between can autorelease,
      // zap the pair.
      if (Push && cast<CallInst>(&Inst)->getArgOperand(0) == Push) {
        Changed = true;
        LLVM_DEBUG(dbgs() << "ObjCARCAPElim::OptimizeBB: Zapping push pop "
                             "autorelease pair:\n"
                             "                           Pop: "
                          << Inst << "\n"
                          << "                           Push: " << *Push
                          << "\n");
        Inst.eraseFromParent();
        Push->eraseFromParent();
      }
      Push = nullptr;
      break;
    case ARCInstKind::CallOrUser:
      if (MayAutorelease(cast<CallBase>(Inst)))
        Push = nullptr;
      break;
    default:
      break;
    }
  }

  return Changed;
}

bool runImpl(Module &M) {
  if (!EnableARCOpts)
    return false;

  // If nothing in the Module uses ARC, don't do anything.
  if (!ModuleHasARC(M))
    return false;
  // Find the llvm.global_ctors variable, as the first step in
  // identifying the global constructors. In theory, unnecessary autorelease
  // pools could occur anywhere, but in practice it's pretty rare. Global
  // ctors are a place where autorelease pools get inserted automatically,
  // so it's pretty common for them to be unnecessary, and it's pretty
  // profitable to eliminate them.
  GlobalVariable *GV = M.getGlobalVariable("llvm.global_ctors");
  if (!GV)
    return false;

  assert(GV->hasDefinitiveInitializer() &&
         "llvm.global_ctors is uncooperative!");

  bool Changed = false;

  // Dig the constructor functions out of GV's initializer.
  ConstantArray *Init = cast<ConstantArray>(GV->getInitializer());
  for (User::op_iterator OI = Init->op_begin(), OE = Init->op_end();
       OI != OE; ++OI) {
    Value *Op = *OI;
    // llvm.global_ctors is an array of three-field structs where the second
    // members are constructor functions.
    Function *F = dyn_cast<Function>(cast<ConstantStruct>(Op)->getOperand(1));
    // If the user used a constructor function with the wrong signature and
    // it got bitcasted or whatever, look the other way.
    if (!F)
      continue;
    // Only look at function definitions.
    if (F->isDeclaration())
      continue;
    // Only look at functions with one basic block.
    if (std::next(F->begin()) != F->end())
      continue;
    // Ok, a single-block constructor function definition. Try to optimize it.
    Changed |= OptimizeBB(&F->front());
  }

  return Changed;
}

} // namespace

PreservedAnalyses ObjCARCAPElimPass::run(Module &M, ModuleAnalysisManager &AM) {
  if (!runImpl(M))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
