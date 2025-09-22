//===- RegisterScavenging.h - Machine register scavenging -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file declares the machine register scavenger class. It can provide
/// information such as unused register at any point in a machine basic block.
/// It also provides a mechanism to make registers available by evicting them
/// to spill slots.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERSCAVENGING_H
#define LLVM_CODEGEN_REGISTERSCAVENGING_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/LaneBitmask.h"

namespace llvm {

class MachineInstr;
class TargetInstrInfo;
class TargetRegisterClass;
class TargetRegisterInfo;

class RegScavenger {
  const TargetRegisterInfo *TRI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  MachineBasicBlock *MBB = nullptr;
  MachineBasicBlock::iterator MBBI;

  /// Information on scavenged registers (held in a spill slot).
  struct ScavengedInfo {
    ScavengedInfo(int FI = -1) : FrameIndex(FI) {}

    /// A spill slot used for scavenging a register post register allocation.
    int FrameIndex;

    /// If non-zero, the specific register is currently being
    /// scavenged. That is, it is spilled to this scavenging stack slot.
    Register Reg;

    /// The instruction that restores the scavenged register from stack.
    const MachineInstr *Restore = nullptr;
  };

  /// A vector of information on scavenged registers.
  SmallVector<ScavengedInfo, 2> Scavenged;

  LiveRegUnits LiveUnits;

public:
  RegScavenger() = default;

  /// Record that \p Reg is in use at scavenging index \p FI. This is for
  /// targets which need to directly manage the spilling process, and need to
  /// update the scavenger's internal state.  It's expected this be called a
  /// second time with \p Restore set to a non-null value, so that the
  /// externally inserted restore instruction resets the scavenged slot
  /// liveness when encountered.
  void assignRegToScavengingIndex(int FI, Register Reg,
                                  MachineInstr *Restore = nullptr) {
    for (ScavengedInfo &Slot : Scavenged) {
      if (Slot.FrameIndex == FI) {
        assert(!Slot.Reg || Slot.Reg == Reg);
        Slot.Reg = Reg;
        Slot.Restore = Restore;
        return;
      }
    }

    llvm_unreachable("did not find scavenging index");
  }

  /// Start tracking liveness from the begin of basic block \p MBB.
  void enterBasicBlock(MachineBasicBlock &MBB);

  /// Start tracking liveness from the end of basic block \p MBB.
  /// Use backward() to move towards the beginning of the block.
  void enterBasicBlockEnd(MachineBasicBlock &MBB);

  /// Update internal register state and move MBB iterator backwards. This
  /// method gives precise results even in the absence of kill flags.
  void backward();

  /// Call backward() to update internal register state to just before \p *I.
  void backward(MachineBasicBlock::iterator I) {
    while (MBBI != I)
      backward();
  }

  /// Return if a specific register is currently used.
  bool isRegUsed(Register Reg, bool includeReserved = true) const;

  /// Return all available registers in the register class in Mask.
  BitVector getRegsAvailable(const TargetRegisterClass *RC);

  /// Find an unused register of the specified register class.
  /// Return 0 if none is found.
  Register FindUnusedReg(const TargetRegisterClass *RC) const;

  /// Add a scavenging frame index.
  void addScavengingFrameIndex(int FI) {
    Scavenged.push_back(ScavengedInfo(FI));
  }

  /// Query whether a frame index is a scavenging frame index.
  bool isScavengingFrameIndex(int FI) const {
    for (const ScavengedInfo &SI : Scavenged)
      if (SI.FrameIndex == FI)
        return true;

    return false;
  }

  /// Get an array of scavenging frame indices.
  void getScavengingFrameIndices(SmallVectorImpl<int> &A) const {
    for (const ScavengedInfo &I : Scavenged)
      if (I.FrameIndex >= 0)
        A.push_back(I.FrameIndex);
  }

  /// Make a register of the specific register class available from the current
  /// position backwards to the place before \p To. If \p RestoreAfter is true
  /// this includes the instruction following the current position.
  /// SPAdj is the stack adjustment due to call frame, it's passed along to
  /// eliminateFrameIndex().
  /// Returns the scavenged register.
  ///
  /// If \p AllowSpill is false, fail if a spill is required to make the
  /// register available, and return NoRegister.
  Register scavengeRegisterBackwards(const TargetRegisterClass &RC,
                                     MachineBasicBlock::iterator To,
                                     bool RestoreAfter, int SPAdj,
                                     bool AllowSpill = true);

  /// Tell the scavenger a register is used.
  void setRegUsed(Register Reg, LaneBitmask LaneMask = LaneBitmask::getAll());

private:
  /// Returns true if a register is reserved. It is never "unused".
  bool isReserved(Register Reg) const { return MRI->isReserved(Reg); }

  /// Initialize RegisterScavenger.
  void init(MachineBasicBlock &MBB);

  /// Spill a register after position \p After and reload it before position
  /// \p UseMI.
  ScavengedInfo &spill(Register Reg, const TargetRegisterClass &RC, int SPAdj,
                       MachineBasicBlock::iterator Before,
                       MachineBasicBlock::iterator &UseMI);
};

/// Replaces all frame index virtual registers with physical registers. Uses the
/// register scavenger to find an appropriate register to use.
void scavengeFrameVirtualRegs(MachineFunction &MF, RegScavenger &RS);

} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERSCAVENGING_H
