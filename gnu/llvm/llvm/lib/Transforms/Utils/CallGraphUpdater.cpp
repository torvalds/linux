//===- CallGraphUpdater.cpp - A (lazy) call graph update helper -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides interfaces used to manipulate a call graph, regardless
/// if it is a "old style" CallGraph or an "new style" LazyCallGraph.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CallGraphUpdater.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

bool CallGraphUpdater::finalize() {
  if (!DeadFunctionsInComdats.empty()) {
    filterDeadComdatFunctions(DeadFunctionsInComdats);
    DeadFunctions.append(DeadFunctionsInComdats.begin(),
                         DeadFunctionsInComdats.end());
  }

  // This is the code path for the new lazy call graph and for the case were
  // no call graph was provided.
  for (Function *DeadFn : DeadFunctions) {
    DeadFn->removeDeadConstantUsers();
    DeadFn->replaceAllUsesWith(PoisonValue::get(DeadFn->getType()));

    if (LCG && !ReplacedFunctions.count(DeadFn)) {
      // Taken mostly from the inliner:
      LazyCallGraph::Node &N = LCG->get(*DeadFn);
      auto *DeadSCC = LCG->lookupSCC(N);
      assert(DeadSCC && DeadSCC->size() == 1 &&
             &DeadSCC->begin()->getFunction() == DeadFn);

      FAM->clear(*DeadFn, DeadFn->getName());
      AM->clear(*DeadSCC, DeadSCC->getName());
      LCG->markDeadFunction(*DeadFn);

      // Mark the relevant parts of the call graph as invalid so we don't
      // visit them.
      UR->InvalidatedSCCs.insert(LCG->lookupSCC(N));
      UR->DeadFunctions.push_back(DeadFn);
    } else {
      // The CGSCC infrastructure batch deletes functions at the end of the
      // call graph walk, so only erase the function if we're not using that
      // infrastructure.
      // The function is now really dead and de-attached from everything.
      DeadFn->eraseFromParent();
    }
  }

  bool Changed = !DeadFunctions.empty();
  DeadFunctionsInComdats.clear();
  DeadFunctions.clear();
  return Changed;
}

void CallGraphUpdater::reanalyzeFunction(Function &Fn) {
  if (LCG) {
    LazyCallGraph::Node &N = LCG->get(Fn);
    LazyCallGraph::SCC *C = LCG->lookupSCC(N);
    updateCGAndAnalysisManagerForCGSCCPass(*LCG, *C, N, *AM, *UR, *FAM);
  }
}

void CallGraphUpdater::registerOutlinedFunction(Function &OriginalFn,
                                                Function &NewFn) {
  if (LCG)
    LCG->addSplitFunction(OriginalFn, NewFn);
}

void CallGraphUpdater::removeFunction(Function &DeadFn) {
  DeadFn.deleteBody();
  DeadFn.setLinkage(GlobalValue::ExternalLinkage);
  if (DeadFn.hasComdat())
    DeadFunctionsInComdats.push_back(&DeadFn);
  else
    DeadFunctions.push_back(&DeadFn);

  if (FAM)
    FAM->clear(DeadFn, DeadFn.getName());
}

void CallGraphUpdater::replaceFunctionWith(Function &OldFn, Function &NewFn) {
  OldFn.removeDeadConstantUsers();
  ReplacedFunctions.insert(&OldFn);
  if (LCG) {
    // Directly substitute the functions in the call graph.
    LazyCallGraph::Node &OldLCGN = LCG->get(OldFn);
    SCC->getOuterRefSCC().replaceNodeFunction(OldLCGN, NewFn);
  }
  removeFunction(OldFn);
}
