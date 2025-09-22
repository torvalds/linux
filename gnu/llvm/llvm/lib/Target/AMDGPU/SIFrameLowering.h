//===--------------------- SIFrameLowering.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIFRAMELOWERING_H
#define LLVM_LIB_TARGET_AMDGPU_SIFRAMELOWERING_H

#include "AMDGPUFrameLowering.h"
#include "SIRegisterInfo.h"

namespace llvm {

class SIFrameLowering final : public AMDGPUFrameLowering {
public:
  SIFrameLowering(StackDirection D, Align StackAl, int LAO,
                  Align TransAl = Align(1))
      : AMDGPUFrameLowering(D, StackAl, LAO, TransAl) {}
  ~SIFrameLowering() override = default;

  void emitEntryFunctionPrologue(MachineFunction &MF,
                                 MachineBasicBlock &MBB) const;
  void emitPrologue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF,
                    MachineBasicBlock &MBB) const override;
  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS = nullptr) const override;
  void determineCalleeSavesSGPR(MachineFunction &MF, BitVector &SavedRegs,
                                RegScavenger *RS = nullptr) const;
  void determinePrologEpilogSGPRSaves(MachineFunction &MF, BitVector &SavedRegs,
                                      bool NeedExecCopyReservedReg) const;
  void emitCSRSpillStores(MachineFunction &MF, MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                          LiveRegUnits &LiveUnits, Register FrameReg,
                          Register FramePtrRegScratchCopy) const;
  void emitCSRSpillRestores(MachineFunction &MF, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, DebugLoc &DL,
                            LiveRegUnits &LiveUnits, Register FrameReg,
                            Register FramePtrRegScratchCopy) const;
  bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const override;

  bool allocateScavengingFrameIndexesNearIncomingSP(
    const MachineFunction &MF) const override;

  bool isSupportedStackID(TargetStackID::Value ID) const override;

  void processFunctionBeforeFrameFinalized(
    MachineFunction &MF,
    RegScavenger *RS = nullptr) const override;

  void processFunctionBeforeFrameIndicesReplaced(
      MachineFunction &MF, RegScavenger *RS = nullptr) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const override;

private:
  void emitEntryFunctionFlatScratchInit(MachineFunction &MF,
                                        MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        const DebugLoc &DL,
                                        Register ScratchWaveOffsetReg) const;

  Register getEntryFunctionReservedScratchRsrcReg(MachineFunction &MF) const;

  void emitEntryFunctionScratchRsrcRegSetup(
      MachineFunction &MF, MachineBasicBlock &MBB,
      MachineBasicBlock::iterator I, const DebugLoc &DL,
      Register PreloadedPrivateBufferReg, Register ScratchRsrcReg,
      Register ScratchWaveOffsetReg) const;

public:
  bool hasFP(const MachineFunction &MF) const override;

  bool requiresStackPointerReference(const MachineFunction &MF) const;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_SIFRAMELOWERING_H
