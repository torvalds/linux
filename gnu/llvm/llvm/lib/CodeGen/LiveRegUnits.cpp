//===- LiveRegUnits.cpp - Register Unit Set -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file imlements the LiveRegUnits set.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

void LiveRegUnits::removeRegsNotPreserved(const uint32_t *RegMask) {
  for (unsigned U = 0, E = TRI->getNumRegUnits(); U != E; ++U) {
    for (MCRegUnitRootIterator RootReg(U, TRI); RootReg.isValid(); ++RootReg) {
      if (MachineOperand::clobbersPhysReg(RegMask, *RootReg)) {
        Units.reset(U);
        break;
      }
    }
  }
}

void LiveRegUnits::addRegsInMask(const uint32_t *RegMask) {
  for (unsigned U = 0, E = TRI->getNumRegUnits(); U != E; ++U) {
    for (MCRegUnitRootIterator RootReg(U, TRI); RootReg.isValid(); ++RootReg) {
      if (MachineOperand::clobbersPhysReg(RegMask, *RootReg)) {
        Units.set(U);
        break;
      }
    }
  }
}

void LiveRegUnits::stepBackward(const MachineInstr &MI) {
  // Remove defined registers and regmask kills from the set.
  for (const MachineOperand &MOP : MI.operands()) {
    if (MOP.isReg()) {
      if (MOP.isDef() && MOP.getReg().isPhysical())
        removeReg(MOP.getReg());
      continue;
    }

    if (MOP.isRegMask()) {
      removeRegsNotPreserved(MOP.getRegMask());
      continue;
    }
  }

  // Add uses to the set.
  for (const MachineOperand &MOP : MI.operands()) {
    if (!MOP.isReg() || !MOP.readsReg())
      continue;

    if (MOP.getReg().isPhysical())
      addReg(MOP.getReg());
  }
}

void LiveRegUnits::accumulate(const MachineInstr &MI) {
  // Add defs, uses and regmask clobbers to the set.
  for (const MachineOperand &MOP : MI.operands()) {
    if (MOP.isReg()) {
      if (!MOP.getReg().isPhysical())
        continue;
      if (MOP.isDef() || MOP.readsReg())
        addReg(MOP.getReg());
      continue;
    }

    if (MOP.isRegMask()) {
      addRegsInMask(MOP.getRegMask());
      continue;
    }
  }
}

/// Add live-in registers of basic block \p MBB to \p LiveUnits.
static void addBlockLiveIns(LiveRegUnits &LiveUnits,
                            const MachineBasicBlock &MBB) {
  for (const auto &LI : MBB.liveins())
    LiveUnits.addRegMasked(LI.PhysReg, LI.LaneMask);
}

/// Adds all callee saved registers to \p LiveUnits.
static void addCalleeSavedRegs(LiveRegUnits &LiveUnits,
                               const MachineFunction &MF) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  for (const MCPhysReg *CSR = MRI.getCalleeSavedRegs(); CSR && *CSR; ++CSR) {
    const unsigned N = *CSR;

    const auto &CSI = MFI.getCalleeSavedInfo();
    auto Info =
        llvm::find_if(CSI, [N](auto Info) { return Info.getReg() == N; });
    // If we have no info for this callee-saved register, assume it is liveout
    if (Info == CSI.end() || Info->isRestored())
      LiveUnits.addReg(N);
  }
}

void LiveRegUnits::addPristines(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.isCalleeSavedInfoValid())
    return;
  /// This function will usually be called on an empty object, handle this
  /// as a special case.
  if (empty()) {
    /// Add all callee saved regs, then remove the ones that are saved and
    /// restored.
    addCalleeSavedRegs(*this, MF);
    /// Remove the ones that are not saved/restored; they are pristine.
    for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
      removeReg(Info.getReg());
    return;
  }
  /// If a callee-saved register that is not pristine is already present
  /// in the set, we should make sure that it stays in it. Precompute the
  /// set of pristine registers in a separate object.
  /// Add all callee saved regs, then remove the ones that are saved+restored.
  LiveRegUnits Pristine(*TRI);
  addCalleeSavedRegs(Pristine, MF);
  /// Remove the ones that are not saved/restored; they are pristine.
  for (const CalleeSavedInfo &Info : MFI.getCalleeSavedInfo())
    Pristine.removeReg(Info.getReg());
  addUnits(Pristine.getBitVector());
}

void LiveRegUnits::addLiveOuts(const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();

  addPristines(MF);

  // To get the live-outs we simply merge the live-ins of all successors.
  for (const MachineBasicBlock *Succ : MBB.successors())
    addBlockLiveIns(*this, *Succ);

  // For the return block: Add all callee saved registers.
  if (MBB.isReturnBlock()) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    if (MFI.isCalleeSavedInfoValid())
      addCalleeSavedRegs(*this, MF);
  }
}

void LiveRegUnits::addLiveIns(const MachineBasicBlock &MBB) {
  const MachineFunction &MF = *MBB.getParent();
  addPristines(MF);
  addBlockLiveIns(*this, MBB);
}
