//===- llvm/CodeGen/MachineDomTreeUpdater.h -----------------------*- C++-*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file exposes interfaces to post dominance information for
// target-specific code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEDOMTREEUPDATER_H
#define LLVM_CODEGEN_MACHINEDOMTREEUPDATER_H

#include "llvm/Analysis/GenericDomTreeUpdater.h"
#include "llvm/CodeGen/MachineDominators.h"

namespace llvm {

class MachinePostDominatorTree;

class MachineDomTreeUpdater
    : public GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                                   MachinePostDominatorTree> {
  friend GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                               MachinePostDominatorTree>;

public:
  using Base =
      GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                            MachinePostDominatorTree>;
  using Base::Base;

  ~MachineDomTreeUpdater() { flush(); }

  ///@{
  /// \name Mutation APIs
  ///

  /// Delete DelBB. DelBB will be removed from its Parent and
  /// erased from available trees if it exists and finally get deleted.
  /// Under Eager UpdateStrategy, DelBB will be processed immediately.
  /// Under Lazy UpdateStrategy, DelBB will be queued until a flush event and
  /// all available trees are up-to-date. Assert if any instruction of DelBB is
  /// modified while awaiting deletion. When both DT and PDT are nullptrs, DelBB
  /// will be queued until flush() is called.
  void deleteBB(MachineBasicBlock *DelBB);

  ///@}

private:
  /// First remove all the instructions of DelBB and then make sure DelBB has a
  /// valid terminator instruction which is necessary to have when DelBB still
  /// has to be inside of its parent Function while awaiting deletion under Lazy
  /// UpdateStrategy to prevent other routines from asserting the state of the
  /// IR is inconsistent. Assert if DelBB is nullptr or has predecessors.
  void validateDeleteBB(MachineBasicBlock *DelBB);

  /// Returns true if at least one MachineBasicBlock is deleted.
  bool forceFlushDeletedBB();
};

extern template class GenericDomTreeUpdater<
    MachineDomTreeUpdater, MachineDominatorTree, MachinePostDominatorTree>;

extern template void
GenericDomTreeUpdater<MachineDomTreeUpdater, MachineDominatorTree,
                      MachinePostDominatorTree>::recalculate(MachineFunction
                                                                 &MF);
} // namespace llvm
#endif // LLVM_CODEGEN_MACHINEDOMTREEUPDATER_H
