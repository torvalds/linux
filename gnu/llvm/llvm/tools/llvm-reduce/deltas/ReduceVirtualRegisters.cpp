//===- ReduceVirtualRegisters.cpp - Specialized Delta Pass ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to simplify virtual registers in MIR.
//
//===----------------------------------------------------------------------===//

#include "ReduceVirtualRegisters.h"
#include "Delta.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

static void dropRegisterHintsFromFunction(Oracle &O, MachineFunction &MF) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register Reg = Register::index2VirtReg(I);

    const std::pair<unsigned, SmallVector<Register, 4>> &Hints =
        MRI.getRegAllocationHints(Reg);
    if (Hints.second.empty())
      continue;

    if (!O.shouldKeep())
      MRI.clearSimpleHint(Reg);
  }
}

static void dropRegisterHintsFromFunctions(Oracle &O,
                                           ReducerWorkItem &WorkItem) {
  for (const Function &F : WorkItem.getModule()) {
    if (auto *MF = WorkItem.MMI->getMachineFunction(F))
      dropRegisterHintsFromFunction(O, *MF);
  }
}

void llvm::reduceVirtualRegisterHintsDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, dropRegisterHintsFromFunctions,
               "Reducing virtual register hints from functions");
}
