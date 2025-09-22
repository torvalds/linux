//===- ReduceRegisterMasks.cpp - Specialized Delta Pass -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce custom register masks from the MachineFunction.
//
//===----------------------------------------------------------------------===//

#include "ReduceRegisterMasks.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

static void reduceMasksInFunction(Oracle &O, MachineFunction &MF) {
  DenseSet<const uint32_t *> ConstRegisterMasks;
  const auto *TRI = MF.getSubtarget().getRegisterInfo();

  // Track predefined/named regmasks which we ignore.
  const unsigned NumRegs = TRI->getNumRegs();
  for (const uint32_t *Mask : TRI->getRegMasks())
    ConstRegisterMasks.insert(Mask);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isRegMask())
          continue;

        const uint32_t *OldRegMask = MO.getRegMask();
        // We're only reducing custom reg masks.
        if (ConstRegisterMasks.count(OldRegMask))
          continue;
        unsigned RegMaskSize =
            MachineOperand::getRegMaskSize(TRI->getNumRegs());
        std::vector<uint32_t> NewMask(RegMaskSize);

        bool MadeChange = false;
        for (unsigned I = 0; I != NumRegs; ++I) {
          if (OldRegMask[I / 32] & (1u << (I % 32))) {
            if (O.shouldKeep())
              NewMask[I / 32] |= 1u << (I % 32);
          } else
            MadeChange = true;
        }

        if (MadeChange) {
          uint32_t *UpdatedMask = MF.allocateRegMask();
          std::memcpy(UpdatedMask, NewMask.data(),
                      RegMaskSize * sizeof(*OldRegMask));
          MO.setRegMask(UpdatedMask);
        }
      }
    }
  }
}

static void reduceMasksInModule(Oracle &O, ReducerWorkItem &WorkItem) {
  for (const Function &F : WorkItem.getModule()) {
    if (auto *MF = WorkItem.MMI->getMachineFunction(F))
      reduceMasksInFunction(O, *MF);
  }
}

void llvm::reduceRegisterMasksMIRDeltaPass(TestRunner &Test) {
  runDeltaPass(Test, reduceMasksInModule, "Reducing register masks");
}
