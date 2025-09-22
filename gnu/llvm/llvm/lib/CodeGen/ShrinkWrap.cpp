//===- ShrinkWrap.cpp - Compute safe point for prolog/epilog insertion ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass looks for safe point where the prologue and epilogue can be
// inserted.
// The safe point for the prologue (resp. epilogue) is called Save
// (resp. Restore).
// A point is safe for prologue (resp. epilogue) if and only if
// it 1) dominates (resp. post-dominates) all the frame related operations and
// between 2) two executions of the Save (resp. Restore) point there is an
// execution of the Restore (resp. Save) point.
//
// For instance, the following points are safe:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Restore -> Save -> Restore ...
// And the following points are not:
// for (int i = 0; i < 10; ++i) {
//   Save
//   ...
// }
// for (int i = 0; i < 10; ++i) {
//   ...
//   Restore
// }
// Indeed, the execution looks like Save -> Save -> ... -> Restore -> Restore.
//
// This pass also ensures that the safe points are 3) cheaper than the regular
// entry and exits blocks.
//
// Property #1 is ensured via the use of MachineDominatorTree and
// MachinePostDominatorTree.
// Property #2 is ensured via property #1 and MachineLoopInfo, i.e., both
// points must be in the same loop.
// Property #3 is ensured via the MachineBlockFrequencyInfo.
//
// If this pass found points matching all these properties, then
// MachineFrameInfo is updated with this information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/RegisterClassInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cstdint>
#include <memory>

using namespace llvm;

#define DEBUG_TYPE "shrink-wrap"

STATISTIC(NumFunc, "Number of functions");
STATISTIC(NumCandidates, "Number of shrink-wrapping candidates");
STATISTIC(NumCandidatesDropped,
          "Number of shrink-wrapping candidates dropped because of frequency");

static cl::opt<cl::boolOrDefault>
EnableShrinkWrapOpt("enable-shrink-wrap", cl::Hidden,
                    cl::desc("enable the shrink-wrapping pass"));
static cl::opt<bool> EnablePostShrinkWrapOpt(
    "enable-shrink-wrap-region-split", cl::init(true), cl::Hidden,
    cl::desc("enable splitting of the restore block if possible"));

namespace {

/// Class to determine where the safe point to insert the
/// prologue and epilogue are.
/// Unlike the paper from Fred C. Chow, PLDI'88, that introduces the
/// shrink-wrapping term for prologue/epilogue placement, this pass
/// does not rely on expensive data-flow analysis. Instead we use the
/// dominance properties and loop information to decide which point
/// are safe for such insertion.
class ShrinkWrap : public MachineFunctionPass {
  /// Hold callee-saved information.
  RegisterClassInfo RCI;
  MachineDominatorTree *MDT = nullptr;
  MachinePostDominatorTree *MPDT = nullptr;

  /// Current safe point found for the prologue.
  /// The prologue will be inserted before the first instruction
  /// in this basic block.
  MachineBasicBlock *Save = nullptr;

  /// Current safe point found for the epilogue.
  /// The epilogue will be inserted before the first terminator instruction
  /// in this basic block.
  MachineBasicBlock *Restore = nullptr;

  /// Hold the information of the basic block frequency.
  /// Use to check the profitability of the new points.
  MachineBlockFrequencyInfo *MBFI = nullptr;

  /// Hold the loop information. Used to determine if Save and Restore
  /// are in the same loop.
  MachineLoopInfo *MLI = nullptr;

  // Emit remarks.
  MachineOptimizationRemarkEmitter *ORE = nullptr;

  /// Frequency of the Entry block.
  BlockFrequency EntryFreq;

  /// Current opcode for frame setup.
  unsigned FrameSetupOpcode = ~0u;

  /// Current opcode for frame destroy.
  unsigned FrameDestroyOpcode = ~0u;

  /// Stack pointer register, used by llvm.{savestack,restorestack}
  Register SP;

  /// Entry block.
  const MachineBasicBlock *Entry = nullptr;

  using SetOfRegs = SmallSetVector<unsigned, 16>;

  /// Registers that need to be saved for the current function.
  mutable SetOfRegs CurrentCSRs;

  /// Current MachineFunction.
  MachineFunction *MachineFunc = nullptr;

  /// Is `true` for the block numbers where we assume possible stack accesses
  /// or computation of stack-relative addresses on any CFG path including the
  /// block itself. Is `false` for basic blocks where we can guarantee the
  /// opposite. False positives won't lead to incorrect analysis results,
  /// therefore this approach is fair.
  BitVector StackAddressUsedBlockInfo;

  /// Check if \p MI uses or defines a callee-saved register or
  /// a frame index. If this is the case, this means \p MI must happen
  /// after Save and before Restore.
  bool useOrDefCSROrFI(const MachineInstr &MI, RegScavenger *RS,
                       bool StackAddressUsed) const;

  const SetOfRegs &getCurrentCSRs(RegScavenger *RS) const {
    if (CurrentCSRs.empty()) {
      BitVector SavedRegs;
      const TargetFrameLowering *TFI =
          MachineFunc->getSubtarget().getFrameLowering();

      TFI->determineCalleeSaves(*MachineFunc, SavedRegs, RS);

      for (int Reg = SavedRegs.find_first(); Reg != -1;
           Reg = SavedRegs.find_next(Reg))
        CurrentCSRs.insert((unsigned)Reg);
    }
    return CurrentCSRs;
  }

  /// Update the Save and Restore points such that \p MBB is in
  /// the region that is dominated by Save and post-dominated by Restore
  /// and Save and Restore still match the safe point definition.
  /// Such point may not exist and Save and/or Restore may be null after
  /// this call.
  void updateSaveRestorePoints(MachineBasicBlock &MBB, RegScavenger *RS);

  // Try to find safe point based on dominance and block frequency without
  // any change in IR.
  bool performShrinkWrapping(
      const ReversePostOrderTraversal<MachineBasicBlock *> &RPOT,
      RegScavenger *RS);

  /// This function tries to split the restore point if doing so can shrink the
  /// save point further. \return True if restore point is split.
  bool postShrinkWrapping(bool HasCandidate, MachineFunction &MF,
                          RegScavenger *RS);

  /// This function analyzes if the restore point can split to create a new
  /// restore point. This function collects
  /// 1. Any preds of current restore that are reachable by callee save/FI
  /// blocks
  /// - indicated by DirtyPreds
  /// 2. Any preds of current restore that are not DirtyPreds - indicated by
  /// CleanPreds
  /// Both sets should be non-empty for considering restore point split.
  bool checkIfRestoreSplittable(
      const MachineBasicBlock *CurRestore,
      const DenseSet<const MachineBasicBlock *> &ReachableByDirty,
      SmallVectorImpl<MachineBasicBlock *> &DirtyPreds,
      SmallVectorImpl<MachineBasicBlock *> &CleanPreds,
      const TargetInstrInfo *TII, RegScavenger *RS);

  /// Initialize the pass for \p MF.
  void init(MachineFunction &MF) {
    RCI.runOnMachineFunction(MF);
    MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
    MPDT = &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree();
    Save = nullptr;
    Restore = nullptr;
    MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
    MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
    ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();
    EntryFreq = MBFI->getEntryFreq();
    const TargetSubtargetInfo &Subtarget = MF.getSubtarget();
    const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
    FrameSetupOpcode = TII.getCallFrameSetupOpcode();
    FrameDestroyOpcode = TII.getCallFrameDestroyOpcode();
    SP = Subtarget.getTargetLowering()->getStackPointerRegisterToSaveRestore();
    Entry = &MF.front();
    CurrentCSRs.clear();
    MachineFunc = &MF;

    ++NumFunc;
  }

  /// Check whether or not Save and Restore points are still interesting for
  /// shrink-wrapping.
  bool ArePointsInteresting() const { return Save != Entry && Save && Restore; }

  /// Check if shrink wrapping is enabled for this target and function.
  static bool isShrinkWrapEnabled(const MachineFunction &MF);

public:
  static char ID;

  ShrinkWrap() : MachineFunctionPass(ID) {
    initializeShrinkWrapPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<MachineOptimizationRemarkEmitterPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
      MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override { return "Shrink Wrapping analysis"; }

  /// Perform the shrink-wrapping analysis and update
  /// the MachineFrameInfo attached to \p MF with the results.
  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char ShrinkWrap::ID = 0;

char &llvm::ShrinkWrapID = ShrinkWrap::ID;

INITIALIZE_PASS_BEGIN(ShrinkWrap, DEBUG_TYPE, "Shrink Wrap Pass", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineOptimizationRemarkEmitterPass)
INITIALIZE_PASS_END(ShrinkWrap, DEBUG_TYPE, "Shrink Wrap Pass", false, false)

bool ShrinkWrap::useOrDefCSROrFI(const MachineInstr &MI, RegScavenger *RS,
                                 bool StackAddressUsed) const {
  /// Check if \p Op is known to access an address not on the function's stack .
  /// At the moment, accesses where the underlying object is a global, function
  /// argument, or jump table are considered non-stack accesses. Note that the
  /// caller's stack may get accessed when passing an argument via the stack,
  /// but not the stack of the current function.
  ///
  auto IsKnownNonStackPtr = [](MachineMemOperand *Op) {
    if (Op->getValue()) {
      const Value *UO = getUnderlyingObject(Op->getValue());
      if (!UO)
        return false;
      if (auto *Arg = dyn_cast<Argument>(UO))
        return !Arg->hasPassPointeeByValueCopyAttr();
      return isa<GlobalValue>(UO);
    }
    if (const PseudoSourceValue *PSV = Op->getPseudoValue())
      return PSV->isJumpTable();
    return false;
  };
  // Load/store operations may access the stack indirectly when we previously
  // computed an address to a stack location.
  if (StackAddressUsed && MI.mayLoadOrStore() &&
      (MI.isCall() || MI.hasUnmodeledSideEffects() || MI.memoperands_empty() ||
       !all_of(MI.memoperands(), IsKnownNonStackPtr)))
    return true;

  if (MI.getOpcode() == FrameSetupOpcode ||
      MI.getOpcode() == FrameDestroyOpcode) {
    LLVM_DEBUG(dbgs() << "Frame instruction: " << MI << '\n');
    return true;
  }
  const MachineFunction *MF = MI.getParent()->getParent();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  for (const MachineOperand &MO : MI.operands()) {
    bool UseOrDefCSR = false;
    if (MO.isReg()) {
      // Ignore instructions like DBG_VALUE which don't read/def the register.
      if (!MO.isDef() && !MO.readsReg())
        continue;
      Register PhysReg = MO.getReg();
      if (!PhysReg)
        continue;
      assert(PhysReg.isPhysical() && "Unallocated register?!");
      // The stack pointer is not normally described as a callee-saved register
      // in calling convention definitions, so we need to watch for it
      // separately. An SP mentioned by a call instruction, we can ignore,
      // though, as it's harmless and we do not want to effectively disable tail
      // calls by forcing the restore point to post-dominate them.
      // PPC's LR is also not normally described as a callee-saved register in
      // calling convention definitions, so we need to watch for it, too. An LR
      // mentioned implicitly by a return (or "branch to link register")
      // instruction we can ignore, otherwise we may pessimize shrinkwrapping.
      UseOrDefCSR =
          (!MI.isCall() && PhysReg == SP) ||
          RCI.getLastCalleeSavedAlias(PhysReg) ||
          (!MI.isReturn() && TRI->isNonallocatableRegisterCalleeSave(PhysReg));
    } else if (MO.isRegMask()) {
      // Check if this regmask clobbers any of the CSRs.
      for (unsigned Reg : getCurrentCSRs(RS)) {
        if (MO.clobbersPhysReg(Reg)) {
          UseOrDefCSR = true;
          break;
        }
      }
    }
    // Skip FrameIndex operands in DBG_VALUE instructions.
    if (UseOrDefCSR || (MO.isFI() && !MI.isDebugValue())) {
      LLVM_DEBUG(dbgs() << "Use or define CSR(" << UseOrDefCSR << ") or FI("
                        << MO.isFI() << "): " << MI << '\n');
      return true;
    }
  }
  return false;
}

/// Helper function to find the immediate (post) dominator.
template <typename ListOfBBs, typename DominanceAnalysis>
static MachineBasicBlock *FindIDom(MachineBasicBlock &Block, ListOfBBs BBs,
                                   DominanceAnalysis &Dom, bool Strict = true) {
  MachineBasicBlock *IDom = &Block;
  for (MachineBasicBlock *BB : BBs) {
    IDom = Dom.findNearestCommonDominator(IDom, BB);
    if (!IDom)
      break;
  }
  if (Strict && IDom == &Block)
    return nullptr;
  return IDom;
}

static bool isAnalyzableBB(const TargetInstrInfo &TII,
                           MachineBasicBlock &Entry) {
  // Check if the block is analyzable.
  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  return !TII.analyzeBranch(Entry, TBB, FBB, Cond);
}

/// Determines if any predecessor of MBB is on the path from block that has use
/// or def of CSRs/FI to MBB.
/// ReachableByDirty: All blocks reachable from block that has use or def of
/// CSR/FI.
static bool
hasDirtyPred(const DenseSet<const MachineBasicBlock *> &ReachableByDirty,
             const MachineBasicBlock &MBB) {
  for (const MachineBasicBlock *PredBB : MBB.predecessors())
    if (ReachableByDirty.count(PredBB))
      return true;
  return false;
}

/// Derives the list of all the basic blocks reachable from MBB.
static void markAllReachable(DenseSet<const MachineBasicBlock *> &Visited,
                             const MachineBasicBlock &MBB) {
  SmallVector<MachineBasicBlock *, 4> Worklist(MBB.succ_begin(),
                                               MBB.succ_end());
  Visited.insert(&MBB);
  while (!Worklist.empty()) {
    MachineBasicBlock *SuccMBB = Worklist.pop_back_val();
    if (!Visited.insert(SuccMBB).second)
      continue;
    Worklist.append(SuccMBB->succ_begin(), SuccMBB->succ_end());
  }
}

/// Collect blocks reachable by use or def of CSRs/FI.
static void collectBlocksReachableByDirty(
    const DenseSet<const MachineBasicBlock *> &DirtyBBs,
    DenseSet<const MachineBasicBlock *> &ReachableByDirty) {
  for (const MachineBasicBlock *MBB : DirtyBBs) {
    if (ReachableByDirty.count(MBB))
      continue;
    // Mark all offsprings as reachable.
    markAllReachable(ReachableByDirty, *MBB);
  }
}

/// \return true if there is a clean path from SavePoint to the original
/// Restore.
static bool
isSaveReachableThroughClean(const MachineBasicBlock *SavePoint,
                            ArrayRef<MachineBasicBlock *> CleanPreds) {
  DenseSet<const MachineBasicBlock *> Visited;
  SmallVector<MachineBasicBlock *, 4> Worklist(CleanPreds.begin(),
                                               CleanPreds.end());
  while (!Worklist.empty()) {
    MachineBasicBlock *CleanBB = Worklist.pop_back_val();
    if (CleanBB == SavePoint)
      return true;
    if (!Visited.insert(CleanBB).second || !CleanBB->pred_size())
      continue;
    Worklist.append(CleanBB->pred_begin(), CleanBB->pred_end());
  }
  return false;
}

/// This function updates the branches post restore point split.
///
/// Restore point has been split.
/// Old restore point: MBB
/// New restore point: NMBB
/// Any basic block(say BBToUpdate) which had a fallthrough to MBB
/// previously should
/// 1. Fallthrough to NMBB iff NMBB is inserted immediately above MBB in the
/// block layout OR
/// 2. Branch unconditionally to NMBB iff NMBB is inserted at any other place.
static void updateTerminator(MachineBasicBlock *BBToUpdate,
                             MachineBasicBlock *NMBB,
                             const TargetInstrInfo *TII) {
  DebugLoc DL = BBToUpdate->findBranchDebugLoc();
  // if NMBB isn't the new layout successor for BBToUpdate, insert unconditional
  // branch to it
  if (!BBToUpdate->isLayoutSuccessor(NMBB))
    TII->insertUnconditionalBranch(*BBToUpdate, NMBB, DL);
}

/// This function splits the restore point and returns new restore point/BB.
///
/// DirtyPreds: Predessors of \p MBB that are ReachableByDirty
///
/// Decision has been made to split the restore point.
/// old restore point: \p MBB
/// new restore point: \p NMBB
/// This function makes the necessary block layout changes so that
/// 1. \p NMBB points to \p MBB unconditionally
/// 2. All dirtyPreds that previously pointed to \p MBB point to \p NMBB
static MachineBasicBlock *
tryToSplitRestore(MachineBasicBlock *MBB,
                  ArrayRef<MachineBasicBlock *> DirtyPreds,
                  const TargetInstrInfo *TII) {
  MachineFunction *MF = MBB->getParent();

  // get the list of DirtyPreds who have a fallthrough to MBB
  // before the block layout change. This is just to ensure that if the NMBB is
  // inserted after MBB, then we create unconditional branch from
  // DirtyPred/CleanPred to NMBB
  SmallPtrSet<MachineBasicBlock *, 8> MBBFallthrough;
  for (MachineBasicBlock *BB : DirtyPreds)
    if (BB->getFallThrough(false) == MBB)
      MBBFallthrough.insert(BB);

  MachineBasicBlock *NMBB = MF->CreateMachineBasicBlock();
  // Insert this block at the end of the function. Inserting in between may
  // interfere with control flow optimizer decisions.
  MF->insert(MF->end(), NMBB);

  for (const MachineBasicBlock::RegisterMaskPair &LI : MBB->liveins())
    NMBB->addLiveIn(LI.PhysReg);

  TII->insertUnconditionalBranch(*NMBB, MBB, DebugLoc());

  // After splitting, all predecessors of the restore point should be dirty
  // blocks.
  for (MachineBasicBlock *SuccBB : DirtyPreds)
    SuccBB->ReplaceUsesOfBlockWith(MBB, NMBB);

  NMBB->addSuccessor(MBB);

  for (MachineBasicBlock *BBToUpdate : MBBFallthrough)
    updateTerminator(BBToUpdate, NMBB, TII);

  return NMBB;
}

/// This function undoes the restore point split done earlier.
///
/// DirtyPreds: All predecessors of \p NMBB that are ReachableByDirty.
///
/// Restore point was split and the change needs to be unrolled. Make necessary
/// changes to reset restore point from \p NMBB to \p MBB.
static void rollbackRestoreSplit(MachineFunction &MF, MachineBasicBlock *NMBB,
                                 MachineBasicBlock *MBB,
                                 ArrayRef<MachineBasicBlock *> DirtyPreds,
                                 const TargetInstrInfo *TII) {
  // For a BB, if NMBB is fallthrough in the current layout, then in the new
  // layout a. BB should fallthrough to MBB OR b. BB should undconditionally
  // branch to MBB
  SmallPtrSet<MachineBasicBlock *, 8> NMBBFallthrough;
  for (MachineBasicBlock *BB : DirtyPreds)
    if (BB->getFallThrough(false) == NMBB)
      NMBBFallthrough.insert(BB);

  NMBB->removeSuccessor(MBB);
  for (MachineBasicBlock *SuccBB : DirtyPreds)
    SuccBB->ReplaceUsesOfBlockWith(NMBB, MBB);

  NMBB->erase(NMBB->begin(), NMBB->end());
  NMBB->eraseFromParent();

  for (MachineBasicBlock *BBToUpdate : NMBBFallthrough)
    updateTerminator(BBToUpdate, MBB, TII);
}

// A block is deemed fit for restore point split iff there exist
// 1. DirtyPreds - preds of CurRestore reachable from use or def of CSR/FI
// 2. CleanPreds - preds of CurRestore that arent DirtyPreds
bool ShrinkWrap::checkIfRestoreSplittable(
    const MachineBasicBlock *CurRestore,
    const DenseSet<const MachineBasicBlock *> &ReachableByDirty,
    SmallVectorImpl<MachineBasicBlock *> &DirtyPreds,
    SmallVectorImpl<MachineBasicBlock *> &CleanPreds,
    const TargetInstrInfo *TII, RegScavenger *RS) {
  for (const MachineInstr &MI : *CurRestore)
    if (useOrDefCSROrFI(MI, RS, /*StackAddressUsed=*/true))
      return false;

  for (MachineBasicBlock *PredBB : CurRestore->predecessors()) {
    if (!isAnalyzableBB(*TII, *PredBB))
      return false;

    if (ReachableByDirty.count(PredBB))
      DirtyPreds.push_back(PredBB);
    else
      CleanPreds.push_back(PredBB);
  }

  return !(CleanPreds.empty() || DirtyPreds.empty());
}

bool ShrinkWrap::postShrinkWrapping(bool HasCandidate, MachineFunction &MF,
                                    RegScavenger *RS) {
  if (!EnablePostShrinkWrapOpt)
    return false;

  MachineBasicBlock *InitSave = nullptr;
  MachineBasicBlock *InitRestore = nullptr;

  if (HasCandidate) {
    InitSave = Save;
    InitRestore = Restore;
  } else {
    InitRestore = nullptr;
    InitSave = &MF.front();
    for (MachineBasicBlock &MBB : MF) {
      if (MBB.isEHFuncletEntry())
        return false;
      if (MBB.isReturnBlock()) {
        // Do not support multiple restore points.
        if (InitRestore)
          return false;
        InitRestore = &MBB;
      }
    }
  }

  if (!InitSave || !InitRestore || InitRestore == InitSave ||
      !MDT->dominates(InitSave, InitRestore) ||
      !MPDT->dominates(InitRestore, InitSave))
    return false;

  // Bail out of the optimization if any of the basic block is target of
  // INLINEASM_BR instruction
  for (MachineBasicBlock &MBB : MF)
    if (MBB.isInlineAsmBrIndirectTarget())
      return false;

  DenseSet<const MachineBasicBlock *> DirtyBBs;
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.isEHPad()) {
      DirtyBBs.insert(&MBB);
      continue;
    }
    for (const MachineInstr &MI : MBB)
      if (useOrDefCSROrFI(MI, RS, /*StackAddressUsed=*/true)) {
        DirtyBBs.insert(&MBB);
        break;
      }
  }

  // Find blocks reachable from the use or def of CSRs/FI.
  DenseSet<const MachineBasicBlock *> ReachableByDirty;
  collectBlocksReachableByDirty(DirtyBBs, ReachableByDirty);

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  SmallVector<MachineBasicBlock *, 2> DirtyPreds;
  SmallVector<MachineBasicBlock *, 2> CleanPreds;
  if (!checkIfRestoreSplittable(InitRestore, ReachableByDirty, DirtyPreds,
                                CleanPreds, TII, RS))
    return false;

  // Trying to reach out to the new save point which dominates all dirty blocks.
  MachineBasicBlock *NewSave =
      FindIDom<>(**DirtyPreds.begin(), DirtyPreds, *MDT, false);

  while (NewSave && (hasDirtyPred(ReachableByDirty, *NewSave) ||
                     EntryFreq < MBFI->getBlockFreq(NewSave) ||
                     /*Entry freq has been observed more than a loop block in
                        some cases*/
                     MLI->getLoopFor(NewSave)))
    NewSave = FindIDom<>(**NewSave->pred_begin(), NewSave->predecessors(), *MDT,
                         false);

  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  if (!NewSave || NewSave == InitSave ||
      isSaveReachableThroughClean(NewSave, CleanPreds) ||
      !TFI->canUseAsPrologue(*NewSave))
    return false;

  // Now we know that splitting a restore point can isolate the restore point
  // from clean blocks and doing so can shrink the save point.
  MachineBasicBlock *NewRestore =
      tryToSplitRestore(InitRestore, DirtyPreds, TII);

  // Make sure if the new restore point is valid as an epilogue, depending on
  // targets.
  if (!TFI->canUseAsEpilogue(*NewRestore)) {
    rollbackRestoreSplit(MF, NewRestore, InitRestore, DirtyPreds, TII);
    return false;
  }

  Save = NewSave;
  Restore = NewRestore;

  MDT->recalculate(MF);
  MPDT->recalculate(MF);

  assert((MDT->dominates(Save, Restore) && MPDT->dominates(Restore, Save)) &&
         "Incorrect save or restore point due to dominance relations");
  assert((!MLI->getLoopFor(Save) && !MLI->getLoopFor(Restore)) &&
         "Unexpected save or restore point in a loop");
  assert((EntryFreq >= MBFI->getBlockFreq(Save) &&
          EntryFreq >= MBFI->getBlockFreq(Restore)) &&
         "Incorrect save or restore point based on block frequency");
  return true;
}

void ShrinkWrap::updateSaveRestorePoints(MachineBasicBlock &MBB,
                                         RegScavenger *RS) {
  // Get rid of the easy cases first.
  if (!Save)
    Save = &MBB;
  else
    Save = MDT->findNearestCommonDominator(Save, &MBB);
  assert(Save);

  if (!Restore)
    Restore = &MBB;
  else if (MPDT->getNode(&MBB)) // If the block is not in the post dom tree, it
                                // means the block never returns. If that's the
                                // case, we don't want to call
                                // `findNearestCommonDominator`, which will
                                // return `Restore`.
    Restore = MPDT->findNearestCommonDominator(Restore, &MBB);
  else
    Restore = nullptr; // Abort, we can't find a restore point in this case.

  // Make sure we would be able to insert the restore code before the
  // terminator.
  if (Restore == &MBB) {
    for (const MachineInstr &Terminator : MBB.terminators()) {
      if (!useOrDefCSROrFI(Terminator, RS, /*StackAddressUsed=*/true))
        continue;
      // One of the terminator needs to happen before the restore point.
      if (MBB.succ_empty()) {
        Restore = nullptr; // Abort, we can't find a restore point in this case.
        break;
      }
      // Look for a restore point that post-dominates all the successors.
      // The immediate post-dominator is what we are looking for.
      Restore = FindIDom<>(*Restore, Restore->successors(), *MPDT);
      break;
    }
  }

  if (!Restore) {
    LLVM_DEBUG(
        dbgs() << "Restore point needs to be spanned on several blocks\n");
    return;
  }

  // Make sure Save and Restore are suitable for shrink-wrapping:
  // 1. all path from Save needs to lead to Restore before exiting.
  // 2. all path to Restore needs to go through Save from Entry.
  // We achieve that by making sure that:
  // A. Save dominates Restore.
  // B. Restore post-dominates Save.
  // C. Save and Restore are in the same loop.
  bool SaveDominatesRestore = false;
  bool RestorePostDominatesSave = false;
  while (Restore &&
         (!(SaveDominatesRestore = MDT->dominates(Save, Restore)) ||
          !(RestorePostDominatesSave = MPDT->dominates(Restore, Save)) ||
          // Post-dominance is not enough in loops to ensure that all uses/defs
          // are after the prologue and before the epilogue at runtime.
          // E.g.,
          // while(1) {
          //  Save
          //  Restore
          //   if (...)
          //     break;
          //  use/def CSRs
          // }
          // All the uses/defs of CSRs are dominated by Save and post-dominated
          // by Restore. However, the CSRs uses are still reachable after
          // Restore and before Save are executed.
          //
          // For now, just push the restore/save points outside of loops.
          // FIXME: Refine the criteria to still find interesting cases
          // for loops.
          MLI->getLoopFor(Save) || MLI->getLoopFor(Restore))) {
    // Fix (A).
    if (!SaveDominatesRestore) {
      Save = MDT->findNearestCommonDominator(Save, Restore);
      continue;
    }
    // Fix (B).
    if (!RestorePostDominatesSave)
      Restore = MPDT->findNearestCommonDominator(Restore, Save);

    // Fix (C).
    if (Restore && (MLI->getLoopFor(Save) || MLI->getLoopFor(Restore))) {
      if (MLI->getLoopDepth(Save) > MLI->getLoopDepth(Restore)) {
        // Push Save outside of this loop if immediate dominator is different
        // from save block. If immediate dominator is not different, bail out.
        Save = FindIDom<>(*Save, Save->predecessors(), *MDT);
        if (!Save)
          break;
      } else {
        // If the loop does not exit, there is no point in looking
        // for a post-dominator outside the loop.
        SmallVector<MachineBasicBlock*, 4> ExitBlocks;
        MLI->getLoopFor(Restore)->getExitingBlocks(ExitBlocks);
        // Push Restore outside of this loop.
        // Look for the immediate post-dominator of the loop exits.
        MachineBasicBlock *IPdom = Restore;
        for (MachineBasicBlock *LoopExitBB: ExitBlocks) {
          IPdom = FindIDom<>(*IPdom, LoopExitBB->successors(), *MPDT);
          if (!IPdom)
            break;
        }
        // If the immediate post-dominator is not in a less nested loop,
        // then we are stuck in a program with an infinite loop.
        // In that case, we will not find a safe point, hence, bail out.
        if (IPdom && MLI->getLoopDepth(IPdom) < MLI->getLoopDepth(Restore))
          Restore = IPdom;
        else {
          Restore = nullptr;
          break;
        }
      }
    }
  }
}

static bool giveUpWithRemarks(MachineOptimizationRemarkEmitter *ORE,
                              StringRef RemarkName, StringRef RemarkMessage,
                              const DiagnosticLocation &Loc,
                              const MachineBasicBlock *MBB) {
  ORE->emit([&]() {
    return MachineOptimizationRemarkMissed(DEBUG_TYPE, RemarkName, Loc, MBB)
           << RemarkMessage;
  });

  LLVM_DEBUG(dbgs() << RemarkMessage << '\n');
  return false;
}

bool ShrinkWrap::performShrinkWrapping(
    const ReversePostOrderTraversal<MachineBasicBlock *> &RPOT,
    RegScavenger *RS) {
  for (MachineBasicBlock *MBB : RPOT) {
    LLVM_DEBUG(dbgs() << "Look into: " << printMBBReference(*MBB) << '\n');

    if (MBB->isEHFuncletEntry())
      return giveUpWithRemarks(ORE, "UnsupportedEHFunclets",
                               "EH Funclets are not supported yet.",
                               MBB->front().getDebugLoc(), MBB);

    if (MBB->isEHPad() || MBB->isInlineAsmBrIndirectTarget()) {
      // Push the prologue and epilogue outside of the region that may throw (or
      // jump out via inlineasm_br), by making sure that all the landing pads
      // are at least at the boundary of the save and restore points.  The
      // problem is that a basic block can jump out from the middle in these
      // cases, which we do not handle.
      updateSaveRestorePoints(*MBB, RS);
      if (!ArePointsInteresting()) {
        LLVM_DEBUG(dbgs() << "EHPad/inlineasm_br prevents shrink-wrapping\n");
        return false;
      }
      continue;
    }

    bool StackAddressUsed = false;
    // Check if we found any stack accesses in the predecessors. We are not
    // doing a full dataflow analysis here to keep things simple but just
    // rely on a reverse portorder traversal (RPOT) to guarantee predecessors
    // are already processed except for loops (and accept the conservative
    // result for loops).
    for (const MachineBasicBlock *Pred : MBB->predecessors()) {
      if (StackAddressUsedBlockInfo.test(Pred->getNumber())) {
        StackAddressUsed = true;
        break;
      }
    }

    for (const MachineInstr &MI : *MBB) {
      if (useOrDefCSROrFI(MI, RS, StackAddressUsed)) {
        // Save (resp. restore) point must dominate (resp. post dominate)
        // MI. Look for the proper basic block for those.
        updateSaveRestorePoints(*MBB, RS);
        // If we are at a point where we cannot improve the placement of
        // save/restore instructions, just give up.
        if (!ArePointsInteresting()) {
          LLVM_DEBUG(dbgs() << "No Shrink wrap candidate found\n");
          return false;
        }
        // No need to look for other instructions, this basic block
        // will already be part of the handled region.
        StackAddressUsed = true;
        break;
      }
    }
    StackAddressUsedBlockInfo[MBB->getNumber()] = StackAddressUsed;
  }
  if (!ArePointsInteresting()) {
    // If the points are not interesting at this point, then they must be null
    // because it means we did not encounter any frame/CSR related code.
    // Otherwise, we would have returned from the previous loop.
    assert(!Save && !Restore && "We miss a shrink-wrap opportunity?!");
    LLVM_DEBUG(dbgs() << "Nothing to shrink-wrap\n");
    return false;
  }

  LLVM_DEBUG(dbgs() << "\n ** Results **\nFrequency of the Entry: "
                    << EntryFreq.getFrequency() << '\n');

  const TargetFrameLowering *TFI =
      MachineFunc->getSubtarget().getFrameLowering();
  do {
    LLVM_DEBUG(dbgs() << "Shrink wrap candidates (#, Name, Freq):\nSave: "
                      << printMBBReference(*Save) << ' '
                      << printBlockFreq(*MBFI, *Save)
                      << "\nRestore: " << printMBBReference(*Restore) << ' '
                      << printBlockFreq(*MBFI, *Restore) << '\n');

    bool IsSaveCheap, TargetCanUseSaveAsPrologue = false;
    if (((IsSaveCheap = EntryFreq >= MBFI->getBlockFreq(Save)) &&
         EntryFreq >= MBFI->getBlockFreq(Restore)) &&
        ((TargetCanUseSaveAsPrologue = TFI->canUseAsPrologue(*Save)) &&
         TFI->canUseAsEpilogue(*Restore)))
      break;
    LLVM_DEBUG(
        dbgs() << "New points are too expensive or invalid for the target\n");
    MachineBasicBlock *NewBB;
    if (!IsSaveCheap || !TargetCanUseSaveAsPrologue) {
      Save = FindIDom<>(*Save, Save->predecessors(), *MDT);
      if (!Save)
        break;
      NewBB = Save;
    } else {
      // Restore is expensive.
      Restore = FindIDom<>(*Restore, Restore->successors(), *MPDT);
      if (!Restore)
        break;
      NewBB = Restore;
    }
    updateSaveRestorePoints(*NewBB, RS);
  } while (Save && Restore);

  if (!ArePointsInteresting()) {
    ++NumCandidatesDropped;
    return false;
  }
  return true;
}

bool ShrinkWrap::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()) || MF.empty() || !isShrinkWrapEnabled(MF))
    return false;

  LLVM_DEBUG(dbgs() << "**** Analysing " << MF.getName() << '\n');

  init(MF);

  ReversePostOrderTraversal<MachineBasicBlock *> RPOT(&*MF.begin());
  if (containsIrreducibleCFG<MachineBasicBlock *>(RPOT, *MLI)) {
    // If MF is irreducible, a block may be in a loop without
    // MachineLoopInfo reporting it. I.e., we may use the
    // post-dominance property in loops, which lead to incorrect
    // results. Moreover, we may miss that the prologue and
    // epilogue are not in the same loop, leading to unbalanced
    // construction/deconstruction of the stack frame.
    return giveUpWithRemarks(ORE, "UnsupportedIrreducibleCFG",
                             "Irreducible CFGs are not supported yet.",
                             MF.getFunction().getSubprogram(), &MF.front());
  }

  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  std::unique_ptr<RegScavenger> RS(
      TRI->requiresRegisterScavenging(MF) ? new RegScavenger() : nullptr);

  bool Changed = false;

  // Initially, conservatively assume that stack addresses can be used in each
  // basic block and change the state only for those basic blocks for which we
  // were able to prove the opposite.
  StackAddressUsedBlockInfo.resize(MF.getNumBlockIDs(), true);
  bool HasCandidate = performShrinkWrapping(RPOT, RS.get());
  StackAddressUsedBlockInfo.clear();
  Changed = postShrinkWrapping(HasCandidate, MF, RS.get());
  if (!HasCandidate && !Changed)
    return false;
  if (!ArePointsInteresting())
    return Changed;

  LLVM_DEBUG(dbgs() << "Final shrink wrap candidates:\nSave: "
                    << printMBBReference(*Save) << ' '
                    << "\nRestore: " << printMBBReference(*Restore) << '\n');

  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setSavePoint(Save);
  MFI.setRestorePoint(Restore);
  ++NumCandidates;
  return Changed;
}

bool ShrinkWrap::isShrinkWrapEnabled(const MachineFunction &MF) {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  switch (EnableShrinkWrapOpt) {
  case cl::BOU_UNSET:
    return TFI->enableShrinkWrapping(MF) &&
           // Windows with CFI has some limitations that make it impossible
           // to use shrink-wrapping.
           !MF.getTarget().getMCAsmInfo()->usesWindowsCFI() &&
           // Sanitizers look at the value of the stack at the location
           // of the crash. Since a crash can happen anywhere, the
           // frame must be lowered before anything else happen for the
           // sanitizers to be able to get a correct stack frame.
           !(MF.getFunction().hasFnAttribute(Attribute::SanitizeAddress) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeThread) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeMemory) ||
             MF.getFunction().hasFnAttribute(Attribute::SanitizeHWAddress));
  // If EnableShrinkWrap is set, it takes precedence on whatever the
  // target sets. The rational is that we assume we want to test
  // something related to shrink-wrapping.
  case cl::BOU_TRUE:
    return true;
  case cl::BOU_FALSE:
    return false;
  }
  llvm_unreachable("Invalid shrink-wrapping state");
}
