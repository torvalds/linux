//===-- llvm/CodeGen/GlobalISel/Legalizer.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file implements the LegalizerHelper class to legalize individual
/// instructions and the LegalizePass wrapper pass for the primary
/// legalization.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/CSEMIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/GISelWorkList.h"
#include "llvm/CodeGen/GlobalISel/LegalizationArtifactCombiner.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/LostDebugLocObserver.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"

#define DEBUG_TYPE "legalizer"

using namespace llvm;

static cl::opt<bool>
    EnableCSEInLegalizer("enable-cse-in-legalizer",
                         cl::desc("Should enable CSE in Legalizer"),
                         cl::Optional, cl::init(false));

// This is a temporary hack, should be removed soon.
static cl::opt<bool> AllowGInsertAsArtifact(
    "allow-ginsert-as-artifact",
    cl::desc("Allow G_INSERT to be considered an artifact. Hack around AMDGPU "
             "test infinite loops."),
    cl::Optional, cl::init(true));

enum class DebugLocVerifyLevel {
  None,
  Legalizations,
  LegalizationsAndArtifactCombiners,
};
#ifndef NDEBUG
static cl::opt<DebugLocVerifyLevel> VerifyDebugLocs(
    "verify-legalizer-debug-locs",
    cl::desc("Verify that debug locations are handled"),
    cl::values(
        clEnumValN(DebugLocVerifyLevel::None, "none", "No verification"),
        clEnumValN(DebugLocVerifyLevel::Legalizations, "legalizations",
                   "Verify legalizations"),
        clEnumValN(DebugLocVerifyLevel::LegalizationsAndArtifactCombiners,
                   "legalizations+artifactcombiners",
                   "Verify legalizations and artifact combines")),
    cl::init(DebugLocVerifyLevel::Legalizations));
#else
// Always disable it for release builds by preventing the observer from being
// installed.
static const DebugLocVerifyLevel VerifyDebugLocs = DebugLocVerifyLevel::None;
#endif

char Legalizer::ID = 0;
INITIALIZE_PASS_BEGIN(Legalizer, DEBUG_TYPE,
                      "Legalize the Machine IR a function's Machine IR", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_END(Legalizer, DEBUG_TYPE,
                    "Legalize the Machine IR a function's Machine IR", false,
                    false)

Legalizer::Legalizer() : MachineFunctionPass(ID) { }

void Legalizer::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.addRequired<GISelCSEAnalysisWrapperPass>();
  AU.addPreserved<GISelCSEAnalysisWrapperPass>();
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();
  getSelectionDAGFallbackAnalysisUsage(AU);
  MachineFunctionPass::getAnalysisUsage(AU);
}

void Legalizer::init(MachineFunction &MF) {
}

static bool isArtifact(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case TargetOpcode::G_TRUNC:
  case TargetOpcode::G_ZEXT:
  case TargetOpcode::G_ANYEXT:
  case TargetOpcode::G_SEXT:
  case TargetOpcode::G_MERGE_VALUES:
  case TargetOpcode::G_UNMERGE_VALUES:
  case TargetOpcode::G_CONCAT_VECTORS:
  case TargetOpcode::G_BUILD_VECTOR:
  case TargetOpcode::G_EXTRACT:
    return true;
  case TargetOpcode::G_INSERT:
    return AllowGInsertAsArtifact;
  }
}
using InstListTy = GISelWorkList<256>;
using ArtifactListTy = GISelWorkList<128>;

namespace {
class LegalizerWorkListManager : public GISelChangeObserver {
  InstListTy &InstList;
  ArtifactListTy &ArtifactList;
#ifndef NDEBUG
  SmallVector<MachineInstr *, 4> NewMIs;
#endif

public:
  LegalizerWorkListManager(InstListTy &Insts, ArtifactListTy &Arts)
      : InstList(Insts), ArtifactList(Arts) {}

  void createdOrChangedInstr(MachineInstr &MI) {
    // Only legalize pre-isel generic instructions.
    // Legalization process could generate Target specific pseudo
    // instructions with generic types. Don't record them
    if (isPreISelGenericOpcode(MI.getOpcode())) {
      if (isArtifact(MI))
        ArtifactList.insert(&MI);
      else
        InstList.insert(&MI);
    }
  }

  void createdInstr(MachineInstr &MI) override {
    LLVM_DEBUG(NewMIs.push_back(&MI));
    createdOrChangedInstr(MI);
  }

  void printNewInstrs() {
    LLVM_DEBUG({
      for (const auto *MI : NewMIs)
        dbgs() << ".. .. New MI: " << *MI;
      NewMIs.clear();
    });
  }

  void erasingInstr(MachineInstr &MI) override {
    LLVM_DEBUG(dbgs() << ".. .. Erasing: " << MI);
    InstList.remove(&MI);
    ArtifactList.remove(&MI);
  }

  void changingInstr(MachineInstr &MI) override {
    LLVM_DEBUG(dbgs() << ".. .. Changing MI: " << MI);
  }

  void changedInstr(MachineInstr &MI) override {
    // When insts change, we want to revisit them to legalize them again.
    // We'll consider them the same as created.
    LLVM_DEBUG(dbgs() << ".. .. Changed MI: " << MI);
    createdOrChangedInstr(MI);
  }
};
} // namespace

Legalizer::MFResult
Legalizer::legalizeMachineFunction(MachineFunction &MF, const LegalizerInfo &LI,
                                   ArrayRef<GISelChangeObserver *> AuxObservers,
                                   LostDebugLocObserver &LocObserver,
                                   MachineIRBuilder &MIRBuilder,
                                   GISelKnownBits *KB) {
  MIRBuilder.setMF(MF);
  MachineRegisterInfo &MRI = MF.getRegInfo();

  // Populate worklists.
  InstListTy InstList;
  ArtifactListTy ArtifactList;
  ReversePostOrderTraversal<MachineFunction *> RPOT(&MF);
  // Perform legalization bottom up so we can DCE as we legalize.
  // Traverse BB in RPOT and within each basic block, add insts top down,
  // so when we pop_back_val in the legalization process, we traverse bottom-up.
  for (auto *MBB : RPOT) {
    if (MBB->empty())
      continue;
    for (MachineInstr &MI : *MBB) {
      // Only legalize pre-isel generic instructions: others don't have types
      // and are assumed to be legal.
      if (!isPreISelGenericOpcode(MI.getOpcode()))
        continue;
      if (isArtifact(MI))
        ArtifactList.deferred_insert(&MI);
      else
        InstList.deferred_insert(&MI);
    }
  }
  ArtifactList.finalize();
  InstList.finalize();

  // This observer keeps the worklists updated.
  LegalizerWorkListManager WorkListObserver(InstList, ArtifactList);
  // We want both WorkListObserver as well as all the auxiliary observers (e.g.
  // CSEInfo) to observe all changes. Use the wrapper observer.
  GISelObserverWrapper WrapperObserver(&WorkListObserver);
  for (GISelChangeObserver *Observer : AuxObservers)
    WrapperObserver.addObserver(Observer);

  // Now install the observer as the delegate to MF.
  // This will keep all the observers notified about new insertions/deletions.
  RAIIMFObsDelInstaller Installer(MF, WrapperObserver);
  LegalizerHelper Helper(MF, LI, WrapperObserver, MIRBuilder, KB);
  LegalizationArtifactCombiner ArtCombiner(MIRBuilder, MRI, LI, KB);
  bool Changed = false;
  SmallVector<MachineInstr *, 128> RetryList;
  do {
    LLVM_DEBUG(dbgs() << "=== New Iteration ===\n");
    assert(RetryList.empty() && "Expected no instructions in RetryList");
    unsigned NumArtifacts = ArtifactList.size();
    while (!InstList.empty()) {
      MachineInstr &MI = *InstList.pop_back_val();
      assert(isPreISelGenericOpcode(MI.getOpcode()) &&
             "Expecting generic opcode");
      if (isTriviallyDead(MI, MRI)) {
        salvageDebugInfo(MRI, MI);
        eraseInstr(MI, MRI, &LocObserver);
        continue;
      }

      // Do the legalization for this instruction.
      auto Res = Helper.legalizeInstrStep(MI, LocObserver);
      // Error out if we couldn't legalize this instruction. We may want to
      // fall back to DAG ISel instead in the future.
      if (Res == LegalizerHelper::UnableToLegalize) {
        // Move illegal artifacts to RetryList instead of aborting because
        // legalizing InstList may generate artifacts that allow
        // ArtifactCombiner to combine away them.
        if (isArtifact(MI)) {
          LLVM_DEBUG(dbgs() << ".. Not legalized, moving to artifacts retry\n");
          assert(NumArtifacts == 0 &&
                 "Artifacts are only expected in instruction list starting the "
                 "second iteration, but each iteration starting second must "
                 "start with an empty artifacts list");
          (void)NumArtifacts;
          RetryList.push_back(&MI);
          continue;
        }
        Helper.MIRBuilder.stopObservingChanges();
        return {Changed, &MI};
      }
      WorkListObserver.printNewInstrs();
      LocObserver.checkpoint();
      Changed |= Res == LegalizerHelper::Legalized;
    }
    // Try to combine the instructions in RetryList again if there
    // are new artifacts. If not, stop legalizing.
    if (!RetryList.empty()) {
      if (!ArtifactList.empty()) {
        while (!RetryList.empty())
          ArtifactList.insert(RetryList.pop_back_val());
      } else {
        LLVM_DEBUG(dbgs() << "No new artifacts created, not retrying!\n");
        Helper.MIRBuilder.stopObservingChanges();
        return {Changed, RetryList.front()};
      }
    }
    LocObserver.checkpoint();
    while (!ArtifactList.empty()) {
      MachineInstr &MI = *ArtifactList.pop_back_val();
      assert(isPreISelGenericOpcode(MI.getOpcode()) &&
             "Expecting generic opcode");
      if (isTriviallyDead(MI, MRI)) {
        salvageDebugInfo(MRI, MI);
        eraseInstr(MI, MRI, &LocObserver);
        continue;
      }
      SmallVector<MachineInstr *, 4> DeadInstructions;
      LLVM_DEBUG(dbgs() << "Trying to combine: " << MI);
      if (ArtCombiner.tryCombineInstruction(MI, DeadInstructions,
                                            WrapperObserver)) {
        WorkListObserver.printNewInstrs();
        eraseInstrs(DeadInstructions, MRI, &LocObserver);
        LocObserver.checkpoint(
            VerifyDebugLocs ==
            DebugLocVerifyLevel::LegalizationsAndArtifactCombiners);
        Changed = true;
        continue;
      }
      // If this was not an artifact (that could be combined away), this might
      // need special handling. Add it to InstList, so when it's processed
      // there, it has to be legal or specially handled.
      else {
        LLVM_DEBUG(dbgs() << ".. Not combined, moving to instructions list\n");
        InstList.insert(&MI);
      }
    }
  } while (!InstList.empty());

  return {Changed, /*FailedOn*/ nullptr};
}

bool Legalizer::runOnMachineFunction(MachineFunction &MF) {
  // If the ISel pipeline failed, do not bother running that pass.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  LLVM_DEBUG(dbgs() << "Legalize Machine IR for: " << MF.getName() << '\n');
  init(MF);
  const TargetPassConfig &TPC = getAnalysis<TargetPassConfig>();
  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  MachineOptimizationRemarkEmitter MORE(MF, /*MBFI=*/nullptr);

  std::unique_ptr<MachineIRBuilder> MIRBuilder;
  GISelCSEInfo *CSEInfo = nullptr;
  bool EnableCSE = EnableCSEInLegalizer.getNumOccurrences()
                       ? EnableCSEInLegalizer
                       : TPC.isGISelCSEEnabled();
  if (EnableCSE) {
    MIRBuilder = std::make_unique<CSEMIRBuilder>();
    CSEInfo = &Wrapper.get(TPC.getCSEConfig());
    MIRBuilder->setCSEInfo(CSEInfo);
  } else
    MIRBuilder = std::make_unique<MachineIRBuilder>();

  SmallVector<GISelChangeObserver *, 1> AuxObservers;
  if (EnableCSE && CSEInfo) {
    // We want CSEInfo in addition to WorkListObserver to observe all changes.
    AuxObservers.push_back(CSEInfo);
  }
  assert(!CSEInfo || !errorToBool(CSEInfo->verify()));
  LostDebugLocObserver LocObserver(DEBUG_TYPE);
  if (VerifyDebugLocs > DebugLocVerifyLevel::None)
    AuxObservers.push_back(&LocObserver);

  // This allows Known Bits Analysis in the legalizer.
  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);

  const LegalizerInfo &LI = *MF.getSubtarget().getLegalizerInfo();
  MFResult Result = legalizeMachineFunction(MF, LI, AuxObservers, LocObserver,
                                            *MIRBuilder, KB);

  if (Result.FailedOn) {
    reportGISelFailure(MF, TPC, MORE, "gisel-legalize",
                       "unable to legalize instruction", *Result.FailedOn);
    return false;
  }

  if (LocObserver.getNumLostDebugLocs()) {
    MachineOptimizationRemarkMissed R("gisel-legalize", "LostDebugLoc",
                                      MF.getFunction().getSubprogram(),
                                      /*MBB=*/&*MF.begin());
    R << "lost "
      << ore::NV("NumLostDebugLocs", LocObserver.getNumLostDebugLocs())
      << " debug locations during pass";
    reportGISelWarning(MF, TPC, MORE, R);
    // Example remark:
    // --- !Missed
    // Pass:            gisel-legalize
    // Name:            GISelFailure
    // DebugLoc:        { File: '.../legalize-urem.mir', Line: 1, Column: 0 }
    // Function:        test_urem_s32
    // Args:
    //   - String:          'lost '
    //   - NumLostDebugLocs: '1'
    //   - String:          ' debug locations during pass'
    // ...
  }

  // If for some reason CSE was not enabled, make sure that we invalidate the
  // CSEInfo object (as we currently declare that the analysis is preserved).
  // The next time get on the wrapper is called, it will force it to recompute
  // the analysis.
  if (!EnableCSE)
    Wrapper.setComputed(false);
  return Result.Changed;
}
