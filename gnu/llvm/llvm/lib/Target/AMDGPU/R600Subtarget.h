//=====-- R600Subtarget.h - Define Subtarget for AMDGPU R600 ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//==-----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU R600 specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600SUBTARGET_H
#define LLVM_LIB_TARGET_AMDGPU_R600SUBTARGET_H

#include "AMDGPUSubtarget.h"
#include "R600FrameLowering.h"
#include "R600ISelLowering.h"
#include "R600InstrInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SUBTARGETINFO_HEADER
#include "R600GenSubtargetInfo.inc"

namespace llvm {

class R600Subtarget final : public R600GenSubtargetInfo,
                            public AMDGPUSubtarget {
private:
  R600InstrInfo InstrInfo;
  R600FrameLowering FrameLowering;
  bool FMA = false;
  bool CaymanISA = false;
  bool CFALUBug = false;
  bool HasVertexCache = false;
  bool R600ALUInst = false;
  bool FP64 = false;
  short TexVTXClauseSize = 0;
  Generation Gen = R600;
  R600TargetLowering TLInfo;
  InstrItineraryData InstrItins;
  SelectionDAGTargetInfo TSInfo;

public:
  R600Subtarget(const Triple &TT, StringRef CPU, StringRef FS,
                const TargetMachine &TM);

  const R600InstrInfo *getInstrInfo() const override { return &InstrInfo; }

  const R600FrameLowering *getFrameLowering() const override {
    return &FrameLowering;
  }

  const R600TargetLowering *getTargetLowering() const override {
    return &TLInfo;
  }

  const R600RegisterInfo *getRegisterInfo() const override {
    return &InstrInfo.getRegisterInfo();
  }

  const InstrItineraryData *getInstrItineraryData() const override {
    return &InstrItins;
  }

  // Nothing implemented, just prevent crashes on use.
  const SelectionDAGTargetInfo *getSelectionDAGInfo() const override {
    return &TSInfo;
  }

  void ParseSubtargetFeatures(StringRef CPU, StringRef TuneCPU, StringRef FS);

  Generation getGeneration() const {
    return Gen;
  }

  Align getStackAlignment() const { return Align(4); }

  R600Subtarget &initializeSubtargetDependencies(const Triple &TT,
                                                 StringRef GPU, StringRef FS);

  bool hasBFE() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasBFI() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasBCNT(unsigned Size) const {
    if (Size == 32)
      return (getGeneration() >= EVERGREEN);

    return false;
  }

  bool hasBORROW() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasCARRY() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasCaymanISA() const {
    return CaymanISA;
  }

  bool hasFFBL() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasFFBH() const {
    return (getGeneration() >= EVERGREEN);
  }

  bool hasFMA() const { return FMA; }

  bool hasCFAluBug() const { return CFALUBug; }

  bool hasVertexCache() const { return HasVertexCache; }

  short getTexVTXClauseSize() const { return TexVTXClauseSize; }

  bool enableMachineScheduler() const override {
    return true;
  }

  bool enableSubRegLiveness() const override {
    return true;
  }

  /// \returns Maximum number of work groups per compute unit supported by the
  /// subtarget and limited by given \p FlatWorkGroupSize.
  unsigned getMaxWorkGroupsPerCU(unsigned FlatWorkGroupSize) const override {
    return AMDGPU::IsaInfo::getMaxWorkGroupsPerCU(this, FlatWorkGroupSize);
  }

  /// \returns Minimum flat work group size supported by the subtarget.
  unsigned getMinFlatWorkGroupSize() const override {
    return AMDGPU::IsaInfo::getMinFlatWorkGroupSize(this);
  }

  /// \returns Maximum flat work group size supported by the subtarget.
  unsigned getMaxFlatWorkGroupSize() const override {
    return AMDGPU::IsaInfo::getMaxFlatWorkGroupSize(this);
  }

  /// \returns Number of waves per execution unit required to support the given
  /// \p FlatWorkGroupSize.
  unsigned
  getWavesPerEUForWorkGroup(unsigned FlatWorkGroupSize) const override {
    return AMDGPU::IsaInfo::getWavesPerEUForWorkGroup(this, FlatWorkGroupSize);
  }

  /// \returns Minimum number of waves per execution unit supported by the
  /// subtarget.
  unsigned getMinWavesPerEU() const override {
    return AMDGPU::IsaInfo::getMinWavesPerEU(this);
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_R600SUBTARGET_H
