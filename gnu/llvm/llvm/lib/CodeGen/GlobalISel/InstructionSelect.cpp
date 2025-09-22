//===- llvm/CodeGen/GlobalISel/InstructionSelect.cpp - InstructionSelect ---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the InstructionSelect class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/Analysis/LazyBlockFrequencyInfo.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/config.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGenCoverage.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "instruction-select"

using namespace llvm;

DEBUG_COUNTER(GlobalISelCounter, "globalisel",
              "Controls whether to select function with GlobalISel");

#ifdef LLVM_GISEL_COV_PREFIX
static cl::opt<std::string>
    CoveragePrefix("gisel-coverage-prefix", cl::init(LLVM_GISEL_COV_PREFIX),
                   cl::desc("Record GlobalISel rule coverage files of this "
                            "prefix if instrumentation was generated"));
#else
static const std::string CoveragePrefix;
#endif

char InstructionSelect::ID = 0;
INITIALIZE_PASS_BEGIN(InstructionSelect, DEBUG_TYPE,
                      "Select target instructions out of generic instructions",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_DEPENDENCY(ProfileSummaryInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LazyBlockFrequencyInfoPass)
INITIALIZE_PASS_END(InstructionSelect, DEBUG_TYPE,
                    "Select target instructions out of generic instructions",
                    false, false)

InstructionSelect::InstructionSelect(CodeGenOptLevel OL, char &PassID)
    : MachineFunctionPass(PassID), OptLevel(OL) {}

void InstructionSelect::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();

  if (OptLevel != CodeGenOptLevel::None) {
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    LazyBlockFrequencyInfoPass::getLazyBFIAnalysisUsage(AU);
  }
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool InstructionSelect::runOnMachineFunction(MachineFunction &MF) {
  // If the ISel pipeline failed, do not bother running that pass.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;

  LLVM_DEBUG(dbgs() << "Selecting function: " << MF.getName() << '\n');

  const TargetPassConfig &TPC = getAnalysis<TargetPassConfig>();
  InstructionSelector *ISel = MF.getSubtarget().getInstructionSelector();
  ISel->setTargetPassConfig(&TPC);

  CodeGenOptLevel OldOptLevel = OptLevel;
  auto RestoreOptLevel = make_scope_exit([=]() { OptLevel = OldOptLevel; });
  OptLevel = MF.getFunction().hasOptNone() ? CodeGenOptLevel::None
                                           : MF.getTarget().getOptLevel();

  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);
  if (OptLevel != CodeGenOptLevel::None) {
    PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
    if (PSI && PSI->hasProfileSummary())
      BFI = &getAnalysis<LazyBlockFrequencyInfoPass>().getBFI();
  }

  CodeGenCoverage CoverageInfo;
  assert(ISel && "Cannot work without InstructionSelector");
  ISel->setupMF(MF, KB, &CoverageInfo, PSI, BFI);

  // An optimization remark emitter. Used to report failures.
  MachineOptimizationRemarkEmitter MORE(MF, /*MBFI=*/nullptr);
  ISel->setRemarkEmitter(&MORE);

  // FIXME: There are many other MF/MFI fields we need to initialize.

  MachineRegisterInfo &MRI = MF.getRegInfo();
#ifndef NDEBUG
  // Check that our input is fully legal: we require the function to have the
  // Legalized property, so it should be.
  // FIXME: This should be in the MachineVerifier, as the RegBankSelected
  // property check already is.
  if (!DisableGISelLegalityCheck)
    if (const MachineInstr *MI = machineFunctionIsIllegal(MF)) {
      reportGISelFailure(MF, TPC, MORE, "gisel-select",
                         "instruction is not legal", *MI);
      return false;
    }
  // FIXME: We could introduce new blocks and will need to fix the outer loop.
  // Until then, keep track of the number of blocks to assert that we don't.
  const size_t NumBlocks = MF.size();
#endif
  // Keep track of selected blocks, so we can delete unreachable ones later.
  DenseSet<MachineBasicBlock *> SelectedBlocks;

  for (MachineBasicBlock *MBB : post_order(&MF)) {
    ISel->CurMBB = MBB;
    SelectedBlocks.insert(MBB);
    if (MBB->empty())
      continue;

    // Select instructions in reverse block order. We permit erasing so have
    // to resort to manually iterating and recognizing the begin (rend) case.
    bool ReachedBegin = false;
    for (auto MII = std::prev(MBB->end()), Begin = MBB->begin();
         !ReachedBegin;) {
#ifndef NDEBUG
      // Keep track of the insertion range for debug printing.
      const auto AfterIt = std::next(MII);
#endif
      // Select this instruction.
      MachineInstr &MI = *MII;

      // And have our iterator point to the next instruction, if there is one.
      if (MII == Begin)
        ReachedBegin = true;
      else
        --MII;

      LLVM_DEBUG(dbgs() << "Selecting: \n  " << MI);

      // We could have folded this instruction away already, making it dead.
      // If so, erase it.
      if (isTriviallyDead(MI, MRI)) {
        LLVM_DEBUG(dbgs() << "Is dead; erasing.\n");
        salvageDebugInfo(MRI, MI);
        MI.eraseFromParent();
        continue;
      }

      // Eliminate hints or G_CONSTANT_FOLD_BARRIER.
      if (isPreISelGenericOptimizationHint(MI.getOpcode()) ||
          MI.getOpcode() == TargetOpcode::G_CONSTANT_FOLD_BARRIER) {
        auto [DstReg, SrcReg] = MI.getFirst2Regs();

        // At this point, the destination register class of the op may have
        // been decided.
        //
        // Propagate that through to the source register.
        const TargetRegisterClass *DstRC = MRI.getRegClassOrNull(DstReg);
        if (DstRC)
          MRI.setRegClass(SrcReg, DstRC);
        assert(canReplaceReg(DstReg, SrcReg, MRI) &&
               "Must be able to replace dst with src!");
        MI.eraseFromParent();
        MRI.replaceRegWith(DstReg, SrcReg);
        continue;
      }

      if (MI.getOpcode() == TargetOpcode::G_INVOKE_REGION_START) {
        MI.eraseFromParent();
        continue;
      }

      if (!ISel->select(MI)) {
        // FIXME: It would be nice to dump all inserted instructions.  It's
        // not obvious how, esp. considering select() can insert after MI.
        reportGISelFailure(MF, TPC, MORE, "gisel-select", "cannot select", MI);
        return false;
      }

      // Dump the range of instructions that MI expanded into.
      LLVM_DEBUG({
        auto InsertedBegin = ReachedBegin ? MBB->begin() : std::next(MII);
        dbgs() << "Into:\n";
        for (auto &InsertedMI : make_range(InsertedBegin, AfterIt))
          dbgs() << "  " << InsertedMI;
        dbgs() << '\n';
      });
    }
  }

  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty())
      continue;

    if (!SelectedBlocks.contains(&MBB)) {
      // This is an unreachable block and therefore hasn't been selected, since
      // the main selection loop above uses a postorder block traversal.
      // We delete all the instructions in this block since it's unreachable.
      MBB.clear();
      // Don't delete the block in case the block has it's address taken or is
      // still being referenced by a phi somewhere.
      continue;
    }
    // Try to find redundant copies b/w vregs of the same register class.
    bool ReachedBegin = false;
    for (auto MII = std::prev(MBB.end()), Begin = MBB.begin(); !ReachedBegin;) {
      // Select this instruction.
      MachineInstr &MI = *MII;

      // And have our iterator point to the next instruction, if there is one.
      if (MII == Begin)
        ReachedBegin = true;
      else
        --MII;
      if (MI.getOpcode() != TargetOpcode::COPY)
        continue;
      Register SrcReg = MI.getOperand(1).getReg();
      Register DstReg = MI.getOperand(0).getReg();
      if (SrcReg.isVirtual() && DstReg.isVirtual()) {
        auto SrcRC = MRI.getRegClass(SrcReg);
        auto DstRC = MRI.getRegClass(DstReg);
        if (SrcRC == DstRC) {
          MRI.replaceRegWith(DstReg, SrcReg);
          MI.eraseFromParent();
        }
      }
    }
  }

#ifndef NDEBUG
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  // Now that selection is complete, there are no more generic vregs.  Verify
  // that the size of the now-constrained vreg is unchanged and that it has a
  // register class.
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register VReg = Register::index2VirtReg(I);

    MachineInstr *MI = nullptr;
    if (!MRI.def_empty(VReg))
      MI = &*MRI.def_instr_begin(VReg);
    else if (!MRI.use_empty(VReg)) {
      MI = &*MRI.use_instr_begin(VReg);
      // Debug value instruction is permitted to use undefined vregs.
      if (MI->isDebugValue())
        continue;
    }
    if (!MI)
      continue;

    const TargetRegisterClass *RC = MRI.getRegClassOrNull(VReg);
    if (!RC) {
      reportGISelFailure(MF, TPC, MORE, "gisel-select",
                         "VReg has no regclass after selection", *MI);
      return false;
    }

    const LLT Ty = MRI.getType(VReg);
    if (Ty.isValid() &&
        TypeSize::isKnownGT(Ty.getSizeInBits(), TRI.getRegSizeInBits(*RC))) {
      reportGISelFailure(
          MF, TPC, MORE, "gisel-select",
          "VReg's low-level type and register class have different sizes", *MI);
      return false;
    }
  }

  if (MF.size() != NumBlocks) {
    MachineOptimizationRemarkMissed R("gisel-select", "GISelFailure",
                                      MF.getFunction().getSubprogram(),
                                      /*MBB=*/nullptr);
    R << "inserting blocks is not supported yet";
    reportGISelFailure(MF, TPC, MORE, R);
    return false;
  }
#endif

  if (!DebugCounter::shouldExecute(GlobalISelCounter)) {
    dbgs() << "Falling back for function " << MF.getName() << "\n";
    MF.getProperties().set(MachineFunctionProperties::Property::FailedISel);
    return false;
  }

  // Determine if there are any calls in this machine function. Ported from
  // SelectionDAG.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  for (const auto &MBB : MF) {
    if (MFI.hasCalls() && MF.hasInlineAsm())
      break;

    for (const auto &MI : MBB) {
      if ((MI.isCall() && !MI.isReturn()) || MI.isStackAligningInlineAsm())
        MFI.setHasCalls(true);
      if (MI.isInlineAsm())
        MF.setHasInlineAsm(true);
    }
  }

  // FIXME: FinalizeISel pass calls finalizeLowering, so it's called twice.
  auto &TLI = *MF.getSubtarget().getTargetLowering();
  TLI.finalizeLowering(MF);

  LLVM_DEBUG({
    dbgs() << "Rules covered by selecting function: " << MF.getName() << ":";
    for (auto RuleID : CoverageInfo.covered())
      dbgs() << " id" << RuleID;
    dbgs() << "\n\n";
  });
  CoverageInfo.emit(CoveragePrefix,
                    TLI.getTargetMachine().getTarget().getBackendName());

  // If we successfully selected the function nothing is going to use the vreg
  // types after us (otherwise MIRPrinter would need them). Make sure the types
  // disappear.
  MRI.clearVirtRegTypes();

  // FIXME: Should we accurately track changes?
  return true;
}
