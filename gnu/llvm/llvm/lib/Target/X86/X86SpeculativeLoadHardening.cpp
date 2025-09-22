//====- X86SpeculativeLoadHardening.cpp - A Spectre v1 mitigation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Provide a pass which mitigates speculative execution attacks which operate
/// by speculating incorrectly past some predicate (a type check, bounds check,
/// or other condition) to reach a load with invalid inputs and leak the data
/// accessed by that load using a side channel out of the speculative domain.
///
/// For details on the attacks, see the first variant in both the Project Zero
/// writeup and the Spectre paper:
/// https://googleprojectzero.blogspot.com/2018/01/reading-privileged-memory-with-side.html
/// https://spectreattack.com/spectre.pdf
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <optional>
#include <utility>

using namespace llvm;

#define PASS_KEY "x86-slh"
#define DEBUG_TYPE PASS_KEY

STATISTIC(NumCondBranchesTraced, "Number of conditional branches traced");
STATISTIC(NumBranchesUntraced, "Number of branches unable to trace");
STATISTIC(NumAddrRegsHardened,
          "Number of address mode used registers hardaned");
STATISTIC(NumPostLoadRegsHardened,
          "Number of post-load register values hardened");
STATISTIC(NumCallsOrJumpsHardened,
          "Number of calls or jumps requiring extra hardening");
STATISTIC(NumInstsInserted, "Number of instructions inserted");
STATISTIC(NumLFENCEsInserted, "Number of lfence instructions inserted");

static cl::opt<bool> EnableSpeculativeLoadHardening(
    "x86-speculative-load-hardening",
    cl::desc("Force enable speculative load hardening"), cl::init(false),
    cl::Hidden);

static cl::opt<bool> HardenEdgesWithLFENCE(
    PASS_KEY "-lfence",
    cl::desc(
        "Use LFENCE along each conditional edge to harden against speculative "
        "loads rather than conditional movs and poisoned pointers."),
    cl::init(false), cl::Hidden);

static cl::opt<bool> EnablePostLoadHardening(
    PASS_KEY "-post-load",
    cl::desc("Harden the value loaded *after* it is loaded by "
             "flushing the loaded bits to 1. This is hard to do "
             "in general but can be done easily for GPRs."),
    cl::init(true), cl::Hidden);

static cl::opt<bool> FenceCallAndRet(
    PASS_KEY "-fence-call-and-ret",
    cl::desc("Use a full speculation fence to harden both call and ret edges "
             "rather than a lighter weight mitigation."),
    cl::init(false), cl::Hidden);

static cl::opt<bool> HardenInterprocedurally(
    PASS_KEY "-ip",
    cl::desc("Harden interprocedurally by passing our state in and out of "
             "functions in the high bits of the stack pointer."),
    cl::init(true), cl::Hidden);

static cl::opt<bool>
    HardenLoads(PASS_KEY "-loads",
                cl::desc("Sanitize loads from memory. When disable, no "
                         "significant security is provided."),
                cl::init(true), cl::Hidden);

static cl::opt<bool> HardenIndirectCallsAndJumps(
    PASS_KEY "-indirect",
    cl::desc("Harden indirect calls and jumps against using speculatively "
             "stored attacker controlled addresses. This is designed to "
             "mitigate Spectre v1.2 style attacks."),
    cl::init(true), cl::Hidden);

namespace {

class X86SpeculativeLoadHardeningPass : public MachineFunctionPass {
public:
  X86SpeculativeLoadHardeningPass() : MachineFunctionPass(ID) { }

  StringRef getPassName() const override {
    return "X86 speculative load hardening";
  }
  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Pass identification, replacement for typeid.
  static char ID;

private:
  /// The information about a block's conditional terminators needed to trace
  /// our predicate state through the exiting edges.
  struct BlockCondInfo {
    MachineBasicBlock *MBB;

    // We mostly have one conditional branch, and in extremely rare cases have
    // two. Three and more are so rare as to be unimportant for compile time.
    SmallVector<MachineInstr *, 2> CondBrs;

    MachineInstr *UncondBr;
  };

  /// Manages the predicate state traced through the program.
  struct PredState {
    unsigned InitialReg = 0;
    unsigned PoisonReg = 0;

    const TargetRegisterClass *RC;
    MachineSSAUpdater SSA;

    PredState(MachineFunction &MF, const TargetRegisterClass *RC)
        : RC(RC), SSA(MF) {}
  };

  const X86Subtarget *Subtarget = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  const X86InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  std::optional<PredState> PS;

  void hardenEdgesWithLFENCE(MachineFunction &MF);

  SmallVector<BlockCondInfo, 16> collectBlockCondInfo(MachineFunction &MF);

  SmallVector<MachineInstr *, 16>
  tracePredStateThroughCFG(MachineFunction &MF, ArrayRef<BlockCondInfo> Infos);

  void unfoldCallAndJumpLoads(MachineFunction &MF);

  SmallVector<MachineInstr *, 16>
  tracePredStateThroughIndirectBranches(MachineFunction &MF);

  void tracePredStateThroughBlocksAndHarden(MachineFunction &MF);

  unsigned saveEFLAGS(MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator InsertPt,
                      const DebugLoc &Loc);
  void restoreEFLAGS(MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator InsertPt, const DebugLoc &Loc,
                     Register Reg);

  void mergePredStateIntoSP(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator InsertPt,
                            const DebugLoc &Loc, unsigned PredStateReg);
  unsigned extractPredStateFromSP(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator InsertPt,
                                  const DebugLoc &Loc);

  void
  hardenLoadAddr(MachineInstr &MI, MachineOperand &BaseMO,
                 MachineOperand &IndexMO,
                 SmallDenseMap<unsigned, unsigned, 32> &AddrRegToHardenedReg);
  MachineInstr *
  sinkPostLoadHardenedInst(MachineInstr &MI,
                           SmallPtrSetImpl<MachineInstr *> &HardenedInstrs);
  bool canHardenRegister(Register Reg);
  unsigned hardenValueInRegister(Register Reg, MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator InsertPt,
                                 const DebugLoc &Loc);
  unsigned hardenPostLoad(MachineInstr &MI);
  void hardenReturnInstr(MachineInstr &MI);
  void tracePredStateThroughCall(MachineInstr &MI);
  void hardenIndirectCallOrJumpInstr(
      MachineInstr &MI,
      SmallDenseMap<unsigned, unsigned, 32> &AddrRegToHardenedReg);
};

} // end anonymous namespace

char X86SpeculativeLoadHardeningPass::ID = 0;

void X86SpeculativeLoadHardeningPass::getAnalysisUsage(
    AnalysisUsage &AU) const {
  MachineFunctionPass::getAnalysisUsage(AU);
}

static MachineBasicBlock &splitEdge(MachineBasicBlock &MBB,
                                    MachineBasicBlock &Succ, int SuccCount,
                                    MachineInstr *Br, MachineInstr *&UncondBr,
                                    const X86InstrInfo &TII) {
  assert(!Succ.isEHPad() && "Shouldn't get edges to EH pads!");

  MachineFunction &MF = *MBB.getParent();

  MachineBasicBlock &NewMBB = *MF.CreateMachineBasicBlock();

  // We have to insert the new block immediately after the current one as we
  // don't know what layout-successor relationships the successor has and we
  // may not be able to (and generally don't want to) try to fix those up.
  MF.insert(std::next(MachineFunction::iterator(&MBB)), &NewMBB);

  // Update the branch instruction if necessary.
  if (Br) {
    assert(Br->getOperand(0).getMBB() == &Succ &&
           "Didn't start with the right target!");
    Br->getOperand(0).setMBB(&NewMBB);

    // If this successor was reached through a branch rather than fallthrough,
    // we might have *broken* fallthrough and so need to inject a new
    // unconditional branch.
    if (!UncondBr) {
      MachineBasicBlock &OldLayoutSucc =
          *std::next(MachineFunction::iterator(&NewMBB));
      assert(MBB.isSuccessor(&OldLayoutSucc) &&
             "Without an unconditional branch, the old layout successor should "
             "be an actual successor!");
      auto BrBuilder =
          BuildMI(&MBB, DebugLoc(), TII.get(X86::JMP_1)).addMBB(&OldLayoutSucc);
      // Update the unconditional branch now that we've added one.
      UncondBr = &*BrBuilder;
    }

    // Insert unconditional "jump Succ" instruction in the new block if
    // necessary.
    if (!NewMBB.isLayoutSuccessor(&Succ)) {
      SmallVector<MachineOperand, 4> Cond;
      TII.insertBranch(NewMBB, &Succ, nullptr, Cond, Br->getDebugLoc());
    }
  } else {
    assert(!UncondBr &&
           "Cannot have a branchless successor and an unconditional branch!");
    assert(NewMBB.isLayoutSuccessor(&Succ) &&
           "A non-branch successor must have been a layout successor before "
           "and now is a layout successor of the new block.");
  }

  // If this is the only edge to the successor, we can just replace it in the
  // CFG. Otherwise we need to add a new entry in the CFG for the new
  // successor.
  if (SuccCount == 1) {
    MBB.replaceSuccessor(&Succ, &NewMBB);
  } else {
    MBB.splitSuccessor(&Succ, &NewMBB);
  }

  // Hook up the edge from the new basic block to the old successor in the CFG.
  NewMBB.addSuccessor(&Succ);

  // Fix PHI nodes in Succ so they refer to NewMBB instead of MBB.
  for (MachineInstr &MI : Succ) {
    if (!MI.isPHI())
      break;
    for (int OpIdx = 1, NumOps = MI.getNumOperands(); OpIdx < NumOps;
         OpIdx += 2) {
      MachineOperand &OpV = MI.getOperand(OpIdx);
      MachineOperand &OpMBB = MI.getOperand(OpIdx + 1);
      assert(OpMBB.isMBB() && "Block operand to a PHI is not a block!");
      if (OpMBB.getMBB() != &MBB)
        continue;

      // If this is the last edge to the succesor, just replace MBB in the PHI
      if (SuccCount == 1) {
        OpMBB.setMBB(&NewMBB);
        break;
      }

      // Otherwise, append a new pair of operands for the new incoming edge.
      MI.addOperand(MF, OpV);
      MI.addOperand(MF, MachineOperand::CreateMBB(&NewMBB));
      break;
    }
  }

  // Inherit live-ins from the successor
  for (auto &LI : Succ.liveins())
    NewMBB.addLiveIn(LI);

  LLVM_DEBUG(dbgs() << "  Split edge from '" << MBB.getName() << "' to '"
                    << Succ.getName() << "'.\n");
  return NewMBB;
}

/// Removing duplicate PHI operands to leave the PHI in a canonical and
/// predictable form.
///
/// FIXME: It's really frustrating that we have to do this, but SSA-form in MIR
/// isn't what you might expect. We may have multiple entries in PHI nodes for
/// a single predecessor. This makes CFG-updating extremely complex, so here we
/// simplify all PHI nodes to a model even simpler than the IR's model: exactly
/// one entry per predecessor, regardless of how many edges there are.
static void canonicalizePHIOperands(MachineFunction &MF) {
  SmallPtrSet<MachineBasicBlock *, 4> Preds;
  SmallVector<int, 4> DupIndices;
  for (auto &MBB : MF)
    for (auto &MI : MBB) {
      if (!MI.isPHI())
        break;

      // First we scan the operands of the PHI looking for duplicate entries
      // a particular predecessor. We retain the operand index of each duplicate
      // entry found.
      for (int OpIdx = 1, NumOps = MI.getNumOperands(); OpIdx < NumOps;
           OpIdx += 2)
        if (!Preds.insert(MI.getOperand(OpIdx + 1).getMBB()).second)
          DupIndices.push_back(OpIdx);

      // Now walk the duplicate indices, removing both the block and value. Note
      // that these are stored as a vector making this element-wise removal
      // :w
      // potentially quadratic.
      //
      // FIXME: It is really frustrating that we have to use a quadratic
      // removal algorithm here. There should be a better way, but the use-def
      // updates required make that impossible using the public API.
      //
      // Note that we have to process these backwards so that we don't
      // invalidate other indices with each removal.
      while (!DupIndices.empty()) {
        int OpIdx = DupIndices.pop_back_val();
        // Remove both the block and value operand, again in reverse order to
        // preserve indices.
        MI.removeOperand(OpIdx + 1);
        MI.removeOperand(OpIdx);
      }

      Preds.clear();
    }
}

/// Helper to scan a function for loads vulnerable to misspeculation that we
/// want to harden.
///
/// We use this to avoid making changes to functions where there is nothing we
/// need to do to harden against misspeculation.
static bool hasVulnerableLoad(MachineFunction &MF) {
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      // Loads within this basic block after an LFENCE are not at risk of
      // speculatively executing with invalid predicates from prior control
      // flow. So break out of this block but continue scanning the function.
      if (MI.getOpcode() == X86::LFENCE)
        break;

      // Looking for loads only.
      if (!MI.mayLoad())
        continue;

      // An MFENCE is modeled as a load but isn't vulnerable to misspeculation.
      if (MI.getOpcode() == X86::MFENCE)
        continue;

      // We found a load.
      return true;
    }
  }

  // No loads found.
  return false;
}

bool X86SpeculativeLoadHardeningPass::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** " << getPassName() << " : " << MF.getName()
                    << " **********\n");

  // Only run if this pass is forced enabled or we detect the relevant function
  // attribute requesting SLH.
  if (!EnableSpeculativeLoadHardening &&
      !MF.getFunction().hasFnAttribute(Attribute::SpeculativeLoadHardening))
    return false;

  Subtarget = &MF.getSubtarget<X86Subtarget>();
  MRI = &MF.getRegInfo();
  TII = Subtarget->getInstrInfo();
  TRI = Subtarget->getRegisterInfo();

  // FIXME: Support for 32-bit.
  PS.emplace(MF, &X86::GR64_NOSPRegClass);

  if (MF.begin() == MF.end())
    // Nothing to do for a degenerate empty function...
    return false;

  // We support an alternative hardening technique based on a debug flag.
  if (HardenEdgesWithLFENCE) {
    hardenEdgesWithLFENCE(MF);
    return true;
  }

  // Create a dummy debug loc to use for all the generated code here.
  DebugLoc Loc;

  MachineBasicBlock &Entry = *MF.begin();
  auto EntryInsertPt = Entry.SkipPHIsLabelsAndDebug(Entry.begin());

  // Do a quick scan to see if we have any checkable loads.
  bool HasVulnerableLoad = hasVulnerableLoad(MF);

  // See if we have any conditional branching blocks that we will need to trace
  // predicate state through.
  SmallVector<BlockCondInfo, 16> Infos = collectBlockCondInfo(MF);

  // If we have no interesting conditions or loads, nothing to do here.
  if (!HasVulnerableLoad && Infos.empty())
    return true;

  // The poison value is required to be an all-ones value for many aspects of
  // this mitigation.
  const int PoisonVal = -1;
  PS->PoisonReg = MRI->createVirtualRegister(PS->RC);
  BuildMI(Entry, EntryInsertPt, Loc, TII->get(X86::MOV64ri32), PS->PoisonReg)
      .addImm(PoisonVal);
  ++NumInstsInserted;

  // If we have loads being hardened and we've asked for call and ret edges to
  // get a full fence-based mitigation, inject that fence.
  if (HasVulnerableLoad && FenceCallAndRet) {
    // We need to insert an LFENCE at the start of the function to suspend any
    // incoming misspeculation from the caller. This helps two-fold: the caller
    // may not have been protected as this code has been, and this code gets to
    // not take any specific action to protect across calls.
    // FIXME: We could skip this for functions which unconditionally return
    // a constant.
    BuildMI(Entry, EntryInsertPt, Loc, TII->get(X86::LFENCE));
    ++NumInstsInserted;
    ++NumLFENCEsInserted;
  }

  // If we guarded the entry with an LFENCE and have no conditionals to protect
  // in blocks, then we're done.
  if (FenceCallAndRet && Infos.empty())
    // We may have changed the function's code at this point to insert fences.
    return true;

  // For every basic block in the function which can b
  if (HardenInterprocedurally && !FenceCallAndRet) {
    // Set up the predicate state by extracting it from the incoming stack
    // pointer so we pick up any misspeculation in our caller.
    PS->InitialReg = extractPredStateFromSP(Entry, EntryInsertPt, Loc);
  } else {
    // Otherwise, just build the predicate state itself by zeroing a register
    // as we don't need any initial state.
    PS->InitialReg = MRI->createVirtualRegister(PS->RC);
    Register PredStateSubReg = MRI->createVirtualRegister(&X86::GR32RegClass);
    auto ZeroI = BuildMI(Entry, EntryInsertPt, Loc, TII->get(X86::MOV32r0),
                         PredStateSubReg);
    ++NumInstsInserted;
    MachineOperand *ZeroEFLAGSDefOp =
        ZeroI->findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr);
    assert(ZeroEFLAGSDefOp && ZeroEFLAGSDefOp->isImplicit() &&
           "Must have an implicit def of EFLAGS!");
    ZeroEFLAGSDefOp->setIsDead(true);
    BuildMI(Entry, EntryInsertPt, Loc, TII->get(X86::SUBREG_TO_REG),
            PS->InitialReg)
        .addImm(0)
        .addReg(PredStateSubReg)
        .addImm(X86::sub_32bit);
  }

  // We're going to need to trace predicate state throughout the function's
  // CFG. Prepare for this by setting up our initial state of PHIs with unique
  // predecessor entries and all the initial predicate state.
  canonicalizePHIOperands(MF);

  // Track the updated values in an SSA updater to rewrite into SSA form at the
  // end.
  PS->SSA.Initialize(PS->InitialReg);
  PS->SSA.AddAvailableValue(&Entry, PS->InitialReg);

  // Trace through the CFG.
  auto CMovs = tracePredStateThroughCFG(MF, Infos);

  // We may also enter basic blocks in this function via exception handling
  // control flow. Here, if we are hardening interprocedurally, we need to
  // re-capture the predicate state from the throwing code. In the Itanium ABI,
  // the throw will always look like a call to __cxa_throw and will have the
  // predicate state in the stack pointer, so extract fresh predicate state from
  // the stack pointer and make it available in SSA.
  // FIXME: Handle non-itanium ABI EH models.
  if (HardenInterprocedurally) {
    for (MachineBasicBlock &MBB : MF) {
      assert(!MBB.isEHScopeEntry() && "Only Itanium ABI EH supported!");
      assert(!MBB.isEHFuncletEntry() && "Only Itanium ABI EH supported!");
      assert(!MBB.isCleanupFuncletEntry() && "Only Itanium ABI EH supported!");
      if (!MBB.isEHPad())
        continue;
      PS->SSA.AddAvailableValue(
          &MBB,
          extractPredStateFromSP(MBB, MBB.SkipPHIsAndLabels(MBB.begin()), Loc));
    }
  }

  if (HardenIndirectCallsAndJumps) {
    // If we are going to harden calls and jumps we need to unfold their memory
    // operands.
    unfoldCallAndJumpLoads(MF);

    // Then we trace predicate state through the indirect branches.
    auto IndirectBrCMovs = tracePredStateThroughIndirectBranches(MF);
    CMovs.append(IndirectBrCMovs.begin(), IndirectBrCMovs.end());
  }

  // Now that we have the predicate state available at the start of each block
  // in the CFG, trace it through each block, hardening vulnerable instructions
  // as we go.
  tracePredStateThroughBlocksAndHarden(MF);

  // Now rewrite all the uses of the pred state using the SSA updater to insert
  // PHIs connecting the state between blocks along the CFG edges.
  for (MachineInstr *CMovI : CMovs)
    for (MachineOperand &Op : CMovI->operands()) {
      if (!Op.isReg() || Op.getReg() != PS->InitialReg)
        continue;

      PS->SSA.RewriteUse(Op);
    }

  LLVM_DEBUG(dbgs() << "Final speculative load hardened function:\n"; MF.dump();
             dbgs() << "\n"; MF.verify(this));
  return true;
}

/// Implements the naive hardening approach of putting an LFENCE after every
/// potentially mis-predicted control flow construct.
///
/// We include this as an alternative mostly for the purpose of comparison. The
/// performance impact of this is expected to be extremely severe and not
/// practical for any real-world users.
void X86SpeculativeLoadHardeningPass::hardenEdgesWithLFENCE(
    MachineFunction &MF) {
  // First, we scan the function looking for blocks that are reached along edges
  // that we might want to harden.
  SmallSetVector<MachineBasicBlock *, 8> Blocks;
  for (MachineBasicBlock &MBB : MF) {
    // If there are no or only one successor, nothing to do here.
    if (MBB.succ_size() <= 1)
      continue;

    // Skip blocks unless their terminators start with a branch. Other
    // terminators don't seem interesting for guarding against misspeculation.
    auto TermIt = MBB.getFirstTerminator();
    if (TermIt == MBB.end() || !TermIt->isBranch())
      continue;

    // Add all the non-EH-pad succossors to the blocks we want to harden. We
    // skip EH pads because there isn't really a condition of interest on
    // entering.
    for (MachineBasicBlock *SuccMBB : MBB.successors())
      if (!SuccMBB->isEHPad())
        Blocks.insert(SuccMBB);
  }

  for (MachineBasicBlock *MBB : Blocks) {
    auto InsertPt = MBB->SkipPHIsAndLabels(MBB->begin());
    BuildMI(*MBB, InsertPt, DebugLoc(), TII->get(X86::LFENCE));
    ++NumInstsInserted;
    ++NumLFENCEsInserted;
  }
}

SmallVector<X86SpeculativeLoadHardeningPass::BlockCondInfo, 16>
X86SpeculativeLoadHardeningPass::collectBlockCondInfo(MachineFunction &MF) {
  SmallVector<BlockCondInfo, 16> Infos;

  // Walk the function and build up a summary for each block's conditions that
  // we need to trace through.
  for (MachineBasicBlock &MBB : MF) {
    // If there are no or only one successor, nothing to do here.
    if (MBB.succ_size() <= 1)
      continue;

    // We want to reliably handle any conditional branch terminators in the
    // MBB, so we manually analyze the branch. We can handle all of the
    // permutations here, including ones that analyze branch cannot.
    //
    // The approach is to walk backwards across the terminators, resetting at
    // any unconditional non-indirect branch, and track all conditional edges
    // to basic blocks as well as the fallthrough or unconditional successor
    // edge. For each conditional edge, we track the target and the opposite
    // condition code in order to inject a "no-op" cmov into that successor
    // that will harden the predicate. For the fallthrough/unconditional
    // edge, we inject a separate cmov for each conditional branch with
    // matching condition codes. This effectively implements an "and" of the
    // condition flags, even if there isn't a single condition flag that would
    // directly implement that. We don't bother trying to optimize either of
    // these cases because if such an optimization is possible, LLVM should
    // have optimized the conditional *branches* in that way already to reduce
    // instruction count. This late, we simply assume the minimal number of
    // branch instructions is being emitted and use that to guide our cmov
    // insertion.

    BlockCondInfo Info = {&MBB, {}, nullptr};

    // Now walk backwards through the terminators and build up successors they
    // reach and the conditions.
    for (MachineInstr &MI : llvm::reverse(MBB)) {
      // Once we've handled all the terminators, we're done.
      if (!MI.isTerminator())
        break;

      // If we see a non-branch terminator, we can't handle anything so bail.
      if (!MI.isBranch()) {
        Info.CondBrs.clear();
        break;
      }

      // If we see an unconditional branch, reset our state, clear any
      // fallthrough, and set this is the "else" successor.
      if (MI.getOpcode() == X86::JMP_1) {
        Info.CondBrs.clear();
        Info.UncondBr = &MI;
        continue;
      }

      // If we get an invalid condition, we have an indirect branch or some
      // other unanalyzable "fallthrough" case. We model this as a nullptr for
      // the destination so we can still guard any conditional successors.
      // Consider code sequences like:
      // ```
      //   jCC L1
      //   jmpq *%rax
      // ```
      // We still want to harden the edge to `L1`.
      if (X86::getCondFromBranch(MI) == X86::COND_INVALID) {
        Info.CondBrs.clear();
        Info.UncondBr = &MI;
        continue;
      }

      // We have a vanilla conditional branch, add it to our list.
      Info.CondBrs.push_back(&MI);
    }
    if (Info.CondBrs.empty()) {
      ++NumBranchesUntraced;
      LLVM_DEBUG(dbgs() << "WARNING: unable to secure successors of block:\n";
                 MBB.dump());
      continue;
    }

    Infos.push_back(Info);
  }

  return Infos;
}

/// Trace the predicate state through the CFG, instrumenting each conditional
/// branch such that misspeculation through an edge will poison the predicate
/// state.
///
/// Returns the list of inserted CMov instructions so that they can have their
/// uses of the predicate state rewritten into proper SSA form once it is
/// complete.
SmallVector<MachineInstr *, 16>
X86SpeculativeLoadHardeningPass::tracePredStateThroughCFG(
    MachineFunction &MF, ArrayRef<BlockCondInfo> Infos) {
  // Collect the inserted cmov instructions so we can rewrite their uses of the
  // predicate state into SSA form.
  SmallVector<MachineInstr *, 16> CMovs;

  // Now walk all of the basic blocks looking for ones that end in conditional
  // jumps where we need to update this register along each edge.
  for (const BlockCondInfo &Info : Infos) {
    MachineBasicBlock &MBB = *Info.MBB;
    const SmallVectorImpl<MachineInstr *> &CondBrs = Info.CondBrs;
    MachineInstr *UncondBr = Info.UncondBr;

    LLVM_DEBUG(dbgs() << "Tracing predicate through block: " << MBB.getName()
                      << "\n");
    ++NumCondBranchesTraced;

    // Compute the non-conditional successor as either the target of any
    // unconditional branch or the layout successor.
    MachineBasicBlock *UncondSucc =
        UncondBr ? (UncondBr->getOpcode() == X86::JMP_1
                        ? UncondBr->getOperand(0).getMBB()
                        : nullptr)
                 : &*std::next(MachineFunction::iterator(&MBB));

    // Count how many edges there are to any given successor.
    SmallDenseMap<MachineBasicBlock *, int> SuccCounts;
    if (UncondSucc)
      ++SuccCounts[UncondSucc];
    for (auto *CondBr : CondBrs)
      ++SuccCounts[CondBr->getOperand(0).getMBB()];

    // A lambda to insert cmov instructions into a block checking all of the
    // condition codes in a sequence.
    auto BuildCheckingBlockForSuccAndConds =
        [&](MachineBasicBlock &MBB, MachineBasicBlock &Succ, int SuccCount,
            MachineInstr *Br, MachineInstr *&UncondBr,
            ArrayRef<X86::CondCode> Conds) {
          // First, we split the edge to insert the checking block into a safe
          // location.
          auto &CheckingMBB =
              (SuccCount == 1 && Succ.pred_size() == 1)
                  ? Succ
                  : splitEdge(MBB, Succ, SuccCount, Br, UncondBr, *TII);

          bool LiveEFLAGS = Succ.isLiveIn(X86::EFLAGS);
          if (!LiveEFLAGS)
            CheckingMBB.addLiveIn(X86::EFLAGS);

          // Now insert the cmovs to implement the checks.
          auto InsertPt = CheckingMBB.begin();
          assert((InsertPt == CheckingMBB.end() || !InsertPt->isPHI()) &&
                 "Should never have a PHI in the initial checking block as it "
                 "always has a single predecessor!");

          // We will wire each cmov to each other, but need to start with the
          // incoming pred state.
          unsigned CurStateReg = PS->InitialReg;

          for (X86::CondCode Cond : Conds) {
            int PredStateSizeInBytes = TRI->getRegSizeInBits(*PS->RC) / 8;
            auto CMovOp = X86::getCMovOpcode(PredStateSizeInBytes);

            Register UpdatedStateReg = MRI->createVirtualRegister(PS->RC);
            // Note that we intentionally use an empty debug location so that
            // this picks up the preceding location.
            auto CMovI = BuildMI(CheckingMBB, InsertPt, DebugLoc(),
                                 TII->get(CMovOp), UpdatedStateReg)
                             .addReg(CurStateReg)
                             .addReg(PS->PoisonReg)
                             .addImm(Cond);
            // If this is the last cmov and the EFLAGS weren't originally
            // live-in, mark them as killed.
            if (!LiveEFLAGS && Cond == Conds.back())
              CMovI->findRegisterUseOperand(X86::EFLAGS, /*TRI=*/nullptr)
                  ->setIsKill(true);

            ++NumInstsInserted;
            LLVM_DEBUG(dbgs() << "  Inserting cmov: "; CMovI->dump();
                       dbgs() << "\n");

            // The first one of the cmovs will be using the top level
            // `PredStateReg` and need to get rewritten into SSA form.
            if (CurStateReg == PS->InitialReg)
              CMovs.push_back(&*CMovI);

            // The next cmov should start from this one's def.
            CurStateReg = UpdatedStateReg;
          }

          // And put the last one into the available values for SSA form of our
          // predicate state.
          PS->SSA.AddAvailableValue(&CheckingMBB, CurStateReg);
        };

    std::vector<X86::CondCode> UncondCodeSeq;
    for (auto *CondBr : CondBrs) {
      MachineBasicBlock &Succ = *CondBr->getOperand(0).getMBB();
      int &SuccCount = SuccCounts[&Succ];

      X86::CondCode Cond = X86::getCondFromBranch(*CondBr);
      X86::CondCode InvCond = X86::GetOppositeBranchCondition(Cond);
      UncondCodeSeq.push_back(Cond);

      BuildCheckingBlockForSuccAndConds(MBB, Succ, SuccCount, CondBr, UncondBr,
                                        {InvCond});

      // Decrement the successor count now that we've split one of the edges.
      // We need to keep the count of edges to the successor accurate in order
      // to know above when to *replace* the successor in the CFG vs. just
      // adding the new successor.
      --SuccCount;
    }

    // Since we may have split edges and changed the number of successors,
    // normalize the probabilities. This avoids doing it each time we split an
    // edge.
    MBB.normalizeSuccProbs();

    // Finally, we need to insert cmovs into the "fallthrough" edge. Here, we
    // need to intersect the other condition codes. We can do this by just
    // doing a cmov for each one.
    if (!UncondSucc)
      // If we have no fallthrough to protect (perhaps it is an indirect jump?)
      // just skip this and continue.
      continue;

    assert(SuccCounts[UncondSucc] == 1 &&
           "We should never have more than one edge to the unconditional "
           "successor at this point because every other edge must have been "
           "split above!");

    // Sort and unique the codes to minimize them.
    llvm::sort(UncondCodeSeq);
    UncondCodeSeq.erase(llvm::unique(UncondCodeSeq), UncondCodeSeq.end());

    // Build a checking version of the successor.
    BuildCheckingBlockForSuccAndConds(MBB, *UncondSucc, /*SuccCount*/ 1,
                                      UncondBr, UncondBr, UncondCodeSeq);
  }

  return CMovs;
}

/// Compute the register class for the unfolded load.
///
/// FIXME: This should probably live in X86InstrInfo, potentially by adding
/// a way to unfold into a newly created vreg rather than requiring a register
/// input.
static const TargetRegisterClass *
getRegClassForUnfoldedLoad(MachineFunction &MF, const X86InstrInfo &TII,
                           unsigned Opcode) {
  unsigned Index;
  unsigned UnfoldedOpc = TII.getOpcodeAfterMemoryUnfold(
      Opcode, /*UnfoldLoad*/ true, /*UnfoldStore*/ false, &Index);
  const MCInstrDesc &MCID = TII.get(UnfoldedOpc);
  return TII.getRegClass(MCID, Index, &TII.getRegisterInfo(), MF);
}

void X86SpeculativeLoadHardeningPass::unfoldCallAndJumpLoads(
    MachineFunction &MF) {
  for (MachineBasicBlock &MBB : MF)
    // We use make_early_inc_range here so we can remove instructions if needed
    // without disturbing the iteration.
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB.instrs())) {
      // Must either be a call or a branch.
      if (!MI.isCall() && !MI.isBranch())
        continue;
      // We only care about loading variants of these instructions.
      if (!MI.mayLoad())
        continue;

      switch (MI.getOpcode()) {
      default: {
        LLVM_DEBUG(
            dbgs() << "ERROR: Found an unexpected loading branch or call "
                      "instruction:\n";
            MI.dump(); dbgs() << "\n");
        report_fatal_error("Unexpected loading branch or call!");
      }

      case X86::FARCALL16m:
      case X86::FARCALL32m:
      case X86::FARCALL64m:
      case X86::FARJMP16m:
      case X86::FARJMP32m:
      case X86::FARJMP64m:
        // We cannot mitigate far jumps or calls, but we also don't expect them
        // to be vulnerable to Spectre v1.2 style attacks.
        continue;

      case X86::CALL16m:
      case X86::CALL16m_NT:
      case X86::CALL32m:
      case X86::CALL32m_NT:
      case X86::CALL64m:
      case X86::CALL64m_NT:
      case X86::JMP16m:
      case X86::JMP16m_NT:
      case X86::JMP32m:
      case X86::JMP32m_NT:
      case X86::JMP64m:
      case X86::JMP64m_NT:
      case X86::TAILJMPm64:
      case X86::TAILJMPm64_REX:
      case X86::TAILJMPm:
      case X86::TCRETURNmi64:
      case X86::TCRETURNmi: {
        // Use the generic unfold logic now that we know we're dealing with
        // expected instructions.
        // FIXME: We don't have test coverage for all of these!
        auto *UnfoldedRC = getRegClassForUnfoldedLoad(MF, *TII, MI.getOpcode());
        if (!UnfoldedRC) {
          LLVM_DEBUG(dbgs()
                         << "ERROR: Unable to unfold load from instruction:\n";
                     MI.dump(); dbgs() << "\n");
          report_fatal_error("Unable to unfold load!");
        }
        Register Reg = MRI->createVirtualRegister(UnfoldedRC);
        SmallVector<MachineInstr *, 2> NewMIs;
        // If we were able to compute an unfolded reg class, any failure here
        // is just a programming error so just assert.
        bool Unfolded =
            TII->unfoldMemoryOperand(MF, MI, Reg, /*UnfoldLoad*/ true,
                                     /*UnfoldStore*/ false, NewMIs);
        (void)Unfolded;
        assert(Unfolded &&
               "Computed unfolded register class but failed to unfold");
        // Now stitch the new instructions into place and erase the old one.
        for (auto *NewMI : NewMIs)
          MBB.insert(MI.getIterator(), NewMI);

        // Update the call site info.
        if (MI.isCandidateForCallSiteEntry())
          MF.eraseCallSiteInfo(&MI);

        MI.eraseFromParent();
        LLVM_DEBUG({
          dbgs() << "Unfolded load successfully into:\n";
          for (auto *NewMI : NewMIs) {
            NewMI->dump();
            dbgs() << "\n";
          }
        });
        continue;
      }
      }
      llvm_unreachable("Escaped switch with default!");
    }
}

/// Trace the predicate state through indirect branches, instrumenting them to
/// poison the state if a target is reached that does not match the expected
/// target.
///
/// This is designed to mitigate Spectre variant 1 attacks where an indirect
/// branch is trained to predict a particular target and then mispredicts that
/// target in a way that can leak data. Despite using an indirect branch, this
/// is really a variant 1 style attack: it does not steer execution to an
/// arbitrary or attacker controlled address, and it does not require any
/// special code executing next to the victim. This attack can also be mitigated
/// through retpolines, but those require either replacing indirect branches
/// with conditional direct branches or lowering them through a device that
/// blocks speculation. This mitigation can replace these retpoline-style
/// mitigations for jump tables and other indirect branches within a function
/// when variant 2 isn't a risk while allowing limited speculation. Indirect
/// calls, however, cannot be mitigated through this technique without changing
/// the ABI in a fundamental way.
SmallVector<MachineInstr *, 16>
X86SpeculativeLoadHardeningPass::tracePredStateThroughIndirectBranches(
    MachineFunction &MF) {
  // We use the SSAUpdater to insert PHI nodes for the target addresses of
  // indirect branches. We don't actually need the full power of the SSA updater
  // in this particular case as we always have immediately available values, but
  // this avoids us having to re-implement the PHI construction logic.
  MachineSSAUpdater TargetAddrSSA(MF);
  TargetAddrSSA.Initialize(MRI->createVirtualRegister(&X86::GR64RegClass));

  // Track which blocks were terminated with an indirect branch.
  SmallPtrSet<MachineBasicBlock *, 4> IndirectTerminatedMBBs;

  // We need to know what blocks end up reached via indirect branches. We
  // expect this to be a subset of those whose address is taken and so track it
  // directly via the CFG.
  SmallPtrSet<MachineBasicBlock *, 4> IndirectTargetMBBs;

  // Walk all the blocks which end in an indirect branch and make the
  // target address available.
  for (MachineBasicBlock &MBB : MF) {
    // Find the last terminator.
    auto MII = MBB.instr_rbegin();
    while (MII != MBB.instr_rend() && MII->isDebugInstr())
      ++MII;
    if (MII == MBB.instr_rend())
      continue;
    MachineInstr &TI = *MII;
    if (!TI.isTerminator() || !TI.isBranch())
      // No terminator or non-branch terminator.
      continue;

    unsigned TargetReg;

    switch (TI.getOpcode()) {
    default:
      // Direct branch or conditional branch (leading to fallthrough).
      continue;

    case X86::FARJMP16m:
    case X86::FARJMP32m:
    case X86::FARJMP64m:
      // We cannot mitigate far jumps or calls, but we also don't expect them
      // to be vulnerable to Spectre v1.2 or v2 (self trained) style attacks.
      continue;

    case X86::JMP16m:
    case X86::JMP16m_NT:
    case X86::JMP32m:
    case X86::JMP32m_NT:
    case X86::JMP64m:
    case X86::JMP64m_NT:
      // Mostly as documentation.
      report_fatal_error("Memory operand jumps should have been unfolded!");

    case X86::JMP16r:
      report_fatal_error(
          "Support for 16-bit indirect branches is not implemented.");
    case X86::JMP32r:
      report_fatal_error(
          "Support for 32-bit indirect branches is not implemented.");

    case X86::JMP64r:
      TargetReg = TI.getOperand(0).getReg();
    }

    // We have definitely found an indirect  branch. Verify that there are no
    // preceding conditional branches as we don't yet support that.
    if (llvm::any_of(MBB.terminators(), [&](MachineInstr &OtherTI) {
          return !OtherTI.isDebugInstr() && &OtherTI != &TI;
        })) {
      LLVM_DEBUG({
        dbgs() << "ERROR: Found other terminators in a block with an indirect "
                  "branch! This is not yet supported! Terminator sequence:\n";
        for (MachineInstr &MI : MBB.terminators()) {
          MI.dump();
          dbgs() << '\n';
        }
      });
      report_fatal_error("Unimplemented terminator sequence!");
    }

    // Make the target register an available value for this block.
    TargetAddrSSA.AddAvailableValue(&MBB, TargetReg);
    IndirectTerminatedMBBs.insert(&MBB);

    // Add all the successors to our target candidates.
    for (MachineBasicBlock *Succ : MBB.successors())
      IndirectTargetMBBs.insert(Succ);
  }

  // Keep track of the cmov instructions we insert so we can return them.
  SmallVector<MachineInstr *, 16> CMovs;

  // If we didn't find any indirect branches with targets, nothing to do here.
  if (IndirectTargetMBBs.empty())
    return CMovs;

  // We found indirect branches and targets that need to be instrumented to
  // harden loads within them. Walk the blocks of the function (to get a stable
  // ordering) and instrument each target of an indirect branch.
  for (MachineBasicBlock &MBB : MF) {
    // Skip the blocks that aren't candidate targets.
    if (!IndirectTargetMBBs.count(&MBB))
      continue;

    // We don't expect EH pads to ever be reached via an indirect branch. If
    // this is desired for some reason, we could simply skip them here rather
    // than asserting.
    assert(!MBB.isEHPad() &&
           "Unexpected EH pad as target of an indirect branch!");

    // We should never end up threading EFLAGS into a block to harden
    // conditional jumps as there would be an additional successor via the
    // indirect branch. As a consequence, all such edges would be split before
    // reaching here, and the inserted block will handle the EFLAGS-based
    // hardening.
    assert(!MBB.isLiveIn(X86::EFLAGS) &&
           "Cannot check within a block that already has live-in EFLAGS!");

    // We can't handle having non-indirect edges into this block unless this is
    // the only successor and we can synthesize the necessary target address.
    for (MachineBasicBlock *Pred : MBB.predecessors()) {
      // If we've already handled this by extracting the target directly,
      // nothing to do.
      if (IndirectTerminatedMBBs.count(Pred))
        continue;

      // Otherwise, we have to be the only successor. We generally expect this
      // to be true as conditional branches should have had a critical edge
      // split already. We don't however need to worry about EH pad successors
      // as they'll happily ignore the target and their hardening strategy is
      // resilient to all ways in which they could be reached speculatively.
      if (!llvm::all_of(Pred->successors(), [&](MachineBasicBlock *Succ) {
            return Succ->isEHPad() || Succ == &MBB;
          })) {
        LLVM_DEBUG({
          dbgs() << "ERROR: Found conditional entry to target of indirect "
                    "branch!\n";
          Pred->dump();
          MBB.dump();
        });
        report_fatal_error("Cannot harden a conditional entry to a target of "
                           "an indirect branch!");
      }

      // Now we need to compute the address of this block and install it as a
      // synthetic target in the predecessor. We do this at the bottom of the
      // predecessor.
      auto InsertPt = Pred->getFirstTerminator();
      Register TargetReg = MRI->createVirtualRegister(&X86::GR64RegClass);
      if (MF.getTarget().getCodeModel() == CodeModel::Small &&
          !Subtarget->isPositionIndependent()) {
        // Directly materialize it into an immediate.
        auto AddrI = BuildMI(*Pred, InsertPt, DebugLoc(),
                             TII->get(X86::MOV64ri32), TargetReg)
                         .addMBB(&MBB);
        ++NumInstsInserted;
        (void)AddrI;
        LLVM_DEBUG(dbgs() << "  Inserting mov: "; AddrI->dump();
                   dbgs() << "\n");
      } else {
        auto AddrI = BuildMI(*Pred, InsertPt, DebugLoc(), TII->get(X86::LEA64r),
                             TargetReg)
                         .addReg(/*Base*/ X86::RIP)
                         .addImm(/*Scale*/ 1)
                         .addReg(/*Index*/ 0)
                         .addMBB(&MBB)
                         .addReg(/*Segment*/ 0);
        ++NumInstsInserted;
        (void)AddrI;
        LLVM_DEBUG(dbgs() << "  Inserting lea: "; AddrI->dump();
                   dbgs() << "\n");
      }
      // And make this available.
      TargetAddrSSA.AddAvailableValue(Pred, TargetReg);
    }

    // Materialize the needed SSA value of the target. Note that we need the
    // middle of the block as this block might at the bottom have an indirect
    // branch back to itself. We can do this here because at this point, every
    // predecessor of this block has an available value. This is basically just
    // automating the construction of a PHI node for this target.
    Register TargetReg = TargetAddrSSA.GetValueInMiddleOfBlock(&MBB);

    // Insert a comparison of the incoming target register with this block's
    // address. This also requires us to mark the block as having its address
    // taken explicitly.
    MBB.setMachineBlockAddressTaken();
    auto InsertPt = MBB.SkipPHIsLabelsAndDebug(MBB.begin());
    if (MF.getTarget().getCodeModel() == CodeModel::Small &&
        !Subtarget->isPositionIndependent()) {
      // Check directly against a relocated immediate when we can.
      auto CheckI = BuildMI(MBB, InsertPt, DebugLoc(), TII->get(X86::CMP64ri32))
                        .addReg(TargetReg, RegState::Kill)
                        .addMBB(&MBB);
      ++NumInstsInserted;
      (void)CheckI;
      LLVM_DEBUG(dbgs() << "  Inserting cmp: "; CheckI->dump(); dbgs() << "\n");
    } else {
      // Otherwise compute the address into a register first.
      Register AddrReg = MRI->createVirtualRegister(&X86::GR64RegClass);
      auto AddrI =
          BuildMI(MBB, InsertPt, DebugLoc(), TII->get(X86::LEA64r), AddrReg)
              .addReg(/*Base*/ X86::RIP)
              .addImm(/*Scale*/ 1)
              .addReg(/*Index*/ 0)
              .addMBB(&MBB)
              .addReg(/*Segment*/ 0);
      ++NumInstsInserted;
      (void)AddrI;
      LLVM_DEBUG(dbgs() << "  Inserting lea: "; AddrI->dump(); dbgs() << "\n");
      auto CheckI = BuildMI(MBB, InsertPt, DebugLoc(), TII->get(X86::CMP64rr))
                        .addReg(TargetReg, RegState::Kill)
                        .addReg(AddrReg, RegState::Kill);
      ++NumInstsInserted;
      (void)CheckI;
      LLVM_DEBUG(dbgs() << "  Inserting cmp: "; CheckI->dump(); dbgs() << "\n");
    }

    // Now cmov over the predicate if the comparison wasn't equal.
    int PredStateSizeInBytes = TRI->getRegSizeInBits(*PS->RC) / 8;
    auto CMovOp = X86::getCMovOpcode(PredStateSizeInBytes);
    Register UpdatedStateReg = MRI->createVirtualRegister(PS->RC);
    auto CMovI =
        BuildMI(MBB, InsertPt, DebugLoc(), TII->get(CMovOp), UpdatedStateReg)
            .addReg(PS->InitialReg)
            .addReg(PS->PoisonReg)
            .addImm(X86::COND_NE);
    CMovI->findRegisterUseOperand(X86::EFLAGS, /*TRI=*/nullptr)
        ->setIsKill(true);
    ++NumInstsInserted;
    LLVM_DEBUG(dbgs() << "  Inserting cmov: "; CMovI->dump(); dbgs() << "\n");
    CMovs.push_back(&*CMovI);

    // And put the new value into the available values for SSA form of our
    // predicate state.
    PS->SSA.AddAvailableValue(&MBB, UpdatedStateReg);
  }

  // Return all the newly inserted cmov instructions of the predicate state.
  return CMovs;
}

// Returns true if the MI has EFLAGS as a register def operand and it's live,
// otherwise it returns false
static bool isEFLAGSDefLive(const MachineInstr &MI) {
  if (const MachineOperand *DefOp =
          MI.findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr)) {
    return !DefOp->isDead();
  }
  return false;
}

static bool isEFLAGSLive(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                         const TargetRegisterInfo &TRI) {
  // Check if EFLAGS are alive by seeing if there is a def of them or they
  // live-in, and then seeing if that def is in turn used.
  for (MachineInstr &MI : llvm::reverse(llvm::make_range(MBB.begin(), I))) {
    if (MachineOperand *DefOp =
            MI.findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr)) {
      // If the def is dead, then EFLAGS is not live.
      if (DefOp->isDead())
        return false;

      // Otherwise we've def'ed it, and it is live.
      return true;
    }
    // While at this instruction, also check if we use and kill EFLAGS
    // which means it isn't live.
    if (MI.killsRegister(X86::EFLAGS, &TRI))
      return false;
  }

  // If we didn't find anything conclusive (neither definitely alive or
  // definitely dead) return whether it lives into the block.
  return MBB.isLiveIn(X86::EFLAGS);
}

/// Trace the predicate state through each of the blocks in the function,
/// hardening everything necessary along the way.
///
/// We call this routine once the initial predicate state has been established
/// for each basic block in the function in the SSA updater. This routine traces
/// it through the instructions within each basic block, and for non-returning
/// blocks informs the SSA updater about the final state that lives out of the
/// block. Along the way, it hardens any vulnerable instruction using the
/// currently valid predicate state. We have to do these two things together
/// because the SSA updater only works across blocks. Within a block, we track
/// the current predicate state directly and update it as it changes.
///
/// This operates in two passes over each block. First, we analyze the loads in
/// the block to determine which strategy will be used to harden them: hardening
/// the address or hardening the loaded value when loaded into a register
/// amenable to hardening. We have to process these first because the two
/// strategies may interact -- later hardening may change what strategy we wish
/// to use. We also will analyze data dependencies between loads and avoid
/// hardening those loads that are data dependent on a load with a hardened
/// address. We also skip hardening loads already behind an LFENCE as that is
/// sufficient to harden them against misspeculation.
///
/// Second, we actively trace the predicate state through the block, applying
/// the hardening steps we determined necessary in the first pass as we go.
///
/// These two passes are applied to each basic block. We operate one block at a
/// time to simplify reasoning about reachability and sequencing.
void X86SpeculativeLoadHardeningPass::tracePredStateThroughBlocksAndHarden(
    MachineFunction &MF) {
  SmallPtrSet<MachineInstr *, 16> HardenPostLoad;
  SmallPtrSet<MachineInstr *, 16> HardenLoadAddr;

  SmallSet<unsigned, 16> HardenedAddrRegs;

  SmallDenseMap<unsigned, unsigned, 32> AddrRegToHardenedReg;

  // Track the set of load-dependent registers through the basic block. Because
  // the values of these registers have an existing data dependency on a loaded
  // value which we would have checked, we can omit any checks on them.
  SparseBitVector<> LoadDepRegs;

  for (MachineBasicBlock &MBB : MF) {
    // The first pass over the block: collect all the loads which can have their
    // loaded value hardened and all the loads that instead need their address
    // hardened. During this walk we propagate load dependence for address
    // hardened loads and also look for LFENCE to stop hardening wherever
    // possible. When deciding whether or not to harden the loaded value or not,
    // we check to see if any registers used in the address will have been
    // hardened at this point and if so, harden any remaining address registers
    // as that often successfully re-uses hardened addresses and minimizes
    // instructions.
    //
    // FIXME: We should consider an aggressive mode where we continue to keep as
    // many loads value hardened even when some address register hardening would
    // be free (due to reuse).
    //
    // Note that we only need this pass if we are actually hardening loads.
    if (HardenLoads)
      for (MachineInstr &MI : MBB) {
        // We naively assume that all def'ed registers of an instruction have
        // a data dependency on all of their operands.
        // FIXME: Do a more careful analysis of x86 to build a conservative
        // model here.
        if (llvm::any_of(MI.uses(), [&](MachineOperand &Op) {
              return Op.isReg() && LoadDepRegs.test(Op.getReg());
            }))
          for (MachineOperand &Def : MI.defs())
            if (Def.isReg())
              LoadDepRegs.set(Def.getReg());

        // Both Intel and AMD are guiding that they will change the semantics of
        // LFENCE to be a speculation barrier, so if we see an LFENCE, there is
        // no more need to guard things in this block.
        if (MI.getOpcode() == X86::LFENCE)
          break;

        // If this instruction cannot load, nothing to do.
        if (!MI.mayLoad())
          continue;

        // Some instructions which "load" are trivially safe or unimportant.
        if (MI.getOpcode() == X86::MFENCE)
          continue;

        // Extract the memory operand information about this instruction.
        const int MemRefBeginIdx = X86::getFirstAddrOperandIdx(MI);
        if (MemRefBeginIdx < 0) {
          LLVM_DEBUG(dbgs()
                         << "WARNING: unable to harden loading instruction: ";
                     MI.dump());
          continue;
        }

        MachineOperand &BaseMO =
            MI.getOperand(MemRefBeginIdx + X86::AddrBaseReg);
        MachineOperand &IndexMO =
            MI.getOperand(MemRefBeginIdx + X86::AddrIndexReg);

        // If we have at least one (non-frame-index, non-RIP) register operand,
        // and neither operand is load-dependent, we need to check the load.
        unsigned BaseReg = 0, IndexReg = 0;
        if (!BaseMO.isFI() && BaseMO.getReg() != X86::RIP &&
            BaseMO.getReg() != X86::NoRegister)
          BaseReg = BaseMO.getReg();
        if (IndexMO.getReg() != X86::NoRegister)
          IndexReg = IndexMO.getReg();

        if (!BaseReg && !IndexReg)
          // No register operands!
          continue;

        // If any register operand is dependent, this load is dependent and we
        // needn't check it.
        // FIXME: Is this true in the case where we are hardening loads after
        // they complete? Unclear, need to investigate.
        if ((BaseReg && LoadDepRegs.test(BaseReg)) ||
            (IndexReg && LoadDepRegs.test(IndexReg)))
          continue;

        // If post-load hardening is enabled, this load is compatible with
        // post-load hardening, and we aren't already going to harden one of the
        // address registers, queue it up to be hardened post-load. Notably,
        // even once hardened this won't introduce a useful dependency that
        // could prune out subsequent loads.
        if (EnablePostLoadHardening && X86InstrInfo::isDataInvariantLoad(MI) &&
            !isEFLAGSDefLive(MI) && MI.getDesc().getNumDefs() == 1 &&
            MI.getOperand(0).isReg() &&
            canHardenRegister(MI.getOperand(0).getReg()) &&
            !HardenedAddrRegs.count(BaseReg) &&
            !HardenedAddrRegs.count(IndexReg)) {
          HardenPostLoad.insert(&MI);
          HardenedAddrRegs.insert(MI.getOperand(0).getReg());
          continue;
        }

        // Record this instruction for address hardening and record its register
        // operands as being address-hardened.
        HardenLoadAddr.insert(&MI);
        if (BaseReg)
          HardenedAddrRegs.insert(BaseReg);
        if (IndexReg)
          HardenedAddrRegs.insert(IndexReg);

        for (MachineOperand &Def : MI.defs())
          if (Def.isReg())
            LoadDepRegs.set(Def.getReg());
      }

    // Now re-walk the instructions in the basic block, and apply whichever
    // hardening strategy we have elected. Note that we do this in a second
    // pass specifically so that we have the complete set of instructions for
    // which we will do post-load hardening and can defer it in certain
    // circumstances.
    for (MachineInstr &MI : MBB) {
      if (HardenLoads) {
        // We cannot both require hardening the def of a load and its address.
        assert(!(HardenLoadAddr.count(&MI) && HardenPostLoad.count(&MI)) &&
               "Requested to harden both the address and def of a load!");

        // Check if this is a load whose address needs to be hardened.
        if (HardenLoadAddr.erase(&MI)) {
          const int MemRefBeginIdx = X86::getFirstAddrOperandIdx(MI);
          assert(MemRefBeginIdx >= 0 && "Cannot have an invalid index here!");

          MachineOperand &BaseMO =
              MI.getOperand(MemRefBeginIdx + X86::AddrBaseReg);
          MachineOperand &IndexMO =
              MI.getOperand(MemRefBeginIdx + X86::AddrIndexReg);
          hardenLoadAddr(MI, BaseMO, IndexMO, AddrRegToHardenedReg);
          continue;
        }

        // Test if this instruction is one of our post load instructions (and
        // remove it from the set if so).
        if (HardenPostLoad.erase(&MI)) {
          assert(!MI.isCall() && "Must not try to post-load harden a call!");

          // If this is a data-invariant load and there is no EFLAGS
          // interference, we want to try and sink any hardening as far as
          // possible.
          if (X86InstrInfo::isDataInvariantLoad(MI) && !isEFLAGSDefLive(MI)) {
            // Sink the instruction we'll need to harden as far as we can down
            // the graph.
            MachineInstr *SunkMI = sinkPostLoadHardenedInst(MI, HardenPostLoad);

            // If we managed to sink this instruction, update everything so we
            // harden that instruction when we reach it in the instruction
            // sequence.
            if (SunkMI != &MI) {
              // If in sinking there was no instruction needing to be hardened,
              // we're done.
              if (!SunkMI)
                continue;

              // Otherwise, add this to the set of defs we harden.
              HardenPostLoad.insert(SunkMI);
              continue;
            }
          }

          unsigned HardenedReg = hardenPostLoad(MI);

          // Mark the resulting hardened register as such so we don't re-harden.
          AddrRegToHardenedReg[HardenedReg] = HardenedReg;

          continue;
        }

        // Check for an indirect call or branch that may need its input hardened
        // even if we couldn't find the specific load used, or were able to
        // avoid hardening it for some reason. Note that here we cannot break
        // out afterward as we may still need to handle any call aspect of this
        // instruction.
        if ((MI.isCall() || MI.isBranch()) && HardenIndirectCallsAndJumps)
          hardenIndirectCallOrJumpInstr(MI, AddrRegToHardenedReg);
      }

      // After we finish hardening loads we handle interprocedural hardening if
      // enabled and relevant for this instruction.
      if (!HardenInterprocedurally)
        continue;
      if (!MI.isCall() && !MI.isReturn())
        continue;

      // If this is a direct return (IE, not a tail call) just directly harden
      // it.
      if (MI.isReturn() && !MI.isCall()) {
        hardenReturnInstr(MI);
        continue;
      }

      // Otherwise we have a call. We need to handle transferring the predicate
      // state into a call and recovering it after the call returns (unless this
      // is a tail call).
      assert(MI.isCall() && "Should only reach here for calls!");
      tracePredStateThroughCall(MI);
    }

    HardenPostLoad.clear();
    HardenLoadAddr.clear();
    HardenedAddrRegs.clear();
    AddrRegToHardenedReg.clear();

    // Currently, we only track data-dependent loads within a basic block.
    // FIXME: We should see if this is necessary or if we could be more
    // aggressive here without opening up attack avenues.
    LoadDepRegs.clear();
  }
}

/// Save EFLAGS into the returned GPR. This can in turn be restored with
/// `restoreEFLAGS`.
///
/// Note that LLVM can only lower very simple patterns of saved and restored
/// EFLAGS registers. The restore should always be within the same basic block
/// as the save so that no PHI nodes are inserted.
unsigned X86SpeculativeLoadHardeningPass::saveEFLAGS(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &Loc) {
  // FIXME: Hard coding this to a 32-bit register class seems weird, but matches
  // what instruction selection does.
  Register Reg = MRI->createVirtualRegister(&X86::GR32RegClass);
  // We directly copy the FLAGS register and rely on later lowering to clean
  // this up into the appropriate setCC instructions.
  BuildMI(MBB, InsertPt, Loc, TII->get(X86::COPY), Reg).addReg(X86::EFLAGS);
  ++NumInstsInserted;
  return Reg;
}

/// Restore EFLAGS from the provided GPR. This should be produced by
/// `saveEFLAGS`.
///
/// This must be done within the same basic block as the save in order to
/// reliably lower.
void X86SpeculativeLoadHardeningPass::restoreEFLAGS(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &Loc, Register Reg) {
  BuildMI(MBB, InsertPt, Loc, TII->get(X86::COPY), X86::EFLAGS).addReg(Reg);
  ++NumInstsInserted;
}

/// Takes the current predicate state (in a register) and merges it into the
/// stack pointer. The state is essentially a single bit, but we merge this in
/// a way that won't form non-canonical pointers and also will be preserved
/// across normal stack adjustments.
void X86SpeculativeLoadHardeningPass::mergePredStateIntoSP(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &Loc, unsigned PredStateReg) {
  Register TmpReg = MRI->createVirtualRegister(PS->RC);
  // FIXME: This hard codes a shift distance based on the number of bits needed
  // to stay canonical on 64-bit. We should compute this somehow and support
  // 32-bit as part of that.
  auto ShiftI = BuildMI(MBB, InsertPt, Loc, TII->get(X86::SHL64ri), TmpReg)
                    .addReg(PredStateReg, RegState::Kill)
                    .addImm(47);
  ShiftI->addRegisterDead(X86::EFLAGS, TRI);
  ++NumInstsInserted;
  auto OrI = BuildMI(MBB, InsertPt, Loc, TII->get(X86::OR64rr), X86::RSP)
                 .addReg(X86::RSP)
                 .addReg(TmpReg, RegState::Kill);
  OrI->addRegisterDead(X86::EFLAGS, TRI);
  ++NumInstsInserted;
}

/// Extracts the predicate state stored in the high bits of the stack pointer.
unsigned X86SpeculativeLoadHardeningPass::extractPredStateFromSP(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &Loc) {
  Register PredStateReg = MRI->createVirtualRegister(PS->RC);
  Register TmpReg = MRI->createVirtualRegister(PS->RC);

  // We know that the stack pointer will have any preserved predicate state in
  // its high bit. We just want to smear this across the other bits. Turns out,
  // this is exactly what an arithmetic right shift does.
  BuildMI(MBB, InsertPt, Loc, TII->get(TargetOpcode::COPY), TmpReg)
      .addReg(X86::RSP);
  auto ShiftI =
      BuildMI(MBB, InsertPt, Loc, TII->get(X86::SAR64ri), PredStateReg)
          .addReg(TmpReg, RegState::Kill)
          .addImm(TRI->getRegSizeInBits(*PS->RC) - 1);
  ShiftI->addRegisterDead(X86::EFLAGS, TRI);
  ++NumInstsInserted;

  return PredStateReg;
}

void X86SpeculativeLoadHardeningPass::hardenLoadAddr(
    MachineInstr &MI, MachineOperand &BaseMO, MachineOperand &IndexMO,
    SmallDenseMap<unsigned, unsigned, 32> &AddrRegToHardenedReg) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &Loc = MI.getDebugLoc();

  // Check if EFLAGS are alive by seeing if there is a def of them or they
  // live-in, and then seeing if that def is in turn used.
  bool EFLAGSLive = isEFLAGSLive(MBB, MI.getIterator(), *TRI);

  SmallVector<MachineOperand *, 2> HardenOpRegs;

  if (BaseMO.isFI()) {
    // A frame index is never a dynamically controllable load, so only
    // harden it if we're covering fixed address loads as well.
    LLVM_DEBUG(
        dbgs() << "  Skipping hardening base of explicit stack frame load: ";
        MI.dump(); dbgs() << "\n");
  } else if (BaseMO.getReg() == X86::RSP) {
    // Some idempotent atomic operations are lowered directly to a locked
    // OR with 0 to the top of stack(or slightly offset from top) which uses an
    // explicit RSP register as the base.
    assert(IndexMO.getReg() == X86::NoRegister &&
           "Explicit RSP access with dynamic index!");
    LLVM_DEBUG(
        dbgs() << "  Cannot harden base of explicit RSP offset in a load!");
  } else if (BaseMO.getReg() == X86::RIP ||
             BaseMO.getReg() == X86::NoRegister) {
    // For both RIP-relative addressed loads or absolute loads, we cannot
    // meaningfully harden them because the address being loaded has no
    // dynamic component.
    //
    // FIXME: When using a segment base (like TLS does) we end up with the
    // dynamic address being the base plus -1 because we can't mutate the
    // segment register here. This allows the signed 32-bit offset to point at
    // valid segment-relative addresses and load them successfully.
    LLVM_DEBUG(
        dbgs() << "  Cannot harden base of "
               << (BaseMO.getReg() == X86::RIP ? "RIP-relative" : "no-base")
               << " address in a load!");
  } else {
    assert(BaseMO.isReg() &&
           "Only allowed to have a frame index or register base.");
    HardenOpRegs.push_back(&BaseMO);
  }

  if (IndexMO.getReg() != X86::NoRegister &&
      (HardenOpRegs.empty() ||
       HardenOpRegs.front()->getReg() != IndexMO.getReg()))
    HardenOpRegs.push_back(&IndexMO);

  assert((HardenOpRegs.size() == 1 || HardenOpRegs.size() == 2) &&
         "Should have exactly one or two registers to harden!");
  assert((HardenOpRegs.size() == 1 ||
          HardenOpRegs[0]->getReg() != HardenOpRegs[1]->getReg()) &&
         "Should not have two of the same registers!");

  // Remove any registers that have alreaded been checked.
  llvm::erase_if(HardenOpRegs, [&](MachineOperand *Op) {
    // See if this operand's register has already been checked.
    auto It = AddrRegToHardenedReg.find(Op->getReg());
    if (It == AddrRegToHardenedReg.end())
      // Not checked, so retain this one.
      return false;

    // Otherwise, we can directly update this operand and remove it.
    Op->setReg(It->second);
    return true;
  });
  // If there are none left, we're done.
  if (HardenOpRegs.empty())
    return;

  // Compute the current predicate state.
  Register StateReg = PS->SSA.GetValueAtEndOfBlock(&MBB);

  auto InsertPt = MI.getIterator();

  // If EFLAGS are live and we don't have access to instructions that avoid
  // clobbering EFLAGS we need to save and restore them. This in turn makes
  // the EFLAGS no longer live.
  unsigned FlagsReg = 0;
  if (EFLAGSLive && !Subtarget->hasBMI2()) {
    EFLAGSLive = false;
    FlagsReg = saveEFLAGS(MBB, InsertPt, Loc);
  }

  for (MachineOperand *Op : HardenOpRegs) {
    Register OpReg = Op->getReg();
    auto *OpRC = MRI->getRegClass(OpReg);
    Register TmpReg = MRI->createVirtualRegister(OpRC);

    // If this is a vector register, we'll need somewhat custom logic to handle
    // hardening it.
    if (!Subtarget->hasVLX() && (OpRC->hasSuperClassEq(&X86::VR128RegClass) ||
                                 OpRC->hasSuperClassEq(&X86::VR256RegClass))) {
      assert(Subtarget->hasAVX2() && "AVX2-specific register classes!");
      bool Is128Bit = OpRC->hasSuperClassEq(&X86::VR128RegClass);

      // Move our state into a vector register.
      // FIXME: We could skip this at the cost of longer encodings with AVX-512
      // but that doesn't seem likely worth it.
      Register VStateReg = MRI->createVirtualRegister(&X86::VR128RegClass);
      auto MovI =
          BuildMI(MBB, InsertPt, Loc, TII->get(X86::VMOV64toPQIrr), VStateReg)
              .addReg(StateReg);
      (void)MovI;
      ++NumInstsInserted;
      LLVM_DEBUG(dbgs() << "  Inserting mov: "; MovI->dump(); dbgs() << "\n");

      // Broadcast it across the vector register.
      Register VBStateReg = MRI->createVirtualRegister(OpRC);
      auto BroadcastI = BuildMI(MBB, InsertPt, Loc,
                                TII->get(Is128Bit ? X86::VPBROADCASTQrr
                                                  : X86::VPBROADCASTQYrr),
                                VBStateReg)
                            .addReg(VStateReg);
      (void)BroadcastI;
      ++NumInstsInserted;
      LLVM_DEBUG(dbgs() << "  Inserting broadcast: "; BroadcastI->dump();
                 dbgs() << "\n");

      // Merge our potential poison state into the value with a vector or.
      auto OrI =
          BuildMI(MBB, InsertPt, Loc,
                  TII->get(Is128Bit ? X86::VPORrr : X86::VPORYrr), TmpReg)
              .addReg(VBStateReg)
              .addReg(OpReg);
      (void)OrI;
      ++NumInstsInserted;
      LLVM_DEBUG(dbgs() << "  Inserting or: "; OrI->dump(); dbgs() << "\n");
    } else if (OpRC->hasSuperClassEq(&X86::VR128XRegClass) ||
               OpRC->hasSuperClassEq(&X86::VR256XRegClass) ||
               OpRC->hasSuperClassEq(&X86::VR512RegClass)) {
      assert(Subtarget->hasAVX512() && "AVX512-specific register classes!");
      bool Is128Bit = OpRC->hasSuperClassEq(&X86::VR128XRegClass);
      bool Is256Bit = OpRC->hasSuperClassEq(&X86::VR256XRegClass);
      if (Is128Bit || Is256Bit)
        assert(Subtarget->hasVLX() && "AVX512VL-specific register classes!");

      // Broadcast our state into a vector register.
      Register VStateReg = MRI->createVirtualRegister(OpRC);
      unsigned BroadcastOp = Is128Bit ? X86::VPBROADCASTQrZ128rr
                                      : Is256Bit ? X86::VPBROADCASTQrZ256rr
                                                 : X86::VPBROADCASTQrZrr;
      auto BroadcastI =
          BuildMI(MBB, InsertPt, Loc, TII->get(BroadcastOp), VStateReg)
              .addReg(StateReg);
      (void)BroadcastI;
      ++NumInstsInserted;
      LLVM_DEBUG(dbgs() << "  Inserting broadcast: "; BroadcastI->dump();
                 dbgs() << "\n");

      // Merge our potential poison state into the value with a vector or.
      unsigned OrOp = Is128Bit ? X86::VPORQZ128rr
                               : Is256Bit ? X86::VPORQZ256rr : X86::VPORQZrr;
      auto OrI = BuildMI(MBB, InsertPt, Loc, TII->get(OrOp), TmpReg)
                     .addReg(VStateReg)
                     .addReg(OpReg);
      (void)OrI;
      ++NumInstsInserted;
      LLVM_DEBUG(dbgs() << "  Inserting or: "; OrI->dump(); dbgs() << "\n");
    } else {
      // FIXME: Need to support GR32 here for 32-bit code.
      assert(OpRC->hasSuperClassEq(&X86::GR64RegClass) &&
             "Not a supported register class for address hardening!");

      if (!EFLAGSLive) {
        // Merge our potential poison state into the value with an or.
        auto OrI = BuildMI(MBB, InsertPt, Loc, TII->get(X86::OR64rr), TmpReg)
                       .addReg(StateReg)
                       .addReg(OpReg);
        OrI->addRegisterDead(X86::EFLAGS, TRI);
        ++NumInstsInserted;
        LLVM_DEBUG(dbgs() << "  Inserting or: "; OrI->dump(); dbgs() << "\n");
      } else {
        // We need to avoid touching EFLAGS so shift out all but the least
        // significant bit using the instruction that doesn't update flags.
        auto ShiftI =
            BuildMI(MBB, InsertPt, Loc, TII->get(X86::SHRX64rr), TmpReg)
                .addReg(OpReg)
                .addReg(StateReg);
        (void)ShiftI;
        ++NumInstsInserted;
        LLVM_DEBUG(dbgs() << "  Inserting shrx: "; ShiftI->dump();
                   dbgs() << "\n");
      }
    }

    // Record this register as checked and update the operand.
    assert(!AddrRegToHardenedReg.count(Op->getReg()) &&
           "Should not have checked this register yet!");
    AddrRegToHardenedReg[Op->getReg()] = TmpReg;
    Op->setReg(TmpReg);
    ++NumAddrRegsHardened;
  }

  // And restore the flags if needed.
  if (FlagsReg)
    restoreEFLAGS(MBB, InsertPt, Loc, FlagsReg);
}

MachineInstr *X86SpeculativeLoadHardeningPass::sinkPostLoadHardenedInst(
    MachineInstr &InitialMI, SmallPtrSetImpl<MachineInstr *> &HardenedInstrs) {
  assert(X86InstrInfo::isDataInvariantLoad(InitialMI) &&
         "Cannot get here with a non-invariant load!");
  assert(!isEFLAGSDefLive(InitialMI) &&
         "Cannot get here with a data invariant load "
         "that interferes with EFLAGS!");

  // See if we can sink hardening the loaded value.
  auto SinkCheckToSingleUse =
      [&](MachineInstr &MI) -> std::optional<MachineInstr *> {
    Register DefReg = MI.getOperand(0).getReg();

    // We need to find a single use which we can sink the check. We can
    // primarily do this because many uses may already end up checked on their
    // own.
    MachineInstr *SingleUseMI = nullptr;
    for (MachineInstr &UseMI : MRI->use_instructions(DefReg)) {
      // If we're already going to harden this use, it is data invariant, it
      // does not interfere with EFLAGS, and within our block.
      if (HardenedInstrs.count(&UseMI)) {
        if (!X86InstrInfo::isDataInvariantLoad(UseMI) || isEFLAGSDefLive(UseMI)) {
          // If we've already decided to harden a non-load, we must have sunk
          // some other post-load hardened instruction to it and it must itself
          // be data-invariant.
          assert(X86InstrInfo::isDataInvariant(UseMI) &&
                 "Data variant instruction being hardened!");
          continue;
        }

        // Otherwise, this is a load and the load component can't be data
        // invariant so check how this register is being used.
        const int MemRefBeginIdx = X86::getFirstAddrOperandIdx(UseMI);
        assert(MemRefBeginIdx >= 0 &&
               "Should always have mem references here!");

        MachineOperand &BaseMO =
            UseMI.getOperand(MemRefBeginIdx + X86::AddrBaseReg);
        MachineOperand &IndexMO =
            UseMI.getOperand(MemRefBeginIdx + X86::AddrIndexReg);
        if ((BaseMO.isReg() && BaseMO.getReg() == DefReg) ||
            (IndexMO.isReg() && IndexMO.getReg() == DefReg))
          // The load uses the register as part of its address making it not
          // invariant.
          return {};

        continue;
      }

      if (SingleUseMI)
        // We already have a single use, this would make two. Bail.
        return {};

      // If this single use isn't data invariant, isn't in this block, or has
      // interfering EFLAGS, we can't sink the hardening to it.
      if (!X86InstrInfo::isDataInvariant(UseMI) || UseMI.getParent() != MI.getParent() ||
          isEFLAGSDefLive(UseMI))
        return {};

      // If this instruction defines multiple registers bail as we won't harden
      // all of them.
      if (UseMI.getDesc().getNumDefs() > 1)
        return {};

      // If this register isn't a virtual register we can't walk uses of sanely,
      // just bail. Also check that its register class is one of the ones we
      // can harden.
      Register UseDefReg = UseMI.getOperand(0).getReg();
      if (!canHardenRegister(UseDefReg))
        return {};

      SingleUseMI = &UseMI;
    }

    // If SingleUseMI is still null, there is no use that needs its own
    // checking. Otherwise, it is the single use that needs checking.
    return {SingleUseMI};
  };

  MachineInstr *MI = &InitialMI;
  while (std::optional<MachineInstr *> SingleUse = SinkCheckToSingleUse(*MI)) {
    // Update which MI we're checking now.
    MI = *SingleUse;
    if (!MI)
      break;
  }

  return MI;
}

bool X86SpeculativeLoadHardeningPass::canHardenRegister(Register Reg) {
  // We only support hardening virtual registers.
  if (!Reg.isVirtual())
    return false;

  auto *RC = MRI->getRegClass(Reg);
  int RegBytes = TRI->getRegSizeInBits(*RC) / 8;
  if (RegBytes > 8)
    // We don't support post-load hardening of vectors.
    return false;

  unsigned RegIdx = Log2_32(RegBytes);
  assert(RegIdx < 4 && "Unsupported register size");

  // If this register class is explicitly constrained to a class that doesn't
  // require REX prefix, we may not be able to satisfy that constraint when
  // emitting the hardening instructions, so bail out here.
  // FIXME: This seems like a pretty lame hack. The way this comes up is when we
  // end up both with a NOREX and REX-only register as operands to the hardening
  // instructions. It would be better to fix that code to handle this situation
  // rather than hack around it in this way.
  const TargetRegisterClass *NOREXRegClasses[] = {
      &X86::GR8_NOREXRegClass, &X86::GR16_NOREXRegClass,
      &X86::GR32_NOREXRegClass, &X86::GR64_NOREXRegClass};
  if (RC == NOREXRegClasses[RegIdx])
    return false;

  const TargetRegisterClass *GPRRegClasses[] = {
      &X86::GR8RegClass, &X86::GR16RegClass, &X86::GR32RegClass,
      &X86::GR64RegClass};
  return RC->hasSuperClassEq(GPRRegClasses[RegIdx]);
}

/// Harden a value in a register.
///
/// This is the low-level logic to fully harden a value sitting in a register
/// against leaking during speculative execution.
///
/// Unlike hardening an address that is used by a load, this routine is required
/// to hide *all* incoming bits in the register.
///
/// `Reg` must be a virtual register. Currently, it is required to be a GPR no
/// larger than the predicate state register. FIXME: We should support vector
/// registers here by broadcasting the predicate state.
///
/// The new, hardened virtual register is returned. It will have the same
/// register class as `Reg`.
unsigned X86SpeculativeLoadHardeningPass::hardenValueInRegister(
    Register Reg, MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertPt,
    const DebugLoc &Loc) {
  assert(canHardenRegister(Reg) && "Cannot harden this register!");

  auto *RC = MRI->getRegClass(Reg);
  int Bytes = TRI->getRegSizeInBits(*RC) / 8;
  Register StateReg = PS->SSA.GetValueAtEndOfBlock(&MBB);
  assert((Bytes == 1 || Bytes == 2 || Bytes == 4 || Bytes == 8) &&
         "Unknown register size");

  // FIXME: Need to teach this about 32-bit mode.
  if (Bytes != 8) {
    unsigned SubRegImms[] = {X86::sub_8bit, X86::sub_16bit, X86::sub_32bit};
    unsigned SubRegImm = SubRegImms[Log2_32(Bytes)];
    Register NarrowStateReg = MRI->createVirtualRegister(RC);
    BuildMI(MBB, InsertPt, Loc, TII->get(TargetOpcode::COPY), NarrowStateReg)
        .addReg(StateReg, 0, SubRegImm);
    StateReg = NarrowStateReg;
  }

  unsigned FlagsReg = 0;
  if (isEFLAGSLive(MBB, InsertPt, *TRI))
    FlagsReg = saveEFLAGS(MBB, InsertPt, Loc);

  Register NewReg = MRI->createVirtualRegister(RC);
  unsigned OrOpCodes[] = {X86::OR8rr, X86::OR16rr, X86::OR32rr, X86::OR64rr};
  unsigned OrOpCode = OrOpCodes[Log2_32(Bytes)];
  auto OrI = BuildMI(MBB, InsertPt, Loc, TII->get(OrOpCode), NewReg)
                 .addReg(StateReg)
                 .addReg(Reg);
  OrI->addRegisterDead(X86::EFLAGS, TRI);
  ++NumInstsInserted;
  LLVM_DEBUG(dbgs() << "  Inserting or: "; OrI->dump(); dbgs() << "\n");

  if (FlagsReg)
    restoreEFLAGS(MBB, InsertPt, Loc, FlagsReg);

  return NewReg;
}

/// Harden a load by hardening the loaded value in the defined register.
///
/// We can harden a non-leaking load into a register without touching the
/// address by just hiding all of the loaded bits during misspeculation. We use
/// an `or` instruction to do this because we set up our poison value as all
/// ones. And the goal is just for the loaded bits to not be exposed to
/// execution and coercing them to one is sufficient.
///
/// Returns the newly hardened register.
unsigned X86SpeculativeLoadHardeningPass::hardenPostLoad(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &Loc = MI.getDebugLoc();

  auto &DefOp = MI.getOperand(0);
  Register OldDefReg = DefOp.getReg();
  auto *DefRC = MRI->getRegClass(OldDefReg);

  // Because we want to completely replace the uses of this def'ed value with
  // the hardened value, create a dedicated new register that will only be used
  // to communicate the unhardened value to the hardening.
  Register UnhardenedReg = MRI->createVirtualRegister(DefRC);
  DefOp.setReg(UnhardenedReg);

  // Now harden this register's value, getting a hardened reg that is safe to
  // use. Note that we insert the instructions to compute this *after* the
  // defining instruction, not before it.
  unsigned HardenedReg = hardenValueInRegister(
      UnhardenedReg, MBB, std::next(MI.getIterator()), Loc);

  // Finally, replace the old register (which now only has the uses of the
  // original def) with the hardened register.
  MRI->replaceRegWith(/*FromReg*/ OldDefReg, /*ToReg*/ HardenedReg);

  ++NumPostLoadRegsHardened;
  return HardenedReg;
}

/// Harden a return instruction.
///
/// Returns implicitly perform a load which we need to harden. Without hardening
/// this load, an attacker my speculatively write over the return address to
/// steer speculation of the return to an attacker controlled address. This is
/// called Spectre v1.1 or Bounds Check Bypass Store (BCBS) and is described in
/// this paper:
/// https://people.csail.mit.edu/vlk/spectre11.pdf
///
/// We can harden this by introducing an LFENCE that will delay any load of the
/// return address until prior instructions have retired (and thus are not being
/// speculated), or we can harden the address used by the implicit load: the
/// stack pointer.
///
/// If we are not using an LFENCE, hardening the stack pointer has an additional
/// benefit: it allows us to pass the predicate state accumulated in this
/// function back to the caller. In the absence of a BCBS attack on the return,
/// the caller will typically be resumed and speculatively executed due to the
/// Return Stack Buffer (RSB) prediction which is very accurate and has a high
/// priority. It is possible that some code from the caller will be executed
/// speculatively even during a BCBS-attacked return until the steering takes
/// effect. Whenever this happens, the caller can recover the (poisoned)
/// predicate state from the stack pointer and continue to harden loads.
void X86SpeculativeLoadHardeningPass::hardenReturnInstr(MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  const DebugLoc &Loc = MI.getDebugLoc();
  auto InsertPt = MI.getIterator();

  if (FenceCallAndRet)
    // No need to fence here as we'll fence at the return site itself. That
    // handles more cases than we can handle here.
    return;

  // Take our predicate state, shift it to the high 17 bits (so that we keep
  // pointers canonical) and merge it into RSP. This will allow the caller to
  // extract it when we return (speculatively).
  mergePredStateIntoSP(MBB, InsertPt, Loc, PS->SSA.GetValueAtEndOfBlock(&MBB));
}

/// Trace the predicate state through a call.
///
/// There are several layers of this needed to handle the full complexity of
/// calls.
///
/// First, we need to send the predicate state into the called function. We do
/// this by merging it into the high bits of the stack pointer.
///
/// For tail calls, this is all we need to do.
///
/// For calls where we might return and resume the control flow, we need to
/// extract the predicate state from the high bits of the stack pointer after
/// control returns from the called function.
///
/// We also need to verify that we intended to return to this location in the
/// code. An attacker might arrange for the processor to mispredict the return
/// to this valid but incorrect return address in the program rather than the
/// correct one. See the paper on this attack, called "ret2spec" by the
/// researchers, here:
/// https://christian-rossow.de/publications/ret2spec-ccs2018.pdf
///
/// The way we verify that we returned to the correct location is by preserving
/// the expected return address across the call. One technique involves taking
/// advantage of the red-zone to load the return address from `8(%rsp)` where it
/// was left by the RET instruction when it popped `%rsp`. Alternatively, we can
/// directly save the address into a register that will be preserved across the
/// call. We compare this intended return address against the address
/// immediately following the call (the observed return address). If these
/// mismatch, we have detected misspeculation and can poison our predicate
/// state.
void X86SpeculativeLoadHardeningPass::tracePredStateThroughCall(
    MachineInstr &MI) {
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  auto InsertPt = MI.getIterator();
  const DebugLoc &Loc = MI.getDebugLoc();

  if (FenceCallAndRet) {
    if (MI.isReturn())
      // Tail call, we don't return to this function.
      // FIXME: We should also handle noreturn calls.
      return;

    // We don't need to fence before the call because the function should fence
    // in its entry. However, we do need to fence after the call returns.
    // Fencing before the return doesn't correctly handle cases where the return
    // itself is mispredicted.
    BuildMI(MBB, std::next(InsertPt), Loc, TII->get(X86::LFENCE));
    ++NumInstsInserted;
    ++NumLFENCEsInserted;
    return;
  }

  // First, we transfer the predicate state into the called function by merging
  // it into the stack pointer. This will kill the current def of the state.
  Register StateReg = PS->SSA.GetValueAtEndOfBlock(&MBB);
  mergePredStateIntoSP(MBB, InsertPt, Loc, StateReg);

  // If this call is also a return, it is a tail call and we don't need anything
  // else to handle it so just return. Also, if there are no further
  // instructions and no successors, this call does not return so we can also
  // bail.
  if (MI.isReturn() || (std::next(InsertPt) == MBB.end() && MBB.succ_empty()))
    return;

  // Create a symbol to track the return address and attach it to the call
  // machine instruction. We will lower extra symbols attached to call
  // instructions as label immediately following the call.
  MCSymbol *RetSymbol =
      MF.getContext().createTempSymbol("slh_ret_addr",
                                       /*AlwaysAddSuffix*/ true);
  MI.setPostInstrSymbol(MF, RetSymbol);

  const TargetRegisterClass *AddrRC = &X86::GR64RegClass;
  unsigned ExpectedRetAddrReg = 0;

  // If we have no red zones or if the function returns twice (possibly without
  // using the `ret` instruction) like setjmp, we need to save the expected
  // return address prior to the call.
  if (!Subtarget->getFrameLowering()->has128ByteRedZone(MF) ||
      MF.exposesReturnsTwice()) {
    // If we don't have red zones, we need to compute the expected return
    // address prior to the call and store it in a register that lives across
    // the call.
    //
    // In some ways, this is doubly satisfying as a mitigation because it will
    // also successfully detect stack smashing bugs in some cases (typically,
    // when a callee-saved register is used and the callee doesn't push it onto
    // the stack). But that isn't our primary goal, so we only use it as
    // a fallback.
    //
    // FIXME: It isn't clear that this is reliable in the face of
    // rematerialization in the register allocator. We somehow need to force
    // that to not occur for this particular instruction, and instead to spill
    // or otherwise preserve the value computed *prior* to the call.
    //
    // FIXME: It is even less clear why MachineCSE can't just fold this when we
    // end up having to use identical instructions both before and after the
    // call to feed the comparison.
    ExpectedRetAddrReg = MRI->createVirtualRegister(AddrRC);
    if (MF.getTarget().getCodeModel() == CodeModel::Small &&
        !Subtarget->isPositionIndependent()) {
      BuildMI(MBB, InsertPt, Loc, TII->get(X86::MOV64ri32), ExpectedRetAddrReg)
          .addSym(RetSymbol);
    } else {
      BuildMI(MBB, InsertPt, Loc, TII->get(X86::LEA64r), ExpectedRetAddrReg)
          .addReg(/*Base*/ X86::RIP)
          .addImm(/*Scale*/ 1)
          .addReg(/*Index*/ 0)
          .addSym(RetSymbol)
          .addReg(/*Segment*/ 0);
    }
  }

  // Step past the call to handle when it returns.
  ++InsertPt;

  // If we didn't pre-compute the expected return address into a register, then
  // red zones are enabled and the return address is still available on the
  // stack immediately after the call. As the very first instruction, we load it
  // into a register.
  if (!ExpectedRetAddrReg) {
    ExpectedRetAddrReg = MRI->createVirtualRegister(AddrRC);
    BuildMI(MBB, InsertPt, Loc, TII->get(X86::MOV64rm), ExpectedRetAddrReg)
        .addReg(/*Base*/ X86::RSP)
        .addImm(/*Scale*/ 1)
        .addReg(/*Index*/ 0)
        .addImm(/*Displacement*/ -8) // The stack pointer has been popped, so
                                     // the return address is 8-bytes past it.
        .addReg(/*Segment*/ 0);
  }

  // Now we extract the callee's predicate state from the stack pointer.
  unsigned NewStateReg = extractPredStateFromSP(MBB, InsertPt, Loc);

  // Test the expected return address against our actual address. If we can
  // form this basic block's address as an immediate, this is easy. Otherwise
  // we compute it.
  if (MF.getTarget().getCodeModel() == CodeModel::Small &&
      !Subtarget->isPositionIndependent()) {
    // FIXME: Could we fold this with the load? It would require careful EFLAGS
    // management.
    BuildMI(MBB, InsertPt, Loc, TII->get(X86::CMP64ri32))
        .addReg(ExpectedRetAddrReg, RegState::Kill)
        .addSym(RetSymbol);
  } else {
    Register ActualRetAddrReg = MRI->createVirtualRegister(AddrRC);
    BuildMI(MBB, InsertPt, Loc, TII->get(X86::LEA64r), ActualRetAddrReg)
        .addReg(/*Base*/ X86::RIP)
        .addImm(/*Scale*/ 1)
        .addReg(/*Index*/ 0)
        .addSym(RetSymbol)
        .addReg(/*Segment*/ 0);
    BuildMI(MBB, InsertPt, Loc, TII->get(X86::CMP64rr))
        .addReg(ExpectedRetAddrReg, RegState::Kill)
        .addReg(ActualRetAddrReg, RegState::Kill);
  }

  // Now conditionally update the predicate state we just extracted if we ended
  // up at a different return address than expected.
  int PredStateSizeInBytes = TRI->getRegSizeInBits(*PS->RC) / 8;
  auto CMovOp = X86::getCMovOpcode(PredStateSizeInBytes);

  Register UpdatedStateReg = MRI->createVirtualRegister(PS->RC);
  auto CMovI = BuildMI(MBB, InsertPt, Loc, TII->get(CMovOp), UpdatedStateReg)
                   .addReg(NewStateReg, RegState::Kill)
                   .addReg(PS->PoisonReg)
                   .addImm(X86::COND_NE);
  CMovI->findRegisterUseOperand(X86::EFLAGS, /*TRI=*/nullptr)->setIsKill(true);
  ++NumInstsInserted;
  LLVM_DEBUG(dbgs() << "  Inserting cmov: "; CMovI->dump(); dbgs() << "\n");

  PS->SSA.AddAvailableValue(&MBB, UpdatedStateReg);
}

/// An attacker may speculatively store over a value that is then speculatively
/// loaded and used as the target of an indirect call or jump instruction. This
/// is called Spectre v1.2 or Bounds Check Bypass Store (BCBS) and is described
/// in this paper:
/// https://people.csail.mit.edu/vlk/spectre11.pdf
///
/// When this happens, the speculative execution of the call or jump will end up
/// being steered to this attacker controlled address. While most such loads
/// will be adequately hardened already, we want to ensure that they are
/// definitively treated as needing post-load hardening. While address hardening
/// is sufficient to prevent secret data from leaking to the attacker, it may
/// not be sufficient to prevent an attacker from steering speculative
/// execution. We forcibly unfolded all relevant loads above and so will always
/// have an opportunity to post-load harden here, we just need to scan for cases
/// not already flagged and add them.
void X86SpeculativeLoadHardeningPass::hardenIndirectCallOrJumpInstr(
    MachineInstr &MI,
    SmallDenseMap<unsigned, unsigned, 32> &AddrRegToHardenedReg) {
  switch (MI.getOpcode()) {
  case X86::FARCALL16m:
  case X86::FARCALL32m:
  case X86::FARCALL64m:
  case X86::FARJMP16m:
  case X86::FARJMP32m:
  case X86::FARJMP64m:
    // We don't need to harden either far calls or far jumps as they are
    // safe from Spectre.
    return;

  default:
    break;
  }

  // We should never see a loading instruction at this point, as those should
  // have been unfolded.
  assert(!MI.mayLoad() && "Found a lingering loading instruction!");

  // If the first operand isn't a register, this is a branch or call
  // instruction with an immediate operand which doesn't need to be hardened.
  if (!MI.getOperand(0).isReg())
    return;

  // For all of these, the target register is the first operand of the
  // instruction.
  auto &TargetOp = MI.getOperand(0);
  Register OldTargetReg = TargetOp.getReg();

  // Try to lookup a hardened version of this register. We retain a reference
  // here as we want to update the map to track any newly computed hardened
  // register.
  unsigned &HardenedTargetReg = AddrRegToHardenedReg[OldTargetReg];

  // If we don't have a hardened register yet, compute one. Otherwise, just use
  // the already hardened register.
  //
  // FIXME: It is a little suspect that we use partially hardened registers that
  // only feed addresses. The complexity of partial hardening with SHRX
  // continues to pile up. Should definitively measure its value and consider
  // eliminating it.
  if (!HardenedTargetReg)
    HardenedTargetReg = hardenValueInRegister(
        OldTargetReg, *MI.getParent(), MI.getIterator(), MI.getDebugLoc());

  // Set the target operand to the hardened register.
  TargetOp.setReg(HardenedTargetReg);

  ++NumCallsOrJumpsHardened;
}

INITIALIZE_PASS_BEGIN(X86SpeculativeLoadHardeningPass, PASS_KEY,
                      "X86 speculative load hardener", false, false)
INITIALIZE_PASS_END(X86SpeculativeLoadHardeningPass, PASS_KEY,
                    "X86 speculative load hardener", false, false)

FunctionPass *llvm::createX86SpeculativeLoadHardeningPass() {
  return new X86SpeculativeLoadHardeningPass();
}
