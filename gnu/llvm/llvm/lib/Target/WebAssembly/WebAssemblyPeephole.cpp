//===-- WebAssemblyPeephole.cpp - WebAssembly Peephole Optimiztions -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Late peephole optimizations for WebAssembly.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-peephole"

static cl::opt<bool> DisableWebAssemblyFallthroughReturnOpt(
    "disable-wasm-fallthrough-return-opt", cl::Hidden,
    cl::desc("WebAssembly: Disable fallthrough-return optimizations."),
    cl::init(false));

namespace {
class WebAssemblyPeephole final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly late peephole optimizer";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID;
  WebAssemblyPeephole() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyPeephole::ID = 0;
INITIALIZE_PASS(WebAssemblyPeephole, DEBUG_TYPE,
                "WebAssembly peephole optimizations", false, false)

FunctionPass *llvm::createWebAssemblyPeephole() {
  return new WebAssemblyPeephole();
}

/// If desirable, rewrite NewReg to a drop register.
static bool maybeRewriteToDrop(unsigned OldReg, unsigned NewReg,
                               MachineOperand &MO, WebAssemblyFunctionInfo &MFI,
                               MachineRegisterInfo &MRI) {
  bool Changed = false;
  if (OldReg == NewReg) {
    Changed = true;
    Register NewReg = MRI.createVirtualRegister(MRI.getRegClass(OldReg));
    MO.setReg(NewReg);
    MO.setIsDead();
    MFI.stackifyVReg(MRI, NewReg);
  }
  return Changed;
}

static bool maybeRewriteToFallthrough(MachineInstr &MI, MachineBasicBlock &MBB,
                                      const MachineFunction &MF,
                                      WebAssemblyFunctionInfo &MFI,
                                      MachineRegisterInfo &MRI,
                                      const WebAssemblyInstrInfo &TII) {
  if (DisableWebAssemblyFallthroughReturnOpt)
    return false;
  if (&MBB != &MF.back())
    return false;

  MachineBasicBlock::iterator End = MBB.end();
  --End;
  assert(End->getOpcode() == WebAssembly::END_FUNCTION);
  --End;
  if (&MI != &*End)
    return false;

  for (auto &MO : MI.explicit_operands()) {
    // If the operand isn't stackified, insert a COPY to read the operands and
    // stackify them.
    Register Reg = MO.getReg();
    if (!MFI.isVRegStackified(Reg)) {
      unsigned CopyLocalOpc;
      const TargetRegisterClass *RegClass = MRI.getRegClass(Reg);
      CopyLocalOpc = WebAssembly::getCopyOpcodeForRegClass(RegClass);
      Register NewReg = MRI.createVirtualRegister(RegClass);
      BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(CopyLocalOpc), NewReg)
          .addReg(Reg);
      MO.setReg(NewReg);
      MFI.stackifyVReg(MRI, NewReg);
    }
  }

  MI.setDesc(TII.get(WebAssembly::FALLTHROUGH_RETURN));
  return true;
}

bool WebAssemblyPeephole::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** Peephole **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  MachineRegisterInfo &MRI = MF.getRegInfo();
  WebAssemblyFunctionInfo &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  const WebAssemblyTargetLowering &TLI =
      *MF.getSubtarget<WebAssemblySubtarget>().getTargetLowering();
  auto &LibInfo =
      getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(MF.getFunction());
  bool Changed = false;

  for (auto &MBB : MF)
    for (auto &MI : MBB)
      switch (MI.getOpcode()) {
      default:
        break;
      case WebAssembly::CALL: {
        MachineOperand &Op1 = MI.getOperand(1);
        if (Op1.isSymbol()) {
          StringRef Name(Op1.getSymbolName());
          if (Name == TLI.getLibcallName(RTLIB::MEMCPY) ||
              Name == TLI.getLibcallName(RTLIB::MEMMOVE) ||
              Name == TLI.getLibcallName(RTLIB::MEMSET)) {
            LibFunc Func;
            if (LibInfo.getLibFunc(Name, Func)) {
              const auto &Op2 = MI.getOperand(2);
              if (!Op2.isReg())
                report_fatal_error("Peephole: call to builtin function with "
                                   "wrong signature, not consuming reg");
              MachineOperand &MO = MI.getOperand(0);
              Register OldReg = MO.getReg();
              Register NewReg = Op2.getReg();

              if (MRI.getRegClass(NewReg) != MRI.getRegClass(OldReg))
                report_fatal_error("Peephole: call to builtin function with "
                                   "wrong signature, from/to mismatch");
              Changed |= maybeRewriteToDrop(OldReg, NewReg, MO, MFI, MRI);
            }
          }
        }
        break;
      }
      // Optimize away an explicit void return at the end of the function.
      case WebAssembly::RETURN:
        Changed |= maybeRewriteToFallthrough(MI, MBB, MF, MFI, MRI, TII);
        break;
      }

  return Changed;
}
