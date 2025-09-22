//===-- llvm/CodeGen/LiveVariables.h - Live Variable Analysis ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LiveVariables analysis pass.  For each machine
// instruction in the function, this pass calculates the set of registers that
// are immediately dead after the instruction (i.e., the instruction calculates
// the value, but it is never used) and the set of registers that are used by
// the instruction, but are never used after the instruction (i.e., they are
// killed).
//
// This class computes live variables using a sparse implementation based on
// the machine code SSA form.  This class computes live variable information for
// each virtual and _register allocatable_ physical register in a function.  It
// uses the dominance properties of SSA form to efficiently compute live
// variables for virtual registers, and assumes that physical registers are only
// live within a single basic block (allowing it to do a single local analysis
// to resolve physical register lifetimes in each basic block).  If a physical
// register is not register allocatable, it is not tracked.  This is useful for
// things like the stack pointer and condition codes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEVARIABLES_H
#define LLVM_CODEGEN_LIVEVARIABLES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"

namespace llvm {

class MachineBasicBlock;
class MachineRegisterInfo;

class LiveVariables {
  friend class LiveVariablesWrapperPass;

public:
  /// VarInfo - This represents the regions where a virtual register is live in
  /// the program.  We represent this with three different pieces of
  /// information: the set of blocks in which the instruction is live
  /// throughout, the set of blocks in which the instruction is actually used,
  /// and the set of non-phi instructions that are the last users of the value.
  ///
  /// In the common case where a value is defined and killed in the same block,
  /// There is one killing instruction, and AliveBlocks is empty.
  ///
  /// Otherwise, the value is live out of the block.  If the value is live
  /// throughout any blocks, these blocks are listed in AliveBlocks.  Blocks
  /// where the liveness range ends are not included in AliveBlocks, instead
  /// being captured by the Kills set.  In these blocks, the value is live into
  /// the block (unless the value is defined and killed in the same block) and
  /// lives until the specified instruction.  Note that there cannot ever be a
  /// value whose Kills set contains two instructions from the same basic block.
  ///
  /// PHI nodes complicate things a bit.  If a PHI node is the last user of a
  /// value in one of its predecessor blocks, it is not listed in the kills set,
  /// but does include the predecessor block in the AliveBlocks set (unless that
  /// block also defines the value).  This leads to the (perfectly sensical)
  /// situation where a value is defined in a block, and the last use is a phi
  /// node in the successor.  In this case, AliveBlocks is empty (the value is
  /// not live across any  blocks) and Kills is empty (phi nodes are not
  /// included). This is sensical because the value must be live to the end of
  /// the block, but is not live in any successor blocks.
  struct VarInfo {
    /// AliveBlocks - Set of blocks in which this value is alive completely
    /// through.  This is a bit set which uses the basic block number as an
    /// index.
    ///
    SparseBitVector<> AliveBlocks;

    /// Kills - List of MachineInstruction's which are the last use of this
    /// virtual register (kill it) in their basic block.
    ///
    std::vector<MachineInstr*> Kills;

    /// removeKill - Delete a kill corresponding to the specified
    /// machine instruction. Returns true if there was a kill
    /// corresponding to this instruction, false otherwise.
    bool removeKill(MachineInstr &MI) {
      std::vector<MachineInstr *>::iterator I = find(Kills, &MI);
      if (I == Kills.end())
        return false;
      Kills.erase(I);
      return true;
    }

    /// findKill - Find a kill instruction in MBB. Return NULL if none is found.
    MachineInstr *findKill(const MachineBasicBlock *MBB) const;

    /// isLiveIn - Is Reg live in to MBB? This means that Reg is live through
    /// MBB, or it is killed in MBB. If Reg is only used by PHI instructions in
    /// MBB, it is not considered live in.
    bool isLiveIn(const MachineBasicBlock &MBB, Register Reg,
                  MachineRegisterInfo &MRI);

    void print(raw_ostream &OS) const;

    void dump() const;
  };

private:
  /// VirtRegInfo - This list is a mapping from virtual register number to
  /// variable information.
  ///
  IndexedMap<VarInfo, VirtReg2IndexFunctor> VirtRegInfo;

private:   // Intermediate data structures
  MachineFunction *MF = nullptr;

  MachineRegisterInfo *MRI = nullptr;

  const TargetRegisterInfo *TRI = nullptr;

  // PhysRegInfo - Keep track of which instruction was the last def of a
  // physical register. This is a purely local property, because all physical
  // register references are presumed dead across basic blocks.
  std::vector<MachineInstr *> PhysRegDef;

  // PhysRegInfo - Keep track of which instruction was the last use of a
  // physical register. This is a purely local property, because all physical
  // register references are presumed dead across basic blocks.
  std::vector<MachineInstr *> PhysRegUse;

  std::vector<SmallVector<unsigned, 4>> PHIVarInfo;

  // DistanceMap - Keep track the distance of a MI from the start of the
  // current basic block.
  DenseMap<MachineInstr*, unsigned> DistanceMap;

  // For legacy pass.
  LiveVariables() = default;

  void analyze(MachineFunction &MF);

  /// HandlePhysRegKill - Add kills of Reg and its sub-registers to the
  /// uses. Pay special attention to the sub-register uses which may come below
  /// the last use of the whole register.
  bool HandlePhysRegKill(Register Reg, MachineInstr *MI);

  /// HandleRegMask - Call HandlePhysRegKill for all registers clobbered by Mask.
  void HandleRegMask(const MachineOperand &, unsigned);

  void HandlePhysRegUse(Register Reg, MachineInstr &MI);
  void HandlePhysRegDef(Register Reg, MachineInstr *MI,
                        SmallVectorImpl<unsigned> &Defs);
  void UpdatePhysRegDefs(MachineInstr &MI, SmallVectorImpl<unsigned> &Defs);

  /// FindLastRefOrPartRef - Return the last reference or partial reference of
  /// the specified register.
  MachineInstr *FindLastRefOrPartRef(Register Reg);

  /// FindLastPartialDef - Return the last partial def of the specified
  /// register. Also returns the sub-registers that're defined by the
  /// instruction.
  MachineInstr *FindLastPartialDef(Register Reg,
                                   SmallSet<unsigned, 4> &PartDefRegs);

  /// analyzePHINodes - Gather information about the PHI nodes in here. In
  /// particular, we want to map the variable information of a virtual
  /// register which is used in a PHI node. We map that to the BB the vreg
  /// is coming from.
  void analyzePHINodes(const MachineFunction& Fn);

  void runOnInstr(MachineInstr &MI, SmallVectorImpl<unsigned> &Defs,
                  unsigned NumRegs);

  void runOnBlock(MachineBasicBlock *MBB, unsigned NumRegs);

public:
  LiveVariables(MachineFunction &MF);

  void print(raw_ostream &OS) const;

  //===--------------------------------------------------------------------===//
  //  API to update live variable information

  /// Recompute liveness from scratch for a virtual register \p Reg that is
  /// known to have a single def that dominates all uses. This can be useful
  /// after removing some uses of \p Reg. It is not necessary for the whole
  /// machine function to be in SSA form.
  void recomputeForSingleDefVirtReg(Register Reg);

  /// replaceKillInstruction - Update register kill info by replacing a kill
  /// instruction with a new one.
  void replaceKillInstruction(Register Reg, MachineInstr &OldMI,
                              MachineInstr &NewMI);

  /// addVirtualRegisterKilled - Add information about the fact that the
  /// specified register is killed after being used by the specified
  /// instruction. If AddIfNotFound is true, add a implicit operand if it's
  /// not found.
  void addVirtualRegisterKilled(Register IncomingReg, MachineInstr &MI,
                                bool AddIfNotFound = false) {
    if (MI.addRegisterKilled(IncomingReg, TRI, AddIfNotFound))
      getVarInfo(IncomingReg).Kills.push_back(&MI);
  }

  /// removeVirtualRegisterKilled - Remove the specified kill of the virtual
  /// register from the live variable information. Returns true if the
  /// variable was marked as killed by the specified instruction,
  /// false otherwise.
  bool removeVirtualRegisterKilled(Register Reg, MachineInstr &MI) {
    if (!getVarInfo(Reg).removeKill(MI))
      return false;

    bool Removed = false;
    for (MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.isKill() && MO.getReg() == Reg) {
        MO.setIsKill(false);
        Removed = true;
        break;
      }
    }

    assert(Removed && "Register is not used by this instruction!");
    (void)Removed;
    return true;
  }

  /// removeVirtualRegistersKilled - Remove all killed info for the specified
  /// instruction.
  void removeVirtualRegistersKilled(MachineInstr &MI);

  /// addVirtualRegisterDead - Add information about the fact that the specified
  /// register is dead after being used by the specified instruction. If
  /// AddIfNotFound is true, add a implicit operand if it's not found.
  void addVirtualRegisterDead(Register IncomingReg, MachineInstr &MI,
                              bool AddIfNotFound = false) {
    if (MI.addRegisterDead(IncomingReg, TRI, AddIfNotFound))
      getVarInfo(IncomingReg).Kills.push_back(&MI);
  }

  /// removeVirtualRegisterDead - Remove the specified kill of the virtual
  /// register from the live variable information. Returns true if the
  /// variable was marked dead at the specified instruction, false
  /// otherwise.
  bool removeVirtualRegisterDead(Register Reg, MachineInstr &MI) {
    if (!getVarInfo(Reg).removeKill(MI))
      return false;

    bool Removed = false;
    for (MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.isDef() && MO.getReg() == Reg) {
        MO.setIsDead(false);
        Removed = true;
        break;
      }
    }
    assert(Removed && "Register is not defined by this instruction!");
    (void)Removed;
    return true;
  }

  /// getVarInfo - Return the VarInfo structure for the specified VIRTUAL
  /// register.
  VarInfo &getVarInfo(Register Reg);

  void MarkVirtRegAliveInBlock(VarInfo& VRInfo, MachineBasicBlock* DefBlock,
                               MachineBasicBlock *BB);
  void MarkVirtRegAliveInBlock(VarInfo &VRInfo, MachineBasicBlock *DefBlock,
                               MachineBasicBlock *BB,
                               SmallVectorImpl<MachineBasicBlock *> &WorkList);

  void HandleVirtRegDef(Register reg, MachineInstr &MI);
  void HandleVirtRegUse(Register reg, MachineBasicBlock *MBB, MachineInstr &MI);

  bool isLiveIn(Register Reg, const MachineBasicBlock &MBB) {
    return getVarInfo(Reg).isLiveIn(MBB, Reg, *MRI);
  }

  /// isLiveOut - Determine if Reg is live out from MBB, when not considering
  /// PHI nodes. This means that Reg is either killed by a successor block or
  /// passed through one.
  bool isLiveOut(Register Reg, const MachineBasicBlock &MBB);

  /// addNewBlock - Add a new basic block BB between DomBB and SuccBB. All
  /// variables that are live out of DomBB and live into SuccBB will be marked
  /// as passing live through BB. This method assumes that the machine code is
  /// still in SSA form.
  void addNewBlock(MachineBasicBlock *BB,
                   MachineBasicBlock *DomBB,
                   MachineBasicBlock *SuccBB);

  void addNewBlock(MachineBasicBlock *BB,
                   MachineBasicBlock *DomBB,
                   MachineBasicBlock *SuccBB,
                   std::vector<SparseBitVector<>> &LiveInSets);
};

class LiveVariablesAnalysis : public AnalysisInfoMixin<LiveVariablesAnalysis> {
  friend AnalysisInfoMixin<LiveVariablesAnalysis>;
  static AnalysisKey Key;

public:
  using Result = LiveVariables;
  Result run(MachineFunction &MF, MachineFunctionAnalysisManager &);
};

class LiveVariablesPrinterPass
    : public PassInfoMixin<LiveVariablesPrinterPass> {
  raw_ostream &OS;

public:
  explicit LiveVariablesPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
  static bool isRequired() { return true; }
};

class LiveVariablesWrapperPass : public MachineFunctionPass {
  LiveVariables LV;

public:
  static char ID; // Pass identification, replacement for typeid

  LiveVariablesWrapperPass() : MachineFunctionPass(ID) {
    initializeLiveVariablesWrapperPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    LV.analyze(MF);
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void releaseMemory() override { LV.VirtRegInfo.clear(); }

  LiveVariables &getLV() { return LV; }
};

} // End llvm namespace

#endif
