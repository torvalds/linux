//===------------ BPFIRPeephole.cpp - IR Peephole Transformation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// IR level peephole optimization, specifically removing @llvm.stacksave() and
// @llvm.stackrestore().
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"

#define DEBUG_TYPE "bpf-ir-peephole"

using namespace llvm;

namespace {

static bool BPFIRPeepholeImpl(Function &F) {
  LLVM_DEBUG(dbgs() << "******** BPF IR Peephole ********\n");

  bool Changed = false;
  Instruction *ToErase = nullptr;
  for (auto &BB : F) {
    for (auto &I : BB) {
      // The following code pattern is handled:
      //     %3 = call i8* @llvm.stacksave()
      //     store i8* %3, i8** %saved_stack, align 8
      //     ...
      //     %4 = load i8*, i8** %saved_stack, align 8
      //     call void @llvm.stackrestore(i8* %4)
      //     ...
      // The goal is to remove the above four instructions,
      // so we won't have instructions with r11 (stack pointer)
      // if eventually there is no variable length stack allocation.
      // InstrCombine also tries to remove the above instructions,
      // if it is proven safe (constant alloca etc.), but depending
      // on code pattern, it may still miss some.
      //
      // With unconditionally removing these instructions, if alloca is
      // constant, we are okay then. Otherwise, SelectionDag will complain
      // since BPF does not support dynamic allocation yet.
      if (ToErase) {
        ToErase->eraseFromParent();
        ToErase = nullptr;
      }

      if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
        if (II->getIntrinsicID() != Intrinsic::stacksave)
          continue;
        if (!II->hasOneUser())
          continue;
        auto *Inst = cast<Instruction>(*II->user_begin());
        LLVM_DEBUG(dbgs() << "Remove:"; I.dump());
        LLVM_DEBUG(dbgs() << "Remove:"; Inst->dump(); dbgs() << '\n');
        Changed = true;
        Inst->eraseFromParent();
        ToErase = &I;
        continue;
      }

      if (auto *LD = dyn_cast<LoadInst>(&I)) {
        if (!LD->hasOneUser())
          continue;
        auto *II = dyn_cast<IntrinsicInst>(*LD->user_begin());
        if (!II)
          continue;
        if (II->getIntrinsicID() != Intrinsic::stackrestore)
          continue;
        LLVM_DEBUG(dbgs() << "Remove:"; I.dump());
        LLVM_DEBUG(dbgs() << "Remove:"; II->dump(); dbgs() << '\n');
        Changed = true;
        II->eraseFromParent();
        ToErase = &I;
      }
    }
  }

  return Changed;
}
} // End anonymous namespace

PreservedAnalyses BPFIRPeepholePass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  return BPFIRPeepholeImpl(F) ? PreservedAnalyses::none()
                              : PreservedAnalyses::all();
}
