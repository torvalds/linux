//===-- SILowerI1Copies.h --------------------------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Interface definition of the PhiLoweringHelper class that implements lane
/// mask merging algorithm for divergent i1 phis.
//
//===----------------------------------------------------------------------===//

#include "GCNSubtarget.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"

namespace llvm {

/// Incoming for lane maks phi as machine instruction, incoming register \p Reg
/// and incoming block \p Block are taken from machine instruction.
/// \p UpdatedReg (if valid) is \p Reg lane mask merged with another lane mask.
struct Incoming {
  Register Reg;
  MachineBasicBlock *Block;
  Register UpdatedReg;

  Incoming(Register Reg, MachineBasicBlock *Block, Register UpdatedReg)
      : Reg(Reg), Block(Block), UpdatedReg(UpdatedReg) {}
};

Register createLaneMaskReg(MachineRegisterInfo *MRI,
                           MachineRegisterInfo::VRegAttrs LaneMaskRegAttrs);

class PhiLoweringHelper {
public:
  PhiLoweringHelper(MachineFunction *MF, MachineDominatorTree *DT,
                    MachinePostDominatorTree *PDT);
  virtual ~PhiLoweringHelper() = default;

protected:
  bool IsWave32 = false;
  MachineFunction *MF = nullptr;
  MachineDominatorTree *DT = nullptr;
  MachinePostDominatorTree *PDT = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  const GCNSubtarget *ST = nullptr;
  const SIInstrInfo *TII = nullptr;
  MachineRegisterInfo::VRegAttrs LaneMaskRegAttrs;

#ifndef NDEBUG
  DenseSet<Register> PhiRegisters;
#endif

  Register ExecReg;
  unsigned MovOp;
  unsigned AndOp;
  unsigned OrOp;
  unsigned XorOp;
  unsigned AndN2Op;
  unsigned OrN2Op;

public:
  bool lowerPhis();
  bool isConstantLaneMask(Register Reg, bool &Val) const;
  MachineBasicBlock::iterator
  getSaluInsertionAtEnd(MachineBasicBlock &MBB) const;

  void initializeLaneMaskRegisterAttributes(Register LaneMask) {
    LaneMaskRegAttrs = MRI->getVRegAttrs(LaneMask);
  }

  bool isLaneMaskReg(Register Reg) const {
    return TII->getRegisterInfo().isSGPRReg(*MRI, Reg) &&
           TII->getRegisterInfo().getRegSizeInBits(Reg, *MRI) ==
               ST->getWavefrontSize();
  }

  // Helpers from lowerPhis that are different between sdag and global-isel.

  virtual void markAsLaneMask(Register DstReg) const = 0;
  virtual void getCandidatesForLowering(
      SmallVectorImpl<MachineInstr *> &Vreg1Phis) const = 0;
  virtual void
  collectIncomingValuesFromPhi(const MachineInstr *MI,
                               SmallVectorImpl<Incoming> &Incomings) const = 0;
  virtual void replaceDstReg(Register NewReg, Register OldReg,
                             MachineBasicBlock *MBB) = 0;
  virtual void buildMergeLaneMasks(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, Register DstReg,
                                   Register PrevReg, Register CurReg) = 0;
  virtual void constrainAsLaneMask(Incoming &In) = 0;
};

} // end namespace llvm
