//===-- WebAssemblyReplacePhysRegs.cpp - Replace phys regs with virt regs -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that replaces physical registers with
/// virtual registers.
///
/// LLVM expects certain physical registers, such as a stack pointer. However,
/// WebAssembly doesn't actually have such physical registers. This pass is run
/// once LLVM no longer needs these registers, and replaces them with virtual
/// registers, so they can participate in register stackifying and coloring in
/// the normal way.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-replace-phys-regs"

namespace {
class WebAssemblyReplacePhysRegs final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyReplacePhysRegs() : MachineFunctionPass(ID) {}

private:
  StringRef getPassName() const override {
    return "WebAssembly Replace Physical Registers";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char WebAssemblyReplacePhysRegs::ID = 0;
INITIALIZE_PASS(WebAssemblyReplacePhysRegs, DEBUG_TYPE,
                "Replace physical registers with virtual registers", false,
                false)

FunctionPass *llvm::createWebAssemblyReplacePhysRegs() {
  return new WebAssemblyReplacePhysRegs();
}

bool WebAssemblyReplacePhysRegs::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Replace Physical Registers **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  auto &TRI = *MF.getSubtarget<WebAssemblySubtarget>().getRegisterInfo();
  bool Changed = false;

  assert(!mustPreserveAnalysisID(LiveIntervalsID) &&
         "LiveIntervals shouldn't be active yet!");

  for (unsigned PReg = WebAssembly::NoRegister + 1;
       PReg < WebAssembly::NUM_TARGET_REGS; ++PReg) {
    // Skip fake registers that are never used explicitly.
    if (PReg == WebAssembly::VALUE_STACK || PReg == WebAssembly::ARGUMENTS)
      continue;

    // Replace explicit uses of the physical register with a virtual register.
    const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(PReg);
    unsigned VReg = WebAssembly::NoRegister;
    for (MachineOperand &MO :
         llvm::make_early_inc_range(MRI.reg_operands(PReg))) {
      if (!MO.isImplicit()) {
        if (VReg == WebAssembly::NoRegister) {
          VReg = MRI.createVirtualRegister(RC);
          if (PReg == TRI.getFrameRegister(MF)) {
            auto FI = MF.getInfo<WebAssemblyFunctionInfo>();
            assert(!FI->isFrameBaseVirtual());
            FI->setFrameBaseVreg(VReg);
            LLVM_DEBUG({
              dbgs() << "replacing preg " << PReg << " with " << VReg << " ("
                     << Register::virtReg2Index(VReg) << ")\n";
            });
          }
        }
        MO.setReg(VReg);
        Changed = true;
      }
    }
  }

  return Changed;
}
