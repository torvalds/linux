//===- llvm/Analysis/DDG.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the Data-Dependence Graph (DDG).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DDG_H
#define LLVM_ANALYSIS_DDG_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DirectedGraph.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/DependenceGraphBuilder.h"
#include "llvm/Analysis/LoopAnalysisManager.h"

namespace llvm {
class Function;
class Loop;
class LoopInfo;
class DDGNode;
class DDGEdge;
using DDGNodeBase = DGNode<DDGNode, DDGEdge>;
using DDGEdgeBase = DGEdge<DDGNode, DDGEdge>;
using DDGBase = DirectedGraph<DDGNode, DDGEdge>;
class LPMUpdater;

/// Data Dependence Graph Node
/// The graph can represent the following types of nodes:
/// 1. Single instruction node containing just one instruction.
/// 2. Multiple instruction node where two or more instructions from
///    the same basic block are merged into one node.
/// 3. Pi-block node which is a group of other DDG nodes that are part of a
///    strongly-connected component of the graph.
///    A pi-block node contains more than one single or multiple instruction
///    nodes. The root node cannot be part of a pi-block.
/// 4. Root node is a special node that connects to all components such that
///    there is always a path from it to any node in the graph.
class DDGNode : public DDGNodeBase {
public:
  using InstructionListType = SmallVectorImpl<Instruction *>;

  enum class NodeKind {
    Unknown,
    SingleInstruction,
    MultiInstruction,
    PiBlock,
    Root,
  };

  DDGNode() = delete;
  DDGNode(const NodeKind K) : Kind(K) {}
  DDGNode(const DDGNode &N) = default;
  DDGNode(DDGNode &&N) : DDGNodeBase(std::move(N)), Kind(N.Kind) {}
  virtual ~DDGNode() = 0;

  DDGNode &operator=(const DDGNode &N) {
    DGNode::operator=(N);
    Kind = N.Kind;
    return *this;
  }

  DDGNode &operator=(DDGNode &&N) {
    DGNode::operator=(std::move(N));
    Kind = N.Kind;
    return *this;
  }

  /// Getter for the kind of this node.
  NodeKind getKind() const { return Kind; }

  /// Collect a list of instructions, in \p IList, for which predicate \p Pred
  /// evaluates to true when iterating over instructions of this node. Return
  /// true if at least one instruction was collected, and false otherwise.
  bool collectInstructions(llvm::function_ref<bool(Instruction *)> const &Pred,
                           InstructionListType &IList) const;

protected:
  /// Setter for the kind of this node.
  void setKind(NodeKind K) { Kind = K; }

private:
  NodeKind Kind;
};

/// Subclass of DDGNode representing the root node of the graph.
/// There should only be one such node in a given graph.
class RootDDGNode : public DDGNode {
public:
  RootDDGNode() : DDGNode(NodeKind::Root) {}
  RootDDGNode(const RootDDGNode &N) = delete;
  RootDDGNode(RootDDGNode &&N) : DDGNode(std::move(N)) {}
  ~RootDDGNode() = default;

  /// Define classof to be able to use isa<>, cast<>, dyn_cast<>, etc.
  static bool classof(const DDGNode *N) {
    return N->getKind() == NodeKind::Root;
  }
  static bool classof(const RootDDGNode *N) { return true; }
};

/// Subclass of DDGNode representing single or multi-instruction nodes.
class SimpleDDGNode : public DDGNode {
  friend class DDGBuilder;

public:
  SimpleDDGNode() = delete;
  SimpleDDGNode(Instruction &I);
  SimpleDDGNode(const SimpleDDGNode &N);
  SimpleDDGNode(SimpleDDGNode &&N);
  ~SimpleDDGNode();

  SimpleDDGNode &operator=(const SimpleDDGNode &N) = default;

  SimpleDDGNode &operator=(SimpleDDGNode &&N) {
    DDGNode::operator=(std::move(N));
    InstList = std::move(N.InstList);
    return *this;
  }

  /// Get the list of instructions in this node.
  const InstructionListType &getInstructions() const {
    assert(!InstList.empty() && "Instruction List is empty.");
    return InstList;
  }
  InstructionListType &getInstructions() {
    return const_cast<InstructionListType &>(
        static_cast<const SimpleDDGNode *>(this)->getInstructions());
  }

  /// Get the first/last instruction in the node.
  Instruction *getFirstInstruction() const { return getInstructions().front(); }
  Instruction *getLastInstruction() const { return getInstructions().back(); }

  /// Define classof to be able to use isa<>, cast<>, dyn_cast<>, etc.
  static bool classof(const DDGNode *N) {
    return N->getKind() == NodeKind::SingleInstruction ||
           N->getKind() == NodeKind::MultiInstruction;
  }
  static bool classof(const SimpleDDGNode *N) { return true; }

private:
  /// Append the list of instructions in \p Input to this node.
  void appendInstructions(const InstructionListType &Input) {
    setKind((InstList.size() == 0 && Input.size() == 1)
                ? NodeKind::SingleInstruction
                : NodeKind::MultiInstruction);
    llvm::append_range(InstList, Input);
  }
  void appendInstructions(const SimpleDDGNode &Input) {
    appendInstructions(Input.getInstructions());
  }

  /// List of instructions associated with a single or multi-instruction node.
  SmallVector<Instruction *, 2> InstList;
};

/// Subclass of DDGNode representing a pi-block. A pi-block represents a group
/// of DDG nodes that are part of a strongly-connected component of the graph.
/// Replacing all the SCCs with pi-blocks results in an acyclic representation
/// of the DDG. For example if we have:
/// {a -> b}, {b -> c, d}, {c -> a}
/// the cycle a -> b -> c -> a is abstracted into a pi-block "p" as follows:
/// {p -> d} with "p" containing: {a -> b}, {b -> c}, {c -> a}
class PiBlockDDGNode : public DDGNode {
public:
  using PiNodeList = SmallVector<DDGNode *, 4>;

  PiBlockDDGNode() = delete;
  PiBlockDDGNode(const PiNodeList &List);
  PiBlockDDGNode(const PiBlockDDGNode &N);
  PiBlockDDGNode(PiBlockDDGNode &&N);
  ~PiBlockDDGNode();

  PiBlockDDGNode &operator=(const PiBlockDDGNode &N) = default;

  PiBlockDDGNode &operator=(PiBlockDDGNode &&N) {
    DDGNode::operator=(std::move(N));
    NodeList = std::move(N.NodeList);
    return *this;
  }

  /// Get the list of nodes in this pi-block.
  const PiNodeList &getNodes() const {
    assert(!NodeList.empty() && "Node list is empty.");
    return NodeList;
  }
  PiNodeList &getNodes() {
    return const_cast<PiNodeList &>(
        static_cast<const PiBlockDDGNode *>(this)->getNodes());
  }

  /// Define classof to be able to use isa<>, cast<>, dyn_cast<>, etc.
  static bool classof(const DDGNode *N) {
    return N->getKind() == NodeKind::PiBlock;
  }

private:
  /// List of nodes in this pi-block.
  PiNodeList NodeList;
};

/// Data Dependency Graph Edge.
/// An edge in the DDG can represent a def-use relationship or
/// a memory dependence based on the result of DependenceAnalysis.
/// A rooted edge connects the root node to one of the components
/// of the graph.
class DDGEdge : public DDGEdgeBase {
public:
  /// The kind of edge in the DDG
  enum class EdgeKind {
    Unknown,
    RegisterDefUse,
    MemoryDependence,
    Rooted,
    Last = Rooted // Must be equal to the largest enum value.
  };

  explicit DDGEdge(DDGNode &N) = delete;
  DDGEdge(DDGNode &N, EdgeKind K) : DDGEdgeBase(N), Kind(K) {}
  DDGEdge(const DDGEdge &E) : DDGEdgeBase(E), Kind(E.getKind()) {}
  DDGEdge(DDGEdge &&E) : DDGEdgeBase(std::move(E)), Kind(E.Kind) {}
  DDGEdge &operator=(const DDGEdge &E) = default;

  DDGEdge &operator=(DDGEdge &&E) {
    DDGEdgeBase::operator=(std::move(E));
    Kind = E.Kind;
    return *this;
  }

  /// Get the edge kind
  EdgeKind getKind() const { return Kind; };

  /// Return true if this is a def-use edge, and false otherwise.
  bool isDefUse() const { return Kind == EdgeKind::RegisterDefUse; }

  /// Return true if this is a memory dependence edge, and false otherwise.
  bool isMemoryDependence() const { return Kind == EdgeKind::MemoryDependence; }

  /// Return true if this is an edge stemming from the root node, and false
  /// otherwise.
  bool isRooted() const { return Kind == EdgeKind::Rooted; }

private:
  EdgeKind Kind;
};

/// Encapsulate some common data and functionality needed for different
/// variations of data dependence graphs.
template <typename NodeType> class DependenceGraphInfo {
public:
  using DependenceList = SmallVector<std::unique_ptr<Dependence>, 1>;

  DependenceGraphInfo() = delete;
  DependenceGraphInfo(const DependenceGraphInfo &G) = delete;
  DependenceGraphInfo(const std::string &N, const DependenceInfo &DepInfo)
      : Name(N), DI(DepInfo), Root(nullptr) {}
  DependenceGraphInfo(DependenceGraphInfo &&G)
      : Name(std::move(G.Name)), DI(std::move(G.DI)), Root(G.Root) {}
  virtual ~DependenceGraphInfo() = default;

  /// Return the label that is used to name this graph.
  StringRef getName() const { return Name; }

  /// Return the root node of the graph.
  NodeType &getRoot() const {
    assert(Root && "Root node is not available yet. Graph construction may "
                   "still be in progress\n");
    return *Root;
  }

  /// Collect all the data dependency infos coming from any pair of memory
  /// accesses from \p Src to \p Dst, and store them into \p Deps. Return true
  /// if a dependence exists, and false otherwise.
  bool getDependencies(const NodeType &Src, const NodeType &Dst,
                       DependenceList &Deps) const;

  /// Return a string representing the type of dependence that the dependence
  /// analysis identified between the two given nodes. This function assumes
  /// that there is a memory dependence between the given two nodes.
  std::string getDependenceString(const NodeType &Src,
                                  const NodeType &Dst) const;

protected:
  // Name of the graph.
  std::string Name;

  // Store a copy of DependenceInfo in the graph, so that individual memory
  // dependencies don't need to be stored. Instead when the dependence is
  // queried it is recomputed using @DI.
  const DependenceInfo DI;

  // A special node in the graph that has an edge to every connected component of
  // the graph, to ensure all nodes are reachable in a graph walk.
  NodeType *Root = nullptr;
};

using DDGInfo = DependenceGraphInfo<DDGNode>;

/// Data Dependency Graph
class DataDependenceGraph : public DDGBase, public DDGInfo {
  friend AbstractDependenceGraphBuilder<DataDependenceGraph>;
  friend class DDGBuilder;

public:
  using NodeType = DDGNode;
  using EdgeType = DDGEdge;

  DataDependenceGraph() = delete;
  DataDependenceGraph(const DataDependenceGraph &G) = delete;
  DataDependenceGraph(DataDependenceGraph &&G)
      : DDGBase(std::move(G)), DDGInfo(std::move(G)) {}
  DataDependenceGraph(Function &F, DependenceInfo &DI);
  DataDependenceGraph(Loop &L, LoopInfo &LI, DependenceInfo &DI);
  ~DataDependenceGraph();

  /// If node \p N belongs to a pi-block return a pointer to the pi-block,
  /// otherwise return null.
  const PiBlockDDGNode *getPiBlock(const NodeType &N) const;

protected:
  /// Add node \p N to the graph, if it's not added yet, and keep track of the
  /// root node as well as pi-blocks and their members. Return true if node is
  /// successfully added.
  bool addNode(NodeType &N);

private:
  using PiBlockMapType = DenseMap<const NodeType *, const PiBlockDDGNode *>;

  /// Mapping from graph nodes to their containing pi-blocks. If a node is not
  /// part of a pi-block, it will not appear in this map.
  PiBlockMapType PiBlockMap;
};

/// Concrete implementation of a pure data dependence graph builder. This class
/// provides custom implementation for the pure-virtual functions used in the
/// generic dependence graph build algorithm.
///
/// For information about time complexity of the build algorithm see the
/// comments near the declaration of AbstractDependenceGraphBuilder.
class DDGBuilder : public AbstractDependenceGraphBuilder<DataDependenceGraph> {
public:
  DDGBuilder(DataDependenceGraph &G, DependenceInfo &D,
             const BasicBlockListType &BBs)
      : AbstractDependenceGraphBuilder(G, D, BBs) {}
  DDGNode &createRootNode() final {
    auto *RN = new RootDDGNode();
    assert(RN && "Failed to allocate memory for DDG root node.");
    Graph.addNode(*RN);
    return *RN;
  }
  DDGNode &createFineGrainedNode(Instruction &I) final {
    auto *SN = new SimpleDDGNode(I);
    assert(SN && "Failed to allocate memory for simple DDG node.");
    Graph.addNode(*SN);
    return *SN;
  }
  DDGNode &createPiBlock(const NodeListType &L) final {
    auto *Pi = new PiBlockDDGNode(L);
    assert(Pi && "Failed to allocate memory for pi-block node.");
    Graph.addNode(*Pi);
    return *Pi;
  }
  DDGEdge &createDefUseEdge(DDGNode &Src, DDGNode &Tgt) final {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::RegisterDefUse);
    assert(E && "Failed to allocate memory for edge");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }
  DDGEdge &createMemoryEdge(DDGNode &Src, DDGNode &Tgt) final {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::MemoryDependence);
    assert(E && "Failed to allocate memory for edge");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }
  DDGEdge &createRootedEdge(DDGNode &Src, DDGNode &Tgt) final {
    auto *E = new DDGEdge(Tgt, DDGEdge::EdgeKind::Rooted);
    assert(E && "Failed to allocate memory for edge");
    assert(isa<RootDDGNode>(Src) && "Expected root node");
    Graph.connect(Src, Tgt, *E);
    return *E;
  }

  const NodeListType &getNodesInPiBlock(const DDGNode &N) final {
    auto *PiNode = dyn_cast<const PiBlockDDGNode>(&N);
    assert(PiNode && "Expected a pi-block node.");
    return PiNode->getNodes();
  }

  /// Return true if the two nodes \pSrc and \pTgt are both simple nodes and
  /// the consecutive instructions after merging belong to the same basic block.
  bool areNodesMergeable(const DDGNode &Src, const DDGNode &Tgt) const final;
  void mergeNodes(DDGNode &Src, DDGNode &Tgt) final;
  bool shouldSimplify() const final;
  bool shouldCreatePiBlocks() const final;
};

raw_ostream &operator<<(raw_ostream &OS, const DDGNode &N);
raw_ostream &operator<<(raw_ostream &OS, const DDGNode::NodeKind K);
raw_ostream &operator<<(raw_ostream &OS, const DDGEdge &E);
raw_ostream &operator<<(raw_ostream &OS, const DDGEdge::EdgeKind K);
raw_ostream &operator<<(raw_ostream &OS, const DataDependenceGraph &G);

//===--------------------------------------------------------------------===//
// DDG Analysis Passes
//===--------------------------------------------------------------------===//

/// Analysis pass that builds the DDG for a loop.
class DDGAnalysis : public AnalysisInfoMixin<DDGAnalysis> {
public:
  using Result = std::unique_ptr<DataDependenceGraph>;
  Result run(Loop &L, LoopAnalysisManager &AM, LoopStandardAnalysisResults &AR);

private:
  friend AnalysisInfoMixin<DDGAnalysis>;
  static AnalysisKey Key;
};

/// Textual printer pass for the DDG of a loop.
class DDGAnalysisPrinterPass : public PassInfoMixin<DDGAnalysisPrinterPass> {
public:
  explicit DDGAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
  static bool isRequired() { return true; }

private:
  raw_ostream &OS;
};

//===--------------------------------------------------------------------===//
// DependenceGraphInfo Implementation
//===--------------------------------------------------------------------===//

template <typename NodeType>
bool DependenceGraphInfo<NodeType>::getDependencies(
    const NodeType &Src, const NodeType &Dst, DependenceList &Deps) const {
  assert(Deps.empty() && "Expected empty output list at the start.");

  // List of memory access instructions from src and dst nodes.
  SmallVector<Instruction *, 8> SrcIList, DstIList;
  auto isMemoryAccess = [](const Instruction *I) {
    return I->mayReadOrWriteMemory();
  };
  Src.collectInstructions(isMemoryAccess, SrcIList);
  Dst.collectInstructions(isMemoryAccess, DstIList);

  for (auto *SrcI : SrcIList)
    for (auto *DstI : DstIList)
      if (auto Dep =
              const_cast<DependenceInfo *>(&DI)->depends(SrcI, DstI, true))
        Deps.push_back(std::move(Dep));

  return !Deps.empty();
}

template <typename NodeType>
std::string
DependenceGraphInfo<NodeType>::getDependenceString(const NodeType &Src,
                                                   const NodeType &Dst) const {
  std::string Str;
  raw_string_ostream OS(Str);
  DependenceList Deps;
  if (!getDependencies(Src, Dst, Deps))
    return Str;
  interleaveComma(Deps, OS, [&](const std::unique_ptr<Dependence> &D) {
    D->dump(OS);
    // Remove the extra new-line character printed by the dump
    // method
    if (Str.back() == '\n')
      Str.pop_back();
  });

  return Str;
}

//===--------------------------------------------------------------------===//
// GraphTraits specializations for the DDG
//===--------------------------------------------------------------------===//

/// non-const versions of the grapth trait specializations for DDG
template <> struct GraphTraits<DDGNode *> {
  using NodeRef = DDGNode *;

  static DDGNode *DDGGetTargetNode(DGEdge<DDGNode, DDGEdge> *P) {
    return &P->getTargetNode();
  }

  // Provide a mapped iterator so that the GraphTrait-based implementations can
  // find the target nodes without having to explicitly go through the edges.
  using ChildIteratorType =
      mapped_iterator<DDGNode::iterator, decltype(&DDGGetTargetNode)>;
  using ChildEdgeIteratorType = DDGNode::iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &DDGGetTargetNode);
  }
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &DDGGetTargetNode);
  }

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->begin();
  }
  static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template <>
struct GraphTraits<DataDependenceGraph *> : public GraphTraits<DDGNode *> {
  using nodes_iterator = DataDependenceGraph::iterator;
  static NodeRef getEntryNode(DataDependenceGraph *DG) {
    return &DG->getRoot();
  }
  static nodes_iterator nodes_begin(DataDependenceGraph *DG) {
    return DG->begin();
  }
  static nodes_iterator nodes_end(DataDependenceGraph *DG) { return DG->end(); }
};

/// const versions of the grapth trait specializations for DDG
template <> struct GraphTraits<const DDGNode *> {
  using NodeRef = const DDGNode *;

  static const DDGNode *DDGGetTargetNode(const DGEdge<DDGNode, DDGEdge> *P) {
    return &P->getTargetNode();
  }

  // Provide a mapped iterator so that the GraphTrait-based implementations can
  // find the target nodes without having to explicitly go through the edges.
  using ChildIteratorType =
      mapped_iterator<DDGNode::const_iterator, decltype(&DDGGetTargetNode)>;
  using ChildEdgeIteratorType = DDGNode::const_iterator;

  static NodeRef getEntryNode(NodeRef N) { return N; }
  static ChildIteratorType child_begin(NodeRef N) {
    return ChildIteratorType(N->begin(), &DDGGetTargetNode);
  }
  static ChildIteratorType child_end(NodeRef N) {
    return ChildIteratorType(N->end(), &DDGGetTargetNode);
  }

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->begin();
  }
  static ChildEdgeIteratorType child_edge_end(NodeRef N) { return N->end(); }
};

template <>
struct GraphTraits<const DataDependenceGraph *>
    : public GraphTraits<const DDGNode *> {
  using nodes_iterator = DataDependenceGraph::const_iterator;
  static NodeRef getEntryNode(const DataDependenceGraph *DG) {
    return &DG->getRoot();
  }
  static nodes_iterator nodes_begin(const DataDependenceGraph *DG) {
    return DG->begin();
  }
  static nodes_iterator nodes_end(const DataDependenceGraph *DG) {
    return DG->end();
  }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_DDG_H
