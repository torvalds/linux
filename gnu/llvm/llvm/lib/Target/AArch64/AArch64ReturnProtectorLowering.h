//===-- AArch64ReturnProtectorLowering.h - --------------------- -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 implementation of ReturnProtectorLowering
// class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64RETURNPROTECTORLOWERING_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64RETURNPROTECTORLOWERING_H

#include "llvm/CodeGen/ReturnProtectorLowering.h"

namespace llvm {

class AArch64ReturnProtectorLowering : public ReturnProtectorLowering {
public:
  /// insertReturnProtectorPrologue/Epilogue - insert return protector
  /// instrumentation in prologue or epilogue.
  virtual void
  insertReturnProtectorPrologue(MachineFunction &MF, MachineBasicBlock &MBB,
                                GlobalVariable *cookie) const override;
  virtual void
  insertReturnProtectorEpilogue(MachineFunction &MF, MachineInstr &MI,
                                GlobalVariable *cookie) const override;

  /// opcodeIsReturn - Reuturn true is the given opcode is a return
  /// instruction needing return protection, false otherwise.
  virtual bool opcodeIsReturn(unsigned opcode) const override;

  /// fillTempRegisters - Fill the list of available temp registers we can
  /// use as a return protector register.
  virtual void
  fillTempRegisters(MachineFunction &MF,
                    std::vector<unsigned> &TempRegs) const override;

  /// saveReturnProtectorRegister - Allows the target to save the
  /// CalculationRegister in the CalleeSavedInfo vector if needed.
  virtual void
  saveReturnProtectorRegister(MachineFunction &MF,
                              std::vector<CalleeSavedInfo> &CSI) const override;
};

} // namespace llvm

#endif
