//===-- X86RegisterInfo.h - X86 Register Information Impl -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86REGISTERINFO_H
#define LLVM_LIB_TARGET_X86_X86REGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "X86GenRegisterInfo.inc"

namespace llvm {
  class Triple;

class X86RegisterInfo final : public X86GenRegisterInfo {
private:
  /// Is64Bit - Is the target 64-bits.
  ///
  bool Is64Bit;

  /// IsWin64 - Is the target on of win64 flavours
  ///
  bool IsWin64;

  /// SlotSize - Stack slot size in bytes.
  ///
  unsigned SlotSize;

  /// StackPtr - X86 physical register used as stack ptr.
  ///
  unsigned StackPtr;

  /// FramePtr - X86 physical register used as frame ptr.
  ///
  unsigned FramePtr;

  /// BasePtr - X86 physical register used as a base ptr in complex stack
  /// frames. I.e., when we need a 3rd base, not just SP and FP, due to
  /// variable size stack objects.
  unsigned BasePtr;

public:
  explicit X86RegisterInfo(const Triple &TT);

  /// Return the number of registers for the function.
  unsigned getNumSupportedRegs(const MachineFunction &MF) const override;

  // FIXME: This should be tablegen'd like getDwarfRegNum is
  int getSEHRegNum(unsigned i) const;

  /// getMatchingSuperRegClass - Return a subclass of the specified register
  /// class A so that each register in it has a sub-register of the
  /// specified sub-register index which is in the specified register class B.
  const TargetRegisterClass *
  getMatchingSuperRegClass(const TargetRegisterClass *A,
                           const TargetRegisterClass *B,
                           unsigned Idx) const override;

  const TargetRegisterClass *
  getSubClassWithSubReg(const TargetRegisterClass *RC,
                        unsigned Idx) const override;

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const override;

  bool shouldRewriteCopySrc(const TargetRegisterClass *DefRC,
                            unsigned DefSubReg,
                            const TargetRegisterClass *SrcRC,
                            unsigned SrcSubReg) const override;

  /// getPointerRegClass - Returns a TargetRegisterClass used for pointer
  /// values.
  const TargetRegisterClass *
  getPointerRegClass(const MachineFunction &MF,
                     unsigned Kind = 0) const override;

  /// getCrossCopyRegClass - Returns a legal register class to copy a register
  /// in the specified class to or from. Returns NULL if it is possible to copy
  /// between a two registers of the specified class.
  const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const override;

  /// getGPRsForTailCall - Returns a register class with registers that can be
  /// used in forming tail calls.
  const TargetRegisterClass *
  getGPRsForTailCall(const MachineFunction &MF) const;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  /// getCalleeSavedRegs - Return a null-terminated list of all of the
  /// callee-save registers on this target.
  const MCPhysReg *
  getCalleeSavedRegs(const MachineFunction* MF) const override;
  const MCPhysReg *
  getCalleeSavedRegsViaCopy(const MachineFunction *MF) const;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  const uint32_t *getNoPreservedMask() const override;

  // Calls involved in thread-local variable lookup save more registers than
  // normal calls, so they need a different mask to represent this.
  const uint32_t *getDarwinTLSCallPreservedMask() const;

  /// getReservedRegs - Returns a bitset indexed by physical register number
  /// indicating if a register is a special register that has particular uses and
  /// should be considered unavailable at all times, e.g. SP, RA. This is used by
  /// register scavenger to determine what registers are free.
  BitVector getReservedRegs(const MachineFunction &MF) const override;

  /// isArgumentReg - Returns true if Reg can be used as an argument to a
  /// function.
  bool isArgumentRegister(const MachineFunction &MF,
                          MCRegister Reg) const override;

  /// Return true if it is tile register class.
  bool isTileRegisterClass(const TargetRegisterClass *RC) const;

  /// Returns true if PhysReg is a fixed register.
  bool isFixedRegister(const MachineFunction &MF,
                       MCRegister PhysReg) const override;

  void adjustStackMapLiveOutMask(uint32_t *Mask) const override;

  bool hasBasePointer(const MachineFunction &MF) const;

  bool canRealignStack(const MachineFunction &MF) const override;

  bool shouldRealignStack(const MachineFunction &MF) const override;

  void eliminateFrameIndex(MachineBasicBlock::iterator II,
                           unsigned FIOperandNum, Register BaseReg,
                           int FIOffset) const;

  bool eliminateFrameIndex(MachineBasicBlock::iterator MI,
                           int SPAdj, unsigned FIOperandNum,
                           RegScavenger *RS = nullptr) const override;

  /// Process frame indices in forwards block order because
  /// X86InstrInfo::getSPAdjust relies on it when searching for the
  /// ADJCALLSTACKUP pseudo following a call.
  /// TODO: Fix this and return true like all other targets.
  bool eliminateFrameIndicesBackwards() const override { return false; }

  /// findDeadCallerSavedReg - Return a caller-saved register that isn't live
  /// when it reaches the "return" instruction. We can then pop a stack object
  /// to this register without worry about clobbering it.
  unsigned findDeadCallerSavedReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator &MBBI) const;

  // Debug information queries.
  Register getFrameRegister(const MachineFunction &MF) const override;
  unsigned getPtrSizedFrameRegister(const MachineFunction &MF) const;
  unsigned getPtrSizedStackRegister(const MachineFunction &MF) const;
  Register getStackRegister() const { return StackPtr; }
  Register getBaseRegister() const { return BasePtr; }
  /// Returns physical register used as frame pointer.
  /// This will always returns the frame pointer register, contrary to
  /// getFrameRegister() which returns the "base pointer" in situations
  /// involving a stack, frame and base pointer.
  Register getFramePtr() const { return FramePtr; }
  // FIXME: Move to FrameInfok
  unsigned getSlotSize() const { return SlotSize; }

  bool getRegAllocationHints(Register VirtReg, ArrayRef<MCPhysReg> Order,
                             SmallVectorImpl<MCPhysReg> &Hints,
                             const MachineFunction &MF, const VirtRegMap *VRM,
                             const LiveRegMatrix *Matrix) const override;
};

} // End llvm namespace

#endif
