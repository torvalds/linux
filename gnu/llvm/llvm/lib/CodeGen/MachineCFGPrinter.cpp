//===- MachineCFGPrinter.cpp - DOT Printer for Machine Functions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//===----------------------------------------------------------------------===//
//
// This file defines the `-dot-machine-cfg` analysis pass, which emits
// Machine Function in DOT format in file titled `<prefix>.<function-name>.dot.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineCFGPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/GraphWriter.h"

using namespace llvm;

#define DEBUG_TYPE "dot-machine-cfg"

static cl::opt<std::string>
    MCFGFuncName("mcfg-func-name", cl::Hidden,
                 cl::desc("The name of a function (or its substring)"
                          " whose CFG is viewed/printed."));

static cl::opt<std::string> MCFGDotFilenamePrefix(
    "mcfg-dot-filename-prefix", cl::Hidden,
    cl::desc("The prefix used for the Machine CFG dot file names."));

static cl::opt<bool>
    CFGOnly("dot-mcfg-only", cl::init(false), cl::Hidden,
            cl::desc("Print only the CFG without blocks body"));

static void writeMCFGToDotFile(MachineFunction &MF) {
  std::string Filename =
      (MCFGDotFilenamePrefix + "." + MF.getName() + ".dot").str();
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);

  DOTMachineFuncInfo MCFGInfo(&MF);

  if (!EC)
    WriteGraph(File, &MCFGInfo, CFGOnly);
  else
    errs() << "  error opening file for writing!";
  errs() << '\n';
}

namespace {

class MachineCFGPrinter : public MachineFunctionPass {
public:
  static char ID;

  MachineCFGPrinter();

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

} // namespace

char MachineCFGPrinter::ID = 0;

char &llvm::MachineCFGPrinterID = MachineCFGPrinter::ID;

INITIALIZE_PASS(MachineCFGPrinter, DEBUG_TYPE, "Machine CFG Printer Pass",
                false, true)

/// Default construct and initialize the pass.
MachineCFGPrinter::MachineCFGPrinter() : MachineFunctionPass(ID) {
  initializeMachineCFGPrinterPass(*PassRegistry::getPassRegistry());
}

bool MachineCFGPrinter::runOnMachineFunction(MachineFunction &MF) {
  if (!MCFGFuncName.empty() && !MF.getName().contains(MCFGFuncName))
    return false;
  errs() << "Writing Machine CFG for function ";
  errs().write_escaped(MF.getName()) << '\n';

  writeMCFGToDotFile(MF);
  return false;
}
