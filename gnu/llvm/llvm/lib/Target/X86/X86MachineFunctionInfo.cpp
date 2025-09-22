//===-- X86MachineFunctionInfo.cpp - X86 machine function info ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "X86MachineFunctionInfo.h"
#include "X86RegisterInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"

using namespace llvm;

yaml::X86MachineFunctionInfo::X86MachineFunctionInfo(
    const llvm::X86MachineFunctionInfo &MFI)
    : AMXProgModel(MFI.getAMXProgModel()) {}

void yaml::X86MachineFunctionInfo::mappingImpl(yaml::IO &YamlIO) {
  MappingTraits<X86MachineFunctionInfo>::mapping(YamlIO, *this);
}

MachineFunctionInfo *X86MachineFunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  return DestMF.cloneInfo<X86MachineFunctionInfo>(*this);
}

void X86MachineFunctionInfo::initializeBaseYamlFields(
    const yaml::X86MachineFunctionInfo &YamlMFI) {
  AMXProgModel = YamlMFI.AMXProgModel;
}

void X86MachineFunctionInfo::anchor() { }

void X86MachineFunctionInfo::setRestoreBasePointer(const MachineFunction *MF) {
  if (!RestoreBasePointerOffset) {
    const X86RegisterInfo *RegInfo = static_cast<const X86RegisterInfo *>(
      MF->getSubtarget().getRegisterInfo());
    unsigned SlotSize = RegInfo->getSlotSize();
    for (const MCPhysReg *CSR = MF->getRegInfo().getCalleeSavedRegs();
         unsigned Reg = *CSR; ++CSR) {
      if (X86::GR64RegClass.contains(Reg) || X86::GR32RegClass.contains(Reg))
        RestoreBasePointerOffset -= SlotSize;
    }
  }
}

