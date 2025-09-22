//===- TailDuplication.cpp - Duplicate blocks into predecessors' tails ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This pass duplicates basic blocks ending in unconditional branches
/// into the tails of their predecessors, using the TailDuplicator utility
/// class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/LazyMachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MBFIWrapper.h"
#include "llvm/CodeGen/MachineBranchProbabilityInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TailDuplicator.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"

using namespace llvm;

#define DEBUG_TYPE "tailduplication"

namespace {

class TailDuplicateBase : public MachineFunctionPass {
  TailDuplicator Duplicator;
  std::unique_ptr<MBFIWrapper> MBFIW;
  bool PreRegAlloc;
public:
  TailDuplicateBase(char &PassID, bool PreRegAlloc)
    : MachineFunctionPass(PassID), PreRegAlloc(PreRegAlloc) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineBranchProbabilityInfoWrapperPass>();
    AU.addRequired<LazyMachineBlockFrequencyInfoPass>();
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

class TailDuplicate : public TailDuplicateBase {
public:
  static char ID;
  TailDuplicate() : TailDuplicateBase(ID, false) {
    initializeTailDuplicatePass(*PassRegistry::getPassRegistry());
  }
};

class EarlyTailDuplicate : public TailDuplicateBase {
public:
  static char ID;
  EarlyTailDuplicate() : TailDuplicateBase(ID, true) {
    initializeEarlyTailDuplicatePass(*PassRegistry::getPassRegistry());
  }

  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties()
      .set(MachineFunctionProperties::Property::NoPHIs);
  }
};

} // end anonymous namespace

char TailDuplicate::ID;
char EarlyTailDuplicate::ID;

char &llvm::TailDuplicateID = TailDuplicate::ID;
char &llvm::EarlyTailDuplicateID = EarlyTailDuplicate::ID;

INITIALIZE_PASS(TailDuplicate, DEBUG_TYPE, "Tail Duplication", false, false)
INITIALIZE_PASS(EarlyTailDuplicate, "early-tailduplication",
                "Early Tail Duplication", false, false)

bool TailDuplicateBase::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  auto MBPI = &getAnalysis<MachineBranchProbabilityInfoWrapperPass>().getMBPI();
  auto *PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  auto *MBFI = (PSI && PSI->hasProfileSummary()) ?
               &getAnalysis<LazyMachineBlockFrequencyInfoPass>().getBFI() :
               nullptr;
  if (MBFI)
    MBFIW = std::make_unique<MBFIWrapper>(*MBFI);
  Duplicator.initMF(MF, PreRegAlloc, MBPI, MBFI ? MBFIW.get() : nullptr, PSI,
                    /*LayoutMode=*/false);

  bool MadeChange = false;
  while (Duplicator.tailDuplicateBlocks())
    MadeChange = true;

  return MadeChange;
}
