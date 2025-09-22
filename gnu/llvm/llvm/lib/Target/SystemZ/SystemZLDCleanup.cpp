//===-- SystemZLDCleanup.cpp - Clean up local-dynamic TLS accesses --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass combines multiple accesses to local-dynamic TLS variables so that
// the TLS base address for the module is only fetched once per execution path
// through the function.
//
//===----------------------------------------------------------------------===//

#include "SystemZMachineFunctionInfo.h"
#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

class SystemZLDCleanup : public MachineFunctionPass {
public:
  static char ID;
  SystemZLDCleanup() : MachineFunctionPass(ID), TII(nullptr), MF(nullptr) {
    initializeSystemZLDCleanupPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  bool VisitNode(MachineDomTreeNode *Node, unsigned TLSBaseAddrReg);
  MachineInstr *ReplaceTLSCall(MachineInstr *I, unsigned TLSBaseAddrReg);
  MachineInstr *SetRegister(MachineInstr *I, unsigned *TLSBaseAddrReg);

  const SystemZInstrInfo *TII;
  MachineFunction *MF;
};

char SystemZLDCleanup::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(SystemZLDCleanup, "systemz-ld-cleanup",
                "SystemZ Local Dynamic TLS Access Clean-up", false, false)

FunctionPass *llvm::createSystemZLDCleanupPass(SystemZTargetMachine &TM) {
  return new SystemZLDCleanup();
}

void SystemZLDCleanup::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool SystemZLDCleanup::runOnMachineFunction(MachineFunction &F) {
  if (skipFunction(F.getFunction()))
    return false;

  TII = F.getSubtarget<SystemZSubtarget>().getInstrInfo();
  MF = &F;

  SystemZMachineFunctionInfo* MFI = F.getInfo<SystemZMachineFunctionInfo>();
  if (MFI->getNumLocalDynamicTLSAccesses() < 2) {
    // No point folding accesses if there isn't at least two.
    return false;
  }

  MachineDominatorTree *DT =
      &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  return VisitNode(DT->getRootNode(), 0);
}

// Visit the dominator subtree rooted at Node in pre-order.
// If TLSBaseAddrReg is non-null, then use that to replace any
// TLS_LDCALL instructions. Otherwise, create the register
// when the first such instruction is seen, and then use it
// as we encounter more instructions.
bool SystemZLDCleanup::VisitNode(MachineDomTreeNode *Node,
                                 unsigned TLSBaseAddrReg) {
  MachineBasicBlock *BB = Node->getBlock();
  bool Changed = false;

  // Traverse the current block.
  for (auto I = BB->begin(), E = BB->end(); I != E; ++I) {
    switch (I->getOpcode()) {
      case SystemZ::TLS_LDCALL:
        if (TLSBaseAddrReg)
          I = ReplaceTLSCall(&*I, TLSBaseAddrReg);
        else
          I = SetRegister(&*I, &TLSBaseAddrReg);
        Changed = true;
        break;
      default:
        break;
    }
  }

  // Visit the children of this block in the dominator tree.
  for (auto &N : *Node)
    Changed |= VisitNode(N, TLSBaseAddrReg);

  return Changed;
}

// Replace the TLS_LDCALL instruction I with a copy from TLSBaseAddrReg,
// returning the new instruction.
MachineInstr *SystemZLDCleanup::ReplaceTLSCall(MachineInstr *I,
                                               unsigned TLSBaseAddrReg) {
  // Insert a Copy from TLSBaseAddrReg to R2.
  MachineInstr *Copy = BuildMI(*I->getParent(), I, I->getDebugLoc(),
                               TII->get(TargetOpcode::COPY), SystemZ::R2D)
                               .addReg(TLSBaseAddrReg);

  // Erase the TLS_LDCALL instruction.
  I->eraseFromParent();

  return Copy;
}

// Create a virtual register in *TLSBaseAddrReg, and populate it by
// inserting a copy instruction after I. Returns the new instruction.
MachineInstr *SystemZLDCleanup::SetRegister(MachineInstr *I,
                                            unsigned *TLSBaseAddrReg) {
  // Create a virtual register for the TLS base address.
  MachineRegisterInfo &RegInfo = MF->getRegInfo();
  *TLSBaseAddrReg = RegInfo.createVirtualRegister(&SystemZ::GR64BitRegClass);

  // Insert a copy from R2 to TLSBaseAddrReg.
  MachineInstr *Next = I->getNextNode();
  MachineInstr *Copy = BuildMI(*I->getParent(), Next, I->getDebugLoc(),
                               TII->get(TargetOpcode::COPY), *TLSBaseAddrReg)
                               .addReg(SystemZ::R2D);

  return Copy;
}

