//===- MIRPrintingPass.cpp - Pass that prints out using the MIR format ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a pass that prints out the LLVM module using the MIR
// serialization format.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

PreservedAnalyses PrintMIRPreparePass::run(Module &M, ModuleAnalysisManager &) {
  printMIR(OS, M);
  return PreservedAnalyses::all();
}

PreservedAnalyses PrintMIRPass::run(MachineFunction &MF,
                                    MachineFunctionAnalysisManager &) {
  printMIR(OS, MF);
  return PreservedAnalyses::all();
}

namespace {

/// This pass prints out the LLVM IR to an output stream using the MIR
/// serialization format.
struct MIRPrintingPass : public MachineFunctionPass {
  static char ID;
  raw_ostream &OS;
  std::string MachineFunctions;

  MIRPrintingPass() : MachineFunctionPass(ID), OS(dbgs()) {}
  MIRPrintingPass(raw_ostream &OS) : MachineFunctionPass(ID), OS(OS) {}

  StringRef getPassName() const override { return "MIR Printing Pass"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    std::string Str;
    raw_string_ostream StrOS(Str);
    printMIR(StrOS, MF);
    MachineFunctions.append(Str);
    return false;
  }

  bool doFinalization(Module &M) override {
    printMIR(OS, M);
    OS << MachineFunctions;
    return false;
  }
};

char MIRPrintingPass::ID = 0;

} // end anonymous namespace

char &llvm::MIRPrintingPassID = MIRPrintingPass::ID;
INITIALIZE_PASS(MIRPrintingPass, "mir-printer", "MIR Printer", false, false)

namespace llvm {

MachineFunctionPass *createPrintMIRPass(raw_ostream &OS) {
  return new MIRPrintingPass(OS);
}

} // end namespace llvm
