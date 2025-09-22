//===- llvm/Analysis/DependenceGraphBuilder.h -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a builder interface that can be used to populate dependence
// graphs such as DDG and PDG.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DEPENDENCEGRAPHBUILDER_H
#define LLVM_ANALYSIS_DEPENDENCEGRAPHBUILDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

class BasicBlock;
class DependenceInfo;
class Instruction;

/// This abstract builder class defines a set of high-level steps for creating
/// DDG-like graphs. The client code is expected to inherit from this class and
/// define concrete implementation for each of the pure virtual functions used
/// in the high-level algorithm.
template <class GraphType> class AbstractDependenceGraphBuilder {
protected:
  using BasicBlockListType = SmallVectorImpl<BasicBlock *>;

private:
  using NodeType = typename GraphType::NodeType;
  using EdgeType = typename GraphType::EdgeType;

public:
  using ClassesType = EquivalenceClasses<BasicBlock *>;
  using NodeListType = SmallVector<NodeType *, 4>;

  AbstractDependenceGraphBuilder(GraphType &G, DependenceInfo &D,
                                 const BasicBlockListType &BBs)
      : Graph(G), DI(D), BBList(BBs) {}
  virtual ~AbstractDependenceGraphBuilder() = default;

  /// The main entry to the graph construction algorithm. It starts by
  /// creating nodes in increasing order of granularity and then
  /// adds def-use and memory edges. As one of the final stages, it
  /// also creates pi-block nodes to facilitate codegen in transformations
  /// that use dependence graphs.
  ///
  /// The algorithmic complexity of this implementation is O(V^2 * I^2), where V
  /// is the number of vertecies (nodes) and I is the number of instructions in
  /// each node. The total number of instructions, N, is equal to V * I,
  /// therefore the worst-case time complexity is O(N^2). The average time
  /// complexity is O((N^2)/2).
  void populate() {
    computeInstructionOrdinals();
    createFineGrainedNodes();
    createDefUseEdges();
    createMemoryDependencyEdges();
    simplify();
    createAndConnectRootNode();
    createPiBlocks();
    sortNodesTopologically();
  }

  /// Compute ordinal numbers for each instruction and store them in a map for
  /// future look up. These ordinals are used to compute node ordinals which are
  /// in turn used to order nodes that are part of a cycle.
  /// Instruction ordinals are assigned based on lexical program order.
  void computeInstructionOrdinals();

  /// Create fine grained nodes. These are typically atomic nodes that
  /// consist of a single instruction.
  void createFineGrainedNodes();

  /// Analyze the def-use chains and create edges from the nodes containing
  /// definitions to the nodes containing the uses.
  void createDefUseEdges();

  /// Analyze data dependencies that exist between memory loads or stores,
  /// in the graph nodes and create edges between them.
  void createMemoryDependencyEdges();

  /// Create a root node and add edges such that each node in the graph is
  /// reachable from the root.
  void createAndConnectRootNode();

  /// Apply graph abstraction to groups of nodes that belong to a strongly
  /// connected component of the graph to create larger compound nodes
  /// called pi-blocks. The purpose of this abstraction is to isolate sets of
  /// program elements that need to stay together during codegen and turn
  /// the dependence graph into an acyclic graph.
  void createPiBlocks();

  /// Go through all the nodes in the graph and collapse any two nodes
  /// 'a' and 'b' if all of the following are true:
  ///   - the only edge from 'a' is a def-use edge to 'b' and
  ///   - the only edge to 'b' is a def-use edge from 'a' and
  ///   - there is no cyclic edge from 'b' to 'a' and
  ///   - all instructions in 'a' and 'b' belong to the same basic block and
  ///   - both 'a' and 'b' are simple (single or multi instruction) nodes.
  void simplify();

  /// Topologically sort the graph nodes.
  void sortNodesTopologically();

protected:
  /// Create the root node of the graph.
  virtual NodeType &createRootNode() = 0;

  /// Create an atomic node in the graph given a single instruction.
  virtual NodeType &createFineGrainedNode(Instruction &I) = 0;

  /// Create a pi-block node in the graph representing a group of nodes in an
  /// SCC of the graph.
  virtual NodeType &createPiBlock(const NodeListType &L) = 0;

  /// Create a def-use edge going from \p Src to \p Tgt.
  virtual EdgeType &createDefUseEdge(NodeType &Src, NodeType &Tgt) = 0;

  /// Create a memory dependence edge going from \p Src to \p Tgt.
  virtual EdgeType &createMemoryEdge(NodeType &Src, NodeType &Tgt) = 0;

  /// Create a rooted edge going from \p Src to \p Tgt .
  virtual EdgeType &createRootedEdge(NodeType &Src, NodeType &Tgt) = 0;

  /// Given a pi-block node, return a vector of all the nodes contained within
  /// it.
  virtual const NodeListType &getNodesInPiBlock(const NodeType &N) = 0;

  /// Deallocate memory of edge \p E.
  virtual void destroyEdge(EdgeType &E) { delete &E; }

  /// Deallocate memory of node \p N.
  virtual void destroyNode(NodeType &N) { delete &N; }

  /// Return true if creation of pi-blocks are supported and desired,
  /// and false otherwise.
  virtual bool shouldCreatePiBlocks() const { return true; }

  /// Return true if graph simplification step is requested, and false
  /// otherwise.
  virtual bool shouldSimplify() const { return true; }

  /// Return true if it's safe to merge the two nodes.
  virtual bool areNodesMergeable(const NodeType &A,
                                 const NodeType &B) const = 0;

  /// Append the content of node \p B into node \p A and remove \p B and
  /// the edge between \p A and \p B from the graph.
  virtual void mergeNodes(NodeType &A, NodeType &B) = 0;

  /// Given an instruction \p I return its associated ordinal number.
  size_t getOrdinal(Instruction &I) {
    assert(InstOrdinalMap.contains(&I) &&
           "No ordinal computed for this instruction.");
    return InstOrdinalMap[&I];
  }

  /// Given a node \p N return its associated ordinal number.
  size_t getOrdinal(NodeType &N) {
    assert(NodeOrdinalMap.contains(&N) && "No ordinal computed for this node.");
    return NodeOrdinalMap[&N];
  }

  /// Map types to map instructions to nodes used when populating the graph.
  using InstToNodeMap = DenseMap<Instruction *, NodeType *>;

  /// Map Types to map instruction/nodes to an ordinal number.
  using InstToOrdinalMap = DenseMap<Instruction *, size_t>;
  using NodeToOrdinalMap = DenseMap<NodeType *, size_t>;

  /// Reference to the graph that gets built by a concrete implementation of
  /// this builder.
  GraphType &Graph;

  /// Dependence information used to create memory dependence edges in the
  /// graph.
  DependenceInfo &DI;

  /// The list of basic blocks to consider when building the graph.
  const BasicBlockListType &BBList;

  /// A mapping from instructions to the corresponding nodes in the graph.
  InstToNodeMap IMap;

  /// A mapping from each instruction to an ordinal number. This map is used to
  /// populate the \p NodeOrdinalMap.
  InstToOrdinalMap InstOrdinalMap;

  /// A mapping from nodes to an ordinal number. This map is used to sort nodes
  /// in a pi-block based on program order.
  NodeToOrdinalMap NodeOrdinalMap;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_DEPENDENCEGRAPHBUILDER_H
