//===-- SIPostRABundler.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass creates bundles of memory instructions to protect adjacent loads
/// and stores from being rescheduled apart from each other post-RA.
///
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineFunctionPass.h"

using namespace llvm;

#define DEBUG_TYPE "si-post-ra-bundler"

namespace {

class SIPostRABundler : public MachineFunctionPass {
public:
  static char ID;

public:
  SIPostRABundler() : MachineFunctionPass(ID) {
    initializeSIPostRABundlerPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "SI post-RA bundler";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  const SIRegisterInfo *TRI;

  SmallSet<Register, 16> Defs;

  void collectUsedRegUnits(const MachineInstr &MI,
                           BitVector &UsedRegUnits) const;

  bool isBundleCandidate(const MachineInstr &MI) const;
  bool isDependentLoad(const MachineInstr &MI) const;
  bool canBundle(const MachineInstr &MI, const MachineInstr &NextMI) const;
};

constexpr uint64_t MemFlags = SIInstrFlags::MTBUF | SIInstrFlags::MUBUF |
                              SIInstrFlags::SMRD | SIInstrFlags::DS |
                              SIInstrFlags::FLAT | SIInstrFlags::MIMG;

} // End anonymous namespace.

INITIALIZE_PASS(SIPostRABundler, DEBUG_TYPE, "SI post-RA bundler", false, false)

char SIPostRABundler::ID = 0;

char &llvm::SIPostRABundlerID = SIPostRABundler::ID;

FunctionPass *llvm::createSIPostRABundlerPass() {
  return new SIPostRABundler();
}

bool SIPostRABundler::isDependentLoad(const MachineInstr &MI) const {
  if (!MI.mayLoad())
    return false;

  for (const MachineOperand &Op : MI.explicit_operands()) {
    if (!Op.isReg())
      continue;
    Register Reg = Op.getReg();
    for (Register Def : Defs)
      if (TRI->regsOverlap(Reg, Def))
        return true;
  }

  return false;
}

void SIPostRABundler::collectUsedRegUnits(const MachineInstr &MI,
                                          BitVector &UsedRegUnits) const {
  if (MI.isDebugInstr())
    return;

  for (const MachineOperand &Op : MI.operands()) {
    if (!Op.isReg() || !Op.readsReg())
      continue;

    Register Reg = Op.getReg();
    assert(!Op.getSubReg() &&
           "subregister indexes should not be present after RA");

    for (MCRegUnit Unit : TRI->regunits(Reg))
      UsedRegUnits.set(Unit);
  }
}

bool SIPostRABundler::isBundleCandidate(const MachineInstr &MI) const {
  const uint64_t IMemFlags = MI.getDesc().TSFlags & MemFlags;
  return IMemFlags != 0 && MI.mayLoadOrStore() && !MI.isBundled();
}

bool SIPostRABundler::canBundle(const MachineInstr &MI,
                                const MachineInstr &NextMI) const {
  const uint64_t IMemFlags = MI.getDesc().TSFlags & MemFlags;

  return (IMemFlags != 0 && MI.mayLoadOrStore() && !NextMI.isBundled() &&
          NextMI.mayLoad() == MI.mayLoad() && NextMI.mayStore() == MI.mayStore() &&
          ((NextMI.getDesc().TSFlags & MemFlags) == IMemFlags) &&
          !isDependentLoad(NextMI));
}

bool SIPostRABundler::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  TRI = MF.getSubtarget<GCNSubtarget>().getRegisterInfo();
  BitVector BundleUsedRegUnits(TRI->getNumRegUnits());
  BitVector KillUsedRegUnits(TRI->getNumRegUnits());

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    bool HasIGLPInstrs = llvm::any_of(MBB.instrs(), [](MachineInstr &MI) {
      unsigned Opc = MI.getOpcode();
      return Opc == AMDGPU::SCHED_GROUP_BARRIER || Opc == AMDGPU::IGLP_OPT;
    });

    // Don't cluster with IGLP instructions.
    if (HasIGLPInstrs)
      continue;

    MachineBasicBlock::instr_iterator Next;
    MachineBasicBlock::instr_iterator B = MBB.instr_begin();
    MachineBasicBlock::instr_iterator E = MBB.instr_end();

    for (auto I = B; I != E; I = Next) {
      Next = std::next(I);
      if (!isBundleCandidate(*I))
        continue;

      assert(Defs.empty());

      if (I->getNumExplicitDefs() != 0)
        Defs.insert(I->defs().begin()->getReg());

      MachineBasicBlock::instr_iterator BundleStart = I;
      MachineBasicBlock::instr_iterator BundleEnd = I;
      unsigned ClauseLength = 1;
      for (I = Next; I != E; I = Next) {
        Next = std::next(I);

        assert(BundleEnd != I);
        if (canBundle(*BundleEnd, *I)) {
          BundleEnd = I;
          if (I->getNumExplicitDefs() != 0)
            Defs.insert(I->defs().begin()->getReg());
          ++ClauseLength;
        } else if (!I->isMetaInstruction()) {
          // Allow meta instructions in between bundle candidates, but do not
          // start or end a bundle on one.
          //
          // TODO: It may be better to move meta instructions like dbg_value
          // after the bundle. We're relying on the memory legalizer to unbundle
          // these.
          break;
        }
      }

      Next = std::next(BundleEnd);
      if (ClauseLength > 1) {
        Changed = true;

        // Before register allocation, kills are inserted after potential soft
        // clauses to hint register allocation. Look for kills that look like
        // this, and erase them.
        if (Next != E && Next->isKill()) {

          // TODO: Should maybe back-propagate kill flags to the bundle.
          for (const MachineInstr &BundleMI : make_range(BundleStart, Next))
            collectUsedRegUnits(BundleMI, BundleUsedRegUnits);

          BundleUsedRegUnits.flip();

          while (Next != E && Next->isKill()) {
            MachineInstr &Kill = *Next;
            collectUsedRegUnits(Kill, KillUsedRegUnits);

            KillUsedRegUnits &= BundleUsedRegUnits;

            // Erase the kill if it's a subset of the used registers.
            //
            // TODO: Should we just remove all kills? Is there any real reason to
            // keep them after RA?
            if (KillUsedRegUnits.none()) {
              ++Next;
              Kill.eraseFromParent();
            } else
              break;

            KillUsedRegUnits.reset();
          }

          BundleUsedRegUnits.reset();
        }

        finalizeBundle(MBB, BundleStart, Next);
      }

      Defs.clear();
    }
  }

  return Changed;
}
