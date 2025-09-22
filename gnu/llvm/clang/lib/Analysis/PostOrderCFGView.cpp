//===- PostOrderCFGView.cpp - Post order view of CFG blocks ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements post order view of the blocks in a CFG.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"

using namespace clang;

void PostOrderCFGView::anchor() {}

PostOrderCFGView::PostOrderCFGView(const CFG *cfg) {
  Blocks.reserve(cfg->getNumBlockIDs());
  CFGBlockSet BSet(cfg);

  for (po_iterator I = po_iterator::begin(cfg, BSet),
                   E = po_iterator::end(cfg, BSet); I != E; ++I) {
    BlockOrder[*I] = Blocks.size() + 1;
    Blocks.push_back(*I);
  }
}

std::unique_ptr<PostOrderCFGView>
PostOrderCFGView::create(AnalysisDeclContext &ctx) {
  const CFG *cfg = ctx.getCFG();
  if (!cfg)
    return nullptr;
  return std::make_unique<PostOrderCFGView>(cfg);
}

const void *PostOrderCFGView::getTag() { static int x; return &x; }

bool PostOrderCFGView::BlockOrderCompare::operator()(const CFGBlock *b1,
                                                     const CFGBlock *b2) const {
  PostOrderCFGView::BlockOrderTy::const_iterator b1It = POV.BlockOrder.find(b1);
  PostOrderCFGView::BlockOrderTy::const_iterator b2It = POV.BlockOrder.find(b2);

  unsigned b1V = (b1It == POV.BlockOrder.end()) ? 0 : b1It->second;
  unsigned b2V = (b2It == POV.BlockOrder.end()) ? 0 : b2It->second;
  return b1V > b2V;
}
