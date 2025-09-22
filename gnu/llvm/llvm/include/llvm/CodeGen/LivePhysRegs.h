//===- llvm/CodeGen/LivePhysRegs.h - Live Physical Register Set -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the LivePhysRegs utility for tracking liveness of
/// physical registers. This can be used for ad-hoc liveness tracking after
/// register allocation. You can start with the live-ins/live-outs at the
/// beginning/end of a block and update the information while walking the
/// instructions inside the block. This implementation tracks the liveness on a
/// sub-register granularity.
///
/// We assume that the high bits of a physical super-register are not preserved
/// unless the instruction has an implicit-use operand reading the super-
/// register.
///
/// X86 Example:
/// %ymm0 = ...
/// %xmm0 = ... (Kills %xmm0, all %xmm0s sub-registers, and %ymm0)
///
/// %ymm0 = ...
/// %xmm0 = ..., implicit %ymm0 (%ymm0 and all its sub-registers are alive)
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEPHYSREGS_H
#define LLVM_CODEGEN_LIVEPHYSREGS_H

#include "llvm/ADT/SparseSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include <cassert>
#include <utility>

namespace llvm {

template <typename T> class ArrayRef;

class MachineInstr;
class MachineFunction;
class MachineOperand;
class MachineRegisterInfo;
class raw_ostream;

/// A set of physical registers with utility functions to track liveness
/// when walking backward/forward through a basic block.
class LivePhysRegs {
  const TargetRegisterInfo *TRI = nullptr;
  using RegisterSet = SparseSet<MCPhysReg, identity<MCPhysReg>>;
  RegisterSet LiveRegs;

public:
  /// Constructs an unitialized set. init() needs to be called to initialize it.
  LivePhysRegs() = default;

  /// Constructs and initializes an empty set.
  LivePhysRegs(const TargetRegisterInfo &TRI) : TRI(&TRI) {
    LiveRegs.setUniverse(TRI.getNumRegs());
  }

  LivePhysRegs(const LivePhysRegs&) = delete;
  LivePhysRegs &operator=(const LivePhysRegs&) = delete;

  /// (re-)initializes and clears the set.
  void init(const TargetRegisterInfo &TRI) {
    this->TRI = &TRI;
    LiveRegs.clear();
    LiveRegs.setUniverse(TRI.getNumRegs());
  }

  /// Clears the set.
  void clear() { LiveRegs.clear(); }

  /// Returns true if the set is empty.
  bool empty() const { return LiveRegs.empty(); }

  /// Adds a physical register and all its sub-registers to the set.
  void addReg(MCPhysReg Reg) {
    assert(TRI && "LivePhysRegs is not initialized.");
    assert(Reg <= TRI->getNumRegs() && "Expected a physical register.");
    for (MCPhysReg SubReg : TRI->subregs_inclusive(Reg))
      LiveRegs.insert(SubReg);
  }

  /// Removes a physical register, all its sub-registers, and all its
  /// super-registers from the set.
  void removeReg(MCPhysReg Reg) {
    assert(TRI && "LivePhysRegs is not initialized.");
    assert(Reg <= TRI->getNumRegs() && "Expected a physical register.");
    for (MCRegAliasIterator R(Reg, TRI, true); R.isValid(); ++R)
      LiveRegs.erase(*R);
  }

  /// Removes physical registers clobbered by the regmask operand \p MO.
  void removeRegsInMask(const MachineOperand &MO,
        SmallVectorImpl<std::pair<MCPhysReg, const MachineOperand*>> *Clobbers =
        nullptr);

  /// Returns true if register \p Reg is contained in the set. This also
  /// works if only the super register of \p Reg has been defined, because
  /// addReg() always adds all sub-registers to the set as well.
  /// Note: Returns false if just some sub registers are live, use available()
  /// when searching a free register.
  bool contains(MCPhysReg Reg) const { return LiveRegs.count(Reg); }

  /// Returns true if register \p Reg and no aliasing register is in the set.
  bool available(const MachineRegisterInfo &MRI, MCPhysReg Reg) const;

  /// Remove defined registers and regmask kills from the set.
  void removeDefs(const MachineInstr &MI);

  /// Add uses to the set.
  void addUses(const MachineInstr &MI);

  /// Simulates liveness when stepping backwards over an instruction(bundle).
  /// Remove Defs, add uses. This is the recommended way of calculating
  /// liveness.
  void stepBackward(const MachineInstr &MI);

  /// Simulates liveness when stepping forward over an instruction(bundle).
  /// Remove killed-uses, add defs. This is the not recommended way, because it
  /// depends on accurate kill flags. If possible use stepBackward() instead of
  /// this function. The clobbers set will be the list of registers either
  /// defined or clobbered by a regmask.  The operand will identify whether this
  /// is a regmask or register operand.
  void stepForward(const MachineInstr &MI,
        SmallVectorImpl<std::pair<MCPhysReg, const MachineOperand*>> &Clobbers);

  /// Adds all live-in registers of basic block \p MBB.
  /// Live in registers are the registers in the blocks live-in list and the
  /// pristine registers.
  void addLiveIns(const MachineBasicBlock &MBB);

  /// Adds all live-in registers of basic block \p MBB but skips pristine
  /// registers.
  void addLiveInsNoPristines(const MachineBasicBlock &MBB);

  /// Adds all live-out registers of basic block \p MBB.
  /// Live out registers are the union of the live-in registers of the successor
  /// blocks and pristine registers. Live out registers of the end block are the
  /// callee saved registers.
  /// If a register is not added by this method, it is guaranteed to not be
  /// live out from MBB, although a sub-register may be. This is true
  /// both before and after regalloc.
  void addLiveOuts(const MachineBasicBlock &MBB);

  /// Adds all live-out registers of basic block \p MBB but skips pristine
  /// registers.
  void addLiveOutsNoPristines(const MachineBasicBlock &MBB);

  using const_iterator = RegisterSet::const_iterator;

  const_iterator begin() const { return LiveRegs.begin(); }
  const_iterator end() const { return LiveRegs.end(); }

  /// Prints the currently live registers to \p OS.
  void print(raw_ostream &OS) const;

  /// Dumps the currently live registers to the debug output.
  void dump() const;

private:
  /// Adds live-in registers from basic block \p MBB, taking associated
  /// lane masks into consideration.
  void addBlockLiveIns(const MachineBasicBlock &MBB);

  /// Adds pristine registers. Pristine registers are callee saved registers
  /// that are unused in the function.
  void addPristines(const MachineFunction &MF);
};

inline raw_ostream &operator<<(raw_ostream &OS, const LivePhysRegs& LR) {
  LR.print(OS);
  return OS;
}

/// Computes registers live-in to \p MBB assuming all of its successors
/// live-in lists are up-to-date. Puts the result into the given LivePhysReg
/// instance \p LiveRegs.
void computeLiveIns(LivePhysRegs &LiveRegs, const MachineBasicBlock &MBB);

/// Recomputes dead and kill flags in \p MBB.
void recomputeLivenessFlags(MachineBasicBlock &MBB);

/// Adds registers contained in \p LiveRegs to the block live-in list of \p MBB.
/// Does not add reserved registers.
void addLiveIns(MachineBasicBlock &MBB, const LivePhysRegs &LiveRegs);

/// Convenience function combining computeLiveIns() and addLiveIns().
void computeAndAddLiveIns(LivePhysRegs &LiveRegs,
                          MachineBasicBlock &MBB);

/// Convenience function for recomputing live-in's for a MBB. Returns true if
/// any changes were made.
static inline bool recomputeLiveIns(MachineBasicBlock &MBB) {
  LivePhysRegs LPR;
  std::vector<MachineBasicBlock::RegisterMaskPair> OldLiveIns;

  MBB.clearLiveIns(OldLiveIns);
  computeAndAddLiveIns(LPR, MBB);
  MBB.sortUniqueLiveIns();

  const std::vector<MachineBasicBlock::RegisterMaskPair> &NewLiveIns =
      MBB.getLiveIns();
  return OldLiveIns != NewLiveIns;
}

/// Convenience function for recomputing live-in's for a set of MBBs until the
/// computation converges.
inline void fullyRecomputeLiveIns(ArrayRef<MachineBasicBlock *> MBBs) {
  MachineBasicBlock *const *Data = MBBs.data();
  const size_t Len = MBBs.size();
  while (true) {
    bool AnyChange = false;
    for (size_t I = 0; I < Len; ++I)
      if (recomputeLiveIns(*Data[I]))
        AnyChange = true;
    if (!AnyChange)
      return;
  }
}


} // end namespace llvm

#endif // LLVM_CODEGEN_LIVEPHYSREGS_H
