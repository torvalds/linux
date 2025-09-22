//===- SwiftErrorValueTracking.h - Track swifterror VReg vals --*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements a limited mem2reg-like analysis to promote uses of function
// arguments and allocas marked with swiftalloc from memory into virtual
// registers tracked by this class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SWIFTERRORVALUETRACKING_H
#define LLVM_CODEGEN_SWIFTERRORVALUETRACKING_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugLoc.h"
#include <utility>


namespace llvm {
  class Function;
  class MachineBasicBlock;
  class MachineFunction;
  class MachineInstr;
  class TargetInstrInfo;
  class TargetLowering;

class SwiftErrorValueTracking {
  // Some useful objects to reduce the number of function arguments needed.
  MachineFunction *MF;
  const Function *Fn;
  const TargetLowering *TLI;
  const TargetInstrInfo *TII;

  /// A map from swifterror value in a basic block to the virtual register it is
  /// currently represented by.
  DenseMap<std::pair<const MachineBasicBlock *, const Value *>, Register>
      VRegDefMap;

  /// A list of upward exposed vreg uses that need to be satisfied by either a
  /// copy def or a phi node at the beginning of the basic block representing
  /// the predecessor(s) swifterror value.
  DenseMap<std::pair<const MachineBasicBlock *, const Value *>, Register>
      VRegUpwardsUse;

  /// A map from instructions that define/use a swifterror value to the virtual
  /// register that represents that def/use.
  llvm::DenseMap<PointerIntPair<const Instruction *, 1, bool>, Register>
      VRegDefUses;

  /// The swifterror argument of the current function.
  const Value *SwiftErrorArg;

  using SwiftErrorValues = SmallVector<const Value*, 1>;
  /// A function can only have a single swifterror argument. And if it does
  /// have a swifterror argument, it must be the first entry in
  /// SwiftErrorVals.
  SwiftErrorValues SwiftErrorVals;

public:
  /// Initialize data structures for specified new function.
  void setFunction(MachineFunction &MF);

  /// Get the (unique) function argument that was marked swifterror, or nullptr
  /// if this function has no swifterror args.
  const Value *getFunctionArg() const {
    return SwiftErrorArg;
  }

  /// Get or create the swifterror value virtual register in
  /// VRegDefMap for this basic block.
  Register getOrCreateVReg(const MachineBasicBlock *, const Value *);

  /// Set the swifterror virtual register in the VRegDefMap for this
  /// basic block.
  void setCurrentVReg(const MachineBasicBlock *MBB, const Value *, Register);

  /// Get or create the swifterror value virtual register for a def of a
  /// swifterror by an instruction.
  Register getOrCreateVRegDefAt(const Instruction *, const MachineBasicBlock *,
                                const Value *);

  /// Get or create the swifterror value virtual register for a use of a
  /// swifterror by an instruction.
  Register getOrCreateVRegUseAt(const Instruction *, const MachineBasicBlock *,
                                const Value *);

  /// Create initial definitions of swifterror values in the entry block of the
  /// current function.
  bool createEntriesInEntryBlock(DebugLoc DbgLoc);

  /// Propagate assigned swifterror vregs through a function, synthesizing PHI
  /// nodes when needed to maintain consistency.
  void propagateVRegs();

  void preassignVRegs(MachineBasicBlock *MBB, BasicBlock::const_iterator Begin,
                      BasicBlock::const_iterator End);
};

}

#endif
