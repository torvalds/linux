//===- RegisterScavenging.cpp - Machine register scavenging ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements the machine register scavenger. It can provide
/// information, such as unused registers, at any point in a machine basic
/// block. It also provides a mechanism to make registers available by evicting
/// them to spill slots.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <limits>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "reg-scavenging"

STATISTIC(NumScavengedRegs, "Number of frame index regs scavenged");

void RegScavenger::setRegUsed(Register Reg, LaneBitmask LaneMask) {
  LiveUnits.addRegMasked(Reg, LaneMask);
}

void RegScavenger::init(MachineBasicBlock &MBB) {
  MachineFunction &MF = *MBB.getParent();
  TII = MF.getSubtarget().getInstrInfo();
  TRI = MF.getSubtarget().getRegisterInfo();
  MRI = &MF.getRegInfo();
  LiveUnits.init(*TRI);

  this->MBB = &MBB;

  for (ScavengedInfo &SI : Scavenged) {
    SI.Reg = 0;
    SI.Restore = nullptr;
  }
}

void RegScavenger::enterBasicBlock(MachineBasicBlock &MBB) {
  init(MBB);
  LiveUnits.addLiveIns(MBB);
  MBBI = MBB.begin();
}

void RegScavenger::enterBasicBlockEnd(MachineBasicBlock &MBB) {
  init(MBB);
  LiveUnits.addLiveOuts(MBB);
  MBBI = MBB.end();
}

void RegScavenger::backward() {
  const MachineInstr &MI = *--MBBI;
  LiveUnits.stepBackward(MI);

  // Expire scavenge spill frameindex uses.
  for (ScavengedInfo &I : Scavenged) {
    if (I.Restore == &MI) {
      I.Reg = 0;
      I.Restore = nullptr;
    }
  }
}

bool RegScavenger::isRegUsed(Register Reg, bool includeReserved) const {
  if (isReserved(Reg))
    return includeReserved;
  return !LiveUnits.available(Reg);
}

Register RegScavenger::FindUnusedReg(const TargetRegisterClass *RC) const {
  for (Register Reg : *RC) {
    if (!isRegUsed(Reg)) {
      LLVM_DEBUG(dbgs() << "Scavenger found unused reg: " << printReg(Reg, TRI)
                        << "\n");
      return Reg;
    }
  }
  return 0;
}

BitVector RegScavenger::getRegsAvailable(const TargetRegisterClass *RC) {
  BitVector Mask(TRI->getNumRegs());
  for (Register Reg : *RC)
    if (!isRegUsed(Reg))
      Mask.set(Reg);
  return Mask;
}

/// Given the bitvector \p Available of free register units at position
/// \p From. Search backwards to find a register that is part of \p
/// Candidates and not used/clobbered until the point \p To. If there is
/// multiple candidates continue searching and pick the one that is not used/
/// clobbered for the longest time.
/// Returns the register and the earliest position we know it to be free or
/// the position MBB.end() if no register is available.
static std::pair<MCPhysReg, MachineBasicBlock::iterator>
findSurvivorBackwards(const MachineRegisterInfo &MRI,
    MachineBasicBlock::iterator From, MachineBasicBlock::iterator To,
    const LiveRegUnits &LiveOut, ArrayRef<MCPhysReg> AllocationOrder,
    bool RestoreAfter) {
  bool FoundTo = false;
  MCPhysReg Survivor = 0;
  MachineBasicBlock::iterator Pos;
  MachineBasicBlock &MBB = *From->getParent();
  unsigned InstrLimit = 25;
  unsigned InstrCountDown = InstrLimit;
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  LiveRegUnits Used(TRI);

  assert(From->getParent() == To->getParent() &&
         "Target instruction is in other than current basic block, use "
         "enterBasicBlockEnd first");

  for (MachineBasicBlock::iterator I = From;; --I) {
    const MachineInstr &MI = *I;

    Used.accumulate(MI);

    if (I == To) {
      // See if one of the registers in RC wasn't used so far.
      for (MCPhysReg Reg : AllocationOrder) {
        if (!MRI.isReserved(Reg) && Used.available(Reg) &&
            LiveOut.available(Reg))
          return std::make_pair(Reg, MBB.end());
      }
      // Otherwise we will continue up to InstrLimit instructions to find
      // the register which is not defined/used for the longest time.
      FoundTo = true;
      Pos = To;
      // Note: It was fine so far to start our search at From, however now that
      // we have to spill, and can only place the restore after From then
      // add the regs used/defed by std::next(From) to the set.
      if (RestoreAfter)
        Used.accumulate(*std::next(From));
    }
    if (FoundTo) {
      // Don't search to FrameSetup instructions if we were searching from
      // Non-FrameSetup instructions. Otherwise, the spill position may point
      // before FrameSetup instructions.
      if (!From->getFlag(MachineInstr::FrameSetup) &&
          MI.getFlag(MachineInstr::FrameSetup))
        break;

      if (Survivor == 0 || !Used.available(Survivor)) {
        MCPhysReg AvilableReg = 0;
        for (MCPhysReg Reg : AllocationOrder) {
          if (!MRI.isReserved(Reg) && Used.available(Reg)) {
            AvilableReg = Reg;
            break;
          }
        }
        if (AvilableReg == 0)
          break;
        Survivor = AvilableReg;
      }
      if (--InstrCountDown == 0)
        break;

      // Keep searching when we find a vreg since the spilled register will
      // be usefull for this other vreg as well later.
      bool FoundVReg = false;
      for (const MachineOperand &MO : MI.operands()) {
        if (MO.isReg() && MO.getReg().isVirtual()) {
          FoundVReg = true;
          break;
        }
      }
      if (FoundVReg) {
        InstrCountDown = InstrLimit;
        Pos = I;
      }
      if (I == MBB.begin())
        break;
    }
    assert(I != MBB.begin() && "Did not find target instruction while "
                               "iterating backwards");
  }

  return std::make_pair(Survivor, Pos);
}

static unsigned getFrameIndexOperandNum(MachineInstr &MI) {
  unsigned i = 0;
  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }
  return i;
}

RegScavenger::ScavengedInfo &
RegScavenger::spill(Register Reg, const TargetRegisterClass &RC, int SPAdj,
                    MachineBasicBlock::iterator Before,
                    MachineBasicBlock::iterator &UseMI) {
  // Find an available scavenging slot with size and alignment matching
  // the requirements of the class RC.
  const MachineFunction &MF = *Before->getMF();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned NeedSize = TRI->getSpillSize(RC);
  Align NeedAlign = TRI->getSpillAlign(RC);

  unsigned SI = Scavenged.size(), Diff = std::numeric_limits<unsigned>::max();
  int FIB = MFI.getObjectIndexBegin(), FIE = MFI.getObjectIndexEnd();
  for (unsigned I = 0; I < Scavenged.size(); ++I) {
    if (Scavenged[I].Reg != 0)
      continue;
    // Verify that this slot is valid for this register.
    int FI = Scavenged[I].FrameIndex;
    if (FI < FIB || FI >= FIE)
      continue;
    unsigned S = MFI.getObjectSize(FI);
    Align A = MFI.getObjectAlign(FI);
    if (NeedSize > S || NeedAlign > A)
      continue;
    // Avoid wasting slots with large size and/or large alignment. Pick one
    // that is the best fit for this register class (in street metric).
    // Picking a larger slot than necessary could happen if a slot for a
    // larger register is reserved before a slot for a smaller one. When
    // trying to spill a smaller register, the large slot would be found
    // first, thus making it impossible to spill the larger register later.
    unsigned D = (S - NeedSize) + (A.value() - NeedAlign.value());
    if (D < Diff) {
      SI = I;
      Diff = D;
    }
  }

  if (SI == Scavenged.size()) {
    // We need to scavenge a register but have no spill slot, the target
    // must know how to do it (if not, we'll assert below).
    Scavenged.push_back(ScavengedInfo(FIE));
  }

  // Avoid infinite regress
  Scavenged[SI].Reg = Reg;

  // If the target knows how to save/restore the register, let it do so;
  // otherwise, use the emergency stack spill slot.
  if (!TRI->saveScavengerRegister(*MBB, Before, UseMI, &RC, Reg)) {
    // Spill the scavenged register before \p Before.
    int FI = Scavenged[SI].FrameIndex;
    if (FI < FIB || FI >= FIE) {
      report_fatal_error(Twine("Error while trying to spill ") +
                         TRI->getName(Reg) + " from class " +
                         TRI->getRegClassName(&RC) +
                         ": Cannot scavenge register without an emergency "
                         "spill slot!");
    }
    TII->storeRegToStackSlot(*MBB, Before, Reg, true, FI, &RC, TRI, Register());
    MachineBasicBlock::iterator II = std::prev(Before);

    unsigned FIOperandNum = getFrameIndexOperandNum(*II);
    TRI->eliminateFrameIndex(II, SPAdj, FIOperandNum, this);

    // Restore the scavenged register before its use (or first terminator).
    TII->loadRegFromStackSlot(*MBB, UseMI, Reg, FI, &RC, TRI, Register());
    II = std::prev(UseMI);

    FIOperandNum = getFrameIndexOperandNum(*II);
    TRI->eliminateFrameIndex(II, SPAdj, FIOperandNum, this);
  }
  return Scavenged[SI];
}

Register RegScavenger::scavengeRegisterBackwards(const TargetRegisterClass &RC,
                                                 MachineBasicBlock::iterator To,
                                                 bool RestoreAfter, int SPAdj,
                                                 bool AllowSpill) {
  const MachineBasicBlock &MBB = *To->getParent();
  const MachineFunction &MF = *MBB.getParent();

  // Find the register whose use is furthest away.
  MachineBasicBlock::iterator UseMI;
  ArrayRef<MCPhysReg> AllocationOrder = RC.getRawAllocationOrder(MF);
  std::pair<MCPhysReg, MachineBasicBlock::iterator> P = findSurvivorBackwards(
      *MRI, std::prev(MBBI), To, LiveUnits, AllocationOrder, RestoreAfter);
  MCPhysReg Reg = P.first;
  MachineBasicBlock::iterator SpillBefore = P.second;
  // Found an available register?
  if (Reg != 0 && SpillBefore == MBB.end()) {
    LLVM_DEBUG(dbgs() << "Scavenged free register: " << printReg(Reg, TRI)
               << '\n');
    return Reg;
  }

  if (!AllowSpill)
    return 0;

  assert(Reg != 0 && "No register left to scavenge!");

  MachineBasicBlock::iterator ReloadBefore =
      RestoreAfter ? std::next(MBBI) : MBBI;
  if (ReloadBefore != MBB.end())
    LLVM_DEBUG(dbgs() << "Reload before: " << *ReloadBefore << '\n');
  ScavengedInfo &Scavenged = spill(Reg, RC, SPAdj, SpillBefore, ReloadBefore);
  Scavenged.Restore = &*std::prev(SpillBefore);
  LiveUnits.removeReg(Reg);
  LLVM_DEBUG(dbgs() << "Scavenged register with spill: " << printReg(Reg, TRI)
             << " until " << *SpillBefore);
  return Reg;
}

/// Allocate a register for the virtual register \p VReg. The last use of
/// \p VReg is around the current position of the register scavenger \p RS.
/// \p ReserveAfter controls whether the scavenged register needs to be reserved
/// after the current instruction, otherwise it will only be reserved before the
/// current instruction.
static Register scavengeVReg(MachineRegisterInfo &MRI, RegScavenger &RS,
                             Register VReg, bool ReserveAfter) {
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
#ifndef NDEBUG
  // Verify that all definitions and uses are in the same basic block.
  const MachineBasicBlock *CommonMBB = nullptr;
  // Real definition for the reg, re-definitions are not considered.
  const MachineInstr *RealDef = nullptr;
  for (MachineOperand &MO : MRI.reg_nodbg_operands(VReg)) {
    MachineBasicBlock *MBB = MO.getParent()->getParent();
    if (CommonMBB == nullptr)
      CommonMBB = MBB;
    assert(MBB == CommonMBB && "All defs+uses must be in the same basic block");
    if (MO.isDef()) {
      const MachineInstr &MI = *MO.getParent();
      if (!MI.readsRegister(VReg, &TRI)) {
        assert((!RealDef || RealDef == &MI) &&
               "Can have at most one definition which is not a redefinition");
        RealDef = &MI;
      }
    }
  }
  assert(RealDef != nullptr && "Must have at least 1 Def");
#endif

  // We should only have one definition of the register. However to accommodate
  // the requirements of two address code we also allow definitions in
  // subsequent instructions provided they also read the register. That way
  // we get a single contiguous lifetime.
  //
  // Definitions in MRI.def_begin() are unordered, search for the first.
  MachineRegisterInfo::def_iterator FirstDef = llvm::find_if(
      MRI.def_operands(VReg), [VReg, &TRI](const MachineOperand &MO) {
        return !MO.getParent()->readsRegister(VReg, &TRI);
      });
  assert(FirstDef != MRI.def_end() &&
         "Must have one definition that does not redefine vreg");
  MachineInstr &DefMI = *FirstDef->getParent();

  // The register scavenger will report a free register inserting an emergency
  // spill/reload if necessary.
  int SPAdj = 0;
  const TargetRegisterClass &RC = *MRI.getRegClass(VReg);
  Register SReg = RS.scavengeRegisterBackwards(RC, DefMI.getIterator(),
                                               ReserveAfter, SPAdj);
  MRI.replaceRegWith(VReg, SReg);
  ++NumScavengedRegs;
  return SReg;
}

/// Allocate (scavenge) vregs inside a single basic block.
/// Returns true if the target spill callback created new vregs and a 2nd pass
/// is necessary.
static bool scavengeFrameVirtualRegsInBlock(MachineRegisterInfo &MRI,
                                            RegScavenger &RS,
                                            MachineBasicBlock &MBB) {
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  RS.enterBasicBlockEnd(MBB);

  unsigned InitialNumVirtRegs = MRI.getNumVirtRegs();
  bool NextInstructionReadsVReg = false;
  for (MachineBasicBlock::iterator I = MBB.end(); I != MBB.begin(); ) {
    // Move RegScavenger to the position between *std::prev(I) and *I.
    RS.backward(I);
    --I;

    // Look for unassigned vregs in the uses of *std::next(I).
    if (NextInstructionReadsVReg) {
      MachineBasicBlock::iterator N = std::next(I);
      const MachineInstr &NMI = *N;
      for (const MachineOperand &MO : NMI.operands()) {
        if (!MO.isReg())
          continue;
        Register Reg = MO.getReg();
        // We only care about virtual registers and ignore virtual registers
        // created by the target callbacks in the process (those will be handled
        // in a scavenging round).
        if (!Reg.isVirtual() ||
            Register::virtReg2Index(Reg) >= InitialNumVirtRegs)
          continue;
        if (!MO.readsReg())
          continue;

        Register SReg = scavengeVReg(MRI, RS, Reg, true);
        N->addRegisterKilled(SReg, &TRI, false);
        RS.setRegUsed(SReg);
      }
    }

    // Look for unassigned vregs in the defs of *I.
    NextInstructionReadsVReg = false;
    const MachineInstr &MI = *I;
    for (const MachineOperand &MO : MI.operands()) {
      if (!MO.isReg())
        continue;
      Register Reg = MO.getReg();
      // Only vregs, no newly created vregs (see above).
      if (!Reg.isVirtual() ||
          Register::virtReg2Index(Reg) >= InitialNumVirtRegs)
        continue;
      // We have to look at all operands anyway so we can precalculate here
      // whether there is a reading operand. This allows use to skip the use
      // step in the next iteration if there was none.
      assert(!MO.isInternalRead() && "Cannot assign inside bundles");
      assert((!MO.isUndef() || MO.isDef()) && "Cannot handle undef uses");
      if (MO.readsReg()) {
        NextInstructionReadsVReg = true;
      }
      if (MO.isDef()) {
        Register SReg = scavengeVReg(MRI, RS, Reg, false);
        I->addRegisterDead(SReg, &TRI, false);
      }
    }
  }
#ifndef NDEBUG
  for (const MachineOperand &MO : MBB.front().operands()) {
    if (!MO.isReg() || !MO.getReg().isVirtual())
      continue;
    assert(!MO.isInternalRead() && "Cannot assign inside bundles");
    assert((!MO.isUndef() || MO.isDef()) && "Cannot handle undef uses");
    assert(!MO.readsReg() && "Vreg use in first instruction not allowed");
  }
#endif

  return MRI.getNumVirtRegs() != InitialNumVirtRegs;
}

void llvm::scavengeFrameVirtualRegs(MachineFunction &MF, RegScavenger &RS) {
  // FIXME: Iterating over the instruction stream is unnecessary. We can simply
  // iterate over the vreg use list, which at this point only contains machine
  // operands for which eliminateFrameIndex need a new scratch reg.
  MachineRegisterInfo &MRI = MF.getRegInfo();
  // Shortcut.
  if (MRI.getNumVirtRegs() == 0) {
    MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
    return;
  }

  // Run through the instructions and find any virtual registers.
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.empty())
      continue;

    bool Again = scavengeFrameVirtualRegsInBlock(MRI, RS, MBB);
    if (Again) {
      LLVM_DEBUG(dbgs() << "Warning: Required two scavenging passes for block "
                        << MBB.getName() << '\n');
      Again = scavengeFrameVirtualRegsInBlock(MRI, RS, MBB);
      // The target required a 2nd run (because it created new vregs while
      // spilling). Refuse to do another pass to keep compiletime in check.
      if (Again)
        report_fatal_error("Incomplete scavenging after 2nd pass");
    }
  }

  MRI.clearVirtRegs();
  MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
}

namespace {

/// This class runs register scavenging independ of the PrologEpilogInserter.
/// This is used in for testing.
class ScavengerTest : public MachineFunctionPass {
public:
  static char ID;

  ScavengerTest() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const TargetSubtargetInfo &STI = MF.getSubtarget();
    const TargetFrameLowering &TFL = *STI.getFrameLowering();

    RegScavenger RS;
    // Let's hope that calling those outside of PrologEpilogueInserter works
    // well enough to initialize the scavenger with some emergency spillslots
    // for the target.
    BitVector SavedRegs;
    TFL.determineCalleeSaves(MF, SavedRegs, &RS);
    TFL.processFunctionBeforeFrameFinalized(MF, &RS);

    // Let's scavenge the current function
    scavengeFrameVirtualRegs(MF, RS);
    return true;
  }
};

} // end anonymous namespace

char ScavengerTest::ID;

INITIALIZE_PASS(ScavengerTest, "scavenger-test",
                "Scavenge virtual registers inside basic blocks", false, false)
