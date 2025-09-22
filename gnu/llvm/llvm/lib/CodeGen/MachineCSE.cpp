//===- MachineCSE.cpp - Machine Common Subexpression Elimination Pass -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs global common subexpression elimination on machine
// instructions using a scoped hash table based value numbering scheme. It
// must be run while the machine function is still in SSA form.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/RecyclingAllocator.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "machine-cse"

STATISTIC(NumCoalesces, "Number of copies coalesced");
STATISTIC(NumCSEs,      "Number of common subexpression eliminated");
STATISTIC(NumPREs,      "Number of partial redundant expression"
                        " transformed to fully redundant");
STATISTIC(NumPhysCSEs,
          "Number of physreg referencing common subexpr eliminated");
STATISTIC(NumCrossBBCSEs,
          "Number of cross-MBB physreg referencing CS eliminated");
STATISTIC(NumCommutes,  "Number of copies coalesced after commuting");

// Threshold to avoid excessive cost to compute isProfitableToCSE.
static cl::opt<int>
    CSUsesThreshold("csuses-threshold", cl::Hidden, cl::init(1024),
                    cl::desc("Threshold for the size of CSUses"));

static cl::opt<bool> AggressiveMachineCSE(
    "aggressive-machine-cse", cl::Hidden, cl::init(false),
    cl::desc("Override the profitability heuristics for Machine CSE"));

namespace {

  class MachineCSE : public MachineFunctionPass {
    const TargetInstrInfo *TII = nullptr;
    const TargetRegisterInfo *TRI = nullptr;
    AliasAnalysis *AA = nullptr;
    MachineDominatorTree *DT = nullptr;
    MachineRegisterInfo *MRI = nullptr;
    MachineBlockFrequencyInfo *MBFI = nullptr;

  public:
    static char ID; // Pass identification

    MachineCSE() : MachineFunctionPass(ID) {
      initializeMachineCSEPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
      AU.addRequired<AAResultsWrapperPass>();
      AU.addPreservedID(MachineLoopInfoID);
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addPreserved<MachineDominatorTreeWrapperPass>();
      AU.addRequired<MachineBlockFrequencyInfoWrapperPass>();
      AU.addPreserved<MachineBlockFrequencyInfoWrapperPass>();
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties()
        .set(MachineFunctionProperties::Property::IsSSA);
    }

    void releaseMemory() override {
      ScopeMap.clear();
      PREMap.clear();
      Exps.clear();
    }

  private:
    using AllocatorTy = RecyclingAllocator<BumpPtrAllocator,
                            ScopedHashTableVal<MachineInstr *, unsigned>>;
    using ScopedHTType =
        ScopedHashTable<MachineInstr *, unsigned, MachineInstrExpressionTrait,
                        AllocatorTy>;
    using ScopeType = ScopedHTType::ScopeTy;
    using PhysDefVector = SmallVector<std::pair<unsigned, unsigned>, 2>;

    unsigned LookAheadLimit = 0;
    DenseMap<MachineBasicBlock *, ScopeType *> ScopeMap;
    DenseMap<MachineInstr *, MachineBasicBlock *, MachineInstrExpressionTrait>
        PREMap;
    ScopedHTType VNT;
    SmallVector<MachineInstr *, 64> Exps;
    unsigned CurrVN = 0;

    bool PerformTrivialCopyPropagation(MachineInstr *MI,
                                       MachineBasicBlock *MBB);
    bool isPhysDefTriviallyDead(MCRegister Reg,
                                MachineBasicBlock::const_iterator I,
                                MachineBasicBlock::const_iterator E) const;
    bool hasLivePhysRegDefUses(const MachineInstr *MI,
                               const MachineBasicBlock *MBB,
                               SmallSet<MCRegister, 8> &PhysRefs,
                               PhysDefVector &PhysDefs, bool &PhysUseDef) const;
    bool PhysRegDefsReach(MachineInstr *CSMI, MachineInstr *MI,
                          SmallSet<MCRegister, 8> &PhysRefs,
                          PhysDefVector &PhysDefs, bool &NonLocal) const;
    bool isCSECandidate(MachineInstr *MI);
    bool isProfitableToCSE(Register CSReg, Register Reg,
                           MachineBasicBlock *CSBB, MachineInstr *MI);
    void EnterScope(MachineBasicBlock *MBB);
    void ExitScope(MachineBasicBlock *MBB);
    bool ProcessBlockCSE(MachineBasicBlock *MBB);
    void ExitScopeIfDone(MachineDomTreeNode *Node,
                         DenseMap<MachineDomTreeNode*, unsigned> &OpenChildren);
    bool PerformCSE(MachineDomTreeNode *Node);

    bool isPRECandidate(MachineInstr *MI, SmallSet<MCRegister, 8> &PhysRefs);
    bool ProcessBlockPRE(MachineDominatorTree *MDT, MachineBasicBlock *MBB);
    bool PerformSimplePRE(MachineDominatorTree *DT);
    /// Heuristics to see if it's profitable to move common computations of MBB
    /// and MBB1 to CandidateBB.
    bool isProfitableToHoistInto(MachineBasicBlock *CandidateBB,
                                 MachineBasicBlock *MBB,
                                 MachineBasicBlock *MBB1);
  };

} // end anonymous namespace

char MachineCSE::ID = 0;

char &llvm::MachineCSEID = MachineCSE::ID;

INITIALIZE_PASS_BEGIN(MachineCSE, DEBUG_TYPE,
                      "Machine Common Subexpression Elimination", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MachineCSE, DEBUG_TYPE,
                    "Machine Common Subexpression Elimination", false, false)

/// The source register of a COPY machine instruction can be propagated to all
/// its users, and this propagation could increase the probability of finding
/// common subexpressions. If the COPY has only one user, the COPY itself can
/// be removed.
bool MachineCSE::PerformTrivialCopyPropagation(MachineInstr *MI,
                                               MachineBasicBlock *MBB) {
  bool Changed = false;
  for (MachineOperand &MO : MI->all_uses()) {
    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;
    bool OnlyOneUse = MRI->hasOneNonDBGUse(Reg);
    MachineInstr *DefMI = MRI->getVRegDef(Reg);
    if (!DefMI || !DefMI->isCopy())
      continue;
    Register SrcReg = DefMI->getOperand(1).getReg();
    if (!SrcReg.isVirtual())
      continue;
    if (DefMI->getOperand(0).getSubReg())
      continue;
    // FIXME: We should trivially coalesce subregister copies to expose CSE
    // opportunities on instructions with truncated operands (see
    // cse-add-with-overflow.ll). This can be done here as follows:
    // if (SrcSubReg)
    //  RC = TRI->getMatchingSuperRegClass(MRI->getRegClass(SrcReg), RC,
    //                                     SrcSubReg);
    // MO.substVirtReg(SrcReg, SrcSubReg, *TRI);
    //
    // The 2-addr pass has been updated to handle coalesced subregs. However,
    // some machine-specific code still can't handle it.
    // To handle it properly we also need a way find a constrained subregister
    // class given a super-reg class and subreg index.
    if (DefMI->getOperand(1).getSubReg())
      continue;
    if (!MRI->constrainRegAttrs(SrcReg, Reg))
      continue;
    LLVM_DEBUG(dbgs() << "Coalescing: " << *DefMI);
    LLVM_DEBUG(dbgs() << "***     to: " << *MI);

    // Propagate SrcReg of copies to MI.
    MO.setReg(SrcReg);
    MRI->clearKillFlags(SrcReg);
    // Coalesce single use copies.
    if (OnlyOneUse) {
      // If (and only if) we've eliminated all uses of the copy, also
      // copy-propagate to any debug-users of MI, or they'll be left using
      // an undefined value.
      DefMI->changeDebugValuesDefReg(SrcReg);

      DefMI->eraseFromParent();
      ++NumCoalesces;
    }
    Changed = true;
  }

  return Changed;
}

bool MachineCSE::isPhysDefTriviallyDead(
    MCRegister Reg, MachineBasicBlock::const_iterator I,
    MachineBasicBlock::const_iterator E) const {
  unsigned LookAheadLeft = LookAheadLimit;
  while (LookAheadLeft) {
    // Skip over dbg_value's.
    I = skipDebugInstructionsForward(I, E);

    if (I == E)
      // Reached end of block, we don't know if register is dead or not.
      return false;

    bool SeenDef = false;
    for (const MachineOperand &MO : I->operands()) {
      if (MO.isRegMask() && MO.clobbersPhysReg(Reg))
        SeenDef = true;
      if (!MO.isReg() || !MO.getReg())
        continue;
      if (!TRI->regsOverlap(MO.getReg(), Reg))
        continue;
      if (MO.isUse())
        // Found a use!
        return false;
      SeenDef = true;
    }
    if (SeenDef)
      // See a def of Reg (or an alias) before encountering any use, it's
      // trivially dead.
      return true;

    --LookAheadLeft;
    ++I;
  }
  return false;
}

static bool isCallerPreservedOrConstPhysReg(MCRegister Reg,
                                            const MachineOperand &MO,
                                            const MachineFunction &MF,
                                            const TargetRegisterInfo &TRI,
                                            const TargetInstrInfo &TII) {
  // MachineRegisterInfo::isConstantPhysReg directly called by
  // MachineRegisterInfo::isCallerPreservedOrConstPhysReg expects the
  // reserved registers to be frozen. That doesn't cause a problem  post-ISel as
  // most (if not all) targets freeze reserved registers right after ISel.
  //
  // It does cause issues mid-GlobalISel, however, hence the additional
  // reservedRegsFrozen check.
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  return TRI.isCallerPreservedPhysReg(Reg, MF) || TII.isIgnorableUse(MO) ||
         (MRI.reservedRegsFrozen() && MRI.isConstantPhysReg(Reg));
}

/// hasLivePhysRegDefUses - Return true if the specified instruction read/write
/// physical registers (except for dead defs of physical registers). It also
/// returns the physical register def by reference if it's the only one and the
/// instruction does not uses a physical register.
bool MachineCSE::hasLivePhysRegDefUses(const MachineInstr *MI,
                                       const MachineBasicBlock *MBB,
                                       SmallSet<MCRegister, 8> &PhysRefs,
                                       PhysDefVector &PhysDefs,
                                       bool &PhysUseDef) const {
  // First, add all uses to PhysRefs.
  for (const MachineOperand &MO : MI->all_uses()) {
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (Reg.isVirtual())
      continue;
    // Reading either caller preserved or constant physregs is ok.
    if (!isCallerPreservedOrConstPhysReg(Reg.asMCReg(), MO, *MI->getMF(), *TRI,
                                         *TII))
      for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
        PhysRefs.insert(*AI);
  }

  // Next, collect all defs into PhysDefs.  If any is already in PhysRefs
  // (which currently contains only uses), set the PhysUseDef flag.
  PhysUseDef = false;
  MachineBasicBlock::const_iterator I = MI; I = std::next(I);
  for (const auto &MOP : llvm::enumerate(MI->operands())) {
    const MachineOperand &MO = MOP.value();
    if (!MO.isReg() || !MO.isDef())
      continue;
    Register Reg = MO.getReg();
    if (!Reg)
      continue;
    if (Reg.isVirtual())
      continue;
    // Check against PhysRefs even if the def is "dead".
    if (PhysRefs.count(Reg.asMCReg()))
      PhysUseDef = true;
    // If the def is dead, it's ok. But the def may not marked "dead". That's
    // common since this pass is run before livevariables. We can scan
    // forward a few instructions and check if it is obviously dead.
    if (!MO.isDead() && !isPhysDefTriviallyDead(Reg.asMCReg(), I, MBB->end()))
      PhysDefs.push_back(std::make_pair(MOP.index(), Reg));
  }

  // Finally, add all defs to PhysRefs as well.
  for (unsigned i = 0, e = PhysDefs.size(); i != e; ++i)
    for (MCRegAliasIterator AI(PhysDefs[i].second, TRI, true); AI.isValid();
         ++AI)
      PhysRefs.insert(*AI);

  return !PhysRefs.empty();
}

bool MachineCSE::PhysRegDefsReach(MachineInstr *CSMI, MachineInstr *MI,
                                  SmallSet<MCRegister, 8> &PhysRefs,
                                  PhysDefVector &PhysDefs,
                                  bool &NonLocal) const {
  // For now conservatively returns false if the common subexpression is
  // not in the same basic block as the given instruction. The only exception
  // is if the common subexpression is in the sole predecessor block.
  const MachineBasicBlock *MBB = MI->getParent();
  const MachineBasicBlock *CSMBB = CSMI->getParent();

  bool CrossMBB = false;
  if (CSMBB != MBB) {
    if (MBB->pred_size() != 1 || *MBB->pred_begin() != CSMBB)
      return false;

    for (unsigned i = 0, e = PhysDefs.size(); i != e; ++i) {
      if (MRI->isAllocatable(PhysDefs[i].second) ||
          MRI->isReserved(PhysDefs[i].second))
        // Avoid extending live range of physical registers if they are
        //allocatable or reserved.
        return false;
    }
    CrossMBB = true;
  }
  MachineBasicBlock::const_iterator I = CSMI; I = std::next(I);
  MachineBasicBlock::const_iterator E = MI;
  MachineBasicBlock::const_iterator EE = CSMBB->end();
  unsigned LookAheadLeft = LookAheadLimit;
  while (LookAheadLeft) {
    // Skip over dbg_value's.
    while (I != E && I != EE && I->isDebugInstr())
      ++I;

    if (I == EE) {
      assert(CrossMBB && "Reaching end-of-MBB without finding MI?");
      (void)CrossMBB;
      CrossMBB = false;
      NonLocal = true;
      I = MBB->begin();
      EE = MBB->end();
      continue;
    }

    if (I == E)
      return true;

    for (const MachineOperand &MO : I->operands()) {
      // RegMasks go on instructions like calls that clobber lots of physregs.
      // Don't attempt to CSE across such an instruction.
      if (MO.isRegMask())
        return false;
      if (!MO.isReg() || !MO.isDef())
        continue;
      Register MOReg = MO.getReg();
      if (MOReg.isVirtual())
        continue;
      if (PhysRefs.count(MOReg.asMCReg()))
        return false;
    }

    --LookAheadLeft;
    ++I;
  }

  return false;
}

bool MachineCSE::isCSECandidate(MachineInstr *MI) {
  if (MI->isPosition() || MI->isPHI() || MI->isImplicitDef() || MI->isKill() ||
      MI->isInlineAsm() || MI->isDebugInstr() || MI->isJumpTableDebugInfo())
    return false;

  // Ignore copies.
  if (MI->isCopyLike())
    return false;

  // Ignore stuff that we obviously can't move.
  if (MI->mayStore() || MI->isCall() || MI->isTerminator() ||
      MI->mayRaiseFPException() || MI->hasUnmodeledSideEffects())
    return false;

  if (MI->mayLoad()) {
    // Okay, this instruction does a load. As a refinement, we allow the target
    // to decide whether the loaded value is actually a constant. If so, we can
    // actually use it as a load.
    if (!MI->isDereferenceableInvariantLoad())
      // FIXME: we should be able to hoist loads with no other side effects if
      // there are no other instructions which can change memory in this loop.
      // This is a trivial form of alias analysis.
      return false;
  }

  // Ignore stack guard loads, otherwise the register that holds CSEed value may
  // be spilled and get loaded back with corrupted data.
  if (MI->getOpcode() == TargetOpcode::LOAD_STACK_GUARD)
    return false;

  return true;
}

/// isProfitableToCSE - Return true if it's profitable to eliminate MI with a
/// common expression that defines Reg. CSBB is basic block where CSReg is
/// defined.
bool MachineCSE::isProfitableToCSE(Register CSReg, Register Reg,
                                   MachineBasicBlock *CSBB, MachineInstr *MI) {
  if (AggressiveMachineCSE)
    return true;

  // FIXME: Heuristics that works around the lack the live range splitting.

  // If CSReg is used at all uses of Reg, CSE should not increase register
  // pressure of CSReg.
  bool MayIncreasePressure = true;
  if (CSReg.isVirtual() && Reg.isVirtual()) {
    MayIncreasePressure = false;
    SmallPtrSet<MachineInstr*, 8> CSUses;
    int NumOfUses = 0;
    for (MachineInstr &MI : MRI->use_nodbg_instructions(CSReg)) {
      CSUses.insert(&MI);
      // Too costly to compute if NumOfUses is very large. Conservatively assume
      // MayIncreasePressure to avoid spending too much time here.
      if (++NumOfUses > CSUsesThreshold) {
        MayIncreasePressure = true;
        break;
      }
    }
    if (!MayIncreasePressure)
      for (MachineInstr &MI : MRI->use_nodbg_instructions(Reg)) {
        if (!CSUses.count(&MI)) {
          MayIncreasePressure = true;
          break;
        }
      }
  }
  if (!MayIncreasePressure) return true;

  // Heuristics #1: Don't CSE "cheap" computation if the def is not local or in
  // an immediate predecessor. We don't want to increase register pressure and
  // end up causing other computation to be spilled.
  if (TII->isAsCheapAsAMove(*MI)) {
    MachineBasicBlock *BB = MI->getParent();
    if (CSBB != BB && !CSBB->isSuccessor(BB))
      return false;
  }

  // Heuristics #2: If the expression doesn't not use a vr and the only use
  // of the redundant computation are copies, do not cse.
  bool HasVRegUse = false;
  for (const MachineOperand &MO : MI->all_uses()) {
    if (MO.getReg().isVirtual()) {
      HasVRegUse = true;
      break;
    }
  }
  if (!HasVRegUse) {
    bool HasNonCopyUse = false;
    for (MachineInstr &MI : MRI->use_nodbg_instructions(Reg)) {
      // Ignore copies.
      if (!MI.isCopyLike()) {
        HasNonCopyUse = true;
        break;
      }
    }
    if (!HasNonCopyUse)
      return false;
  }

  // Heuristics #3: If the common subexpression is used by PHIs, do not reuse
  // it unless the defined value is already used in the BB of the new use.
  bool HasPHI = false;
  for (MachineInstr &UseMI : MRI->use_nodbg_instructions(CSReg)) {
    HasPHI |= UseMI.isPHI();
    if (UseMI.getParent() == MI->getParent())
      return true;
  }

  return !HasPHI;
}

void MachineCSE::EnterScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Entering: " << MBB->getName() << '\n');
  ScopeType *Scope = new ScopeType(VNT);
  ScopeMap[MBB] = Scope;
}

void MachineCSE::ExitScope(MachineBasicBlock *MBB) {
  LLVM_DEBUG(dbgs() << "Exiting: " << MBB->getName() << '\n');
  DenseMap<MachineBasicBlock*, ScopeType*>::iterator SI = ScopeMap.find(MBB);
  assert(SI != ScopeMap.end());
  delete SI->second;
  ScopeMap.erase(SI);
}

bool MachineCSE::ProcessBlockCSE(MachineBasicBlock *MBB) {
  bool Changed = false;

  SmallVector<std::pair<unsigned, unsigned>, 8> CSEPairs;
  SmallVector<unsigned, 2> ImplicitDefsToUpdate;
  SmallVector<unsigned, 2> ImplicitDefs;
  for (MachineInstr &MI : llvm::make_early_inc_range(*MBB)) {
    if (!isCSECandidate(&MI))
      continue;

    bool FoundCSE = VNT.count(&MI);
    if (!FoundCSE) {
      // Using trivial copy propagation to find more CSE opportunities.
      if (PerformTrivialCopyPropagation(&MI, MBB)) {
        Changed = true;

        // After coalescing MI itself may become a copy.
        if (MI.isCopyLike())
          continue;

        // Try again to see if CSE is possible.
        FoundCSE = VNT.count(&MI);
      }
    }

    // Commute commutable instructions.
    bool Commuted = false;
    if (!FoundCSE && MI.isCommutable()) {
      if (MachineInstr *NewMI = TII->commuteInstruction(MI)) {
        Commuted = true;
        FoundCSE = VNT.count(NewMI);
        if (NewMI != &MI) {
          // New instruction. It doesn't need to be kept.
          NewMI->eraseFromParent();
          Changed = true;
        } else if (!FoundCSE)
          // MI was changed but it didn't help, commute it back!
          (void)TII->commuteInstruction(MI);
      }
    }

    // If the instruction defines physical registers and the values *may* be
    // used, then it's not safe to replace it with a common subexpression.
    // It's also not safe if the instruction uses physical registers.
    bool CrossMBBPhysDef = false;
    SmallSet<MCRegister, 8> PhysRefs;
    PhysDefVector PhysDefs;
    bool PhysUseDef = false;
    if (FoundCSE &&
        hasLivePhysRegDefUses(&MI, MBB, PhysRefs, PhysDefs, PhysUseDef)) {
      FoundCSE = false;

      // ... Unless the CS is local or is in the sole predecessor block
      // and it also defines the physical register which is not clobbered
      // in between and the physical register uses were not clobbered.
      // This can never be the case if the instruction both uses and
      // defines the same physical register, which was detected above.
      if (!PhysUseDef) {
        unsigned CSVN = VNT.lookup(&MI);
        MachineInstr *CSMI = Exps[CSVN];
        if (PhysRegDefsReach(CSMI, &MI, PhysRefs, PhysDefs, CrossMBBPhysDef))
          FoundCSE = true;
      }
    }

    if (!FoundCSE) {
      VNT.insert(&MI, CurrVN++);
      Exps.push_back(&MI);
      continue;
    }

    // Found a common subexpression, eliminate it.
    unsigned CSVN = VNT.lookup(&MI);
    MachineInstr *CSMI = Exps[CSVN];
    LLVM_DEBUG(dbgs() << "Examining: " << MI);
    LLVM_DEBUG(dbgs() << "*** Found a common subexpression: " << *CSMI);

    // Prevent CSE-ing non-local convergent instructions.
    // LLVM's current definition of `isConvergent` does not necessarily prove
    // that non-local CSE is illegal. The following check extends the definition
    // of `isConvergent` to assume a convergent instruction is dependent not
    // only on additional conditions, but also on fewer conditions. LLVM does
    // not have a MachineInstr attribute which expresses this extended
    // definition, so it's necessary to use `isConvergent` to prevent illegally
    // CSE-ing the subset of `isConvergent` instructions which do fall into this
    // extended definition.
    if (MI.isConvergent() && MI.getParent() != CSMI->getParent()) {
      LLVM_DEBUG(dbgs() << "*** Convergent MI and subexpression exist in "
                           "different BBs, avoid CSE!\n");
      VNT.insert(&MI, CurrVN++);
      Exps.push_back(&MI);
      continue;
    }

    // Check if it's profitable to perform this CSE.
    bool DoCSE = true;
    unsigned NumDefs = MI.getNumDefs();

    for (unsigned i = 0, e = MI.getNumOperands(); NumDefs && i != e; ++i) {
      MachineOperand &MO = MI.getOperand(i);
      if (!MO.isReg() || !MO.isDef())
        continue;
      Register OldReg = MO.getReg();
      Register NewReg = CSMI->getOperand(i).getReg();

      // Go through implicit defs of CSMI and MI, if a def is not dead at MI,
      // we should make sure it is not dead at CSMI.
      if (MO.isImplicit() && !MO.isDead() && CSMI->getOperand(i).isDead())
        ImplicitDefsToUpdate.push_back(i);

      // Keep track of implicit defs of CSMI and MI, to clear possibly
      // made-redundant kill flags.
      if (MO.isImplicit() && !MO.isDead() && OldReg == NewReg)
        ImplicitDefs.push_back(OldReg);

      if (OldReg == NewReg) {
        --NumDefs;
        continue;
      }

      assert(OldReg.isVirtual() && NewReg.isVirtual() &&
             "Do not CSE physical register defs!");

      if (!isProfitableToCSE(NewReg, OldReg, CSMI->getParent(), &MI)) {
        LLVM_DEBUG(dbgs() << "*** Not profitable, avoid CSE!\n");
        DoCSE = false;
        break;
      }

      // Don't perform CSE if the result of the new instruction cannot exist
      // within the constraints (register class, bank, or low-level type) of
      // the old instruction.
      if (!MRI->constrainRegAttrs(NewReg, OldReg)) {
        LLVM_DEBUG(
            dbgs() << "*** Not the same register constraints, avoid CSE!\n");
        DoCSE = false;
        break;
      }

      CSEPairs.push_back(std::make_pair(OldReg, NewReg));
      --NumDefs;
    }

    // Actually perform the elimination.
    if (DoCSE) {
      for (const std::pair<unsigned, unsigned> &CSEPair : CSEPairs) {
        unsigned OldReg = CSEPair.first;
        unsigned NewReg = CSEPair.second;
        // OldReg may have been unused but is used now, clear the Dead flag
        MachineInstr *Def = MRI->getUniqueVRegDef(NewReg);
        assert(Def != nullptr && "CSEd register has no unique definition?");
        Def->clearRegisterDeads(NewReg);
        // Replace with NewReg and clear kill flags which may be wrong now.
        MRI->replaceRegWith(OldReg, NewReg);
        MRI->clearKillFlags(NewReg);
      }

      // Go through implicit defs of CSMI and MI, if a def is not dead at MI,
      // we should make sure it is not dead at CSMI.
      for (unsigned ImplicitDefToUpdate : ImplicitDefsToUpdate)
        CSMI->getOperand(ImplicitDefToUpdate).setIsDead(false);
      for (const auto &PhysDef : PhysDefs)
        if (!MI.getOperand(PhysDef.first).isDead())
          CSMI->getOperand(PhysDef.first).setIsDead(false);

      // Go through implicit defs of CSMI and MI, and clear the kill flags on
      // their uses in all the instructions between CSMI and MI.
      // We might have made some of the kill flags redundant, consider:
      //   subs  ... implicit-def %nzcv    <- CSMI
      //   csinc ... implicit killed %nzcv <- this kill flag isn't valid anymore
      //   subs  ... implicit-def %nzcv    <- MI, to be eliminated
      //   csinc ... implicit killed %nzcv
      // Since we eliminated MI, and reused a register imp-def'd by CSMI
      // (here %nzcv), that register, if it was killed before MI, should have
      // that kill flag removed, because it's lifetime was extended.
      if (CSMI->getParent() == MI.getParent()) {
        for (MachineBasicBlock::iterator II = CSMI, IE = &MI; II != IE; ++II)
          for (auto ImplicitDef : ImplicitDefs)
            if (MachineOperand *MO = II->findRegisterUseOperand(
                    ImplicitDef, TRI, /*isKill=*/true))
              MO->setIsKill(false);
      } else {
        // If the instructions aren't in the same BB, bail out and clear the
        // kill flag on all uses of the imp-def'd register.
        for (auto ImplicitDef : ImplicitDefs)
          MRI->clearKillFlags(ImplicitDef);
      }

      if (CrossMBBPhysDef) {
        // Add physical register defs now coming in from a predecessor to MBB
        // livein list.
        while (!PhysDefs.empty()) {
          auto LiveIn = PhysDefs.pop_back_val();
          if (!MBB->isLiveIn(LiveIn.second))
            MBB->addLiveIn(LiveIn.second);
        }
        ++NumCrossBBCSEs;
      }

      MI.eraseFromParent();
      ++NumCSEs;
      if (!PhysRefs.empty())
        ++NumPhysCSEs;
      if (Commuted)
        ++NumCommutes;
      Changed = true;
    } else {
      VNT.insert(&MI, CurrVN++);
      Exps.push_back(&MI);
    }
    CSEPairs.clear();
    ImplicitDefsToUpdate.clear();
    ImplicitDefs.clear();
  }

  return Changed;
}

/// ExitScopeIfDone - Destroy scope for the MBB that corresponds to the given
/// dominator tree node if its a leaf or all of its children are done. Walk
/// up the dominator tree to destroy ancestors which are now done.
void
MachineCSE::ExitScopeIfDone(MachineDomTreeNode *Node,
                        DenseMap<MachineDomTreeNode*, unsigned> &OpenChildren) {
  if (OpenChildren[Node])
    return;

  // Pop scope.
  ExitScope(Node->getBlock());

  // Now traverse upwards to pop ancestors whose offsprings are all done.
  while (MachineDomTreeNode *Parent = Node->getIDom()) {
    unsigned Left = --OpenChildren[Parent];
    if (Left != 0)
      break;
    ExitScope(Parent->getBlock());
    Node = Parent;
  }
}

bool MachineCSE::PerformCSE(MachineDomTreeNode *Node) {
  SmallVector<MachineDomTreeNode*, 32> Scopes;
  SmallVector<MachineDomTreeNode*, 8> WorkList;
  DenseMap<MachineDomTreeNode*, unsigned> OpenChildren;

  CurrVN = 0;

  // Perform a DFS walk to determine the order of visit.
  WorkList.push_back(Node);
  do {
    Node = WorkList.pop_back_val();
    Scopes.push_back(Node);
    OpenChildren[Node] = Node->getNumChildren();
    append_range(WorkList, Node->children());
  } while (!WorkList.empty());

  // Now perform CSE.
  bool Changed = false;
  for (MachineDomTreeNode *Node : Scopes) {
    MachineBasicBlock *MBB = Node->getBlock();
    EnterScope(MBB);
    Changed |= ProcessBlockCSE(MBB);
    // If it's a leaf node, it's done. Traverse upwards to pop ancestors.
    ExitScopeIfDone(Node, OpenChildren);
  }

  return Changed;
}

// We use stronger checks for PRE candidate rather than for CSE ones to embrace
// checks inside ProcessBlockCSE(), not only inside isCSECandidate(). This helps
// to exclude instrs created by PRE that won't be CSEed later.
bool MachineCSE::isPRECandidate(MachineInstr *MI,
                                SmallSet<MCRegister, 8> &PhysRefs) {
  if (!isCSECandidate(MI) ||
      MI->isNotDuplicable() ||
      MI->mayLoad() ||
      TII->isAsCheapAsAMove(*MI) ||
      MI->getNumDefs() != 1 ||
      MI->getNumExplicitDefs() != 1)
    return false;

  for (const MachineOperand &MO : MI->operands()) {
    if (MO.isReg() && !MO.getReg().isVirtual()) {
      if (MO.isDef())
        return false;
      else
        PhysRefs.insert(MO.getReg());
    }
  }

  return true;
}

bool MachineCSE::ProcessBlockPRE(MachineDominatorTree *DT,
                                 MachineBasicBlock *MBB) {
  bool Changed = false;
  for (MachineInstr &MI : llvm::make_early_inc_range(*MBB)) {
    SmallSet<MCRegister, 8> PhysRefs;
    if (!isPRECandidate(&MI, PhysRefs))
      continue;

    if (!PREMap.count(&MI)) {
      PREMap[&MI] = MBB;
      continue;
    }

    auto MBB1 = PREMap[&MI];
    assert(
        !DT->properlyDominates(MBB, MBB1) &&
        "MBB cannot properly dominate MBB1 while DFS through dominators tree!");
    auto CMBB = DT->findNearestCommonDominator(MBB, MBB1);
    if (!CMBB->isLegalToHoistInto())
      continue;

    if (!isProfitableToHoistInto(CMBB, MBB, MBB1))
      continue;

    // Two instrs are partial redundant if their basic blocks are reachable
    // from one to another but one doesn't dominate another.
    if (CMBB != MBB1) {
      auto BB = MBB->getBasicBlock(), BB1 = MBB1->getBasicBlock();
      if (BB != nullptr && BB1 != nullptr &&
          (isPotentiallyReachable(BB1, BB) ||
           isPotentiallyReachable(BB, BB1))) {
        // The following check extends the definition of `isConvergent` to
        // assume a convergent instruction is dependent not only on additional
        // conditions, but also on fewer conditions. LLVM does not have a
        // MachineInstr attribute which expresses this extended definition, so
        // it's necessary to use `isConvergent` to prevent illegally PRE-ing the
        // subset of `isConvergent` instructions which do fall into this
        // extended definition.
        if (MI.isConvergent() && CMBB != MBB)
          continue;

        // If this instruction uses physical registers then we can only do PRE
        // if it's using the value that is live at the place we're hoisting to.
        bool NonLocal;
        PhysDefVector PhysDefs;
        if (!PhysRefs.empty() &&
            !PhysRegDefsReach(&*(CMBB->getFirstTerminator()), &MI, PhysRefs,
                              PhysDefs, NonLocal))
          continue;

        assert(MI.getOperand(0).isDef() &&
               "First operand of instr with one explicit def must be this def");
        Register VReg = MI.getOperand(0).getReg();
        Register NewReg = MRI->cloneVirtualRegister(VReg);
        if (!isProfitableToCSE(NewReg, VReg, CMBB, &MI))
          continue;
        MachineInstr &NewMI =
            TII->duplicate(*CMBB, CMBB->getFirstTerminator(), MI);

        // When hoisting, make sure we don't carry the debug location of
        // the original instruction, as that's not correct and can cause
        // unexpected jumps when debugging optimized code.
        auto EmptyDL = DebugLoc();
        NewMI.setDebugLoc(EmptyDL);

        NewMI.getOperand(0).setReg(NewReg);

        PREMap[&MI] = CMBB;
        ++NumPREs;
        Changed = true;
      }
    }
  }
  return Changed;
}

// This simple PRE (partial redundancy elimination) pass doesn't actually
// eliminate partial redundancy but transforms it to full redundancy,
// anticipating that the next CSE step will eliminate this created redundancy.
// If CSE doesn't eliminate this, than created instruction will remain dead
// and eliminated later by Remove Dead Machine Instructions pass.
bool MachineCSE::PerformSimplePRE(MachineDominatorTree *DT) {
  SmallVector<MachineDomTreeNode *, 32> BBs;

  PREMap.clear();
  bool Changed = false;
  BBs.push_back(DT->getRootNode());
  do {
    auto Node = BBs.pop_back_val();
    append_range(BBs, Node->children());

    MachineBasicBlock *MBB = Node->getBlock();
    Changed |= ProcessBlockPRE(DT, MBB);

  } while (!BBs.empty());

  return Changed;
}

bool MachineCSE::isProfitableToHoistInto(MachineBasicBlock *CandidateBB,
                                         MachineBasicBlock *MBB,
                                         MachineBasicBlock *MBB1) {
  if (CandidateBB->getParent()->getFunction().hasMinSize())
    return true;
  assert(DT->dominates(CandidateBB, MBB) && "CandidateBB should dominate MBB");
  assert(DT->dominates(CandidateBB, MBB1) &&
         "CandidateBB should dominate MBB1");
  return MBFI->getBlockFreq(CandidateBB) <=
         MBFI->getBlockFreq(MBB) + MBFI->getBlockFreq(MBB1);
}

bool MachineCSE::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
  DT = &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  MBFI = &getAnalysis<MachineBlockFrequencyInfoWrapperPass>().getMBFI();
  LookAheadLimit = TII->getMachineCSELookAheadLimit();
  bool ChangedPRE, ChangedCSE;
  ChangedPRE = PerformSimplePRE(DT);
  ChangedCSE = PerformCSE(DT->getRootNode());
  return ChangedPRE || ChangedCSE;
}
