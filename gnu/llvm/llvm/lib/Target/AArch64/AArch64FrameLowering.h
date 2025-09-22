//==-- AArch64FrameLowering.h - TargetFrameLowering for AArch64 --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64FRAMELOWERING_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64FRAMELOWERING_H

#include "AArch64ReturnProtectorLowering.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/TypeSize.h"

namespace llvm {

class AArch64FrameLowering : public TargetFrameLowering {
public:

  const AArch64ReturnProtectorLowering RPL;

  explicit AArch64FrameLowering()
      : TargetFrameLowering(StackGrowsDown, Align(16), 0, Align(16),
                            true /*StackRealignable*/), RPL() {}

  void resetCFIToInitialState(MachineBasicBlock &MBB) const override;

  MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I) const override;

  /// emitProlog/emitEpilog - These methods insert prolog and epilog code into
  /// the function.
  void emitPrologue(MachineFunction &MF, MachineBasicBlock &MBB) const override;
  void emitEpilogue(MachineFunction &MF, MachineBasicBlock &MBB) const override;

  const ReturnProtectorLowering *getReturnProtector() const override;

  bool enableCFIFixup(MachineFunction &MF) const override;

  bool canUseAsPrologue(const MachineBasicBlock &MBB) const override;

  StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                     Register &FrameReg) const override;
  StackOffset getFrameIndexReferenceFromSP(const MachineFunction &MF,
                                           int FI) const override;
  StackOffset resolveFrameIndexReference(const MachineFunction &MF, int FI,
                                         Register &FrameReg, bool PreferFP,
                                         bool ForSimm) const;
  StackOffset resolveFrameOffsetReference(const MachineFunction &MF,
                                          int64_t ObjectOffset, bool isFixed,
                                          bool isSVE, Register &FrameReg,
                                          bool PreferFP, bool ForSimm) const;
  bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 ArrayRef<CalleeSavedInfo> CSI,
                                 const TargetRegisterInfo *TRI) const override;

  bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const override;

  /// Can this function use the red zone for local allocations.
  bool canUseRedZone(const MachineFunction &MF) const;

  bool hasFP(const MachineFunction &MF) const override;
  bool hasReservedCallFrame(const MachineFunction &MF) const override;

  bool assignCalleeSavedSpillSlots(MachineFunction &MF,
                                   const TargetRegisterInfo *TRI,
                                   std::vector<CalleeSavedInfo> &CSI,
                                   unsigned &MinCSFrameIndex,
                                   unsigned &MaxCSFrameIndex) const override;

  void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                            RegScavenger *RS) const override;

  /// Returns true if the target will correctly handle shrink wrapping.
  bool enableShrinkWrapping(const MachineFunction &MF) const override {
    return true;
  }

  bool enableStackSlotScavenging(const MachineFunction &MF) const override;
  TargetStackID::Value getStackIDForScalableVectors() const override;

  void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                           RegScavenger *RS) const override;

  void
  processFunctionBeforeFrameIndicesReplaced(MachineFunction &MF,
                                            RegScavenger *RS) const override;

  unsigned getWinEHParentFrameOffset(const MachineFunction &MF) const override;

  unsigned getWinEHFuncletFrameSize(const MachineFunction &MF) const;

  StackOffset
  getFrameIndexReferencePreferSP(const MachineFunction &MF, int FI,
                                 Register &FrameReg,
                                 bool IgnoreSPUpdates) const override;
  StackOffset getNonLocalFrameIndexReference(const MachineFunction &MF,
                                             int FI) const override;
  int getSEHFrameIndexOffset(const MachineFunction &MF, int FI) const;

  bool isSupportedStackID(TargetStackID::Value ID) const override {
    switch (ID) {
    default:
      return false;
    case TargetStackID::Default:
    case TargetStackID::ScalableVector:
    case TargetStackID::NoAlloc:
      return true;
    }
  }

  bool isStackIdSafeForLocalArea(unsigned StackId) const override {
    // We don't support putting SVE objects into the pre-allocated local
    // frame block at the moment.
    return StackId != TargetStackID::ScalableVector;
  }

  void
  orderFrameObjects(const MachineFunction &MF,
                    SmallVectorImpl<int> &ObjectsToAllocate) const override;

private:
  /// Returns true if a homogeneous prolog or epilog code can be emitted
  /// for the size optimization. If so, HOM_Prolog/HOM_Epilog pseudo
  /// instructions are emitted in place. When Exit block is given, this check is
  /// for epilog.
  bool homogeneousPrologEpilog(MachineFunction &MF,
                               MachineBasicBlock *Exit = nullptr) const;

  /// Returns true if CSRs should be paired.
  bool producePairRegisters(MachineFunction &MF) const;

  bool shouldCombineCSRLocalStackBump(MachineFunction &MF,
                                      uint64_t StackBumpBytes) const;

  int64_t estimateSVEStackObjectOffsets(MachineFrameInfo &MF) const;
  int64_t assignSVEStackObjectOffsets(MachineFrameInfo &MF,
                                      int &MinCSFrameIndex,
                                      int &MaxCSFrameIndex) const;
  bool shouldCombineCSRLocalStackBumpInEpilogue(MachineBasicBlock &MBB,
                                                unsigned StackBumpBytes) const;
  void emitCalleeSavedGPRLocations(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI) const;
  void emitCalleeSavedSVELocations(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI) const;
  void emitCalleeSavedGPRRestores(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI) const;
  void emitCalleeSavedSVERestores(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MBBI) const;
  void allocateStackSpace(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI,
                          int64_t RealignmentPadding, StackOffset AllocSize,
                          bool NeedsWinCFI, bool *HasWinCFI, bool EmitCFI,
                          StackOffset InitialOffset, bool FollowupAllocs) const;
  /// Make a determination whether a Hazard slot is used and create it if
  /// needed.
  void determineStackHazardSlot(MachineFunction &MF,
                                BitVector &SavedRegs) const;

  /// Emit target zero call-used regs.
  void emitZeroCallUsedRegs(BitVector RegsToZero,
                            MachineBasicBlock &MBB) const override;

  /// Replace a StackProbe stub (if any) with the actual probe code inline
  void inlineStackProbe(MachineFunction &MF,
                        MachineBasicBlock &PrologueMBB) const override;

  void inlineStackProbeFixed(MachineBasicBlock::iterator MBBI,
                             Register ScratchReg, int64_t FrameSize,
                             StackOffset CFAOffset) const;

  MachineBasicBlock::iterator
  inlineStackProbeLoopExactMultiple(MachineBasicBlock::iterator MBBI,
                                    int64_t NegProbeSize,
                                    Register TargetReg) const;

  void emitRemarks(const MachineFunction &MF,
                   MachineOptimizationRemarkEmitter *ORE) const override;
};

} // End llvm namespace

#endif
