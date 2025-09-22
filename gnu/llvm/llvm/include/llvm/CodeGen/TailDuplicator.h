//===- llvm/CodeGen/TailDuplicator.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the TailDuplicator class. Used by the
// TailDuplication pass, and MachineBlockPlacement.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TAILDUPLICATOR_H
#define LLVM_CODEGEN_TAILDUPLICATOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <utility>
#include <vector>

namespace llvm {

template <typename T, unsigned int N> class SmallSetVector;
template <typename Fn> class function_ref;
class MBFIWrapper;
class MachineBasicBlock;
class MachineBranchProbabilityInfo;
class MachineFunction;
class MachineInstr;
class MachineModuleInfo;
class MachineRegisterInfo;
class ProfileSummaryInfo;
class TargetRegisterInfo;

/// Utility class to perform tail duplication.
class TailDuplicator {
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const MachineBranchProbabilityInfo *MBPI;
  MachineRegisterInfo *MRI;
  MachineFunction *MF;
  MBFIWrapper *MBFI;
  ProfileSummaryInfo *PSI;
  bool PreRegAlloc;
  bool LayoutMode;
  unsigned TailDupSize;

  // A list of virtual registers for which to update SSA form.
  SmallVector<Register, 16> SSAUpdateVRs;

  // For each virtual register in SSAUpdateVals keep a list of source virtual
  // registers.
  using AvailableValsTy = std::vector<std::pair<MachineBasicBlock *, Register>>;

  DenseMap<Register, AvailableValsTy> SSAUpdateVals;

public:
  /// Prepare to run on a specific machine function.
  /// @param MF - Function that will be processed
  /// @param PreRegAlloc - true if used before register allocation
  /// @param MBPI - Branch Probability Info. Used to propagate correct
  ///     probabilities when modifying the CFG.
  /// @param LayoutMode - When true, don't use the existing layout to make
  ///     decisions.
  /// @param TailDupSize - Maxmimum size of blocks to tail-duplicate. Zero
  ///     default implies using the command line value TailDupSize.
  void initMF(MachineFunction &MF, bool PreRegAlloc,
              const MachineBranchProbabilityInfo *MBPI,
              MBFIWrapper *MBFI,
              ProfileSummaryInfo *PSI,
              bool LayoutMode, unsigned TailDupSize = 0);

  bool tailDuplicateBlocks();
  static bool isSimpleBB(MachineBasicBlock *TailBB);
  bool shouldTailDuplicate(bool IsSimple, MachineBasicBlock &TailBB);

  /// Returns true if TailBB can successfully be duplicated into PredBB
  bool canTailDuplicate(MachineBasicBlock *TailBB, MachineBasicBlock *PredBB);

  /// Tail duplicate a single basic block into its predecessors, and then clean
  /// up.
  /// If \p DuplicatePreds is not null, it will be updated to contain the list
  /// of predecessors that received a copy of \p MBB.
  /// If \p RemovalCallback is non-null. It will be called before MBB is
  /// deleted.
  /// If \p CandidatePtr is not null, duplicate into these blocks only.
  bool tailDuplicateAndUpdate(
      bool IsSimple, MachineBasicBlock *MBB,
      MachineBasicBlock *ForcedLayoutPred,
      SmallVectorImpl<MachineBasicBlock*> *DuplicatedPreds = nullptr,
      function_ref<void(MachineBasicBlock *)> *RemovalCallback = nullptr,
      SmallVectorImpl<MachineBasicBlock *> *CandidatePtr = nullptr);

private:
  using RegSubRegPair = TargetInstrInfo::RegSubRegPair;

  void addSSAUpdateEntry(Register OrigReg, Register NewReg,
                         MachineBasicBlock *BB);
  void processPHI(MachineInstr *MI, MachineBasicBlock *TailBB,
                  MachineBasicBlock *PredBB,
                  DenseMap<Register, RegSubRegPair> &LocalVRMap,
                  SmallVectorImpl<std::pair<Register, RegSubRegPair>> &Copies,
                  const DenseSet<Register> &UsedByPhi, bool Remove);
  void duplicateInstruction(MachineInstr *MI, MachineBasicBlock *TailBB,
                            MachineBasicBlock *PredBB,
                            DenseMap<Register, RegSubRegPair> &LocalVRMap,
                            const DenseSet<Register> &UsedByPhi);
  void updateSuccessorsPHIs(MachineBasicBlock *FromBB, bool isDead,
                            SmallVectorImpl<MachineBasicBlock *> &TDBBs,
                            SmallSetVector<MachineBasicBlock *, 8> &Succs);
  bool canCompletelyDuplicateBB(MachineBasicBlock &BB);
  bool duplicateSimpleBB(MachineBasicBlock *TailBB,
                         SmallVectorImpl<MachineBasicBlock *> &TDBBs,
                         const DenseSet<Register> &RegsUsedByPhi);
  bool tailDuplicate(bool IsSimple,
                     MachineBasicBlock *TailBB,
                     MachineBasicBlock *ForcedLayoutPred,
                     SmallVectorImpl<MachineBasicBlock *> &TDBBs,
                     SmallVectorImpl<MachineInstr *> &Copies,
                     SmallVectorImpl<MachineBasicBlock *> *CandidatePtr);
  void appendCopies(MachineBasicBlock *MBB,
                 SmallVectorImpl<std::pair<Register, RegSubRegPair>> &CopyInfos,
                 SmallVectorImpl<MachineInstr *> &Copies);

  void removeDeadBlock(
      MachineBasicBlock *MBB,
      function_ref<void(MachineBasicBlock *)> *RemovalCallback = nullptr);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TAILDUPLICATOR_H
