//=== AArch64PostSelectOptimize.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does post-instruction-selection optimizations in the GlobalISel
// pipeline, before the rest of codegen runs.
//
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64TargetMachine.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

#define DEBUG_TYPE "aarch64-post-select-optimize"

using namespace llvm;

namespace {
class AArch64PostSelectOptimize : public MachineFunctionPass {
public:
  static char ID;

  AArch64PostSelectOptimize();

  StringRef getPassName() const override {
    return "AArch64 Post Select Optimizer";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  bool optimizeNZCVDefs(MachineBasicBlock &MBB);
  bool doPeepholeOpts(MachineBasicBlock &MBB);
  /// Look for cross regclass copies that can be trivially eliminated.
  bool foldSimpleCrossClassCopies(MachineInstr &MI);
  bool foldCopyDup(MachineInstr &MI);
};
} // end anonymous namespace

void AArch64PostSelectOptimize::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

AArch64PostSelectOptimize::AArch64PostSelectOptimize()
    : MachineFunctionPass(ID) {
  initializeAArch64PostSelectOptimizePass(*PassRegistry::getPassRegistry());
}

unsigned getNonFlagSettingVariant(unsigned Opc) {
  switch (Opc) {
  default:
    return 0;
  case AArch64::SUBSXrr:
    return AArch64::SUBXrr;
  case AArch64::SUBSWrr:
    return AArch64::SUBWrr;
  case AArch64::SUBSXrs:
    return AArch64::SUBXrs;
  case AArch64::SUBSWrs:
    return AArch64::SUBWrs;
  case AArch64::SUBSXri:
    return AArch64::SUBXri;
  case AArch64::SUBSWri:
    return AArch64::SUBWri;
  case AArch64::ADDSXrr:
    return AArch64::ADDXrr;
  case AArch64::ADDSWrr:
    return AArch64::ADDWrr;
  case AArch64::ADDSXrs:
    return AArch64::ADDXrs;
  case AArch64::ADDSWrs:
    return AArch64::ADDWrs;
  case AArch64::ADDSXri:
    return AArch64::ADDXri;
  case AArch64::ADDSWri:
    return AArch64::ADDWri;
  case AArch64::SBCSXr:
    return AArch64::SBCXr;
  case AArch64::SBCSWr:
    return AArch64::SBCWr;
  case AArch64::ADCSXr:
    return AArch64::ADCXr;
  case AArch64::ADCSWr:
    return AArch64::ADCWr;
  }
}

bool AArch64PostSelectOptimize::doPeepholeOpts(MachineBasicBlock &MBB) {
  bool Changed = false;
  for (auto &MI : make_early_inc_range(make_range(MBB.begin(), MBB.end()))) {
    bool CurrentIterChanged = foldSimpleCrossClassCopies(MI);
    if (!CurrentIterChanged)
      CurrentIterChanged |= foldCopyDup(MI);
    Changed |= CurrentIterChanged;
  }
  return Changed;
}

bool AArch64PostSelectOptimize::foldSimpleCrossClassCopies(MachineInstr &MI) {
  auto *MF = MI.getMF();
  auto &MRI = MF->getRegInfo();

  if (!MI.isCopy())
    return false;

  if (MI.getOperand(1).getSubReg())
    return false; // Don't deal with subreg copies

  Register Src = MI.getOperand(1).getReg();
  Register Dst = MI.getOperand(0).getReg();

  if (Src.isPhysical() || Dst.isPhysical())
    return false;

  const TargetRegisterClass *SrcRC = MRI.getRegClass(Src);
  const TargetRegisterClass *DstRC = MRI.getRegClass(Dst);

  if (SrcRC == DstRC)
    return false;


  if (SrcRC->hasSubClass(DstRC)) {
    // This is the case where the source class is a superclass of the dest, so
    // if the copy is the only user of the source, we can just constrain the
    // source reg to the dest class.

    if (!MRI.hasOneNonDBGUse(Src))
      return false; // Only constrain single uses of the source.

    // Constrain to dst reg class as long as it's not a weird class that only
    // has a few registers.
    if (!MRI.constrainRegClass(Src, DstRC, /* MinNumRegs */ 25))
      return false;
  } else if (DstRC->hasSubClass(SrcRC)) {
    // This is the inverse case, where the destination class is a superclass of
    // the source. Here, if the copy is the only user, we can just constrain
    // the user of the copy to use the smaller class of the source.
  } else {
    return false;
  }

  MRI.replaceRegWith(Dst, Src);
  MI.eraseFromParent();
  return true;
}

bool AArch64PostSelectOptimize::foldCopyDup(MachineInstr &MI) {
  if (!MI.isCopy())
    return false;

  auto *MF = MI.getMF();
  auto &MRI = MF->getRegInfo();
  auto *TII = MF->getSubtarget().getInstrInfo();

  // Optimize COPY(y:GPR, DUP(x:FPR, i)) -> UMOV(y:GPR, x:FPR, i).
  // Here Dst is y and Src is the result of DUP.
  Register Dst = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();

  if (!Dst.isVirtual() || !Src.isVirtual())
    return false;

  auto TryMatchDUP = [&](const TargetRegisterClass *GPRRegClass,
                         const TargetRegisterClass *FPRRegClass, unsigned DUP,
                         unsigned UMOV) {
    if (MRI.getRegClassOrNull(Dst) != GPRRegClass ||
        MRI.getRegClassOrNull(Src) != FPRRegClass)
      return false;

    // There is a special case when one of the uses is COPY(z:FPR, y:GPR).
    // In this case, we get COPY(z:FPR, COPY(y:GPR, DUP(x:FPR, i))), which can
    // be folded by peephole-opt into just DUP(z:FPR, i), so this transform is
    // not worthwhile in that case.
    for (auto &Use : MRI.use_nodbg_instructions(Dst)) {
      if (!Use.isCopy())
        continue;

      Register UseOp0 = Use.getOperand(0).getReg();
      Register UseOp1 = Use.getOperand(1).getReg();
      if (UseOp0.isPhysical() || UseOp1.isPhysical())
        return false;

      if (MRI.getRegClassOrNull(UseOp0) == FPRRegClass &&
          MRI.getRegClassOrNull(UseOp1) == GPRRegClass)
        return false;
    }

    MachineInstr *SrcMI = MRI.getUniqueVRegDef(Src);
    if (!SrcMI || SrcMI->getOpcode() != DUP || !MRI.hasOneNonDBGUse(Src))
      return false;

    Register DupSrc = SrcMI->getOperand(1).getReg();
    int64_t DupImm = SrcMI->getOperand(2).getImm();

    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(UMOV), Dst)
        .addReg(DupSrc)
        .addImm(DupImm);
    SrcMI->eraseFromParent();
    MI.eraseFromParent();
    return true;
  };

  return TryMatchDUP(&AArch64::GPR32RegClass, &AArch64::FPR32RegClass,
                     AArch64::DUPi32, AArch64::UMOVvi32) ||
         TryMatchDUP(&AArch64::GPR64RegClass, &AArch64::FPR64RegClass,
                     AArch64::DUPi64, AArch64::UMOVvi64);
}

bool AArch64PostSelectOptimize::optimizeNZCVDefs(MachineBasicBlock &MBB) {
  // If we find a dead NZCV implicit-def, we
  // - try to convert the operation to a non-flag-setting equivalent
  // - or mark the def as dead to aid later peephole optimizations.

  // Use cases:
  // 1)
  // Consider the following code:
  //  FCMPSrr %0, %1, implicit-def $nzcv
  //  %sel1:gpr32 = CSELWr %_, %_, 12, implicit $nzcv
  //  %sub:gpr32 = SUBSWrr %_, %_, implicit-def $nzcv
  //  FCMPSrr %0, %1, implicit-def $nzcv
  //  %sel2:gpr32 = CSELWr %_, %_, 12, implicit $nzcv
  // This kind of code where we have 2 FCMPs each feeding a CSEL can happen
  // when we have a single IR fcmp being used by two selects. During selection,
  // to ensure that there can be no clobbering of nzcv between the fcmp and the
  // csel, we have to generate an fcmp immediately before each csel is
  // selected.
  // However, often we can essentially CSE these together later in MachineCSE.
  // This doesn't work though if there are unrelated flag-setting instructions
  // in between the two FCMPs. In this case, the SUBS defines NZCV
  // but it doesn't have any users, being overwritten by the second FCMP.
  //
  // 2)
  // The instruction selector always emits the flag-setting variant of ADC/SBC
  // while selecting G_UADDE/G_SADDE/G_USUBE/G_SSUBE. If the carry-out of these
  // instructions is never used, we can switch to the non-flag-setting variant.

  bool Changed = false;
  auto &MF = *MBB.getParent();
  auto &Subtarget = MF.getSubtarget();
  const auto &TII = Subtarget.getInstrInfo();
  auto TRI = Subtarget.getRegisterInfo();
  auto RBI = Subtarget.getRegBankInfo();
  auto &MRI = MF.getRegInfo();

  LiveRegUnits LRU(*MBB.getParent()->getSubtarget().getRegisterInfo());
  LRU.addLiveOuts(MBB);

  for (auto &II : instructionsWithoutDebug(MBB.rbegin(), MBB.rend())) {
    bool NZCVDead = LRU.available(AArch64::NZCV);
    if (NZCVDead && II.definesRegister(AArch64::NZCV, /*TRI=*/nullptr)) {
      // The instruction defines NZCV, but NZCV is dead.
      unsigned NewOpc = getNonFlagSettingVariant(II.getOpcode());
      int DeadNZCVIdx =
          II.findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr);
      if (DeadNZCVIdx != -1) {
        if (NewOpc) {
          // If there is an equivalent non-flag-setting op, we convert.
          LLVM_DEBUG(dbgs() << "Post-select optimizer: converting flag-setting "
                               "op: "
                            << II);
          II.setDesc(TII->get(NewOpc));
          II.removeOperand(DeadNZCVIdx);
          // Changing the opcode can result in differing regclass requirements,
          // e.g. SUBSWri uses gpr32 for the dest, whereas SUBWri uses gpr32sp.
          // Constrain the regclasses, possibly introducing a copy.
          constrainOperandRegClass(MF, *TRI, MRI, *TII, *RBI, II, II.getDesc(),
                                   II.getOperand(0), 0);
          Changed |= true;
        } else {
          // Otherwise, we just set the nzcv imp-def operand to be dead, so the
          // peephole optimizations can optimize them further.
          II.getOperand(DeadNZCVIdx).setIsDead();
        }
      }
    }
    LRU.stepBackward(II);
  }
  return Changed;
}

bool AArch64PostSelectOptimize::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  assert(MF.getProperties().hasProperty(
             MachineFunctionProperties::Property::Selected) &&
         "Expected a selected MF");

  bool Changed = false;
  for (auto &BB : MF) {
    Changed |= optimizeNZCVDefs(BB);
    Changed |= doPeepholeOpts(BB);
  }
  return Changed;
}

char AArch64PostSelectOptimize::ID = 0;
INITIALIZE_PASS_BEGIN(AArch64PostSelectOptimize, DEBUG_TYPE,
                      "Optimize AArch64 selected instructions",
                      false, false)
INITIALIZE_PASS_END(AArch64PostSelectOptimize, DEBUG_TYPE,
                    "Optimize AArch64 selected instructions", false,
                    false)

namespace llvm {
FunctionPass *createAArch64PostSelectOptimize() {
  return new AArch64PostSelectOptimize();
}
} // end namespace llvm
