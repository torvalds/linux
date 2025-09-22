//===-------- EdgeBundles.cpp - Bundles of CFG edges ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the implementation of the EdgeBundles analysis.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/EdgeBundles.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static cl::opt<bool>
ViewEdgeBundles("view-edge-bundles", cl::Hidden,
                cl::desc("Pop up a window to show edge bundle graphs"));

char EdgeBundles::ID = 0;

INITIALIZE_PASS(EdgeBundles, "edge-bundles", "Bundle Machine CFG Edges",
                /* cfg = */true, /* is_analysis = */ true)

char &llvm::EdgeBundlesID = EdgeBundles::ID;

void EdgeBundles::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  MachineFunctionPass::getAnalysisUsage(AU);
}

bool EdgeBundles::runOnMachineFunction(MachineFunction &mf) {
  MF = &mf;
  EC.clear();
  EC.grow(2 * MF->getNumBlockIDs());

  for (const auto &MBB : *MF) {
    unsigned OutE = 2 * MBB.getNumber() + 1;
    // Join the outgoing bundle with the ingoing bundles of all successors.
    for (const MachineBasicBlock *Succ : MBB.successors())
      EC.join(OutE, 2 * Succ->getNumber());
  }
  EC.compress();
  if (ViewEdgeBundles)
    view();

  // Compute the reverse mapping.
  Blocks.clear();
  Blocks.resize(getNumBundles());

  for (unsigned i = 0, e = MF->getNumBlockIDs(); i != e; ++i) {
    unsigned b0 = getBundle(i, false);
    unsigned b1 = getBundle(i, true);
    Blocks[b0].push_back(i);
    if (b1 != b0)
      Blocks[b1].push_back(i);
  }

  return false;
}

namespace llvm {

/// Specialize WriteGraph, the standard implementation won't work.
template<>
raw_ostream &WriteGraph<>(raw_ostream &O, const EdgeBundles &G,
                          bool ShortNames,
                          const Twine &Title) {
  const MachineFunction *MF = G.getMachineFunction();

  O << "digraph {\n";
  for (const auto &MBB : *MF) {
    unsigned BB = MBB.getNumber();
    O << "\t\"" << printMBBReference(MBB) << "\" [ shape=box ]\n"
      << '\t' << G.getBundle(BB, false) << " -> \"" << printMBBReference(MBB)
      << "\"\n"
      << "\t\"" << printMBBReference(MBB) << "\" -> " << G.getBundle(BB, true)
      << '\n';
    for (const MachineBasicBlock *Succ : MBB.successors())
      O << "\t\"" << printMBBReference(MBB) << "\" -> \""
        << printMBBReference(*Succ) << "\" [ color=lightgray ]\n";
  }
  O << "}\n";
  return O;
}

} // end namespace llvm

/// view - Visualize the annotated bipartite CFG with Graphviz.
void EdgeBundles::view() const {
  ViewGraph(*this, "EdgeBundles");
}
