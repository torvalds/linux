//===- ADT/SCCIterator.h - Strongly Connected Comp. Iter. -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This builds on the llvm/ADT/GraphTraits.h file to find the strongly
/// connected components (SCCs) of a graph in O(N+E) time using Tarjan's DFS
/// algorithm.
///
/// The SCC iterator has the important property that if a node in SCC S1 has an
/// edge to a node in SCC S2, then it visits S1 *after* S2.
///
/// To visit S1 *before* S2, use the scc_iterator on the Inverse graph. (NOTE:
/// This requires some simple wrappers and is not supported yet.)
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SCCITERATOR_H
#define LLVM_ADT_SCCITERATOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/iterator.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace llvm {

/// Enumerate the SCCs of a directed graph in reverse topological order
/// of the SCC DAG.
///
/// This is implemented using Tarjan's DFS algorithm using an internal stack to
/// build up a vector of nodes in a particular SCC. Note that it is a forward
/// iterator and thus you cannot backtrack or re-visit nodes.
template <class GraphT, class GT = GraphTraits<GraphT>>
class scc_iterator : public iterator_facade_base<
                         scc_iterator<GraphT, GT>, std::forward_iterator_tag,
                         const std::vector<typename GT::NodeRef>, ptrdiff_t> {
  using NodeRef = typename GT::NodeRef;
  using ChildItTy = typename GT::ChildIteratorType;
  using SccTy = std::vector<NodeRef>;
  using reference = typename scc_iterator::reference;

  /// Element of VisitStack during DFS.
  struct StackElement {
    NodeRef Node;         ///< The current node pointer.
    ChildItTy NextChild;  ///< The next child, modified inplace during DFS.
    unsigned MinVisited;  ///< Minimum uplink value of all children of Node.

    StackElement(NodeRef Node, const ChildItTy &Child, unsigned Min)
        : Node(Node), NextChild(Child), MinVisited(Min) {}

    bool operator==(const StackElement &Other) const {
      return Node == Other.Node &&
             NextChild == Other.NextChild &&
             MinVisited == Other.MinVisited;
    }
  };

  /// The visit counters used to detect when a complete SCC is on the stack.
  /// visitNum is the global counter.
  ///
  /// nodeVisitNumbers are per-node visit numbers, also used as DFS flags.
  unsigned visitNum;
  DenseMap<NodeRef, unsigned> nodeVisitNumbers;

  /// Stack holding nodes of the SCC.
  std::vector<NodeRef> SCCNodeStack;

  /// The current SCC, retrieved using operator*().
  SccTy CurrentSCC;

  /// DFS stack, Used to maintain the ordering.  The top contains the current
  /// node, the next child to visit, and the minimum uplink value of all child
  std::vector<StackElement> VisitStack;

  /// A single "visit" within the non-recursive DFS traversal.
  void DFSVisitOne(NodeRef N);

  /// The stack-based DFS traversal; defined below.
  void DFSVisitChildren();

  /// Compute the next SCC using the DFS traversal.
  void GetNextSCC();

  scc_iterator(NodeRef entryN) : visitNum(0) {
    DFSVisitOne(entryN);
    GetNextSCC();
  }

  /// End is when the DFS stack is empty.
  scc_iterator() = default;

public:
  static scc_iterator begin(const GraphT &G) {
    return scc_iterator(GT::getEntryNode(G));
  }
  static scc_iterator end(const GraphT &) { return scc_iterator(); }

  /// Direct loop termination test which is more efficient than
  /// comparison with \c end().
  bool isAtEnd() const {
    assert(!CurrentSCC.empty() || VisitStack.empty());
    return CurrentSCC.empty();
  }

  bool operator==(const scc_iterator &x) const {
    return VisitStack == x.VisitStack && CurrentSCC == x.CurrentSCC;
  }

  scc_iterator &operator++() {
    GetNextSCC();
    return *this;
  }

  reference operator*() const {
    assert(!CurrentSCC.empty() && "Dereferencing END SCC iterator!");
    return CurrentSCC;
  }

  /// Test if the current SCC has a cycle.
  ///
  /// If the SCC has more than one node, this is trivially true.  If not, it may
  /// still contain a cycle if the node has an edge back to itself.
  bool hasCycle() const;

  /// This informs the \c scc_iterator that the specified \c Old node
  /// has been deleted, and \c New is to be used in its place.
  void ReplaceNode(NodeRef Old, NodeRef New) {
    assert(nodeVisitNumbers.count(Old) && "Old not in scc_iterator?");
    // Do the assignment in two steps, in case 'New' is not yet in the map, and
    // inserting it causes the map to grow.
    auto tempVal = nodeVisitNumbers[Old];
    nodeVisitNumbers[New] = tempVal;
    nodeVisitNumbers.erase(Old);
  }
};

template <class GraphT, class GT>
void scc_iterator<GraphT, GT>::DFSVisitOne(NodeRef N) {
  ++visitNum;
  nodeVisitNumbers[N] = visitNum;
  SCCNodeStack.push_back(N);
  VisitStack.push_back(StackElement(N, GT::child_begin(N), visitNum));
#if 0 // Enable if needed when debugging.
  dbgs() << "TarjanSCC: Node " << N <<
        " : visitNum = " << visitNum << "\n";
#endif
}

template <class GraphT, class GT>
void scc_iterator<GraphT, GT>::DFSVisitChildren() {
  assert(!VisitStack.empty());
  while (VisitStack.back().NextChild != GT::child_end(VisitStack.back().Node)) {
    // TOS has at least one more child so continue DFS
    NodeRef childN = *VisitStack.back().NextChild++;
    typename DenseMap<NodeRef, unsigned>::iterator Visited =
        nodeVisitNumbers.find(childN);
    if (Visited == nodeVisitNumbers.end()) {
      // this node has never been seen.
      DFSVisitOne(childN);
      continue;
    }

    unsigned childNum = Visited->second;
    if (VisitStack.back().MinVisited > childNum)
      VisitStack.back().MinVisited = childNum;
  }
}

template <class GraphT, class GT> void scc_iterator<GraphT, GT>::GetNextSCC() {
  CurrentSCC.clear(); // Prepare to compute the next SCC
  while (!VisitStack.empty()) {
    DFSVisitChildren();

    // Pop the leaf on top of the VisitStack.
    NodeRef visitingN = VisitStack.back().Node;
    unsigned minVisitNum = VisitStack.back().MinVisited;
    assert(VisitStack.back().NextChild == GT::child_end(visitingN));
    VisitStack.pop_back();

    // Propagate MinVisitNum to parent so we can detect the SCC starting node.
    if (!VisitStack.empty() && VisitStack.back().MinVisited > minVisitNum)
      VisitStack.back().MinVisited = minVisitNum;

#if 0 // Enable if needed when debugging.
    dbgs() << "TarjanSCC: Popped node " << visitingN <<
          " : minVisitNum = " << minVisitNum << "; Node visit num = " <<
          nodeVisitNumbers[visitingN] << "\n";
#endif

    if (minVisitNum != nodeVisitNumbers[visitingN])
      continue;

    // A full SCC is on the SCCNodeStack!  It includes all nodes below
    // visitingN on the stack.  Copy those nodes to CurrentSCC,
    // reset their minVisit values, and return (this suspends
    // the DFS traversal till the next ++).
    do {
      CurrentSCC.push_back(SCCNodeStack.back());
      SCCNodeStack.pop_back();
      nodeVisitNumbers[CurrentSCC.back()] = ~0U;
    } while (CurrentSCC.back() != visitingN);
    return;
  }
}

template <class GraphT, class GT>
bool scc_iterator<GraphT, GT>::hasCycle() const {
    assert(!CurrentSCC.empty() && "Dereferencing END SCC iterator!");
    if (CurrentSCC.size() > 1)
      return true;
    NodeRef N = CurrentSCC.front();
    for (ChildItTy CI = GT::child_begin(N), CE = GT::child_end(N); CI != CE;
         ++CI)
      if (*CI == N)
        return true;
    return false;
  }

/// Construct the begin iterator for a deduced graph type T.
template <class T> scc_iterator<T> scc_begin(const T &G) {
  return scc_iterator<T>::begin(G);
}

/// Construct the end iterator for a deduced graph type T.
template <class T> scc_iterator<T> scc_end(const T &G) {
  return scc_iterator<T>::end(G);
}

/// Sort the nodes of a directed SCC in the decreasing order of the edge
/// weights. The instantiating GraphT type should have weighted edge type
/// declared in its graph traits in order to use this iterator.
///
/// This is implemented using Kruskal's minimal spanning tree algorithm followed
/// by Kahn's algorithm to compute a topological order on the MST. First a
/// maximum spanning tree (forest) is built based on all edges within the SCC
/// collection. Then a topological walk is initiated on tree nodes that do not
/// have a predecessor and then applied to all nodes of the SCC. Such order
/// ensures that high-weighted edges are visited first during the traversal.
template <class GraphT, class GT = GraphTraits<GraphT>>
class scc_member_iterator {
  using NodeType = typename GT::NodeType;
  using EdgeType = typename GT::EdgeType;
  using NodesType = std::vector<NodeType *>;

  // Auxilary node information used during the MST calculation.
  struct NodeInfo {
    NodeInfo *Group = this;
    uint32_t Rank = 0;
    bool Visited = false;
    DenseSet<const EdgeType *> IncomingMSTEdges;
  };

  // Find the root group of the node and compress the path from node to the
  // root.
  NodeInfo *find(NodeInfo *Node) {
    if (Node->Group != Node)
      Node->Group = find(Node->Group);
    return Node->Group;
  }

  // Union the source and target node into the same group and return true.
  // Returns false if they are already in the same group.
  bool unionGroups(const EdgeType *Edge) {
    NodeInfo *G1 = find(&NodeInfoMap[Edge->Source]);
    NodeInfo *G2 = find(&NodeInfoMap[Edge->Target]);

    // If the edge forms a cycle, do not add it to MST
    if (G1 == G2)
      return false;

    // Make the smaller rank tree a direct child of high rank tree.
    if (G1->Rank < G2->Rank)
      G1->Group = G2;
    else {
      G2->Group = G1;
      // If the ranks are the same, increment root of one tree by one.
      if (G1->Rank == G2->Rank)
        G1->Rank++;
    }
    return true;
  }

  std::unordered_map<NodeType *, NodeInfo> NodeInfoMap;
  NodesType Nodes;

public:
  scc_member_iterator(const NodesType &InputNodes);

  NodesType &operator*() { return Nodes; }
};

template <class GraphT, class GT>
scc_member_iterator<GraphT, GT>::scc_member_iterator(
    const NodesType &InputNodes) {
  if (InputNodes.size() <= 1) {
    Nodes = InputNodes;
    return;
  }

  // Initialize auxilary node information.
  NodeInfoMap.clear();
  for (auto *Node : InputNodes) {
    // This is specifically used to construct a `NodeInfo` object in place. An
    // insert operation will involve a copy construction which invalidate the
    // initial value of the `Group` field which should be `this`.
    (void)NodeInfoMap[Node].Group;
  }

  // Sort edges by weights.
  struct EdgeComparer {
    bool operator()(const EdgeType *L, const EdgeType *R) const {
      return L->Weight > R->Weight;
    }
  };

  std::multiset<const EdgeType *, EdgeComparer> SortedEdges;
  for (auto *Node : InputNodes) {
    for (auto &Edge : Node->Edges) {
      if (NodeInfoMap.count(Edge.Target))
        SortedEdges.insert(&Edge);
    }
  }

  // Traverse all the edges and compute the Maximum Weight Spanning Tree
  // using Kruskal's algorithm.
  std::unordered_set<const EdgeType *> MSTEdges;
  for (auto *Edge : SortedEdges) {
    if (unionGroups(Edge))
      MSTEdges.insert(Edge);
  }

  // Run Kahn's algorithm on MST to compute a topological traversal order.
  // The algorithm starts from nodes that have no incoming edge. These nodes are
  // "roots" of the MST forest. This ensures that nodes are visited before their
  // descendants are, thus ensures hot edges are processed before cold edges,
  // based on how MST is computed.
  std::queue<NodeType *> Queue;
  for (const auto *Edge : MSTEdges)
    NodeInfoMap[Edge->Target].IncomingMSTEdges.insert(Edge);

  // Walk through SortedEdges to initialize the queue, instead of using NodeInfoMap
  // to ensure an ordered deterministic push.
  for (auto *Edge : SortedEdges) {
    if (!NodeInfoMap[Edge->Source].Visited &&
        NodeInfoMap[Edge->Source].IncomingMSTEdges.empty()) {
      Queue.push(Edge->Source);
      NodeInfoMap[Edge->Source].Visited = true;
    }
  }

  while (!Queue.empty()) {
    auto *Node = Queue.front();
    Queue.pop();
    Nodes.push_back(Node);
    for (auto &Edge : Node->Edges) {
      NodeInfoMap[Edge.Target].IncomingMSTEdges.erase(&Edge);
      if (MSTEdges.count(&Edge) &&
          NodeInfoMap[Edge.Target].IncomingMSTEdges.empty()) {
        Queue.push(Edge.Target);
      }
    }
  }

  assert(InputNodes.size() == Nodes.size() && "missing nodes in MST");
  std::reverse(Nodes.begin(), Nodes.end());
}
} // end namespace llvm

#endif // LLVM_ADT_SCCITERATOR_H
