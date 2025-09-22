//===- TailDuplicator.cpp - Duplicate blocks into predecessors' tails -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility class duplicates basic blocks ending in unconditional branches
// into the tails of their predecessors.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/TailDuplicator.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/CodeGen/MachineSizeOpts.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "tailduplication"

STATISTIC(NumTails, "Number of tails duplicated");
STATISTIC(NumTailDups, "Number of tail duplicated blocks");
STATISTIC(NumTailDupAdded,
          "Number of instructions added due to tail duplication");
STATISTIC(NumTailDupRemoved,
          "Number of instructions removed due to tail duplication");
STATISTIC(NumDeadBlocks, "Number of dead blocks removed");
STATISTIC(NumAddedPHIs, "Number of phis added");

// Heuristic for tail duplication.
static cl::opt<unsigned> TailDuplicateSize(
    "tail-dup-size",
    cl::desc("Maximum instructions to consider tail duplicating"), cl::init(2),
    cl::Hidden);

static cl::opt<unsigned> TailDupIndirectBranchSize(
    "tail-dup-indirect-size",
    cl::desc("Maximum instructions to consider tail duplicating blocks that "
             "end with indirect branches."), cl::init(20),
    cl::Hidden);

static cl::opt<unsigned>
    TailDupPredSize("tail-dup-pred-size",
                    cl::desc("Maximum predecessors (maximum successors at the "
                             "same time) to consider tail duplicating blocks."),
                    cl::init(16), cl::Hidden);

static cl::opt<unsigned>
    TailDupSuccSize("tail-dup-succ-size",
                    cl::desc("Maximum successors (maximum predecessors at the "
                             "same time) to consider tail duplicating blocks."),
                    cl::init(16), cl::Hidden);

static cl::opt<bool>
    TailDupVerify("tail-dup-verify",
                  cl::desc("Verify sanity of PHI instructions during taildup"),
                  cl::init(false), cl::Hidden);

static cl::opt<unsigned> TailDupLimit("tail-dup-limit", cl::init(~0U),
                                      cl::Hidden);

void TailDuplicator::initMF(MachineFunction &MFin, bool PreRegAlloc,
                            const MachineBranchProbabilityInfo *MBPIin,
                            MBFIWrapper *MBFIin,
                            ProfileSummaryInfo *PSIin,
                            bool LayoutModeIn, unsigned TailDupSizeIn) {
  MF = &MFin;
  TII = MF->getSubtarget().getInstrInfo();
  TRI = MF->getSubtarget().getRegisterInfo();
  MRI = &MF->getRegInfo();
  MBPI = MBPIin;
  MBFI = MBFIin;
  PSI = PSIin;
  TailDupSize = TailDupSizeIn;

  assert(MBPI != nullptr && "Machine Branch Probability Info required");

  LayoutMode = LayoutModeIn;
  this->PreRegAlloc = PreRegAlloc;
}

static void VerifyPHIs(MachineFunction &MF, bool CheckExtra) {
  for (MachineBasicBlock &MBB : llvm::drop_begin(MF)) {
    SmallSetVector<MachineBasicBlock *, 8> Preds(MBB.pred_begin(),
                                                 MBB.pred_end());
    MachineBasicBlock::iterator MI = MBB.begin();
    while (MI != MBB.end()) {
      if (!MI->isPHI())
        break;
      for (MachineBasicBlock *PredBB : Preds) {
        bool Found = false;
        for (unsigned i = 1, e = MI->getNumOperands(); i != e; i += 2) {
          MachineBasicBlock *PHIBB = MI->getOperand(i + 1).getMBB();
          if (PHIBB == PredBB) {
            Found = true;
            break;
          }
        }
        if (!Found) {
          dbgs() << "Malformed PHI in " << printMBBReference(MBB) << ": "
                 << *MI;
          dbgs() << "  missing input from predecessor "
                 << printMBBReference(*PredBB) << '\n';
          llvm_unreachable(nullptr);
        }
      }

      for (unsigned i = 1, e = MI->getNumOperands(); i != e; i += 2) {
        MachineBasicBlock *PHIBB = MI->getOperand(i + 1).getMBB();
        if (CheckExtra && !Preds.count(PHIBB)) {
          dbgs() << "Warning: malformed PHI in " << printMBBReference(MBB)
                 << ": " << *MI;
          dbgs() << "  extra input from predecessor "
                 << printMBBReference(*PHIBB) << '\n';
          llvm_unreachable(nullptr);
        }
        if (PHIBB->getNumber() < 0) {
          dbgs() << "Malformed PHI in " << printMBBReference(MBB) << ": "
                 << *MI;
          dbgs() << "  non-existing " << printMBBReference(*PHIBB) << '\n';
          llvm_unreachable(nullptr);
        }
      }
      ++MI;
    }
  }
}

/// Tail duplicate the block and cleanup.
/// \p IsSimple - return value of isSimpleBB
/// \p MBB - block to be duplicated
/// \p ForcedLayoutPred - If non-null, treat this block as the layout
///     predecessor, instead of using the ordering in MF
/// \p DuplicatedPreds - if non-null, \p DuplicatedPreds will contain a list of
///     all Preds that received a copy of \p MBB.
/// \p RemovalCallback - if non-null, called just before MBB is deleted.
bool TailDuplicator::tailDuplicateAndUpdate(
    bool IsSimple, MachineBasicBlock *MBB,
    MachineBasicBlock *ForcedLayoutPred,
    SmallVectorImpl<MachineBasicBlock*> *DuplicatedPreds,
    function_ref<void(MachineBasicBlock *)> *RemovalCallback,
    SmallVectorImpl<MachineBasicBlock *> *CandidatePtr) {
  // Save the successors list.
  SmallSetVector<MachineBasicBlock *, 8> Succs(MBB->succ_begin(),
                                               MBB->succ_end());

  SmallVector<MachineBasicBlock *, 8> TDBBs;
  SmallVector<MachineInstr *, 16> Copies;
  if (!tailDuplicate(IsSimple, MBB, ForcedLayoutPred,
                     TDBBs, Copies, CandidatePtr))
    return false;

  ++NumTails;

  SmallVector<MachineInstr *, 8> NewPHIs;
  MachineSSAUpdater SSAUpdate(*MF, &NewPHIs);

  // TailBB's immediate successors are now successors of those predecessors
  // which duplicated TailBB. Add the predecessors as sources to the PHI
  // instructions.
  bool isDead = MBB->pred_empty() && !MBB->hasAddressTaken();
  if (PreRegAlloc)
    updateSuccessorsPHIs(MBB, isDead, TDBBs, Succs);

  // If it is dead, remove it.
  if (isDead) {
    NumTailDupRemoved += MBB->size();
    removeDeadBlock(MBB, RemovalCallback);
    ++NumDeadBlocks;
  }

  // Update SSA form.
  if (!SSAUpdateVRs.empty()) {
    for (unsigned VReg : SSAUpdateVRs) {
      SSAUpdate.Initialize(VReg);

      // If the original definition is still around, add it as an available
      // value.
      MachineInstr *DefMI = MRI->getVRegDef(VReg);
      MachineBasicBlock *DefBB = nullptr;
      if (DefMI) {
        DefBB = DefMI->getParent();
        SSAUpdate.AddAvailableValue(DefBB, VReg);
      }

      // Add the new vregs as available values.
      DenseMap<Register, AvailableValsTy>::iterator LI =
          SSAUpdateVals.find(VReg);
      for (std::pair<MachineBasicBlock *, Register> &J : LI->second) {
        MachineBasicBlock *SrcBB = J.first;
        Register SrcReg = J.second;
        SSAUpdate.AddAvailableValue(SrcBB, SrcReg);
      }

      SmallVector<MachineOperand *> DebugUses;
      // Rewrite uses that are outside of the original def's block.
      for (MachineOperand &UseMO :
           llvm::make_early_inc_range(MRI->use_operands(VReg))) {
        MachineInstr *UseMI = UseMO.getParent();
        // Rewrite debug uses last so that they can take advantage of any
        // register mappings introduced by other users in its BB, since we
        // cannot create new register definitions specifically for the debug
        // instruction (as debug instructions should not affect CodeGen).
        if (UseMI->isDebugValue()) {
          DebugUses.push_back(&UseMO);
          continue;
        }
        if (UseMI->getParent() == DefBB && !UseMI->isPHI())
          continue;
        SSAUpdate.RewriteUse(UseMO);
      }
      for (auto *UseMO : DebugUses) {
        MachineInstr *UseMI = UseMO->getParent();
        UseMO->setReg(
            SSAUpdate.GetValueInMiddleOfBlock(UseMI->getParent(), true));
      }
    }

    SSAUpdateVRs.clear();
    SSAUpdateVals.clear();
  }

  // Eliminate some of the copies inserted by tail duplication to maintain
  // SSA form.
  for (MachineInstr *Copy : Copies) {
    if (!Copy->isCopy())
      continue;
    Register Dst = Copy->getOperand(0).getReg();
    Register Src = Copy->getOperand(1).getReg();
    if (MRI->hasOneNonDBGUse(Src) &&
        MRI->constrainRegClass(Src, MRI->getRegClass(Dst))) {
      // Copy is the only use. Do trivial copy propagation here.
      MRI->replaceRegWith(Dst, Src);
      Copy->eraseFromParent();
    }
  }

  if (NewPHIs.size())
    NumAddedPHIs += NewPHIs.size();

  if (DuplicatedPreds)
    *DuplicatedPreds = std::move(TDBBs);

  return true;
}

/// Look for small blocks that are unconditionally branched to and do not fall
/// through. Tail-duplicate their instructions into their predecessors to
/// eliminate (dynamic) branches.
bool TailDuplicator::tailDuplicateBlocks() {
  bool MadeChange = false;

  if (PreRegAlloc && TailDupVerify) {
    LLVM_DEBUG(dbgs() << "\n*** Before tail-duplicating\n");
    VerifyPHIs(*MF, true);
  }

  for (MachineBasicBlock &MBB :
       llvm::make_early_inc_range(llvm::drop_begin(*MF))) {
    if (NumTails == TailDupLimit)
      break;

    bool IsSimple = isSimpleBB(&MBB);

    if (!shouldTailDuplicate(IsSimple, MBB))
      continue;

    MadeChange |= tailDuplicateAndUpdate(IsSimple, &MBB, nullptr);
  }

  if (PreRegAlloc && TailDupVerify)
    VerifyPHIs(*MF, false);

  return MadeChange;
}

static bool isDefLiveOut(Register Reg, MachineBasicBlock *BB,
                         const MachineRegisterInfo *MRI) {
  for (MachineInstr &UseMI : MRI->use_instructions(Reg)) {
    if (UseMI.isDebugValue())
      continue;
    if (UseMI.getParent() != BB)
      return true;
  }
  return false;
}

static unsigned getPHISrcRegOpIdx(MachineInstr *MI, MachineBasicBlock *SrcBB) {
  for (unsigned i = 1, e = MI->getNumOperands(); i != e; i += 2)
    if (MI->getOperand(i + 1).getMBB() == SrcBB)
      return i;
  return 0;
}

// Remember which registers are used by phis in this block. This is
// used to determine which registers are liveout while modifying the
// block (which is why we need to copy the information).
static void getRegsUsedByPHIs(const MachineBasicBlock &BB,
                              DenseSet<Register> *UsedByPhi) {
  for (const auto &MI : BB) {
    if (!MI.isPHI())
      break;
    for (unsigned i = 1, e = MI.getNumOperands(); i != e; i += 2) {
      Register SrcReg = MI.getOperand(i).getReg();
      UsedByPhi->insert(SrcReg);
    }
  }
}

/// Add a definition and source virtual registers pair for SSA update.
void TailDuplicator::addSSAUpdateEntry(Register OrigReg, Register NewReg,
                                       MachineBasicBlock *BB) {
  DenseMap<Register, AvailableValsTy>::iterator LI =
      SSAUpdateVals.find(OrigReg);
  if (LI != SSAUpdateVals.end())
    LI->second.push_back(std::make_pair(BB, NewReg));
  else {
    AvailableValsTy Vals;
    Vals.push_back(std::make_pair(BB, NewReg));
    SSAUpdateVals.insert(std::make_pair(OrigReg, Vals));
    SSAUpdateVRs.push_back(OrigReg);
  }
}

/// Process PHI node in TailBB by turning it into a copy in PredBB. Remember the
/// source register that's contributed by PredBB and update SSA update map.
void TailDuplicator::processPHI(
    MachineInstr *MI, MachineBasicBlock *TailBB, MachineBasicBlock *PredBB,
    DenseMap<Register, RegSubRegPair> &LocalVRMap,
    SmallVectorImpl<std::pair<Register, RegSubRegPair>> &Copies,
    const DenseSet<Register> &RegsUsedByPhi, bool Remove) {
  Register DefReg = MI->getOperand(0).getReg();
  unsigned SrcOpIdx = getPHISrcRegOpIdx(MI, PredBB);
  assert(SrcOpIdx && "Unable to find matching PHI source?");
  Register SrcReg = MI->getOperand(SrcOpIdx).getReg();
  unsigned SrcSubReg = MI->getOperand(SrcOpIdx).getSubReg();
  const TargetRegisterClass *RC = MRI->getRegClass(DefReg);
  LocalVRMap.insert(std::make_pair(DefReg, RegSubRegPair(SrcReg, SrcSubReg)));

  // Insert a copy from source to the end of the block. The def register is the
  // available value liveout of the block.
  Register NewDef = MRI->createVirtualRegister(RC);
  Copies.push_back(std::make_pair(NewDef, RegSubRegPair(SrcReg, SrcSubReg)));
  if (isDefLiveOut(DefReg, TailBB, MRI) || RegsUsedByPhi.count(DefReg))
    addSSAUpdateEntry(DefReg, NewDef, PredBB);

  if (!Remove)
    return;

  // Remove PredBB from the PHI node.
  MI->removeOperand(SrcOpIdx + 1);
  MI->removeOperand(SrcOpIdx);
  if (MI->getNumOperands() == 1 && !TailBB->hasAddressTaken())
    MI->eraseFromParent();
  else if (MI->getNumOperands() == 1)
    MI->setDesc(TII->get(TargetOpcode::IMPLICIT_DEF));
}

/// Duplicate a TailBB instruction to PredBB and update
/// the source operands due to earlier PHI translation.
void TailDuplicator::duplicateInstruction(
    MachineInstr *MI, MachineBasicBlock *TailBB, MachineBasicBlock *PredBB,
    DenseMap<Register, RegSubRegPair> &LocalVRMap,
    const DenseSet<Register> &UsedByPhi) {
  // Allow duplication of CFI instructions.
  if (MI->isCFIInstruction()) {
    BuildMI(*PredBB, PredBB->end(), PredBB->findDebugLoc(PredBB->begin()),
            TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(MI->getOperand(0).getCFIIndex())
        .setMIFlags(MI->getFlags());
    return;
  }
  MachineInstr &NewMI = TII->duplicate(*PredBB, PredBB->end(), *MI);
  if (PreRegAlloc) {
    for (unsigned i = 0, e = NewMI.getNumOperands(); i != e; ++i) {
      MachineOperand &MO = NewMI.getOperand(i);
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      if (!Reg.isVirtual())
        continue;
      if (MO.isDef()) {
        const TargetRegisterClass *RC = MRI->getRegClass(Reg);
        Register NewReg = MRI->createVirtualRegister(RC);
        MO.setReg(NewReg);
        LocalVRMap.insert(std::make_pair(Reg, RegSubRegPair(NewReg, 0)));
        if (isDefLiveOut(Reg, TailBB, MRI) || UsedByPhi.count(Reg))
          addSSAUpdateEntry(Reg, NewReg, PredBB);
      } else {
        auto VI = LocalVRMap.find(Reg);
        if (VI != LocalVRMap.end()) {
          // Need to make sure that the register class of the mapped register
          // will satisfy the constraints of the class of the register being
          // replaced.
          auto *OrigRC = MRI->getRegClass(Reg);
          auto *MappedRC = MRI->getRegClass(VI->second.Reg);
          const TargetRegisterClass *ConstrRC;
          if (VI->second.SubReg != 0) {
            ConstrRC = TRI->getMatchingSuperRegClass(MappedRC, OrigRC,
                                                     VI->second.SubReg);
            if (ConstrRC) {
              // The actual constraining (as in "find appropriate new class")
              // is done by getMatchingSuperRegClass, so now we only need to
              // change the class of the mapped register.
              MRI->setRegClass(VI->second.Reg, ConstrRC);
            }
          } else {
            // For mapped registers that do not have sub-registers, simply
            // restrict their class to match the original one.

            // We don't want debug instructions affecting the resulting code so
            // if we're cloning a debug instruction then just use MappedRC
            // rather than constraining the register class further.
            ConstrRC = NewMI.isDebugInstr()
                           ? MappedRC
                           : MRI->constrainRegClass(VI->second.Reg, OrigRC);
          }

          if (ConstrRC) {
            // If the class constraining succeeded, we can simply replace
            // the old register with the mapped one.
            MO.setReg(VI->second.Reg);
            // We have Reg -> VI.Reg:VI.SubReg, so if Reg is used with a
            // sub-register, we need to compose the sub-register indices.
            MO.setSubReg(
                TRI->composeSubRegIndices(VI->second.SubReg, MO.getSubReg()));
          } else {
            // The direct replacement is not possible, due to failing register
            // class constraints. An explicit COPY is necessary. Create one
            // that can be reused.
            Register NewReg = MRI->createVirtualRegister(OrigRC);
            BuildMI(*PredBB, NewMI, NewMI.getDebugLoc(),
                    TII->get(TargetOpcode::COPY), NewReg)
                .addReg(VI->second.Reg, 0, VI->second.SubReg);
            LocalVRMap.erase(VI);
            LocalVRMap.insert(std::make_pair(Reg, RegSubRegPair(NewReg, 0)));
            MO.setReg(NewReg);
            // The composed VI.Reg:VI.SubReg is replaced with NewReg, which
            // is equivalent to the whole register Reg. Hence, Reg:subreg
            // is same as NewReg:subreg, so keep the sub-register index
            // unchanged.
          }
          // Clear any kill flags from this operand.  The new register could
          // have uses after this one, so kills are not valid here.
          MO.setIsKill(false);
        }
      }
    }
  }
}

/// After FromBB is tail duplicated into its predecessor blocks, the successors
/// have gained new predecessors. Update the PHI instructions in them
/// accordingly.
void TailDuplicator::updateSuccessorsPHIs(
    MachineBasicBlock *FromBB, bool isDead,
    SmallVectorImpl<MachineBasicBlock *> &TDBBs,
    SmallSetVector<MachineBasicBlock *, 8> &Succs) {
  for (MachineBasicBlock *SuccBB : Succs) {
    for (MachineInstr &MI : *SuccBB) {
      if (!MI.isPHI())
        break;
      MachineInstrBuilder MIB(*FromBB->getParent(), MI);
      unsigned Idx = 0;
      for (unsigned i = 1, e = MI.getNumOperands(); i != e; i += 2) {
        MachineOperand &MO = MI.getOperand(i + 1);
        if (MO.getMBB() == FromBB) {
          Idx = i;
          break;
        }
      }

      assert(Idx != 0);
      MachineOperand &MO0 = MI.getOperand(Idx);
      Register Reg = MO0.getReg();
      if (isDead) {
        // Folded into the previous BB.
        // There could be duplicate phi source entries. FIXME: Should sdisel
        // or earlier pass fixed this?
        for (unsigned i = MI.getNumOperands() - 2; i != Idx; i -= 2) {
          MachineOperand &MO = MI.getOperand(i + 1);
          if (MO.getMBB() == FromBB) {
            MI.removeOperand(i + 1);
            MI.removeOperand(i);
          }
        }
      } else
        Idx = 0;

      // If Idx is set, the operands at Idx and Idx+1 must be removed.
      // We reuse the location to avoid expensive removeOperand calls.

      DenseMap<Register, AvailableValsTy>::iterator LI =
          SSAUpdateVals.find(Reg);
      if (LI != SSAUpdateVals.end()) {
        // This register is defined in the tail block.
        for (const std::pair<MachineBasicBlock *, Register> &J : LI->second) {
          MachineBasicBlock *SrcBB = J.first;
          // If we didn't duplicate a bb into a particular predecessor, we
          // might still have added an entry to SSAUpdateVals to correcly
          // recompute SSA. If that case, avoid adding a dummy extra argument
          // this PHI.
          if (!SrcBB->isSuccessor(SuccBB))
            continue;

          Register SrcReg = J.second;
          if (Idx != 0) {
            MI.getOperand(Idx).setReg(SrcReg);
            MI.getOperand(Idx + 1).setMBB(SrcBB);
            Idx = 0;
          } else {
            MIB.addReg(SrcReg).addMBB(SrcBB);
          }
        }
      } else {
        // Live in tail block, must also be live in predecessors.
        for (MachineBasicBlock *SrcBB : TDBBs) {
          if (Idx != 0) {
            MI.getOperand(Idx).setReg(Reg);
            MI.getOperand(Idx + 1).setMBB(SrcBB);
            Idx = 0;
          } else {
            MIB.addReg(Reg).addMBB(SrcBB);
          }
        }
      }
      if (Idx != 0) {
        MI.removeOperand(Idx + 1);
        MI.removeOperand(Idx);
      }
    }
  }
}

/// Determine if it is profitable to duplicate this block.
bool TailDuplicator::shouldTailDuplicate(bool IsSimple,
                                         MachineBasicBlock &TailBB) {
  // When doing tail-duplication during layout, the block ordering is in flux,
  // so canFallThrough returns a result based on incorrect information and
  // should just be ignored.
  if (!LayoutMode && TailBB.canFallThrough())
    return false;

  // Don't try to tail-duplicate single-block loops.
  if (TailBB.isSuccessor(&TailBB))
    return false;

  // Duplicating a BB which has both multiple predecessors and successors will
  // result in a complex CFG and also may cause huge amount of PHI nodes. If we
  // want to remove this limitation, we have to address
  // https://github.com/llvm/llvm-project/issues/78578.
  if (TailBB.pred_size() > TailDupPredSize &&
      TailBB.succ_size() > TailDupSuccSize)
    return false;

  // Set the limit on the cost to duplicate. When optimizing for size,
  // duplicate only one, because one branch instruction can be eliminated to
  // compensate for the duplication.
  unsigned MaxDuplicateCount;
  bool OptForSize = MF->getFunction().hasOptSize() ||
                    llvm::shouldOptimizeForSize(&TailBB, PSI, MBFI);
  if (TailDupSize == 0)
    MaxDuplicateCount = TailDuplicateSize;
  else
    MaxDuplicateCount = TailDupSize;
  if (OptForSize)
    MaxDuplicateCount = 1;

  // If the block to be duplicated ends in an unanalyzable fallthrough, don't
  // duplicate it.
  // A similar check is necessary in MachineBlockPlacement to make sure pairs of
  // blocks with unanalyzable fallthrough get layed out contiguously.
  MachineBasicBlock *PredTBB = nullptr, *PredFBB = nullptr;
  SmallVector<MachineOperand, 4> PredCond;
  if (TII->analyzeBranch(TailBB, PredTBB, PredFBB, PredCond) &&
      TailBB.canFallThrough())
    return false;

  // If the target has hardware branch prediction that can handle indirect
  // branches, duplicating them can often make them predictable when there
  // are common paths through the code.  The limit needs to be high enough
  // to allow undoing the effects of tail merging and other optimizations
  // that rearrange the predecessors of the indirect branch.

  bool HasIndirectbr = false;
  if (!TailBB.empty())
    HasIndirectbr = TailBB.back().isIndirectBranch();

  if (HasIndirectbr && PreRegAlloc)
    MaxDuplicateCount = TailDupIndirectBranchSize;

  // Check the instructions in the block to determine whether tail-duplication
  // is invalid or unlikely to be profitable.
  unsigned InstrCount = 0;
  for (MachineInstr &MI : TailBB) {
    // Non-duplicable things shouldn't be tail-duplicated.
    // CFI instructions are marked as non-duplicable, because Darwin compact
    // unwind info emission can't handle multiple prologue setups. In case of
    // DWARF, allow them be duplicated, so that their existence doesn't prevent
    // tail duplication of some basic blocks, that would be duplicated otherwise.
    if (MI.isNotDuplicable() &&
        (TailBB.getParent()->getTarget().getTargetTriple().isOSDarwin() ||
        !MI.isCFIInstruction()))
      return false;

    // Convergent instructions can be duplicated only if doing so doesn't add
    // new control dependencies, which is what we're going to do here.
    if (MI.isConvergent())
      return false;

    // Do not duplicate 'return' instructions if this is a pre-regalloc run.
    // A return may expand into a lot more instructions (e.g. reload of callee
    // saved registers) after PEI.
    if (PreRegAlloc && MI.isReturn())
      return false;

    // Avoid duplicating calls before register allocation. Calls presents a
    // barrier to register allocation so duplicating them may end up increasing
    // spills.
    if (PreRegAlloc && MI.isCall())
      return false;

    // TailDuplicator::appendCopies will erroneously place COPYs after
    // INLINEASM_BR instructions after 4b0aa5724fea, which demonstrates the same
    // bug that was fixed in f7a53d82c090.
    // FIXME: Use findPHICopyInsertPoint() to find the correct insertion point
    //        for the COPY when replacing PHIs.
    if (MI.getOpcode() == TargetOpcode::INLINEASM_BR)
      return false;

    if (MI.isBundle())
      InstrCount += MI.getBundleSize();
    else if (!MI.isPHI() && !MI.isMetaInstruction())
      InstrCount += 1;

    if (InstrCount > MaxDuplicateCount)
      return false;
  }

  // Check if any of the successors of TailBB has a PHI node in which the
  // value corresponding to TailBB uses a subregister.
  // If a phi node uses a register paired with a subregister, the actual
  // "value type" of the phi may differ from the type of the register without
  // any subregisters. Due to a bug, tail duplication may add a new operand
  // without a necessary subregister, producing an invalid code. This is
  // demonstrated by test/CodeGen/Hexagon/tail-dup-subreg-abort.ll.
  // Disable tail duplication for this case for now, until the problem is
  // fixed.
  for (auto *SB : TailBB.successors()) {
    for (auto &I : *SB) {
      if (!I.isPHI())
        break;
      unsigned Idx = getPHISrcRegOpIdx(&I, &TailBB);
      assert(Idx != 0);
      MachineOperand &PU = I.getOperand(Idx);
      if (PU.getSubReg() != 0)
        return false;
    }
  }

  if (HasIndirectbr && PreRegAlloc)
    return true;

  if (IsSimple)
    return true;

  if (!PreRegAlloc)
    return true;

  return canCompletelyDuplicateBB(TailBB);
}

/// True if this BB has only one unconditional jump.
bool TailDuplicator::isSimpleBB(MachineBasicBlock *TailBB) {
  if (TailBB->succ_size() != 1)
    return false;
  if (TailBB->pred_empty())
    return false;
  MachineBasicBlock::iterator I = TailBB->getFirstNonDebugInstr(true);
  if (I == TailBB->end())
    return true;
  return I->isUnconditionalBranch();
}

static bool bothUsedInPHI(const MachineBasicBlock &A,
                          const SmallPtrSet<MachineBasicBlock *, 8> &SuccsB) {
  for (MachineBasicBlock *BB : A.successors())
    if (SuccsB.count(BB) && !BB->empty() && BB->begin()->isPHI())
      return true;

  return false;
}

bool TailDuplicator::canCompletelyDuplicateBB(MachineBasicBlock &BB) {
  for (MachineBasicBlock *PredBB : BB.predecessors()) {
    if (PredBB->succ_size() > 1)
      return false;

    MachineBasicBlock *PredTBB = nullptr, *PredFBB = nullptr;
    SmallVector<MachineOperand, 4> PredCond;
    if (TII->analyzeBranch(*PredBB, PredTBB, PredFBB, PredCond))
      return false;

    if (!PredCond.empty())
      return false;
  }
  return true;
}

bool TailDuplicator::duplicateSimpleBB(
    MachineBasicBlock *TailBB, SmallVectorImpl<MachineBasicBlock *> &TDBBs,
    const DenseSet<Register> &UsedByPhi) {
  SmallPtrSet<MachineBasicBlock *, 8> Succs(TailBB->succ_begin(),
                                            TailBB->succ_end());
  SmallVector<MachineBasicBlock *, 8> Preds(TailBB->predecessors());
  bool Changed = false;
  for (MachineBasicBlock *PredBB : Preds) {
    if (PredBB->hasEHPadSuccessor() || PredBB->mayHaveInlineAsmBr())
      continue;

    if (bothUsedInPHI(*PredBB, Succs))
      continue;

    MachineBasicBlock *PredTBB = nullptr, *PredFBB = nullptr;
    SmallVector<MachineOperand, 4> PredCond;
    if (TII->analyzeBranch(*PredBB, PredTBB, PredFBB, PredCond))
      continue;

    Changed = true;
    LLVM_DEBUG(dbgs() << "\nTail-duplicating into PredBB: " << *PredBB
                      << "From simple Succ: " << *TailBB);

    MachineBasicBlock *NewTarget = *TailBB->succ_begin();
    MachineBasicBlock *NextBB = PredBB->getNextNode();

    // Make PredFBB explicit.
    if (PredCond.empty())
      PredFBB = PredTBB;

    // Make fall through explicit.
    if (!PredTBB)
      PredTBB = NextBB;
    if (!PredFBB)
      PredFBB = NextBB;

    // Redirect
    if (PredFBB == TailBB)
      PredFBB = NewTarget;
    if (PredTBB == TailBB)
      PredTBB = NewTarget;

    // Make the branch unconditional if possible
    if (PredTBB == PredFBB) {
      PredCond.clear();
      PredFBB = nullptr;
    }

    // Avoid adding fall through branches.
    if (PredFBB == NextBB)
      PredFBB = nullptr;
    if (PredTBB == NextBB && PredFBB == nullptr)
      PredTBB = nullptr;

    auto DL = PredBB->findBranchDebugLoc();
    TII->removeBranch(*PredBB);

    if (!PredBB->isSuccessor(NewTarget))
      PredBB->replaceSuccessor(TailBB, NewTarget);
    else {
      PredBB->removeSuccessor(TailBB, true);
      assert(PredBB->succ_size() <= 1);
    }

    if (PredTBB)
      TII->insertBranch(*PredBB, PredTBB, PredFBB, PredCond, DL);

    TDBBs.push_back(PredBB);
  }
  return Changed;
}

bool TailDuplicator::canTailDuplicate(MachineBasicBlock *TailBB,
                                      MachineBasicBlock *PredBB) {
  // EH edges are ignored by analyzeBranch.
  if (PredBB->succ_size() > 1)
    return false;

  MachineBasicBlock *PredTBB = nullptr, *PredFBB = nullptr;
  SmallVector<MachineOperand, 4> PredCond;
  if (TII->analyzeBranch(*PredBB, PredTBB, PredFBB, PredCond))
    return false;
  if (!PredCond.empty())
    return false;
  // FIXME: This is overly conservative; it may be ok to relax this in the
  // future under more specific conditions. If TailBB is an INLINEASM_BR
  // indirect target, we need to see if the edge from PredBB to TailBB is from
  // an INLINEASM_BR in PredBB, and then also if that edge was from the
  // indirect target list, fallthrough/default target, or potentially both. If
  // it's both, TailDuplicator::tailDuplicate will remove the edge, corrupting
  // the successor list in PredBB and predecessor list in TailBB.
  if (TailBB->isInlineAsmBrIndirectTarget())
    return false;
  return true;
}

/// If it is profitable, duplicate TailBB's contents in each
/// of its predecessors.
/// \p IsSimple result of isSimpleBB
/// \p TailBB   Block to be duplicated.
/// \p ForcedLayoutPred  When non-null, use this block as the layout predecessor
///                      instead of the previous block in MF's order.
/// \p TDBBs             A vector to keep track of all blocks tail-duplicated
///                      into.
/// \p Copies            A vector of copy instructions inserted. Used later to
///                      walk all the inserted copies and remove redundant ones.
bool TailDuplicator::tailDuplicate(bool IsSimple, MachineBasicBlock *TailBB,
                          MachineBasicBlock *ForcedLayoutPred,
                          SmallVectorImpl<MachineBasicBlock *> &TDBBs,
                          SmallVectorImpl<MachineInstr *> &Copies,
                          SmallVectorImpl<MachineBasicBlock *> *CandidatePtr) {
  LLVM_DEBUG(dbgs() << "\n*** Tail-duplicating " << printMBBReference(*TailBB)
                    << '\n');

  bool ShouldUpdateTerminators = TailBB->canFallThrough();

  DenseSet<Register> UsedByPhi;
  getRegsUsedByPHIs(*TailBB, &UsedByPhi);

  if (IsSimple)
    return duplicateSimpleBB(TailBB, TDBBs, UsedByPhi);

  // Iterate through all the unique predecessors and tail-duplicate this
  // block into them, if possible. Copying the list ahead of time also
  // avoids trouble with the predecessor list reallocating.
  bool Changed = false;
  SmallSetVector<MachineBasicBlock *, 8> Preds;
  if (CandidatePtr)
    Preds.insert(CandidatePtr->begin(), CandidatePtr->end());
  else
    Preds.insert(TailBB->pred_begin(), TailBB->pred_end());

  for (MachineBasicBlock *PredBB : Preds) {
    assert(TailBB != PredBB &&
           "Single-block loop should have been rejected earlier!");

    if (!canTailDuplicate(TailBB, PredBB))
      continue;

    // Don't duplicate into a fall-through predecessor (at least for now).
    // If profile is available, findDuplicateCandidates can choose better
    // fall-through predecessor.
    if (!(MF->getFunction().hasProfileData() && LayoutMode)) {
      bool IsLayoutSuccessor = false;
      if (ForcedLayoutPred)
        IsLayoutSuccessor = (ForcedLayoutPred == PredBB);
      else if (PredBB->isLayoutSuccessor(TailBB) && PredBB->canFallThrough())
        IsLayoutSuccessor = true;
      if (IsLayoutSuccessor)
        continue;
    }

    LLVM_DEBUG(dbgs() << "\nTail-duplicating into PredBB: " << *PredBB
                      << "From Succ: " << *TailBB);

    TDBBs.push_back(PredBB);

    // Remove PredBB's unconditional branch.
    TII->removeBranch(*PredBB);

    // Clone the contents of TailBB into PredBB.
    DenseMap<Register, RegSubRegPair> LocalVRMap;
    SmallVector<std::pair<Register, RegSubRegPair>, 4> CopyInfos;
    for (MachineInstr &MI : llvm::make_early_inc_range(*TailBB)) {
      if (MI.isPHI()) {
        // Replace the uses of the def of the PHI with the register coming
        // from PredBB.
        processPHI(&MI, TailBB, PredBB, LocalVRMap, CopyInfos, UsedByPhi, true);
      } else {
        // Replace def of virtual registers with new registers, and update
        // uses with PHI source register or the new registers.
        duplicateInstruction(&MI, TailBB, PredBB, LocalVRMap, UsedByPhi);
      }
    }
    appendCopies(PredBB, CopyInfos, Copies);

    NumTailDupAdded += TailBB->size() - 1; // subtract one for removed branch

    // Update the CFG.
    PredBB->removeSuccessor(PredBB->succ_begin());
    assert(PredBB->succ_empty() &&
           "TailDuplicate called on block with multiple successors!");
    for (MachineBasicBlock *Succ : TailBB->successors())
      PredBB->addSuccessor(Succ, MBPI->getEdgeProbability(TailBB, Succ));

    // Update branches in pred to jump to tail's layout successor if needed.
    if (ShouldUpdateTerminators)
      PredBB->updateTerminator(TailBB->getNextNode());

    Changed = true;
    ++NumTailDups;
  }

  // If TailBB was duplicated into all its predecessors except for the prior
  // block, which falls through unconditionally, move the contents of this
  // block into the prior block.
  MachineBasicBlock *PrevBB = ForcedLayoutPred;
  if (!PrevBB)
    PrevBB = &*std::prev(TailBB->getIterator());
  MachineBasicBlock *PriorTBB = nullptr, *PriorFBB = nullptr;
  SmallVector<MachineOperand, 4> PriorCond;
  // This has to check PrevBB->succ_size() because EH edges are ignored by
  // analyzeBranch.
  if (PrevBB->succ_size() == 1 &&
      // Layout preds are not always CFG preds. Check.
      *PrevBB->succ_begin() == TailBB &&
      !TII->analyzeBranch(*PrevBB, PriorTBB, PriorFBB, PriorCond) &&
      PriorCond.empty() &&
      (!PriorTBB || PriorTBB == TailBB) &&
      TailBB->pred_size() == 1 &&
      !TailBB->hasAddressTaken()) {
    LLVM_DEBUG(dbgs() << "\nMerging into block: " << *PrevBB
                      << "From MBB: " << *TailBB);
    // There may be a branch to the layout successor. This is unlikely but it
    // happens. The correct thing to do is to remove the branch before
    // duplicating the instructions in all cases.
    bool RemovedBranches = TII->removeBranch(*PrevBB) != 0;

    // If there are still tail instructions, abort the merge
    if (PrevBB->getFirstTerminator() == PrevBB->end()) {
      if (PreRegAlloc) {
        DenseMap<Register, RegSubRegPair> LocalVRMap;
        SmallVector<std::pair<Register, RegSubRegPair>, 4> CopyInfos;
        MachineBasicBlock::iterator I = TailBB->begin();
        // Process PHI instructions first.
        while (I != TailBB->end() && I->isPHI()) {
          // Replace the uses of the def of the PHI with the register coming
          // from PredBB.
          MachineInstr *MI = &*I++;
          processPHI(MI, TailBB, PrevBB, LocalVRMap, CopyInfos, UsedByPhi,
                     true);
        }

        // Now copy the non-PHI instructions.
        while (I != TailBB->end()) {
          // Replace def of virtual registers with new registers, and update
          // uses with PHI source register or the new registers.
          MachineInstr *MI = &*I++;
          assert(!MI->isBundle() && "Not expecting bundles before regalloc!");
          duplicateInstruction(MI, TailBB, PrevBB, LocalVRMap, UsedByPhi);
          MI->eraseFromParent();
        }
        appendCopies(PrevBB, CopyInfos, Copies);
      } else {
        TII->removeBranch(*PrevBB);
        // No PHIs to worry about, just splice the instructions over.
        PrevBB->splice(PrevBB->end(), TailBB, TailBB->begin(), TailBB->end());
      }
      PrevBB->removeSuccessor(PrevBB->succ_begin());
      assert(PrevBB->succ_empty());
      PrevBB->transferSuccessors(TailBB);

      // Update branches in PrevBB based on Tail's layout successor.
      if (ShouldUpdateTerminators)
        PrevBB->updateTerminator(TailBB->getNextNode());

      TDBBs.push_back(PrevBB);
      Changed = true;
    } else {
      LLVM_DEBUG(dbgs() << "Abort merging blocks, the predecessor still "
                           "contains terminator instructions");
      // Return early if no changes were made
      if (!Changed)
        return RemovedBranches;
    }
    Changed |= RemovedBranches;
  }

  // If this is after register allocation, there are no phis to fix.
  if (!PreRegAlloc)
    return Changed;

  // If we made no changes so far, we are safe.
  if (!Changed)
    return Changed;

  // Handle the nasty case in that we duplicated a block that is part of a loop
  // into some but not all of its predecessors. For example:
  //    1 -> 2 <-> 3                 |
  //          \                      |
  //           \---> rest            |
  // if we duplicate 2 into 1 but not into 3, we end up with
  // 12 -> 3 <-> 2 -> rest           |
  //   \             /               |
  //    \----->-----/                |
  // If there was a "var = phi(1, 3)" in 2, it has to be ultimately replaced
  // with a phi in 3 (which now dominates 2).
  // What we do here is introduce a copy in 3 of the register defined by the
  // phi, just like when we are duplicating 2 into 3, but we don't copy any
  // real instructions or remove the 3 -> 2 edge from the phi in 2.
  for (MachineBasicBlock *PredBB : Preds) {
    if (is_contained(TDBBs, PredBB))
      continue;

    // EH edges
    if (PredBB->succ_size() != 1)
      continue;

    DenseMap<Register, RegSubRegPair> LocalVRMap;
    SmallVector<std::pair<Register, RegSubRegPair>, 4> CopyInfos;
    // Process PHI instructions first.
    for (MachineInstr &MI : make_early_inc_range(TailBB->phis())) {
      // Replace the uses of the def of the PHI with the register coming
      // from PredBB.
      processPHI(&MI, TailBB, PredBB, LocalVRMap, CopyInfos, UsedByPhi, false);
    }
    appendCopies(PredBB, CopyInfos, Copies);
  }

  return Changed;
}

/// At the end of the block \p MBB generate COPY instructions between registers
/// described by \p CopyInfos. Append resulting instructions to \p Copies.
void TailDuplicator::appendCopies(MachineBasicBlock *MBB,
      SmallVectorImpl<std::pair<Register, RegSubRegPair>> &CopyInfos,
      SmallVectorImpl<MachineInstr*> &Copies) {
  MachineBasicBlock::iterator Loc = MBB->getFirstTerminator();
  const MCInstrDesc &CopyD = TII->get(TargetOpcode::COPY);
  for (auto &CI : CopyInfos) {
    auto C = BuildMI(*MBB, Loc, DebugLoc(), CopyD, CI.first)
                .addReg(CI.second.Reg, 0, CI.second.SubReg);
    Copies.push_back(C);
  }
}

/// Remove the specified dead machine basic block from the function, updating
/// the CFG.
void TailDuplicator::removeDeadBlock(
    MachineBasicBlock *MBB,
    function_ref<void(MachineBasicBlock *)> *RemovalCallback) {
  assert(MBB->pred_empty() && "MBB must be dead!");
  LLVM_DEBUG(dbgs() << "\nRemoving MBB: " << *MBB);

  MachineFunction *MF = MBB->getParent();
  // Update the call site info.
  for (const MachineInstr &MI : *MBB)
    if (MI.shouldUpdateCallSiteInfo())
      MF->eraseCallSiteInfo(&MI);

  if (RemovalCallback)
    (*RemovalCallback)(MBB);

  // Remove all successors.
  while (!MBB->succ_empty())
    MBB->removeSuccessor(MBB->succ_end() - 1);

  // Remove the block.
  MBB->eraseFromParent();
}
