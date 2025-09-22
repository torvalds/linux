//===-- lib/CodeGen/MachineInstrBundle.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineInstrBundle.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include <utility>
using namespace llvm;

namespace {
  class UnpackMachineBundles : public MachineFunctionPass {
  public:
    static char ID; // Pass identification
    UnpackMachineBundles(
        std::function<bool(const MachineFunction &)> Ftor = nullptr)
        : MachineFunctionPass(ID), PredicateFtor(std::move(Ftor)) {
      initializeUnpackMachineBundlesPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    std::function<bool(const MachineFunction &)> PredicateFtor;
  };
} // end anonymous namespace

char UnpackMachineBundles::ID = 0;
char &llvm::UnpackMachineBundlesID = UnpackMachineBundles::ID;
INITIALIZE_PASS(UnpackMachineBundles, "unpack-mi-bundles",
                "Unpack machine instruction bundles", false, false)

bool UnpackMachineBundles::runOnMachineFunction(MachineFunction &MF) {
  if (PredicateFtor && !PredicateFtor(MF))
    return false;

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::instr_iterator MII = MBB.instr_begin(),
           MIE = MBB.instr_end(); MII != MIE; ) {
      MachineInstr *MI = &*MII;

      // Remove BUNDLE instruction and the InsideBundle flags from bundled
      // instructions.
      if (MI->isBundle()) {
        while (++MII != MIE && MII->isBundledWithPred()) {
          MII->unbundleFromPred();
          for (MachineOperand &MO  : MII->operands()) {
            if (MO.isReg() && MO.isInternalRead())
              MO.setIsInternalRead(false);
          }
        }
        MI->eraseFromParent();

        Changed = true;
        continue;
      }

      ++MII;
    }
  }

  return Changed;
}

FunctionPass *
llvm::createUnpackMachineBundles(
    std::function<bool(const MachineFunction &)> Ftor) {
  return new UnpackMachineBundles(std::move(Ftor));
}

namespace {
  class FinalizeMachineBundles : public MachineFunctionPass {
  public:
    static char ID; // Pass identification
    FinalizeMachineBundles() : MachineFunctionPass(ID) {
      initializeFinalizeMachineBundlesPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override;
  };
} // end anonymous namespace

char FinalizeMachineBundles::ID = 0;
char &llvm::FinalizeMachineBundlesID = FinalizeMachineBundles::ID;
INITIALIZE_PASS(FinalizeMachineBundles, "finalize-mi-bundles",
                "Finalize machine instruction bundles", false, false)

bool FinalizeMachineBundles::runOnMachineFunction(MachineFunction &MF) {
  return llvm::finalizeBundles(MF);
}

/// Return the first found DebugLoc that has a DILocation, given a range of
/// instructions. The search range is from FirstMI to LastMI (exclusive). If no
/// DILocation is found, then an empty location is returned.
static DebugLoc getDebugLoc(MachineBasicBlock::instr_iterator FirstMI,
                            MachineBasicBlock::instr_iterator LastMI) {
  for (auto MII = FirstMI; MII != LastMI; ++MII)
    if (MII->getDebugLoc())
      return MII->getDebugLoc();
  return DebugLoc();
}

/// finalizeBundle - Finalize a machine instruction bundle which includes
/// a sequence of instructions starting from FirstMI to LastMI (exclusive).
/// This routine adds a BUNDLE instruction to represent the bundle, it adds
/// IsInternalRead markers to MachineOperands which are defined inside the
/// bundle, and it copies externally visible defs and uses to the BUNDLE
/// instruction.
void llvm::finalizeBundle(MachineBasicBlock &MBB,
                          MachineBasicBlock::instr_iterator FirstMI,
                          MachineBasicBlock::instr_iterator LastMI) {
  assert(FirstMI != LastMI && "Empty bundle?");
  MIBundleBuilder Bundle(MBB, FirstMI, LastMI);

  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  MachineInstrBuilder MIB =
      BuildMI(MF, getDebugLoc(FirstMI, LastMI), TII->get(TargetOpcode::BUNDLE));
  Bundle.prepend(MIB);

  SmallVector<Register, 32> LocalDefs;
  SmallSet<Register, 32> LocalDefSet;
  SmallSet<Register, 8> DeadDefSet;
  SmallSet<Register, 16> KilledDefSet;
  SmallVector<Register, 8> ExternUses;
  SmallSet<Register, 8> ExternUseSet;
  SmallSet<Register, 8> KilledUseSet;
  SmallSet<Register, 8> UndefUseSet;
  SmallVector<MachineOperand*, 4> Defs;
  for (auto MII = FirstMI; MII != LastMI; ++MII) {
    // Debug instructions have no effects to track.
    if (MII->isDebugInstr())
      continue;

    for (MachineOperand &MO : MII->operands()) {
      if (!MO.isReg())
        continue;
      if (MO.isDef()) {
        Defs.push_back(&MO);
        continue;
      }

      Register Reg = MO.getReg();
      if (!Reg)
        continue;

      if (LocalDefSet.count(Reg)) {
        MO.setIsInternalRead();
        if (MO.isKill())
          // Internal def is now killed.
          KilledDefSet.insert(Reg);
      } else {
        if (ExternUseSet.insert(Reg).second) {
          ExternUses.push_back(Reg);
          if (MO.isUndef())
            UndefUseSet.insert(Reg);
        }
        if (MO.isKill())
          // External def is now killed.
          KilledUseSet.insert(Reg);
      }
    }

    for (MachineOperand *MO : Defs) {
      Register Reg = MO->getReg();
      if (!Reg)
        continue;

      if (LocalDefSet.insert(Reg).second) {
        LocalDefs.push_back(Reg);
        if (MO->isDead()) {
          DeadDefSet.insert(Reg);
        }
      } else {
        // Re-defined inside the bundle, it's no longer killed.
        KilledDefSet.erase(Reg);
        if (!MO->isDead())
          // Previously defined but dead.
          DeadDefSet.erase(Reg);
      }

      if (!MO->isDead() && Reg.isPhysical()) {
        for (MCPhysReg SubReg : TRI->subregs(Reg)) {
          if (LocalDefSet.insert(SubReg).second)
            LocalDefs.push_back(SubReg);
        }
      }
    }

    Defs.clear();
  }

  SmallSet<Register, 32> Added;
  for (Register Reg : LocalDefs) {
    if (Added.insert(Reg).second) {
      // If it's not live beyond end of the bundle, mark it dead.
      bool isDead = DeadDefSet.count(Reg) || KilledDefSet.count(Reg);
      MIB.addReg(Reg, getDefRegState(true) | getDeadRegState(isDead) |
                 getImplRegState(true));
    }
  }

  for (Register Reg : ExternUses) {
    bool isKill = KilledUseSet.count(Reg);
    bool isUndef = UndefUseSet.count(Reg);
    MIB.addReg(Reg, getKillRegState(isKill) | getUndefRegState(isUndef) |
               getImplRegState(true));
  }

  // Set FrameSetup/FrameDestroy for the bundle. If any of the instructions got
  // the property, then also set it on the bundle.
  for (auto MII = FirstMI; MII != LastMI; ++MII) {
    if (MII->getFlag(MachineInstr::FrameSetup))
      MIB.setMIFlag(MachineInstr::FrameSetup);
    if (MII->getFlag(MachineInstr::FrameDestroy))
      MIB.setMIFlag(MachineInstr::FrameDestroy);
  }
}

/// finalizeBundle - Same functionality as the previous finalizeBundle except
/// the last instruction in the bundle is not provided as an input. This is
/// used in cases where bundles are pre-determined by marking instructions
/// with 'InsideBundle' marker. It returns the MBB instruction iterator that
/// points to the end of the bundle.
MachineBasicBlock::instr_iterator
llvm::finalizeBundle(MachineBasicBlock &MBB,
                     MachineBasicBlock::instr_iterator FirstMI) {
  MachineBasicBlock::instr_iterator E = MBB.instr_end();
  MachineBasicBlock::instr_iterator LastMI = std::next(FirstMI);
  while (LastMI != E && LastMI->isInsideBundle())
    ++LastMI;
  finalizeBundle(MBB, FirstMI, LastMI);
  return LastMI;
}

/// finalizeBundles - Finalize instruction bundles in the specified
/// MachineFunction. Return true if any bundles are finalized.
bool llvm::finalizeBundles(MachineFunction &MF) {
  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::instr_iterator MII = MBB.instr_begin();
    MachineBasicBlock::instr_iterator MIE = MBB.instr_end();
    if (MII == MIE)
      continue;
    assert(!MII->isInsideBundle() &&
           "First instr cannot be inside bundle before finalization!");

    for (++MII; MII != MIE; ) {
      if (!MII->isInsideBundle())
        ++MII;
      else {
        MII = finalizeBundle(MBB, std::prev(MII));
        Changed = true;
      }
    }
  }

  return Changed;
}

VirtRegInfo llvm::AnalyzeVirtRegInBundle(
    MachineInstr &MI, Register Reg,
    SmallVectorImpl<std::pair<MachineInstr *, unsigned>> *Ops) {
  VirtRegInfo RI = {false, false, false};
  for (MIBundleOperands O(MI); O.isValid(); ++O) {
    MachineOperand &MO = *O;
    if (!MO.isReg() || MO.getReg() != Reg)
      continue;

    // Remember each (MI, OpNo) that refers to Reg.
    if (Ops)
      Ops->push_back(std::make_pair(MO.getParent(), O.getOperandNo()));

    // Both defs and uses can read virtual registers.
    if (MO.readsReg()) {
      RI.Reads = true;
      if (MO.isDef())
        RI.Tied = true;
    }

    // Only defs can write.
    if (MO.isDef())
      RI.Writes = true;
    else if (!RI.Tied &&
             MO.getParent()->isRegTiedToDefOperand(O.getOperandNo()))
      RI.Tied = true;
  }
  return RI;
}

std::pair<LaneBitmask, LaneBitmask>
llvm::AnalyzeVirtRegLanesInBundle(const MachineInstr &MI, Register Reg,
                                  const MachineRegisterInfo &MRI,
                                  const TargetRegisterInfo &TRI) {

  LaneBitmask UseMask, DefMask;

  for (const MachineOperand &MO : const_mi_bundle_ops(MI)) {
    if (!MO.isReg() || MO.getReg() != Reg)
      continue;

    unsigned SubReg = MO.getSubReg();
    if (SubReg == 0 && MO.isUse() && !MO.isUndef())
      UseMask |= MRI.getMaxLaneMaskForVReg(Reg);

    LaneBitmask SubRegMask = TRI.getSubRegIndexLaneMask(SubReg);
    if (MO.isDef()) {
      if (!MO.isUndef())
        UseMask |= ~SubRegMask;
      DefMask |= SubRegMask;
    } else if (!MO.isUndef())
      UseMask |= SubRegMask;
  }

  return {UseMask, DefMask};
}

PhysRegInfo llvm::AnalyzePhysRegInBundle(const MachineInstr &MI, Register Reg,
                                         const TargetRegisterInfo *TRI) {
  bool AllDefsDead = true;
  PhysRegInfo PRI = {false, false, false, false, false, false, false, false};

  assert(Reg.isPhysical() && "analyzePhysReg not given a physical register!");
  for (const MachineOperand &MO : const_mi_bundle_ops(MI)) {
    if (MO.isRegMask() && MO.clobbersPhysReg(Reg)) {
      PRI.Clobbered = true;
      continue;
    }

    if (!MO.isReg())
      continue;

    Register MOReg = MO.getReg();
    if (!MOReg || !MOReg.isPhysical())
      continue;

    if (!TRI->regsOverlap(MOReg, Reg))
      continue;

    bool Covered = TRI->isSuperRegisterEq(Reg, MOReg);
    if (MO.readsReg()) {
      PRI.Read = true;
      if (Covered) {
        PRI.FullyRead = true;
        if (MO.isKill())
          PRI.Killed = true;
      }
    } else if (MO.isDef()) {
      PRI.Defined = true;
      if (Covered)
        PRI.FullyDefined = true;
      if (!MO.isDead())
        AllDefsDead = false;
    }
  }

  if (AllDefsDead) {
    if (PRI.FullyDefined || PRI.Clobbered)
      PRI.DeadDef = true;
    else if (PRI.Defined)
      PRI.PartialDeadDef = true;
  }

  return PRI;
}
