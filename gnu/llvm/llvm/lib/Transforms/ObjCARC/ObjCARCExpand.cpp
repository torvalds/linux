//===- ObjCARCExpand.cpp - ObjC ARC Optimization --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines ObjC ARC optimizations. ARC stands for Automatic
/// Reference Counting and is a system for managing reference counts for objects
/// in Objective C.
///
/// This specific file deals with early optimizations which perform certain
/// cleanup operations.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ObjCARCAnalysisUtils.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/ObjCARC.h"

#define DEBUG_TYPE "objc-arc-expand"

using namespace llvm;
using namespace llvm::objcarc;

namespace {
static bool runImpl(Function &F) {
  if (!EnableARCOpts)
    return false;

  // If nothing in the Module uses ARC, don't do anything.
  if (!ModuleHasARC(*F.getParent()))
    return false;

  bool Changed = false;

  LLVM_DEBUG(dbgs() << "ObjCARCExpand: Visiting Function: " << F.getName()
                    << "\n");

  for (Instruction &Inst : instructions(&F)) {
    LLVM_DEBUG(dbgs() << "ObjCARCExpand: Visiting: " << Inst << "\n");

    switch (GetBasicARCInstKind(&Inst)) {
    case ARCInstKind::Retain:
    case ARCInstKind::RetainRV:
    case ARCInstKind::Autorelease:
    case ARCInstKind::AutoreleaseRV:
    case ARCInstKind::FusedRetainAutorelease:
    case ARCInstKind::FusedRetainAutoreleaseRV: {
      // These calls return their argument verbatim, as a low-level
      // optimization. However, this makes high-level optimizations
      // harder. Undo any uses of this optimization that the front-end
      // emitted here. We'll redo them in the contract pass.
      Changed = true;
      Value *Value = cast<CallInst>(&Inst)->getArgOperand(0);
      LLVM_DEBUG(dbgs() << "ObjCARCExpand: Old = " << Inst
                        << "\n"
                           "               New = "
                        << *Value << "\n");
      Inst.replaceAllUsesWith(Value);
      break;
    }
    default:
      break;
    }
  }

  LLVM_DEBUG(dbgs() << "ObjCARCExpand: Finished List.\n\n");

  return Changed;
}

} // namespace

PreservedAnalyses ObjCARCExpandPass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  if (!runImpl(F))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  return PA;
}
