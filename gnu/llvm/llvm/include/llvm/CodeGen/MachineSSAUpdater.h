//===- MachineSSAUpdater.h - Unstructured SSA Update Tool -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MachineSSAUpdater class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINESSAUPDATER_H
#define LLVM_CODEGEN_MACHINESSAUPDATER_H

#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class MachineBasicBlock;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class MachineRegisterInfo;
class TargetInstrInfo;
class TargetRegisterClass;
template<typename T> class SmallVectorImpl;
template<typename T> class SSAUpdaterTraits;

/// MachineSSAUpdater - This class updates SSA form for a set of virtual
/// registers defined in multiple blocks.  This is used when code duplication
/// or another unstructured transformation wants to rewrite a set of uses of one
/// vreg with uses of a set of vregs.
class MachineSSAUpdater {
  friend class SSAUpdaterTraits<MachineSSAUpdater>;

private:
  /// AvailableVals - This keeps track of which value to use on a per-block
  /// basis.  When we insert PHI nodes, we keep track of them here.
  //typedef DenseMap<MachineBasicBlock*, Register> AvailableValsTy;
  void *AV = nullptr;

  /// Register class or bank and LLT of current virtual register.
  MachineRegisterInfo::VRegAttrs RegAttrs;

  /// InsertedPHIs - If this is non-null, the MachineSSAUpdater adds all PHI
  /// nodes that it creates to the vector.
  SmallVectorImpl<MachineInstr*> *InsertedPHIs;

  const TargetInstrInfo *TII = nullptr;
  MachineRegisterInfo *MRI = nullptr;

public:
  /// MachineSSAUpdater constructor.  If InsertedPHIs is specified, it will be
  /// filled in with all PHI Nodes created by rewriting.
  explicit MachineSSAUpdater(MachineFunction &MF,
                        SmallVectorImpl<MachineInstr*> *NewPHI = nullptr);
  MachineSSAUpdater(const MachineSSAUpdater &) = delete;
  MachineSSAUpdater &operator=(const MachineSSAUpdater &) = delete;
  ~MachineSSAUpdater();

  /// Initialize - Reset this object to get ready for a new set of SSA
  /// updates.
  void Initialize(Register V);

  /// AddAvailableValue - Indicate that a rewritten value is available at the
  /// end of the specified block with the specified value.
  void AddAvailableValue(MachineBasicBlock *BB, Register V);

  /// HasValueForBlock - Return true if the MachineSSAUpdater already has a
  /// value for the specified block.
  bool HasValueForBlock(MachineBasicBlock *BB) const;

  /// GetValueAtEndOfBlock - Construct SSA form, materializing a value that is
  /// live at the end of the specified block.
  Register GetValueAtEndOfBlock(MachineBasicBlock *BB);

  /// GetValueInMiddleOfBlock - Construct SSA form, materializing a value that
  /// is live in the middle of the specified block. If ExistingValueOnly is
  /// true then this will only return an existing value or $noreg; otherwise new
  /// instructions may be inserted to materialize a value.
  ///
  /// GetValueInMiddleOfBlock is the same as GetValueAtEndOfBlock except in one
  /// important case: if there is a definition of the rewritten value after the
  /// 'use' in BB.  Consider code like this:
  ///
  ///      X1 = ...
  ///   SomeBB:
  ///      use(X)
  ///      X2 = ...
  ///      br Cond, SomeBB, OutBB
  ///
  /// In this case, there are two values (X1 and X2) added to the AvailableVals
  /// set by the client of the rewriter, and those values are both live out of
  /// their respective blocks.  However, the use of X happens in the *middle* of
  /// a block.  Because of this, we need to insert a new PHI node in SomeBB to
  /// merge the appropriate values, and this value isn't live out of the block.
  Register GetValueInMiddleOfBlock(MachineBasicBlock *BB,
                                   bool ExistingValueOnly = false);

  /// RewriteUse - Rewrite a use of the symbolic value.  This handles PHI nodes,
  /// which use their value in the corresponding predecessor.  Note that this
  /// will not work if the use is supposed to be rewritten to a value defined in
  /// the same block as the use, but above it.  Any 'AddAvailableValue's added
  /// for the use's block will be considered to be below it.
  void RewriteUse(MachineOperand &U);

private:
  // If ExistingValueOnly is true, will not create any new instructions. Used
  // for debug values, which cannot modify Codegen.
  Register GetValueAtEndOfBlockInternal(MachineBasicBlock *BB,
                                        bool ExistingValueOnly = false);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINESSAUPDATER_H
