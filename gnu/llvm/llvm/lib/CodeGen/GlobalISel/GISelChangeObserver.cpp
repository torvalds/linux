//===-- lib/CodeGen/GlobalISel/GISelChangeObserver.cpp --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file constains common code to combine machine functions at generic
// level.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

void GISelChangeObserver::changingAllUsesOfReg(
    const MachineRegisterInfo &MRI, Register Reg) {
  for (auto &ChangingMI : MRI.use_instructions(Reg)) {
    changingInstr(ChangingMI);
    ChangingAllUsesOfReg.insert(&ChangingMI);
  }
}

void GISelChangeObserver::finishedChangingAllUsesOfReg() {
  for (auto *ChangedMI : ChangingAllUsesOfReg)
    changedInstr(*ChangedMI);
  ChangingAllUsesOfReg.clear();
}

RAIIDelegateInstaller::RAIIDelegateInstaller(MachineFunction &MF,
                                             MachineFunction::Delegate *Del)
    : MF(MF), Delegate(Del) {
  // Register this as the delegate for handling insertions and deletions of
  // instructions.
  MF.setDelegate(Del);
}

RAIIDelegateInstaller::~RAIIDelegateInstaller() { MF.resetDelegate(Delegate); }

RAIIMFObserverInstaller::RAIIMFObserverInstaller(MachineFunction &MF,
                                                 GISelChangeObserver &Observer)
    : MF(MF) {
  MF.setObserver(&Observer);
}

RAIIMFObserverInstaller::~RAIIMFObserverInstaller() { MF.setObserver(nullptr); }
