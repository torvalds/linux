//===-- LiveVariables.cpp - Live Variable Analysis for Machine Code -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LiveVariable analysis pass.  For each machine
// instruction in the function, this pass calculates the set of registers that
// are immediately dead after the instruction (i.e., the instruction calculates
// the value, but it is never used) and the set of registers that are used by
// the instruction, but are never used after the instruction (i.e., they are
// killed).
//
// This class computes live variables using a sparse implementation based on
// the machine code SSA form.  This class computes live variable information for
// each virtual and _register allocatable_ physical register in a function.  It
// uses the dominance properties of SSA form to efficiently compute live
// variables for virtual registers, and assumes that physical registers are only
// live within a single basic block (allowing it to do a single local analysis
// to resolve physical register lifetimes in each basic block).  If a physical
// register is not register allocatable, it is not tracked.  This is useful for
// things like the stack pointer and condition codes.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
using namespace llvm;

AnalysisKey LiveVariablesAnalysis::Key;

LiveVariablesAnalysis::Result
LiveVariablesAnalysis::run(MachineFunction &MF,
                           MachineFunctionAnalysisManager &) {
  return Result(MF);
}

PreservedAnalyses
LiveVariablesPrinterPass::run(MachineFunction &MF,
                              MachineFunctionAnalysisManager &MFAM) {
  OS << "Live variables in machine function: " << MF.getName() << '\n';
  MFAM.getResult<LiveVariablesAnalysis>(MF).print(OS);
  return PreservedAnalyses::all();
}

char LiveVariablesWrapperPass::ID = 0;
char &llvm::LiveVariablesID = LiveVariablesWrapperPass::ID;
INITIALIZE_PASS_BEGIN(LiveVariablesWrapperPass, "livevars",
                      "Live Variable Analysis", false, false)
INITIALIZE_PASS_DEPENDENCY(UnreachableMachineBlockElim)
INITIALIZE_PASS_END(LiveVariablesWrapperPass, "livevars",
                    "Live Variable Analysis", false, false)

void LiveVariablesWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequiredID(UnreachableMachineBlockElimID);
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

LiveVariables::LiveVariables(MachineFunction &MF)
    : MF(&MF), MRI(&MF.getRegInfo()), TRI(MF.getSubtarget().getRegisterInfo()) {
  analyze(MF);
}

void LiveVariables::print(raw_ostream &OS) const {
  for (size_t I = 0, E = VirtRegInfo.size(); I != E; ++I) {
    const Register Reg = Register::index2VirtReg(I);
    OS << "Virtual register '%" << I << "':\n";
    VirtRegInfo[Reg].print(OS);
  }
}

MachineInstr *
LiveVariables::VarInfo::findKill(const MachineBasicBlock *MBB) const {
  for (MachineInstr *MI : Kills)
    if (MI->getParent() == MBB)
      return MI;
  return nullptr;
}

void LiveVariables::VarInfo::print(raw_ostream &OS) const {
  OS << "  Alive in blocks: ";
  for (unsigned AB : AliveBlocks)
    OS << AB << ", ";
  OS << "\n  Killed by:";
  if (Kills.empty())
    OS << " No instructions.\n\n";
  else {
    for (unsigned i = 0, e = Kills.size(); i != e; ++i)
      OS << "\n    #" << i << ": " << *Kills[i];
    OS << "\n";
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void LiveVariables::VarInfo::dump() const { print(dbgs()); }
#endif

/// getVarInfo - Get (possibly creating) a VarInfo object for the given vreg.
LiveVariables::VarInfo &LiveVariables::getVarInfo(Register Reg) {
  assert(Reg.isVirtual() && "getVarInfo: not a virtual register!");
  VirtRegInfo.grow(Reg);
  return VirtRegInfo[Reg];
}

void LiveVariables::MarkVirtRegAliveInBlock(
    VarInfo &VRInfo, MachineBasicBlock *DefBlock, MachineBasicBlock *MBB,
    SmallVectorImpl<MachineBasicBlock *> &WorkList) {
  unsigned BBNum = MBB->getNumber();

  // Check to see if this basic block is one of the killing blocks.  If so,
  // remove it.
  for (unsigned i = 0, e = VRInfo.Kills.size(); i != e; ++i)
    if (VRInfo.Kills[i]->getParent() == MBB) {
      VRInfo.Kills.erase(VRInfo.Kills.begin()+i);  // Erase entry
      break;
    }

  if (MBB == DefBlock) return;  // Terminate recursion

  if (VRInfo.AliveBlocks.test(BBNum))
    return;  // We already know the block is live

  // Mark the variable known alive in this bb
  VRInfo.AliveBlocks.set(BBNum);

  assert(MBB != &MF->front() && "Can't find reaching def for virtreg");
  WorkList.insert(WorkList.end(), MBB->pred_rbegin(), MBB->pred_rend());
}

void LiveVariables::MarkVirtRegAliveInBlock(VarInfo &VRInfo,
                                            MachineBasicBlock *DefBlock,
                                            MachineBasicBlock *MBB) {
  SmallVector<MachineBasicBlock *, 16> WorkList;
  MarkVirtRegAliveInBlock(VRInfo, DefBlock, MBB, WorkList);

  while (!WorkList.empty()) {
    MachineBasicBlock *Pred = WorkList.pop_back_val();
    MarkVirtRegAliveInBlock(VRInfo, DefBlock, Pred, WorkList);
  }
}

void LiveVariables::HandleVirtRegUse(Register Reg, MachineBasicBlock *MBB,
                                     MachineInstr &MI) {
  assert(MRI->getVRegDef(Reg) && "Register use before def!");

  unsigned BBNum = MBB->getNumber();

  VarInfo &VRInfo = getVarInfo(Reg);

  // Check to see if this basic block is already a kill block.
  if (!VRInfo.Kills.empty() && VRInfo.Kills.back()->getParent() == MBB) {
    // Yes, this register is killed in this basic block already. Increase the
    // live range by updating the kill instruction.
    VRInfo.Kills.back() = &MI;
    return;
  }

#ifndef NDEBUG
  for (MachineInstr *Kill : VRInfo.Kills)
    assert(Kill->getParent() != MBB && "entry should be at end!");
#endif

  // This situation can occur:
  //
  //     ,------.
  //     |      |
  //     |      v
  //     |   t2 = phi ... t1 ...
  //     |      |
  //     |      v
  //     |   t1 = ...
  //     |  ... = ... t1 ...
  //     |      |
  //     `------'
  //
  // where there is a use in a PHI node that's a predecessor to the defining
  // block. We don't want to mark all predecessors as having the value "alive"
  // in this case.
  if (MBB == MRI->getVRegDef(Reg)->getParent())
    return;

  // Add a new kill entry for this basic block. If this virtual register is
  // already marked as alive in this basic block, that means it is alive in at
  // least one of the successor blocks, it's not a kill.
  if (!VRInfo.AliveBlocks.test(BBNum))
    VRInfo.Kills.push_back(&MI);

  // Update all dominating blocks to mark them as "known live".
  for (MachineBasicBlock *Pred : MBB->predecessors())
    MarkVirtRegAliveInBlock(VRInfo, MRI->getVRegDef(Reg)->getParent(), Pred);
}

void LiveVariables::HandleVirtRegDef(Register Reg, MachineInstr &MI) {
  VarInfo &VRInfo = getVarInfo(Reg);

  if (VRInfo.AliveBlocks.empty())
    // If vr is not alive in any block, then defaults to dead.
    VRInfo.Kills.push_back(&MI);
}

/// FindLastPartialDef - Return the last partial def of the specified register.
/// Also returns the sub-registers that're defined by the instruction.
MachineInstr *
LiveVariables::FindLastPartialDef(Register Reg,
                                  SmallSet<unsigned, 4> &PartDefRegs) {
  unsigned LastDefReg = 0;
  unsigned LastDefDist = 0;
  MachineInstr *LastDef = nullptr;
  for (MCPhysReg SubReg : TRI->subregs(Reg)) {
    MachineInstr *Def = PhysRegDef[SubReg];
    if (!Def)
      continue;
    unsigned Dist = DistanceMap[Def];
    if (Dist > LastDefDist) {
      LastDefReg  = SubReg;
      LastDef     = Def;
      LastDefDist = Dist;
    }
  }

  if (!LastDef)
    return nullptr;

  PartDefRegs.insert(LastDefReg);
  for (MachineOperand &MO : LastDef->all_defs()) {
    if (MO.getReg() == 0)
      continue;
    Register DefReg = MO.getReg();
    if (TRI->isSubRegister(Reg, DefReg)) {
      for (MCPhysReg SubReg : TRI->subregs_inclusive(DefReg))
        PartDefRegs.insert(SubReg);
    }
  }
  return LastDef;
}

/// HandlePhysRegUse - Turn previous partial def's into read/mod/writes. Add
/// implicit defs to a machine instruction if there was an earlier def of its
/// super-register.
void LiveVariables::HandlePhysRegUse(Register Reg, MachineInstr &MI) {
  MachineInstr *LastDef = PhysRegDef[Reg];
  // If there was a previous use or a "full" def all is well.
  if (!LastDef && !PhysRegUse[Reg]) {
    // Otherwise, the last sub-register def implicitly defines this register.
    // e.g.
    // AH =
    // AL = ... implicit-def EAX, implicit killed AH
    //    = AH
    // ...
    //    = EAX
    // All of the sub-registers must have been defined before the use of Reg!
    SmallSet<unsigned, 4> PartDefRegs;
    MachineInstr *LastPartialDef = FindLastPartialDef(Reg, PartDefRegs);
    // If LastPartialDef is NULL, it must be using a livein register.
    if (LastPartialDef) {
      LastPartialDef->addOperand(MachineOperand::CreateReg(Reg, true/*IsDef*/,
                                                           true/*IsImp*/));
      PhysRegDef[Reg] = LastPartialDef;
      SmallSet<unsigned, 8> Processed;
      for (MCPhysReg SubReg : TRI->subregs(Reg)) {
        if (Processed.count(SubReg))
          continue;
        if (PartDefRegs.count(SubReg))
          continue;
        // This part of Reg was defined before the last partial def. It's killed
        // here.
        LastPartialDef->addOperand(MachineOperand::CreateReg(SubReg,
                                                             false/*IsDef*/,
                                                             true/*IsImp*/));
        PhysRegDef[SubReg] = LastPartialDef;
        for (MCPhysReg SS : TRI->subregs(SubReg))
          Processed.insert(SS);
      }
    }
  } else if (LastDef && !PhysRegUse[Reg] &&
             !LastDef->findRegisterDefOperand(Reg, /*TRI=*/nullptr))
    // Last def defines the super register, add an implicit def of reg.
    LastDef->addOperand(MachineOperand::CreateReg(Reg, true/*IsDef*/,
                                                  true/*IsImp*/));

  // Remember this use.
  for (MCPhysReg SubReg : TRI->subregs_inclusive(Reg))
    PhysRegUse[SubReg] = &MI;
}

/// FindLastRefOrPartRef - Return the last reference or partial reference of
/// the specified register.
MachineInstr *LiveVariables::FindLastRefOrPartRef(Register Reg) {
  MachineInstr *LastDef = PhysRegDef[Reg];
  MachineInstr *LastUse = PhysRegUse[Reg];
  if (!LastDef && !LastUse)
    return nullptr;

  MachineInstr *LastRefOrPartRef = LastUse ? LastUse : LastDef;
  unsigned LastRefOrPartRefDist = DistanceMap[LastRefOrPartRef];
  unsigned LastPartDefDist = 0;
  for (MCPhysReg SubReg : TRI->subregs(Reg)) {
    MachineInstr *Def = PhysRegDef[SubReg];
    if (Def && Def != LastDef) {
      // There was a def of this sub-register in between. This is a partial
      // def, keep track of the last one.
      unsigned Dist = DistanceMap[Def];
      if (Dist > LastPartDefDist)
        LastPartDefDist = Dist;
    } else if (MachineInstr *Use = PhysRegUse[SubReg]) {
      unsigned Dist = DistanceMap[Use];
      if (Dist > LastRefOrPartRefDist) {
        LastRefOrPartRefDist = Dist;
        LastRefOrPartRef = Use;
      }
    }
  }

  return LastRefOrPartRef;
}

bool LiveVariables::HandlePhysRegKill(Register Reg, MachineInstr *MI) {
  MachineInstr *LastDef = PhysRegDef[Reg];
  MachineInstr *LastUse = PhysRegUse[Reg];
  if (!LastDef && !LastUse)
    return false;

  MachineInstr *LastRefOrPartRef = LastUse ? LastUse : LastDef;
  unsigned LastRefOrPartRefDist = DistanceMap[LastRefOrPartRef];
  // The whole register is used.
  // AL =
  // AH =
  //
  //    = AX
  //    = AL, implicit killed AX
  // AX =
  //
  // Or whole register is defined, but not used at all.
  // dead AX =
  // ...
  // AX =
  //
  // Or whole register is defined, but only partly used.
  // dead AX = implicit-def AL
  //    = killed AL
  // AX =
  MachineInstr *LastPartDef = nullptr;
  unsigned LastPartDefDist = 0;
  SmallSet<unsigned, 8> PartUses;
  for (MCPhysReg SubReg : TRI->subregs(Reg)) {
    MachineInstr *Def = PhysRegDef[SubReg];
    if (Def && Def != LastDef) {
      // There was a def of this sub-register in between. This is a partial
      // def, keep track of the last one.
      unsigned Dist = DistanceMap[Def];
      if (Dist > LastPartDefDist) {
        LastPartDefDist = Dist;
        LastPartDef = Def;
      }
      continue;
    }
    if (MachineInstr *Use = PhysRegUse[SubReg]) {
      for (MCPhysReg SS : TRI->subregs_inclusive(SubReg))
        PartUses.insert(SS);
      unsigned Dist = DistanceMap[Use];
      if (Dist > LastRefOrPartRefDist) {
        LastRefOrPartRefDist = Dist;
        LastRefOrPartRef = Use;
      }
    }
  }

  if (!PhysRegUse[Reg]) {
    // Partial uses. Mark register def dead and add implicit def of
    // sub-registers which are used.
    // dead EAX  = op  implicit-def AL
    // That is, EAX def is dead but AL def extends pass it.
    PhysRegDef[Reg]->addRegisterDead(Reg, TRI, true);
    for (MCPhysReg SubReg : TRI->subregs(Reg)) {
      if (!PartUses.count(SubReg))
        continue;
      bool NeedDef = true;
      if (PhysRegDef[Reg] == PhysRegDef[SubReg]) {
        MachineOperand *MO =
            PhysRegDef[Reg]->findRegisterDefOperand(SubReg, /*TRI=*/nullptr);
        if (MO) {
          NeedDef = false;
          assert(!MO->isDead());
        }
      }
      if (NeedDef)
        PhysRegDef[Reg]->addOperand(MachineOperand::CreateReg(SubReg,
                                                 true/*IsDef*/, true/*IsImp*/));
      MachineInstr *LastSubRef = FindLastRefOrPartRef(SubReg);
      if (LastSubRef)
        LastSubRef->addRegisterKilled(SubReg, TRI, true);
      else {
        LastRefOrPartRef->addRegisterKilled(SubReg, TRI, true);
        for (MCPhysReg SS : TRI->subregs_inclusive(SubReg))
          PhysRegUse[SS] = LastRefOrPartRef;
      }
      for (MCPhysReg SS : TRI->subregs(SubReg))
        PartUses.erase(SS);
    }
  } else if (LastRefOrPartRef == PhysRegDef[Reg] && LastRefOrPartRef != MI) {
    if (LastPartDef)
      // The last partial def kills the register.
      LastPartDef->addOperand(MachineOperand::CreateReg(Reg, false/*IsDef*/,
                                                true/*IsImp*/, true/*IsKill*/));
    else {
      MachineOperand *MO =
          LastRefOrPartRef->findRegisterDefOperand(Reg, TRI, false, false);
      bool NeedEC = MO->isEarlyClobber() && MO->getReg() != Reg;
      // If the last reference is the last def, then it's not used at all.
      // That is, unless we are currently processing the last reference itself.
      LastRefOrPartRef->addRegisterDead(Reg, TRI, true);
      if (NeedEC) {
        // If we are adding a subreg def and the superreg def is marked early
        // clobber, add an early clobber marker to the subreg def.
        MO = LastRefOrPartRef->findRegisterDefOperand(Reg, /*TRI=*/nullptr);
        if (MO)
          MO->setIsEarlyClobber();
      }
    }
  } else
    LastRefOrPartRef->addRegisterKilled(Reg, TRI, true);
  return true;
}

void LiveVariables::HandleRegMask(const MachineOperand &MO, unsigned NumRegs) {
  // Call HandlePhysRegKill() for all live registers clobbered by Mask.
  // Clobbered registers are always dead, sp there is no need to use
  // HandlePhysRegDef().
  for (unsigned Reg = 1; Reg != NumRegs; ++Reg) {
    // Skip dead regs.
    if (!PhysRegDef[Reg] && !PhysRegUse[Reg])
      continue;
    // Skip mask-preserved regs.
    if (!MO.clobbersPhysReg(Reg))
      continue;
    // Kill the largest clobbered super-register.
    // This avoids needless implicit operands.
    unsigned Super = Reg;
    for (MCPhysReg SR : TRI->superregs(Reg))
      if (SR < NumRegs && (PhysRegDef[SR] || PhysRegUse[SR]) &&
          MO.clobbersPhysReg(SR))
        Super = SR;
    HandlePhysRegKill(Super, nullptr);
  }
}

void LiveVariables::HandlePhysRegDef(Register Reg, MachineInstr *MI,
                                     SmallVectorImpl<unsigned> &Defs) {
  // What parts of the register are previously defined?
  SmallSet<unsigned, 32> Live;
  if (PhysRegDef[Reg] || PhysRegUse[Reg]) {
    for (MCPhysReg SubReg : TRI->subregs_inclusive(Reg))
      Live.insert(SubReg);
  } else {
    for (MCPhysReg SubReg : TRI->subregs(Reg)) {
      // If a register isn't itself defined, but all parts that make up of it
      // are defined, then consider it also defined.
      // e.g.
      // AL =
      // AH =
      //    = AX
      if (Live.count(SubReg))
        continue;
      if (PhysRegDef[SubReg] || PhysRegUse[SubReg]) {
        for (MCPhysReg SS : TRI->subregs_inclusive(SubReg))
          Live.insert(SS);
      }
    }
  }

  // Start from the largest piece, find the last time any part of the register
  // is referenced.
  HandlePhysRegKill(Reg, MI);
  // Only some of the sub-registers are used.
  for (MCPhysReg SubReg : TRI->subregs(Reg)) {
    if (!Live.count(SubReg))
      // Skip if this sub-register isn't defined.
      continue;
    HandlePhysRegKill(SubReg, MI);
  }

  if (MI)
    Defs.push_back(Reg);  // Remember this def.
}

void LiveVariables::UpdatePhysRegDefs(MachineInstr &MI,
                                      SmallVectorImpl<unsigned> &Defs) {
  while (!Defs.empty()) {
    Register Reg = Defs.pop_back_val();
    for (MCPhysReg SubReg : TRI->subregs_inclusive(Reg)) {
      PhysRegDef[SubReg] = &MI;
      PhysRegUse[SubReg]  = nullptr;
    }
  }
}

void LiveVariables::runOnInstr(MachineInstr &MI,
                               SmallVectorImpl<unsigned> &Defs,
                               unsigned NumRegs) {
  assert(!MI.isDebugOrPseudoInstr());
  // Process all of the operands of the instruction...
  unsigned NumOperandsToProcess = MI.getNumOperands();

  // Unless it is a PHI node.  In this case, ONLY process the DEF, not any
  // of the uses.  They will be handled in other basic blocks.
  if (MI.isPHI())
    NumOperandsToProcess = 1;

  // Clear kill and dead markers. LV will recompute them.
  SmallVector<unsigned, 4> UseRegs;
  SmallVector<unsigned, 4> DefRegs;
  SmallVector<unsigned, 1> RegMasks;
  for (unsigned i = 0; i != NumOperandsToProcess; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (MO.isRegMask()) {
      RegMasks.push_back(i);
      continue;
    }
    if (!MO.isReg() || MO.getReg() == 0)
      continue;
    Register MOReg = MO.getReg();
    if (MO.isUse()) {
      if (!(MOReg.isPhysical() && MRI->isReserved(MOReg)))
        MO.setIsKill(false);
      if (MO.readsReg())
        UseRegs.push_back(MOReg);
    } else {
      assert(MO.isDef());
      // FIXME: We should not remove any dead flags. However the MIPS RDDSP
      // instruction needs it at the moment: http://llvm.org/PR27116.
      if (MOReg.isPhysical() && !MRI->isReserved(MOReg))
        MO.setIsDead(false);
      DefRegs.push_back(MOReg);
    }
  }

  MachineBasicBlock *MBB = MI.getParent();
  // Process all uses.
  for (unsigned MOReg : UseRegs) {
    if (Register::isVirtualRegister(MOReg))
      HandleVirtRegUse(MOReg, MBB, MI);
    else if (!MRI->isReserved(MOReg))
      HandlePhysRegUse(MOReg, MI);
  }

  // Process all masked registers. (Call clobbers).
  for (unsigned Mask : RegMasks)
    HandleRegMask(MI.getOperand(Mask), NumRegs);

  // Process all defs.
  for (unsigned MOReg : DefRegs) {
    if (Register::isVirtualRegister(MOReg))
      HandleVirtRegDef(MOReg, MI);
    else if (!MRI->isReserved(MOReg))
      HandlePhysRegDef(MOReg, &MI, Defs);
  }
  UpdatePhysRegDefs(MI, Defs);
}

void LiveVariables::runOnBlock(MachineBasicBlock *MBB, unsigned NumRegs) {
  // Mark live-in registers as live-in.
  SmallVector<unsigned, 4> Defs;
  for (const auto &LI : MBB->liveins()) {
    assert(Register::isPhysicalRegister(LI.PhysReg) &&
           "Cannot have a live-in virtual register!");
    HandlePhysRegDef(LI.PhysReg, nullptr, Defs);
  }

  // Loop over all of the instructions, processing them.
  DistanceMap.clear();
  unsigned Dist = 0;
  for (MachineInstr &MI : *MBB) {
    if (MI.isDebugOrPseudoInstr())
      continue;
    DistanceMap.insert(std::make_pair(&MI, Dist++));

    runOnInstr(MI, Defs, NumRegs);
  }

  // Handle any virtual assignments from PHI nodes which might be at the
  // bottom of this basic block.  We check all of our successor blocks to see
  // if they have PHI nodes, and if so, we simulate an assignment at the end
  // of the current block.
  if (!PHIVarInfo[MBB->getNumber()].empty()) {
    SmallVectorImpl<unsigned> &VarInfoVec = PHIVarInfo[MBB->getNumber()];

    for (unsigned I : VarInfoVec)
      // Mark it alive only in the block we are representing.
      MarkVirtRegAliveInBlock(getVarInfo(I), MRI->getVRegDef(I)->getParent(),
                              MBB);
  }

  // MachineCSE may CSE instructions which write to non-allocatable physical
  // registers across MBBs. Remember if any reserved register is liveout.
  SmallSet<unsigned, 4> LiveOuts;
  for (const MachineBasicBlock *SuccMBB : MBB->successors()) {
    if (SuccMBB->isEHPad())
      continue;
    for (const auto &LI : SuccMBB->liveins()) {
      if (!TRI->isInAllocatableClass(LI.PhysReg))
        // Ignore other live-ins, e.g. those that are live into landing pads.
        LiveOuts.insert(LI.PhysReg);
    }
  }

  // Loop over PhysRegDef / PhysRegUse, killing any registers that are
  // available at the end of the basic block.
  for (unsigned i = 0; i != NumRegs; ++i)
    if ((PhysRegDef[i] || PhysRegUse[i]) && !LiveOuts.count(i))
      HandlePhysRegDef(i, nullptr, Defs);
}

void LiveVariables::analyze(MachineFunction &mf) {
  MF = &mf;
  MRI = &mf.getRegInfo();
  TRI = MF->getSubtarget().getRegisterInfo();

  const unsigned NumRegs = TRI->getNumSupportedRegs(mf);
  PhysRegDef.assign(NumRegs, nullptr);
  PhysRegUse.assign(NumRegs, nullptr);
  PHIVarInfo.resize(MF->getNumBlockIDs());

  // FIXME: LiveIntervals will be updated to remove its dependence on
  // LiveVariables to improve compilation time and eliminate bizarre pass
  // dependencies. Until then, we can't change much in -O0.
  if (!MRI->isSSA())
    report_fatal_error("regalloc=... not currently supported with -O0");

  analyzePHINodes(mf);

  // Calculate live variable information in depth first order on the CFG of the
  // function.  This guarantees that we will see the definition of a virtual
  // register before its uses due to dominance properties of SSA (except for PHI
  // nodes, which are treated as a special case).
  MachineBasicBlock *Entry = &MF->front();
  df_iterator_default_set<MachineBasicBlock*,16> Visited;

  for (MachineBasicBlock *MBB : depth_first_ext(Entry, Visited)) {
    runOnBlock(MBB, NumRegs);

    PhysRegDef.assign(NumRegs, nullptr);
    PhysRegUse.assign(NumRegs, nullptr);
  }

  // Convert and transfer the dead / killed information we have gathered into
  // VirtRegInfo onto MI's.
  for (unsigned i = 0, e1 = VirtRegInfo.size(); i != e1; ++i) {
    const Register Reg = Register::index2VirtReg(i);
    for (unsigned j = 0, e2 = VirtRegInfo[Reg].Kills.size(); j != e2; ++j)
      if (VirtRegInfo[Reg].Kills[j] == MRI->getVRegDef(Reg))
        VirtRegInfo[Reg].Kills[j]->addRegisterDead(Reg, TRI);
      else
        VirtRegInfo[Reg].Kills[j]->addRegisterKilled(Reg, TRI);
  }

  // Check to make sure there are no unreachable blocks in the MC CFG for the
  // function.  If so, it is due to a bug in the instruction selector or some
  // other part of the code generator if this happens.
#ifndef NDEBUG
  for (const MachineBasicBlock &MBB : *MF)
    assert(Visited.contains(&MBB) && "unreachable basic block found");
#endif

  PhysRegDef.clear();
  PhysRegUse.clear();
  PHIVarInfo.clear();
}

void LiveVariables::recomputeForSingleDefVirtReg(Register Reg) {
  assert(Reg.isVirtual());

  VarInfo &VI = getVarInfo(Reg);
  VI.AliveBlocks.clear();
  VI.Kills.clear();

  MachineInstr &DefMI = *MRI->getUniqueVRegDef(Reg);
  MachineBasicBlock &DefBB = *DefMI.getParent();

  // Initialize a worklist of BBs that Reg is live-to-end of. (Here
  // "live-to-end" means Reg is live at the end of a block even if it is only
  // live because of phi uses in a successor. This is different from isLiveOut()
  // which does not consider phi uses.)
  SmallVector<MachineBasicBlock *> LiveToEndBlocks;
  SparseBitVector<> UseBlocks;
  unsigned NumRealUses = 0;
  for (auto &UseMO : MRI->use_nodbg_operands(Reg)) {
    UseMO.setIsKill(false);
    if (!UseMO.readsReg())
      continue;
    ++NumRealUses;
    MachineInstr &UseMI = *UseMO.getParent();
    MachineBasicBlock &UseBB = *UseMI.getParent();
    UseBlocks.set(UseBB.getNumber());
    if (UseMI.isPHI()) {
      // If Reg is used in a phi then it is live-to-end of the corresponding
      // predecessor.
      unsigned Idx = UseMO.getOperandNo();
      LiveToEndBlocks.push_back(UseMI.getOperand(Idx + 1).getMBB());
    } else if (&UseBB == &DefBB) {
      // A non-phi use in the same BB as the single def must come after the def.
    } else {
      // Otherwise Reg must be live-to-end of all predecessors.
      LiveToEndBlocks.append(UseBB.pred_begin(), UseBB.pred_end());
    }
  }

  // Handle the case where all uses have been removed.
  if (NumRealUses == 0) {
    VI.Kills.push_back(&DefMI);
    DefMI.addRegisterDead(Reg, nullptr);
    return;
  }
  DefMI.clearRegisterDeads(Reg);

  // Iterate over the worklist adding blocks to AliveBlocks.
  bool LiveToEndOfDefBB = false;
  while (!LiveToEndBlocks.empty()) {
    MachineBasicBlock &BB = *LiveToEndBlocks.pop_back_val();
    if (&BB == &DefBB) {
      LiveToEndOfDefBB = true;
      continue;
    }
    if (VI.AliveBlocks.test(BB.getNumber()))
      continue;
    VI.AliveBlocks.set(BB.getNumber());
    LiveToEndBlocks.append(BB.pred_begin(), BB.pred_end());
  }

  // Recompute kill flags. For each block in which Reg is used but is not
  // live-through, find the last instruction that uses Reg. Ignore phi nodes
  // because they should not be included in Kills.
  for (unsigned UseBBNum : UseBlocks) {
    if (VI.AliveBlocks.test(UseBBNum))
      continue;
    MachineBasicBlock &UseBB = *MF->getBlockNumbered(UseBBNum);
    if (&UseBB == &DefBB && LiveToEndOfDefBB)
      continue;
    for (auto &MI : reverse(UseBB)) {
      if (MI.isDebugOrPseudoInstr())
        continue;
      if (MI.isPHI())
        break;
      if (MI.readsVirtualRegister(Reg)) {
        assert(!MI.killsRegister(Reg, /*TRI=*/nullptr));
        MI.addRegisterKilled(Reg, nullptr);
        VI.Kills.push_back(&MI);
        break;
      }
    }
  }
}

/// replaceKillInstruction - Update register kill info by replacing a kill
/// instruction with a new one.
void LiveVariables::replaceKillInstruction(Register Reg, MachineInstr &OldMI,
                                           MachineInstr &NewMI) {
  VarInfo &VI = getVarInfo(Reg);
  std::replace(VI.Kills.begin(), VI.Kills.end(), &OldMI, &NewMI);
}

/// removeVirtualRegistersKilled - Remove all killed info for the specified
/// instruction.
void LiveVariables::removeVirtualRegistersKilled(MachineInstr &MI) {
  for (MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isKill()) {
      MO.setIsKill(false);
      Register Reg = MO.getReg();
      if (Reg.isVirtual()) {
        bool removed = getVarInfo(Reg).removeKill(MI);
        assert(removed && "kill not in register's VarInfo?");
        (void)removed;
      }
    }
  }
}

/// analyzePHINodes - Gather information about the PHI nodes in here. In
/// particular, we want to map the variable information of a virtual register
/// which is used in a PHI node. We map that to the BB the vreg is coming from.
///
void LiveVariables::analyzePHINodes(const MachineFunction& Fn) {
  for (const auto &MBB : Fn)
    for (const auto &BBI : MBB) {
      if (!BBI.isPHI())
        break;
      for (unsigned i = 1, e = BBI.getNumOperands(); i != e; i += 2)
        if (BBI.getOperand(i).readsReg())
          PHIVarInfo[BBI.getOperand(i + 1).getMBB()->getNumber()]
            .push_back(BBI.getOperand(i).getReg());
    }
}

bool LiveVariables::VarInfo::isLiveIn(const MachineBasicBlock &MBB,
                                      Register Reg, MachineRegisterInfo &MRI) {
  unsigned Num = MBB.getNumber();

  // Reg is live-through.
  if (AliveBlocks.test(Num))
    return true;

  // Registers defined in MBB cannot be live in.
  const MachineInstr *Def = MRI.getVRegDef(Reg);
  if (Def && Def->getParent() == &MBB)
    return false;

 // Reg was not defined in MBB, was it killed here?
  return findKill(&MBB);
}

bool LiveVariables::isLiveOut(Register Reg, const MachineBasicBlock &MBB) {
  LiveVariables::VarInfo &VI = getVarInfo(Reg);

  SmallPtrSet<const MachineBasicBlock *, 8> Kills;
  for (MachineInstr *MI : VI.Kills)
    Kills.insert(MI->getParent());

  // Loop over all of the successors of the basic block, checking to see if
  // the value is either live in the block, or if it is killed in the block.
  for (const MachineBasicBlock *SuccMBB : MBB.successors()) {
    // Is it alive in this successor?
    unsigned SuccIdx = SuccMBB->getNumber();
    if (VI.AliveBlocks.test(SuccIdx))
      return true;
    // Or is it live because there is a use in a successor that kills it?
    if (Kills.count(SuccMBB))
      return true;
  }

  return false;
}

/// addNewBlock - Add a new basic block BB as an empty succcessor to DomBB. All
/// variables that are live out of DomBB will be marked as passing live through
/// BB.
void LiveVariables::addNewBlock(MachineBasicBlock *BB,
                                MachineBasicBlock *DomBB,
                                MachineBasicBlock *SuccBB) {
  const unsigned NumNew = BB->getNumber();

  DenseSet<unsigned> Defs, Kills;

  MachineBasicBlock::iterator BBI = SuccBB->begin(), BBE = SuccBB->end();
  for (; BBI != BBE && BBI->isPHI(); ++BBI) {
    // Record the def of the PHI node.
    Defs.insert(BBI->getOperand(0).getReg());

    // All registers used by PHI nodes in SuccBB must be live through BB.
    for (unsigned i = 1, e = BBI->getNumOperands(); i != e; i += 2)
      if (BBI->getOperand(i+1).getMBB() == BB)
        getVarInfo(BBI->getOperand(i).getReg()).AliveBlocks.set(NumNew);
  }

  // Record all vreg defs and kills of all instructions in SuccBB.
  for (; BBI != BBE; ++BBI) {
    for (const MachineOperand &Op : BBI->operands()) {
      if (Op.isReg() && Op.getReg().isVirtual()) {
        if (Op.isDef())
          Defs.insert(Op.getReg());
        else if (Op.isKill())
          Kills.insert(Op.getReg());
      }
    }
  }

  // Update info for all live variables
  for (unsigned i = 0, e = MRI->getNumVirtRegs(); i != e; ++i) {
    Register Reg = Register::index2VirtReg(i);

    // If the Defs is defined in the successor it can't be live in BB.
    if (Defs.count(Reg))
      continue;

    // If the register is either killed in or live through SuccBB it's also live
    // through BB.
    VarInfo &VI = getVarInfo(Reg);
    if (Kills.count(Reg) || VI.AliveBlocks.test(SuccBB->getNumber()))
      VI.AliveBlocks.set(NumNew);
  }
}

/// addNewBlock - Add a new basic block BB as an empty succcessor to DomBB. All
/// variables that are live out of DomBB will be marked as passing live through
/// BB. LiveInSets[BB] is *not* updated (because it is not needed during
/// PHIElimination).
void LiveVariables::addNewBlock(MachineBasicBlock *BB,
                                MachineBasicBlock *DomBB,
                                MachineBasicBlock *SuccBB,
                                std::vector<SparseBitVector<>> &LiveInSets) {
  const unsigned NumNew = BB->getNumber();

  SparseBitVector<> &BV = LiveInSets[SuccBB->getNumber()];
  for (unsigned R : BV) {
    Register VirtReg = Register::index2VirtReg(R);
    LiveVariables::VarInfo &VI = getVarInfo(VirtReg);
    VI.AliveBlocks.set(NumNew);
  }
  // All registers used by PHI nodes in SuccBB must be live through BB.
  for (MachineBasicBlock::iterator BBI = SuccBB->begin(),
         BBE = SuccBB->end();
       BBI != BBE && BBI->isPHI(); ++BBI) {
    for (unsigned i = 1, e = BBI->getNumOperands(); i != e; i += 2)
      if (BBI->getOperand(i + 1).getMBB() == BB &&
          BBI->getOperand(i).readsReg())
        getVarInfo(BBI->getOperand(i).getReg())
          .AliveBlocks.set(NumNew);
  }
}
