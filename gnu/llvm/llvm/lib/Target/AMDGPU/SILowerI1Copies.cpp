//===-- SILowerI1Copies.cpp - Lower I1 Copies -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers all occurrences of i1 values (with a vreg_1 register class)
// to lane masks (32 / 64-bit scalar registers). The pass assumes machine SSA
// form and a wave-level control flow graph.
//
// Before this pass, values that are semantically i1 and are defined and used
// within the same basic block are already represented as lane masks in scalar
// registers. However, values that cross basic blocks are always transferred
// between basic blocks in vreg_1 virtual registers and are lowered by this
// pass.
//
// The only instructions that use or define vreg_1 virtual registers are COPY,
// PHI, and IMPLICIT_DEF.
//
//===----------------------------------------------------------------------===//

#include "SILowerI1Copies.h"
#include "AMDGPU.h"
#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/InitializePasses.h"
#include "llvm/Target/CGPassBuilderOption.h"

#define DEBUG_TYPE "si-i1-copies"

using namespace llvm;

static Register
insertUndefLaneMask(MachineBasicBlock *MBB, MachineRegisterInfo *MRI,
                    MachineRegisterInfo::VRegAttrs LaneMaskRegAttrs);

namespace {

class SILowerI1Copies : public MachineFunctionPass {
public:
  static char ID;

  SILowerI1Copies() : MachineFunctionPass(ID) {
    initializeSILowerI1CopiesPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "SI Lower i1 Copies"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachinePostDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

class Vreg1LoweringHelper : public PhiLoweringHelper {
public:
  Vreg1LoweringHelper(MachineFunction *MF, MachineDominatorTree *DT,
                      MachinePostDominatorTree *PDT);

private:
  DenseSet<Register> ConstrainRegs;

public:
  void markAsLaneMask(Register DstReg) const override;
  void getCandidatesForLowering(
      SmallVectorImpl<MachineInstr *> &Vreg1Phis) const override;
  void collectIncomingValuesFromPhi(
      const MachineInstr *MI,
      SmallVectorImpl<Incoming> &Incomings) const override;
  void replaceDstReg(Register NewReg, Register OldReg,
                     MachineBasicBlock *MBB) override;
  void buildMergeLaneMasks(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator I, const DebugLoc &DL,
                           Register DstReg, Register PrevReg,
                           Register CurReg) override;
  void constrainAsLaneMask(Incoming &In) override;

  bool lowerCopiesFromI1();
  bool lowerCopiesToI1();
  bool cleanConstrainRegs(bool Changed);
  bool isVreg1(Register Reg) const {
    return Reg.isVirtual() && MRI->getRegClass(Reg) == &AMDGPU::VReg_1RegClass;
  }
};

Vreg1LoweringHelper::Vreg1LoweringHelper(MachineFunction *MF,
                                         MachineDominatorTree *DT,
                                         MachinePostDominatorTree *PDT)
    : PhiLoweringHelper(MF, DT, PDT) {}

bool Vreg1LoweringHelper::cleanConstrainRegs(bool Changed) {
  assert(Changed || ConstrainRegs.empty());
  for (Register Reg : ConstrainRegs)
    MRI->constrainRegClass(Reg, &AMDGPU::SReg_1_XEXECRegClass);
  ConstrainRegs.clear();

  return Changed;
}

/// Helper class that determines the relationship between incoming values of a
/// phi in the control flow graph to determine where an incoming value can
/// simply be taken as a scalar lane mask as-is, and where it needs to be
/// merged with another, previously defined lane mask.
///
/// The approach is as follows:
///  - Determine all basic blocks which, starting from the incoming blocks,
///    a wave may reach before entering the def block (the block containing the
///    phi).
///  - If an incoming block has no predecessors in this set, we can take the
///    incoming value as a scalar lane mask as-is.
///  -- A special case of this is when the def block has a self-loop.
///  - Otherwise, the incoming value needs to be merged with a previously
///    defined lane mask.
///  - If there is a path into the set of reachable blocks that does _not_ go
///    through an incoming block where we can take the scalar lane mask as-is,
///    we need to invent an available value for the SSAUpdater. Choices are
///    0 and undef, with differing consequences for how to merge values etc.
///
/// TODO: We could use region analysis to quickly skip over SESE regions during
///       the traversal.
///
class PhiIncomingAnalysis {
  MachinePostDominatorTree &PDT;
  const SIInstrInfo *TII;

  // For each reachable basic block, whether it is a source in the induced
  // subgraph of the CFG.
  DenseMap<MachineBasicBlock *, bool> ReachableMap;
  SmallVector<MachineBasicBlock *, 4> ReachableOrdered;
  SmallVector<MachineBasicBlock *, 4> Stack;
  SmallVector<MachineBasicBlock *, 4> Predecessors;

public:
  PhiIncomingAnalysis(MachinePostDominatorTree &PDT, const SIInstrInfo *TII)
      : PDT(PDT), TII(TII) {}

  /// Returns whether \p MBB is a source in the induced subgraph of reachable
  /// blocks.
  bool isSource(MachineBasicBlock &MBB) const {
    return ReachableMap.find(&MBB)->second;
  }

  ArrayRef<MachineBasicBlock *> predecessors() const { return Predecessors; }

  void analyze(MachineBasicBlock &DefBlock, ArrayRef<Incoming> Incomings) {
    assert(Stack.empty());
    ReachableMap.clear();
    ReachableOrdered.clear();
    Predecessors.clear();

    // Insert the def block first, so that it acts as an end point for the
    // traversal.
    ReachableMap.try_emplace(&DefBlock, false);
    ReachableOrdered.push_back(&DefBlock);

    for (auto Incoming : Incomings) {
      MachineBasicBlock *MBB = Incoming.Block;
      if (MBB == &DefBlock) {
        ReachableMap[&DefBlock] = true; // self-loop on DefBlock
        continue;
      }

      ReachableMap.try_emplace(MBB, false);
      ReachableOrdered.push_back(MBB);

      // If this block has a divergent terminator and the def block is its
      // post-dominator, the wave may first visit the other successors.
      if (TII->hasDivergentBranch(MBB) && PDT.dominates(&DefBlock, MBB))
        append_range(Stack, MBB->successors());
    }

    while (!Stack.empty()) {
      MachineBasicBlock *MBB = Stack.pop_back_val();
      if (!ReachableMap.try_emplace(MBB, false).second)
        continue;
      ReachableOrdered.push_back(MBB);

      append_range(Stack, MBB->successors());
    }

    for (MachineBasicBlock *MBB : ReachableOrdered) {
      bool HaveReachablePred = false;
      for (MachineBasicBlock *Pred : MBB->predecessors()) {
        if (ReachableMap.count(Pred)) {
          HaveReachablePred = true;
        } else {
          Stack.push_back(Pred);
        }
      }
      if (!HaveReachablePred)
        ReachableMap[MBB] = true;
      if (HaveReachablePred) {
        for (MachineBasicBlock *UnreachablePred : Stack) {
          if (!llvm::is_contained(Predecessors, UnreachablePred))
            Predecessors.push_back(UnreachablePred);
        }
      }
      Stack.clear();
    }
  }
};

/// Helper class that detects loops which require us to lower an i1 COPY into
/// bitwise manipulation.
///
/// Unfortunately, we cannot use LoopInfo because LoopInfo does not distinguish
/// between loops with the same header. Consider this example:
///
///  A-+-+
///  | | |
///  B-+ |
///  |   |
///  C---+
///
/// A is the header of a loop containing A, B, and C as far as LoopInfo is
/// concerned. However, an i1 COPY in B that is used in C must be lowered to
/// bitwise operations to combine results from different loop iterations when
/// B has a divergent branch (since by default we will compile this code such
/// that threads in a wave are merged at the entry of C).
///
/// The following rule is implemented to determine whether bitwise operations
/// are required: use the bitwise lowering for a def in block B if a backward
/// edge to B is reachable without going through the nearest common
/// post-dominator of B and all uses of the def.
///
/// TODO: This rule is conservative because it does not check whether the
///       relevant branches are actually divergent.
///
/// The class is designed to cache the CFG traversal so that it can be re-used
/// for multiple defs within the same basic block.
///
/// TODO: We could use region analysis to quickly skip over SESE regions during
///       the traversal.
///
class LoopFinder {
  MachineDominatorTree &DT;
  MachinePostDominatorTree &PDT;

  // All visited / reachable block, tagged by level (level 0 is the def block,
  // level 1 are all blocks reachable including but not going through the def
  // block's IPDOM, etc.).
  DenseMap<MachineBasicBlock *, unsigned> Visited;

  // Nearest common dominator of all visited blocks by level (level 0 is the
  // def block). Used for seeding the SSAUpdater.
  SmallVector<MachineBasicBlock *, 4> CommonDominators;

  // Post-dominator of all visited blocks.
  MachineBasicBlock *VisitedPostDom = nullptr;

  // Level at which a loop was found: 0 is not possible; 1 = a backward edge is
  // reachable without going through the IPDOM of the def block (if the IPDOM
  // itself has an edge to the def block, the loop level is 2), etc.
  unsigned FoundLoopLevel = ~0u;

  MachineBasicBlock *DefBlock = nullptr;
  SmallVector<MachineBasicBlock *, 4> Stack;
  SmallVector<MachineBasicBlock *, 4> NextLevel;

public:
  LoopFinder(MachineDominatorTree &DT, MachinePostDominatorTree &PDT)
      : DT(DT), PDT(PDT) {}

  void initialize(MachineBasicBlock &MBB) {
    Visited.clear();
    CommonDominators.clear();
    Stack.clear();
    NextLevel.clear();
    VisitedPostDom = nullptr;
    FoundLoopLevel = ~0u;

    DefBlock = &MBB;
  }

  /// Check whether a backward edge can be reached without going through the
  /// given \p PostDom of the def block.
  ///
  /// Return the level of \p PostDom if a loop was found, or 0 otherwise.
  unsigned findLoop(MachineBasicBlock *PostDom) {
    MachineDomTreeNode *PDNode = PDT.getNode(DefBlock);

    if (!VisitedPostDom)
      advanceLevel();

    unsigned Level = 0;
    while (PDNode->getBlock() != PostDom) {
      if (PDNode->getBlock() == VisitedPostDom)
        advanceLevel();
      PDNode = PDNode->getIDom();
      Level++;
      if (FoundLoopLevel == Level)
        return Level;
    }

    return 0;
  }

  /// Add undef values dominating the loop and the optionally given additional
  /// blocks, so that the SSA updater doesn't have to search all the way to the
  /// function entry.
  void addLoopEntries(unsigned LoopLevel, MachineSSAUpdater &SSAUpdater,
                      MachineRegisterInfo &MRI,
                      MachineRegisterInfo::VRegAttrs LaneMaskRegAttrs,
                      ArrayRef<Incoming> Incomings = {}) {
    assert(LoopLevel < CommonDominators.size());

    MachineBasicBlock *Dom = CommonDominators[LoopLevel];
    for (auto &Incoming : Incomings)
      Dom = DT.findNearestCommonDominator(Dom, Incoming.Block);

    if (!inLoopLevel(*Dom, LoopLevel, Incomings)) {
      SSAUpdater.AddAvailableValue(
          Dom, insertUndefLaneMask(Dom, &MRI, LaneMaskRegAttrs));
    } else {
      // The dominator is part of the loop or the given blocks, so add the
      // undef value to unreachable predecessors instead.
      for (MachineBasicBlock *Pred : Dom->predecessors()) {
        if (!inLoopLevel(*Pred, LoopLevel, Incomings))
          SSAUpdater.AddAvailableValue(
              Pred, insertUndefLaneMask(Pred, &MRI, LaneMaskRegAttrs));
      }
    }
  }

private:
  bool inLoopLevel(MachineBasicBlock &MBB, unsigned LoopLevel,
                   ArrayRef<Incoming> Incomings) const {
    auto DomIt = Visited.find(&MBB);
    if (DomIt != Visited.end() && DomIt->second <= LoopLevel)
      return true;

    for (auto &Incoming : Incomings)
      if (Incoming.Block == &MBB)
        return true;

    return false;
  }

  void advanceLevel() {
    MachineBasicBlock *VisitedDom;

    if (!VisitedPostDom) {
      VisitedPostDom = DefBlock;
      VisitedDom = DefBlock;
      Stack.push_back(DefBlock);
    } else {
      VisitedPostDom = PDT.getNode(VisitedPostDom)->getIDom()->getBlock();
      VisitedDom = CommonDominators.back();

      for (unsigned i = 0; i < NextLevel.size();) {
        if (PDT.dominates(VisitedPostDom, NextLevel[i])) {
          Stack.push_back(NextLevel[i]);

          NextLevel[i] = NextLevel.back();
          NextLevel.pop_back();
        } else {
          i++;
        }
      }
    }

    unsigned Level = CommonDominators.size();
    while (!Stack.empty()) {
      MachineBasicBlock *MBB = Stack.pop_back_val();
      if (!PDT.dominates(VisitedPostDom, MBB))
        NextLevel.push_back(MBB);

      Visited[MBB] = Level;
      VisitedDom = DT.findNearestCommonDominator(VisitedDom, MBB);

      for (MachineBasicBlock *Succ : MBB->successors()) {
        if (Succ == DefBlock) {
          if (MBB == VisitedPostDom)
            FoundLoopLevel = std::min(FoundLoopLevel, Level + 1);
          else
            FoundLoopLevel = std::min(FoundLoopLevel, Level);
          continue;
        }

        if (Visited.try_emplace(Succ, ~0u).second) {
          if (MBB == VisitedPostDom)
            NextLevel.push_back(Succ);
          else
            Stack.push_back(Succ);
        }
      }
    }

    CommonDominators.push_back(VisitedDom);
  }
};

} // End anonymous namespace.

INITIALIZE_PASS_BEGIN(SILowerI1Copies, DEBUG_TYPE, "SI Lower i1 Copies", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachinePostDominatorTreeWrapperPass)
INITIALIZE_PASS_END(SILowerI1Copies, DEBUG_TYPE, "SI Lower i1 Copies", false,
                    false)

char SILowerI1Copies::ID = 0;

char &llvm::SILowerI1CopiesID = SILowerI1Copies::ID;

FunctionPass *llvm::createSILowerI1CopiesPass() {
  return new SILowerI1Copies();
}

Register
llvm::createLaneMaskReg(MachineRegisterInfo *MRI,
                        MachineRegisterInfo::VRegAttrs LaneMaskRegAttrs) {
  return MRI->createVirtualRegister(LaneMaskRegAttrs);
}

static Register
insertUndefLaneMask(MachineBasicBlock *MBB, MachineRegisterInfo *MRI,
                    MachineRegisterInfo::VRegAttrs LaneMaskRegAttrs) {
  MachineFunction &MF = *MBB->getParent();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  Register UndefReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);
  BuildMI(*MBB, MBB->getFirstTerminator(), {}, TII->get(AMDGPU::IMPLICIT_DEF),
          UndefReg);
  return UndefReg;
}

/// Lower all instructions that def or use vreg_1 registers.
///
/// In a first pass, we lower COPYs from vreg_1 to vector registers, as can
/// occur around inline assembly. We do this first, before vreg_1 registers
/// are changed to scalar mask registers.
///
/// Then we lower all defs of vreg_1 registers. Phi nodes are lowered before
/// all others, because phi lowering looks through copies and can therefore
/// often make copy lowering unnecessary.
bool SILowerI1Copies::runOnMachineFunction(MachineFunction &TheMF) {
  // Only need to run this in SelectionDAG path.
  if (TheMF.getProperties().hasProperty(
          MachineFunctionProperties::Property::Selected))
    return false;

  Vreg1LoweringHelper Helper(
      &TheMF, &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree(),
      &getAnalysis<MachinePostDominatorTreeWrapperPass>().getPostDomTree());

  bool Changed = false;
  Changed |= Helper.lowerCopiesFromI1();
  Changed |= Helper.lowerPhis();
  Changed |= Helper.lowerCopiesToI1();
  return Helper.cleanConstrainRegs(Changed);
}

#ifndef NDEBUG
static bool isVRegCompatibleReg(const SIRegisterInfo &TRI,
                                const MachineRegisterInfo &MRI,
                                Register Reg) {
  unsigned Size = TRI.getRegSizeInBits(Reg, MRI);
  return Size == 1 || Size == 32;
}
#endif

bool Vreg1LoweringHelper::lowerCopiesFromI1() {
  bool Changed = false;
  SmallVector<MachineInstr *, 4> DeadCopies;

  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != AMDGPU::COPY)
        continue;

      Register DstReg = MI.getOperand(0).getReg();
      Register SrcReg = MI.getOperand(1).getReg();
      if (!isVreg1(SrcReg))
        continue;

      if (isLaneMaskReg(DstReg) || isVreg1(DstReg))
        continue;

      Changed = true;

      // Copy into a 32-bit vector register.
      LLVM_DEBUG(dbgs() << "Lower copy from i1: " << MI);
      DebugLoc DL = MI.getDebugLoc();

      assert(isVRegCompatibleReg(TII->getRegisterInfo(), *MRI, DstReg));
      assert(!MI.getOperand(0).getSubReg());

      ConstrainRegs.insert(SrcReg);
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_CNDMASK_B32_e64), DstReg)
          .addImm(0)
          .addImm(0)
          .addImm(0)
          .addImm(-1)
          .addReg(SrcReg);
      DeadCopies.push_back(&MI);
    }

    for (MachineInstr *MI : DeadCopies)
      MI->eraseFromParent();
    DeadCopies.clear();
  }
  return Changed;
}

PhiLoweringHelper::PhiLoweringHelper(MachineFunction *MF,
                                     MachineDominatorTree *DT,
                                     MachinePostDominatorTree *PDT)
    : MF(MF), DT(DT), PDT(PDT) {
  MRI = &MF->getRegInfo();

  ST = &MF->getSubtarget<GCNSubtarget>();
  TII = ST->getInstrInfo();
  IsWave32 = ST->isWave32();

  if (IsWave32) {
    ExecReg = AMDGPU::EXEC_LO;
    MovOp = AMDGPU::S_MOV_B32;
    AndOp = AMDGPU::S_AND_B32;
    OrOp = AMDGPU::S_OR_B32;
    XorOp = AMDGPU::S_XOR_B32;
    AndN2Op = AMDGPU::S_ANDN2_B32;
    OrN2Op = AMDGPU::S_ORN2_B32;
  } else {
    ExecReg = AMDGPU::EXEC;
    MovOp = AMDGPU::S_MOV_B64;
    AndOp = AMDGPU::S_AND_B64;
    OrOp = AMDGPU::S_OR_B64;
    XorOp = AMDGPU::S_XOR_B64;
    AndN2Op = AMDGPU::S_ANDN2_B64;
    OrN2Op = AMDGPU::S_ORN2_B64;
  }
}

bool PhiLoweringHelper::lowerPhis() {
  MachineSSAUpdater SSAUpdater(*MF);
  LoopFinder LF(*DT, *PDT);
  PhiIncomingAnalysis PIA(*PDT, TII);
  SmallVector<MachineInstr *, 4> Vreg1Phis;
  SmallVector<Incoming, 4> Incomings;

  getCandidatesForLowering(Vreg1Phis);
  if (Vreg1Phis.empty())
    return false;

  DT->getBase().updateDFSNumbers();
  MachineBasicBlock *PrevMBB = nullptr;
  for (MachineInstr *MI : Vreg1Phis) {
    MachineBasicBlock &MBB = *MI->getParent();
    if (&MBB != PrevMBB) {
      LF.initialize(MBB);
      PrevMBB = &MBB;
    }

    LLVM_DEBUG(dbgs() << "Lower PHI: " << *MI);

    Register DstReg = MI->getOperand(0).getReg();
    markAsLaneMask(DstReg);
    initializeLaneMaskRegisterAttributes(DstReg);

    collectIncomingValuesFromPhi(MI, Incomings);

    // Sort the incomings such that incoming values that dominate other incoming
    // values are sorted earlier. This allows us to do some amount of on-the-fly
    // constant folding.
    // Incoming with smaller DFSNumIn goes first, DFSNumIn is 0 for entry block.
    llvm::sort(Incomings, [this](Incoming LHS, Incoming RHS) {
      return DT->getNode(LHS.Block)->getDFSNumIn() <
             DT->getNode(RHS.Block)->getDFSNumIn();
    });

#ifndef NDEBUG
    PhiRegisters.insert(DstReg);
#endif

    // Phis in a loop that are observed outside the loop receive a simple but
    // conservatively correct treatment.
    std::vector<MachineBasicBlock *> DomBlocks = {&MBB};
    for (MachineInstr &Use : MRI->use_instructions(DstReg))
      DomBlocks.push_back(Use.getParent());

    MachineBasicBlock *PostDomBound =
        PDT->findNearestCommonDominator(DomBlocks);

    // FIXME: This fails to find irreducible cycles. If we have a def (other
    // than a constant) in a pair of blocks that end up looping back to each
    // other, it will be mishandle. Due to structurization this shouldn't occur
    // in practice.
    unsigned FoundLoopLevel = LF.findLoop(PostDomBound);

    SSAUpdater.Initialize(DstReg);

    if (FoundLoopLevel) {
      LF.addLoopEntries(FoundLoopLevel, SSAUpdater, *MRI, LaneMaskRegAttrs,
                        Incomings);

      for (auto &Incoming : Incomings) {
        Incoming.UpdatedReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);
        SSAUpdater.AddAvailableValue(Incoming.Block, Incoming.UpdatedReg);
      }

      for (auto &Incoming : Incomings) {
        MachineBasicBlock &IMBB = *Incoming.Block;
        buildMergeLaneMasks(
            IMBB, getSaluInsertionAtEnd(IMBB), {}, Incoming.UpdatedReg,
            SSAUpdater.GetValueInMiddleOfBlock(&IMBB), Incoming.Reg);
      }
    } else {
      // The phi is not observed from outside a loop. Use a more accurate
      // lowering.
      PIA.analyze(MBB, Incomings);

      for (MachineBasicBlock *MBB : PIA.predecessors())
        SSAUpdater.AddAvailableValue(
            MBB, insertUndefLaneMask(MBB, MRI, LaneMaskRegAttrs));

      for (auto &Incoming : Incomings) {
        MachineBasicBlock &IMBB = *Incoming.Block;
        if (PIA.isSource(IMBB)) {
          constrainAsLaneMask(Incoming);
          SSAUpdater.AddAvailableValue(&IMBB, Incoming.Reg);
        } else {
          Incoming.UpdatedReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);
          SSAUpdater.AddAvailableValue(&IMBB, Incoming.UpdatedReg);
        }
      }

      for (auto &Incoming : Incomings) {
        if (!Incoming.UpdatedReg.isValid())
          continue;

        MachineBasicBlock &IMBB = *Incoming.Block;
        buildMergeLaneMasks(
            IMBB, getSaluInsertionAtEnd(IMBB), {}, Incoming.UpdatedReg,
            SSAUpdater.GetValueInMiddleOfBlock(&IMBB), Incoming.Reg);
      }
    }

    Register NewReg = SSAUpdater.GetValueInMiddleOfBlock(&MBB);
    if (NewReg != DstReg) {
      replaceDstReg(NewReg, DstReg, &MBB);
      MI->eraseFromParent();
    }

    Incomings.clear();
  }
  return true;
}

bool Vreg1LoweringHelper::lowerCopiesToI1() {
  bool Changed = false;
  MachineSSAUpdater SSAUpdater(*MF);
  LoopFinder LF(*DT, *PDT);
  SmallVector<MachineInstr *, 4> DeadCopies;

  for (MachineBasicBlock &MBB : *MF) {
    LF.initialize(MBB);

    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != AMDGPU::IMPLICIT_DEF &&
          MI.getOpcode() != AMDGPU::COPY)
        continue;

      Register DstReg = MI.getOperand(0).getReg();
      if (!isVreg1(DstReg))
        continue;

      Changed = true;

      if (MRI->use_empty(DstReg)) {
        DeadCopies.push_back(&MI);
        continue;
      }

      LLVM_DEBUG(dbgs() << "Lower Other: " << MI);

      markAsLaneMask(DstReg);
      initializeLaneMaskRegisterAttributes(DstReg);

      if (MI.getOpcode() == AMDGPU::IMPLICIT_DEF)
        continue;

      DebugLoc DL = MI.getDebugLoc();
      Register SrcReg = MI.getOperand(1).getReg();
      assert(!MI.getOperand(1).getSubReg());

      if (!SrcReg.isVirtual() || (!isLaneMaskReg(SrcReg) && !isVreg1(SrcReg))) {
        assert(TII->getRegisterInfo().getRegSizeInBits(SrcReg, *MRI) == 32);
        Register TmpReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);
        BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_CMP_NE_U32_e64), TmpReg)
            .addReg(SrcReg)
            .addImm(0);
        MI.getOperand(1).setReg(TmpReg);
        SrcReg = TmpReg;
      } else {
        // SrcReg needs to be live beyond copy.
        MI.getOperand(1).setIsKill(false);
      }

      // Defs in a loop that are observed outside the loop must be transformed
      // into appropriate bit manipulation.
      std::vector<MachineBasicBlock *> DomBlocks = {&MBB};
      for (MachineInstr &Use : MRI->use_instructions(DstReg))
        DomBlocks.push_back(Use.getParent());

      MachineBasicBlock *PostDomBound =
          PDT->findNearestCommonDominator(DomBlocks);
      unsigned FoundLoopLevel = LF.findLoop(PostDomBound);
      if (FoundLoopLevel) {
        SSAUpdater.Initialize(DstReg);
        SSAUpdater.AddAvailableValue(&MBB, DstReg);
        LF.addLoopEntries(FoundLoopLevel, SSAUpdater, *MRI, LaneMaskRegAttrs);

        buildMergeLaneMasks(MBB, MI, DL, DstReg,
                            SSAUpdater.GetValueInMiddleOfBlock(&MBB), SrcReg);
        DeadCopies.push_back(&MI);
      }
    }

    for (MachineInstr *MI : DeadCopies)
      MI->eraseFromParent();
    DeadCopies.clear();
  }
  return Changed;
}

bool PhiLoweringHelper::isConstantLaneMask(Register Reg, bool &Val) const {
  const MachineInstr *MI;
  for (;;) {
    MI = MRI->getUniqueVRegDef(Reg);
    if (MI->getOpcode() == AMDGPU::IMPLICIT_DEF)
      return true;

    if (MI->getOpcode() != AMDGPU::COPY)
      break;

    Reg = MI->getOperand(1).getReg();
    if (!Reg.isVirtual())
      return false;
    if (!isLaneMaskReg(Reg))
      return false;
  }

  if (MI->getOpcode() != MovOp)
    return false;

  if (!MI->getOperand(1).isImm())
    return false;

  int64_t Imm = MI->getOperand(1).getImm();
  if (Imm == 0) {
    Val = false;
    return true;
  }
  if (Imm == -1) {
    Val = true;
    return true;
  }

  return false;
}

static void instrDefsUsesSCC(const MachineInstr &MI, bool &Def, bool &Use) {
  Def = false;
  Use = false;

  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.getReg() == AMDGPU::SCC) {
      if (MO.isUse())
        Use = true;
      else
        Def = true;
    }
  }
}

/// Return a point at the end of the given \p MBB to insert SALU instructions
/// for lane mask calculation. Take terminators and SCC into account.
MachineBasicBlock::iterator
PhiLoweringHelper::getSaluInsertionAtEnd(MachineBasicBlock &MBB) const {
  auto InsertionPt = MBB.getFirstTerminator();
  bool TerminatorsUseSCC = false;
  for (auto I = InsertionPt, E = MBB.end(); I != E; ++I) {
    bool DefsSCC;
    instrDefsUsesSCC(*I, DefsSCC, TerminatorsUseSCC);
    if (TerminatorsUseSCC || DefsSCC)
      break;
  }

  if (!TerminatorsUseSCC)
    return InsertionPt;

  while (InsertionPt != MBB.begin()) {
    InsertionPt--;

    bool DefSCC, UseSCC;
    instrDefsUsesSCC(*InsertionPt, DefSCC, UseSCC);
    if (DefSCC)
      return InsertionPt;
  }

  // We should have at least seen an IMPLICIT_DEF or COPY
  llvm_unreachable("SCC used by terminator but no def in block");
}

// VReg_1 -> SReg_32 or SReg_64
void Vreg1LoweringHelper::markAsLaneMask(Register DstReg) const {
  MRI->setRegClass(DstReg, ST->getBoolRC());
}

void Vreg1LoweringHelper::getCandidatesForLowering(
    SmallVectorImpl<MachineInstr *> &Vreg1Phis) const {
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB.phis()) {
      if (isVreg1(MI.getOperand(0).getReg()))
        Vreg1Phis.push_back(&MI);
    }
  }
}

void Vreg1LoweringHelper::collectIncomingValuesFromPhi(
    const MachineInstr *MI, SmallVectorImpl<Incoming> &Incomings) const {
  for (unsigned i = 1; i < MI->getNumOperands(); i += 2) {
    assert(i + 1 < MI->getNumOperands());
    Register IncomingReg = MI->getOperand(i).getReg();
    MachineBasicBlock *IncomingMBB = MI->getOperand(i + 1).getMBB();
    MachineInstr *IncomingDef = MRI->getUniqueVRegDef(IncomingReg);

    if (IncomingDef->getOpcode() == AMDGPU::COPY) {
      IncomingReg = IncomingDef->getOperand(1).getReg();
      assert(isLaneMaskReg(IncomingReg) || isVreg1(IncomingReg));
      assert(!IncomingDef->getOperand(1).getSubReg());
    } else if (IncomingDef->getOpcode() == AMDGPU::IMPLICIT_DEF) {
      continue;
    } else {
      assert(IncomingDef->isPHI() || PhiRegisters.count(IncomingReg));
    }

    Incomings.emplace_back(IncomingReg, IncomingMBB, Register());
  }
}

void Vreg1LoweringHelper::replaceDstReg(Register NewReg, Register OldReg,
                                        MachineBasicBlock *MBB) {
  MRI->replaceRegWith(NewReg, OldReg);
}

void Vreg1LoweringHelper::buildMergeLaneMasks(MachineBasicBlock &MBB,
                                              MachineBasicBlock::iterator I,
                                              const DebugLoc &DL,
                                              Register DstReg, Register PrevReg,
                                              Register CurReg) {
  bool PrevVal = false;
  bool PrevConstant = isConstantLaneMask(PrevReg, PrevVal);
  bool CurVal = false;
  bool CurConstant = isConstantLaneMask(CurReg, CurVal);

  if (PrevConstant && CurConstant) {
    if (PrevVal == CurVal) {
      BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg).addReg(CurReg);
    } else if (CurVal) {
      BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg).addReg(ExecReg);
    } else {
      BuildMI(MBB, I, DL, TII->get(XorOp), DstReg)
          .addReg(ExecReg)
          .addImm(-1);
    }
    return;
  }

  Register PrevMaskedReg;
  Register CurMaskedReg;
  if (!PrevConstant) {
    if (CurConstant && CurVal) {
      PrevMaskedReg = PrevReg;
    } else {
      PrevMaskedReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);
      BuildMI(MBB, I, DL, TII->get(AndN2Op), PrevMaskedReg)
          .addReg(PrevReg)
          .addReg(ExecReg);
    }
  }
  if (!CurConstant) {
    // TODO: check whether CurReg is already masked by EXEC
    if (PrevConstant && PrevVal) {
      CurMaskedReg = CurReg;
    } else {
      CurMaskedReg = createLaneMaskReg(MRI, LaneMaskRegAttrs);
      BuildMI(MBB, I, DL, TII->get(AndOp), CurMaskedReg)
          .addReg(CurReg)
          .addReg(ExecReg);
    }
  }

  if (PrevConstant && !PrevVal) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg)
        .addReg(CurMaskedReg);
  } else if (CurConstant && !CurVal) {
    BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), DstReg)
        .addReg(PrevMaskedReg);
  } else if (PrevConstant && PrevVal) {
    BuildMI(MBB, I, DL, TII->get(OrN2Op), DstReg)
        .addReg(CurMaskedReg)
        .addReg(ExecReg);
  } else {
    BuildMI(MBB, I, DL, TII->get(OrOp), DstReg)
        .addReg(PrevMaskedReg)
        .addReg(CurMaskedReg ? CurMaskedReg : ExecReg);
  }
}

void Vreg1LoweringHelper::constrainAsLaneMask(Incoming &In) {}
