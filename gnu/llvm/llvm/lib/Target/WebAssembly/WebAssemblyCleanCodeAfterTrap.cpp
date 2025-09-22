//===-- WebAssemblyCleanCodeAfterTrap.cpp - Clean Code After Trap ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file remove instruction after trap.
/// ``llvm.trap`` will be convert as ``unreachable`` which is terminator.
/// Instruction after terminator will cause validation failed.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "WebAssemblyUtilities.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-clean-code-after-trap"

namespace {
class WebAssemblyCleanCodeAfterTrap final : public MachineFunctionPass {
public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyCleanCodeAfterTrap() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "WebAssembly Clean Code After Trap";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};
} // end anonymous namespace

char WebAssemblyCleanCodeAfterTrap::ID = 0;
INITIALIZE_PASS(WebAssemblyCleanCodeAfterTrap, DEBUG_TYPE,
                "WebAssembly Clean Code After Trap", false, false)

FunctionPass *llvm::createWebAssemblyCleanCodeAfterTrap() {
  return new WebAssemblyCleanCodeAfterTrap();
}

bool WebAssemblyCleanCodeAfterTrap::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG({
    dbgs() << "********** CleanCodeAfterTrap **********\n"
           << "********** Function: " << MF.getName() << '\n';
  });

  bool Changed = false;

  for (MachineBasicBlock &BB : MF) {
    bool HasTerminator = false;
    llvm::SmallVector<MachineInstr *> RemoveMI{};
    for (MachineInstr &MI : BB) {
      if (HasTerminator)
        RemoveMI.push_back(&MI);
      if (MI.hasProperty(MCID::Trap) && MI.isTerminator())
        HasTerminator = true;
    }
    if (!RemoveMI.empty()) {
      Changed = true;
      LLVM_DEBUG({
        for (MachineInstr *MI : RemoveMI) {
          llvm::dbgs() << "* remove ";
          MI->print(llvm::dbgs());
        }
      });
      for (MachineInstr *MI : RemoveMI)
        MI->eraseFromParent();
    }
  }
  return Changed;
}
