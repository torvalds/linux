//===- llvm/CodeGen/MachineLoopInfo.h - Natural Loop Calculator -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineLoopInfo class that is used to identify natural
// loops and determine the loop depth of various nodes of the CFG.  Note that
// natural loops may actually be several loops that share the same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks the make up the loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * the trip count
//  * etc...
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINELOOPINFO_H
#define LLVM_CODEGEN_MACHINELOOPINFO_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/GenericLoopInfo.h"

namespace llvm {

class MachineDominatorTree;
// Implementation in LoopInfoImpl.h
class MachineLoop;
extern template class LoopBase<MachineBasicBlock, MachineLoop>;

class MachineLoop : public LoopBase<MachineBasicBlock, MachineLoop> {
public:
  /// Return the "top" block in the loop, which is the first block in the linear
  /// layout, ignoring any parts of the loop not contiguous with the part that
  /// contains the header.
  MachineBasicBlock *getTopBlock();

  /// Return the "bottom" block in the loop, which is the last block in the
  /// linear layout, ignoring any parts of the loop not contiguous with the part
  /// that contains the header.
  MachineBasicBlock *getBottomBlock();

  /// Find the block that contains the loop control variable and the
  /// loop test. This will return the latch block if it's one of the exiting
  /// blocks. Otherwise, return the exiting block. Return 'null' when
  /// multiple exiting blocks are present.
  MachineBasicBlock *findLoopControlBlock() const;

  /// Return the debug location of the start of this loop.
  /// This looks for a BB terminating instruction with a known debug
  /// location by looking at the preheader and header blocks. If it
  /// cannot find a terminating instruction with location information,
  /// it returns an unknown location.
  DebugLoc getStartLoc() const;

  /// Find the llvm.loop metadata for this loop.
  /// If each branch to the header of this loop contains the same llvm.loop
  /// metadata, then this metadata node is returned. Otherwise, if any
  /// latch instruction does not contain the llvm.loop metadata or
  /// multiple latch instructions contain different llvm.loop metadata nodes,
  /// then null is returned.
  MDNode *getLoopID() const;

  /// Returns true if the instruction is loop invariant.
  /// I.e., all virtual register operands are defined outside of the loop,
  /// physical registers aren't accessed explicitly, and there are no side
  /// effects that aren't captured by the operands or other flags.
  /// ExcludeReg can be used to exclude the given register from the check
  /// i.e. when we're considering hoisting it's definition but not hoisted it
  /// yet
  bool isLoopInvariant(MachineInstr &I, const Register ExcludeReg = 0) const;

  void dump() const;

private:
  friend class LoopInfoBase<MachineBasicBlock, MachineLoop>;

  /// Returns true if the given physreg has no defs inside the loop.
  bool isLoopInvariantImplicitPhysReg(Register Reg) const;

  explicit MachineLoop(MachineBasicBlock *MBB)
    : LoopBase<MachineBasicBlock, MachineLoop>(MBB) {}

  MachineLoop() = default;
};

// Implementation in LoopInfoImpl.h
extern template class LoopInfoBase<MachineBasicBlock, MachineLoop>;

class MachineLoopInfo : public LoopInfoBase<MachineBasicBlock, MachineLoop> {
  friend class LoopBase<MachineBasicBlock, MachineLoop>;
  friend class MachineLoopInfoWrapperPass;

public:
  MachineLoopInfo() = default;
  explicit MachineLoopInfo(MachineDominatorTree &MDT) { calculate(MDT); }
  MachineLoopInfo(MachineLoopInfo &&) = default;
  MachineLoopInfo(const MachineLoopInfo &) = delete;
  MachineLoopInfo &operator=(const MachineLoopInfo &) = delete;

  /// Handle invalidation explicitly.
  bool invalidate(MachineFunction &, const PreservedAnalyses &PA,
                  MachineFunctionAnalysisManager::Invalidator &);

  /// Find the block that either is the loop preheader, or could
  /// speculatively be used as the preheader. This is e.g. useful to place
  /// loop setup code. Code that cannot be speculated should not be placed
  /// here. SpeculativePreheader is controlling whether it also tries to
  /// find the speculative preheader if the regular preheader is not present.
  /// With FindMultiLoopPreheader = false, nullptr will be returned if the found
  /// preheader is the preheader of multiple loops.
  MachineBasicBlock *
  findLoopPreheader(MachineLoop *L, bool SpeculativePreheader = false,
                    bool FindMultiLoopPreheader = false) const;

  /// Calculate the natural loop information.
  void calculate(MachineDominatorTree &MDT);
};

/// Analysis pass that exposes the \c MachineLoopInfo for a machine function.
class MachineLoopAnalysis : public AnalysisInfoMixin<MachineLoopAnalysis> {
  friend AnalysisInfoMixin<MachineLoopAnalysis>;
  static AnalysisKey Key;

public:
  using Result = MachineLoopInfo;
  Result run(MachineFunction &MF, MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c LoopAnalysis results.
class MachineLoopPrinterPass : public PassInfoMixin<MachineLoopPrinterPass> {
  raw_ostream &OS;

public:
  explicit MachineLoopPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
  static bool isRequired() { return true; }
};

class MachineLoopInfoWrapperPass : public MachineFunctionPass {
  MachineLoopInfo LI;

public:
  static char ID; // Pass identification, replacement for typeid

  MachineLoopInfoWrapperPass();

  bool runOnMachineFunction(MachineFunction &F) override;

  void releaseMemory() override { LI.releaseMemory(); }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  MachineLoopInfo &getLI() { return LI; }
};

// Allow clients to walk the list of nested loops...
template <> struct GraphTraits<const MachineLoop*> {
  using NodeRef = const MachineLoop *;
  using ChildIteratorType = MachineLoopInfo::iterator;

  static NodeRef getEntryNode(const MachineLoop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

template <> struct GraphTraits<MachineLoop*> {
  using NodeRef = MachineLoop *;
  using ChildIteratorType = MachineLoopInfo::iterator;

  static NodeRef getEntryNode(MachineLoop *L) { return L; }
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINELOOPINFO_H
