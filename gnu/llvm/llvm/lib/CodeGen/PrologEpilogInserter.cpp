//===- PrologEpilogInserter.cpp - Insert Prolog/Epilog code in function ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass is responsible for finalizing the functions frame layout, saving
// callee saved registers, and for emitting prolog & epilog code for the
// function.
//
// This pass must be run after register allocation.  After this pass is
// executed, it is illegal to construct MO_FrameIndex operands.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "prologepilog"

using MBBVector = SmallVector<MachineBasicBlock *, 4>;

STATISTIC(NumLeafFuncWithSpills, "Number of leaf functions with CSRs");
STATISTIC(NumFuncSeen, "Number of functions seen in PEI");


namespace {

class PEI : public MachineFunctionPass {
public:
  static char ID;

  PEI() : MachineFunctionPass(ID) {
    initializePEIPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// runOnMachineFunction - Insert prolog/epilog code and replace abstract
  /// frame indexes with appropriate references.
  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  RegScavenger *RS = nullptr;

  // MinCSFrameIndex, MaxCSFrameIndex - Keeps the range of callee saved
  // stack frame indexes.
  unsigned MinCSFrameIndex = std::numeric_limits<unsigned>::max();
  unsigned MaxCSFrameIndex = 0;

  // Save and Restore blocks of the current function. Typically there is a
  // single save block, unless Windows EH funclets are involved.
  MBBVector SaveBlocks;
  MBBVector RestoreBlocks;

  // Flag to control whether to use the register scavenger to resolve
  // frame index materialization registers. Set according to
  // TRI->requiresFrameIndexScavenging() for the current function.
  bool FrameIndexVirtualScavenging = false;

  // Flag to control whether the scavenger should be passed even though
  // FrameIndexVirtualScavenging is used.
  bool FrameIndexEliminationScavenging = false;

  // Emit remarks.
  MachineOptimizationRemarkEmitter *ORE = nullptr;

  void calculateCallFrameInfo(MachineFunction &MF);
  void calculateSaveRestoreBlocks(MachineFunction &MF);
  void spillCalleeSavedRegs(MachineFunction &MF);

  void calculateFrameObjectOffsets(MachineFunction &MF);
  void replaceFrameIndices(MachineFunction &MF);
  void replaceFrameIndices(MachineBasicBlock *BB, MachineFunction &MF,
                           int &SPAdj);
  // Frame indices in debug values are encoded in a target independent
  // way with simply the frame index and offset rather than any
  // target-specific addressing mode.
  bool replaceFrameIndexDebugInstr(MachineFunction &MF, MachineInstr &MI,
                                   unsigned OpIdx, int SPAdj = 0);
  // Does same as replaceFrameIndices but using the backward MIR walk and
  // backward register scavenger walk.
  void replaceFrameIndicesBackward(MachineFunction &MF);
  void replaceFrameIndicesBackward(MachineBasicBlock *BB, MachineFunction &MF,
                                   int &SPAdj);

  void insertPrologEpilogCode(MachineFunction &MF);
  void insertZeroCallUsedRegs(MachineFunction &MF);
};

} // end anonymous namespace

char PEI::ID = 0;

char &llvm::PrologEpilogCodeInserterID = PEI::ID;

INITIALIZE_PASS_BEGIN(PEI, DEBUG_TYPE, "Prologue/Epilogue Insertion", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineOptimizationRemarkEmitterPass)
INITIALIZE_PASS_END(PEI, DEBUG_TYPE,
                    "Prologue/Epilogue Insertion & Frame Finalization", false,
                    false)

MachineFunctionPass *llvm::createPrologEpilogInserterPass() {
  return new PEI();
}

STATISTIC(NumBytesStackSpace,
          "Number of bytes used for stack in all functions");

void PEI::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addPreserved<MachineLoopInfoWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<MachineOptimizationRemarkEmitterPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

/// StackObjSet - A set of stack object indexes
using StackObjSet = SmallSetVector<int, 8>;

using SavedDbgValuesMap =
    SmallDenseMap<MachineBasicBlock *, SmallVector<MachineInstr *, 4>, 4>;

/// Stash DBG_VALUEs that describe parameters and which are placed at the start
/// of the block. Later on, after the prologue code has been emitted, the
/// stashed DBG_VALUEs will be reinserted at the start of the block.
static void stashEntryDbgValues(MachineBasicBlock &MBB,
                                SavedDbgValuesMap &EntryDbgValues) {
  SmallVector<const MachineInstr *, 4> FrameIndexValues;

  for (auto &MI : MBB) {
    if (!MI.isDebugInstr())
      break;
    if (!MI.isDebugValue() || !MI.getDebugVariable()->isParameter())
      continue;
    if (any_of(MI.debug_operands(),
               [](const MachineOperand &MO) { return MO.isFI(); })) {
      // We can only emit valid locations for frame indices after the frame
      // setup, so do not stash away them.
      FrameIndexValues.push_back(&MI);
      continue;
    }
    const DILocalVariable *Var = MI.getDebugVariable();
    const DIExpression *Expr = MI.getDebugExpression();
    auto Overlaps = [Var, Expr](const MachineInstr *DV) {
      return Var == DV->getDebugVariable() &&
             Expr->fragmentsOverlap(DV->getDebugExpression());
    };
    // See if the debug value overlaps with any preceding debug value that will
    // not be stashed. If that is the case, then we can't stash this value, as
    // we would then reorder the values at reinsertion.
    if (llvm::none_of(FrameIndexValues, Overlaps))
      EntryDbgValues[&MBB].push_back(&MI);
  }

  // Remove stashed debug values from the block.
  if (EntryDbgValues.count(&MBB))
    for (auto *MI : EntryDbgValues[&MBB])
      MI->removeFromParent();
}

/// runOnMachineFunction - Insert prolog/epilog code and replace abstract
/// frame indexes with appropriate references.
bool PEI::runOnMachineFunction(MachineFunction &MF) {
  NumFuncSeen++;
  const Function &F = MF.getFunction();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const ReturnProtectorLowering *RPL = TFI->getReturnProtector();

  if (RPL)
      RPL->setupReturnProtector(MF);

  RS = TRI->requiresRegisterScavenging(MF) ? new RegScavenger() : nullptr;
  FrameIndexVirtualScavenging = TRI->requiresFrameIndexScavenging(MF);
  ORE = &getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE();

  // Calculate the MaxCallFrameSize value for the function's frame
  // information. Also eliminates call frame pseudo instructions.
  calculateCallFrameInfo(MF);

  // Determine placement of CSR spill/restore code and prolog/epilog code:
  // place all spills in the entry block, all restores in return blocks.
  calculateSaveRestoreBlocks(MF);

  // Stash away DBG_VALUEs that should not be moved by insertion of prolog code.
  SavedDbgValuesMap EntryDbgValues;
  for (MachineBasicBlock *SaveBlock : SaveBlocks)
    stashEntryDbgValues(*SaveBlock, EntryDbgValues);

  // Handle CSR spilling and restoring, for targets that need it.
  if (MF.getTarget().usesPhysRegsForValues())
    spillCalleeSavedRegs(MF);

  // Allow the target machine to make final modifications to the function
  // before the frame layout is finalized.
  TFI->processFunctionBeforeFrameFinalized(MF, RS);

  // Calculate actual frame offsets for all abstract stack objects...
  calculateFrameObjectOffsets(MF);

  // Add prolog and epilog code to the function.  This function is required
  // to align the stack frame as necessary for any stack variables or
  // called functions.  Because of this, calculateCalleeSavedRegisters()
  // must be called before this function in order to set the AdjustsStack
  // and MaxCallFrameSize variables.
  if (!F.hasFnAttribute(Attribute::Naked))
    insertPrologEpilogCode(MF);

  // Add Return Protectors if using them
  if (RPL)
      RPL->insertReturnProtectors(MF);

  // Reinsert stashed debug values at the start of the entry blocks.
  for (auto &I : EntryDbgValues)
    I.first->insert(I.first->begin(), I.second.begin(), I.second.end());

  // Allow the target machine to make final modifications to the function
  // before the frame layout is finalized.
  TFI->processFunctionBeforeFrameIndicesReplaced(MF, RS);

  // Replace all MO_FrameIndex operands with physical register references
  // and actual offsets.
  if (TFI->needsFrameIndexResolution(MF)) {
    // Allow the target to determine this after knowing the frame size.
    FrameIndexEliminationScavenging =
        (RS && !FrameIndexVirtualScavenging) ||
        TRI->requiresFrameIndexReplacementScavenging(MF);

    if (TRI->eliminateFrameIndicesBackwards())
      replaceFrameIndicesBackward(MF);
    else
      replaceFrameIndices(MF);
  }

  // If register scavenging is needed, as we've enabled doing it as a
  // post-pass, scavenge the virtual registers that frame index elimination
  // inserted.
  if (TRI->requiresRegisterScavenging(MF) && FrameIndexVirtualScavenging)
    scavengeFrameVirtualRegs(MF, *RS);

  // Warn on stack size when we exceeds the given limit.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  uint64_t StackSize = MFI.getStackSize();

  uint64_t Threshold = TFI->getStackThreshold();
  if (MF.getFunction().hasFnAttribute("warn-stack-size")) {
    bool Failed = MF.getFunction()
                      .getFnAttribute("warn-stack-size")
                      .getValueAsString()
                      .getAsInteger(10, Threshold);
    // Verifier should have caught this.
    assert(!Failed && "Invalid warn-stack-size fn attr value");
    (void)Failed;
  }
  uint64_t UnsafeStackSize = MFI.getUnsafeStackSize();
  if (MF.getFunction().hasFnAttribute(Attribute::SafeStack))
    StackSize += UnsafeStackSize;

  if (StackSize > Threshold) {
    DiagnosticInfoStackSize DiagStackSize(F, StackSize, Threshold, DS_Warning);
    F.getContext().diagnose(DiagStackSize);
    int64_t SpillSize = 0;
    for (int Idx = MFI.getObjectIndexBegin(), End = MFI.getObjectIndexEnd();
         Idx != End; ++Idx) {
      if (MFI.isSpillSlotObjectIndex(Idx))
        SpillSize += MFI.getObjectSize(Idx);
    }

    [[maybe_unused]] float SpillPct =
        static_cast<float>(SpillSize) / static_cast<float>(StackSize);
    LLVM_DEBUG(
        dbgs() << formatv("{0}/{1} ({3:P}) spills, {2}/{1} ({4:P}) variables",
                          SpillSize, StackSize, StackSize - SpillSize, SpillPct,
                          1.0f - SpillPct));
    if (UnsafeStackSize != 0) {
      LLVM_DEBUG(dbgs() << formatv(", {0}/{2} ({1:P}) unsafe stack",
                                   UnsafeStackSize,
                                   static_cast<float>(UnsafeStackSize) /
                                       static_cast<float>(StackSize),
                                   StackSize));
    }
    LLVM_DEBUG(dbgs() << "\n");
  }

  ORE->emit([&]() {
    return MachineOptimizationRemarkAnalysis(DEBUG_TYPE, "StackSize",
                                             MF.getFunction().getSubprogram(),
                                             &MF.front())
           << ore::NV("NumStackBytes", StackSize)
           << " stack bytes in function '"
           << ore::NV("Function", MF.getFunction().getName()) << "'";
  });

  // Emit any remarks implemented for the target, based on final frame layout.
  TFI->emitRemarks(MF, ORE);

  delete RS;
  SaveBlocks.clear();
  RestoreBlocks.clear();
  MFI.setSavePoint(nullptr);
  MFI.setRestorePoint(nullptr);
  return true;
}

/// Calculate the MaxCallFrameSize variable for the function's frame
/// information and eliminate call frame pseudo instructions.
void PEI::calculateCallFrameInfo(MachineFunction &MF) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Get the function call frame set-up and tear-down instruction opcode
  unsigned FrameSetupOpcode = TII.getCallFrameSetupOpcode();
  unsigned FrameDestroyOpcode = TII.getCallFrameDestroyOpcode();

  // Early exit for targets which have no call frame setup/destroy pseudo
  // instructions.
  if (FrameSetupOpcode == ~0u && FrameDestroyOpcode == ~0u)
    return;

  // (Re-)Compute the MaxCallFrameSize.
  [[maybe_unused]] uint64_t MaxCFSIn =
      MFI.isMaxCallFrameSizeComputed() ? MFI.getMaxCallFrameSize() : UINT64_MAX;
  std::vector<MachineBasicBlock::iterator> FrameSDOps;
  MFI.computeMaxCallFrameSize(MF, &FrameSDOps);
  assert(MFI.getMaxCallFrameSize() <= MaxCFSIn &&
         "Recomputing MaxCFS gave a larger value.");
  assert((FrameSDOps.empty() || MF.getFrameInfo().adjustsStack()) &&
         "AdjustsStack not set in presence of a frame pseudo instruction.");

  if (TFI->canSimplifyCallFramePseudos(MF)) {
    // If call frames are not being included as part of the stack frame, and
    // the target doesn't indicate otherwise, remove the call frame pseudos
    // here. The sub/add sp instruction pairs are still inserted, but we don't
    // need to track the SP adjustment for frame index elimination.
    for (MachineBasicBlock::iterator I : FrameSDOps)
      TFI->eliminateCallFramePseudoInstr(MF, *I->getParent(), I);

    // We can't track the call frame size after call frame pseudos have been
    // eliminated. Set it to zero everywhere to keep MachineVerifier happy.
    for (MachineBasicBlock &MBB : MF)
      MBB.setCallFrameSize(0);
  }
}

/// Compute the sets of entry and return blocks for saving and restoring
/// callee-saved registers, and placing prolog and epilog code.
void PEI::calculateSaveRestoreBlocks(MachineFunction &MF) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const ReturnProtectorLowering *RPL = TFI->getReturnProtector();

  // Even when we do not change any CSR, we still want to insert the
  // prologue and epilogue of the function.
  // So set the save points for those.

  // Use the points found by shrink-wrapping, if any.
  if (MFI.getSavePoint()) {
    SaveBlocks.push_back(MFI.getSavePoint());
    assert(MFI.getRestorePoint() && "Both restore and save must be set");
    MachineBasicBlock *RestoreBlock = MFI.getRestorePoint();
    // If RestoreBlock does not have any successor and is not a return block
    // then the end point is unreachable and we do not need to insert any
    // epilogue.
    if (!RestoreBlock->succ_empty() || RestoreBlock->isReturnBlock())
      RestoreBlocks.push_back(RestoreBlock);

    // If we are adding return protectors ensure we can find a free register
    if (RPL &&
       !RPL->determineReturnProtectorRegister(MF, SaveBlocks, RestoreBlocks)) {
      // Shrinkwrapping will prevent finding a free register
      SaveBlocks.clear();
      RestoreBlocks.clear();
      MFI.setSavePoint(nullptr);
      MFI.setRestorePoint(nullptr);
    } else {
      return;
    }
  }

  // Save refs to entry and return blocks.
  SaveBlocks.push_back(&MF.front());
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.isEHFuncletEntry())
      SaveBlocks.push_back(&MBB);
    if (MBB.isReturnBlock())
      RestoreBlocks.push_back(&MBB);
  }

  if (RPL)
    RPL->determineReturnProtectorRegister(MF, SaveBlocks, RestoreBlocks);
}

static void assignCalleeSavedSpillSlots(MachineFunction &F,
                                        const BitVector &SavedRegs,
                                        unsigned &MinCSFrameIndex,
                                        unsigned &MaxCSFrameIndex) {
  if (SavedRegs.empty())
    return;

  const TargetRegisterInfo *RegInfo = F.getSubtarget().getRegisterInfo();
  const MCPhysReg *CSRegs = F.getRegInfo().getCalleeSavedRegs();
  BitVector CSMask(SavedRegs.size());

  for (unsigned i = 0; CSRegs[i]; ++i)
    CSMask.set(CSRegs[i]);

  std::vector<CalleeSavedInfo> CSI;
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];
    if (SavedRegs.test(Reg)) {
      bool SavedSuper = false;
      for (const MCPhysReg &SuperReg : RegInfo->superregs(Reg)) {
        // Some backends set all aliases for some registers as saved, such as
        // Mips's $fp, so they appear in SavedRegs but not CSRegs.
        if (SavedRegs.test(SuperReg) && CSMask.test(SuperReg)) {
          SavedSuper = true;
          break;
        }
      }

      if (!SavedSuper)
        CSI.push_back(CalleeSavedInfo(Reg));
    }
  }

  const TargetFrameLowering *TFI = F.getSubtarget().getFrameLowering();
  MachineFrameInfo &MFI = F.getFrameInfo();

  if (TFI->getReturnProtector())
      TFI->getReturnProtector()->saveReturnProtectorRegister(F, CSI);

  if (!TFI->assignCalleeSavedSpillSlots(F, RegInfo, CSI, MinCSFrameIndex,
                                        MaxCSFrameIndex)) {
    // If target doesn't implement this, use generic code.

    if (CSI.empty())
      return; // Early exit if no callee saved registers are modified!

    unsigned NumFixedSpillSlots;
    const TargetFrameLowering::SpillSlot *FixedSpillSlots =
        TFI->getCalleeSavedSpillSlots(NumFixedSpillSlots);

    // Now that we know which registers need to be saved and restored, allocate
    // stack slots for them.
    for (auto &CS : CSI) {
      // If the target has spilled this register to another register, we don't
      // need to allocate a stack slot.
      if (CS.isSpilledToReg())
        continue;

      unsigned Reg = CS.getReg();
      const TargetRegisterClass *RC = RegInfo->getMinimalPhysRegClass(Reg);

      int FrameIdx;
      if (RegInfo->hasReservedSpillSlot(F, Reg, FrameIdx)) {
        CS.setFrameIdx(FrameIdx);
        continue;
      }

      // Check to see if this physreg must be spilled to a particular stack slot
      // on this target.
      const TargetFrameLowering::SpillSlot *FixedSlot = FixedSpillSlots;
      while (FixedSlot != FixedSpillSlots + NumFixedSpillSlots &&
             FixedSlot->Reg != Reg)
        ++FixedSlot;

      unsigned Size = RegInfo->getSpillSize(*RC);
      if (FixedSlot == FixedSpillSlots + NumFixedSpillSlots) {
        // Nope, just spill it anywhere convenient.
        Align Alignment = RegInfo->getSpillAlign(*RC);
        // We may not be able to satisfy the desired alignment specification of
        // the TargetRegisterClass if the stack alignment is smaller. Use the
        // min.
        Alignment = std::min(Alignment, TFI->getStackAlign());
        FrameIdx = MFI.CreateStackObject(Size, Alignment, true);
        if ((unsigned)FrameIdx < MinCSFrameIndex) MinCSFrameIndex = FrameIdx;
        if ((unsigned)FrameIdx > MaxCSFrameIndex) MaxCSFrameIndex = FrameIdx;
      } else {
        // Spill it to the stack where we must.
        FrameIdx = MFI.CreateFixedSpillStackObject(Size, FixedSlot->Offset);
      }

      CS.setFrameIdx(FrameIdx);
    }
  }

  MFI.setCalleeSavedInfo(CSI);
}

/// Helper function to update the liveness information for the callee-saved
/// registers.
static void updateLiveness(MachineFunction &MF) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  // Visited will contain all the basic blocks that are in the region
  // where the callee saved registers are alive:
  // - Anything that is not Save or Restore -> LiveThrough.
  // - Save -> LiveIn.
  // - Restore -> LiveOut.
  // The live-out is not attached to the block, so no need to keep
  // Restore in this set.
  SmallPtrSet<MachineBasicBlock *, 8> Visited;
  SmallVector<MachineBasicBlock *, 8> WorkList;
  MachineBasicBlock *Entry = &MF.front();
  MachineBasicBlock *Save = MFI.getSavePoint();

  if (!Save)
    Save = Entry;

  if (Entry != Save) {
    WorkList.push_back(Entry);
    Visited.insert(Entry);
  }
  Visited.insert(Save);

  MachineBasicBlock *Restore = MFI.getRestorePoint();
  if (Restore)
    // By construction Restore cannot be visited, otherwise it
    // means there exists a path to Restore that does not go
    // through Save.
    WorkList.push_back(Restore);

  while (!WorkList.empty()) {
    const MachineBasicBlock *CurBB = WorkList.pop_back_val();
    // By construction, the region that is after the save point is
    // dominated by the Save and post-dominated by the Restore.
    if (CurBB == Save && Save != Restore)
      continue;
    // Enqueue all the successors not already visited.
    // Those are by construction either before Save or after Restore.
    for (MachineBasicBlock *SuccBB : CurBB->successors())
      if (Visited.insert(SuccBB).second)
        WorkList.push_back(SuccBB);
  }

  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  MachineRegisterInfo &MRI = MF.getRegInfo();
  for (const CalleeSavedInfo &I : CSI) {
    for (MachineBasicBlock *MBB : Visited) {
      MCPhysReg Reg = I.getReg();
      // Add the callee-saved register as live-in.
      // It's killed at the spill.
      if (!MRI.isReserved(Reg) && !MBB->isLiveIn(Reg))
        MBB->addLiveIn(Reg);
    }
    // If callee-saved register is spilled to another register rather than
    // spilling to stack, the destination register has to be marked as live for
    // each MBB between the prologue and epilogue so that it is not clobbered
    // before it is reloaded in the epilogue. The Visited set contains all
    // blocks outside of the region delimited by prologue/epilogue.
    if (I.isSpilledToReg()) {
      for (MachineBasicBlock &MBB : MF) {
        if (Visited.count(&MBB))
          continue;
        MCPhysReg DstReg = I.getDstReg();
        if (!MBB.isLiveIn(DstReg))
          MBB.addLiveIn(DstReg);
      }
    }
  }
}

/// Insert spill code for the callee-saved registers used in the function.
static void insertCSRSaves(MachineBasicBlock &SaveBlock,
                           ArrayRef<CalleeSavedInfo> CSI) {
  MachineFunction &MF = *SaveBlock.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  MachineBasicBlock::iterator I = SaveBlock.begin();
  if (!TFI->spillCalleeSavedRegisters(SaveBlock, I, CSI, TRI)) {
    for (const CalleeSavedInfo &CS : CSI) {
      // Insert the spill to the stack frame.
      unsigned Reg = CS.getReg();

      if (CS.isSpilledToReg()) {
        BuildMI(SaveBlock, I, DebugLoc(),
                TII.get(TargetOpcode::COPY), CS.getDstReg())
          .addReg(Reg, getKillRegState(true));
      } else {
        const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
        TII.storeRegToStackSlot(SaveBlock, I, Reg, true, CS.getFrameIdx(), RC,
                                TRI, Register());
      }
    }
  }
}

/// Insert restore code for the callee-saved registers used in the function.
static void insertCSRRestores(MachineBasicBlock &RestoreBlock,
                              std::vector<CalleeSavedInfo> &CSI) {
  MachineFunction &MF = *RestoreBlock.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  // Restore all registers immediately before the return and any
  // terminators that precede it.
  MachineBasicBlock::iterator I = RestoreBlock.getFirstTerminator();

  if (!TFI->restoreCalleeSavedRegisters(RestoreBlock, I, CSI, TRI)) {
    for (const CalleeSavedInfo &CI : reverse(CSI)) {
      unsigned Reg = CI.getReg();
      if (CI.isSpilledToReg()) {
        BuildMI(RestoreBlock, I, DebugLoc(), TII.get(TargetOpcode::COPY), Reg)
          .addReg(CI.getDstReg(), getKillRegState(true));
      } else {
        const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
        TII.loadRegFromStackSlot(RestoreBlock, I, Reg, CI.getFrameIdx(), RC,
                                 TRI, Register());
        assert(I != RestoreBlock.begin() &&
               "loadRegFromStackSlot didn't insert any code!");
        // Insert in reverse order.  loadRegFromStackSlot can insert
        // multiple instructions.
      }
    }
  }
}

void PEI::spillCalleeSavedRegs(MachineFunction &MF) {
  // We can't list this requirement in getRequiredProperties because some
  // targets (WebAssembly) use virtual registers past this point, and the pass
  // pipeline is set up without giving the passes a chance to look at the
  // TargetMachine.
  // FIXME: Find a way to express this in getRequiredProperties.
  assert(MF.getProperties().hasProperty(
      MachineFunctionProperties::Property::NoVRegs));

  const Function &F = MF.getFunction();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MinCSFrameIndex = std::numeric_limits<unsigned>::max();
  MaxCSFrameIndex = 0;

  // Determine which of the registers in the callee save list should be saved.
  BitVector SavedRegs;
  TFI->determineCalleeSaves(MF, SavedRegs, RS);

  // Assign stack slots for any callee-saved registers that must be spilled.
  assignCalleeSavedSpillSlots(MF, SavedRegs, MinCSFrameIndex, MaxCSFrameIndex);

  // Add the code to save and restore the callee saved registers.
  if (!F.hasFnAttribute(Attribute::Naked)) {
    MFI.setCalleeSavedInfoValid(true);

    std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
    if (!CSI.empty()) {
      if (!MFI.hasCalls())
        NumLeafFuncWithSpills++;

      for (MachineBasicBlock *SaveBlock : SaveBlocks)
        insertCSRSaves(*SaveBlock, CSI);

      // Update the live-in information of all the blocks up to the save point.
      updateLiveness(MF);

      for (MachineBasicBlock *RestoreBlock : RestoreBlocks)
        insertCSRRestores(*RestoreBlock, CSI);
    }
  }
}

/// AdjustStackOffset - Helper function used to adjust the stack frame offset.
static inline void AdjustStackOffset(MachineFrameInfo &MFI, int FrameIdx,
                                     bool StackGrowsDown, int64_t &Offset,
                                     Align &MaxAlign) {
  // If the stack grows down, add the object size to find the lowest address.
  if (StackGrowsDown)
    Offset += MFI.getObjectSize(FrameIdx);

  Align Alignment = MFI.getObjectAlign(FrameIdx);

  // If the alignment of this object is greater than that of the stack, then
  // increase the stack alignment to match.
  MaxAlign = std::max(MaxAlign, Alignment);

  // Adjust to alignment boundary.
  Offset = alignTo(Offset, Alignment);

  if (StackGrowsDown) {
    LLVM_DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") at SP[" << -Offset
                      << "]\n");
    MFI.setObjectOffset(FrameIdx, -Offset); // Set the computed offset
  } else {
    LLVM_DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") at SP[" << Offset
                      << "]\n");
    MFI.setObjectOffset(FrameIdx, Offset);
    Offset += MFI.getObjectSize(FrameIdx);
  }
}

/// Compute which bytes of fixed and callee-save stack area are unused and keep
/// track of them in StackBytesFree.
static inline void
computeFreeStackSlots(MachineFrameInfo &MFI, bool StackGrowsDown,
                      unsigned MinCSFrameIndex, unsigned MaxCSFrameIndex,
                      int64_t FixedCSEnd, BitVector &StackBytesFree) {
  // Avoid undefined int64_t -> int conversion below in extreme case.
  if (FixedCSEnd > std::numeric_limits<int>::max())
    return;

  StackBytesFree.resize(FixedCSEnd, true);

  SmallVector<int, 16> AllocatedFrameSlots;
  // Add fixed objects.
  for (int i = MFI.getObjectIndexBegin(); i != 0; ++i)
    // StackSlot scavenging is only implemented for the default stack.
    if (MFI.getStackID(i) == TargetStackID::Default)
      AllocatedFrameSlots.push_back(i);
  // Add callee-save objects if there are any.
  if (MinCSFrameIndex <= MaxCSFrameIndex) {
    for (int i = MinCSFrameIndex; i <= (int)MaxCSFrameIndex; ++i)
      if (MFI.getStackID(i) == TargetStackID::Default)
        AllocatedFrameSlots.push_back(i);
  }

  for (int i : AllocatedFrameSlots) {
    // These are converted from int64_t, but they should always fit in int
    // because of the FixedCSEnd check above.
    int ObjOffset = MFI.getObjectOffset(i);
    int ObjSize = MFI.getObjectSize(i);
    int ObjStart, ObjEnd;
    if (StackGrowsDown) {
      // ObjOffset is negative when StackGrowsDown is true.
      ObjStart = -ObjOffset - ObjSize;
      ObjEnd = -ObjOffset;
    } else {
      ObjStart = ObjOffset;
      ObjEnd = ObjOffset + ObjSize;
    }
    // Ignore fixed holes that are in the previous stack frame.
    if (ObjEnd > 0)
      StackBytesFree.reset(ObjStart, ObjEnd);
  }
}

/// Assign frame object to an unused portion of the stack in the fixed stack
/// object range.  Return true if the allocation was successful.
static inline bool scavengeStackSlot(MachineFrameInfo &MFI, int FrameIdx,
                                     bool StackGrowsDown, Align MaxAlign,
                                     BitVector &StackBytesFree) {
  if (MFI.isVariableSizedObjectIndex(FrameIdx))
    return false;

  if (StackBytesFree.none()) {
    // clear it to speed up later scavengeStackSlot calls to
    // StackBytesFree.none()
    StackBytesFree.clear();
    return false;
  }

  Align ObjAlign = MFI.getObjectAlign(FrameIdx);
  if (ObjAlign > MaxAlign)
    return false;

  int64_t ObjSize = MFI.getObjectSize(FrameIdx);
  int FreeStart;
  for (FreeStart = StackBytesFree.find_first(); FreeStart != -1;
       FreeStart = StackBytesFree.find_next(FreeStart)) {

    // Check that free space has suitable alignment.
    unsigned ObjStart = StackGrowsDown ? FreeStart + ObjSize : FreeStart;
    if (alignTo(ObjStart, ObjAlign) != ObjStart)
      continue;

    if (FreeStart + ObjSize > StackBytesFree.size())
      return false;

    bool AllBytesFree = true;
    for (unsigned Byte = 0; Byte < ObjSize; ++Byte)
      if (!StackBytesFree.test(FreeStart + Byte)) {
        AllBytesFree = false;
        break;
      }
    if (AllBytesFree)
      break;
  }

  if (FreeStart == -1)
    return false;

  if (StackGrowsDown) {
    int ObjStart = -(FreeStart + ObjSize);
    LLVM_DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") scavenged at SP["
                      << ObjStart << "]\n");
    MFI.setObjectOffset(FrameIdx, ObjStart);
  } else {
    LLVM_DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") scavenged at SP["
                      << FreeStart << "]\n");
    MFI.setObjectOffset(FrameIdx, FreeStart);
  }

  StackBytesFree.reset(FreeStart, FreeStart + ObjSize);
  return true;
}

/// AssignProtectedObjSet - Helper function to assign large stack objects (i.e.,
/// those required to be close to the Stack Protector) to stack offsets.
static void AssignProtectedObjSet(const StackObjSet &UnassignedObjs,
                                  SmallSet<int, 16> &ProtectedObjs,
                                  MachineFrameInfo &MFI, bool StackGrowsDown,
                                  int64_t &Offset, Align &MaxAlign) {

  for (int i : UnassignedObjs) {
    AdjustStackOffset(MFI, i, StackGrowsDown, Offset, MaxAlign);
    ProtectedObjs.insert(i);
  }
}

/// calculateFrameObjectOffsets - Calculate actual frame offsets for all of the
/// abstract stack objects.
void PEI::calculateFrameObjectOffsets(MachineFunction &MF) {
  const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();

  bool StackGrowsDown =
    TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsDown;

  // Loop over all of the stack objects, assigning sequential addresses...
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Start at the beginning of the local area.
  // The Offset is the distance from the stack top in the direction
  // of stack growth -- so it's always nonnegative.
  int LocalAreaOffset = TFI.getOffsetOfLocalArea();
  if (StackGrowsDown)
    LocalAreaOffset = -LocalAreaOffset;
  assert(LocalAreaOffset >= 0
         && "Local area offset should be in direction of stack growth");
  int64_t Offset = LocalAreaOffset;

#ifdef EXPENSIVE_CHECKS
  for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i)
    if (!MFI.isDeadObjectIndex(i) &&
        MFI.getStackID(i) == TargetStackID::Default)
      assert(MFI.getObjectAlign(i) <= MFI.getMaxAlign() &&
             "MaxAlignment is invalid");
#endif

  // If there are fixed sized objects that are preallocated in the local area,
  // non-fixed objects can't be allocated right at the start of local area.
  // Adjust 'Offset' to point to the end of last fixed sized preallocated
  // object.
  for (int i = MFI.getObjectIndexBegin(); i != 0; ++i) {
    // Only allocate objects on the default stack.
    if (MFI.getStackID(i) != TargetStackID::Default)
      continue;

    int64_t FixedOff;
    if (StackGrowsDown) {
      // The maximum distance from the stack pointer is at lower address of
      // the object -- which is given by offset. For down growing stack
      // the offset is negative, so we negate the offset to get the distance.
      FixedOff = -MFI.getObjectOffset(i);
    } else {
      // The maximum distance from the start pointer is at the upper
      // address of the object.
      FixedOff = MFI.getObjectOffset(i) + MFI.getObjectSize(i);
    }
    if (FixedOff > Offset) Offset = FixedOff;
  }

  Align MaxAlign = MFI.getMaxAlign();
  // First assign frame offsets to stack objects that are used to spill
  // callee saved registers.
  if (MaxCSFrameIndex >= MinCSFrameIndex) {
    for (unsigned i = 0; i <= MaxCSFrameIndex - MinCSFrameIndex; ++i) {
      unsigned FrameIndex =
          StackGrowsDown ? MinCSFrameIndex + i : MaxCSFrameIndex - i;

      // Only allocate objects on the default stack.
      if (MFI.getStackID(FrameIndex) != TargetStackID::Default)
        continue;

      // TODO: should this just be if (MFI.isDeadObjectIndex(FrameIndex))
      if (!StackGrowsDown && MFI.isDeadObjectIndex(FrameIndex))
        continue;

      AdjustStackOffset(MFI, FrameIndex, StackGrowsDown, Offset, MaxAlign);
    }
  }

  assert(MaxAlign == MFI.getMaxAlign() &&
         "MFI.getMaxAlign should already account for all callee-saved "
         "registers without a fixed stack slot");

  // FixedCSEnd is the stack offset to the end of the fixed and callee-save
  // stack area.
  int64_t FixedCSEnd = Offset;

  // Make sure the special register scavenging spill slot is closest to the
  // incoming stack pointer if a frame pointer is required and is closer
  // to the incoming rather than the final stack pointer.
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  bool EarlyScavengingSlots = TFI.allocateScavengingFrameIndexesNearIncomingSP(MF);
  if (RS && EarlyScavengingSlots) {
    SmallVector<int, 2> SFIs;
    RS->getScavengingFrameIndices(SFIs);
    for (int SFI : SFIs)
      AdjustStackOffset(MFI, SFI, StackGrowsDown, Offset, MaxAlign);
  }

  // FIXME: Once this is working, then enable flag will change to a target
  // check for whether the frame is large enough to want to use virtual
  // frame index registers. Functions which don't want/need this optimization
  // will continue to use the existing code path.
  if (MFI.getUseLocalStackAllocationBlock()) {
    Align Alignment = MFI.getLocalFrameMaxAlign();

    // Adjust to alignment boundary.
    Offset = alignTo(Offset, Alignment);

    LLVM_DEBUG(dbgs() << "Local frame base offset: " << Offset << "\n");

    // Resolve offsets for objects in the local block.
    for (unsigned i = 0, e = MFI.getLocalFrameObjectCount(); i != e; ++i) {
      std::pair<int, int64_t> Entry = MFI.getLocalFrameObjectMap(i);
      int64_t FIOffset = (StackGrowsDown ? -Offset : Offset) + Entry.second;
      LLVM_DEBUG(dbgs() << "alloc FI(" << Entry.first << ") at SP[" << FIOffset
                        << "]\n");
      MFI.setObjectOffset(Entry.first, FIOffset);
    }
    // Allocate the local block
    Offset += MFI.getLocalFrameSize();

    MaxAlign = std::max(Alignment, MaxAlign);
  }

  // Retrieve the Exception Handler registration node.
  int EHRegNodeFrameIndex = std::numeric_limits<int>::max();
  if (const WinEHFuncInfo *FuncInfo = MF.getWinEHFuncInfo())
    EHRegNodeFrameIndex = FuncInfo->EHRegNodeFrameIndex;

  // Make sure that the stack protector comes before the local variables on the
  // stack.
  SmallSet<int, 16> ProtectedObjs;
  if (MFI.hasStackProtectorIndex()) {
    int StackProtectorFI = MFI.getStackProtectorIndex();
    StackObjSet LargeArrayObjs;
    StackObjSet SmallArrayObjs;
    StackObjSet AddrOfObjs;

    // If we need a stack protector, we need to make sure that
    // LocalStackSlotPass didn't already allocate a slot for it.
    // If we are told to use the LocalStackAllocationBlock, the stack protector
    // is expected to be already pre-allocated.
    if (MFI.getStackID(StackProtectorFI) != TargetStackID::Default) {
      // If the stack protector isn't on the default stack then it's up to the
      // target to set the stack offset.
      assert(MFI.getObjectOffset(StackProtectorFI) != 0 &&
             "Offset of stack protector on non-default stack expected to be "
             "already set.");
      assert(!MFI.isObjectPreAllocated(MFI.getStackProtectorIndex()) &&
             "Stack protector on non-default stack expected to not be "
             "pre-allocated by LocalStackSlotPass.");
    } else if (!MFI.getUseLocalStackAllocationBlock()) {
      AdjustStackOffset(MFI, StackProtectorFI, StackGrowsDown, Offset,
                        MaxAlign);
    } else if (!MFI.isObjectPreAllocated(MFI.getStackProtectorIndex())) {
      llvm_unreachable(
          "Stack protector not pre-allocated by LocalStackSlotPass.");
    }

    // Assign large stack objects first.
    for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
      if (MFI.isObjectPreAllocated(i) && MFI.getUseLocalStackAllocationBlock())
        continue;
      if (i >= MinCSFrameIndex && i <= MaxCSFrameIndex)
        continue;
      if (RS && RS->isScavengingFrameIndex((int)i))
        continue;
      if (MFI.isDeadObjectIndex(i))
        continue;
      if (StackProtectorFI == (int)i || EHRegNodeFrameIndex == (int)i)
        continue;
      // Only allocate objects on the default stack.
      if (MFI.getStackID(i) != TargetStackID::Default)
        continue;

      switch (MFI.getObjectSSPLayout(i)) {
      case MachineFrameInfo::SSPLK_None:
        continue;
      case MachineFrameInfo::SSPLK_SmallArray:
        SmallArrayObjs.insert(i);
        continue;
      case MachineFrameInfo::SSPLK_AddrOf:
        AddrOfObjs.insert(i);
        continue;
      case MachineFrameInfo::SSPLK_LargeArray:
        LargeArrayObjs.insert(i);
        continue;
      }
      llvm_unreachable("Unexpected SSPLayoutKind.");
    }

    // We expect **all** the protected stack objects to be pre-allocated by
    // LocalStackSlotPass. If it turns out that PEI still has to allocate some
    // of them, we may end up messing up the expected order of the objects.
    if (MFI.getUseLocalStackAllocationBlock() &&
        !(LargeArrayObjs.empty() && SmallArrayObjs.empty() &&
          AddrOfObjs.empty()))
      llvm_unreachable("Found protected stack objects not pre-allocated by "
                       "LocalStackSlotPass.");

    AssignProtectedObjSet(LargeArrayObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign);
    AssignProtectedObjSet(SmallArrayObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign);
    AssignProtectedObjSet(AddrOfObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign);
  }

  SmallVector<int, 8> ObjectsToAllocate;

  // Then prepare to assign frame offsets to stack objects that are not used to
  // spill callee saved registers.
  for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
    if (MFI.isObjectPreAllocated(i) && MFI.getUseLocalStackAllocationBlock())
      continue;
    if (i >= MinCSFrameIndex && i <= MaxCSFrameIndex)
      continue;
    if (RS && RS->isScavengingFrameIndex((int)i))
      continue;
    if (MFI.isDeadObjectIndex(i))
      continue;
    if (MFI.getStackProtectorIndex() == (int)i || EHRegNodeFrameIndex == (int)i)
      continue;
    if (ProtectedObjs.count(i))
      continue;
    // Only allocate objects on the default stack.
    if (MFI.getStackID(i) != TargetStackID::Default)
      continue;

    // Add the objects that we need to allocate to our working set.
    ObjectsToAllocate.push_back(i);
  }

  // Allocate the EH registration node first if one is present.
  if (EHRegNodeFrameIndex != std::numeric_limits<int>::max())
    AdjustStackOffset(MFI, EHRegNodeFrameIndex, StackGrowsDown, Offset,
                      MaxAlign);

  // Give the targets a chance to order the objects the way they like it.
  if (MF.getTarget().getOptLevel() != CodeGenOptLevel::None &&
      MF.getTarget().Options.StackSymbolOrdering)
    TFI.orderFrameObjects(MF, ObjectsToAllocate);

  // Keep track of which bytes in the fixed and callee-save range are used so we
  // can use the holes when allocating later stack objects.  Only do this if
  // stack protector isn't being used and the target requests it and we're
  // optimizing.
  BitVector StackBytesFree;
  if (!ObjectsToAllocate.empty() &&
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None &&
      MFI.getStackProtectorIndex() < 0 && TFI.enableStackSlotScavenging(MF))
    computeFreeStackSlots(MFI, StackGrowsDown, MinCSFrameIndex, MaxCSFrameIndex,
                          FixedCSEnd, StackBytesFree);

  // Now walk the objects and actually assign base offsets to them.
  for (auto &Object : ObjectsToAllocate)
    if (!scavengeStackSlot(MFI, Object, StackGrowsDown, MaxAlign,
                           StackBytesFree))
      AdjustStackOffset(MFI, Object, StackGrowsDown, Offset, MaxAlign);

  // Make sure the special register scavenging spill slot is closest to the
  // stack pointer.
  if (RS && !EarlyScavengingSlots) {
    SmallVector<int, 2> SFIs;
    RS->getScavengingFrameIndices(SFIs);
    for (int SFI : SFIs)
      AdjustStackOffset(MFI, SFI, StackGrowsDown, Offset, MaxAlign);
  }

  if (!TFI.targetHandlesStackFrameRounding()) {
    // If we have reserved argument space for call sites in the function
    // immediately on entry to the current function, count it as part of the
    // overall stack size.
    if (MFI.adjustsStack() && TFI.hasReservedCallFrame(MF))
      Offset += MFI.getMaxCallFrameSize();

    // Round up the size to a multiple of the alignment.  If the function has
    // any calls or alloca's, align to the target's StackAlignment value to
    // ensure that the callee's frame or the alloca data is suitably aligned;
    // otherwise, for leaf functions, align to the TransientStackAlignment
    // value.
    Align StackAlign;
    if (MFI.adjustsStack() || MFI.hasVarSizedObjects() ||
        (RegInfo->hasStackRealignment(MF) && MFI.getObjectIndexEnd() != 0))
      StackAlign = TFI.getStackAlign();
    else
      StackAlign = TFI.getTransientStackAlign();

    // If the frame pointer is eliminated, all frame offsets will be relative to
    // SP not FP. Align to MaxAlign so this works.
    StackAlign = std::max(StackAlign, MaxAlign);
    int64_t OffsetBeforeAlignment = Offset;
    Offset = alignTo(Offset, StackAlign);

    // If we have increased the offset to fulfill the alignment constrants,
    // then the scavenging spill slots may become harder to reach from the
    // stack pointer, float them so they stay close.
    if (StackGrowsDown && OffsetBeforeAlignment != Offset && RS &&
        !EarlyScavengingSlots) {
      SmallVector<int, 2> SFIs;
      RS->getScavengingFrameIndices(SFIs);
      LLVM_DEBUG(if (!SFIs.empty()) llvm::dbgs()
                     << "Adjusting emergency spill slots!\n";);
      int64_t Delta = Offset - OffsetBeforeAlignment;
      for (int SFI : SFIs) {
        LLVM_DEBUG(llvm::dbgs()
                       << "Adjusting offset of emergency spill slot #" << SFI
                       << " from " << MFI.getObjectOffset(SFI););
        MFI.setObjectOffset(SFI, MFI.getObjectOffset(SFI) - Delta);
        LLVM_DEBUG(llvm::dbgs() << " to " << MFI.getObjectOffset(SFI) << "\n";);
      }
    }
  }

  // Update frame info to pretend that this is part of the stack...
  int64_t StackSize = Offset - LocalAreaOffset;
  MFI.setStackSize(StackSize);
  NumBytesStackSpace += StackSize;
}

/// insertPrologEpilogCode - Scan the function for modified callee saved
/// registers, insert spill code for these callee saved registers, then add
/// prolog and epilog code to the function.
void PEI::insertPrologEpilogCode(MachineFunction &MF) {
  const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();

  // Add prologue to the function...
  for (MachineBasicBlock *SaveBlock : SaveBlocks)
    TFI.emitPrologue(MF, *SaveBlock);

  // Add epilogue to restore the callee-save registers in each exiting block.
  for (MachineBasicBlock *RestoreBlock : RestoreBlocks)
    TFI.emitEpilogue(MF, *RestoreBlock);

  // Zero call used registers before restoring callee-saved registers.
  insertZeroCallUsedRegs(MF);

  for (MachineBasicBlock *SaveBlock : SaveBlocks)
    TFI.inlineStackProbe(MF, *SaveBlock);

  // Emit additional code that is required to support segmented stacks, if
  // we've been asked for it.  This, when linked with a runtime with support
  // for segmented stacks (libgcc is one), will result in allocating stack
  // space in small chunks instead of one large contiguous block.
  if (MF.shouldSplitStack()) {
    for (MachineBasicBlock *SaveBlock : SaveBlocks)
      TFI.adjustForSegmentedStacks(MF, *SaveBlock);
  }

  // Emit additional code that is required to explicitly handle the stack in
  // HiPE native code (if needed) when loaded in the Erlang/OTP runtime. The
  // approach is rather similar to that of Segmented Stacks, but it uses a
  // different conditional check and another BIF for allocating more stack
  // space.
  if (MF.getFunction().getCallingConv() == CallingConv::HiPE)
    for (MachineBasicBlock *SaveBlock : SaveBlocks)
      TFI.adjustForHiPEPrologue(MF, *SaveBlock);
}

/// insertZeroCallUsedRegs - Zero out call used registers.
void PEI::insertZeroCallUsedRegs(MachineFunction &MF) {
  const Function &F = MF.getFunction();

  if (!F.hasFnAttribute("zero-call-used-regs"))
    return;

  using namespace ZeroCallUsedRegs;

  ZeroCallUsedRegsKind ZeroRegsKind =
      StringSwitch<ZeroCallUsedRegsKind>(
          F.getFnAttribute("zero-call-used-regs").getValueAsString())
          .Case("skip", ZeroCallUsedRegsKind::Skip)
          .Case("used-gpr-arg", ZeroCallUsedRegsKind::UsedGPRArg)
          .Case("used-gpr", ZeroCallUsedRegsKind::UsedGPR)
          .Case("used-arg", ZeroCallUsedRegsKind::UsedArg)
          .Case("used", ZeroCallUsedRegsKind::Used)
          .Case("all-gpr-arg", ZeroCallUsedRegsKind::AllGPRArg)
          .Case("all-gpr", ZeroCallUsedRegsKind::AllGPR)
          .Case("all-arg", ZeroCallUsedRegsKind::AllArg)
          .Case("all", ZeroCallUsedRegsKind::All);

  if (ZeroRegsKind == ZeroCallUsedRegsKind::Skip)
    return;

  const bool OnlyGPR = static_cast<unsigned>(ZeroRegsKind) & ONLY_GPR;
  const bool OnlyUsed = static_cast<unsigned>(ZeroRegsKind) & ONLY_USED;
  const bool OnlyArg = static_cast<unsigned>(ZeroRegsKind) & ONLY_ARG;

  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const BitVector AllocatableSet(TRI.getAllocatableSet(MF));

  // Mark all used registers.
  BitVector UsedRegs(TRI.getNumRegs());
  if (OnlyUsed)
    for (const MachineBasicBlock &MBB : MF)
      for (const MachineInstr &MI : MBB) {
        // skip debug instructions
        if (MI.isDebugInstr())
          continue;

        for (const MachineOperand &MO : MI.operands()) {
          if (!MO.isReg())
            continue;

          MCRegister Reg = MO.getReg();
          if (AllocatableSet[Reg] && !MO.isImplicit() &&
              (MO.isDef() || MO.isUse()))
            UsedRegs.set(Reg);
        }
      }

  // Get a list of registers that are used.
  BitVector LiveIns(TRI.getNumRegs());
  for (const MachineBasicBlock::RegisterMaskPair &LI : MF.front().liveins())
    LiveIns.set(LI.PhysReg);

  BitVector RegsToZero(TRI.getNumRegs());
  for (MCRegister Reg : AllocatableSet.set_bits()) {
    // Skip over fixed registers.
    if (TRI.isFixedRegister(MF, Reg))
      continue;

    // Want only general purpose registers.
    if (OnlyGPR && !TRI.isGeneralPurposeRegister(MF, Reg))
      continue;

    // Want only used registers.
    if (OnlyUsed && !UsedRegs[Reg])
      continue;

    // Want only registers used for arguments.
    if (OnlyArg) {
      if (OnlyUsed) {
        if (!LiveIns[Reg])
          continue;
      } else if (!TRI.isArgumentRegister(MF, Reg)) {
        continue;
      }
    }

    RegsToZero.set(Reg);
  }

  // Don't clear registers that are live when leaving the function.
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB.terminators()) {
      if (!MI.isReturn())
        continue;

      for (const auto &MO : MI.operands()) {
        if (!MO.isReg())
          continue;

        MCRegister Reg = MO.getReg();
        if (!Reg)
          continue;

        // This picks up sibling registers (e.q. %al -> %ah).
        for (MCRegUnit Unit : TRI.regunits(Reg))
          RegsToZero.reset(Unit);

        for (MCPhysReg SReg : TRI.sub_and_superregs_inclusive(Reg))
          RegsToZero.reset(SReg);
      }
    }

  // Don't need to clear registers that are used/clobbered by terminating
  // instructions.
  for (const MachineBasicBlock &MBB : MF) {
    if (!MBB.isReturnBlock())
      continue;

    MachineBasicBlock::const_iterator MBBI = MBB.getFirstTerminator();
    for (MachineBasicBlock::const_iterator I = MBBI, E = MBB.end(); I != E;
         ++I) {
      for (const MachineOperand &MO : I->operands()) {
        if (!MO.isReg())
          continue;

        MCRegister Reg = MO.getReg();
        if (!Reg)
          continue;

        for (const MCPhysReg Reg : TRI.sub_and_superregs_inclusive(Reg))
          RegsToZero.reset(Reg);
      }
    }
  }

  // Don't clear registers that must be preserved.
  for (const MCPhysReg *CSRegs = TRI.getCalleeSavedRegs(&MF);
       MCPhysReg CSReg = *CSRegs; ++CSRegs)
    for (MCRegister Reg : TRI.sub_and_superregs_inclusive(CSReg))
      RegsToZero.reset(Reg);

  // Don't touch the return protector register if it is used
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (MFI.hasReturnProtectorRegister()) {
    MCRegister RGReg = MCRegister(MFI.getReturnProtectorRegister());
    for (MCRegister Reg : TRI.sub_and_superregs_inclusive(RGReg)) {
      RegsToZero.reset(Reg);
    }
  }

  const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();
  for (MachineBasicBlock &MBB : MF)
    if (MBB.isReturnBlock())
      TFI.emitZeroCallUsedRegs(RegsToZero, MBB);
}

/// Replace all FrameIndex operands with physical register references and actual
/// offsets.
void PEI::replaceFrameIndicesBackward(MachineFunction &MF) {
  const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();

  for (auto &MBB : MF) {
    int SPAdj = 0;
    if (!MBB.succ_empty()) {
      // Get the SP adjustment for the end of MBB from the start of any of its
      // successors. They should all be the same.
      assert(all_of(MBB.successors(), [&MBB](const MachineBasicBlock *Succ) {
        return Succ->getCallFrameSize() ==
               (*MBB.succ_begin())->getCallFrameSize();
      }));
      const MachineBasicBlock &FirstSucc = **MBB.succ_begin();
      SPAdj = TFI.alignSPAdjust(FirstSucc.getCallFrameSize());
      if (TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsUp)
        SPAdj = -SPAdj;
    }

    replaceFrameIndicesBackward(&MBB, MF, SPAdj);

    // We can't track the call frame size after call frame pseudos have been
    // eliminated. Set it to zero everywhere to keep MachineVerifier happy.
    MBB.setCallFrameSize(0);
  }
}

/// replaceFrameIndices - Replace all MO_FrameIndex operands with physical
/// register references and actual offsets.
void PEI::replaceFrameIndices(MachineFunction &MF) {
  const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();

  for (auto &MBB : MF) {
    int SPAdj = TFI.alignSPAdjust(MBB.getCallFrameSize());
    if (TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsUp)
      SPAdj = -SPAdj;

    replaceFrameIndices(&MBB, MF, SPAdj);

    // We can't track the call frame size after call frame pseudos have been
    // eliminated. Set it to zero everywhere to keep MachineVerifier happy.
    MBB.setCallFrameSize(0);
  }
}

bool PEI::replaceFrameIndexDebugInstr(MachineFunction &MF, MachineInstr &MI,
                                      unsigned OpIdx, int SPAdj) {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  if (MI.isDebugValue()) {

    MachineOperand &Op = MI.getOperand(OpIdx);
    assert(MI.isDebugOperand(&Op) &&
           "Frame indices can only appear as a debug operand in a DBG_VALUE*"
           " machine instruction");
    Register Reg;
    unsigned FrameIdx = Op.getIndex();
    unsigned Size = MF.getFrameInfo().getObjectSize(FrameIdx);

    StackOffset Offset = TFI->getFrameIndexReference(MF, FrameIdx, Reg);
    Op.ChangeToRegister(Reg, false /*isDef*/);

    const DIExpression *DIExpr = MI.getDebugExpression();

    // If we have a direct DBG_VALUE, and its location expression isn't
    // currently complex, then adding an offset will morph it into a
    // complex location that is interpreted as being a memory address.
    // This changes a pointer-valued variable to dereference that pointer,
    // which is incorrect. Fix by adding DW_OP_stack_value.

    if (MI.isNonListDebugValue()) {
      unsigned PrependFlags = DIExpression::ApplyOffset;
      if (!MI.isIndirectDebugValue() && !DIExpr->isComplex())
        PrependFlags |= DIExpression::StackValue;

      // If we have DBG_VALUE that is indirect and has a Implicit location
      // expression need to insert a deref before prepending a Memory
      // location expression. Also after doing this we change the DBG_VALUE
      // to be direct.
      if (MI.isIndirectDebugValue() && DIExpr->isImplicit()) {
        SmallVector<uint64_t, 2> Ops = {dwarf::DW_OP_deref_size, Size};
        bool WithStackValue = true;
        DIExpr = DIExpression::prependOpcodes(DIExpr, Ops, WithStackValue);
        // Make the DBG_VALUE direct.
        MI.getDebugOffset().ChangeToRegister(0, false);
      }
      DIExpr = TRI.prependOffsetExpression(DIExpr, PrependFlags, Offset);
    } else {
      // The debug operand at DebugOpIndex was a frame index at offset
      // `Offset`; now the operand has been replaced with the frame
      // register, we must add Offset with `register x, plus Offset`.
      unsigned DebugOpIndex = MI.getDebugOperandIndex(&Op);
      SmallVector<uint64_t, 3> Ops;
      TRI.getOffsetOpcodes(Offset, Ops);
      DIExpr = DIExpression::appendOpsToArg(DIExpr, Ops, DebugOpIndex);
    }
    MI.getDebugExpressionOp().setMetadata(DIExpr);
    return true;
  }

  if (MI.isDebugPHI()) {
    // Allow stack ref to continue onwards.
    return true;
  }

  // TODO: This code should be commoned with the code for
  // PATCHPOINT. There's no good reason for the difference in
  // implementation other than historical accident.  The only
  // remaining difference is the unconditional use of the stack
  // pointer as the base register.
  if (MI.getOpcode() == TargetOpcode::STATEPOINT) {
    assert((!MI.isDebugValue() || OpIdx == 0) &&
           "Frame indices can only appear as the first operand of a "
           "DBG_VALUE machine instruction");
    Register Reg;
    MachineOperand &Offset = MI.getOperand(OpIdx + 1);
    StackOffset refOffset = TFI->getFrameIndexReferencePreferSP(
        MF, MI.getOperand(OpIdx).getIndex(), Reg, /*IgnoreSPUpdates*/ false);
    assert(!refOffset.getScalable() &&
           "Frame offsets with a scalable component are not supported");
    Offset.setImm(Offset.getImm() + refOffset.getFixed() + SPAdj);
    MI.getOperand(OpIdx).ChangeToRegister(Reg, false /*isDef*/);
    return true;
  }
  return false;
}

void PEI::replaceFrameIndicesBackward(MachineBasicBlock *BB,
                                      MachineFunction &MF, int &SPAdj) {
  assert(MF.getSubtarget().getRegisterInfo() &&
         "getRegisterInfo() must be implemented!");

  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetFrameLowering &TFI = *MF.getSubtarget().getFrameLowering();

  RegScavenger *LocalRS = FrameIndexEliminationScavenging ? RS : nullptr;
  if (LocalRS)
    LocalRS->enterBasicBlockEnd(*BB);

  for (MachineBasicBlock::iterator I = BB->end(); I != BB->begin();) {
    MachineInstr &MI = *std::prev(I);

    if (TII.isFrameInstr(MI)) {
      SPAdj -= TII.getSPAdjust(MI);
      TFI.eliminateCallFramePseudoInstr(MF, *BB, &MI);
      continue;
    }

    // Step backwards to get the liveness state at (immedately after) MI.
    if (LocalRS)
      LocalRS->backward(I);

    bool RemovedMI = false;
    for (const auto &[Idx, Op] : enumerate(MI.operands())) {
      if (!Op.isFI())
        continue;

      if (replaceFrameIndexDebugInstr(MF, MI, Idx, SPAdj))
        continue;

      // Eliminate this FrameIndex operand.
      RemovedMI = TRI.eliminateFrameIndex(MI, SPAdj, Idx, LocalRS);
      if (RemovedMI)
        break;
    }

    if (!RemovedMI)
      --I;
  }
}

void PEI::replaceFrameIndices(MachineBasicBlock *BB, MachineFunction &MF,
                              int &SPAdj) {
  assert(MF.getSubtarget().getRegisterInfo() &&
         "getRegisterInfo() must be implemented!");
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  bool InsideCallSequence = false;

  for (MachineBasicBlock::iterator I = BB->begin(); I != BB->end(); ) {
    if (TII.isFrameInstr(*I)) {
      InsideCallSequence = TII.isFrameSetup(*I);
      SPAdj += TII.getSPAdjust(*I);
      I = TFI->eliminateCallFramePseudoInstr(MF, *BB, I);
      continue;
    }

    MachineInstr &MI = *I;
    bool DoIncr = true;
    bool DidFinishLoop = true;
    for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
      if (!MI.getOperand(i).isFI())
        continue;

      if (replaceFrameIndexDebugInstr(MF, MI, i, SPAdj))
        continue;

      // Some instructions (e.g. inline asm instructions) can have
      // multiple frame indices and/or cause eliminateFrameIndex
      // to insert more than one instruction. We need the register
      // scavenger to go through all of these instructions so that
      // it can update its register information. We keep the
      // iterator at the point before insertion so that we can
      // revisit them in full.
      bool AtBeginning = (I == BB->begin());
      if (!AtBeginning) --I;

      // If this instruction has a FrameIndex operand, we need to
      // use that target machine register info object to eliminate
      // it.
      TRI.eliminateFrameIndex(MI, SPAdj, i);

      // Reset the iterator if we were at the beginning of the BB.
      if (AtBeginning) {
        I = BB->begin();
        DoIncr = false;
      }

      DidFinishLoop = false;
      break;
    }

    // If we are looking at a call sequence, we need to keep track of
    // the SP adjustment made by each instruction in the sequence.
    // This includes both the frame setup/destroy pseudos (handled above),
    // as well as other instructions that have side effects w.r.t the SP.
    // Note that this must come after eliminateFrameIndex, because
    // if I itself referred to a frame index, we shouldn't count its own
    // adjustment.
    if (DidFinishLoop && InsideCallSequence)
      SPAdj += TII.getSPAdjust(MI);

    if (DoIncr && I != BB->end())
      ++I;
  }
}
