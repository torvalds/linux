//===-- NVPTXPeephole.cpp - NVPTX Peephole Optimiztions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// In NVPTX, NVPTXFrameLowering will emit following instruction at the beginning
// of a MachineFunction.
//
//   mov %SPL, %depot
//   cvta.local %SP, %SPL
//
// Because Frame Index is a generic address and alloca can only return generic
// pointer, without this pass the instructions producing alloca'ed address will
// be based on %SP. NVPTXLowerAlloca tends to help replace store and load on
// this address with their .local versions, but this may introduce a lot of
// cvta.to.local instructions. Performance can be improved if we avoid casting
// address back and forth and directly calculate local address based on %SPL.
// This peephole pass optimizes these cases, for example
//
// It will transform the following pattern
//    %0 = LEA_ADDRi64 %VRFrame64, 4
//    %1 = cvta_to_local_64 %0
//
// into
//    %1 = LEA_ADDRi64 %VRFrameLocal64, 4
//
// %VRFrameLocal64 is the virtual register name of %SPL
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "NVPTXRegisterInfo.h"
#include "NVPTXSubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "nvptx-peephole"

namespace llvm {
void initializeNVPTXPeepholePass(PassRegistry &);
}

namespace {
struct NVPTXPeephole : public MachineFunctionPass {
 public:
  static char ID;
  NVPTXPeephole() : MachineFunctionPass(ID) {
    initializeNVPTXPeepholePass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return "NVPTX optimize redundant cvta.to.local instruction";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
}

char NVPTXPeephole::ID = 0;

INITIALIZE_PASS(NVPTXPeephole, "nvptx-peephole", "NVPTX Peephole", false, false)

static bool isCVTAToLocalCombinationCandidate(MachineInstr &Root) {
  auto &MBB = *Root.getParent();
  auto &MF = *MBB.getParent();
  // Check current instruction is cvta.to.local
  if (Root.getOpcode() != NVPTX::cvta_to_local_64 &&
      Root.getOpcode() != NVPTX::cvta_to_local)
    return false;

  auto &Op = Root.getOperand(1);
  const auto &MRI = MF.getRegInfo();
  MachineInstr *GenericAddrDef = nullptr;
  if (Op.isReg() && Op.getReg().isVirtual()) {
    GenericAddrDef = MRI.getUniqueVRegDef(Op.getReg());
  }

  // Check the register operand is uniquely defined by LEA_ADDRi instruction
  if (!GenericAddrDef || GenericAddrDef->getParent() != &MBB ||
      (GenericAddrDef->getOpcode() != NVPTX::LEA_ADDRi64 &&
       GenericAddrDef->getOpcode() != NVPTX::LEA_ADDRi)) {
    return false;
  }

  const NVPTXRegisterInfo *NRI =
      MF.getSubtarget<NVPTXSubtarget>().getRegisterInfo();

  // Check the LEA_ADDRi operand is Frame index
  auto &BaseAddrOp = GenericAddrDef->getOperand(1);
  if (BaseAddrOp.isReg() && BaseAddrOp.getReg() == NRI->getFrameRegister(MF)) {
    return true;
  }

  return false;
}

static void CombineCVTAToLocal(MachineInstr &Root) {
  auto &MBB = *Root.getParent();
  auto &MF = *MBB.getParent();
  const auto &MRI = MF.getRegInfo();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  auto &Prev = *MRI.getUniqueVRegDef(Root.getOperand(1).getReg());

  const NVPTXRegisterInfo *NRI =
      MF.getSubtarget<NVPTXSubtarget>().getRegisterInfo();

  MachineInstrBuilder MIB =
      BuildMI(MF, Root.getDebugLoc(), TII->get(Prev.getOpcode()),
              Root.getOperand(0).getReg())
          .addReg(NRI->getFrameLocalRegister(MF))
          .add(Prev.getOperand(2));

  MBB.insert((MachineBasicBlock::iterator)&Root, MIB);

  // Check if MRI has only one non dbg use, which is Root
  if (MRI.hasOneNonDBGUse(Prev.getOperand(0).getReg())) {
    Prev.eraseFromParent();
  }
  Root.eraseFromParent();
}

bool NVPTXPeephole::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  bool Changed = false;
  // Loop over all of the basic blocks.
  for (auto &MBB : MF) {
    // Traverse the basic block.
    auto BlockIter = MBB.begin();

    while (BlockIter != MBB.end()) {
      auto &MI = *BlockIter++;
      if (isCVTAToLocalCombinationCandidate(MI)) {
        CombineCVTAToLocal(MI);
        Changed = true;
      }
    }  // Instruction
  }    // Basic Block

  const NVPTXRegisterInfo *NRI =
      MF.getSubtarget<NVPTXSubtarget>().getRegisterInfo();

  // Remove unnecessary %VRFrame = cvta.local %VRFrameLocal
  const auto &MRI = MF.getRegInfo();
  if (MRI.use_empty(NRI->getFrameRegister(MF))) {
    if (auto MI = MRI.getUniqueVRegDef(NRI->getFrameRegister(MF))) {
      MI->eraseFromParent();
    }
  }

  return Changed;
}

MachineFunctionPass *llvm::createNVPTXPeephole() { return new NVPTXPeephole(); }
