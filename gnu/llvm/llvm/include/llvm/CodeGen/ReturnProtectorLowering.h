//===-- llvm/CodeGen/ReturnProtectorLowering.h ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// A class to insert and lower the return protector instrumentation
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_RETURNPROTECTORLOWERING_H
#define LLVM_CODEGEN_RETURNPROTECTORLOWERING_H

#include "llvm/ADT/SmallVector.h"

#include <utility>
#include <vector>

namespace llvm {
class CalleeSavedInfo;
class GlobalVariable;
class MachineBasicBlock;
class MachineFunction;
class MachineInstr;

class ReturnProtectorLowering {
public:
  virtual ~ReturnProtectorLowering() {}
  /// Subclass interface - subclasses need to implement these functions.

  /// insertReturnProtectorPrologue/Epilogue - insert return protector
  /// instrumentation in prologue or epilogue.
  virtual void insertReturnProtectorPrologue(MachineFunction &MF,
                                             MachineBasicBlock &MBB,
                                             GlobalVariable *cookie) const {}
  virtual void insertReturnProtectorEpilogue(MachineFunction &MF,
                                             MachineInstr &MI,
                                             GlobalVariable *cookie) const {}

  /// opcodeIsReturn - Reuturn true is the given opcode is a return
  /// instruction needing return protection, false otherwise.
  virtual bool opcodeIsReturn(unsigned opcode) const { return false; }

  /// fillTempRegisters - Fill the list of available temp registers we can
  /// use as a CalculationRegister.
  virtual void fillTempRegisters(MachineFunction &MF,
                                 std::vector<unsigned> &TempRegs) const {}

  /// Generic public interface used by llvm

  /// setupReturnProtector - Checks the function for ROP friendly return
  /// instructions and sets ReturnProtectorNeeded in the frame if found.
  virtual void setupReturnProtector(MachineFunction &MF) const;

  /// saveReturnProtectorRegister - Allows the target to save the
  /// CalculationRegister in the CalleeSavedInfo vector if needed.
  virtual void
  saveReturnProtectorRegister(MachineFunction &MF,
                              std::vector<CalleeSavedInfo> &CSI) const;

  /// determineReturnProtectorTempRegister - Find a register that can be used
  /// during function prologue / epilogue to store the return protector cookie.
  /// Returns false if a register is needed but could not be found,
  /// otherwise returns true.
  virtual bool determineReturnProtectorRegister(
      MachineFunction &MF,
      const SmallVector<MachineBasicBlock *, 4> &SaveBlocks,
      const SmallVector<MachineBasicBlock *, 4> &RestoreBlocks) const;

  /// insertReturnProtectors - insert return protector instrumentation.
  virtual void insertReturnProtectors(MachineFunction &MF) const;
};

} // namespace llvm

#endif
