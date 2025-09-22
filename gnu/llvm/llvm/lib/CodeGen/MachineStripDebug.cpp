//===- MachineStripDebug.cpp - Strip debug info ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This removes debug info from everything. It can be used to ensure
/// tests can be debugified without affecting the output MIR.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/Debugify.h"

#define DEBUG_TYPE "mir-strip-debug"

using namespace llvm;

namespace {
cl::opt<bool>
    OnlyDebugifiedDefault("mir-strip-debugify-only",
                          cl::desc("Should mir-strip-debug only strip debug "
                                   "info from debugified modules by default"),
                          cl::init(true));

struct StripDebugMachineModule : public ModulePass {
  bool runOnModule(Module &M) override {
    if (OnlyDebugified) {
      NamedMDNode *DebugifyMD = M.getNamedMetadata("llvm.debugify");
      if (!DebugifyMD) {
        LLVM_DEBUG(dbgs() << "Not stripping debug info"
                             " (debugify metadata not found)?\n");
        return false;
      }
    }

    MachineModuleInfo &MMI =
        getAnalysis<MachineModuleInfoWrapperPass>().getMMI();

    bool Changed = false;
    for (Function &F : M.functions()) {
      MachineFunction *MaybeMF = MMI.getMachineFunction(F);
      if (!MaybeMF)
        continue;
      MachineFunction &MF = *MaybeMF;
      for (MachineBasicBlock &MBB : MF) {
        for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
          if (MI.isDebugInstr()) {
            // FIXME: We should remove all of them. However, AArch64 emits an
            //        invalid `DBG_VALUE $lr` with only one operand instead of
            //        the usual three and has a test that depends on it's
            //        preservation. Preserve it for now.
            if (MI.getNumOperands() > 1) {
              LLVM_DEBUG(dbgs() << "Removing debug instruction " << MI);
              MBB.erase(&MI);
              Changed |= true;
              continue;
            }
          }
          if (MI.getDebugLoc()) {
            LLVM_DEBUG(dbgs() << "Removing location " << MI);
            MI.setDebugLoc(DebugLoc());
            Changed |= true;
            continue;
          }
          LLVM_DEBUG(dbgs() << "Keeping " << MI);
        }
      }
    }

    Changed |= stripDebugifyMetadata(M);

    return Changed;
  }

  StripDebugMachineModule() : StripDebugMachineModule(OnlyDebugifiedDefault) {}
  StripDebugMachineModule(bool OnlyDebugified)
      : ModulePass(ID), OnlyDebugified(OnlyDebugified) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesCFG();
  }

  static char ID; // Pass identification.

protected:
  bool OnlyDebugified;
};
char StripDebugMachineModule::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(StripDebugMachineModule, DEBUG_TYPE,
                      "Machine Strip Debug Module", false, false)
INITIALIZE_PASS_END(StripDebugMachineModule, DEBUG_TYPE,
                    "Machine Strip Debug Module", false, false)

ModulePass *llvm::createStripDebugMachineModulePass(bool OnlyDebugified) {
  return new StripDebugMachineModule(OnlyDebugified);
}
