//===- llvm/CodeGen/GlobalISel/GISelKnownBits.h ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Provides analysis for querying information about KnownBits during GISel
/// passes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GISELKNOWNBITS_H
#define LLVM_CODEGEN_GLOBALISEL_GISELKNOWNBITS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/KnownBits.h"

namespace llvm {

class TargetLowering;
class DataLayout;

class GISelKnownBits : public GISelChangeObserver {
  MachineFunction &MF;
  MachineRegisterInfo &MRI;
  const TargetLowering &TL;
  const DataLayout &DL;
  unsigned MaxDepth;
  /// Cache maintained during a computeKnownBits request.
  SmallDenseMap<Register, KnownBits, 16> ComputeKnownBitsCache;

  void computeKnownBitsMin(Register Src0, Register Src1, KnownBits &Known,
                           const APInt &DemandedElts,
                           unsigned Depth = 0);

  unsigned computeNumSignBitsMin(Register Src0, Register Src1,
                                 const APInt &DemandedElts, unsigned Depth = 0);

public:
  GISelKnownBits(MachineFunction &MF, unsigned MaxDepth = 6);
  virtual ~GISelKnownBits() = default;

  const MachineFunction &getMachineFunction() const {
    return MF;
  }

  const DataLayout &getDataLayout() const {
    return DL;
  }

  virtual void computeKnownBitsImpl(Register R, KnownBits &Known,
                                    const APInt &DemandedElts,
                                    unsigned Depth = 0);

  unsigned computeNumSignBits(Register R, const APInt &DemandedElts,
                              unsigned Depth = 0);
  unsigned computeNumSignBits(Register R, unsigned Depth = 0);

  // KnownBitsAPI
  KnownBits getKnownBits(Register R);
  KnownBits getKnownBits(Register R, const APInt &DemandedElts,
                         unsigned Depth = 0);

  // Calls getKnownBits for first operand def of MI.
  KnownBits getKnownBits(MachineInstr &MI);
  APInt getKnownZeroes(Register R);
  APInt getKnownOnes(Register R);

  /// \return true if 'V & Mask' is known to be zero in DemandedElts. We use
  /// this predicate to simplify operations downstream.
  /// Mask is known to be zero for bits that V cannot have.
  bool maskedValueIsZero(Register Val, const APInt &Mask) {
    return Mask.isSubsetOf(getKnownBits(Val).Zero);
  }

  /// \return true if the sign bit of Op is known to be zero.  We use this
  /// predicate to simplify operations downstream.
  bool signBitIsZero(Register Op);

  static void computeKnownBitsForAlignment(KnownBits &Known,
                                           Align Alignment) {
    // The low bits are known zero if the pointer is aligned.
    Known.Zero.setLowBits(Log2(Alignment));
  }

  /// \return The known alignment for the pointer-like value \p R.
  Align computeKnownAlignment(Register R, unsigned Depth = 0);

  // Observer API. No-op for non-caching implementation.
  void erasingInstr(MachineInstr &MI) override {}
  void createdInstr(MachineInstr &MI) override {}
  void changingInstr(MachineInstr &MI) override {}
  void changedInstr(MachineInstr &MI) override {}

protected:
  unsigned getMaxDepth() const { return MaxDepth; }
};

/// To use KnownBitsInfo analysis in a pass,
/// KnownBitsInfo &Info = getAnalysis<GISelKnownBitsInfoAnalysis>().get(MF);
/// Add to observer if the Info is caching.
/// WrapperObserver.addObserver(Info);

/// Eventually add other features such as caching/ser/deserializing
/// to MIR etc. Those implementations can derive from GISelKnownBits
/// and override computeKnownBitsImpl.
class GISelKnownBitsAnalysis : public MachineFunctionPass {
  std::unique_ptr<GISelKnownBits> Info;

public:
  static char ID;
  GISelKnownBitsAnalysis() : MachineFunctionPass(ID) {
    initializeGISelKnownBitsAnalysisPass(*PassRegistry::getPassRegistry());
  }
  GISelKnownBits &get(MachineFunction &MF);
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnMachineFunction(MachineFunction &MF) override;
  void releaseMemory() override { Info.reset(); }
};
} // namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_GISELKNOWNBITS_H
