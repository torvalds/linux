//===-- CFGuardLongjmp.cpp - Longjmp symbols for CFGuard --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a machine function pass to insert a symbol after each
/// call to _setjmp and store this in the MachineFunction's LongjmpTargets
/// vector. This will be used to emit the table of valid longjmp targets used
/// by Control Flow Guard.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "cfguard-longjmp"

STATISTIC(CFGuardLongjmpTargets,
          "Number of Control Flow Guard longjmp targets");

namespace {

/// MachineFunction pass to insert a symbol after each call to _setjmp and store
/// this in the MachineFunction's LongjmpTargets vector.
class CFGuardLongjmp : public MachineFunctionPass {
public:
  static char ID;

  CFGuardLongjmp() : MachineFunctionPass(ID) {
    initializeCFGuardLongjmpPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Control Flow Guard longjmp targets";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char CFGuardLongjmp::ID = 0;

INITIALIZE_PASS(CFGuardLongjmp, "CFGuardLongjmp",
                "Insert symbols at valid longjmp targets for /guard:cf", false,
                false)
FunctionPass *llvm::createCFGuardLongjmpPass() { return new CFGuardLongjmp(); }

bool CFGuardLongjmp::runOnMachineFunction(MachineFunction &MF) {

  // Skip modules for which the cfguard flag is not set.
  if (!MF.getFunction().getParent()->getModuleFlag("cfguard"))
    return false;

  // Skip functions that do not have calls to _setjmp.
  if (!MF.getFunction().callsFunctionThatReturnsTwice())
    return false;

  SmallVector<MachineInstr *, 8> SetjmpCalls;

  // Iterate over all instructions in the function and add calls to functions
  // that return twice to the list of targets.
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {

      // Skip instructions that are not calls.
      if (!MI.isCall() || MI.getNumOperands() < 1)
        continue;

      // Iterate over operands to find calls to global functions.
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isGlobal())
          continue;

        auto *F = dyn_cast<Function>(MO.getGlobal());
        if (!F)
          continue;

        // If the instruction calls a function that returns twice, add
        // it to the list of targets.
        if (F->hasFnAttribute(Attribute::ReturnsTwice)) {
          SetjmpCalls.push_back(&MI);
          break;
        }
      }
    }
  }

  if (SetjmpCalls.empty())
    return false;

  unsigned SetjmpNum = 0;

  // For each possible target, create a new symbol and insert it immediately
  // after the call to setjmp. Add this symbol to the MachineFunction's list
  // of longjmp targets.
  for (MachineInstr *Setjmp : SetjmpCalls) {
    SmallString<128> SymbolName;
    raw_svector_ostream(SymbolName) << "$cfgsj_" << MF.getName() << SetjmpNum++;
    MCSymbol *SjSymbol = MF.getContext().getOrCreateSymbol(SymbolName);

    Setjmp->setPostInstrSymbol(MF, SjSymbol);
    MF.addLongjmpTarget(SjSymbol);
    CFGuardLongjmpTargets++;
  }

  return true;
}
