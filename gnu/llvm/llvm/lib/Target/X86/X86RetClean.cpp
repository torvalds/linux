//===-- X86RetClean.cpp - Clean Retaddr off stack upon function return ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines a function pass that clears the ret-address from
/// the top of the stack, immediately upon return to the caller, the goal
/// is remove this subtle but powerful info-leak which hints at the
/// address space location of the lower level libraries.
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define RETCLEAN_DESC "X86 Ret Clean"
#define RETCLEAN_NAME "x86-ret-clean"

#define DEBUG_TYPE RETCLEAN_NAME

// Toggle with cc1 option: -mllvm -x86-ret-clean=<true|false>
static cl::opt<bool> RetClean(
    "x86-ret-clean", cl::Hidden,
    cl::desc("clean return address off stack after call"),
    cl::init(false));

namespace {
class RetCleanPass : public MachineFunctionPass {

public:
  static char ID;

  StringRef getPassName() const override { return RETCLEAN_DESC; }

  RetCleanPass()
      : MachineFunctionPass(ID) {}

  /// Loop over all the instructions and replace ret with ret+clean
  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  bool fixupInstruction(MachineFunction &MF, MachineBasicBlock &MBB,
                        MachineInstr &MI);
};
char RetCleanPass::ID = 0;
} // namespace

FunctionPass *llvm::createX86RetCleanPass() {
  return new RetCleanPass();
}

bool RetCleanPass::fixupInstruction(MachineFunction &MF,
                               MachineBasicBlock &MBB,
                               MachineInstr &MI) {

  const X86InstrInfo *TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
  bool Is64Bit = MF.getTarget().getTargetTriple().getArch() == Triple::x86_64;
  unsigned Opc = Is64Bit ? X86::MOV64mi32 : X86::MOV32mi;
  unsigned Offset = Is64Bit ? -8 : -4;
  Register SPReg = Is64Bit ? X86::RSP : X86::ESP;

  // add "movq $0, -8(%rsp)" (or similar) in caller, to clear the
  // ret-addr info-leak off the stack
  addRegOffset(BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(Opc)),
    SPReg, false, Offset)
    .addImm(0);
  return true;
}

bool RetCleanPass::runOnMachineFunction(MachineFunction &MF) {
  if (!RetClean)
    return false;

  bool modified = false;

  // If a setjmp-like function is called by this function, we should not clean
  if (MF.exposesReturnsTwice())
    return false;

  for (auto &MBB : MF) {
    std::vector<MachineInstr*> fixups;
    bool foundcall = false;

    for (auto &MI : MBB) {
      if (MI.isCall()) {
        foundcall = true;	// queue the insert before the next MI
      } else if (foundcall) {
         fixups.push_back(&MI);
         foundcall = false;
      }
    }
    for (auto *fixup : fixups)
      modified |= fixupInstruction(MF, MBB, *fixup);
  }
  return modified;
}
