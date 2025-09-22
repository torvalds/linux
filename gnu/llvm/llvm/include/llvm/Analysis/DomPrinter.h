//===-- DomPrinter.h - Dom printer external interface ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines external functions that can be called to explicitly
// instantiate the dominance tree printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMPRINTER_H
#define LLVM_ANALYSIS_DOMPRINTER_H

#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

template <>
struct DOTGraphTraits<DomTreeNode *> : public DefaultDOTGraphTraits {

  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  std::string getNodeLabel(DomTreeNode *Node, DomTreeNode *Graph) {

    BasicBlock *BB = Node->getBlock();

    if (!BB)
      return "Post dominance root node";

    if (isSimple())
      return DOTGraphTraits<DOTFuncInfo *>::getSimpleNodeLabel(BB, nullptr);

    return DOTGraphTraits<DOTFuncInfo *>::getCompleteNodeLabel(BB, nullptr);
  }
};

template <>
struct DOTGraphTraits<DominatorTree *>
    : public DOTGraphTraits<DomTreeNode *> {

  DOTGraphTraits(bool isSimple = false)
      : DOTGraphTraits<DomTreeNode *>(isSimple) {}

  static std::string getGraphName(DominatorTree *DT) {
    return "Dominator tree";
  }

  std::string getNodeLabel(DomTreeNode *Node, DominatorTree *G) {
    return DOTGraphTraits<DomTreeNode *>::getNodeLabel(Node,
                                                             G->getRootNode());
  }
};

template<>
struct DOTGraphTraits<PostDominatorTree *>
  : public DOTGraphTraits<DomTreeNode*> {

  DOTGraphTraits (bool isSimple=false)
    : DOTGraphTraits<DomTreeNode*>(isSimple) {}

  static std::string getGraphName(PostDominatorTree *DT) {
    return "Post dominator tree";
  }

  std::string getNodeLabel(DomTreeNode *Node,
                           PostDominatorTree *G) {
    return DOTGraphTraits<DomTreeNode*>::getNodeLabel(Node, G->getRootNode());
  }
};

struct DomViewer final : DOTGraphTraitsViewer<DominatorTreeAnalysis, false> {
  DomViewer() : DOTGraphTraitsViewer<DominatorTreeAnalysis, false>("dom") {}
};

struct DomOnlyViewer final : DOTGraphTraitsViewer<DominatorTreeAnalysis, true> {
  DomOnlyViewer()
      : DOTGraphTraitsViewer<DominatorTreeAnalysis, true>("domonly") {}
};

struct PostDomViewer final
    : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, false> {
  PostDomViewer()
      : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, false>("postdom") {}
};

struct PostDomOnlyViewer final
    : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, true> {
  PostDomOnlyViewer()
      : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, true>("postdomonly") {}
};

struct DomPrinter final : DOTGraphTraitsPrinter<DominatorTreeAnalysis, false> {
  DomPrinter() : DOTGraphTraitsPrinter<DominatorTreeAnalysis, false>("dom") {}
};

struct DomOnlyPrinter final
    : DOTGraphTraitsPrinter<DominatorTreeAnalysis, true> {
  DomOnlyPrinter()
      : DOTGraphTraitsPrinter<DominatorTreeAnalysis, true>("domonly") {}
};

struct PostDomPrinter final
    : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, false> {
  PostDomPrinter()
      : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, false>("postdom") {}
};

struct PostDomOnlyPrinter final
    : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, true> {
  PostDomOnlyPrinter()
      : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, true>("postdomonly") {}
};
} // namespace llvm

namespace llvm {
  class FunctionPass;
  FunctionPass *createDomPrinterWrapperPassPass();
  FunctionPass *createDomOnlyPrinterWrapperPassPass();
  FunctionPass *createDomViewerWrapperPassPass();
  FunctionPass *createDomOnlyViewerWrapperPassPass();
  FunctionPass *createPostDomPrinterWrapperPassPass();
  FunctionPass *createPostDomOnlyPrinterWrapperPassPass();
  FunctionPass *createPostDomViewerWrapperPassPass();
  FunctionPass *createPostDomOnlyViewerWrapperPassPass();
} // End llvm namespace

#endif
