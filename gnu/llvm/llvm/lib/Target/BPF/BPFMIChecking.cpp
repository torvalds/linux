//===-------------- BPFMIChecking.cpp - MI Checking Legality -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs checking to signal errors for certain illegal usages at
// MachineInstruction layer. Specially, the result of XADD{32,64} insn should
// not be used. The pass is done at the PreEmit pass right before the
// machine code is emitted at which point the register liveness information
// is still available.
//
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "BPFInstrInfo.h"
#include "BPFTargetMachine.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "bpf-mi-checking"

namespace {

struct BPFMIPreEmitChecking : public MachineFunctionPass {

  static char ID;
  MachineFunction *MF;
  const TargetRegisterInfo *TRI;

  BPFMIPreEmitChecking() : MachineFunctionPass(ID) {
    initializeBPFMIPreEmitCheckingPass(*PassRegistry::getPassRegistry());
  }

private:
  // Initialize class variables.
  void initialize(MachineFunction &MFParm);

  bool processAtomicInsts();

public:

  // Main entry point for this pass.
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (!skipFunction(MF.getFunction())) {
      initialize(MF);
      return processAtomicInsts();
    }
    return false;
  }
};

// Initialize class variables.
void BPFMIPreEmitChecking::initialize(MachineFunction &MFParm) {
  MF = &MFParm;
  TRI = MF->getSubtarget<BPFSubtarget>().getRegisterInfo();
  LLVM_DEBUG(dbgs() << "*** BPF PreEmit checking pass ***\n\n");
}

// Make sure all Defs of XADD are dead, meaning any result of XADD insn is not
// used.
//
// NOTE: BPF backend hasn't enabled sub-register liveness track, so when the
// source and destination operands of XADD are GPR32, there is no sub-register
// dead info. If we rely on the generic MachineInstr::allDefsAreDead, then we
// will raise false alarm on GPR32 Def.
//
// To support GPR32 Def, ideally we could just enable sub-registr liveness track
// on BPF backend, then allDefsAreDead could work on GPR32 Def. This requires
// implementing TargetSubtargetInfo::enableSubRegLiveness on BPF.
//
// However, sub-register liveness tracking module inside LLVM is actually
// designed for the situation where one register could be split into more than
// one sub-registers for which case each sub-register could have their own
// liveness and kill one of them doesn't kill others. So, tracking liveness for
// each make sense.
//
// For BPF, each 64-bit register could only have one 32-bit sub-register. This
// is exactly the case which LLVM think brings no benefits for doing
// sub-register tracking, because the live range of sub-register must always
// equal to its parent register, therefore liveness tracking is disabled even
// the back-end has implemented enableSubRegLiveness. The detailed information
// is at r232695:
//
//   Author: Matthias Braun <matze@braunis.de>
//   Date:   Thu Mar 19 00:21:58 2015 +0000
//   Do not track subregister liveness when it brings no benefits
//
// Hence, for BPF, we enhance MachineInstr::allDefsAreDead. Given the solo
// sub-register always has the same liveness as its parent register, LLVM is
// already attaching a implicit 64-bit register Def whenever the there is
// a sub-register Def. The liveness of the implicit 64-bit Def is available.
// For example, for "lock *(u32 *)(r0 + 4) += w9", the MachineOperand info could
// be:
//
//   $w9 = XADDW32 killed $r0, 4, $w9(tied-def 0),
//                        implicit killed $r9, implicit-def dead $r9
//
// Even though w9 is not marked as Dead, the parent register r9 is marked as
// Dead correctly, and it is safe to use such information or our purpose.
static bool hasLiveDefs(const MachineInstr &MI, const TargetRegisterInfo *TRI) {
  const MCRegisterClass *GPR64RegClass =
    &BPFMCRegisterClasses[BPF::GPRRegClassID];
  std::vector<unsigned> GPR32LiveDefs;
  std::vector<unsigned> GPR64DeadDefs;

  for (const MachineOperand &MO : MI.operands()) {
    bool RegIsGPR64;

    if (!MO.isReg() || MO.isUse())
      continue;

    RegIsGPR64 = GPR64RegClass->contains(MO.getReg());
    if (!MO.isDead()) {
      // It is a GPR64 live Def, we are sure it is live. */
      if (RegIsGPR64)
        return true;
      // It is a GPR32 live Def, we are unsure whether it is really dead due to
      // no sub-register liveness tracking. Push it to vector for deferred
      // check.
      GPR32LiveDefs.push_back(MO.getReg());
      continue;
    }

    // Record any GPR64 dead Def as some unmarked GPR32 could be alias of its
    // low 32-bit.
    if (RegIsGPR64)
      GPR64DeadDefs.push_back(MO.getReg());
  }

  // No GPR32 live Def, safe to return false.
  if (GPR32LiveDefs.empty())
    return false;

  // No GPR64 dead Def, so all those GPR32 live Def can't have alias, therefore
  // must be truely live, safe to return true.
  if (GPR64DeadDefs.empty())
    return true;

  // Otherwise, return true if any aliased SuperReg of GPR32 is not dead.
  for (auto I : GPR32LiveDefs)
    for (MCPhysReg SR : TRI->superregs(I))
      if (!llvm::is_contained(GPR64DeadDefs, SR))
        return true;

  return false;
}

bool BPFMIPreEmitChecking::processAtomicInsts() {
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      if (MI.getOpcode() != BPF::XADDW &&
          MI.getOpcode() != BPF::XADDD &&
          MI.getOpcode() != BPF::XADDW32)
        continue;

      LLVM_DEBUG(MI.dump());
      if (hasLiveDefs(MI, TRI)) {
        DebugLoc Empty;
        const DebugLoc &DL = MI.getDebugLoc();
        const Function &F = MF->getFunction();
        F.getContext().diagnose(DiagnosticInfoUnsupported{
            F, "Invalid usage of the XADD return value", DL});
      }
    }
  }

  // Check return values of atomic_fetch_and_{add,and,or,xor}.
  // If the return is not used, the atomic_fetch_and_<op> instruction
  // is replaced with atomic_<op> instruction.
  MachineInstr *ToErase = nullptr;
  bool Changed = false;
  const BPFInstrInfo *TII = MF->getSubtarget<BPFSubtarget>().getInstrInfo();
  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      if (ToErase) {
        ToErase->eraseFromParent();
        ToErase = nullptr;
      }

      if (MI.getOpcode() != BPF::XFADDW32 && MI.getOpcode() != BPF::XFADDD &&
          MI.getOpcode() != BPF::XFANDW32 && MI.getOpcode() != BPF::XFANDD &&
          MI.getOpcode() != BPF::XFXORW32 && MI.getOpcode() != BPF::XFXORD &&
          MI.getOpcode() != BPF::XFORW32 && MI.getOpcode() != BPF::XFORD)
        continue;

      if (hasLiveDefs(MI, TRI))
        continue;

      LLVM_DEBUG(dbgs() << "Transforming "; MI.dump());
      unsigned newOpcode;
      switch (MI.getOpcode()) {
      case BPF::XFADDW32:
        newOpcode = BPF::XADDW32;
        break;
      case BPF::XFADDD:
        newOpcode = BPF::XADDD;
        break;
      case BPF::XFANDW32:
        newOpcode = BPF::XANDW32;
        break;
      case BPF::XFANDD:
        newOpcode = BPF::XANDD;
        break;
      case BPF::XFXORW32:
        newOpcode = BPF::XXORW32;
        break;
      case BPF::XFXORD:
        newOpcode = BPF::XXORD;
        break;
      case BPF::XFORW32:
        newOpcode = BPF::XORW32;
        break;
      case BPF::XFORD:
        newOpcode = BPF::XORD;
        break;
      default:
        llvm_unreachable("Incorrect Atomic Instruction Opcode");
      }

      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(newOpcode))
          .add(MI.getOperand(0))
          .add(MI.getOperand(1))
          .add(MI.getOperand(2))
          .add(MI.getOperand(3));

      ToErase = &MI;
      Changed = true;
    }
  }

  return Changed;
}

} // end default namespace

INITIALIZE_PASS(BPFMIPreEmitChecking, "bpf-mi-pemit-checking",
                "BPF PreEmit Checking", false, false)

char BPFMIPreEmitChecking::ID = 0;
FunctionPass* llvm::createBPFMIPreEmitCheckingPass()
{
  return new BPFMIPreEmitChecking();
}
