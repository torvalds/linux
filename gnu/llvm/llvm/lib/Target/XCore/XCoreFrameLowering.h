//===-- XCoreFrameLowering.h - Frame info for XCore Target ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains XCore frame information that doesn't fit anywhere else
// cleanly...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCOREFRAMELOWERING_H
#define LLVM_LIB_TARGET_XCORE_XCOREFRAMELOWERING_H

#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
  class XCoreSubtarget;

  class XCoreFrameLowering: public TargetFrameLowering {
  public:
    XCoreFrameLowering(const XCoreSubtarget &STI);

    /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
    /// the function.
    void emitPrologue(MachineFunction &MF,
                      MachineBasicBlock &MBB) const override;
    void emitEpilogue(MachineFunction &MF,
                      MachineBasicBlock &MBB) const override;

    bool
    spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              ArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;
    bool
    restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI,
                                MutableArrayRef<CalleeSavedInfo> CSI,
                                const TargetRegisterInfo *TRI) const override;

    MachineBasicBlock::iterator
    eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator I) const override;

    bool hasFP(const MachineFunction &MF) const override;

    void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                              RegScavenger *RS = nullptr) const override;

    void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                     RegScavenger *RS = nullptr) const override;

    //! Stack slot size (4 bytes)
    static int stackSlotSize() {
      return 4;
    }
  };
}

#endif
