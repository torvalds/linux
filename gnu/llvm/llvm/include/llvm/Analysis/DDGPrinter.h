//===- llvm/Analysis/DDGPrinter.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// This file defines the DOT printer for the Data-Dependence Graph (DDG).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DDGPRINTER_H
#define LLVM_ANALYSIS_DDGPRINTER_H

#include "llvm/Analysis/DDG.h"
#include "llvm/Support/DOTGraphTraits.h"

namespace llvm {
class LPMUpdater;
class Loop;

//===--------------------------------------------------------------------===//
// Implementation of DDG DOT Printer for a loop.
//===--------------------------------------------------------------------===//
class DDGDotPrinterPass : public PassInfoMixin<DDGDotPrinterPass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
  static bool isRequired() { return true; }
};

//===--------------------------------------------------------------------===//
// Specialization of DOTGraphTraits.
//===--------------------------------------------------------------------===//
template <>
struct DOTGraphTraits<const DataDependenceGraph *>
    : public DefaultDOTGraphTraits {

  DOTGraphTraits(bool IsSimple = false) : DefaultDOTGraphTraits(IsSimple) {}

  /// Generate a title for the graph in DOT format
  std::string getGraphName(const DataDependenceGraph *G) {
    assert(G && "expected a valid pointer to the graph.");
    return "DDG for '" + std::string(G->getName()) + "'";
  }

  /// Print a DDG node either in concise form (-ddg-dot-only) or
  /// verbose mode (-ddg-dot).
  std::string getNodeLabel(const DDGNode *Node,
                           const DataDependenceGraph *Graph);

  /// Print attributes of an edge in the DDG graph. If the edge
  /// is a MemoryDependence edge, then detailed dependence info
  /// available from DependenceAnalysis is displayed.
  std::string
  getEdgeAttributes(const DDGNode *Node,
                    GraphTraits<const DDGNode *>::ChildIteratorType I,
                    const DataDependenceGraph *G);

  /// Do not print nodes that are part of a pi-block separately. They
  /// will be printed when their containing pi-block is being printed.
  bool isNodeHidden(const DDGNode *Node, const DataDependenceGraph *G);

private:
  /// Print a DDG node in concise form.
  static std::string getSimpleNodeLabel(const DDGNode *Node,
                                        const DataDependenceGraph *G);

  /// Print a DDG node with more information including containing instructions
  /// and detailed information about the dependence edges.
  static std::string getVerboseNodeLabel(const DDGNode *Node,
                                         const DataDependenceGraph *G);

  /// Print a DDG edge in concise form.
  static std::string getSimpleEdgeAttributes(const DDGNode *Src,
                                             const DDGEdge *Edge,
                                             const DataDependenceGraph *G);

  /// Print a DDG edge with more information including detailed information
  /// about the dependence edges.
  static std::string getVerboseEdgeAttributes(const DDGNode *Src,
                                              const DDGEdge *Edge,
                                              const DataDependenceGraph *G);
};

using DDGDotGraphTraits = DOTGraphTraits<const DataDependenceGraph *>;

} // namespace llvm

#endif // LLVM_ANALYSIS_DDGPRINTER_H
