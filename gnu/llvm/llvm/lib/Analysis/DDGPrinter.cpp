//===- DDGPrinter.cpp - DOT printer for the data dependence graph ----------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// This file defines the `-dot-ddg` analysis pass, which emits DDG in DOT format
// in a file named `ddg.<graph-name>.dot` for each loop  in a function.
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/DDGPrinter.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GraphWriter.h"

using namespace llvm;

static cl::opt<bool> DotOnly("dot-ddg-only", cl::Hidden,
                             cl::desc("simple ddg dot graph"));
static cl::opt<std::string> DDGDotFilenamePrefix(
    "dot-ddg-filename-prefix", cl::init("ddg"), cl::Hidden,
    cl::desc("The prefix used for the DDG dot file names."));

static void writeDDGToDotFile(DataDependenceGraph &G, bool DOnly = false);

//===--------------------------------------------------------------------===//
// Implementation of DDG DOT Printer for a loop
//===--------------------------------------------------------------------===//
PreservedAnalyses DDGDotPrinterPass::run(Loop &L, LoopAnalysisManager &AM,
                                         LoopStandardAnalysisResults &AR,
                                         LPMUpdater &U) {
  writeDDGToDotFile(*AM.getResult<DDGAnalysis>(L, AR), DotOnly);
  return PreservedAnalyses::all();
}

static void writeDDGToDotFile(DataDependenceGraph &G, bool DOnly) {
  std::string Filename =
      Twine(DDGDotFilenamePrefix + "." + G.getName() + ".dot").str();
  errs() << "Writing '" << Filename << "'...";

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);

  if (!EC)
    // We only provide the constant verson of the DOTGraphTrait specialization,
    // hence the conversion to const pointer
    WriteGraph(File, (const DataDependenceGraph *)&G, DOnly);
  else
    errs() << "  error opening file for writing!";
  errs() << "\n";
}

//===--------------------------------------------------------------------===//
// DDG DOT Printer Implementation
//===--------------------------------------------------------------------===//
std::string DDGDotGraphTraits::getNodeLabel(const DDGNode *Node,
                                            const DataDependenceGraph *Graph) {
  if (isSimple())
    return getSimpleNodeLabel(Node, Graph);
  else
    return getVerboseNodeLabel(Node, Graph);
}

std::string DDGDotGraphTraits::getEdgeAttributes(
    const DDGNode *Node, GraphTraits<const DDGNode *>::ChildIteratorType I,
    const DataDependenceGraph *G) {
  const DDGEdge *E = static_cast<const DDGEdge *>(*I.getCurrent());
  if (isSimple())
    return getSimpleEdgeAttributes(Node, E, G);
  else
    return getVerboseEdgeAttributes(Node, E, G);
}

bool DDGDotGraphTraits::isNodeHidden(const DDGNode *Node,
                                     const DataDependenceGraph *Graph) {
  if (isSimple() && isa<RootDDGNode>(Node))
    return true;
  assert(Graph && "expected a valid graph pointer");
  return Graph->getPiBlock(*Node) != nullptr;
}

std::string
DDGDotGraphTraits::getSimpleNodeLabel(const DDGNode *Node,
                                      const DataDependenceGraph *G) {
  std::string Str;
  raw_string_ostream OS(Str);
  if (isa<SimpleDDGNode>(Node))
    for (auto *II : static_cast<const SimpleDDGNode *>(Node)->getInstructions())
      OS << *II << "\n";
  else if (isa<PiBlockDDGNode>(Node))
    OS << "pi-block\nwith\n"
       << cast<PiBlockDDGNode>(Node)->getNodes().size() << " nodes\n";
  else if (isa<RootDDGNode>(Node))
    OS << "root\n";
  else
    llvm_unreachable("Unimplemented type of node");
  return OS.str();
}

std::string
DDGDotGraphTraits::getVerboseNodeLabel(const DDGNode *Node,
                                       const DataDependenceGraph *G) {
  std::string Str;
  raw_string_ostream OS(Str);
  OS << "<kind:" << Node->getKind() << ">\n";
  if (isa<SimpleDDGNode>(Node))
    for (auto *II : static_cast<const SimpleDDGNode *>(Node)->getInstructions())
      OS << *II << "\n";
  else if (isa<PiBlockDDGNode>(Node)) {
    OS << "--- start of nodes in pi-block ---\n";
    unsigned Count = 0;
    const auto &PNodes = cast<PiBlockDDGNode>(Node)->getNodes();
    for (auto *PN : PNodes) {
      OS << getVerboseNodeLabel(PN, G);
      if (++Count != PNodes.size())
        OS << "\n";
    }
    OS << "--- end of nodes in pi-block ---\n";
  } else if (isa<RootDDGNode>(Node))
    OS << "root\n";
  else
    llvm_unreachable("Unimplemented type of node");
  return OS.str();
}

std::string DDGDotGraphTraits::getSimpleEdgeAttributes(
    const DDGNode *Src, const DDGEdge *Edge, const DataDependenceGraph *G) {
  std::string Str;
  raw_string_ostream OS(Str);
  DDGEdge::EdgeKind Kind = Edge->getKind();
  OS << "label=\"[" << Kind << "]\"";
  return OS.str();
}

std::string DDGDotGraphTraits::getVerboseEdgeAttributes(
    const DDGNode *Src, const DDGEdge *Edge, const DataDependenceGraph *G) {
  std::string Str;
  raw_string_ostream OS(Str);
  DDGEdge::EdgeKind Kind = Edge->getKind();
  OS << "label=\"[";
  if (Kind == DDGEdge::EdgeKind::MemoryDependence)
    OS << G->getDependenceString(*Src, Edge->getTargetNode());
  else
    OS << Kind;
  OS << "]\"";
  return OS.str();
}
