//===- MachineLoopInfo.cpp - Natural Loop Calculator ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MachineLoopInfo class that is used to identify natural
// loops and determine the loop depth of various nodes of the CFG.  Note that
// the loops identified may actually be several natural loops that share the
// same header node... not just a single natural loop.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/GenericLoopInfoImpl.h"

using namespace llvm;

// Explicitly instantiate methods in LoopInfoImpl.h for MI-level Loops.
template class llvm::LoopBase<MachineBasicBlock, MachineLoop>;
template class llvm::LoopInfoBase<MachineBasicBlock, MachineLoop>;

AnalysisKey MachineLoopAnalysis::Key;

MachineLoopAnalysis::Result
MachineLoopAnalysis::run(MachineFunction &MF,
                         MachineFunctionAnalysisManager &MFAM) {
  return MachineLoopInfo(MFAM.getResult<MachineDominatorTreeAnalysis>(MF));
}

PreservedAnalyses
MachineLoopPrinterPass::run(MachineFunction &MF,
                            MachineFunctionAnalysisManager &MFAM) {
  OS << "Machine loop info for machine function '" << MF.getName() << "':\n";
  MFAM.getResult<MachineLoopAnalysis>(MF).print(OS);
  return PreservedAnalyses::all();
}

char MachineLoopInfoWrapperPass::ID = 0;
MachineLoopInfoWrapperPass::MachineLoopInfoWrapperPass()
    : MachineFunctionPass(ID) {
  initializeMachineLoopInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}
INITIALIZE_PASS_BEGIN(MachineLoopInfoWrapperPass, "machine-loops",
                      "Machine Natural Loop Construction", true, true)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(MachineLoopInfoWrapperPass, "machine-loops",
                    "Machine Natural Loop Construction", true, true)

char &llvm::MachineLoopInfoID = MachineLoopInfoWrapperPass::ID;

bool MachineLoopInfoWrapperPass::runOnMachineFunction(MachineFunction &) {
  LI.calculate(getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree());
  return false;
}

bool MachineLoopInfo::invalidate(
    MachineFunction &, const PreservedAnalyses &PA,
    MachineFunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on functions, or the function's
  // CFG have been preserved.
  auto PAC = PA.getChecker<MachineLoopAnalysis>();
  return !PAC.preserved() &&
         !PAC.preservedSet<AllAnalysesOn<MachineFunction>>() &&
         !PAC.preservedSet<CFGAnalyses>();
}

void MachineLoopInfo::calculate(MachineDominatorTree &MDT) {
  releaseMemory();
  analyze(MDT.getBase());
}

void MachineLoopInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

MachineBasicBlock *MachineLoop::getTopBlock() {
  MachineBasicBlock *TopMBB = getHeader();
  MachineFunction::iterator Begin = TopMBB->getParent()->begin();
  if (TopMBB->getIterator() != Begin) {
    MachineBasicBlock *PriorMBB = &*std::prev(TopMBB->getIterator());
    while (contains(PriorMBB)) {
      TopMBB = PriorMBB;
      if (TopMBB->getIterator() == Begin)
        break;
      PriorMBB = &*std::prev(TopMBB->getIterator());
    }
  }
  return TopMBB;
}

MachineBasicBlock *MachineLoop::getBottomBlock() {
  MachineBasicBlock *BotMBB = getHeader();
  MachineFunction::iterator End = BotMBB->getParent()->end();
  if (BotMBB->getIterator() != std::prev(End)) {
    MachineBasicBlock *NextMBB = &*std::next(BotMBB->getIterator());
    while (contains(NextMBB)) {
      BotMBB = NextMBB;
      if (BotMBB == &*std::next(BotMBB->getIterator()))
        break;
      NextMBB = &*std::next(BotMBB->getIterator());
    }
  }
  return BotMBB;
}

MachineBasicBlock *MachineLoop::findLoopControlBlock() const {
  if (MachineBasicBlock *Latch = getLoopLatch()) {
    if (isLoopExiting(Latch))
      return Latch;
    else
      return getExitingBlock();
  }
  return nullptr;
}

DebugLoc MachineLoop::getStartLoc() const {
  // Try the pre-header first.
  if (MachineBasicBlock *PHeadMBB = getLoopPreheader())
    if (const BasicBlock *PHeadBB = PHeadMBB->getBasicBlock())
      if (DebugLoc DL = PHeadBB->getTerminator()->getDebugLoc())
        return DL;

  // If we have no pre-header or there are no instructions with debug
  // info in it, try the header.
  if (MachineBasicBlock *HeadMBB = getHeader())
    if (const BasicBlock *HeadBB = HeadMBB->getBasicBlock())
      return HeadBB->getTerminator()->getDebugLoc();

  return DebugLoc();
}

MachineBasicBlock *
MachineLoopInfo::findLoopPreheader(MachineLoop *L, bool SpeculativePreheader,
                                   bool FindMultiLoopPreheader) const {
  if (MachineBasicBlock *PB = L->getLoopPreheader())
    return PB;

  if (!SpeculativePreheader)
    return nullptr;

  MachineBasicBlock *HB = L->getHeader(), *LB = L->getLoopLatch();
  if (HB->pred_size() != 2 || HB->hasAddressTaken())
    return nullptr;
  // Find the predecessor of the header that is not the latch block.
  MachineBasicBlock *Preheader = nullptr;
  for (MachineBasicBlock *P : HB->predecessors()) {
    if (P == LB)
      continue;
    // Sanity.
    if (Preheader)
      return nullptr;
    Preheader = P;
  }

  // Check if the preheader candidate is a successor of any other loop
  // headers. We want to avoid having two loop setups in the same block.
  if (!FindMultiLoopPreheader) {
    for (MachineBasicBlock *S : Preheader->successors()) {
      if (S == HB)
        continue;
      MachineLoop *T = getLoopFor(S);
      if (T && T->getHeader() == S)
        return nullptr;
    }
  }
  return Preheader;
}

MDNode *MachineLoop::getLoopID() const {
  MDNode *LoopID = nullptr;
  if (const auto *MBB = findLoopControlBlock()) {
    // If there is a single latch block, then the metadata
    // node is attached to its terminating instruction.
    const auto *BB = MBB->getBasicBlock();
    if (!BB)
      return nullptr;
    if (const auto *TI = BB->getTerminator())
      LoopID = TI->getMetadata(LLVMContext::MD_loop);
  } else if (const auto *MBB = getHeader()) {
    // There seem to be multiple latch blocks, so we have to
    // visit all predecessors of the loop header and check
    // their terminating instructions for the metadata.
    if (const auto *Header = MBB->getBasicBlock()) {
      // Walk over all blocks in the loop.
      for (const auto *MBB : this->blocks()) {
        const auto *BB = MBB->getBasicBlock();
        if (!BB)
          return nullptr;
        const auto *TI = BB->getTerminator();
        if (!TI)
          return nullptr;
        MDNode *MD = nullptr;
        // Check if this terminating instruction jumps to the loop header.
        for (const auto *Succ : successors(TI)) {
          if (Succ == Header) {
            // This is a jump to the header - gather the metadata from it.
            MD = TI->getMetadata(LLVMContext::MD_loop);
            break;
          }
        }
        if (!MD)
          return nullptr;
        if (!LoopID)
          LoopID = MD;
        else if (MD != LoopID)
          return nullptr;
      }
    }
  }
  if (LoopID &&
      (LoopID->getNumOperands() == 0 || LoopID->getOperand(0) != LoopID))
    LoopID = nullptr;
  return LoopID;
}

bool MachineLoop::isLoopInvariantImplicitPhysReg(Register Reg) const {
  MachineFunction *MF = getHeader()->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();

  if (MRI->isConstantPhysReg(Reg))
    return true;

  if (!MF->getSubtarget()
           .getRegisterInfo()
           ->shouldAnalyzePhysregInMachineLoopInfo(Reg))
    return false;

  return !llvm::any_of(
      MRI->def_instructions(Reg),
      [this](const MachineInstr &MI) { return this->contains(&MI); });
}

bool MachineLoop::isLoopInvariant(MachineInstr &I,
                                  const Register ExcludeReg) const {
  MachineFunction *MF = I.getParent()->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  const TargetSubtargetInfo &ST = MF->getSubtarget();
  const TargetRegisterInfo *TRI = ST.getRegisterInfo();
  const TargetInstrInfo *TII = ST.getInstrInfo();

  // The instruction is loop invariant if all of its operands are.
  for (const MachineOperand &MO : I.operands()) {
    if (!MO.isReg())
      continue;

    Register Reg = MO.getReg();
    if (Reg == 0) continue;

    if (ExcludeReg == Reg)
      continue;

    // An instruction that uses or defines a physical register can't e.g. be
    // hoisted, so mark this as not invariant.
    if (Reg.isPhysical()) {
      if (MO.isUse()) {
        // If the physreg has no defs anywhere, it's just an ambient register
        // and we can freely move its uses. Alternatively, if it's allocatable,
        // it could get allocated to something with a def during allocation.
        // However, if the physreg is known to always be caller saved/restored
        // then this use is safe to hoist.
        if (!isLoopInvariantImplicitPhysReg(Reg) &&
            !(TRI->isCallerPreservedPhysReg(Reg.asMCReg(), *I.getMF())) &&
            !TII->isIgnorableUse(MO))
          return false;
        // Otherwise it's safe to move.
        continue;
      } else if (!MO.isDead()) {
        // A def that isn't dead can't be moved.
        return false;
      } else if (getHeader()->isLiveIn(Reg)) {
        // If the reg is live into the loop, we can't hoist an instruction
        // which would clobber it.
        return false;
      }
    }

    if (!MO.isUse())
      continue;

    assert(MRI->getVRegDef(Reg) &&
           "Machine instr not mapped for this vreg?!");

    // If the loop contains the definition of an operand, then the instruction
    // isn't loop invariant.
    if (contains(MRI->getVRegDef(Reg)))
      return false;
  }

  // If we got this far, the instruction is loop invariant!
  return true;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineLoop::dump() const {
  print(dbgs());
}
#endif
