//===-- PPCRegisterInfo.h - PowerPC Register Information Impl ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the PowerPC implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_POWERPC_PPCREGISTERINFO_H
#define LLVM_LIB_TARGET_POWERPC_PPCREGISTERINFO_H

#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"

#define GET_REGINFO_HEADER
#include "PPCGenRegisterInfo.inc"

namespace llvm {
class PPCTargetMachine;

inline static unsigned getCRFromCRBit(unsigned SrcReg) {
  unsigned Reg = 0;
  if (SrcReg == PPC::CR0LT || SrcReg == PPC::CR0GT ||
      SrcReg == PPC::CR0EQ || SrcReg == PPC::CR0UN)
    Reg = PPC::CR0;
  else if (SrcReg == PPC::CR1LT || SrcReg == PPC::CR1GT ||
           SrcReg == PPC::CR1EQ || SrcReg == PPC::CR1UN)
    Reg = PPC::CR1;
  else if (SrcReg == PPC::CR2LT || SrcReg == PPC::CR2GT ||
           SrcReg == PPC::CR2EQ || SrcReg == PPC::CR2UN)
    Reg = PPC::CR2;
  else if (SrcReg == PPC::CR3LT || SrcReg == PPC::CR3GT ||
           SrcReg == PPC::CR3EQ || SrcReg == PPC::CR3UN)
    Reg = PPC::CR3;
  else if (SrcReg == PPC::CR4LT || SrcReg == PPC::CR4GT ||
           SrcReg == PPC::CR4EQ || SrcReg == PPC::CR4UN)
    Reg = PPC::CR4;
  else if (SrcReg == PPC::CR5LT || SrcReg == PPC::CR5GT ||
           SrcReg == PPC::CR5EQ || SrcReg == PPC::CR5UN)
    Reg = PPC::CR5;
  else if (SrcReg == PPC::CR6LT || SrcReg == PPC::CR6GT ||
           SrcReg == PPC::CR6EQ || SrcReg == PPC::CR6UN)
    Reg = PPC::CR6;
  else if (SrcReg == PPC::CR7LT || SrcReg == PPC::CR7GT ||
           SrcReg == PPC::CR7EQ || SrcReg == PPC::CR7UN)
    Reg = PPC::CR7;

  assert(Reg != 0 && "Invalid CR bit register");
  return Reg;
}

class PPCRegisterInfo : public PPCGenRegisterInfo {
  DenseMap<unsigned, unsigned> ImmToIdxMap;
  const PPCTargetMachine &TM;

public:
  PPCRegisterInfo(const PPCTargetMachine &TM);

  /// getMappedIdxOpcForImmOpc - Return the mapped index form load/store opcode
  /// for a given imm form load/store opcode \p ImmFormOpcode.
  /// FIXME: move this to PPCInstrInfo class.
  unsigned getMappedIdxOpcForImmOpc(unsigned ImmOpcode) const {
    if (!ImmToIdxMap.count(ImmOpcode))
      return PPC::INSTRUCTION_LIST_END;
    return ImmToIdxMap.find(ImmOpcode)->second;
  }

  /// getPointerRegClass - Return the register class to use to hold pointers.
  /// This is used for addressing modes.
  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF, unsigned Kind=0) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const override;

  /// Code Generation virtual methods...
  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;
  const uint32_t *getNoPreservedMask() const override;

  void adjustStackMapLiveOutMask(uint32_t *Mask) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool isAsmClobberable(const MachineFunction &MF,
                        MCRegister PhysReg) const override;
  bool isCallerPreservedPhysReg(MCRegister PhysReg,
                                const MachineFunction &MF) const override;

  // Provide hints to the register allocator for allocating subregisters
  // of primed and unprimed accumulators. For example, if accumulator
  // ACC5 is assigned, we also want to assign UACC5 to the input.
  // Similarly if UACC5 is assigned, we want to assign VSRp10, VSRp11
  // to its inputs.
  bool getRegAllocationHints(Register VirtReg, ArrayRef<MCPhysReg> Order,
                             SmallVectorImpl<MCPhysReg> &Hints,
                             const MachineFunction &MF, const VirtRegMap *VRM,
                             const LiveRegMatrix *Matrix) const override;

  /// We require the register scavenger.
  bool requiresRegisterScavenging(const MachineFunction &MF) const override {
    return true;
  }

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override;

  bool requiresVirtualBaseRegisters(const MachineFunction &MF) const override;

  void lowerDynamicAlloc(MachineBasicBlock::iterator II) const;
  void lowerDynamicAreaOffset(MachineBasicBlock::iterator II) const;
  void prepareDynamicAlloca(MachineBasicBlock::iterator II,
                            Register &NegSizeReg, bool &KillNegSizeReg,
                            Register &FramePointer) const;
  void lowerPrepareProbedAlloca(MachineBasicBlock::iterator II) const;
  void lowerCRSpilling(MachineBasicBlock::iterator II,
                       unsigned FrameIndex) const;
  void lowerCRRestore(MachineBasicBlock::iterator II,
                      unsigned FrameIndex) const;
  void lowerCRBitSpilling(MachineBasicBlock::iterator II,
                          unsigned FrameIndex) const;
  void lowerCRBitRestore(MachineBasicBlock::iterator II,
                         unsigned FrameIndex) const;

  void lowerOctWordSpilling(MachineBasicBlock::iterator II,
                            unsigned FrameIndex) const;
  void lowerACCSpilling(MachineBasicBlock::iterator II,
                        unsigned FrameIndex) const;
  void lowerACCRestore(MachineBasicBlock::iterator II,
                       unsigned FrameIndex) const;

  void lowerWACCSpilling(MachineBasicBlock::iterator II,
                         unsigned FrameIndex) const;
  void lowerWACCRestore(MachineBasicBlock::iterator II,
                        unsigned FrameIndex) const;

  void lowerQuadwordSpilling(MachineBasicBlock::iterator II,
                             unsigned FrameIndex) const;
  void lowerQuadwordRestore(MachineBasicBlock::iterator II,
                            unsigned FrameIndex) const;

  static void emitAccCopyInfo(MachineBasicBlock &MBB, MCRegister DestReg,
                              MCRegister SrcReg);

  bool hasReservedSpillSlot(const MachineFunction &MF, Register Reg,
                            int &FrameIdx) const override;
  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  // Support for virtual base registers.
  bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const override;
  Register materializeFrameBaseRegister(MachineBasicBlock *MBB, int FrameIdx,
                                        int64_t Offset) const override;
  void resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                         int64_t Offset) const override;
  bool isFrameOffsetLegal(const MachineInstr *MI, Register BaseReg,
                          int64_t Offset) const override;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;

  // Base pointer (stack realignment) support.
  Register getBaseRegister(const MachineFunction &MF) const;
  bool hasBasePointer(const MachineFunction &MF) const;

  bool isNonallocatableRegisterCalleeSave(MCRegister Reg) const override {
    return Reg == PPC::LR || Reg == PPC::LR8;
  }
};

} // end namespace llvm

#endif
