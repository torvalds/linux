//===- PhiElimination.cpp - Eliminate PHI nodes by inserting copies -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass eliminates machine instruction PHI nodes by inserting copy
// instructions.  This destroys SSA information, but is the desired input for
// some register allocators.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/PHIElimination.h"
#include "PHIEliminationUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "phi-node-elimination"

static cl::opt<bool>
    DisableEdgeSplitting("disable-phi-elim-edge-splitting", cl::init(false),
                         cl::Hidden,
                         cl::desc("Disable critical edge splitting "
                                  "during PHI elimination"));

static cl::opt<bool>
    SplitAllCriticalEdges("phi-elim-split-all-critical-edges", cl::init(false),
                          cl::Hidden,
                          cl::desc("Split all critical edges during "
                                   "PHI elimination"));

static cl::opt<bool> NoPhiElimLiveOutEarlyExit(
    "no-phi-elim-live-out-early-exit", cl::init(false), cl::Hidden,
    cl::desc("Do not use an early exit if isLiveOutPastPHIs returns true."));

namespace {

class PHIEliminationImpl {
  MachineRegisterInfo *MRI = nullptr; // Machine register information
  LiveVariables *LV = nullptr;
  LiveIntervals *LIS = nullptr;
  MachineLoopInfo *MLI = nullptr;
  MachineDominatorTree *MDT = nullptr;

  /// EliminatePHINodes - Eliminate phi nodes by inserting copy instructions
  /// in predecessor basic blocks.
  bool EliminatePHINodes(MachineFunction &MF, MachineBasicBlock &MBB);

  void LowerPHINode(MachineBasicBlock &MBB,
                    MachineBasicBlock::iterator LastPHIIt,
                    bool AllEdgesCritical);

  /// analyzePHINodes - Gather information about the PHI nodes in
  /// here. In particular, we want to map the number of uses of a virtual
  /// register which is used in a PHI node. We map that to the BB the
  /// vreg is coming from. This is used later to determine when the vreg
  /// is killed in the BB.
  void analyzePHINodes(const MachineFunction &MF);

  /// Split critical edges where necessary for good coalescer performance.
  bool SplitPHIEdges(MachineFunction &MF, MachineBasicBlock &MBB,
                     MachineLoopInfo *MLI,
                     std::vector<SparseBitVector<>> *LiveInSets);

  // These functions are temporary abstractions around LiveVariables and
  // LiveIntervals, so they can go away when LiveVariables does.
  bool isLiveIn(Register Reg, const MachineBasicBlock *MBB);
  bool isLiveOutPastPHIs(Register Reg, const MachineBasicBlock *MBB);

  using BBVRegPair = std::pair<unsigned, Register>;
  using VRegPHIUse = DenseMap<BBVRegPair, unsigned>;

  // Count the number of non-undef PHI uses of each register in each BB.
  VRegPHIUse VRegPHIUseCount;

  // Defs of PHI sources which are implicit_def.
  SmallPtrSet<MachineInstr *, 4> ImpDefs;

  // Map reusable lowered PHI node -> incoming join register.
  using LoweredPHIMap =
      DenseMap<MachineInstr *, unsigned, MachineInstrExpressionTrait>;
  LoweredPHIMap LoweredPHIs;

  MachineFunctionPass *P = nullptr;
  MachineFunctionAnalysisManager *MFAM = nullptr;

public:
  PHIEliminationImpl(MachineFunctionPass *P) : P(P) {
    auto *LVWrapper = P->getAnalysisIfAvailable<LiveVariablesWrapperPass>();
    auto *LISWrapper = P->getAnalysisIfAvailable<LiveIntervalsWrapperPass>();
    auto *MLIWrapper = P->getAnalysisIfAvailable<MachineLoopInfoWrapperPass>();
    auto *MDTWrapper =
        P->getAnalysisIfAvailable<MachineDominatorTreeWrapperPass>();
    LV = LVWrapper ? &LVWrapper->getLV() : nullptr;
    LIS = LISWrapper ? &LISWrapper->getLIS() : nullptr;
    MLI = MLIWrapper ? &MLIWrapper->getLI() : nullptr;
    MDT = MDTWrapper ? &MDTWrapper->getDomTree() : nullptr;
  }

  PHIEliminationImpl(MachineFunction &MF, MachineFunctionAnalysisManager &AM)
      : LV(AM.getCachedResult<LiveVariablesAnalysis>(MF)),
        LIS(AM.getCachedResult<LiveIntervalsAnalysis>(MF)),
        MLI(AM.getCachedResult<MachineLoopAnalysis>(MF)),
        MDT(AM.getCachedResult<MachineDominatorTreeAnalysis>(MF)), MFAM(&AM) {}

  bool run(MachineFunction &MF);
};

class PHIElimination : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid

  PHIElimination() : MachineFunctionPass(ID) {
    initializePHIEliminationPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    PHIEliminationImpl Impl(this);
    return Impl.run(MF);
  }

  MachineFunctionProperties getSetProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

} // end anonymous namespace

PreservedAnalyses
PHIEliminationPass::run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM) {
  PHIEliminationImpl Impl(MF, MFAM);
  bool Changed = Impl.run(MF);
  if (!Changed)
    return PreservedAnalyses::all();
  auto PA = getMachineFunctionPassPreservedAnalyses();
  PA.preserve<LiveIntervalsAnalysis>();
  PA.preserve<LiveVariablesAnalysis>();
  PA.preserve<SlotIndexesAnalysis>();
  PA.preserve<MachineDominatorTreeAnalysis>();
  PA.preserve<MachineLoopAnalysis>();
  return PA;
}

STATISTIC(NumLowered, "Number of phis lowered");
STATISTIC(NumCriticalEdgesSplit, "Number of critical edges split");
STATISTIC(NumReused, "Number of reused lowered phis");

char PHIElimination::ID = 0;

char &llvm::PHIEliminationID = PHIElimination::ID;

INITIALIZE_PASS_BEGIN(PHIElimination, DEBUG_TYPE,
                      "Eliminate PHI nodes for register allocation", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(LiveVariablesWrapperPass)
INITIALIZE_PASS_END(PHIElimination, DEBUG_TYPE,
                    "Eliminate PHI nodes for register allocation", false, false)

void PHIElimination::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addUsedIfAvailable<LiveVariablesWrapperPass>();
  AU.addPreserved<LiveVariablesWrapperPass>();
  AU.addPreserved<SlotIndexesWrapperPass>();
  AU.addPreserved<LiveIntervalsWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool PHIEliminationImpl::run(MachineFunction &MF) {
  MRI = &MF.getRegInfo();

  bool Changed = false;

  // Split critical edges to help the coalescer.
  if (!DisableEdgeSplitting && (LV || LIS)) {
    // A set of live-in regs for each MBB which is used to update LV
    // efficiently also with large functions.
    std::vector<SparseBitVector<>> LiveInSets;
    if (LV) {
      LiveInSets.resize(MF.size());
      for (unsigned Index = 0, e = MRI->getNumVirtRegs(); Index != e; ++Index) {
        // Set the bit for this register for each MBB where it is
        // live-through or live-in (killed).
        Register VirtReg = Register::index2VirtReg(Index);
        MachineInstr *DefMI = MRI->getVRegDef(VirtReg);
        if (!DefMI)
          continue;
        LiveVariables::VarInfo &VI = LV->getVarInfo(VirtReg);
        SparseBitVector<>::iterator AliveBlockItr = VI.AliveBlocks.begin();
        SparseBitVector<>::iterator EndItr = VI.AliveBlocks.end();
        while (AliveBlockItr != EndItr) {
          unsigned BlockNum = *(AliveBlockItr++);
          LiveInSets[BlockNum].set(Index);
        }
        // The register is live into an MBB in which it is killed but not
        // defined. See comment for VarInfo in LiveVariables.h.
        MachineBasicBlock *DefMBB = DefMI->getParent();
        if (VI.Kills.size() > 1 ||
            (!VI.Kills.empty() && VI.Kills.front()->getParent() != DefMBB))
          for (auto *MI : VI.Kills)
            LiveInSets[MI->getParent()->getNumber()].set(Index);
      }
    }

    for (auto &MBB : MF)
      Changed |= SplitPHIEdges(MF, MBB, MLI, (LV ? &LiveInSets : nullptr));
  }

  // This pass takes the function out of SSA form.
  MRI->leaveSSA();

  // Populate VRegPHIUseCount
  if (LV || LIS)
    analyzePHINodes(MF);

  // Eliminate PHI instructions by inserting copies into predecessor blocks.
  for (auto &MBB : MF)
    Changed |= EliminatePHINodes(MF, MBB);

  // Remove dead IMPLICIT_DEF instructions.
  for (MachineInstr *DefMI : ImpDefs) {
    Register DefReg = DefMI->getOperand(0).getReg();
    if (MRI->use_nodbg_empty(DefReg)) {
      if (LIS)
        LIS->RemoveMachineInstrFromMaps(*DefMI);
      DefMI->eraseFromParent();
    }
  }

  // Clean up the lowered PHI instructions.
  for (auto &I : LoweredPHIs) {
    if (LIS)
      LIS->RemoveMachineInstrFromMaps(*I.first);
    MF.deleteMachineInstr(I.first);
  }

  // TODO: we should use the incremental DomTree updater here.
  if (Changed && MDT)
    MDT->getBase().recalculate(MF);

  LoweredPHIs.clear();
  ImpDefs.clear();
  VRegPHIUseCount.clear();

  MF.getProperties().set(MachineFunctionProperties::Property::NoPHIs);

  return Changed;
}

/// EliminatePHINodes - Eliminate phi nodes by inserting copy instructions in
/// predecessor basic blocks.
bool PHIEliminationImpl::EliminatePHINodes(MachineFunction &MF,
                                           MachineBasicBlock &MBB) {
  if (MBB.empty() || !MBB.front().isPHI())
    return false; // Quick exit for basic blocks without PHIs.

  // Get an iterator to the last PHI node.
  MachineBasicBlock::iterator LastPHIIt =
      std::prev(MBB.SkipPHIsAndLabels(MBB.begin()));

  // If all incoming edges are critical, we try to deduplicate identical PHIs so
  // that we generate fewer copies. If at any edge is non-critical, we either
  // have less than two predecessors (=> no PHIs) or a predecessor has only us
  // as a successor (=> identical PHI node can't occur in different block).
  bool AllEdgesCritical = MBB.pred_size() >= 2;
  for (MachineBasicBlock *Pred : MBB.predecessors()) {
    if (Pred->succ_size() < 2) {
      AllEdgesCritical = false;
      break;
    }
  }

  while (MBB.front().isPHI())
    LowerPHINode(MBB, LastPHIIt, AllEdgesCritical);

  return true;
}

/// Return true if all defs of VirtReg are implicit-defs.
/// This includes registers with no defs.
static bool isImplicitlyDefined(unsigned VirtReg,
                                const MachineRegisterInfo &MRI) {
  for (MachineInstr &DI : MRI.def_instructions(VirtReg))
    if (!DI.isImplicitDef())
      return false;
  return true;
}

/// Return true if all sources of the phi node are implicit_def's, or undef's.
static bool allPhiOperandsUndefined(const MachineInstr &MPhi,
                                    const MachineRegisterInfo &MRI) {
  for (unsigned I = 1, E = MPhi.getNumOperands(); I != E; I += 2) {
    const MachineOperand &MO = MPhi.getOperand(I);
    if (!isImplicitlyDefined(MO.getReg(), MRI) && !MO.isUndef())
      return false;
  }
  return true;
}
/// LowerPHINode - Lower the PHI node at the top of the specified block.
void PHIEliminationImpl::LowerPHINode(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator LastPHIIt,
                                      bool AllEdgesCritical) {
  ++NumLowered;

  MachineBasicBlock::iterator AfterPHIsIt = std::next(LastPHIIt);

  // Unlink the PHI node from the basic block, but don't delete the PHI yet.
  MachineInstr *MPhi = MBB.remove(&*MBB.begin());

  unsigned NumSrcs = (MPhi->getNumOperands() - 1) / 2;
  Register DestReg = MPhi->getOperand(0).getReg();
  assert(MPhi->getOperand(0).getSubReg() == 0 && "Can't handle sub-reg PHIs");
  bool isDead = MPhi->getOperand(0).isDead();

  // Create a new register for the incoming PHI arguments.
  MachineFunction &MF = *MBB.getParent();
  unsigned IncomingReg = 0;
  bool EliminateNow = true;    // delay elimination of nodes in LoweredPHIs
  bool reusedIncoming = false; // Is IncomingReg reused from an earlier PHI?

  // Insert a register to register copy at the top of the current block (but
  // after any remaining phi nodes) which copies the new incoming register
  // into the phi node destination.
  MachineInstr *PHICopy = nullptr;
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  if (allPhiOperandsUndefined(*MPhi, *MRI))
    // If all sources of a PHI node are implicit_def or undef uses, just emit an
    // implicit_def instead of a copy.
    PHICopy = BuildMI(MBB, AfterPHIsIt, MPhi->getDebugLoc(),
                      TII->get(TargetOpcode::IMPLICIT_DEF), DestReg);
  else {
    // Can we reuse an earlier PHI node? This only happens for critical edges,
    // typically those created by tail duplication. Typically, an identical PHI
    // node can't occur, so avoid hashing/storing such PHIs, which is somewhat
    // expensive.
    unsigned *Entry = nullptr;
    if (AllEdgesCritical)
      Entry = &LoweredPHIs[MPhi];
    if (Entry && *Entry) {
      // An identical PHI node was already lowered. Reuse the incoming register.
      IncomingReg = *Entry;
      reusedIncoming = true;
      ++NumReused;
      LLVM_DEBUG(dbgs() << "Reusing " << printReg(IncomingReg) << " for "
                        << *MPhi);
    } else {
      const TargetRegisterClass *RC = MF.getRegInfo().getRegClass(DestReg);
      IncomingReg = MF.getRegInfo().createVirtualRegister(RC);
      if (Entry) {
        EliminateNow = false;
        *Entry = IncomingReg;
      }
    }

    // Give the target possiblity to handle special cases fallthrough otherwise
    PHICopy = TII->createPHIDestinationCopy(
        MBB, AfterPHIsIt, MPhi->getDebugLoc(), IncomingReg, DestReg);
  }

  if (MPhi->peekDebugInstrNum()) {
    // If referred to by debug-info, store where this PHI was.
    MachineFunction *MF = MBB.getParent();
    unsigned ID = MPhi->peekDebugInstrNum();
    auto P = MachineFunction::DebugPHIRegallocPos(&MBB, IncomingReg, 0);
    auto Res = MF->DebugPHIPositions.insert({ID, P});
    assert(Res.second);
    (void)Res;
  }

  // Update live variable information if there is any.
  if (LV) {
    if (IncomingReg) {
      LiveVariables::VarInfo &VI = LV->getVarInfo(IncomingReg);

      MachineInstr *OldKill = nullptr;
      bool IsPHICopyAfterOldKill = false;

      if (reusedIncoming && (OldKill = VI.findKill(&MBB))) {
        // Calculate whether the PHICopy is after the OldKill.
        // In general, the PHICopy is inserted as the first non-phi instruction
        // by default, so it's before the OldKill. But some Target hooks for
        // createPHIDestinationCopy() may modify the default insert position of
        // PHICopy.
        for (auto I = MBB.SkipPHIsAndLabels(MBB.begin()), E = MBB.end(); I != E;
             ++I) {
          if (I == PHICopy)
            break;

          if (I == OldKill) {
            IsPHICopyAfterOldKill = true;
            break;
          }
        }
      }

      // When we are reusing the incoming register and it has been marked killed
      // by OldKill, if the PHICopy is after the OldKill, we should remove the
      // killed flag from OldKill.
      if (IsPHICopyAfterOldKill) {
        LLVM_DEBUG(dbgs() << "Remove old kill from " << *OldKill);
        LV->removeVirtualRegisterKilled(IncomingReg, *OldKill);
        LLVM_DEBUG(MBB.dump());
      }

      // Add information to LiveVariables to know that the first used incoming
      // value or the resued incoming value whose PHICopy is after the OldKIll
      // is killed. Note that because the value is defined in several places
      // (once each for each incoming block), the "def" block and instruction
      // fields for the VarInfo is not filled in.
      if (!OldKill || IsPHICopyAfterOldKill)
        LV->addVirtualRegisterKilled(IncomingReg, *PHICopy);
    }

    // Since we are going to be deleting the PHI node, if it is the last use of
    // any registers, or if the value itself is dead, we need to move this
    // information over to the new copy we just inserted.
    LV->removeVirtualRegistersKilled(*MPhi);

    // If the result is dead, update LV.
    if (isDead) {
      LV->addVirtualRegisterDead(DestReg, *PHICopy);
      LV->removeVirtualRegisterDead(DestReg, *MPhi);
    }
  }

  // Update LiveIntervals for the new copy or implicit def.
  if (LIS) {
    SlotIndex DestCopyIndex = LIS->InsertMachineInstrInMaps(*PHICopy);

    SlotIndex MBBStartIndex = LIS->getMBBStartIdx(&MBB);
    if (IncomingReg) {
      // Add the region from the beginning of MBB to the copy instruction to
      // IncomingReg's live interval.
      LiveInterval &IncomingLI = LIS->getOrCreateEmptyInterval(IncomingReg);
      VNInfo *IncomingVNI = IncomingLI.getVNInfoAt(MBBStartIndex);
      if (!IncomingVNI)
        IncomingVNI =
            IncomingLI.getNextValue(MBBStartIndex, LIS->getVNInfoAllocator());
      IncomingLI.addSegment(LiveInterval::Segment(
          MBBStartIndex, DestCopyIndex.getRegSlot(), IncomingVNI));
    }

    LiveInterval &DestLI = LIS->getInterval(DestReg);
    assert(!DestLI.empty() && "PHIs should have non-empty LiveIntervals.");

    SlotIndex NewStart = DestCopyIndex.getRegSlot();

    SmallVector<LiveRange *> ToUpdate({&DestLI});
    for (auto &SR : DestLI.subranges())
      ToUpdate.push_back(&SR);

    for (auto LR : ToUpdate) {
      auto DestSegment = LR->find(MBBStartIndex);
      assert(DestSegment != LR->end() &&
             "PHI destination must be live in block");

      if (LR->endIndex().isDead()) {
        // A dead PHI's live range begins and ends at the start of the MBB, but
        // the lowered copy, which will still be dead, needs to begin and end at
        // the copy instruction.
        VNInfo *OrigDestVNI = LR->getVNInfoAt(DestSegment->start);
        assert(OrigDestVNI && "PHI destination should be live at block entry.");
        LR->removeSegment(DestSegment->start, DestSegment->start.getDeadSlot());
        LR->createDeadDef(NewStart, LIS->getVNInfoAllocator());
        LR->removeValNo(OrigDestVNI);
        continue;
      }

      // Destination copies are not inserted in the same order as the PHI nodes
      // they replace. Hence the start of the live range may need to be adjusted
      // to match the actual slot index of the copy.
      if (DestSegment->start > NewStart) {
        VNInfo *VNI = LR->getVNInfoAt(DestSegment->start);
        assert(VNI && "value should be defined for known segment");
        LR->addSegment(
            LiveInterval::Segment(NewStart, DestSegment->start, VNI));
      } else if (DestSegment->start < NewStart) {
        assert(DestSegment->start >= MBBStartIndex);
        assert(DestSegment->end >= DestCopyIndex.getRegSlot());
        LR->removeSegment(DestSegment->start, NewStart);
      }
      VNInfo *DestVNI = LR->getVNInfoAt(NewStart);
      assert(DestVNI && "PHI destination should be live at its definition.");
      DestVNI->def = NewStart;
    }
  }

  // Adjust the VRegPHIUseCount map to account for the removal of this PHI node.
  if (LV || LIS) {
    for (unsigned i = 1; i != MPhi->getNumOperands(); i += 2) {
      if (!MPhi->getOperand(i).isUndef()) {
        --VRegPHIUseCount[BBVRegPair(
            MPhi->getOperand(i + 1).getMBB()->getNumber(),
            MPhi->getOperand(i).getReg())];
      }
    }
  }

  // Now loop over all of the incoming arguments, changing them to copy into the
  // IncomingReg register in the corresponding predecessor basic block.
  SmallPtrSet<MachineBasicBlock *, 8> MBBsInsertedInto;
  for (int i = NumSrcs - 1; i >= 0; --i) {
    Register SrcReg = MPhi->getOperand(i * 2 + 1).getReg();
    unsigned SrcSubReg = MPhi->getOperand(i * 2 + 1).getSubReg();
    bool SrcUndef = MPhi->getOperand(i * 2 + 1).isUndef() ||
                    isImplicitlyDefined(SrcReg, *MRI);
    assert(SrcReg.isVirtual() &&
           "Machine PHI Operands must all be virtual registers!");

    // Get the MachineBasicBlock equivalent of the BasicBlock that is the source
    // path the PHI.
    MachineBasicBlock &opBlock = *MPhi->getOperand(i * 2 + 2).getMBB();

    // Check to make sure we haven't already emitted the copy for this block.
    // This can happen because PHI nodes may have multiple entries for the same
    // basic block.
    if (!MBBsInsertedInto.insert(&opBlock).second)
      continue; // If the copy has already been emitted, we're done.

    MachineInstr *SrcRegDef = MRI->getVRegDef(SrcReg);
    if (SrcRegDef && TII->isUnspillableTerminator(SrcRegDef)) {
      assert(SrcRegDef->getOperand(0).isReg() &&
             SrcRegDef->getOperand(0).isDef() &&
             "Expected operand 0 to be a reg def!");
      // Now that the PHI's use has been removed (as the instruction was
      // removed) there should be no other uses of the SrcReg.
      assert(MRI->use_empty(SrcReg) &&
             "Expected a single use from UnspillableTerminator");
      SrcRegDef->getOperand(0).setReg(IncomingReg);

      // Update LiveVariables.
      if (LV) {
        LiveVariables::VarInfo &SrcVI = LV->getVarInfo(SrcReg);
        LiveVariables::VarInfo &IncomingVI = LV->getVarInfo(IncomingReg);
        IncomingVI.AliveBlocks = std::move(SrcVI.AliveBlocks);
        SrcVI.AliveBlocks.clear();
      }

      continue;
    }

    // Find a safe location to insert the copy, this may be the first terminator
    // in the block (or end()).
    MachineBasicBlock::iterator InsertPos =
        findPHICopyInsertPoint(&opBlock, &MBB, SrcReg);

    // Insert the copy.
    MachineInstr *NewSrcInstr = nullptr;
    if (!reusedIncoming && IncomingReg) {
      if (SrcUndef) {
        // The source register is undefined, so there is no need for a real
        // COPY, but we still need to ensure joint dominance by defs.
        // Insert an IMPLICIT_DEF instruction.
        NewSrcInstr =
            BuildMI(opBlock, InsertPos, MPhi->getDebugLoc(),
                    TII->get(TargetOpcode::IMPLICIT_DEF), IncomingReg);

        // Clean up the old implicit-def, if there even was one.
        if (MachineInstr *DefMI = MRI->getVRegDef(SrcReg))
          if (DefMI->isImplicitDef())
            ImpDefs.insert(DefMI);
      } else {
        // Delete the debug location, since the copy is inserted into a
        // different basic block.
        NewSrcInstr = TII->createPHISourceCopy(opBlock, InsertPos, nullptr,
                                               SrcReg, SrcSubReg, IncomingReg);
      }
    }

    // We only need to update the LiveVariables kill of SrcReg if this was the
    // last PHI use of SrcReg to be lowered on this CFG edge and it is not live
    // out of the predecessor. We can also ignore undef sources.
    if (LV && !SrcUndef &&
        !VRegPHIUseCount[BBVRegPair(opBlock.getNumber(), SrcReg)] &&
        !LV->isLiveOut(SrcReg, opBlock)) {
      // We want to be able to insert a kill of the register if this PHI (aka,
      // the copy we just inserted) is the last use of the source value. Live
      // variable analysis conservatively handles this by saying that the value
      // is live until the end of the block the PHI entry lives in. If the value
      // really is dead at the PHI copy, there will be no successor blocks which
      // have the value live-in.

      // Okay, if we now know that the value is not live out of the block, we
      // can add a kill marker in this block saying that it kills the incoming
      // value!

      // In our final twist, we have to decide which instruction kills the
      // register.  In most cases this is the copy, however, terminator
      // instructions at the end of the block may also use the value. In this
      // case, we should mark the last such terminator as being the killing
      // block, not the copy.
      MachineBasicBlock::iterator KillInst = opBlock.end();
      for (MachineBasicBlock::iterator Term = InsertPos; Term != opBlock.end();
           ++Term) {
        if (Term->readsRegister(SrcReg, /*TRI=*/nullptr))
          KillInst = Term;
      }

      if (KillInst == opBlock.end()) {
        // No terminator uses the register.

        if (reusedIncoming || !IncomingReg) {
          // We may have to rewind a bit if we didn't insert a copy this time.
          KillInst = InsertPos;
          while (KillInst != opBlock.begin()) {
            --KillInst;
            if (KillInst->isDebugInstr())
              continue;
            if (KillInst->readsRegister(SrcReg, /*TRI=*/nullptr))
              break;
          }
        } else {
          // We just inserted this copy.
          KillInst = NewSrcInstr;
        }
      }
      assert(KillInst->readsRegister(SrcReg, /*TRI=*/nullptr) &&
             "Cannot find kill instruction");

      // Finally, mark it killed.
      LV->addVirtualRegisterKilled(SrcReg, *KillInst);

      // This vreg no longer lives all of the way through opBlock.
      unsigned opBlockNum = opBlock.getNumber();
      LV->getVarInfo(SrcReg).AliveBlocks.reset(opBlockNum);
    }

    if (LIS) {
      if (NewSrcInstr) {
        LIS->InsertMachineInstrInMaps(*NewSrcInstr);
        LIS->addSegmentToEndOfBlock(IncomingReg, *NewSrcInstr);
      }

      if (!SrcUndef &&
          !VRegPHIUseCount[BBVRegPair(opBlock.getNumber(), SrcReg)]) {
        LiveInterval &SrcLI = LIS->getInterval(SrcReg);

        bool isLiveOut = false;
        for (MachineBasicBlock *Succ : opBlock.successors()) {
          SlotIndex startIdx = LIS->getMBBStartIdx(Succ);
          VNInfo *VNI = SrcLI.getVNInfoAt(startIdx);

          // Definitions by other PHIs are not truly live-in for our purposes.
          if (VNI && VNI->def != startIdx) {
            isLiveOut = true;
            break;
          }
        }

        if (!isLiveOut) {
          MachineBasicBlock::iterator KillInst = opBlock.end();
          for (MachineBasicBlock::iterator Term = InsertPos;
               Term != opBlock.end(); ++Term) {
            if (Term->readsRegister(SrcReg, /*TRI=*/nullptr))
              KillInst = Term;
          }

          if (KillInst == opBlock.end()) {
            // No terminator uses the register.

            if (reusedIncoming || !IncomingReg) {
              // We may have to rewind a bit if we didn't just insert a copy.
              KillInst = InsertPos;
              while (KillInst != opBlock.begin()) {
                --KillInst;
                if (KillInst->isDebugInstr())
                  continue;
                if (KillInst->readsRegister(SrcReg, /*TRI=*/nullptr))
                  break;
              }
            } else {
              // We just inserted this copy.
              KillInst = std::prev(InsertPos);
            }
          }
          assert(KillInst->readsRegister(SrcReg, /*TRI=*/nullptr) &&
                 "Cannot find kill instruction");

          SlotIndex LastUseIndex = LIS->getInstructionIndex(*KillInst);
          SrcLI.removeSegment(LastUseIndex.getRegSlot(),
                              LIS->getMBBEndIdx(&opBlock));
          for (auto &SR : SrcLI.subranges()) {
            SR.removeSegment(LastUseIndex.getRegSlot(),
                             LIS->getMBBEndIdx(&opBlock));
          }
        }
      }
    }
  }

  // Really delete the PHI instruction now, if it is not in the LoweredPHIs map.
  if (EliminateNow) {
    if (LIS)
      LIS->RemoveMachineInstrFromMaps(*MPhi);
    MF.deleteMachineInstr(MPhi);
  }
}

/// analyzePHINodes - Gather information about the PHI nodes in here. In
/// particular, we want to map the number of uses of a virtual register which is
/// used in a PHI node. We map that to the BB the vreg is coming from. This is
/// used later to determine when the vreg is killed in the BB.
void PHIEliminationImpl::analyzePHINodes(const MachineFunction &MF) {
  for (const auto &MBB : MF) {
    for (const auto &BBI : MBB) {
      if (!BBI.isPHI())
        break;
      for (unsigned i = 1, e = BBI.getNumOperands(); i != e; i += 2) {
        if (!BBI.getOperand(i).isUndef()) {
          ++VRegPHIUseCount[BBVRegPair(
              BBI.getOperand(i + 1).getMBB()->getNumber(),
              BBI.getOperand(i).getReg())];
        }
      }
    }
  }
}

bool PHIEliminationImpl::SplitPHIEdges(
    MachineFunction &MF, MachineBasicBlock &MBB, MachineLoopInfo *MLI,
    std::vector<SparseBitVector<>> *LiveInSets) {
  if (MBB.empty() || !MBB.front().isPHI() || MBB.isEHPad())
    return false; // Quick exit for basic blocks without PHIs.

  const MachineLoop *CurLoop = MLI ? MLI->getLoopFor(&MBB) : nullptr;
  bool IsLoopHeader = CurLoop && &MBB == CurLoop->getHeader();

  bool Changed = false;
  for (MachineBasicBlock::iterator BBI = MBB.begin(), BBE = MBB.end();
       BBI != BBE && BBI->isPHI(); ++BBI) {
    for (unsigned i = 1, e = BBI->getNumOperands(); i != e; i += 2) {
      Register Reg = BBI->getOperand(i).getReg();
      MachineBasicBlock *PreMBB = BBI->getOperand(i + 1).getMBB();
      // Is there a critical edge from PreMBB to MBB?
      if (PreMBB->succ_size() == 1)
        continue;

      // Avoid splitting backedges of loops. It would introduce small
      // out-of-line blocks into the loop which is very bad for code placement.
      if (PreMBB == &MBB && !SplitAllCriticalEdges)
        continue;
      const MachineLoop *PreLoop = MLI ? MLI->getLoopFor(PreMBB) : nullptr;
      if (IsLoopHeader && PreLoop == CurLoop && !SplitAllCriticalEdges)
        continue;

      // LV doesn't consider a phi use live-out, so isLiveOut only returns true
      // when the source register is live-out for some other reason than a phi
      // use. That means the copy we will insert in PreMBB won't be a kill, and
      // there is a risk it may not be coalesced away.
      //
      // If the copy would be a kill, there is no need to split the edge.
      bool ShouldSplit = isLiveOutPastPHIs(Reg, PreMBB);
      if (!ShouldSplit && !NoPhiElimLiveOutEarlyExit)
        continue;
      if (ShouldSplit) {
        LLVM_DEBUG(dbgs() << printReg(Reg) << " live-out before critical edge "
                          << printMBBReference(*PreMBB) << " -> "
                          << printMBBReference(MBB) << ": " << *BBI);
      }

      // If Reg is not live-in to MBB, it means it must be live-in to some
      // other PreMBB successor, and we can avoid the interference by splitting
      // the edge.
      //
      // If Reg *is* live-in to MBB, the interference is inevitable and a copy
      // is likely to be left after coalescing. If we are looking at a loop
      // exiting edge, split it so we won't insert code in the loop, otherwise
      // don't bother.
      ShouldSplit = ShouldSplit && !isLiveIn(Reg, &MBB);

      // Check for a loop exiting edge.
      if (!ShouldSplit && CurLoop != PreLoop) {
        LLVM_DEBUG({
          dbgs() << "Split wouldn't help, maybe avoid loop copies?\n";
          if (PreLoop)
            dbgs() << "PreLoop: " << *PreLoop;
          if (CurLoop)
            dbgs() << "CurLoop: " << *CurLoop;
        });
        // This edge could be entering a loop, exiting a loop, or it could be
        // both: Jumping directly form one loop to the header of a sibling
        // loop.
        // Split unless this edge is entering CurLoop from an outer loop.
        ShouldSplit = PreLoop && !PreLoop->contains(CurLoop);
      }
      if (!ShouldSplit && !SplitAllCriticalEdges)
        continue;
      if (!(P ? PreMBB->SplitCriticalEdge(&MBB, *P, LiveInSets)
              : PreMBB->SplitCriticalEdge(&MBB, *MFAM, LiveInSets))) {
        LLVM_DEBUG(dbgs() << "Failed to split critical edge.\n");
        continue;
      }
      Changed = true;
      ++NumCriticalEdgesSplit;
    }
  }
  return Changed;
}

bool PHIEliminationImpl::isLiveIn(Register Reg, const MachineBasicBlock *MBB) {
  assert((LV || LIS) &&
         "isLiveIn() requires either LiveVariables or LiveIntervals");
  if (LIS)
    return LIS->isLiveInToMBB(LIS->getInterval(Reg), MBB);
  else
    return LV->isLiveIn(Reg, *MBB);
}

bool PHIEliminationImpl::isLiveOutPastPHIs(Register Reg,
                                           const MachineBasicBlock *MBB) {
  assert((LV || LIS) &&
         "isLiveOutPastPHIs() requires either LiveVariables or LiveIntervals");
  // LiveVariables considers uses in PHIs to be in the predecessor basic block,
  // so that a register used only in a PHI is not live out of the block. In
  // contrast, LiveIntervals considers uses in PHIs to be on the edge rather
  // than in the predecessor basic block, so that a register used only in a PHI
  // is live out of the block.
  if (LIS) {
    const LiveInterval &LI = LIS->getInterval(Reg);
    for (const MachineBasicBlock *SI : MBB->successors())
      if (LI.liveAt(LIS->getMBBStartIdx(SI)))
        return true;
    return false;
  } else {
    return LV->isLiveOut(Reg, *MBB);
  }
}
