//===- MachineDominators.cpp - Machine Dominator Calculation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements simple dominator construction algorithms for finding
// forward dominators on machine functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GenericDomTreeConstruction.h"

using namespace llvm;

namespace llvm {
// Always verify dominfo if expensive checking is enabled.
#ifdef EXPENSIVE_CHECKS
bool VerifyMachineDomInfo = true;
#else
bool VerifyMachineDomInfo = false;
#endif
} // namespace llvm

static cl::opt<bool, true> VerifyMachineDomInfoX(
    "verify-machine-dom-info", cl::location(VerifyMachineDomInfo), cl::Hidden,
    cl::desc("Verify machine dominator info (time consuming)"));

namespace llvm {
template class DomTreeNodeBase<MachineBasicBlock>;
template class DominatorTreeBase<MachineBasicBlock, false>; // DomTreeBase

namespace DomTreeBuilder {
template void Calculate<MBBDomTree>(MBBDomTree &DT);
template void CalculateWithUpdates<MBBDomTree>(MBBDomTree &DT, MBBUpdates U);

template void InsertEdge<MBBDomTree>(MBBDomTree &DT, MachineBasicBlock *From,
                                     MachineBasicBlock *To);

template void DeleteEdge<MBBDomTree>(MBBDomTree &DT, MachineBasicBlock *From,
                                     MachineBasicBlock *To);

template void ApplyUpdates<MBBDomTree>(MBBDomTree &DT, MBBDomTreeGraphDiff &,
                                       MBBDomTreeGraphDiff *);

template bool Verify<MBBDomTree>(const MBBDomTree &DT,
                                 MBBDomTree::VerificationLevel VL);
} // namespace DomTreeBuilder
}

bool MachineDominatorTree::invalidate(
    MachineFunction &, const PreservedAnalyses &PA,
    MachineFunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on machine functions, or the
  // machine function's CFG have been preserved.
  auto PAC = PA.getChecker<MachineDominatorTreeAnalysis>();
  return !PAC.preserved() &&
         !PAC.preservedSet<AllAnalysesOn<MachineFunction>>() &&
         !PAC.preservedSet<CFGAnalyses>();
}

AnalysisKey MachineDominatorTreeAnalysis::Key;

MachineDominatorTreeAnalysis::Result
MachineDominatorTreeAnalysis::run(MachineFunction &MF,
                                  MachineFunctionAnalysisManager &) {
  return MachineDominatorTree(MF);
}

PreservedAnalyses
MachineDominatorTreePrinterPass::run(MachineFunction &MF,
                                     MachineFunctionAnalysisManager &MFAM) {
  OS << "MachineDominatorTree for machine function: " << MF.getName() << '\n';
  MFAM.getResult<MachineDominatorTreeAnalysis>(MF).print(OS);
  return PreservedAnalyses::all();
}

char MachineDominatorTreeWrapperPass::ID = 0;

INITIALIZE_PASS(MachineDominatorTreeWrapperPass, "machinedomtree",
                "MachineDominator Tree Construction", true, true)

MachineDominatorTreeWrapperPass::MachineDominatorTreeWrapperPass()
    : MachineFunctionPass(ID) {
  initializeMachineDominatorTreeWrapperPassPass(
      *PassRegistry::getPassRegistry());
}

void MachineDominatorTree::calculate(MachineFunction &F) {
  CriticalEdgesToSplit.clear();
  NewBBs.clear();
  recalculate(F);
}

char &llvm::MachineDominatorsID = MachineDominatorTreeWrapperPass::ID;

bool MachineDominatorTreeWrapperPass::runOnMachineFunction(MachineFunction &F) {
  DT = MachineDominatorTree(F);
  return false;
}

void MachineDominatorTreeWrapperPass::releaseMemory() { DT.reset(); }

void MachineDominatorTreeWrapperPass::verifyAnalysis() const {
  if (VerifyMachineDomInfo && DT)
    if (!DT->verify(MachineDominatorTree::VerificationLevel::Basic))
      report_fatal_error("MachineDominatorTree verification failed!");
}

void MachineDominatorTreeWrapperPass::print(raw_ostream &OS,
                                            const Module *) const {
  if (DT)
    DT->print(OS);
}

void MachineDominatorTree::applySplitCriticalEdges() const {
  // Bail out early if there is nothing to do.
  if (CriticalEdgesToSplit.empty())
    return;

  // For each element in CriticalEdgesToSplit, remember whether or not element
  // is the new immediate domminator of its successor. The mapping is done by
  // index, i.e., the information for the ith element of CriticalEdgesToSplit is
  // the ith element of IsNewIDom.
  SmallBitVector IsNewIDom(CriticalEdgesToSplit.size(), true);
  size_t Idx = 0;

  // Collect all the dominance properties info, before invalidating
  // the underlying DT.
  for (CriticalEdge &Edge : CriticalEdgesToSplit) {
    // Update dominator information.
    MachineBasicBlock *Succ = Edge.ToBB;
    MachineDomTreeNode *SuccDTNode = Base::getNode(Succ);

    for (MachineBasicBlock *PredBB : Succ->predecessors()) {
      if (PredBB == Edge.NewBB)
        continue;
      // If we are in this situation:
      // FromBB1        FromBB2
      //    +              +
      //   + +            + +
      //  +   +          +   +
      // ...  Split1  Split2 ...
      //           +   +
      //            + +
      //             +
      //            Succ
      // Instead of checking the domiance property with Split2, we check it with
      // FromBB2 since Split2 is still unknown of the underlying DT structure.
      if (NewBBs.count(PredBB)) {
        assert(PredBB->pred_size() == 1 && "A basic block resulting from a "
                                           "critical edge split has more "
                                           "than one predecessor!");
        PredBB = *PredBB->pred_begin();
      }
      if (!Base::dominates(SuccDTNode, Base::getNode(PredBB))) {
        IsNewIDom[Idx] = false;
        break;
      }
    }
    ++Idx;
  }

  // Now, update DT with the collected dominance properties info.
  Idx = 0;
  for (CriticalEdge &Edge : CriticalEdgesToSplit) {
    // We know FromBB dominates NewBB.
    MachineDomTreeNode *NewDTNode =
        const_cast<MachineDominatorTree *>(this)->Base::addNewBlock(
            Edge.NewBB, Edge.FromBB);

    // If all the other predecessors of "Succ" are dominated by "Succ" itself
    // then the new block is the new immediate dominator of "Succ". Otherwise,
    // the new block doesn't dominate anything.
    if (IsNewIDom[Idx])
      const_cast<MachineDominatorTree *>(this)->Base::changeImmediateDominator(
          Base::getNode(Edge.ToBB), NewDTNode);
    ++Idx;
  }
  NewBBs.clear();
  CriticalEdgesToSplit.clear();
}
