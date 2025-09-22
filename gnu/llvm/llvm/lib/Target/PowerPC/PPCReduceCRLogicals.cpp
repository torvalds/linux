//===---- PPCReduceCRLogicals.cpp - Reduce CR Bit Logical operations ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This pass aims to reduce the number of logical operations on bits in the CR
// register. These instructions have a fairly high latency and only a single
// pipeline at their disposal in modern PPC cores. Furthermore, they have a
// tendency to occur in fairly small blocks where there's little opportunity
// to hide the latency between the CR logical operation and its user.
//
//===---------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-reduce-cr-ops"

STATISTIC(NumContainedSingleUseBinOps,
          "Number of single-use binary CR logical ops contained in a block");
STATISTIC(NumToSplitBlocks,
          "Number of binary CR logical ops that can be used to split blocks");
STATISTIC(TotalCRLogicals, "Number of CR logical ops.");
STATISTIC(TotalNullaryCRLogicals,
          "Number of nullary CR logical ops (CRSET/CRUNSET).");
STATISTIC(TotalUnaryCRLogicals, "Number of unary CR logical ops.");
STATISTIC(TotalBinaryCRLogicals, "Number of CR logical ops.");
STATISTIC(NumBlocksSplitOnBinaryCROp,
          "Number of blocks split on CR binary logical ops.");
STATISTIC(NumNotSplitIdenticalOperands,
          "Number of blocks not split due to operands being identical.");
STATISTIC(NumNotSplitChainCopies,
          "Number of blocks not split due to operands being chained copies.");
STATISTIC(NumNotSplitWrongOpcode,
          "Number of blocks not split due to the wrong opcode.");

/// Given a basic block \p Successor that potentially contains PHIs, this
/// function will look for any incoming values in the PHIs that are supposed to
/// be coming from \p OrigMBB but whose definition is actually in \p NewMBB.
/// Any such PHIs will be updated to reflect reality.
static void updatePHIs(MachineBasicBlock *Successor, MachineBasicBlock *OrigMBB,
                       MachineBasicBlock *NewMBB, MachineRegisterInfo *MRI) {
  for (auto &MI : Successor->instrs()) {
    if (!MI.isPHI())
      continue;
    // This is a really ugly-looking loop, but it was pillaged directly from
    // MachineBasicBlock::transferSuccessorsAndUpdatePHIs().
    for (unsigned i = 2, e = MI.getNumOperands() + 1; i != e; i += 2) {
      MachineOperand &MO = MI.getOperand(i);
      if (MO.getMBB() == OrigMBB) {
        // Check if the instruction is actually defined in NewMBB.
        if (MI.getOperand(i - 1).isReg()) {
          MachineInstr *DefMI = MRI->getVRegDef(MI.getOperand(i - 1).getReg());
          if (DefMI->getParent() == NewMBB ||
              !OrigMBB->isSuccessor(Successor)) {
            MO.setMBB(NewMBB);
            break;
          }
        }
      }
    }
  }
}

/// Given a basic block \p Successor that potentially contains PHIs, this
/// function will look for PHIs that have an incoming value from \p OrigMBB
/// and will add the same incoming value from \p NewMBB.
/// NOTE: This should only be used if \p NewMBB is an immediate dominator of
/// \p OrigMBB.
static void addIncomingValuesToPHIs(MachineBasicBlock *Successor,
                                    MachineBasicBlock *OrigMBB,
                                    MachineBasicBlock *NewMBB,
                                    MachineRegisterInfo *MRI) {
  assert(OrigMBB->isSuccessor(NewMBB) &&
         "NewMBB must be a successor of OrigMBB");
  for (auto &MI : Successor->instrs()) {
    if (!MI.isPHI())
      continue;
    // This is a really ugly-looking loop, but it was pillaged directly from
    // MachineBasicBlock::transferSuccessorsAndUpdatePHIs().
    for (unsigned i = 2, e = MI.getNumOperands() + 1; i != e; i += 2) {
      MachineOperand &MO = MI.getOperand(i);
      if (MO.getMBB() == OrigMBB) {
        MachineInstrBuilder MIB(*MI.getParent()->getParent(), &MI);
        MIB.addReg(MI.getOperand(i - 1).getReg()).addMBB(NewMBB);
        break;
      }
    }
  }
}

struct BlockSplitInfo {
  MachineInstr *OrigBranch;
  MachineInstr *SplitBefore;
  MachineInstr *SplitCond;
  bool InvertNewBranch;
  bool InvertOrigBranch;
  bool BranchToFallThrough;
  const MachineBranchProbabilityInfo *MBPI;
  MachineInstr *MIToDelete;
  MachineInstr *NewCond;
  bool allInstrsInSameMBB() {
    if (!OrigBranch || !SplitBefore || !SplitCond)
      return false;
    MachineBasicBlock *MBB = OrigBranch->getParent();
    if (SplitBefore->getParent() != MBB || SplitCond->getParent() != MBB)
      return false;
    if (MIToDelete && MIToDelete->getParent() != MBB)
      return false;
    if (NewCond && NewCond->getParent() != MBB)
      return false;
    return true;
  }
};

/// Splits a MachineBasicBlock to branch before \p SplitBefore. The original
/// branch is \p OrigBranch. The target of the new branch can either be the same
/// as the target of the original branch or the fallthrough successor of the
/// original block as determined by \p BranchToFallThrough. The branch
/// conditions will be inverted according to \p InvertNewBranch and
/// \p InvertOrigBranch. If an instruction that previously fed the branch is to
/// be deleted, it is provided in \p MIToDelete and \p NewCond will be used as
/// the branch condition. The branch probabilities will be set if the
/// MachineBranchProbabilityInfo isn't null.
static bool splitMBB(BlockSplitInfo &BSI) {
  assert(BSI.allInstrsInSameMBB() &&
         "All instructions must be in the same block.");

  MachineBasicBlock *ThisMBB = BSI.OrigBranch->getParent();
  MachineFunction *MF = ThisMBB->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  assert(MRI->isSSA() && "Can only do this while the function is in SSA form.");
  if (ThisMBB->succ_size() != 2) {
    LLVM_DEBUG(
        dbgs() << "Don't know how to handle blocks that don't have exactly"
               << " two successors.\n");
    return false;
  }

  const PPCInstrInfo *TII = MF->getSubtarget<PPCSubtarget>().getInstrInfo();
  unsigned OrigBROpcode = BSI.OrigBranch->getOpcode();
  unsigned InvertedOpcode =
      OrigBROpcode == PPC::BC
          ? PPC::BCn
          : OrigBROpcode == PPC::BCn
                ? PPC::BC
                : OrigBROpcode == PPC::BCLR ? PPC::BCLRn : PPC::BCLR;
  unsigned NewBROpcode = BSI.InvertNewBranch ? InvertedOpcode : OrigBROpcode;
  MachineBasicBlock *OrigTarget = BSI.OrigBranch->getOperand(1).getMBB();
  MachineBasicBlock *OrigFallThrough = OrigTarget == *ThisMBB->succ_begin()
                                           ? *ThisMBB->succ_rbegin()
                                           : *ThisMBB->succ_begin();
  MachineBasicBlock *NewBRTarget =
      BSI.BranchToFallThrough ? OrigFallThrough : OrigTarget;

  // It's impossible to know the precise branch probability after the split.
  // But it still needs to be reasonable, the whole probability to original
  // targets should not be changed.
  // After split NewBRTarget will get two incoming edges. Assume P0 is the
  // original branch probability to NewBRTarget, P1 and P2 are new branch
  // probabilies to NewBRTarget after split. If the two edge frequencies are
  // same, then
  //      F * P1 = F * P0 / 2            ==>  P1 = P0 / 2
  //      F * (1 - P1) * P2 = F * P1     ==>  P2 = P1 / (1 - P1)
  BranchProbability ProbToNewTarget, ProbFallThrough;     // Prob for new Br.
  BranchProbability ProbOrigTarget, ProbOrigFallThrough;  // Prob for orig Br.
  ProbToNewTarget = ProbFallThrough = BranchProbability::getUnknown();
  ProbOrigTarget = ProbOrigFallThrough = BranchProbability::getUnknown();
  if (BSI.MBPI) {
    if (BSI.BranchToFallThrough) {
      ProbToNewTarget = BSI.MBPI->getEdgeProbability(ThisMBB, OrigFallThrough) / 2;
      ProbFallThrough = ProbToNewTarget.getCompl();
      ProbOrigFallThrough = ProbToNewTarget / ProbToNewTarget.getCompl();
      ProbOrigTarget = ProbOrigFallThrough.getCompl();
    } else {
      ProbToNewTarget = BSI.MBPI->getEdgeProbability(ThisMBB, OrigTarget) / 2;
      ProbFallThrough = ProbToNewTarget.getCompl();
      ProbOrigTarget = ProbToNewTarget / ProbToNewTarget.getCompl();
      ProbOrigFallThrough = ProbOrigTarget.getCompl();
    }
  }

  // Create a new basic block.
  MachineBasicBlock::iterator InsertPoint = BSI.SplitBefore;
  const BasicBlock *LLVM_BB = ThisMBB->getBasicBlock();
  MachineFunction::iterator It = ThisMBB->getIterator();
  MachineBasicBlock *NewMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(++It, NewMBB);

  // Move everything after SplitBefore into the new block.
  NewMBB->splice(NewMBB->end(), ThisMBB, InsertPoint, ThisMBB->end());
  NewMBB->transferSuccessors(ThisMBB);
  if (!ProbOrigTarget.isUnknown()) {
    auto MBBI = find(NewMBB->successors(), OrigTarget);
    NewMBB->setSuccProbability(MBBI, ProbOrigTarget);
    MBBI = find(NewMBB->successors(), OrigFallThrough);
    NewMBB->setSuccProbability(MBBI, ProbOrigFallThrough);
  }

  // Add the two successors to ThisMBB.
  ThisMBB->addSuccessor(NewBRTarget, ProbToNewTarget);
  ThisMBB->addSuccessor(NewMBB, ProbFallThrough);

  // Add the branches to ThisMBB.
  BuildMI(*ThisMBB, ThisMBB->end(), BSI.SplitBefore->getDebugLoc(),
          TII->get(NewBROpcode))
      .addReg(BSI.SplitCond->getOperand(0).getReg())
      .addMBB(NewBRTarget);
  BuildMI(*ThisMBB, ThisMBB->end(), BSI.SplitBefore->getDebugLoc(),
          TII->get(PPC::B))
      .addMBB(NewMBB);
  if (BSI.MIToDelete)
    BSI.MIToDelete->eraseFromParent();

  // Change the condition on the original branch and invert it if requested.
  auto FirstTerminator = NewMBB->getFirstTerminator();
  if (BSI.NewCond) {
    assert(FirstTerminator->getOperand(0).isReg() &&
           "Can't update condition of unconditional branch.");
    FirstTerminator->getOperand(0).setReg(BSI.NewCond->getOperand(0).getReg());
  }
  if (BSI.InvertOrigBranch)
    FirstTerminator->setDesc(TII->get(InvertedOpcode));

  // If any of the PHIs in the successors of NewMBB reference values that
  // now come from NewMBB, they need to be updated.
  for (auto *Succ : NewMBB->successors()) {
    updatePHIs(Succ, ThisMBB, NewMBB, MRI);
  }
  addIncomingValuesToPHIs(NewBRTarget, ThisMBB, NewMBB, MRI);

  LLVM_DEBUG(dbgs() << "After splitting, ThisMBB:\n"; ThisMBB->dump());
  LLVM_DEBUG(dbgs() << "NewMBB:\n"; NewMBB->dump());
  LLVM_DEBUG(dbgs() << "New branch-to block:\n"; NewBRTarget->dump());
  return true;
}

static bool isBinary(MachineInstr &MI) {
  return MI.getNumOperands() == 3;
}

static bool isNullary(MachineInstr &MI) {
  return MI.getNumOperands() == 1;
}

/// Given a CR logical operation \p CROp, branch opcode \p BROp as well as
/// a flag to indicate if the first operand of \p CROp is used as the
/// SplitBefore operand, determines whether either of the branches are to be
/// inverted as well as whether the new target should be the original
/// fall-through block.
static void
computeBranchTargetAndInversion(unsigned CROp, unsigned BROp, bool UsingDef1,
                                bool &InvertNewBranch, bool &InvertOrigBranch,
                                bool &TargetIsFallThrough) {
  // The conditions under which each of the output operands should be [un]set
  // can certainly be written much more concisely with just 3 if statements or
  // ternary expressions. However, this provides a much clearer overview to the
  // reader as to what is set for each <CROp, BROp, OpUsed> combination.
  if (BROp == PPC::BC || BROp == PPC::BCLR) {
    // Regular branches.
    switch (CROp) {
    default:
      llvm_unreachable("Don't know how to handle this CR logical.");
    case PPC::CROR:
      InvertNewBranch = false;
      InvertOrigBranch = false;
      TargetIsFallThrough = false;
      return;
    case PPC::CRAND:
      InvertNewBranch = true;
      InvertOrigBranch = false;
      TargetIsFallThrough = true;
      return;
    case PPC::CRNAND:
      InvertNewBranch = true;
      InvertOrigBranch = true;
      TargetIsFallThrough = false;
      return;
    case PPC::CRNOR:
      InvertNewBranch = false;
      InvertOrigBranch = true;
      TargetIsFallThrough = true;
      return;
    case PPC::CRORC:
      InvertNewBranch = UsingDef1;
      InvertOrigBranch = !UsingDef1;
      TargetIsFallThrough = false;
      return;
    case PPC::CRANDC:
      InvertNewBranch = !UsingDef1;
      InvertOrigBranch = !UsingDef1;
      TargetIsFallThrough = true;
      return;
    }
  } else if (BROp == PPC::BCn || BROp == PPC::BCLRn) {
    // Negated branches.
    switch (CROp) {
    default:
      llvm_unreachable("Don't know how to handle this CR logical.");
    case PPC::CROR:
      InvertNewBranch = true;
      InvertOrigBranch = false;
      TargetIsFallThrough = true;
      return;
    case PPC::CRAND:
      InvertNewBranch = false;
      InvertOrigBranch = false;
      TargetIsFallThrough = false;
      return;
    case PPC::CRNAND:
      InvertNewBranch = false;
      InvertOrigBranch = true;
      TargetIsFallThrough = true;
      return;
    case PPC::CRNOR:
      InvertNewBranch = true;
      InvertOrigBranch = true;
      TargetIsFallThrough = false;
      return;
    case PPC::CRORC:
      InvertNewBranch = !UsingDef1;
      InvertOrigBranch = !UsingDef1;
      TargetIsFallThrough = true;
      return;
    case PPC::CRANDC:
      InvertNewBranch = UsingDef1;
      InvertOrigBranch = !UsingDef1;
      TargetIsFallThrough = false;
      return;
    }
  } else
    llvm_unreachable("Don't know how to handle this branch.");
}

namespace {

class PPCReduceCRLogicals : public MachineFunctionPass {

public:
  static char ID;
  struct CRLogicalOpInfo {
    MachineInstr *MI;
    // FIXME: If chains of copies are to be handled, this should be a vector.
    std::pair<MachineInstr*, MachineInstr*> CopyDefs;
    std::pair<MachineInstr*, MachineInstr*> TrueDefs;
    unsigned IsBinary : 1;
    unsigned IsNullary : 1;
    unsigned ContainedInBlock : 1;
    unsigned FeedsISEL : 1;
    unsigned FeedsBR : 1;
    unsigned FeedsLogical : 1;
    unsigned SingleUse : 1;
    unsigned DefsSingleUse : 1;
    unsigned SubregDef1;
    unsigned SubregDef2;
    CRLogicalOpInfo() : MI(nullptr), IsBinary(0), IsNullary(0),
                        ContainedInBlock(0), FeedsISEL(0), FeedsBR(0),
                        FeedsLogical(0), SingleUse(0), DefsSingleUse(1),
                        SubregDef1(0), SubregDef2(0) { }
    void dump();
  };

private:
  const PPCInstrInfo *TII = nullptr;
  MachineFunction *MF = nullptr;
  MachineRegisterInfo *MRI = nullptr;
  const MachineBranchProbabilityInfo *MBPI = nullptr;

  // A vector to contain all the CR logical operations
  SmallVector<CRLogicalOpInfo, 16> AllCRLogicalOps;
  void initialize(MachineFunction &MFParm);
  void collectCRLogicals();
  bool handleCROp(unsigned Idx);
  bool splitBlockOnBinaryCROp(CRLogicalOpInfo &CRI);
  static bool isCRLogical(MachineInstr &MI) {
    unsigned Opc = MI.getOpcode();
    return Opc == PPC::CRAND || Opc == PPC::CRNAND || Opc == PPC::CROR ||
           Opc == PPC::CRXOR || Opc == PPC::CRNOR || Opc == PPC::CRNOT ||
           Opc == PPC::CREQV || Opc == PPC::CRANDC || Opc == PPC::CRORC ||
           Opc == PPC::CRSET || Opc == PPC::CRUNSET || Opc == PPC::CR6SET ||
           Opc == PPC::CR6UNSET;
  }
  bool simplifyCode() {
    bool Changed = false;
    // Not using a range-based for loop here as the vector may grow while being
    // operated on.
    for (unsigned i = 0; i < AllCRLogicalOps.size(); i++)
      Changed |= handleCROp(i);
    return Changed;
  }

public:
  PPCReduceCRLogicals() : MachineFunctionPass(ID) {
    initializePPCReduceCRLogicalsPass(*PassRegistry::getPassRegistry());
  }

  MachineInstr *lookThroughCRCopy(unsigned Reg, unsigned &Subreg,
                                  MachineInstr *&CpDef);
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;

    // If the subtarget doesn't use CR bits, there's nothing to do.
    const PPCSubtarget &STI = MF.getSubtarget<PPCSubtarget>();
    if (!STI.useCRBits())
      return false;

    initialize(MF);
    collectCRLogicals();
    return simplifyCode();
  }
  CRLogicalOpInfo createCRLogicalOpInfo(MachineInstr &MI);
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void PPCReduceCRLogicals::CRLogicalOpInfo::dump() {
  dbgs() << "CRLogicalOpMI: ";
  MI->dump();
  dbgs() << "IsBinary: " << IsBinary << ", FeedsISEL: " << FeedsISEL;
  dbgs() << ", FeedsBR: " << FeedsBR << ", FeedsLogical: ";
  dbgs() << FeedsLogical << ", SingleUse: " << SingleUse;
  dbgs() << ", DefsSingleUse: " << DefsSingleUse;
  dbgs() << ", SubregDef1: " << SubregDef1 << ", SubregDef2: ";
  dbgs() << SubregDef2 << ", ContainedInBlock: " << ContainedInBlock;
  if (!IsNullary) {
    dbgs() << "\nDefs:\n";
    TrueDefs.first->dump();
  }
  if (IsBinary)
    TrueDefs.second->dump();
  dbgs() << "\n";
  if (CopyDefs.first) {
    dbgs() << "CopyDef1: ";
    CopyDefs.first->dump();
  }
  if (CopyDefs.second) {
    dbgs() << "CopyDef2: ";
    CopyDefs.second->dump();
  }
}
#endif

PPCReduceCRLogicals::CRLogicalOpInfo
PPCReduceCRLogicals::createCRLogicalOpInfo(MachineInstr &MIParam) {
  CRLogicalOpInfo Ret;
  Ret.MI = &MIParam;
  // Get the defs
  if (isNullary(MIParam)) {
    Ret.IsNullary = 1;
    Ret.TrueDefs = std::make_pair(nullptr, nullptr);
    Ret.CopyDefs = std::make_pair(nullptr, nullptr);
  } else {
    MachineInstr *Def1 = lookThroughCRCopy(MIParam.getOperand(1).getReg(),
                                           Ret.SubregDef1, Ret.CopyDefs.first);
    assert(Def1 && "Must be able to find a definition of operand 1.");
    Ret.DefsSingleUse &=
      MRI->hasOneNonDBGUse(Def1->getOperand(0).getReg());
    Ret.DefsSingleUse &=
      MRI->hasOneNonDBGUse(Ret.CopyDefs.first->getOperand(0).getReg());
    if (isBinary(MIParam)) {
      Ret.IsBinary = 1;
      MachineInstr *Def2 = lookThroughCRCopy(MIParam.getOperand(2).getReg(),
                                             Ret.SubregDef2,
                                             Ret.CopyDefs.second);
      assert(Def2 && "Must be able to find a definition of operand 2.");
      Ret.DefsSingleUse &=
        MRI->hasOneNonDBGUse(Def2->getOperand(0).getReg());
      Ret.DefsSingleUse &=
        MRI->hasOneNonDBGUse(Ret.CopyDefs.second->getOperand(0).getReg());
      Ret.TrueDefs = std::make_pair(Def1, Def2);
    } else {
      Ret.TrueDefs = std::make_pair(Def1, nullptr);
      Ret.CopyDefs.second = nullptr;
    }
  }

  Ret.ContainedInBlock = 1;
  // Get the uses
  for (MachineInstr &UseMI :
       MRI->use_nodbg_instructions(MIParam.getOperand(0).getReg())) {
    unsigned Opc = UseMI.getOpcode();
    if (Opc == PPC::ISEL || Opc == PPC::ISEL8)
      Ret.FeedsISEL = 1;
    if (Opc == PPC::BC || Opc == PPC::BCn || Opc == PPC::BCLR ||
        Opc == PPC::BCLRn)
      Ret.FeedsBR = 1;
    Ret.FeedsLogical = isCRLogical(UseMI);
    if (UseMI.getParent() != MIParam.getParent())
      Ret.ContainedInBlock = 0;
  }
  Ret.SingleUse = MRI->hasOneNonDBGUse(MIParam.getOperand(0).getReg()) ? 1 : 0;

  // We now know whether all the uses of the CR logical are in the same block.
  if (!Ret.IsNullary) {
    Ret.ContainedInBlock &=
      (MIParam.getParent() == Ret.TrueDefs.first->getParent());
    if (Ret.IsBinary)
      Ret.ContainedInBlock &=
        (MIParam.getParent() == Ret.TrueDefs.second->getParent());
  }
  LLVM_DEBUG(Ret.dump());
  if (Ret.IsBinary && Ret.ContainedInBlock && Ret.SingleUse) {
    NumContainedSingleUseBinOps++;
    if (Ret.FeedsBR && Ret.DefsSingleUse)
      NumToSplitBlocks++;
  }
  return Ret;
}

/// Looks through a COPY instruction to the actual definition of the CR-bit
/// register and returns the instruction that defines it.
/// FIXME: This currently handles what is by-far the most common case:
/// an instruction that defines a CR field followed by a single copy of a bit
/// from that field into a virtual register. If chains of copies need to be
/// handled, this should have a loop until a non-copy instruction is found.
MachineInstr *PPCReduceCRLogicals::lookThroughCRCopy(unsigned Reg,
                                                     unsigned &Subreg,
                                                     MachineInstr *&CpDef) {
  Subreg = -1;
  if (!Register::isVirtualRegister(Reg))
    return nullptr;
  MachineInstr *Copy = MRI->getVRegDef(Reg);
  CpDef = Copy;
  if (!Copy->isCopy())
    return Copy;
  Register CopySrc = Copy->getOperand(1).getReg();
  Subreg = Copy->getOperand(1).getSubReg();
  if (!CopySrc.isVirtual()) {
    const TargetRegisterInfo *TRI = &TII->getRegisterInfo();
    // Set the Subreg
    if (CopySrc == PPC::CR0EQ || CopySrc == PPC::CR6EQ)
      Subreg = PPC::sub_eq;
    if (CopySrc == PPC::CR0LT || CopySrc == PPC::CR6LT)
      Subreg = PPC::sub_lt;
    if (CopySrc == PPC::CR0GT || CopySrc == PPC::CR6GT)
      Subreg = PPC::sub_gt;
    if (CopySrc == PPC::CR0UN || CopySrc == PPC::CR6UN)
      Subreg = PPC::sub_un;
    // Loop backwards and return the first MI that modifies the physical CR Reg.
    MachineBasicBlock::iterator Me = Copy, B = Copy->getParent()->begin();
    while (Me != B)
      if ((--Me)->modifiesRegister(CopySrc, TRI))
        return &*Me;
    return nullptr;
  }
  return MRI->getVRegDef(CopySrc);
}

void PPCReduceCRLogicals::initialize(MachineFunction &MFParam) {
  MF = &MFParam;
  MRI = &MF->getRegInfo();
  TII = MF->getSubtarget<PPCSubtarget>().getInstrInfo();
  MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();

  AllCRLogicalOps.clear();
}

/// Contains all the implemented transformations on CR logical operations.
/// For example, a binary CR logical can be used to split a block on its inputs,
/// a unary CR logical might be used to change the condition code on a
/// comparison feeding it. A nullary CR logical might simply be removable
/// if the user of the bit it [un]sets can be transformed.
bool PPCReduceCRLogicals::handleCROp(unsigned Idx) {
  // We can definitely split a block on the inputs to a binary CR operation
  // whose defs and (single) use are within the same block.
  bool Changed = false;
  CRLogicalOpInfo CRI = AllCRLogicalOps[Idx];
  if (CRI.IsBinary && CRI.ContainedInBlock && CRI.SingleUse && CRI.FeedsBR &&
      CRI.DefsSingleUse) {
    Changed = splitBlockOnBinaryCROp(CRI);
    if (Changed)
      NumBlocksSplitOnBinaryCROp++;
  }
  return Changed;
}

/// Splits a block that contains a CR-logical operation that feeds a branch
/// and whose operands are produced within the block.
/// Example:
///    %vr5<def> = CMPDI %vr2, 0; CRRC:%vr5 G8RC:%vr2
///    %vr6<def> = COPY %vr5:sub_eq; CRBITRC:%vr6 CRRC:%vr5
///    %vr7<def> = CMPDI %vr3, 0; CRRC:%vr7 G8RC:%vr3
///    %vr8<def> = COPY %vr7:sub_eq; CRBITRC:%vr8 CRRC:%vr7
///    %vr9<def> = CROR %vr6<kill>, %vr8<kill>; CRBITRC:%vr9,%vr6,%vr8
///    BC %vr9<kill>, <BB#2>; CRBITRC:%vr9
/// Becomes:
///    %vr5<def> = CMPDI %vr2, 0; CRRC:%vr5 G8RC:%vr2
///    %vr6<def> = COPY %vr5:sub_eq; CRBITRC:%vr6 CRRC:%vr5
///    BC %vr6<kill>, <BB#2>; CRBITRC:%vr6
///
///    %vr7<def> = CMPDI %vr3, 0; CRRC:%vr7 G8RC:%vr3
///    %vr8<def> = COPY %vr7:sub_eq; CRBITRC:%vr8 CRRC:%vr7
///    BC %vr9<kill>, <BB#2>; CRBITRC:%vr9
bool PPCReduceCRLogicals::splitBlockOnBinaryCROp(CRLogicalOpInfo &CRI) {
  if (CRI.CopyDefs.first == CRI.CopyDefs.second) {
    LLVM_DEBUG(dbgs() << "Unable to split as the two operands are the same\n");
    NumNotSplitIdenticalOperands++;
    return false;
  }
  if (CRI.TrueDefs.first->isCopy() || CRI.TrueDefs.second->isCopy() ||
      CRI.TrueDefs.first->isPHI() || CRI.TrueDefs.second->isPHI()) {
    LLVM_DEBUG(
        dbgs() << "Unable to split because one of the operands is a PHI or "
                  "chain of copies.\n");
    NumNotSplitChainCopies++;
    return false;
  }
  // Note: keep in sync with computeBranchTargetAndInversion().
  if (CRI.MI->getOpcode() != PPC::CROR &&
      CRI.MI->getOpcode() != PPC::CRAND &&
      CRI.MI->getOpcode() != PPC::CRNOR &&
      CRI.MI->getOpcode() != PPC::CRNAND &&
      CRI.MI->getOpcode() != PPC::CRORC &&
      CRI.MI->getOpcode() != PPC::CRANDC) {
    LLVM_DEBUG(dbgs() << "Unable to split blocks on this opcode.\n");
    NumNotSplitWrongOpcode++;
    return false;
  }
  LLVM_DEBUG(dbgs() << "Splitting the following CR op:\n"; CRI.dump());
  MachineBasicBlock::iterator Def1It = CRI.TrueDefs.first;
  MachineBasicBlock::iterator Def2It = CRI.TrueDefs.second;

  bool UsingDef1 = false;
  MachineInstr *SplitBefore = &*Def2It;
  for (auto E = CRI.MI->getParent()->end(); Def2It != E; ++Def2It) {
    if (Def1It == Def2It) { // Def2 comes before Def1.
      SplitBefore = &*Def1It;
      UsingDef1 = true;
      break;
    }
  }

  LLVM_DEBUG(dbgs() << "We will split the following block:\n";);
  LLVM_DEBUG(CRI.MI->getParent()->dump());
  LLVM_DEBUG(dbgs() << "Before instruction:\n"; SplitBefore->dump());

  // Get the branch instruction.
  MachineInstr *Branch =
    MRI->use_nodbg_begin(CRI.MI->getOperand(0).getReg())->getParent();

  // We want the new block to have no code in it other than the definition
  // of the input to the CR logical and the CR logical itself. So we move
  // those to the bottom of the block (just before the branch). Then we
  // will split before the CR logical.
  MachineBasicBlock *MBB = SplitBefore->getParent();
  auto FirstTerminator = MBB->getFirstTerminator();
  MachineBasicBlock::iterator FirstInstrToMove =
    UsingDef1 ? CRI.TrueDefs.first : CRI.TrueDefs.second;
  MachineBasicBlock::iterator SecondInstrToMove =
    UsingDef1 ? CRI.CopyDefs.first : CRI.CopyDefs.second;

  // The instructions that need to be moved are not guaranteed to be
  // contiguous. Move them individually.
  // FIXME: If one of the operands is a chain of (single use) copies, they
  // can all be moved and we can still split.
  MBB->splice(FirstTerminator, MBB, FirstInstrToMove);
  if (FirstInstrToMove != SecondInstrToMove)
    MBB->splice(FirstTerminator, MBB, SecondInstrToMove);
  MBB->splice(FirstTerminator, MBB, CRI.MI);

  unsigned Opc = CRI.MI->getOpcode();
  bool InvertOrigBranch, InvertNewBranch, TargetIsFallThrough;
  computeBranchTargetAndInversion(Opc, Branch->getOpcode(), UsingDef1,
                                  InvertNewBranch, InvertOrigBranch,
                                  TargetIsFallThrough);
  MachineInstr *SplitCond =
    UsingDef1 ? CRI.CopyDefs.second : CRI.CopyDefs.first;
  LLVM_DEBUG(dbgs() << "We will " << (InvertNewBranch ? "invert" : "copy"));
  LLVM_DEBUG(dbgs() << " the original branch and the target is the "
                    << (TargetIsFallThrough ? "fallthrough block\n"
                                            : "orig. target block\n"));
  LLVM_DEBUG(dbgs() << "Original branch instruction: "; Branch->dump());
  BlockSplitInfo BSI { Branch, SplitBefore, SplitCond, InvertNewBranch,
    InvertOrigBranch, TargetIsFallThrough, MBPI, CRI.MI,
    UsingDef1 ? CRI.CopyDefs.first : CRI.CopyDefs.second };
  bool Changed = splitMBB(BSI);
  // If we've split on a CR logical that is fed by a CR logical,
  // recompute the source CR logical as it may be usable for splitting.
  if (Changed) {
    bool Input1CRlogical =
      CRI.TrueDefs.first && isCRLogical(*CRI.TrueDefs.first);
    bool Input2CRlogical =
      CRI.TrueDefs.second && isCRLogical(*CRI.TrueDefs.second);
    if (Input1CRlogical)
      AllCRLogicalOps.push_back(createCRLogicalOpInfo(*CRI.TrueDefs.first));
    if (Input2CRlogical)
      AllCRLogicalOps.push_back(createCRLogicalOpInfo(*CRI.TrueDefs.second));
  }
  return Changed;
}

void PPCReduceCRLogicals::collectCRLogicals() {
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      if (isCRLogical(MI)) {
        AllCRLogicalOps.push_back(createCRLogicalOpInfo(MI));
        TotalCRLogicals++;
        if (AllCRLogicalOps.back().IsNullary)
          TotalNullaryCRLogicals++;
        else if (AllCRLogicalOps.back().IsBinary)
          TotalBinaryCRLogicals++;
        else
          TotalUnaryCRLogicals++;
      }
    }
  }
}

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(PPCReduceCRLogicals, DEBUG_TYPE,
                      "PowerPC Reduce CR logical Operation", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(PPCReduceCRLogicals, DEBUG_TYPE,
                    "PowerPC Reduce CR logical Operation", false, false)

char PPCReduceCRLogicals::ID = 0;
FunctionPass*
llvm::createPPCReduceCRLogicalsPass() { return new PPCReduceCRLogicals(); }
