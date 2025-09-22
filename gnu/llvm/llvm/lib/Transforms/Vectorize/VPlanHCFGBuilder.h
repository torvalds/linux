//===-- VPlanHCFGBuilder.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the VPlanHCFGBuilder class which contains the public
/// interface (buildHierarchicalCFG) to build a VPlan-based Hierarchical CFG
/// (H-CFG) for an incoming IR.
///
/// A H-CFG in VPlan is a control-flow graph whose nodes are VPBasicBlocks
/// and/or VPRegionBlocks (i.e., other H-CFGs). The outermost H-CFG of a VPlan
/// consists of a VPRegionBlock, denoted Top Region, which encloses any other
/// VPBlockBase in the H-CFG. This guarantees that any VPBlockBase in the H-CFG
/// other than the Top Region will have a parent VPRegionBlock and allows us
/// to easily add more nodes before/after the main vector loop (such as the
/// reduction epilogue).
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPLAN_VPLANHCFGBUILDER_H
#define LLVM_TRANSFORMS_VECTORIZE_VPLAN_VPLANHCFGBUILDER_H

#include "VPlanDominatorTree.h"

namespace llvm {

class Loop;
class LoopInfo;
class VPRegionBlock;
class VPlan;
class VPlanTestBase;

/// Main class to build the VPlan H-CFG for an incoming IR.
class VPlanHCFGBuilder {
  friend VPlanTestBase;

private:
  // The outermost loop of the input loop nest considered for vectorization.
  Loop *TheLoop;

  // Loop Info analysis.
  LoopInfo *LI;

  // The VPlan that will contain the H-CFG we are building.
  VPlan &Plan;

  // Dominator analysis for VPlan plain CFG to be used in the
  // construction of the H-CFG. This analysis is no longer valid once regions
  // are introduced.
  VPDominatorTree VPDomTree;

  /// Build plain CFG for TheLoop and connects it to Plan's entry.
  void buildPlainCFG();

public:
  VPlanHCFGBuilder(Loop *Lp, LoopInfo *LI, VPlan &P)
      : TheLoop(Lp), LI(LI), Plan(P) {}

  /// Build H-CFG for TheLoop and update Plan accordingly.
  void buildHierarchicalCFG();
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_VPLAN_VPLANHCFGBUILDER_H
