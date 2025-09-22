//===- ReduceInstructionFlagsMIR.cpp - Specialized Delta Pass -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting MachineInstr flags from the MachineFunction.
//
//===----------------------------------------------------------------------===//

#include "ReduceInstructionFlagsMIR.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
using namespace llvm;

static void removeFlagsFromModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (const Function &F : WorkItem.getModule()) {
    if (auto *MF = WorkItem.MMI->getMachineFunction(F)) {
      for (MachineBasicBlock &MBB : *MF) {
        for (MachineInstr &MI : MBB) {
          // TODO: Should this clear flags individually?
          if (MI.getFlags() != 0 && !O.shouldKeep())
            MI.setFlags(0);
        }
      }
    }
  }
}

void llvm::reduceInstructionFlagsMIRDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, removeFlagsFromModule, "Reducing Instruction Flags");
}
