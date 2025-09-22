//===---- ReachingDefAnalysis.cpp - Reaching Def Analysis ---*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ReachingDefAnalysis.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "reaching-deps-analysis"

char ReachingDefAnalysis::ID = 0;
INITIALIZE_PASS(ReachingDefAnalysis, DEBUG_TYPE, "ReachingDefAnalysis", false,
                true)

static bool isValidReg(const MachineOperand &MO) {
  return MO.isReg() && MO.getReg();
}

static bool isValidRegUse(const MachineOperand &MO) {
  return isValidReg(MO) && MO.isUse();
}

static bool isValidRegUseOf(const MachineOperand &MO, MCRegister PhysReg,
                            const TargetRegisterInfo *TRI) {
  if (!isValidRegUse(MO))
    return false;
  return TRI->regsOverlap(MO.getReg(), PhysReg);
}

static bool isValidRegDef(const MachineOperand &MO) {
  return isValidReg(MO) && MO.isDef();
}

static bool isValidRegDefOf(const MachineOperand &MO, MCRegister PhysReg,
                            const TargetRegisterInfo *TRI) {
  if (!isValidRegDef(MO))
    return false;
  return TRI->regsOverlap(MO.getReg(), PhysReg);
}

void ReachingDefAnalysis::enterBasicBlock(MachineBasicBlock *MBB) {
  unsigned MBBNumber = MBB->getNumber();
  assert(MBBNumber < MBBReachingDefs.size() &&
         "Unexpected basic block number.");
  MBBReachingDefs[MBBNumber].resize(NumRegUnits);

  // Reset instruction counter in each basic block.
  CurInstr = 0;

  // Set up LiveRegs to represent registers entering MBB.
  // Default values are 'nothing happened a long time ago'.
  if (LiveRegs.empty())
    LiveRegs.assign(NumRegUnits, ReachingDefDefaultVal);

  // This is the entry block.
  if (MBB->pred_empty()) {
    for (const auto &LI : MBB->liveins()) {
      for (MCRegUnit Unit : TRI->regunits(LI.PhysReg)) {
        // Treat function live-ins as if they were defined just before the first
        // instruction.  Usually, function arguments are set up immediately
        // before the call.
        if (LiveRegs[Unit] != -1) {
          LiveRegs[Unit] = -1;
          MBBReachingDefs[MBBNumber][Unit].push_back(-1);
        }
      }
    }
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << ": entry\n");
    return;
  }

  // Try to coalesce live-out registers from predecessors.
  for (MachineBasicBlock *pred : MBB->predecessors()) {
    assert(unsigned(pred->getNumber()) < MBBOutRegsInfos.size() &&
           "Should have pre-allocated MBBInfos for all MBBs");
    const LiveRegsDefInfo &Incoming = MBBOutRegsInfos[pred->getNumber()];
    // Incoming is null if this is a backedge from a BB
    // we haven't processed yet
    if (Incoming.empty())
      continue;

    // Find the most recent reaching definition from a predecessor.
    for (unsigned Unit = 0; Unit != NumRegUnits; ++Unit)
      LiveRegs[Unit] = std::max(LiveRegs[Unit], Incoming[Unit]);
  }

  // Insert the most recent reaching definition we found.
  for (unsigned Unit = 0; Unit != NumRegUnits; ++Unit)
    if (LiveRegs[Unit] != ReachingDefDefaultVal)
      MBBReachingDefs[MBBNumber][Unit].push_back(LiveRegs[Unit]);
}

void ReachingDefAnalysis::leaveBasicBlock(MachineBasicBlock *MBB) {
  assert(!LiveRegs.empty() && "Must enter basic block first.");
  unsigned MBBNumber = MBB->getNumber();
  assert(MBBNumber < MBBOutRegsInfos.size() &&
         "Unexpected basic block number.");
  // Save register clearances at end of MBB - used by enterBasicBlock().
  MBBOutRegsInfos[MBBNumber] = LiveRegs;

  // While processing the basic block, we kept `Def` relative to the start
  // of the basic block for convenience. However, future use of this information
  // only cares about the clearance from the end of the block, so adjust
  // everything to be relative to the end of the basic block.
  for (int &OutLiveReg : MBBOutRegsInfos[MBBNumber])
    if (OutLiveReg != ReachingDefDefaultVal)
      OutLiveReg -= CurInstr;
  LiveRegs.clear();
}

void ReachingDefAnalysis::processDefs(MachineInstr *MI) {
  assert(!MI->isDebugInstr() && "Won't process debug instructions");

  unsigned MBBNumber = MI->getParent()->getNumber();
  assert(MBBNumber < MBBReachingDefs.size() &&
         "Unexpected basic block number.");

  for (auto &MO : MI->operands()) {
    if (!isValidRegDef(MO))
      continue;
    for (MCRegUnit Unit : TRI->regunits(MO.getReg().asMCReg())) {
      // This instruction explicitly defines the current reg unit.
      LLVM_DEBUG(dbgs() << printRegUnit(Unit, TRI) << ":\t" << CurInstr << '\t'
                        << *MI);

      // How many instructions since this reg unit was last written?
      if (LiveRegs[Unit] != CurInstr) {
        LiveRegs[Unit] = CurInstr;
        MBBReachingDefs[MBBNumber][Unit].push_back(CurInstr);
      }
    }
  }
  InstIds[MI] = CurInstr;
  ++CurInstr;
}

void ReachingDefAnalysis::reprocessBasicBlock(MachineBasicBlock *MBB) {
  unsigned MBBNumber = MBB->getNumber();
  assert(MBBNumber < MBBReachingDefs.size() &&
         "Unexpected basic block number.");

  // Count number of non-debug instructions for end of block adjustment.
  auto NonDbgInsts =
    instructionsWithoutDebug(MBB->instr_begin(), MBB->instr_end());
  int NumInsts = std::distance(NonDbgInsts.begin(), NonDbgInsts.end());

  // When reprocessing a block, the only thing we need to do is check whether
  // there is now a more recent incoming reaching definition from a predecessor.
  for (MachineBasicBlock *pred : MBB->predecessors()) {
    assert(unsigned(pred->getNumber()) < MBBOutRegsInfos.size() &&
           "Should have pre-allocated MBBInfos for all MBBs");
    const LiveRegsDefInfo &Incoming = MBBOutRegsInfos[pred->getNumber()];
    // Incoming may be empty for dead predecessors.
    if (Incoming.empty())
      continue;

    for (unsigned Unit = 0; Unit != NumRegUnits; ++Unit) {
      int Def = Incoming[Unit];
      if (Def == ReachingDefDefaultVal)
        continue;

      auto Start = MBBReachingDefs[MBBNumber][Unit].begin();
      if (Start != MBBReachingDefs[MBBNumber][Unit].end() && *Start < 0) {
        if (*Start >= Def)
          continue;

        // Update existing reaching def from predecessor to a more recent one.
        *Start = Def;
      } else {
        // Insert new reaching def from predecessor.
        MBBReachingDefs[MBBNumber][Unit].insert(Start, Def);
      }

      // Update reaching def at end of BB. Keep in mind that these are
      // adjusted relative to the end of the basic block.
      if (MBBOutRegsInfos[MBBNumber][Unit] < Def - NumInsts)
        MBBOutRegsInfos[MBBNumber][Unit] = Def - NumInsts;
    }
  }
}

void ReachingDefAnalysis::processBasicBlock(
    const LoopTraversal::TraversedMBBInfo &TraversedMBB) {
  MachineBasicBlock *MBB = TraversedMBB.MBB;
  LLVM_DEBUG(dbgs() << printMBBReference(*MBB)
                    << (!TraversedMBB.IsDone ? ": incomplete\n"
                                             : ": all preds known\n"));

  if (!TraversedMBB.PrimaryPass) {
    // Reprocess MBB that is part of a loop.
    reprocessBasicBlock(MBB);
    return;
  }

  enterBasicBlock(MBB);
  for (MachineInstr &MI :
       instructionsWithoutDebug(MBB->instr_begin(), MBB->instr_end()))
    processDefs(&MI);
  leaveBasicBlock(MBB);
}

bool ReachingDefAnalysis::runOnMachineFunction(MachineFunction &mf) {
  MF = &mf;
  TRI = MF->getSubtarget().getRegisterInfo();
  LLVM_DEBUG(dbgs() << "********** REACHING DEFINITION ANALYSIS **********\n");
  init();
  traverse();
  return false;
}

void ReachingDefAnalysis::releaseMemory() {
  // Clear the internal vectors.
  MBBOutRegsInfos.clear();
  MBBReachingDefs.clear();
  InstIds.clear();
  LiveRegs.clear();
}

void ReachingDefAnalysis::reset() {
  releaseMemory();
  init();
  traverse();
}

void ReachingDefAnalysis::init() {
  NumRegUnits = TRI->getNumRegUnits();
  MBBReachingDefs.resize(MF->getNumBlockIDs());
  // Initialize the MBBOutRegsInfos
  MBBOutRegsInfos.resize(MF->getNumBlockIDs());
  LoopTraversal Traversal;
  TraversedMBBOrder = Traversal.traverse(*MF);
}

void ReachingDefAnalysis::traverse() {
  // Traverse the basic blocks.
  for (LoopTraversal::TraversedMBBInfo TraversedMBB : TraversedMBBOrder)
    processBasicBlock(TraversedMBB);
#ifndef NDEBUG
  // Make sure reaching defs are sorted and unique.
  for (MBBDefsInfo &MBBDefs : MBBReachingDefs) {
    for (MBBRegUnitDefs &RegUnitDefs : MBBDefs) {
      int LastDef = ReachingDefDefaultVal;
      for (int Def : RegUnitDefs) {
        assert(Def > LastDef && "Defs must be sorted and unique");
        LastDef = Def;
      }
    }
  }
#endif
}

int ReachingDefAnalysis::getReachingDef(MachineInstr *MI,
                                        MCRegister PhysReg) const {
  assert(InstIds.count(MI) && "Unexpected machine instuction.");
  int InstId = InstIds.lookup(MI);
  int DefRes = ReachingDefDefaultVal;
  unsigned MBBNumber = MI->getParent()->getNumber();
  assert(MBBNumber < MBBReachingDefs.size() &&
         "Unexpected basic block number.");
  int LatestDef = ReachingDefDefaultVal;
  for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
    for (int Def : MBBReachingDefs[MBBNumber][Unit]) {
      if (Def >= InstId)
        break;
      DefRes = Def;
    }
    LatestDef = std::max(LatestDef, DefRes);
  }
  return LatestDef;
}

MachineInstr *
ReachingDefAnalysis::getReachingLocalMIDef(MachineInstr *MI,
                                           MCRegister PhysReg) const {
  return hasLocalDefBefore(MI, PhysReg)
    ? getInstFromId(MI->getParent(), getReachingDef(MI, PhysReg))
    : nullptr;
}

bool ReachingDefAnalysis::hasSameReachingDef(MachineInstr *A, MachineInstr *B,
                                             MCRegister PhysReg) const {
  MachineBasicBlock *ParentA = A->getParent();
  MachineBasicBlock *ParentB = B->getParent();
  if (ParentA != ParentB)
    return false;

  return getReachingDef(A, PhysReg) == getReachingDef(B, PhysReg);
}

MachineInstr *ReachingDefAnalysis::getInstFromId(MachineBasicBlock *MBB,
                                                 int InstId) const {
  assert(static_cast<size_t>(MBB->getNumber()) < MBBReachingDefs.size() &&
         "Unexpected basic block number.");
  assert(InstId < static_cast<int>(MBB->size()) &&
         "Unexpected instruction id.");

  if (InstId < 0)
    return nullptr;

  for (auto &MI : *MBB) {
    auto F = InstIds.find(&MI);
    if (F != InstIds.end() && F->second == InstId)
      return &MI;
  }

  return nullptr;
}

int ReachingDefAnalysis::getClearance(MachineInstr *MI,
                                      MCRegister PhysReg) const {
  assert(InstIds.count(MI) && "Unexpected machine instuction.");
  return InstIds.lookup(MI) - getReachingDef(MI, PhysReg);
}

bool ReachingDefAnalysis::hasLocalDefBefore(MachineInstr *MI,
                                            MCRegister PhysReg) const {
  return getReachingDef(MI, PhysReg) >= 0;
}

void ReachingDefAnalysis::getReachingLocalUses(MachineInstr *Def,
                                               MCRegister PhysReg,
                                               InstSet &Uses) const {
  MachineBasicBlock *MBB = Def->getParent();
  MachineBasicBlock::iterator MI = MachineBasicBlock::iterator(Def);
  while (++MI != MBB->end()) {
    if (MI->isDebugInstr())
      continue;

    // If/when we find a new reaching def, we know that there's no more uses
    // of 'Def'.
    if (getReachingLocalMIDef(&*MI, PhysReg) != Def)
      return;

    for (auto &MO : MI->operands()) {
      if (!isValidRegUseOf(MO, PhysReg, TRI))
        continue;

      Uses.insert(&*MI);
      if (MO.isKill())
        return;
    }
  }
}

bool ReachingDefAnalysis::getLiveInUses(MachineBasicBlock *MBB,
                                        MCRegister PhysReg,
                                        InstSet &Uses) const {
  for (MachineInstr &MI :
       instructionsWithoutDebug(MBB->instr_begin(), MBB->instr_end())) {
    for (auto &MO : MI.operands()) {
      if (!isValidRegUseOf(MO, PhysReg, TRI))
        continue;
      if (getReachingDef(&MI, PhysReg) >= 0)
        return false;
      Uses.insert(&MI);
    }
  }
  auto Last = MBB->getLastNonDebugInstr();
  if (Last == MBB->end())
    return true;
  return isReachingDefLiveOut(&*Last, PhysReg);
}

void ReachingDefAnalysis::getGlobalUses(MachineInstr *MI, MCRegister PhysReg,
                                        InstSet &Uses) const {
  MachineBasicBlock *MBB = MI->getParent();

  // Collect the uses that each def touches within the block.
  getReachingLocalUses(MI, PhysReg, Uses);

  // Handle live-out values.
  if (auto *LiveOut = getLocalLiveOutMIDef(MI->getParent(), PhysReg)) {
    if (LiveOut != MI)
      return;

    SmallVector<MachineBasicBlock *, 4> ToVisit(MBB->successors());
    SmallPtrSet<MachineBasicBlock*, 4>Visited;
    while (!ToVisit.empty()) {
      MachineBasicBlock *MBB = ToVisit.pop_back_val();
      if (Visited.count(MBB) || !MBB->isLiveIn(PhysReg))
        continue;
      if (getLiveInUses(MBB, PhysReg, Uses))
        llvm::append_range(ToVisit, MBB->successors());
      Visited.insert(MBB);
    }
  }
}

void ReachingDefAnalysis::getGlobalReachingDefs(MachineInstr *MI,
                                                MCRegister PhysReg,
                                                InstSet &Defs) const {
  if (auto *Def = getUniqueReachingMIDef(MI, PhysReg)) {
    Defs.insert(Def);
    return;
  }

  for (auto *MBB : MI->getParent()->predecessors())
    getLiveOuts(MBB, PhysReg, Defs);
}

void ReachingDefAnalysis::getLiveOuts(MachineBasicBlock *MBB,
                                      MCRegister PhysReg, InstSet &Defs) const {
  SmallPtrSet<MachineBasicBlock*, 2> VisitedBBs;
  getLiveOuts(MBB, PhysReg, Defs, VisitedBBs);
}

void ReachingDefAnalysis::getLiveOuts(MachineBasicBlock *MBB,
                                      MCRegister PhysReg, InstSet &Defs,
                                      BlockSet &VisitedBBs) const {
  if (VisitedBBs.count(MBB))
    return;

  VisitedBBs.insert(MBB);
  LiveRegUnits LiveRegs(*TRI);
  LiveRegs.addLiveOuts(*MBB);
  if (LiveRegs.available(PhysReg))
    return;

  if (auto *Def = getLocalLiveOutMIDef(MBB, PhysReg))
    Defs.insert(Def);
  else
    for (auto *Pred : MBB->predecessors())
      getLiveOuts(Pred, PhysReg, Defs, VisitedBBs);
}

MachineInstr *
ReachingDefAnalysis::getUniqueReachingMIDef(MachineInstr *MI,
                                            MCRegister PhysReg) const {
  // If there's a local def before MI, return it.
  MachineInstr *LocalDef = getReachingLocalMIDef(MI, PhysReg);
  if (LocalDef && InstIds.lookup(LocalDef) < InstIds.lookup(MI))
    return LocalDef;

  SmallPtrSet<MachineInstr*, 2> Incoming;
  MachineBasicBlock *Parent = MI->getParent();
  for (auto *Pred : Parent->predecessors())
    getLiveOuts(Pred, PhysReg, Incoming);

  // Check that we have a single incoming value and that it does not
  // come from the same block as MI - since it would mean that the def
  // is executed after MI.
  if (Incoming.size() == 1 && (*Incoming.begin())->getParent() != Parent)
    return *Incoming.begin();
  return nullptr;
}

MachineInstr *ReachingDefAnalysis::getMIOperand(MachineInstr *MI,
                                                unsigned Idx) const {
  assert(MI->getOperand(Idx).isReg() && "Expected register operand");
  return getUniqueReachingMIDef(MI, MI->getOperand(Idx).getReg());
}

MachineInstr *ReachingDefAnalysis::getMIOperand(MachineInstr *MI,
                                                MachineOperand &MO) const {
  assert(MO.isReg() && "Expected register operand");
  return getUniqueReachingMIDef(MI, MO.getReg());
}

bool ReachingDefAnalysis::isRegUsedAfter(MachineInstr *MI,
                                         MCRegister PhysReg) const {
  MachineBasicBlock *MBB = MI->getParent();
  LiveRegUnits LiveRegs(*TRI);
  LiveRegs.addLiveOuts(*MBB);

  // Yes if the register is live out of the basic block.
  if (!LiveRegs.available(PhysReg))
    return true;

  // Walk backwards through the block to see if the register is live at some
  // point.
  for (MachineInstr &Last :
       instructionsWithoutDebug(MBB->instr_rbegin(), MBB->instr_rend())) {
    LiveRegs.stepBackward(Last);
    if (!LiveRegs.available(PhysReg))
      return InstIds.lookup(&Last) > InstIds.lookup(MI);
  }
  return false;
}

bool ReachingDefAnalysis::isRegDefinedAfter(MachineInstr *MI,
                                            MCRegister PhysReg) const {
  MachineBasicBlock *MBB = MI->getParent();
  auto Last = MBB->getLastNonDebugInstr();
  if (Last != MBB->end() &&
      getReachingDef(MI, PhysReg) != getReachingDef(&*Last, PhysReg))
    return true;

  if (auto *Def = getLocalLiveOutMIDef(MBB, PhysReg))
    return Def == getReachingLocalMIDef(MI, PhysReg);

  return false;
}

bool ReachingDefAnalysis::isReachingDefLiveOut(MachineInstr *MI,
                                               MCRegister PhysReg) const {
  MachineBasicBlock *MBB = MI->getParent();
  LiveRegUnits LiveRegs(*TRI);
  LiveRegs.addLiveOuts(*MBB);
  if (LiveRegs.available(PhysReg))
    return false;

  auto Last = MBB->getLastNonDebugInstr();
  int Def = getReachingDef(MI, PhysReg);
  if (Last != MBB->end() && getReachingDef(&*Last, PhysReg) != Def)
    return false;

  // Finally check that the last instruction doesn't redefine the register.
  for (auto &MO : Last->operands())
    if (isValidRegDefOf(MO, PhysReg, TRI))
      return false;

  return true;
}

MachineInstr *
ReachingDefAnalysis::getLocalLiveOutMIDef(MachineBasicBlock *MBB,
                                          MCRegister PhysReg) const {
  LiveRegUnits LiveRegs(*TRI);
  LiveRegs.addLiveOuts(*MBB);
  if (LiveRegs.available(PhysReg))
    return nullptr;

  auto Last = MBB->getLastNonDebugInstr();
  if (Last == MBB->end())
    return nullptr;

  int Def = getReachingDef(&*Last, PhysReg);
  for (auto &MO : Last->operands())
    if (isValidRegDefOf(MO, PhysReg, TRI))
      return &*Last;

  return Def < 0 ? nullptr : getInstFromId(MBB, Def);
}

static bool mayHaveSideEffects(MachineInstr &MI) {
  return MI.mayLoadOrStore() || MI.mayRaiseFPException() ||
         MI.hasUnmodeledSideEffects() || MI.isTerminator() ||
         MI.isCall() || MI.isBarrier() || MI.isBranch() || MI.isReturn();
}

// Can we safely move 'From' to just before 'To'? To satisfy this, 'From' must
// not define a register that is used by any instructions, after and including,
// 'To'. These instructions also must not redefine any of Froms operands.
template<typename Iterator>
bool ReachingDefAnalysis::isSafeToMove(MachineInstr *From,
                                       MachineInstr *To) const {
  if (From->getParent() != To->getParent() || From == To)
    return false;

  SmallSet<int, 2> Defs;
  // First check that From would compute the same value if moved.
  for (auto &MO : From->operands()) {
    if (!isValidReg(MO))
      continue;
    if (MO.isDef())
      Defs.insert(MO.getReg());
    else if (!hasSameReachingDef(From, To, MO.getReg()))
      return false;
  }

  // Now walk checking that the rest of the instructions will compute the same
  // value and that we're not overwriting anything. Don't move the instruction
  // past any memory, control-flow or other ambiguous instructions.
  for (auto I = ++Iterator(From), E = Iterator(To); I != E; ++I) {
    if (mayHaveSideEffects(*I))
      return false;
    for (auto &MO : I->operands())
      if (MO.isReg() && MO.getReg() && Defs.count(MO.getReg()))
        return false;
  }
  return true;
}

bool ReachingDefAnalysis::isSafeToMoveForwards(MachineInstr *From,
                                               MachineInstr *To) const {
  using Iterator = MachineBasicBlock::iterator;
  // Walk forwards until we find the instruction.
  for (auto I = Iterator(From), E = From->getParent()->end(); I != E; ++I)
    if (&*I == To)
      return isSafeToMove<Iterator>(From, To);
  return false;
}

bool ReachingDefAnalysis::isSafeToMoveBackwards(MachineInstr *From,
                                                MachineInstr *To) const {
  using Iterator = MachineBasicBlock::reverse_iterator;
  // Walk backwards until we find the instruction.
  for (auto I = Iterator(From), E = From->getParent()->rend(); I != E; ++I)
    if (&*I == To)
      return isSafeToMove<Iterator>(From, To);
  return false;
}

bool ReachingDefAnalysis::isSafeToRemove(MachineInstr *MI,
                                         InstSet &ToRemove) const {
  SmallPtrSet<MachineInstr*, 1> Ignore;
  SmallPtrSet<MachineInstr*, 2> Visited;
  return isSafeToRemove(MI, Visited, ToRemove, Ignore);
}

bool
ReachingDefAnalysis::isSafeToRemove(MachineInstr *MI, InstSet &ToRemove,
                                    InstSet &Ignore) const {
  SmallPtrSet<MachineInstr*, 2> Visited;
  return isSafeToRemove(MI, Visited, ToRemove, Ignore);
}

bool
ReachingDefAnalysis::isSafeToRemove(MachineInstr *MI, InstSet &Visited,
                                    InstSet &ToRemove, InstSet &Ignore) const {
  if (Visited.count(MI) || Ignore.count(MI))
    return true;
  else if (mayHaveSideEffects(*MI)) {
    // Unless told to ignore the instruction, don't remove anything which has
    // side effects.
    return false;
  }

  Visited.insert(MI);
  for (auto &MO : MI->operands()) {
    if (!isValidRegDef(MO))
      continue;

    SmallPtrSet<MachineInstr*, 4> Uses;
    getGlobalUses(MI, MO.getReg(), Uses);

    for (auto *I : Uses) {
      if (Ignore.count(I) || ToRemove.count(I))
        continue;
      if (!isSafeToRemove(I, Visited, ToRemove, Ignore))
        return false;
    }
  }
  ToRemove.insert(MI);
  return true;
}

void ReachingDefAnalysis::collectKilledOperands(MachineInstr *MI,
                                                InstSet &Dead) const {
  Dead.insert(MI);
  auto IsDead = [this, &Dead](MachineInstr *Def, MCRegister PhysReg) {
    if (mayHaveSideEffects(*Def))
      return false;

    unsigned LiveDefs = 0;
    for (auto &MO : Def->operands()) {
      if (!isValidRegDef(MO))
        continue;
      if (!MO.isDead())
        ++LiveDefs;
    }

    if (LiveDefs > 1)
      return false;

    SmallPtrSet<MachineInstr*, 4> Uses;
    getGlobalUses(Def, PhysReg, Uses);
    return llvm::set_is_subset(Uses, Dead);
  };

  for (auto &MO : MI->operands()) {
    if (!isValidRegUse(MO))
      continue;
    if (MachineInstr *Def = getMIOperand(MI, MO))
      if (IsDead(Def, MO.getReg()))
        collectKilledOperands(Def, Dead);
  }
}

bool ReachingDefAnalysis::isSafeToDefRegAt(MachineInstr *MI,
                                           MCRegister PhysReg) const {
  SmallPtrSet<MachineInstr*, 1> Ignore;
  return isSafeToDefRegAt(MI, PhysReg, Ignore);
}

bool ReachingDefAnalysis::isSafeToDefRegAt(MachineInstr *MI, MCRegister PhysReg,
                                           InstSet &Ignore) const {
  // Check for any uses of the register after MI.
  if (isRegUsedAfter(MI, PhysReg)) {
    if (auto *Def = getReachingLocalMIDef(MI, PhysReg)) {
      SmallPtrSet<MachineInstr*, 2> Uses;
      getGlobalUses(Def, PhysReg, Uses);
      if (!llvm::set_is_subset(Uses, Ignore))
        return false;
    } else
      return false;
  }

  MachineBasicBlock *MBB = MI->getParent();
  // Check for any defs after MI.
  if (isRegDefinedAfter(MI, PhysReg)) {
    auto I = MachineBasicBlock::iterator(MI);
    for (auto E = MBB->end(); I != E; ++I) {
      if (Ignore.count(&*I))
        continue;
      for (auto &MO : I->operands())
        if (isValidRegDefOf(MO, PhysReg, TRI))
          return false;
    }
  }
  return true;
}
