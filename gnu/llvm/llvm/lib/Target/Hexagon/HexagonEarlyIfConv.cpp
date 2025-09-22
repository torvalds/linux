//===- HexagonEarlyIfConv.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements a Hexagon-specific if-conversion pass that runs on the
// SSA form.
// In SSA it is not straightforward to represent instructions that condi-
// tionally define registers, since a conditionally-defined register may
// only be used under the same condition on which the definition was based.
// To avoid complications of this nature, this patch will only generate
// predicated stores, and speculate other instructions from the "if-conver-
// ted" block.
// The code will recognize CFG patterns where a block with a conditional
// branch "splits" into a "true block" and a "false block". Either of these
// could be omitted (in case of a triangle, for example).
// If after conversion of the side block(s) the CFG allows it, the resul-
// ting blocks may be merged. If the "join" block contained PHI nodes, they
// will be replaced with MUX (or MUX-like) instructions to maintain the
// semantics of the PHI.
//
// Example:
//
//         %40 = L2_loadrub_io killed %39, 1
//         %41 = S2_tstbit_i killed %40, 0
//         J2_jumpt killed %41, <%bb.5>, implicit dead %pc
//         J2_jump <%bb.4>, implicit dead %pc
//     Successors according to CFG: %bb.4(62) %bb.5(62)
//
// %bb.4: derived from LLVM BB %if.then
//     Predecessors according to CFG: %bb.3
//         %11 = A2_addp %6, %10
//         S2_storerd_io %32, 16, %11
//     Successors according to CFG: %bb.5
//
// %bb.5: derived from LLVM BB %if.end
//     Predecessors according to CFG: %bb.3 %bb.4
//         %12 = PHI %6, <%bb.3>, %11, <%bb.4>
//         %13 = A2_addp %7, %12
//         %42 = C2_cmpeqi %9, 10
//         J2_jumpf killed %42, <%bb.3>, implicit dead %pc
//         J2_jump <%bb.6>, implicit dead %pc
//     Successors according to CFG: %bb.6(4) %bb.3(124)
//
// would become:
//
//         %40 = L2_loadrub_io killed %39, 1
//         %41 = S2_tstbit_i killed %40, 0
// spec->  %11 = A2_addp %6, %10
// pred->  S2_pstorerdf_io %41, %32, 16, %11
//         %46 = PS_pselect %41, %6, %11
//         %13 = A2_addp %7, %46
//         %42 = C2_cmpeqi %9, 10
//         J2_jumpf killed %42, <%bb.3>, implicit dead %pc
//         J2_jump <%bb.6>, implicit dead %pc
//     Successors according to CFG: %bb.6 %bb.3

#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Pass.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>

#define DEBUG_TYPE "hexagon-eif"

using namespace llvm;

namespace llvm {

  FunctionPass *createHexagonEarlyIfConversion();
  void initializeHexagonEarlyIfConversionPass(PassRegistry& Registry);

} // end namespace llvm

static cl::opt<bool> EnableHexagonBP("enable-hexagon-br-prob", cl::Hidden,
  cl::init(true), cl::desc("Enable branch probability info"));
static cl::opt<unsigned> SizeLimit("eif-limit", cl::init(6), cl::Hidden,
  cl::desc("Size limit in Hexagon early if-conversion"));
static cl::opt<bool> SkipExitBranches("eif-no-loop-exit", cl::init(false),
  cl::Hidden, cl::desc("Do not convert branches that may exit the loop"));

namespace {

  struct PrintMB {
    PrintMB(const MachineBasicBlock *B) : MB(B) {}

    const MachineBasicBlock *MB;
  };
  raw_ostream &operator<< (raw_ostream &OS, const PrintMB &P) {
    if (!P.MB)
      return OS << "<none>";
    return OS << '#' << P.MB->getNumber();
  }

  struct FlowPattern {
    FlowPattern() = default;
    FlowPattern(MachineBasicBlock *B, unsigned PR, MachineBasicBlock *TB,
          MachineBasicBlock *FB, MachineBasicBlock *JB)
      : SplitB(B), TrueB(TB), FalseB(FB), JoinB(JB), PredR(PR) {}

    MachineBasicBlock *SplitB = nullptr;
    MachineBasicBlock *TrueB = nullptr;
    MachineBasicBlock *FalseB = nullptr;
    MachineBasicBlock *JoinB = nullptr;
    unsigned PredR = 0;
  };

  struct PrintFP {
    PrintFP(const FlowPattern &P, const TargetRegisterInfo &T)
      : FP(P), TRI(T) {}

    const FlowPattern &FP;
    const TargetRegisterInfo &TRI;
    friend raw_ostream &operator<< (raw_ostream &OS, const PrintFP &P);
  };
  raw_ostream &operator<<(raw_ostream &OS,
                          const PrintFP &P) LLVM_ATTRIBUTE_UNUSED;
  raw_ostream &operator<<(raw_ostream &OS, const PrintFP &P) {
    OS << "{ SplitB:" << PrintMB(P.FP.SplitB)
       << ", PredR:" << printReg(P.FP.PredR, &P.TRI)
       << ", TrueB:" << PrintMB(P.FP.TrueB)
       << ", FalseB:" << PrintMB(P.FP.FalseB)
       << ", JoinB:" << PrintMB(P.FP.JoinB) << " }";
    return OS;
  }

  class HexagonEarlyIfConversion : public MachineFunctionPass {
  public:
    static char ID;

    HexagonEarlyIfConversion() : MachineFunctionPass(ID) {}

    StringRef getPassName() const override {
      return "Hexagon early if conversion";
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addPreserved<MachineDominatorTreeWrapperPass>();
      AU.addRequired<MachineLoopInfoWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    using BlockSetType = DenseSet<MachineBasicBlock *>;

    bool isPreheader(const MachineBasicBlock *B) const;
    bool matchFlowPattern(MachineBasicBlock *B, MachineLoop *L,
          FlowPattern &FP);
    bool visitBlock(MachineBasicBlock *B, MachineLoop *L);
    bool visitLoop(MachineLoop *L);

    bool hasEHLabel(const MachineBasicBlock *B) const;
    bool hasUncondBranch(const MachineBasicBlock *B) const;
    bool isValidCandidate(const MachineBasicBlock *B) const;
    bool usesUndefVReg(const MachineInstr *MI) const;
    bool isValid(const FlowPattern &FP) const;
    unsigned countPredicateDefs(const MachineBasicBlock *B) const;
    unsigned computePhiCost(const MachineBasicBlock *B,
          const FlowPattern &FP) const;
    bool isProfitable(const FlowPattern &FP) const;
    bool isPredicableStore(const MachineInstr *MI) const;
    bool isSafeToSpeculate(const MachineInstr *MI) const;
    bool isPredicate(unsigned R) const;

    unsigned getCondStoreOpcode(unsigned Opc, bool IfTrue) const;
    void predicateInstr(MachineBasicBlock *ToB, MachineBasicBlock::iterator At,
          MachineInstr *MI, unsigned PredR, bool IfTrue);
    void predicateBlockNB(MachineBasicBlock *ToB,
          MachineBasicBlock::iterator At, MachineBasicBlock *FromB,
          unsigned PredR, bool IfTrue);

    unsigned buildMux(MachineBasicBlock *B, MachineBasicBlock::iterator At,
          const TargetRegisterClass *DRC, unsigned PredR, unsigned TR,
          unsigned TSR, unsigned FR, unsigned FSR);
    void updatePhiNodes(MachineBasicBlock *WhereB, const FlowPattern &FP);
    void convert(const FlowPattern &FP);

    void removeBlock(MachineBasicBlock *B);
    void eliminatePhis(MachineBasicBlock *B);
    void mergeBlocks(MachineBasicBlock *PredB, MachineBasicBlock *SuccB);
    void simplifyFlowGraph(const FlowPattern &FP);

    const HexagonInstrInfo *HII = nullptr;
    const TargetRegisterInfo *TRI = nullptr;
    MachineFunction *MFN = nullptr;
    MachineRegisterInfo *MRI = nullptr;
    MachineDominatorTree *MDT = nullptr;
    MachineLoopInfo *MLI = nullptr;
    BlockSetType Deleted;
    const MachineBranchProbabilityInfo *MBPI = nullptr;
  };

} // end anonymous namespace

char HexagonEarlyIfConversion::ID = 0;

INITIALIZE_PASS(HexagonEarlyIfConversion, "hexagon-early-if",
  "Hexagon early if conversion", false, false)

bool HexagonEarlyIfConversion::isPreheader(const MachineBasicBlock *B) const {
  if (B->succ_size() != 1)
    return false;
  MachineBasicBlock *SB = *B->succ_begin();
  MachineLoop *L = MLI->getLoopFor(SB);
  return L && SB == L->getHeader() && MDT->dominates(B, SB);
}

bool HexagonEarlyIfConversion::matchFlowPattern(MachineBasicBlock *B,
    MachineLoop *L, FlowPattern &FP) {
  LLVM_DEBUG(dbgs() << "Checking flow pattern at " << printMBBReference(*B)
                    << "\n");

  // Interested only in conditional branches, no .new, no new-value, etc.
  // Check the terminators directly, it's easier than handling all responses
  // from analyzeBranch.
  MachineBasicBlock *TB = nullptr, *FB = nullptr;
  MachineBasicBlock::const_iterator T1I = B->getFirstTerminator();
  if (T1I == B->end())
    return false;
  unsigned Opc = T1I->getOpcode();
  if (Opc != Hexagon::J2_jumpt && Opc != Hexagon::J2_jumpf)
    return false;
  Register PredR = T1I->getOperand(0).getReg();

  // Get the layout successor, or 0 if B does not have one.
  MachineFunction::iterator NextBI = std::next(MachineFunction::iterator(B));
  MachineBasicBlock *NextB = (NextBI != MFN->end()) ? &*NextBI : nullptr;

  MachineBasicBlock *T1B = T1I->getOperand(1).getMBB();
  MachineBasicBlock::const_iterator T2I = std::next(T1I);
  // The second terminator should be an unconditional branch.
  assert(T2I == B->end() || T2I->getOpcode() == Hexagon::J2_jump);
  MachineBasicBlock *T2B = (T2I == B->end()) ? NextB
                                             : T2I->getOperand(0).getMBB();
  if (T1B == T2B) {
    // XXX merge if T1B == NextB, or convert branch to unconditional.
    // mark as diamond with both sides equal?
    return false;
  }

  // Record the true/false blocks in such a way that "true" means "if (PredR)",
  // and "false" means "if (!PredR)".
  if (Opc == Hexagon::J2_jumpt)
    TB = T1B, FB = T2B;
  else
    TB = T2B, FB = T1B;

  if (!MDT->properlyDominates(B, TB) || !MDT->properlyDominates(B, FB))
    return false;

  // Detect triangle first. In case of a triangle, one of the blocks TB/FB
  // can fall through into the other, in other words, it will be executed
  // in both cases. We only want to predicate the block that is executed
  // conditionally.
  assert(TB && FB && "Failed to find triangle control flow blocks");
  unsigned TNP = TB->pred_size(), FNP = FB->pred_size();
  unsigned TNS = TB->succ_size(), FNS = FB->succ_size();

  // A block is predicable if it has one predecessor (it must be B), and
  // it has a single successor. In fact, the block has to end either with
  // an unconditional branch (which can be predicated), or with a fall-
  // through.
  // Also, skip blocks that do not belong to the same loop.
  bool TOk = (TNP == 1 && TNS == 1 && MLI->getLoopFor(TB) == L);
  bool FOk = (FNP == 1 && FNS == 1 && MLI->getLoopFor(FB) == L);

  // If requested (via an option), do not consider branches where the
  // true and false targets do not belong to the same loop.
  if (SkipExitBranches && MLI->getLoopFor(TB) != MLI->getLoopFor(FB))
    return false;

  // If neither is predicable, there is nothing interesting.
  if (!TOk && !FOk)
    return false;

  MachineBasicBlock *TSB = (TNS > 0) ? *TB->succ_begin() : nullptr;
  MachineBasicBlock *FSB = (FNS > 0) ? *FB->succ_begin() : nullptr;
  MachineBasicBlock *JB = nullptr;

  if (TOk) {
    if (FOk) {
      if (TSB == FSB)
        JB = TSB;
      // Diamond: "if (P) then TB; else FB;".
    } else {
      // TOk && !FOk
      if (TSB == FB)
        JB = FB;
      FB = nullptr;
    }
  } else {
    // !TOk && FOk  (at least one must be true by now).
    if (FSB == TB)
      JB = TB;
    TB = nullptr;
  }
  // Don't try to predicate loop preheaders.
  if ((TB && isPreheader(TB)) || (FB && isPreheader(FB))) {
    LLVM_DEBUG(dbgs() << "One of blocks " << PrintMB(TB) << ", " << PrintMB(FB)
                      << " is a loop preheader. Skipping.\n");
    return false;
  }

  FP = FlowPattern(B, PredR, TB, FB, JB);
  LLVM_DEBUG(dbgs() << "Detected " << PrintFP(FP, *TRI) << "\n");
  return true;
}

// KLUDGE: HexagonInstrInfo::analyzeBranch won't work on a block that
// contains EH_LABEL.
bool HexagonEarlyIfConversion::hasEHLabel(const MachineBasicBlock *B) const {
  for (auto &I : *B)
    if (I.isEHLabel())
      return true;
  return false;
}

// KLUDGE: HexagonInstrInfo::analyzeBranch may be unable to recognize
// that a block can never fall-through.
bool HexagonEarlyIfConversion::hasUncondBranch(const MachineBasicBlock *B)
      const {
  MachineBasicBlock::const_iterator I = B->getFirstTerminator(), E = B->end();
  while (I != E) {
    if (I->isBarrier())
      return true;
    ++I;
  }
  return false;
}

bool HexagonEarlyIfConversion::isValidCandidate(const MachineBasicBlock *B)
      const {
  if (!B)
    return true;
  if (B->isEHPad() || B->hasAddressTaken())
    return false;
  if (B->succ_empty())
    return false;

  for (auto &MI : *B) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isConditionalBranch())
      return false;
    unsigned Opc = MI.getOpcode();
    bool IsJMP = (Opc == Hexagon::J2_jump);
    if (!isPredicableStore(&MI) && !IsJMP && !isSafeToSpeculate(&MI))
      return false;
    // Look for predicate registers defined by this instruction. It's ok
    // to speculate such an instruction, but the predicate register cannot
    // be used outside of this block (or else it won't be possible to
    // update the use of it after predication). PHI uses will be updated
    // to use a result of a MUX, and a MUX cannot be created for predicate
    // registers.
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      Register R = MO.getReg();
      if (!R.isVirtual())
        continue;
      if (!isPredicate(R))
        continue;
      for (const MachineOperand &U : MRI->use_operands(R))
        if (U.getParent()->isPHI())
          return false;
    }
  }
  return true;
}

bool HexagonEarlyIfConversion::usesUndefVReg(const MachineInstr *MI) const {
  for (const MachineOperand &MO : MI->operands()) {
    if (!MO.isReg() || !MO.isUse())
      continue;
    Register R = MO.getReg();
    if (!R.isVirtual())
      continue;
    const MachineInstr *DefI = MRI->getVRegDef(R);
    // "Undefined" virtual registers are actually defined via IMPLICIT_DEF.
    assert(DefI && "Expecting a reaching def in MRI");
    if (DefI->isImplicitDef())
      return true;
  }
  return false;
}

bool HexagonEarlyIfConversion::isValid(const FlowPattern &FP) const {
  if (hasEHLabel(FP.SplitB))  // KLUDGE: see function definition
    return false;
  if (FP.TrueB && !isValidCandidate(FP.TrueB))
    return false;
  if (FP.FalseB && !isValidCandidate(FP.FalseB))
    return false;
  // Check the PHIs in the join block. If any of them use a register
  // that is defined as IMPLICIT_DEF, do not convert this. This can
  // legitimately happen if one side of the split never executes, but
  // the compiler is unable to prove it. That side may then seem to
  // provide an "undef" value to the join block, however it will never
  // execute at run-time. If we convert this case, the "undef" will
  // be used in a MUX instruction, and that may seem like actually
  // using an undefined value to other optimizations. This could lead
  // to trouble further down the optimization stream, cause assertions
  // to fail, etc.
  if (FP.JoinB) {
    const MachineBasicBlock &B = *FP.JoinB;
    for (auto &MI : B) {
      if (!MI.isPHI())
        break;
      if (usesUndefVReg(&MI))
        return false;
      Register DefR = MI.getOperand(0).getReg();
      if (isPredicate(DefR))
        return false;
    }
  }
  return true;
}

unsigned HexagonEarlyIfConversion::computePhiCost(const MachineBasicBlock *B,
      const FlowPattern &FP) const {
  if (B->pred_size() < 2)
    return 0;

  unsigned Cost = 0;
  for (const MachineInstr &MI : *B) {
    if (!MI.isPHI())
      break;
    // If both incoming blocks are one of the TrueB/FalseB/SplitB, then
    // a MUX may be needed. Otherwise the PHI will need to be updated at
    // no extra cost.
    // Find the interesting PHI operands for further checks.
    SmallVector<unsigned,2> Inc;
    for (unsigned i = 1, e = MI.getNumOperands(); i != e; i += 2) {
      const MachineBasicBlock *BB = MI.getOperand(i+1).getMBB();
      if (BB == FP.SplitB || BB == FP.TrueB || BB == FP.FalseB)
        Inc.push_back(i);
    }
    assert(Inc.size() <= 2);
    if (Inc.size() < 2)
      continue;

    const MachineOperand &RA = MI.getOperand(1);
    const MachineOperand &RB = MI.getOperand(3);
    assert(RA.isReg() && RB.isReg());
    // Must have a MUX if the phi uses a subregister.
    if (RA.getSubReg() != 0 || RB.getSubReg() != 0) {
      Cost++;
      continue;
    }
    const MachineInstr *Def1 = MRI->getVRegDef(RA.getReg());
    const MachineInstr *Def3 = MRI->getVRegDef(RB.getReg());
    if (!HII->isPredicable(*Def1) || !HII->isPredicable(*Def3))
      Cost++;
  }
  return Cost;
}

unsigned HexagonEarlyIfConversion::countPredicateDefs(
      const MachineBasicBlock *B) const {
  unsigned PredDefs = 0;
  for (auto &MI : *B) {
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg() || !MO.isDef())
        continue;
      Register R = MO.getReg();
      if (!R.isVirtual())
        continue;
      if (isPredicate(R))
        PredDefs++;
    }
  }
  return PredDefs;
}

bool HexagonEarlyIfConversion::isProfitable(const FlowPattern &FP) const {
  BranchProbability JumpProb(1, 10);
  BranchProbability Prob(9, 10);
  if (MBPI && FP.TrueB && !FP.FalseB &&
      (MBPI->getEdgeProbability(FP.SplitB, FP.TrueB) < JumpProb ||
       MBPI->getEdgeProbability(FP.SplitB, FP.TrueB) > Prob))
    return false;

  if (MBPI && !FP.TrueB && FP.FalseB &&
      (MBPI->getEdgeProbability(FP.SplitB, FP.FalseB) < JumpProb ||
       MBPI->getEdgeProbability(FP.SplitB, FP.FalseB) > Prob))
    return false;

  if (FP.TrueB && FP.FalseB) {
    // Do not IfCovert if the branch is one sided.
    if (MBPI) {
      if (MBPI->getEdgeProbability(FP.SplitB, FP.TrueB) > Prob)
        return false;
      if (MBPI->getEdgeProbability(FP.SplitB, FP.FalseB) > Prob)
        return false;
    }

    // If both sides are predicable, convert them if they join, and the
    // join block has no other predecessors.
    MachineBasicBlock *TSB = *FP.TrueB->succ_begin();
    MachineBasicBlock *FSB = *FP.FalseB->succ_begin();
    if (TSB != FSB)
      return false;
    if (TSB->pred_size() != 2)
      return false;
  }

  // Calculate the total size of the predicated blocks.
  // Assume instruction counts without branches to be the approximation of
  // the code size. If the predicated blocks are smaller than a packet size,
  // approximate the spare room in the packet that could be filled with the
  // predicated/speculated instructions.
  auto TotalCount = [] (const MachineBasicBlock *B, unsigned &Spare) {
    if (!B)
      return 0u;
    unsigned T = std::count_if(B->begin(), B->getFirstTerminator(),
                               [](const MachineInstr &MI) {
                                 return !MI.isMetaInstruction();
                               });
    if (T < HEXAGON_PACKET_SIZE)
      Spare += HEXAGON_PACKET_SIZE-T;
    return T;
  };
  unsigned Spare = 0;
  unsigned TotalIn = TotalCount(FP.TrueB, Spare) + TotalCount(FP.FalseB, Spare);
  LLVM_DEBUG(
      dbgs() << "Total number of instructions to be predicated/speculated: "
             << TotalIn << ", spare room: " << Spare << "\n");
  if (TotalIn >= SizeLimit+Spare)
    return false;

  // Count the number of PHI nodes that will need to be updated (converted
  // to MUX). Those can be later converted to predicated instructions, so
  // they aren't always adding extra cost.
  // KLUDGE: Also, count the number of predicate register definitions in
  // each block. The scheduler may increase the pressure of these and cause
  // expensive spills (e.g. bitmnp01).
  unsigned TotalPh = 0;
  unsigned PredDefs = countPredicateDefs(FP.SplitB);
  if (FP.JoinB) {
    TotalPh = computePhiCost(FP.JoinB, FP);
    PredDefs += countPredicateDefs(FP.JoinB);
  } else {
    if (FP.TrueB && !FP.TrueB->succ_empty()) {
      MachineBasicBlock *SB = *FP.TrueB->succ_begin();
      TotalPh += computePhiCost(SB, FP);
      PredDefs += countPredicateDefs(SB);
    }
    if (FP.FalseB && !FP.FalseB->succ_empty()) {
      MachineBasicBlock *SB = *FP.FalseB->succ_begin();
      TotalPh += computePhiCost(SB, FP);
      PredDefs += countPredicateDefs(SB);
    }
  }
  LLVM_DEBUG(dbgs() << "Total number of extra muxes from converted phis: "
                    << TotalPh << "\n");
  if (TotalIn+TotalPh >= SizeLimit+Spare)
    return false;

  LLVM_DEBUG(dbgs() << "Total number of predicate registers: " << PredDefs
                    << "\n");
  if (PredDefs > 4)
    return false;

  return true;
}

bool HexagonEarlyIfConversion::visitBlock(MachineBasicBlock *B,
      MachineLoop *L) {
  bool Changed = false;

  // Visit all dominated blocks from the same loop first, then process B.
  MachineDomTreeNode *N = MDT->getNode(B);

  // We will change CFG/DT during this traversal, so take precautions to
  // avoid problems related to invalidated iterators. In fact, processing
  // a child C of B cannot cause another child to be removed, but it can
  // cause a new child to be added (which was a child of C before C itself
  // was removed. This new child C, however, would have been processed
  // prior to processing B, so there is no need to process it again.
  // Simply keep a list of children of B, and traverse that list.
  using DTNodeVectType = SmallVector<MachineDomTreeNode *, 4>;
  DTNodeVectType Cn(llvm::children<MachineDomTreeNode *>(N));
  for (auto &I : Cn) {
    MachineBasicBlock *SB = I->getBlock();
    if (!Deleted.count(SB))
      Changed |= visitBlock(SB, L);
  }
  // When walking down the dominator tree, we want to traverse through
  // blocks from nested (other) loops, because they can dominate blocks
  // that are in L. Skip the non-L blocks only after the tree traversal.
  if (MLI->getLoopFor(B) != L)
    return Changed;

  FlowPattern FP;
  if (!matchFlowPattern(B, L, FP))
    return Changed;

  if (!isValid(FP)) {
    LLVM_DEBUG(dbgs() << "Conversion is not valid\n");
    return Changed;
  }
  if (!isProfitable(FP)) {
    LLVM_DEBUG(dbgs() << "Conversion is not profitable\n");
    return Changed;
  }

  convert(FP);
  simplifyFlowGraph(FP);
  return true;
}

bool HexagonEarlyIfConversion::visitLoop(MachineLoop *L) {
  MachineBasicBlock *HB = L ? L->getHeader() : nullptr;
  LLVM_DEBUG((L ? dbgs() << "Visiting loop H:" << PrintMB(HB)
                : dbgs() << "Visiting function")
             << "\n");
  bool Changed = false;
  if (L) {
    for (MachineLoop *I : *L)
      Changed |= visitLoop(I);
  }

  MachineBasicBlock *EntryB = GraphTraits<MachineFunction*>::getEntryNode(MFN);
  Changed |= visitBlock(L ? HB : EntryB, L);
  return Changed;
}

bool HexagonEarlyIfConversion::isPredicableStore(const MachineInstr *MI)
      const {
  // HexagonInstrInfo::isPredicable will consider these stores are non-
  // -predicable if the offset would become constant-extended after
  // predication.
  unsigned Opc = MI->getOpcode();
  switch (Opc) {
    case Hexagon::S2_storerb_io:
    case Hexagon::S2_storerbnew_io:
    case Hexagon::S2_storerh_io:
    case Hexagon::S2_storerhnew_io:
    case Hexagon::S2_storeri_io:
    case Hexagon::S2_storerinew_io:
    case Hexagon::S2_storerd_io:
    case Hexagon::S4_storeirb_io:
    case Hexagon::S4_storeirh_io:
    case Hexagon::S4_storeiri_io:
      return true;
  }

  // TargetInstrInfo::isPredicable takes a non-const pointer.
  return MI->mayStore() && HII->isPredicable(const_cast<MachineInstr&>(*MI));
}

bool HexagonEarlyIfConversion::isSafeToSpeculate(const MachineInstr *MI)
      const {
  if (MI->mayLoadOrStore())
    return false;
  if (MI->isCall() || MI->isBarrier() || MI->isBranch())
    return false;
  if (MI->hasUnmodeledSideEffects())
    return false;
  if (MI->getOpcode() == TargetOpcode::LIFETIME_END)
    return false;

  return true;
}

bool HexagonEarlyIfConversion::isPredicate(unsigned R) const {
  const TargetRegisterClass *RC = MRI->getRegClass(R);
  return RC == &Hexagon::PredRegsRegClass ||
         RC == &Hexagon::HvxQRRegClass;
}

unsigned HexagonEarlyIfConversion::getCondStoreOpcode(unsigned Opc,
      bool IfTrue) const {
  return HII->getCondOpcode(Opc, !IfTrue);
}

void HexagonEarlyIfConversion::predicateInstr(MachineBasicBlock *ToB,
      MachineBasicBlock::iterator At, MachineInstr *MI,
      unsigned PredR, bool IfTrue) {
  DebugLoc DL;
  if (At != ToB->end())
    DL = At->getDebugLoc();
  else if (!ToB->empty())
    DL = ToB->back().getDebugLoc();

  unsigned Opc = MI->getOpcode();

  if (isPredicableStore(MI)) {
    unsigned COpc = getCondStoreOpcode(Opc, IfTrue);
    assert(COpc);
    MachineInstrBuilder MIB = BuildMI(*ToB, At, DL, HII->get(COpc));
    MachineInstr::mop_iterator MOI = MI->operands_begin();
    if (HII->isPostIncrement(*MI)) {
      MIB.add(*MOI);
      ++MOI;
    }
    MIB.addReg(PredR);
    for (const MachineOperand &MO : make_range(MOI, MI->operands_end()))
      MIB.add(MO);

    // Set memory references.
    MIB.cloneMemRefs(*MI);

    MI->eraseFromParent();
    return;
  }

  if (Opc == Hexagon::J2_jump) {
    MachineBasicBlock *TB = MI->getOperand(0).getMBB();
    const MCInstrDesc &D = HII->get(IfTrue ? Hexagon::J2_jumpt
                                           : Hexagon::J2_jumpf);
    BuildMI(*ToB, At, DL, D)
      .addReg(PredR)
      .addMBB(TB);
    MI->eraseFromParent();
    return;
  }

  // Print the offending instruction unconditionally as we are about to
  // abort.
  dbgs() << *MI;
  llvm_unreachable("Unexpected instruction");
}

// Predicate/speculate non-branch instructions from FromB into block ToB.
// Leave the branches alone, they will be handled later. Btw, at this point
// FromB should have at most one branch, and it should be unconditional.
void HexagonEarlyIfConversion::predicateBlockNB(MachineBasicBlock *ToB,
      MachineBasicBlock::iterator At, MachineBasicBlock *FromB,
      unsigned PredR, bool IfTrue) {
  LLVM_DEBUG(dbgs() << "Predicating block " << PrintMB(FromB) << "\n");
  MachineBasicBlock::iterator End = FromB->getFirstTerminator();
  MachineBasicBlock::iterator I, NextI;

  for (I = FromB->begin(); I != End; I = NextI) {
    assert(!I->isPHI());
    NextI = std::next(I);
    if (isSafeToSpeculate(&*I))
      ToB->splice(At, FromB, I);
    else
      predicateInstr(ToB, At, &*I, PredR, IfTrue);
  }
}

unsigned HexagonEarlyIfConversion::buildMux(MachineBasicBlock *B,
      MachineBasicBlock::iterator At, const TargetRegisterClass *DRC,
      unsigned PredR, unsigned TR, unsigned TSR, unsigned FR, unsigned FSR) {
  unsigned Opc = 0;
  switch (DRC->getID()) {
    case Hexagon::IntRegsRegClassID:
    case Hexagon::IntRegsLow8RegClassID:
      Opc = Hexagon::C2_mux;
      break;
    case Hexagon::DoubleRegsRegClassID:
    case Hexagon::GeneralDoubleLow8RegsRegClassID:
      Opc = Hexagon::PS_pselect;
      break;
    case Hexagon::HvxVRRegClassID:
      Opc = Hexagon::PS_vselect;
      break;
    case Hexagon::HvxWRRegClassID:
      Opc = Hexagon::PS_wselect;
      break;
    default:
      llvm_unreachable("unexpected register type");
  }
  const MCInstrDesc &D = HII->get(Opc);

  DebugLoc DL = B->findBranchDebugLoc();
  Register MuxR = MRI->createVirtualRegister(DRC);
  BuildMI(*B, At, DL, D, MuxR)
    .addReg(PredR)
    .addReg(TR, 0, TSR)
    .addReg(FR, 0, FSR);
  return MuxR;
}

void HexagonEarlyIfConversion::updatePhiNodes(MachineBasicBlock *WhereB,
      const FlowPattern &FP) {
  // Visit all PHI nodes in the WhereB block and generate MUX instructions
  // in the split block. Update the PHI nodes with the values of the MUX.
  auto NonPHI = WhereB->getFirstNonPHI();
  for (auto I = WhereB->begin(); I != NonPHI; ++I) {
    MachineInstr *PN = &*I;
    // Registers and subregisters corresponding to TrueB, FalseB and SplitB.
    unsigned TR = 0, TSR = 0, FR = 0, FSR = 0, SR = 0, SSR = 0;
    for (int i = PN->getNumOperands()-2; i > 0; i -= 2) {
      const MachineOperand &RO = PN->getOperand(i), &BO = PN->getOperand(i+1);
      if (BO.getMBB() == FP.SplitB)
        SR = RO.getReg(), SSR = RO.getSubReg();
      else if (BO.getMBB() == FP.TrueB)
        TR = RO.getReg(), TSR = RO.getSubReg();
      else if (BO.getMBB() == FP.FalseB)
        FR = RO.getReg(), FSR = RO.getSubReg();
      else
        continue;
      PN->removeOperand(i+1);
      PN->removeOperand(i);
    }
    if (TR == 0)
      TR = SR, TSR = SSR;
    else if (FR == 0)
      FR = SR, FSR = SSR;

    assert(TR || FR);
    unsigned MuxR = 0, MuxSR = 0;

    if (TR && FR) {
      Register DR = PN->getOperand(0).getReg();
      const TargetRegisterClass *RC = MRI->getRegClass(DR);
      MuxR = buildMux(FP.SplitB, FP.SplitB->getFirstTerminator(), RC,
                      FP.PredR, TR, TSR, FR, FSR);
    } else if (TR) {
      MuxR = TR;
      MuxSR = TSR;
    } else {
      MuxR = FR;
      MuxSR = FSR;
    }

    PN->addOperand(MachineOperand::CreateReg(MuxR, false, false, false, false,
                                             false, false, MuxSR));
    PN->addOperand(MachineOperand::CreateMBB(FP.SplitB));
  }
}

void HexagonEarlyIfConversion::convert(const FlowPattern &FP) {
  MachineBasicBlock *TSB = nullptr, *FSB = nullptr;
  MachineBasicBlock::iterator OldTI = FP.SplitB->getFirstTerminator();
  assert(OldTI != FP.SplitB->end());
  DebugLoc DL = OldTI->getDebugLoc();

  if (FP.TrueB) {
    TSB = *FP.TrueB->succ_begin();
    predicateBlockNB(FP.SplitB, OldTI, FP.TrueB, FP.PredR, true);
  }
  if (FP.FalseB) {
    FSB = *FP.FalseB->succ_begin();
    MachineBasicBlock::iterator At = FP.SplitB->getFirstTerminator();
    predicateBlockNB(FP.SplitB, At, FP.FalseB, FP.PredR, false);
  }

  // Regenerate new terminators in the split block and update the successors.
  // First, remember any information that may be needed later and remove the
  // existing terminators/successors from the split block.
  MachineBasicBlock *SSB = nullptr;
  FP.SplitB->erase(OldTI, FP.SplitB->end());
  while (!FP.SplitB->succ_empty()) {
    MachineBasicBlock *T = *FP.SplitB->succ_begin();
    // It's possible that the split block had a successor that is not a pre-
    // dicated block. This could only happen if there was only one block to
    // be predicated. Example:
    //   split_b:
    //     if (p) jump true_b
    //     jump unrelated2_b
    //   unrelated1_b:
    //     ...
    //   unrelated2_b:  ; can have other predecessors, so it's not "false_b"
    //     jump other_b
    //   true_b:        ; only reachable from split_b, can be predicated
    //     ...
    //
    // Find this successor (SSB) if it exists.
    if (T != FP.TrueB && T != FP.FalseB) {
      assert(!SSB);
      SSB = T;
    }
    FP.SplitB->removeSuccessor(FP.SplitB->succ_begin());
  }

  // Insert new branches and update the successors of the split block. This
  // may create unconditional branches to the layout successor, etc., but
  // that will be cleaned up later. For now, make sure that correct code is
  // generated.
  if (FP.JoinB) {
    assert(!SSB || SSB == FP.JoinB);
    BuildMI(*FP.SplitB, FP.SplitB->end(), DL, HII->get(Hexagon::J2_jump))
      .addMBB(FP.JoinB);
    FP.SplitB->addSuccessor(FP.JoinB);
  } else {
    bool HasBranch = false;
    if (TSB) {
      BuildMI(*FP.SplitB, FP.SplitB->end(), DL, HII->get(Hexagon::J2_jumpt))
        .addReg(FP.PredR)
        .addMBB(TSB);
      FP.SplitB->addSuccessor(TSB);
      HasBranch = true;
    }
    if (FSB) {
      const MCInstrDesc &D = HasBranch ? HII->get(Hexagon::J2_jump)
                                       : HII->get(Hexagon::J2_jumpf);
      MachineInstrBuilder MIB = BuildMI(*FP.SplitB, FP.SplitB->end(), DL, D);
      if (!HasBranch)
        MIB.addReg(FP.PredR);
      MIB.addMBB(FSB);
      FP.SplitB->addSuccessor(FSB);
    }
    if (SSB) {
      // This cannot happen if both TSB and FSB are set. [TF]SB are the
      // successor blocks of the TrueB and FalseB (or null of the TrueB
      // or FalseB block is null). SSB is the potential successor block
      // of the SplitB that is neither TrueB nor FalseB.
      BuildMI(*FP.SplitB, FP.SplitB->end(), DL, HII->get(Hexagon::J2_jump))
        .addMBB(SSB);
      FP.SplitB->addSuccessor(SSB);
    }
  }

  // What is left to do is to update the PHI nodes that could have entries
  // referring to predicated blocks.
  if (FP.JoinB) {
    updatePhiNodes(FP.JoinB, FP);
  } else {
    if (TSB)
      updatePhiNodes(TSB, FP);
    if (FSB)
      updatePhiNodes(FSB, FP);
    // Nothing to update in SSB, since SSB's predecessors haven't changed.
  }
}

void HexagonEarlyIfConversion::removeBlock(MachineBasicBlock *B) {
  LLVM_DEBUG(dbgs() << "Removing block " << PrintMB(B) << "\n");

  // Transfer the immediate dominator information from B to its descendants.
  MachineDomTreeNode *N = MDT->getNode(B);
  MachineDomTreeNode *IDN = N->getIDom();
  if (IDN) {
    MachineBasicBlock *IDB = IDN->getBlock();

    using GTN = GraphTraits<MachineDomTreeNode *>;
    using DTNodeVectType = SmallVector<MachineDomTreeNode *, 4>;

    DTNodeVectType Cn(GTN::child_begin(N), GTN::child_end(N));
    for (auto &I : Cn) {
      MachineBasicBlock *SB = I->getBlock();
      MDT->changeImmediateDominator(SB, IDB);
    }
  }

  while (!B->succ_empty())
    B->removeSuccessor(B->succ_begin());

  for (MachineBasicBlock *Pred : B->predecessors())
    Pred->removeSuccessor(B, true);

  Deleted.insert(B);
  MDT->eraseNode(B);
  MFN->erase(B->getIterator());
}

void HexagonEarlyIfConversion::eliminatePhis(MachineBasicBlock *B) {
  LLVM_DEBUG(dbgs() << "Removing phi nodes from block " << PrintMB(B) << "\n");
  MachineBasicBlock::iterator I, NextI, NonPHI = B->getFirstNonPHI();
  for (I = B->begin(); I != NonPHI; I = NextI) {
    NextI = std::next(I);
    MachineInstr *PN = &*I;
    assert(PN->getNumOperands() == 3 && "Invalid phi node");
    MachineOperand &UO = PN->getOperand(1);
    Register UseR = UO.getReg(), UseSR = UO.getSubReg();
    Register DefR = PN->getOperand(0).getReg();
    unsigned NewR = UseR;
    if (UseSR) {
      // MRI.replaceVregUsesWith does not allow to update the subregister,
      // so instead of doing the use-iteration here, create a copy into a
      // "non-subregistered" register.
      const DebugLoc &DL = PN->getDebugLoc();
      const TargetRegisterClass *RC = MRI->getRegClass(DefR);
      NewR = MRI->createVirtualRegister(RC);
      NonPHI = BuildMI(*B, NonPHI, DL, HII->get(TargetOpcode::COPY), NewR)
        .addReg(UseR, 0, UseSR);
    }
    MRI->replaceRegWith(DefR, NewR);
    B->erase(I);
  }
}

void HexagonEarlyIfConversion::mergeBlocks(MachineBasicBlock *PredB,
      MachineBasicBlock *SuccB) {
  LLVM_DEBUG(dbgs() << "Merging blocks " << PrintMB(PredB) << " and "
                    << PrintMB(SuccB) << "\n");
  bool TermOk = hasUncondBranch(SuccB);
  eliminatePhis(SuccB);
  HII->removeBranch(*PredB);
  PredB->removeSuccessor(SuccB);
  PredB->splice(PredB->end(), SuccB, SuccB->begin(), SuccB->end());
  PredB->transferSuccessorsAndUpdatePHIs(SuccB);
  MachineBasicBlock *OldLayoutSuccessor = SuccB->getNextNode();
  removeBlock(SuccB);
  if (!TermOk)
    PredB->updateTerminator(OldLayoutSuccessor);
}

void HexagonEarlyIfConversion::simplifyFlowGraph(const FlowPattern &FP) {
  MachineBasicBlock *OldLayoutSuccessor = FP.SplitB->getNextNode();
  if (FP.TrueB)
    removeBlock(FP.TrueB);
  if (FP.FalseB)
    removeBlock(FP.FalseB);

  FP.SplitB->updateTerminator(OldLayoutSuccessor);
  if (FP.SplitB->succ_size() != 1)
    return;

  MachineBasicBlock *SB = *FP.SplitB->succ_begin();
  if (SB->pred_size() != 1)
    return;

  // By now, the split block has only one successor (SB), and SB has only
  // one predecessor. We can try to merge them. We will need to update ter-
  // minators in FP.Split+SB, and that requires working analyzeBranch, which
  // fails on Hexagon for blocks that have EH_LABELs. However, if SB ends
  // with an unconditional branch, we won't need to touch the terminators.
  if (!hasEHLabel(SB) || hasUncondBranch(SB))
    mergeBlocks(FP.SplitB, SB);
}

bool HexagonEarlyIfConversion::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  auto &ST = MF.getSubtarget<HexagonSubtarget>();
  HII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  MFN = &MF;
  MRI = &MF.getRegInfo();
  MDT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MLI = &getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  MBPI = EnableHexagonBP
             ? &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI()
             : nullptr;

  Deleted.clear();
  bool Changed = false;

  for (MachineLoop *L : *MLI)
    Changed |= visitLoop(L);
  Changed |= visitLoop(nullptr);

  return Changed;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//
FunctionPass *llvm::createHexagonEarlyIfConversion() {
  return new HexagonEarlyIfConversion();
}
