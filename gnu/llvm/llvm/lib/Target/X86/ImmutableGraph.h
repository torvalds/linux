//==========-- ImmutableGraph.h - A fast DAG implementation ---------=========//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Description: ImmutableGraph is a fast DAG implementation that cannot be
/// modified, except by creating a new ImmutableGraph. ImmutableGraph is
/// implemented as two arrays: one containing nodes, and one containing edges.
/// The advantages to this implementation are two-fold:
/// 1. Iteration and traversal operations benefit from cache locality.
/// 2. Operations on sets of nodes/edges are efficient, and representations of
///    those sets in memory are compact. For instance, a set of edges is
///    implemented as a bit vector, wherein each bit corresponds to one edge in
///    the edge array. This implies a lower bound of 64x spatial improvement
///    over, e.g., an llvm::DenseSet or llvm::SmallSet. It also means that
///    insert/erase/contains operations complete in negligible constant time:
///    insert and erase require one load and one store, and contains requires
///    just one load.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_IMMUTABLEGRAPH_H
#define LLVM_LIB_TARGET_X86_IMMUTABLEGRAPH_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace llvm {

template <typename NodeValueT, typename EdgeValueT> class ImmutableGraph {
  using Traits = GraphTraits<ImmutableGraph<NodeValueT, EdgeValueT> *>;
  template <typename> friend class ImmutableGraphBuilder;

public:
  using node_value_type = NodeValueT;
  using edge_value_type = EdgeValueT;
  using size_type = int;
  class Node;
  class Edge {
    friend class ImmutableGraph;
    template <typename> friend class ImmutableGraphBuilder;

    const Node *Dest;
    edge_value_type Value;

  public:
    const Node *getDest() const { return Dest; };
    const edge_value_type &getValue() const { return Value; }
  };
  class Node {
    friend class ImmutableGraph;
    template <typename> friend class ImmutableGraphBuilder;

    const Edge *Edges;
    node_value_type Value;

  public:
    const node_value_type &getValue() const { return Value; }

    const Edge *edges_begin() const { return Edges; }
    // Nodes are allocated sequentially. Edges for a node are stored together.
    // The end of this Node's edges is the beginning of the next node's edges.
    // An extra node was allocated to hold the end pointer for the last real
    // node.
    const Edge *edges_end() const { return (this + 1)->Edges; }
    ArrayRef<Edge> edges() const {
      return ArrayRef(edges_begin(), edges_end());
    }
  };

protected:
  ImmutableGraph(std::unique_ptr<Node[]> Nodes, std::unique_ptr<Edge[]> Edges,
                 size_type NodesSize, size_type EdgesSize)
      : Nodes(std::move(Nodes)), Edges(std::move(Edges)), NodesSize(NodesSize),
        EdgesSize(EdgesSize) {}
  ImmutableGraph(const ImmutableGraph &) = delete;
  ImmutableGraph(ImmutableGraph &&) = delete;
  ImmutableGraph &operator=(const ImmutableGraph &) = delete;
  ImmutableGraph &operator=(ImmutableGraph &&) = delete;

public:
  ArrayRef<Node> nodes() const { return ArrayRef(Nodes.get(), NodesSize); }
  const Node *nodes_begin() const { return nodes().begin(); }
  const Node *nodes_end() const { return nodes().end(); }

  ArrayRef<Edge> edges() const { return ArrayRef(Edges.get(), EdgesSize); }
  const Edge *edges_begin() const { return edges().begin(); }
  const Edge *edges_end() const { return edges().end(); }

  size_type nodes_size() const { return NodesSize; }
  size_type edges_size() const { return EdgesSize; }

  // Node N must belong to this ImmutableGraph.
  size_type getNodeIndex(const Node &N) const {
    return std::distance(nodes_begin(), &N);
  }
  // Edge E must belong to this ImmutableGraph.
  size_type getEdgeIndex(const Edge &E) const {
    return std::distance(edges_begin(), &E);
  }

  // FIXME: Could NodeSet and EdgeSet be templated to share code?
  class NodeSet {
    const ImmutableGraph &G;
    BitVector V;

  public:
    NodeSet(const ImmutableGraph &G, bool ContainsAll = false)
        : G{G}, V{static_cast<unsigned>(G.nodes_size()), ContainsAll} {}
    bool insert(const Node &N) {
      size_type Idx = G.getNodeIndex(N);
      bool AlreadyExists = V.test(Idx);
      V.set(Idx);
      return !AlreadyExists;
    }
    void erase(const Node &N) {
      size_type Idx = G.getNodeIndex(N);
      V.reset(Idx);
    }
    bool contains(const Node &N) const {
      size_type Idx = G.getNodeIndex(N);
      return V.test(Idx);
    }
    void clear() { V.reset(); }
    size_type empty() const { return V.none(); }
    /// Return the number of elements in the set
    size_type count() const { return V.count(); }
    /// Return the size of the set's domain
    size_type size() const { return V.size(); }
    /// Set union
    NodeSet &operator|=(const NodeSet &RHS) {
      assert(&this->G == &RHS.G);
      V |= RHS.V;
      return *this;
    }
    /// Set intersection
    NodeSet &operator&=(const NodeSet &RHS) {
      assert(&this->G == &RHS.G);
      V &= RHS.V;
      return *this;
    }
    /// Set disjoint union
    NodeSet &operator^=(const NodeSet &RHS) {
      assert(&this->G == &RHS.G);
      V ^= RHS.V;
      return *this;
    }

    using index_iterator = typename BitVector::const_set_bits_iterator;
    index_iterator index_begin() const { return V.set_bits_begin(); }
    index_iterator index_end() const { return V.set_bits_end(); }
    void set(size_type Idx) { V.set(Idx); }
    void reset(size_type Idx) { V.reset(Idx); }

    class iterator {
      const NodeSet &Set;
      size_type Current;

      void advance() {
        assert(Current != -1);
        Current = Set.V.find_next(Current);
      }

    public:
      iterator(const NodeSet &Set, size_type Begin)
          : Set{Set}, Current{Begin} {}
      iterator operator++(int) {
        iterator Tmp = *this;
        advance();
        return Tmp;
      }
      iterator &operator++() {
        advance();
        return *this;
      }
      Node *operator*() const {
        assert(Current != -1);
        return Set.G.nodes_begin() + Current;
      }
      bool operator==(const iterator &other) const {
        assert(&this->Set == &other.Set);
        return this->Current == other.Current;
      }
      bool operator!=(const iterator &other) const { return !(*this == other); }
    };

    iterator begin() const { return iterator{*this, V.find_first()}; }
    iterator end() const { return iterator{*this, -1}; }
  };

  class EdgeSet {
    const ImmutableGraph &G;
    BitVector V;

  public:
    EdgeSet(const ImmutableGraph &G, bool ContainsAll = false)
        : G{G}, V{static_cast<unsigned>(G.edges_size()), ContainsAll} {}
    bool insert(const Edge &E) {
      size_type Idx = G.getEdgeIndex(E);
      bool AlreadyExists = V.test(Idx);
      V.set(Idx);
      return !AlreadyExists;
    }
    void erase(const Edge &E) {
      size_type Idx = G.getEdgeIndex(E);
      V.reset(Idx);
    }
    bool contains(const Edge &E) const {
      size_type Idx = G.getEdgeIndex(E);
      return V.test(Idx);
    }
    void clear() { V.reset(); }
    bool empty() const { return V.none(); }
    /// Return the number of elements in the set
    size_type count() const { return V.count(); }
    /// Return the size of the set's domain
    size_type size() const { return V.size(); }
    /// Set union
    EdgeSet &operator|=(const EdgeSet &RHS) {
      assert(&this->G == &RHS.G);
      V |= RHS.V;
      return *this;
    }
    /// Set intersection
    EdgeSet &operator&=(const EdgeSet &RHS) {
      assert(&this->G == &RHS.G);
      V &= RHS.V;
      return *this;
    }
    /// Set disjoint union
    EdgeSet &operator^=(const EdgeSet &RHS) {
      assert(&this->G == &RHS.G);
      V ^= RHS.V;
      return *this;
    }

    using index_iterator = typename BitVector::const_set_bits_iterator;
    index_iterator index_begin() const { return V.set_bits_begin(); }
    index_iterator index_end() const { return V.set_bits_end(); }
    void set(size_type Idx) { V.set(Idx); }
    void reset(size_type Idx) { V.reset(Idx); }

    class iterator {
      const EdgeSet &Set;
      size_type Current;

      void advance() {
        assert(Current != -1);
        Current = Set.V.find_next(Current);
      }

    public:
      iterator(const EdgeSet &Set, size_type Begin)
          : Set{Set}, Current{Begin} {}
      iterator operator++(int) {
        iterator Tmp = *this;
        advance();
        return Tmp;
      }
      iterator &operator++() {
        advance();
        return *this;
      }
      Edge *operator*() const {
        assert(Current != -1);
        return Set.G.edges_begin() + Current;
      }
      bool operator==(const iterator &other) const {
        assert(&this->Set == &other.Set);
        return this->Current == other.Current;
      }
      bool operator!=(const iterator &other) const { return !(*this == other); }
    };

    iterator begin() const { return iterator{*this, V.find_first()}; }
    iterator end() const { return iterator{*this, -1}; }
  };

private:
  std::unique_ptr<Node[]> Nodes;
  std::unique_ptr<Edge[]> Edges;
  size_type NodesSize;
  size_type EdgesSize;
};

template <typename GraphT> class ImmutableGraphBuilder {
  using node_value_type = typename GraphT::node_value_type;
  using edge_value_type = typename GraphT::edge_value_type;
  static_assert(
      std::is_base_of<ImmutableGraph<node_value_type, edge_value_type>,
                      GraphT>::value,
      "Template argument to ImmutableGraphBuilder must derive from "
      "ImmutableGraph<>");
  using size_type = typename GraphT::size_type;
  using NodeSet = typename GraphT::NodeSet;
  using Node = typename GraphT::Node;
  using EdgeSet = typename GraphT::EdgeSet;
  using Edge = typename GraphT::Edge;
  using BuilderEdge = std::pair<edge_value_type, size_type>;
  using EdgeList = std::vector<BuilderEdge>;
  using BuilderVertex = std::pair<node_value_type, EdgeList>;
  using VertexVec = std::vector<BuilderVertex>;

public:
  using BuilderNodeRef = size_type;

  BuilderNodeRef addVertex(const node_value_type &V) {
    auto I = AdjList.emplace(AdjList.end(), V, EdgeList{});
    return std::distance(AdjList.begin(), I);
  }

  void addEdge(const edge_value_type &E, BuilderNodeRef From,
               BuilderNodeRef To) {
    AdjList[From].second.emplace_back(E, To);
  }

  bool empty() const { return AdjList.empty(); }

  template <typename... ArgT> std::unique_ptr<GraphT> get(ArgT &&... Args) {
    size_type VertexSize = AdjList.size(), EdgeSize = 0;
    for (const auto &V : AdjList) {
      EdgeSize += V.second.size();
    }
    auto VertexArray =
        std::make_unique<Node[]>(VertexSize + 1 /* terminator node */);
    auto EdgeArray = std::make_unique<Edge[]>(EdgeSize);
    size_type VI = 0, EI = 0;
    for (; VI < VertexSize; ++VI) {
      VertexArray[VI].Value = std::move(AdjList[VI].first);
      VertexArray[VI].Edges = &EdgeArray[EI];
      auto NumEdges = static_cast<size_type>(AdjList[VI].second.size());
      for (size_type VEI = 0; VEI < NumEdges; ++VEI, ++EI) {
        auto &E = AdjList[VI].second[VEI];
        EdgeArray[EI].Value = std::move(E.first);
        EdgeArray[EI].Dest = &VertexArray[E.second];
      }
    }
    assert(VI == VertexSize && EI == EdgeSize && "ImmutableGraph malformed");
    VertexArray[VI].Edges = &EdgeArray[EdgeSize]; // terminator node
    return std::make_unique<GraphT>(std::move(VertexArray),
                                    std::move(EdgeArray), VertexSize, EdgeSize,
                                    std::forward<ArgT>(Args)...);
  }

  template <typename... ArgT>
  static std::unique_ptr<GraphT> trim(const GraphT &G, const NodeSet &TrimNodes,
                                      const EdgeSet &TrimEdges,
                                      ArgT &&... Args) {
    size_type NewVertexSize = G.nodes_size() - TrimNodes.count();
    size_type NewEdgeSize = G.edges_size() - TrimEdges.count();
    auto NewVertexArray =
        std::make_unique<Node[]>(NewVertexSize + 1 /* terminator node */);
    auto NewEdgeArray = std::make_unique<Edge[]>(NewEdgeSize);

    // Walk the nodes and determine the new index for each node.
    size_type NewNodeIndex = 0;
    std::vector<size_type> RemappedNodeIndex(G.nodes_size());
    for (const Node &N : G.nodes()) {
      if (TrimNodes.contains(N))
        continue;
      RemappedNodeIndex[G.getNodeIndex(N)] = NewNodeIndex++;
    }
    assert(NewNodeIndex == NewVertexSize &&
           "Should have assigned NewVertexSize indices");

    size_type VertexI = 0, EdgeI = 0;
    for (const Node &N : G.nodes()) {
      if (TrimNodes.contains(N))
        continue;
      NewVertexArray[VertexI].Value = N.getValue();
      NewVertexArray[VertexI].Edges = &NewEdgeArray[EdgeI];
      for (const Edge &E : N.edges()) {
        if (TrimEdges.contains(E))
          continue;
        NewEdgeArray[EdgeI].Value = E.getValue();
        size_type DestIdx = G.getNodeIndex(*E.getDest());
        size_type NewIdx = RemappedNodeIndex[DestIdx];
        assert(NewIdx < NewVertexSize);
        NewEdgeArray[EdgeI].Dest = &NewVertexArray[NewIdx];
        ++EdgeI;
      }
      ++VertexI;
    }
    assert(VertexI == NewVertexSize && EdgeI == NewEdgeSize &&
           "Gadget graph malformed");
    NewVertexArray[VertexI].Edges = &NewEdgeArray[NewEdgeSize]; // terminator
    return std::make_unique<GraphT>(std::move(NewVertexArray),
                                    std::move(NewEdgeArray), NewVertexSize,
                                    NewEdgeSize, std::forward<ArgT>(Args)...);
  }

private:
  VertexVec AdjList;
};

template <typename NodeValueT, typename EdgeValueT>
struct GraphTraits<ImmutableGraph<NodeValueT, EdgeValueT> *> {
  using GraphT = ImmutableGraph<NodeValueT, EdgeValueT>;
  using NodeRef = typename GraphT::Node const *;
  using EdgeRef = typename GraphT::Edge const &;

  static NodeRef edge_dest(EdgeRef E) { return E.getDest(); }
  using ChildIteratorType =
      mapped_iterator<typename GraphT::Edge const *, decltype(&edge_dest)>;

  static NodeRef getEntryNode(GraphT *G) { return G->nodes_begin(); }
  static ChildIteratorType child_begin(NodeRef N) {
    return {N->edges_begin(), &edge_dest};
  }
  static ChildIteratorType child_end(NodeRef N) {
    return {N->edges_end(), &edge_dest};
  }

  static NodeRef getNode(typename GraphT::Node const &N) { return NodeRef{&N}; }
  using nodes_iterator =
      mapped_iterator<typename GraphT::Node const *, decltype(&getNode)>;
  static nodes_iterator nodes_begin(GraphT *G) {
    return {G->nodes_begin(), &getNode};
  }
  static nodes_iterator nodes_end(GraphT *G) {
    return {G->nodes_end(), &getNode};
  }

  using ChildEdgeIteratorType = typename GraphT::Edge const *;

  static ChildEdgeIteratorType child_edge_begin(NodeRef N) {
    return N->edges_begin();
  }
  static ChildEdgeIteratorType child_edge_end(NodeRef N) {
    return N->edges_end();
  }
  static typename GraphT::size_type size(GraphT *G) { return G->nodes_size(); }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_X86_IMMUTABLEGRAPH_H
