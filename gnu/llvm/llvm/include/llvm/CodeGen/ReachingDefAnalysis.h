//==--- llvm/CodeGen/ReachingDefAnalysis.h - Reaching Def Analysis -*- C++ -*---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Reaching Defs Analysis pass.
///
/// This pass tracks for each instruction what is the "closest" reaching def of
/// a given register. It is used by BreakFalseDeps (for clearance calculation)
/// and ExecutionDomainFix (for arbitrating conflicting domains).
///
/// Note that this is different from the usual definition notion of liveness.
/// The CPU doesn't care whether or not we consider a register killed.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REACHINGDEFANALYSIS_H
#define LLVM_CODEGEN_REACHINGDEFANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/CodeGen/LoopTraversal.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/InitializePasses.h"

namespace llvm {

class MachineBasicBlock;
class MachineInstr;

/// Thin wrapper around "int" used to store reaching definitions,
/// using an encoding that makes it compatible with TinyPtrVector.
/// The 0th LSB is forced zero (and will be used for pointer union tagging),
/// The 1st LSB is forced one (to make sure the value is non-zero).
class ReachingDef {
  uintptr_t Encoded;
  friend struct PointerLikeTypeTraits<ReachingDef>;
  explicit ReachingDef(uintptr_t Encoded) : Encoded(Encoded) {}

public:
  ReachingDef(std::nullptr_t) : Encoded(0) {}
  ReachingDef(int Instr) : Encoded(((uintptr_t) Instr << 2) | 2) {}
  operator int() const { return ((int) Encoded) >> 2; }
};

template<>
struct PointerLikeTypeTraits<ReachingDef> {
  static constexpr int NumLowBitsAvailable = 1;

  static inline void *getAsVoidPointer(const ReachingDef &RD) {
    return reinterpret_cast<void *>(RD.Encoded);
  }

  static inline ReachingDef getFromVoidPointer(void *P) {
    return ReachingDef(reinterpret_cast<uintptr_t>(P));
  }

  static inline ReachingDef getFromVoidPointer(const void *P) {
    return ReachingDef(reinterpret_cast<uintptr_t>(P));
  }
};

/// This class provides the reaching def analysis.
class ReachingDefAnalysis : public MachineFunctionPass {
private:
  MachineFunction *MF = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  LoopTraversal::TraversalOrder TraversedMBBOrder;
  unsigned NumRegUnits = 0;
  /// Instruction that defined each register, relative to the beginning of the
  /// current basic block.  When a LiveRegsDefInfo is used to represent a
  /// live-out register, this value is relative to the end of the basic block,
  /// so it will be a negative number.
  using LiveRegsDefInfo = std::vector<int>;
  LiveRegsDefInfo LiveRegs;

  /// Keeps clearance information for all registers. Note that this
  /// is different from the usual definition notion of liveness. The CPU
  /// doesn't care whether or not we consider a register killed.
  using OutRegsInfoMap = SmallVector<LiveRegsDefInfo, 4>;
  OutRegsInfoMap MBBOutRegsInfos;

  /// Current instruction number.
  /// The first instruction in each basic block is 0.
  int CurInstr = -1;

  /// Maps instructions to their instruction Ids, relative to the beginning of
  /// their basic blocks.
  DenseMap<MachineInstr *, int> InstIds;

  /// All reaching defs of a given RegUnit for a given MBB.
  using MBBRegUnitDefs = TinyPtrVector<ReachingDef>;
  /// All reaching defs of all reg units for a given MBB
  using MBBDefsInfo = std::vector<MBBRegUnitDefs>;
  /// All reaching defs of all reg units for a all MBBs
  using MBBReachingDefsInfo = SmallVector<MBBDefsInfo, 4>;
  MBBReachingDefsInfo MBBReachingDefs;

  /// Default values are 'nothing happened a long time ago'.
  const int ReachingDefDefaultVal = -(1 << 21);

  using InstSet = SmallPtrSetImpl<MachineInstr*>;
  using BlockSet = SmallPtrSetImpl<MachineBasicBlock*>;

public:
  static char ID; // Pass identification, replacement for typeid

  ReachingDefAnalysis() : MachineFunctionPass(ID) {
    initializeReachingDefAnalysisPass(*PassRegistry::getPassRegistry());
  }
  void releaseMemory() override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs).set(
          MachineFunctionProperties::Property::TracksLiveness);
  }

  /// Re-run the analysis.
  void reset();

  /// Initialize data structures.
  void init();

  /// Traverse the machine function, mapping definitions.
  void traverse();

  /// Provides the instruction id of the closest reaching def instruction of
  /// PhysReg that reaches MI, relative to the begining of MI's basic block.
  int getReachingDef(MachineInstr *MI, MCRegister PhysReg) const;

  /// Return whether A and B use the same def of PhysReg.
  bool hasSameReachingDef(MachineInstr *A, MachineInstr *B,
                          MCRegister PhysReg) const;

  /// Return whether the reaching def for MI also is live out of its parent
  /// block.
  bool isReachingDefLiveOut(MachineInstr *MI, MCRegister PhysReg) const;

  /// Return the local MI that produces the live out value for PhysReg, or
  /// nullptr for a non-live out or non-local def.
  MachineInstr *getLocalLiveOutMIDef(MachineBasicBlock *MBB,
                                     MCRegister PhysReg) const;

  /// If a single MachineInstr creates the reaching definition, then return it.
  /// Otherwise return null.
  MachineInstr *getUniqueReachingMIDef(MachineInstr *MI,
                                       MCRegister PhysReg) const;

  /// If a single MachineInstr creates the reaching definition, for MIs operand
  /// at Idx, then return it. Otherwise return null.
  MachineInstr *getMIOperand(MachineInstr *MI, unsigned Idx) const;

  /// If a single MachineInstr creates the reaching definition, for MIs MO,
  /// then return it. Otherwise return null.
  MachineInstr *getMIOperand(MachineInstr *MI, MachineOperand &MO) const;

  /// Provide whether the register has been defined in the same basic block as,
  /// and before, MI.
  bool hasLocalDefBefore(MachineInstr *MI, MCRegister PhysReg) const;

  /// Return whether the given register is used after MI, whether it's a local
  /// use or a live out.
  bool isRegUsedAfter(MachineInstr *MI, MCRegister PhysReg) const;

  /// Return whether the given register is defined after MI.
  bool isRegDefinedAfter(MachineInstr *MI, MCRegister PhysReg) const;

  /// Provides the clearance - the number of instructions since the closest
  /// reaching def instuction of PhysReg that reaches MI.
  int getClearance(MachineInstr *MI, MCRegister PhysReg) const;

  /// Provides the uses, in the same block as MI, of register that MI defines.
  /// This does not consider live-outs.
  void getReachingLocalUses(MachineInstr *MI, MCRegister PhysReg,
                            InstSet &Uses) const;

  /// Search MBB for a definition of PhysReg and insert it into Defs. If no
  /// definition is found, recursively search the predecessor blocks for them.
  void getLiveOuts(MachineBasicBlock *MBB, MCRegister PhysReg, InstSet &Defs,
                   BlockSet &VisitedBBs) const;
  void getLiveOuts(MachineBasicBlock *MBB, MCRegister PhysReg,
                   InstSet &Defs) const;

  /// For the given block, collect the instructions that use the live-in
  /// value of the provided register. Return whether the value is still
  /// live on exit.
  bool getLiveInUses(MachineBasicBlock *MBB, MCRegister PhysReg,
                     InstSet &Uses) const;

  /// Collect the users of the value stored in PhysReg, which is defined
  /// by MI.
  void getGlobalUses(MachineInstr *MI, MCRegister PhysReg, InstSet &Uses) const;

  /// Collect all possible definitions of the value stored in PhysReg, which is
  /// used by MI.
  void getGlobalReachingDefs(MachineInstr *MI, MCRegister PhysReg,
                             InstSet &Defs) const;

  /// Return whether From can be moved forwards to just before To.
  bool isSafeToMoveForwards(MachineInstr *From, MachineInstr *To) const;

  /// Return whether From can be moved backwards to just after To.
  bool isSafeToMoveBackwards(MachineInstr *From, MachineInstr *To) const;

  /// Assuming MI is dead, recursively search the incoming operands which are
  /// killed by MI and collect those that would become dead.
  void collectKilledOperands(MachineInstr *MI, InstSet &Dead) const;

  /// Return whether removing this instruction will have no effect on the
  /// program, returning the redundant use-def chain.
  bool isSafeToRemove(MachineInstr *MI, InstSet &ToRemove) const;

  /// Return whether removing this instruction will have no effect on the
  /// program, ignoring the possible effects on some instructions, returning
  /// the redundant use-def chain.
  bool isSafeToRemove(MachineInstr *MI, InstSet &ToRemove,
                      InstSet &Ignore) const;

  /// Return whether a MachineInstr could be inserted at MI and safely define
  /// the given register without affecting the program.
  bool isSafeToDefRegAt(MachineInstr *MI, MCRegister PhysReg) const;

  /// Return whether a MachineInstr could be inserted at MI and safely define
  /// the given register without affecting the program, ignoring any effects
  /// on the provided instructions.
  bool isSafeToDefRegAt(MachineInstr *MI, MCRegister PhysReg,
                        InstSet &Ignore) const;

private:
  /// Set up LiveRegs by merging predecessor live-out values.
  void enterBasicBlock(MachineBasicBlock *MBB);

  /// Update live-out values.
  void leaveBasicBlock(MachineBasicBlock *MBB);

  /// Process he given basic block.
  void processBasicBlock(const LoopTraversal::TraversedMBBInfo &TraversedMBB);

  /// Process block that is part of a loop again.
  void reprocessBasicBlock(MachineBasicBlock *MBB);

  /// Update def-ages for registers defined by MI.
  /// Also break dependencies on partial defs and undef uses.
  void processDefs(MachineInstr *);

  /// Utility function for isSafeToMoveForwards/Backwards.
  template<typename Iterator>
  bool isSafeToMove(MachineInstr *From, MachineInstr *To) const;

  /// Return whether removing this instruction will have no effect on the
  /// program, ignoring the possible effects on some instructions, returning
  /// the redundant use-def chain.
  bool isSafeToRemove(MachineInstr *MI, InstSet &Visited,
                      InstSet &ToRemove, InstSet &Ignore) const;

  /// Provides the MI, from the given block, corresponding to the Id or a
  /// nullptr if the id does not refer to the block.
  MachineInstr *getInstFromId(MachineBasicBlock *MBB, int InstId) const;

  /// Provides the instruction of the closest reaching def instruction of
  /// PhysReg that reaches MI, relative to the begining of MI's basic block.
  MachineInstr *getReachingLocalMIDef(MachineInstr *MI,
                                      MCRegister PhysReg) const;
};

} // namespace llvm

#endif // LLVM_CODEGEN_REACHINGDEFANALYSIS_H
