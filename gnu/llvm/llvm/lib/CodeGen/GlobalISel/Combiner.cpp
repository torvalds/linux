//===-- lib/CodeGen/GlobalISel/Combiner.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file constains common code to combine machine functions at generic
// level.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/CSEMIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelChangeObserver.h"
#include "llvm/CodeGen/GlobalISel/GISelWorkList.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "gi-combiner"

using namespace llvm;

STATISTIC(NumOneIteration, "Number of functions with one iteration");
STATISTIC(NumTwoIterations, "Number of functions with two iterations");
STATISTIC(NumThreeOrMoreIterations,
          "Number of functions with three or more iterations");

namespace llvm {
cl::OptionCategory GICombinerOptionCategory(
    "GlobalISel Combiner",
    "Control the rules which are enabled. These options all take a comma "
    "separated list of rules to disable and may be specified by number "
    "or number range (e.g. 1-10)."
#ifndef NDEBUG
    " They may also be specified by name."
#endif
);
} // end namespace llvm

/// This class acts as the glue the joins the CombinerHelper to the overall
/// Combine algorithm. The CombinerHelper is intended to report the
/// modifications it makes to the MIR to the GISelChangeObserver and the
/// observer subclass will act on these events. In this case, instruction
/// erasure will cancel any future visits to the erased instruction and
/// instruction creation will schedule that instruction for a future visit.
/// Other Combiner implementations may require more complex behaviour from
/// their GISelChangeObserver subclass.
class Combiner::WorkListMaintainer : public GISelChangeObserver {
  using WorkListTy = GISelWorkList<512>;
  WorkListTy &WorkList;
  /// The instructions that have been created but we want to report once they
  /// have their operands. This is only maintained if debug output is requested.
#ifndef NDEBUG
  SetVector<const MachineInstr *> CreatedInstrs;
#endif

public:
  WorkListMaintainer(WorkListTy &WorkList) : WorkList(WorkList) {}
  virtual ~WorkListMaintainer() = default;

  void erasingInstr(MachineInstr &MI) override {
    LLVM_DEBUG(dbgs() << "Erasing: " << MI << "\n");
    WorkList.remove(&MI);
  }
  void createdInstr(MachineInstr &MI) override {
    LLVM_DEBUG(dbgs() << "Creating: " << MI << "\n");
    WorkList.insert(&MI);
    LLVM_DEBUG(CreatedInstrs.insert(&MI));
  }
  void changingInstr(MachineInstr &MI) override {
    LLVM_DEBUG(dbgs() << "Changing: " << MI << "\n");
    WorkList.insert(&MI);
  }
  void changedInstr(MachineInstr &MI) override {
    LLVM_DEBUG(dbgs() << "Changed: " << MI << "\n");
    WorkList.insert(&MI);
  }

  void reportFullyCreatedInstrs() {
    LLVM_DEBUG(for (const auto *MI
                    : CreatedInstrs) {
      dbgs() << "Created: ";
      MI->print(dbgs());
    });
    LLVM_DEBUG(CreatedInstrs.clear());
  }
};

Combiner::Combiner(MachineFunction &MF, CombinerInfo &CInfo,
                   const TargetPassConfig *TPC, GISelKnownBits *KB,
                   GISelCSEInfo *CSEInfo)
    : Builder(CSEInfo ? std::make_unique<CSEMIRBuilder>()
                      : std::make_unique<MachineIRBuilder>()),
      WLObserver(std::make_unique<WorkListMaintainer>(WorkList)),
      ObserverWrapper(std::make_unique<GISelObserverWrapper>()), CInfo(CInfo),
      Observer(*ObserverWrapper), B(*Builder), MF(MF), MRI(MF.getRegInfo()),
      KB(KB), TPC(TPC), CSEInfo(CSEInfo) {
  (void)this->TPC; // FIXME: Remove when used.

  // Setup builder.
  B.setMF(MF);
  if (CSEInfo)
    B.setCSEInfo(CSEInfo);

  // Setup observer.
  ObserverWrapper->addObserver(WLObserver.get());
  if (CSEInfo)
    ObserverWrapper->addObserver(CSEInfo);

  B.setChangeObserver(*ObserverWrapper);
}

Combiner::~Combiner() = default;

bool Combiner::combineMachineInstrs() {
  // If the ISel pipeline failed, do not bother running this pass.
  // FIXME: Should this be here or in individual combiner passes.
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;

  // We can't call this in the constructor because the derived class is
  // uninitialized at that time.
  if (!HasSetupMF) {
    HasSetupMF = true;
    setupMF(MF, KB);
  }

  LLVM_DEBUG(dbgs() << "Generic MI Combiner for: " << MF.getName() << '\n');

  MachineOptimizationRemarkEmitter MORE(MF, /*MBFI=*/nullptr);

  bool MFChanged = false;
  bool Changed;

  unsigned Iteration = 0;
  while (true) {
    ++Iteration;
    LLVM_DEBUG(dbgs() << "\n\nCombiner iteration #" << Iteration << '\n');

    WorkList.clear();

    // Collect all instructions. Do a post order traversal for basic blocks and
    // insert with list bottom up, so while we pop_back_val, we'll traverse top
    // down RPOT.
    Changed = false;

    RAIIDelegateInstaller DelInstall(MF, ObserverWrapper.get());
    for (MachineBasicBlock *MBB : post_order(&MF)) {
      for (MachineInstr &CurMI :
           llvm::make_early_inc_range(llvm::reverse(*MBB))) {
        // Erase dead insts before even adding to the list.
        if (isTriviallyDead(CurMI, MRI)) {
          LLVM_DEBUG(dbgs() << CurMI << "Is dead; erasing.\n");
          llvm::salvageDebugInfo(MRI, CurMI);
          CurMI.eraseFromParent();
          continue;
        }
        WorkList.deferred_insert(&CurMI);
      }
    }
    WorkList.finalize();
    // Main Loop. Process the instructions here.
    while (!WorkList.empty()) {
      MachineInstr *CurrInst = WorkList.pop_back_val();
      LLVM_DEBUG(dbgs() << "\nTry combining " << *CurrInst;);
      Changed |= tryCombineAll(*CurrInst);
      WLObserver->reportFullyCreatedInstrs();
    }
    MFChanged |= Changed;

    if (!Changed) {
      LLVM_DEBUG(dbgs() << "\nCombiner reached fixed-point after iteration #"
                        << Iteration << '\n');
      break;
    }
    // Iterate until a fixed-point is reached if MaxIterations == 0,
    // otherwise limit the number of iterations.
    if (CInfo.MaxIterations && Iteration >= CInfo.MaxIterations) {
      LLVM_DEBUG(
          dbgs() << "\nCombiner reached iteration limit after iteration #"
                 << Iteration << '\n');
      break;
    }
  }

  if (Iteration == 1)
    ++NumOneIteration;
  else if (Iteration == 2)
    ++NumTwoIterations;
  else
    ++NumThreeOrMoreIterations;

#ifndef NDEBUG
  if (CSEInfo) {
    if (auto E = CSEInfo->verify()) {
      errs() << E << '\n';
      assert(false && "CSEInfo is not consistent. Likely missing calls to "
                      "observer on mutations.");
    }
  }
#endif
  return MFChanged;
}
