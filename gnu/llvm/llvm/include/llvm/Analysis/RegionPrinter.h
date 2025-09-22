//===-- RegionPrinter.h - Region printer external interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines external functions that can be called to explicitly
// instantiate the region printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REGIONPRINTER_H
#define LLVM_ANALYSIS_REGIONPRINTER_H

#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/RegionInfo.h"

namespace llvm {
  class FunctionPass;
  class Function;
  class RegionInfo;

  FunctionPass *createRegionViewerPass();
  FunctionPass *createRegionOnlyViewerPass();
  FunctionPass *createRegionPrinterPass();
  FunctionPass *createRegionOnlyPrinterPass();

  template <>
  struct DOTGraphTraits<RegionNode *> : public DefaultDOTGraphTraits {
    DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

    std::string getNodeLabel(RegionNode *Node, RegionNode *Graph);
  };

#ifndef NDEBUG
  /// Open a viewer to display the GraphViz vizualization of the analysis
  /// result.
  ///
  /// Practical to call in the debugger.
  /// Includes the instructions in each BasicBlock.
  ///
  /// @param RI The analysis to display.
  void viewRegion(llvm::RegionInfo *RI);

  /// Analyze the regions of a function and open its GraphViz
  /// visualization in a viewer.
  ///
  /// Useful to call in the debugger.
  /// Includes the instructions in each BasicBlock.
  /// The result of a new analysis may differ from the RegionInfo the pass
  /// manager currently holds.
  ///
  /// @param F Function to analyze.
  void viewRegion(const llvm::Function *F);

  /// Open a viewer to display the GraphViz vizualization of the analysis
  /// result.
  ///
  /// Useful to call in the debugger.
  /// Shows only the BasicBlock names without their instructions.
  ///
  /// @param RI The analysis to display.
  void viewRegionOnly(llvm::RegionInfo *RI);

  /// Analyze the regions of a function and open its GraphViz
  /// visualization in a viewer.
  ///
  /// Useful to call in the debugger.
  /// Shows only the BasicBlock names without their instructions.
  /// The result of a new analysis may differ from the RegionInfo the pass
  /// manager currently holds.
  ///
  /// @param F Function to analyze.
  void viewRegionOnly(const llvm::Function *F);
#endif
} // End llvm namespace

#endif
