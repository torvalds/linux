//===- DDG.cpp - Data Dependence Graph -------------------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The implementation for the data dependence graph.
//===----------------------------------------------------------------------===//
#include "llvm/Analysis/DDG.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> SimplifyDDG(
    "ddg-simplify", cl::init(true), cl::Hidden,
    cl::desc(
        "Simplify DDG by merging nodes that have less interesting edges."));

static cl::opt<bool> CreatePiBlocks("ddg-pi-blocks", cl::init(true), cl::Hidden,
                                    cl::desc("Create pi-block nodes."));

#define DEBUG_TYPE "ddg"

template class llvm::DGEdge<DDGNode, DDGEdge>;
template class llvm::DGNode<DDGNode, DDGEdge>;
template class llvm::DirectedGraph<DDGNode, DDGEdge>;

//===--------------------------------------------------------------------===//
// DDGNode implementation
//===--------------------------------------------------------------------===//
DDGNode::~DDGNode() = default;

bool DDGNode::collectInstructions(
    llvm::function_ref<bool(Instruction *)> const &Pred,
    InstructionListType &IList) const {
  assert(IList.empty() && "Expected the IList to be empty on entry.");
  if (isa<SimpleDDGNode>(this)) {
    for (Instruction *I : cast<const SimpleDDGNode>(this)->getInstructions())
      if (Pred(I))
        IList.push_back(I);
  } else if (isa<PiBlockDDGNode>(this)) {
    for (const DDGNode *PN : cast<const PiBlockDDGNode>(this)->getNodes()) {
      assert(!isa<PiBlockDDGNode>(PN) && "Nested PiBlocks are not supported.");
      SmallVector<Instruction *, 8> TmpIList;
      PN->collectInstructions(Pred, TmpIList);
      llvm::append_range(IList, TmpIList);
    }
  } else
    llvm_unreachable("unimplemented type of node");
  return !IList.empty();
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const DDGNode::NodeKind K) {
  const char *Out;
  switch (K) {
  case DDGNode::NodeKind::SingleInstruction:
    Out = "single-instruction";
    break;
  case DDGNode::NodeKind::MultiInstruction:
    Out = "multi-instruction";
    break;
  case DDGNode::NodeKind::PiBlock:
    Out = "pi-block";
    break;
  case DDGNode::NodeKind::Root:
    Out = "root";
    break;
  case DDGNode::NodeKind::Unknown:
    Out = "?? (error)";
    break;
  }
  OS << Out;
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const DDGNode &N) {
  OS << "Node Address:" << &N << ":" << N.getKind() << "\n";
  if (isa<SimpleDDGNode>(N)) {
    OS << " Instructions:\n";
    for (const Instruction *I : cast<const SimpleDDGNode>(N).getInstructions())
      OS.indent(2) << *I << "\n";
  } else if (isa<PiBlockDDGNode>(&N)) {
    OS << "--- start of nodes in pi-block ---\n";
    auto &Nodes = cast<const PiBlockDDGNode>(&N)->getNodes();
    unsigned Count = 0;
    for (const DDGNode *N : Nodes)
      OS << *N << (++Count == Nodes.size() ? "" : "\n");
    OS << "--- end of nodes in pi-block ---\n";
  } else if (!isa<RootDDGNode>(N))
    llvm_unreachable("unimplemented type of node");

  OS << (N.getEdges().empty() ? " Edges:none!\n" : " Edges:\n");
  for (const auto &E : N.getEdges())
    OS.indent(2) << *E;
  return OS;
}

//===--------------------------------------------------------------------===//
// SimpleDDGNode implementation
//===--------------------------------------------------------------------===//

SimpleDDGNode::SimpleDDGNode(Instruction &I)
    : DDGNode(NodeKind::SingleInstruction) {
  assert(InstList.empty() && "Expected empty list.");
  InstList.push_back(&I);
}

SimpleDDGNode::SimpleDDGNode(const SimpleDDGNode &N)
    : DDGNode(N), InstList(N.InstList) {
  assert(((getKind() == NodeKind::SingleInstruction && InstList.size() == 1) ||
          (getKind() == NodeKind::MultiInstruction && InstList.size() > 1)) &&
         "constructing from invalid simple node.");
}

SimpleDDGNode::SimpleDDGNode(SimpleDDGNode &&N)
    : DDGNode(std::move(N)), InstList(std::move(N.InstList)) {
  assert(((getKind() == NodeKind::SingleInstruction && InstList.size() == 1) ||
          (getKind() == NodeKind::MultiInstruction && InstList.size() > 1)) &&
         "constructing from invalid simple node.");
}

SimpleDDGNode::~SimpleDDGNode() { InstList.clear(); }

//===--------------------------------------------------------------------===//
// PiBlockDDGNode implementation
//===--------------------------------------------------------------------===//

PiBlockDDGNode::PiBlockDDGNode(const PiNodeList &List)
    : DDGNode(NodeKind::PiBlock), NodeList(List) {
  assert(!NodeList.empty() && "pi-block node constructed with an empty list.");
}

PiBlockDDGNode::PiBlockDDGNode(const PiBlockDDGNode &N)
    : DDGNode(N), NodeList(N.NodeList) {
  assert(getKind() == NodeKind::PiBlock && !NodeList.empty() &&
         "constructing from invalid pi-block node.");
}

PiBlockDDGNode::PiBlockDDGNode(PiBlockDDGNode &&N)
    : DDGNode(std::move(N)), NodeList(std::move(N.NodeList)) {
  assert(getKind() == NodeKind::PiBlock && !NodeList.empty() &&
         "constructing from invalid pi-block node.");
}

PiBlockDDGNode::~PiBlockDDGNode() { NodeList.clear(); }

//===--------------------------------------------------------------------===//
// DDGEdge implementation
//===--------------------------------------------------------------------===//

raw_ostream &llvm::operator<<(raw_ostream &OS, const DDGEdge::EdgeKind K) {
  const char *Out;
  switch (K) {
  case DDGEdge::EdgeKind::RegisterDefUse:
    Out = "def-use";
    break;
  case DDGEdge::EdgeKind::MemoryDependence:
    Out = "memory";
    break;
  case DDGEdge::EdgeKind::Rooted:
    Out = "rooted";
    break;
  case DDGEdge::EdgeKind::Unknown:
    Out = "?? (error)";
    break;
  }
  OS << Out;
  return OS;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const DDGEdge &E) {
  OS << "[" << E.getKind() << "] to " << &E.getTargetNode() << "\n";
  return OS;
}

//===--------------------------------------------------------------------===//
// DataDependenceGraph implementation
//===--------------------------------------------------------------------===//
using BasicBlockListType = SmallVector<BasicBlock *, 8>;

DataDependenceGraph::DataDependenceGraph(Function &F, DependenceInfo &D)
    : DependenceGraphInfo(F.getName().str(), D) {
  // Put the basic blocks in program order for correct dependence
  // directions.
  BasicBlockListType BBList;
  for (const auto &SCC : make_range(scc_begin(&F), scc_end(&F)))
    append_range(BBList, SCC);
  std::reverse(BBList.begin(), BBList.end());
  DDGBuilder(*this, D, BBList).populate();
}

DataDependenceGraph::DataDependenceGraph(Loop &L, LoopInfo &LI,
                                         DependenceInfo &D)
    : DependenceGraphInfo(Twine(L.getHeader()->getParent()->getName() + "." +
                                L.getHeader()->getName())
                              .str(),
                          D) {
  // Put the basic blocks in program order for correct dependence
  // directions.
  LoopBlocksDFS DFS(&L);
  DFS.perform(&LI);
  BasicBlockListType BBList;
  append_range(BBList, make_range(DFS.beginRPO(), DFS.endRPO()));
  DDGBuilder(*this, D, BBList).populate();
}

DataDependenceGraph::~DataDependenceGraph() {
  for (auto *N : Nodes) {
    for (auto *E : *N)
      delete E;
    delete N;
  }
}

bool DataDependenceGraph::addNode(DDGNode &N) {
  if (!DDGBase::addNode(N))
    return false;

  // In general, if the root node is already created and linked, it is not safe
  // to add new nodes since they may be unreachable by the root. However,
  // pi-block nodes need to be added after the root node is linked, and they are
  // always reachable by the root, because they represent components that are
  // already reachable by root.
  auto *Pi = dyn_cast<PiBlockDDGNode>(&N);
  assert((!Root || Pi) &&
         "Root node is already added. No more nodes can be added.");

  if (isa<RootDDGNode>(N))
    Root = &N;

  if (Pi)
    for (DDGNode *NI : Pi->getNodes())
      PiBlockMap.insert(std::make_pair(NI, Pi));

  return true;
}

const PiBlockDDGNode *DataDependenceGraph::getPiBlock(const NodeType &N) const {
  if (!PiBlockMap.contains(&N))
    return nullptr;
  auto *Pi = PiBlockMap.find(&N)->second;
  assert(!PiBlockMap.contains(Pi) && "Nested pi-blocks detected.");
  return Pi;
}

raw_ostream &llvm::operator<<(raw_ostream &OS, const DataDependenceGraph &G) {
  for (DDGNode *Node : G)
    // Avoid printing nodes that are part of a pi-block twice. They will get
    // printed when the pi-block is printed.
    if (!G.getPiBlock(*Node))
      OS << *Node << "\n";
  OS << "\n";
  return OS;
}

//===--------------------------------------------------------------------===//
// DDGBuilder implementation
//===--------------------------------------------------------------------===//

bool DDGBuilder::areNodesMergeable(const DDGNode &Src,
                                   const DDGNode &Tgt) const {
  // Only merge two nodes if they are both simple nodes and the consecutive
  // instructions after merging belong to the same BB.
  const auto *SimpleSrc = dyn_cast<const SimpleDDGNode>(&Src);
  const auto *SimpleTgt = dyn_cast<const SimpleDDGNode>(&Tgt);
  if (!SimpleSrc || !SimpleTgt)
    return false;

  return SimpleSrc->getLastInstruction()->getParent() ==
         SimpleTgt->getFirstInstruction()->getParent();
}

void DDGBuilder::mergeNodes(DDGNode &A, DDGNode &B) {
  DDGEdge &EdgeToFold = A.back();
  assert(A.getEdges().size() == 1 && EdgeToFold.getTargetNode() == B &&
         "Expected A to have a single edge to B.");
  assert(isa<SimpleDDGNode>(&A) && isa<SimpleDDGNode>(&B) &&
         "Expected simple nodes");

  // Copy instructions from B to the end of A.
  cast<SimpleDDGNode>(&A)->appendInstructions(*cast<SimpleDDGNode>(&B));

  // Move to A any outgoing edges from B.
  for (DDGEdge *BE : B)
    Graph.connect(A, BE->getTargetNode(), *BE);

  A.removeEdge(EdgeToFold);
  destroyEdge(EdgeToFold);
  Graph.removeNode(B);
  destroyNode(B);
}

bool DDGBuilder::shouldSimplify() const { return SimplifyDDG; }

bool DDGBuilder::shouldCreatePiBlocks() const { return CreatePiBlocks; }

//===--------------------------------------------------------------------===//
// DDG Analysis Passes
//===--------------------------------------------------------------------===//

/// DDG as a loop pass.
DDGAnalysis::Result DDGAnalysis::run(Loop &L, LoopAnalysisManager &AM,
                                     LoopStandardAnalysisResults &AR) {
  Function *F = L.getHeader()->getParent();
  DependenceInfo DI(F, &AR.AA, &AR.SE, &AR.LI);
  return std::make_unique<DataDependenceGraph>(L, AR.LI, DI);
}
AnalysisKey DDGAnalysis::Key;

PreservedAnalyses DDGAnalysisPrinterPass::run(Loop &L, LoopAnalysisManager &AM,
                                              LoopStandardAnalysisResults &AR,
                                              LPMUpdater &U) {
  OS << "'DDG' for loop '" << L.getHeader()->getName() << "':\n";
  OS << *AM.getResult<DDGAnalysis>(L, AR);
  return PreservedAnalyses::all();
}
