//===----- PostRAHazardRecognizer.cpp - hazard recognizer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This runs the hazard recognizer and emits noops when necessary.  This
/// gives targets a way to run the hazard recognizer without running one of
/// the schedulers.  Example use cases for this pass would be:
///
/// - Targets that need the hazard recognizer to be run at -O0.
/// - Targets that want to guarantee that hazards at the beginning of
///   scheduling regions are handled correctly.  The post-RA scheduler is
///   a top-down scheduler, but when there are multiple scheduling regions
///   in a basic block, it visits the regions in bottom-up order.  This
///   makes it impossible for the scheduler to gauranttee it can correctly
///   handle hazards at the beginning of scheduling regions.
///
/// This pass traverses all the instructions in a program in top-down order.
/// In contrast to the instruction scheduling passes, this pass never resets
/// the hazard recognizer to ensure it can correctly handles noop hazards at
/// the beginning of blocks.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/ScheduleHazardRecognizer.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
using namespace llvm;

#define DEBUG_TYPE "post-RA-hazard-rec"

STATISTIC(NumNoops, "Number of noops inserted");

namespace {
  class PostRAHazardRecognizer : public MachineFunctionPass {

  public:
    static char ID;
    PostRAHazardRecognizer() : MachineFunctionPass(ID) {}

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    bool runOnMachineFunction(MachineFunction &Fn) override;

  };
  char PostRAHazardRecognizer::ID = 0;

}

char &llvm::PostRAHazardRecognizerID = PostRAHazardRecognizer::ID;

INITIALIZE_PASS(PostRAHazardRecognizer, DEBUG_TYPE,
                "Post RA hazard recognizer", false, false)

bool PostRAHazardRecognizer::runOnMachineFunction(MachineFunction &Fn) {
  const TargetInstrInfo *TII = Fn.getSubtarget().getInstrInfo();
  std::unique_ptr<ScheduleHazardRecognizer> HazardRec(
      TII->CreateTargetPostRAHazardRecognizer(Fn));

  // Return if the target has not implemented a hazard recognizer.
  if (!HazardRec)
    return false;

  // Loop over all of the basic blocks
  bool Changed = false;
  for (auto &MBB : Fn) {
    // We do not call HazardRec->reset() here to make sure we are handling noop
    // hazards at the start of basic blocks.
    for (MachineInstr &MI : MBB) {
      // If we need to emit noops prior to this instruction, then do so.
      unsigned NumPreNoops = HazardRec->PreEmitNoops(&MI);
      HazardRec->EmitNoops(NumPreNoops);
      TII->insertNoops(MBB, MachineBasicBlock::iterator(MI), NumPreNoops);
      NumNoops += NumPreNoops;
      if (NumPreNoops)
        Changed = true;

      HazardRec->EmitInstruction(&MI);
      if (HazardRec->atIssueLimit()) {
        HazardRec->AdvanceCycle();
      }
    }
  }
  return Changed;
}
