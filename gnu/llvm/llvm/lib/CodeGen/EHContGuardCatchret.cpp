//===-- EHContGuardCatchret.cpp - Catchret target symbols -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a machine function pass to insert a symbol before each
/// valid catchret target and store this in the MachineFunction's
/// CatchRetTargets vector. This will be used to emit the table of valid targets
/// used by EHCont Guard.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "ehcontguard-catchret"

STATISTIC(EHContGuardCatchretTargets,
          "Number of EHCont Guard catchret targets");

namespace {

/// MachineFunction pass to insert a symbol before each valid catchret target
/// and store these in the MachineFunction's CatchRetTargets vector.
class EHContGuardCatchret : public MachineFunctionPass {
public:
  static char ID;

  EHContGuardCatchret() : MachineFunctionPass(ID) {
    initializeEHContGuardCatchretPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "EH Cont Guard catchret targets";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
};

} // end anonymous namespace

char EHContGuardCatchret::ID = 0;

INITIALIZE_PASS(EHContGuardCatchret, "EHContGuardCatchret",
                "Insert symbols at valid catchret targets for /guard:ehcont",
                false, false)
FunctionPass *llvm::createEHContGuardCatchretPass() {
  return new EHContGuardCatchret();
}

bool EHContGuardCatchret::runOnMachineFunction(MachineFunction &MF) {

  // Skip modules for which the ehcontguard flag is not set.
  if (!MF.getFunction().getParent()->getModuleFlag("ehcontguard"))
    return false;

  // Skip functions that do not have catchret
  if (!MF.hasEHCatchret())
    return false;

  bool Result = false;

  for (MachineBasicBlock &MBB : MF) {
    if (MBB.isEHCatchretTarget()) {
      MF.addCatchretTarget(MBB.getEHCatchretSymbol());
      EHContGuardCatchretTargets++;
      Result = true;
    }
  }

  return Result;
}
