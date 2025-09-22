//===- DomPrinter.cpp - DOT printer for the dominance trees    ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines '-dot-dom' and '-dot-postdom' analysis passes, which emit
// a dom.<fnname>.dot or postdom.<fnname>.dot file for each function in the
// program, with a graph of the dominance/postdominance tree of that
// function.
//
// There are also passes available to directly call dotty ('-view-dom' or
// '-view-postdom'). By appending '-only' like '-dot-dom-only' only the
// names of the bbs are printed, but the content is hidden.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DomPrinter.h"
#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/InitializePasses.h"

using namespace llvm;


void DominatorTree::viewGraph(const Twine &Name, const Twine &Title) {
#ifndef NDEBUG
  ViewGraph(this, Name, false, Title);
#else
  errs() << "DomTree dump not available, build with DEBUG\n";
#endif  // NDEBUG
}

void DominatorTree::viewGraph() {
#ifndef NDEBUG
  this->viewGraph("domtree", "Dominator Tree for function");
#else
  errs() << "DomTree dump not available, build with DEBUG\n";
#endif  // NDEBUG
}

namespace {
struct LegacyDominatorTreeWrapperPassAnalysisGraphTraits {
  static DominatorTree *getGraph(DominatorTreeWrapperPass *DTWP) {
    return &DTWP->getDomTree();
  }
};

struct DomViewerWrapperPass
    : public DOTGraphTraitsViewerWrapperPass<
          DominatorTreeWrapperPass, false, DominatorTree *,
          LegacyDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomViewerWrapperPass()
      : DOTGraphTraitsViewerWrapperPass<
            DominatorTreeWrapperPass, false, DominatorTree *,
            LegacyDominatorTreeWrapperPassAnalysisGraphTraits>("dom", ID) {
    initializeDomViewerWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};

struct DomOnlyViewerWrapperPass
    : public DOTGraphTraitsViewerWrapperPass<
          DominatorTreeWrapperPass, true, DominatorTree *,
          LegacyDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomOnlyViewerWrapperPass()
      : DOTGraphTraitsViewerWrapperPass<
            DominatorTreeWrapperPass, true, DominatorTree *,
            LegacyDominatorTreeWrapperPassAnalysisGraphTraits>("domonly", ID) {
    initializeDomOnlyViewerWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};

struct LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits {
  static PostDominatorTree *getGraph(PostDominatorTreeWrapperPass *PDTWP) {
    return &PDTWP->getPostDomTree();
  }
};

struct PostDomViewerWrapperPass
    : public DOTGraphTraitsViewerWrapperPass<
          PostDominatorTreeWrapperPass, false, PostDominatorTree *,
          LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomViewerWrapperPass()
      : DOTGraphTraitsViewerWrapperPass<
            PostDominatorTreeWrapperPass, false, PostDominatorTree *,
            LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits>("postdom",
                                                                   ID) {
    initializePostDomViewerWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};

struct PostDomOnlyViewerWrapperPass
    : public DOTGraphTraitsViewerWrapperPass<
          PostDominatorTreeWrapperPass, true, PostDominatorTree *,
          LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomOnlyViewerWrapperPass()
      : DOTGraphTraitsViewerWrapperPass<
            PostDominatorTreeWrapperPass, true, PostDominatorTree *,
            LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits>(
            "postdomonly", ID) {
    initializePostDomOnlyViewerWrapperPassPass(
        *PassRegistry::getPassRegistry());
  }
};
} // end anonymous namespace

char DomViewerWrapperPass::ID = 0;
INITIALIZE_PASS(DomViewerWrapperPass, "view-dom",
                "View dominance tree of function", false, false)

char DomOnlyViewerWrapperPass::ID = 0;
INITIALIZE_PASS(DomOnlyViewerWrapperPass, "view-dom-only",
                "View dominance tree of function (with no function bodies)",
                false, false)

char PostDomViewerWrapperPass::ID = 0;
INITIALIZE_PASS(PostDomViewerWrapperPass, "view-postdom",
                "View postdominance tree of function", false, false)

char PostDomOnlyViewerWrapperPass::ID = 0;
INITIALIZE_PASS(PostDomOnlyViewerWrapperPass, "view-postdom-only",
                "View postdominance tree of function "
                "(with no function bodies)",
                false, false)

namespace {
struct DomPrinterWrapperPass
    : public DOTGraphTraitsPrinterWrapperPass<
          DominatorTreeWrapperPass, false, DominatorTree *,
          LegacyDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomPrinterWrapperPass()
      : DOTGraphTraitsPrinterWrapperPass<
            DominatorTreeWrapperPass, false, DominatorTree *,
            LegacyDominatorTreeWrapperPassAnalysisGraphTraits>("dom", ID) {
    initializeDomPrinterWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};

struct DomOnlyPrinterWrapperPass
    : public DOTGraphTraitsPrinterWrapperPass<
          DominatorTreeWrapperPass, true, DominatorTree *,
          LegacyDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  DomOnlyPrinterWrapperPass()
      : DOTGraphTraitsPrinterWrapperPass<
            DominatorTreeWrapperPass, true, DominatorTree *,
            LegacyDominatorTreeWrapperPassAnalysisGraphTraits>("domonly", ID) {
    initializeDomOnlyPrinterWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};

struct PostDomPrinterWrapperPass
    : public DOTGraphTraitsPrinterWrapperPass<
          PostDominatorTreeWrapperPass, false, PostDominatorTree *,
          LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomPrinterWrapperPass()
      : DOTGraphTraitsPrinterWrapperPass<
            PostDominatorTreeWrapperPass, false, PostDominatorTree *,
            LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits>("postdom",
                                                                   ID) {
    initializePostDomPrinterWrapperPassPass(*PassRegistry::getPassRegistry());
  }
};

struct PostDomOnlyPrinterWrapperPass
    : public DOTGraphTraitsPrinterWrapperPass<
          PostDominatorTreeWrapperPass, true, PostDominatorTree *,
          LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits> {
  static char ID;
  PostDomOnlyPrinterWrapperPass()
      : DOTGraphTraitsPrinterWrapperPass<
            PostDominatorTreeWrapperPass, true, PostDominatorTree *,
            LegacyPostDominatorTreeWrapperPassAnalysisGraphTraits>(
            "postdomonly", ID) {
    initializePostDomOnlyPrinterWrapperPassPass(
        *PassRegistry::getPassRegistry());
  }
};
} // end anonymous namespace

char DomPrinterWrapperPass::ID = 0;
INITIALIZE_PASS(DomPrinterWrapperPass, "dot-dom",
                "Print dominance tree of function to 'dot' file", false, false)

char DomOnlyPrinterWrapperPass::ID = 0;
INITIALIZE_PASS(DomOnlyPrinterWrapperPass, "dot-dom-only",
                "Print dominance tree of function to 'dot' file "
                "(with no function bodies)",
                false, false)

char PostDomPrinterWrapperPass::ID = 0;
INITIALIZE_PASS(PostDomPrinterWrapperPass, "dot-postdom",
                "Print postdominance tree of function to 'dot' file", false,
                false)

char PostDomOnlyPrinterWrapperPass::ID = 0;
INITIALIZE_PASS(PostDomOnlyPrinterWrapperPass, "dot-postdom-only",
                "Print postdominance tree of function to 'dot' file "
                "(with no function bodies)",
                false, false)

// Create methods available outside of this file, to use them
// "include/llvm/LinkAllPasses.h". Otherwise the pass would be deleted by
// the link time optimization.

FunctionPass *llvm::createDomPrinterWrapperPassPass() {
  return new DomPrinterWrapperPass();
}

FunctionPass *llvm::createDomOnlyPrinterWrapperPassPass() {
  return new DomOnlyPrinterWrapperPass();
}

FunctionPass *llvm::createDomViewerWrapperPassPass() {
  return new DomViewerWrapperPass();
}

FunctionPass *llvm::createDomOnlyViewerWrapperPassPass() {
  return new DomOnlyViewerWrapperPass();
}

FunctionPass *llvm::createPostDomPrinterWrapperPassPass() {
  return new PostDomPrinterWrapperPass();
}

FunctionPass *llvm::createPostDomOnlyPrinterWrapperPassPass() {
  return new PostDomOnlyPrinterWrapperPass();
}

FunctionPass *llvm::createPostDomViewerWrapperPassPass() {
  return new PostDomViewerWrapperPass();
}

FunctionPass *llvm::createPostDomOnlyViewerWrapperPassPass() {
  return new PostDomOnlyViewerWrapperPass();
}
