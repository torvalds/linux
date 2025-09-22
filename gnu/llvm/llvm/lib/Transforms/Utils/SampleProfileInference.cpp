//===- SampleProfileInference.cpp - Adjust sample profiles in the IR ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a profile inference algorithm. Given an incomplete and
// possibly imprecise block counts, the algorithm reconstructs realistic block
// and edge counts that satisfy flow conservation rules, while minimally modify
// input block counts.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SampleProfileInference.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include <queue>
#include <set>
#include <stack>
#include <unordered_set>

using namespace llvm;
#define DEBUG_TYPE "sample-profile-inference"

namespace {

static cl::opt<bool> SampleProfileEvenFlowDistribution(
    "sample-profile-even-flow-distribution", cl::init(true), cl::Hidden,
    cl::desc("Try to evenly distribute flow when there are multiple equally "
             "likely options."));

static cl::opt<bool> SampleProfileRebalanceUnknown(
    "sample-profile-rebalance-unknown", cl::init(true), cl::Hidden,
    cl::desc("Evenly re-distribute flow among unknown subgraphs."));

static cl::opt<bool> SampleProfileJoinIslands(
    "sample-profile-join-islands", cl::init(true), cl::Hidden,
    cl::desc("Join isolated components having positive flow."));

static cl::opt<unsigned> SampleProfileProfiCostBlockInc(
    "sample-profile-profi-cost-block-inc", cl::init(10), cl::Hidden,
    cl::desc("The cost of increasing a block's count by one."));

static cl::opt<unsigned> SampleProfileProfiCostBlockDec(
    "sample-profile-profi-cost-block-dec", cl::init(20), cl::Hidden,
    cl::desc("The cost of decreasing a block's count by one."));

static cl::opt<unsigned> SampleProfileProfiCostBlockEntryInc(
    "sample-profile-profi-cost-block-entry-inc", cl::init(40), cl::Hidden,
    cl::desc("The cost of increasing the entry block's count by one."));

static cl::opt<unsigned> SampleProfileProfiCostBlockEntryDec(
    "sample-profile-profi-cost-block-entry-dec", cl::init(10), cl::Hidden,
    cl::desc("The cost of decreasing the entry block's count by one."));

static cl::opt<unsigned> SampleProfileProfiCostBlockZeroInc(
    "sample-profile-profi-cost-block-zero-inc", cl::init(11), cl::Hidden,
    cl::desc("The cost of increasing a count of zero-weight block by one."));

static cl::opt<unsigned> SampleProfileProfiCostBlockUnknownInc(
    "sample-profile-profi-cost-block-unknown-inc", cl::init(0), cl::Hidden,
    cl::desc("The cost of increasing an unknown block's count by one."));

/// A value indicating an infinite flow/capacity/weight of a block/edge.
/// Not using numeric_limits<int64_t>::max(), as the values can be summed up
/// during the execution.
static constexpr int64_t INF = ((int64_t)1) << 50;

/// The minimum-cost maximum flow algorithm.
///
/// The algorithm finds the maximum flow of minimum cost on a given (directed)
/// network using a modified version of the classical Moore-Bellman-Ford
/// approach. The algorithm applies a number of augmentation iterations in which
/// flow is sent along paths of positive capacity from the source to the sink.
/// The worst-case time complexity of the implementation is O(v(f)*m*n), where
/// where m is the number of edges, n is the number of vertices, and v(f) is the
/// value of the maximum flow. However, the observed running time on typical
/// instances is sub-quadratic, that is, o(n^2).
///
/// The input is a set of edges with specified costs and capacities, and a pair
/// of nodes (source and sink). The output is the flow along each edge of the
/// minimum total cost respecting the given edge capacities.
class MinCostMaxFlow {
public:
  MinCostMaxFlow(const ProfiParams &Params) : Params(Params) {}

  // Initialize algorithm's data structures for a network of a given size.
  void initialize(uint64_t NodeCount, uint64_t SourceNode, uint64_t SinkNode) {
    Source = SourceNode;
    Target = SinkNode;

    Nodes = std::vector<Node>(NodeCount);
    Edges = std::vector<std::vector<Edge>>(NodeCount, std::vector<Edge>());
    if (Params.EvenFlowDistribution)
      AugmentingEdges =
          std::vector<std::vector<Edge *>>(NodeCount, std::vector<Edge *>());
  }

  // Run the algorithm.
  int64_t run() {
    LLVM_DEBUG(dbgs() << "Starting profi for " << Nodes.size() << " nodes\n");

    // Iteratively find an augmentation path/dag in the network and send the
    // flow along its edges
    size_t AugmentationIters = applyFlowAugmentation();

    // Compute the total flow and its cost
    int64_t TotalCost = 0;
    int64_t TotalFlow = 0;
    for (uint64_t Src = 0; Src < Nodes.size(); Src++) {
      for (auto &Edge : Edges[Src]) {
        if (Edge.Flow > 0) {
          TotalCost += Edge.Cost * Edge.Flow;
          if (Src == Source)
            TotalFlow += Edge.Flow;
        }
      }
    }
    LLVM_DEBUG(dbgs() << "Completed profi after " << AugmentationIters
                      << " iterations with " << TotalFlow << " total flow"
                      << " of " << TotalCost << " cost\n");
    (void)TotalFlow;
    (void)AugmentationIters;
    return TotalCost;
  }

  /// Adding an edge to the network with a specified capacity and a cost.
  /// Multiple edges between a pair of nodes are allowed but self-edges
  /// are not supported.
  void addEdge(uint64_t Src, uint64_t Dst, int64_t Capacity, int64_t Cost) {
    assert(Capacity > 0 && "adding an edge of zero capacity");
    assert(Src != Dst && "loop edge are not supported");

    Edge SrcEdge;
    SrcEdge.Dst = Dst;
    SrcEdge.Cost = Cost;
    SrcEdge.Capacity = Capacity;
    SrcEdge.Flow = 0;
    SrcEdge.RevEdgeIndex = Edges[Dst].size();

    Edge DstEdge;
    DstEdge.Dst = Src;
    DstEdge.Cost = -Cost;
    DstEdge.Capacity = 0;
    DstEdge.Flow = 0;
    DstEdge.RevEdgeIndex = Edges[Src].size();

    Edges[Src].push_back(SrcEdge);
    Edges[Dst].push_back(DstEdge);
  }

  /// Adding an edge to the network of infinite capacity and a given cost.
  void addEdge(uint64_t Src, uint64_t Dst, int64_t Cost) {
    addEdge(Src, Dst, INF, Cost);
  }

  /// Get the total flow from a given source node.
  /// Returns a list of pairs (target node, amount of flow to the target).
  std::vector<std::pair<uint64_t, int64_t>> getFlow(uint64_t Src) const {
    std::vector<std::pair<uint64_t, int64_t>> Flow;
    for (const auto &Edge : Edges[Src]) {
      if (Edge.Flow > 0)
        Flow.push_back(std::make_pair(Edge.Dst, Edge.Flow));
    }
    return Flow;
  }

  /// Get the total flow between a pair of nodes.
  int64_t getFlow(uint64_t Src, uint64_t Dst) const {
    int64_t Flow = 0;
    for (const auto &Edge : Edges[Src]) {
      if (Edge.Dst == Dst) {
        Flow += Edge.Flow;
      }
    }
    return Flow;
  }

private:
  /// Iteratively find an augmentation path/dag in the network and send the
  /// flow along its edges. The method returns the number of applied iterations.
  size_t applyFlowAugmentation() {
    size_t AugmentationIters = 0;
    while (findAugmentingPath()) {
      uint64_t PathCapacity = computeAugmentingPathCapacity();
      while (PathCapacity > 0) {
        bool Progress = false;
        if (Params.EvenFlowDistribution) {
          // Identify node/edge candidates for augmentation
          identifyShortestEdges(PathCapacity);

          // Find an augmenting DAG
          auto AugmentingOrder = findAugmentingDAG();

          // Apply the DAG augmentation
          Progress = augmentFlowAlongDAG(AugmentingOrder);
          PathCapacity = computeAugmentingPathCapacity();
        }

        if (!Progress) {
          augmentFlowAlongPath(PathCapacity);
          PathCapacity = 0;
        }

        AugmentationIters++;
      }
    }
    return AugmentationIters;
  }

  /// Compute the capacity of the cannonical augmenting path. If the path is
  /// saturated (that is, no flow can be sent along the path), then return 0.
  uint64_t computeAugmentingPathCapacity() {
    uint64_t PathCapacity = INF;
    uint64_t Now = Target;
    while (Now != Source) {
      uint64_t Pred = Nodes[Now].ParentNode;
      auto &Edge = Edges[Pred][Nodes[Now].ParentEdgeIndex];

      assert(Edge.Capacity >= Edge.Flow && "incorrect edge flow");
      uint64_t EdgeCapacity = uint64_t(Edge.Capacity - Edge.Flow);
      PathCapacity = std::min(PathCapacity, EdgeCapacity);

      Now = Pred;
    }
    return PathCapacity;
  }

  /// Check for existence of an augmenting path with a positive capacity.
  bool findAugmentingPath() {
    // Initialize data structures
    for (auto &Node : Nodes) {
      Node.Distance = INF;
      Node.ParentNode = uint64_t(-1);
      Node.ParentEdgeIndex = uint64_t(-1);
      Node.Taken = false;
    }

    std::queue<uint64_t> Queue;
    Queue.push(Source);
    Nodes[Source].Distance = 0;
    Nodes[Source].Taken = true;
    while (!Queue.empty()) {
      uint64_t Src = Queue.front();
      Queue.pop();
      Nodes[Src].Taken = false;
      // Although the residual network contains edges with negative costs
      // (in particular, backward edges), it can be shown that there are no
      // negative-weight cycles and the following two invariants are maintained:
      // (i) Dist[Source, V] >= 0 and (ii) Dist[V, Target] >= 0 for all nodes V,
      // where Dist is the length of the shortest path between two nodes. This
      // allows to prune the search-space of the path-finding algorithm using
      // the following early-stop criteria:
      // -- If we find a path with zero-distance from Source to Target, stop the
      //    search, as the path is the shortest since Dist[Source, Target] >= 0;
      // -- If we have Dist[Source, V] > Dist[Source, Target], then do not
      //    process node V, as it is guaranteed _not_ to be on a shortest path
      //    from Source to Target; it follows from inequalities
      //    Dist[Source, Target] >= Dist[Source, V] + Dist[V, Target]
      //                         >= Dist[Source, V]
      if (!Params.EvenFlowDistribution && Nodes[Target].Distance == 0)
        break;
      if (Nodes[Src].Distance > Nodes[Target].Distance)
        continue;

      // Process adjacent edges
      for (uint64_t EdgeIdx = 0; EdgeIdx < Edges[Src].size(); EdgeIdx++) {
        auto &Edge = Edges[Src][EdgeIdx];
        if (Edge.Flow < Edge.Capacity) {
          uint64_t Dst = Edge.Dst;
          int64_t NewDistance = Nodes[Src].Distance + Edge.Cost;
          if (Nodes[Dst].Distance > NewDistance) {
            // Update the distance and the parent node/edge
            Nodes[Dst].Distance = NewDistance;
            Nodes[Dst].ParentNode = Src;
            Nodes[Dst].ParentEdgeIndex = EdgeIdx;
            // Add the node to the queue, if it is not there yet
            if (!Nodes[Dst].Taken) {
              Queue.push(Dst);
              Nodes[Dst].Taken = true;
            }
          }
        }
      }
    }

    return Nodes[Target].Distance != INF;
  }

  /// Update the current flow along the augmenting path.
  void augmentFlowAlongPath(uint64_t PathCapacity) {
    assert(PathCapacity > 0 && "found an incorrect augmenting path");
    uint64_t Now = Target;
    while (Now != Source) {
      uint64_t Pred = Nodes[Now].ParentNode;
      auto &Edge = Edges[Pred][Nodes[Now].ParentEdgeIndex];
      auto &RevEdge = Edges[Now][Edge.RevEdgeIndex];

      Edge.Flow += PathCapacity;
      RevEdge.Flow -= PathCapacity;

      Now = Pred;
    }
  }

  /// Find an Augmenting DAG order using a modified version of DFS in which we
  /// can visit a node multiple times. In the DFS search, when scanning each
  /// edge out of a node, continue search at Edge.Dst endpoint if it has not
  /// been discovered yet and its NumCalls < MaxDfsCalls. The algorithm
  /// runs in O(MaxDfsCalls * |Edges| + |Nodes|) time.
  /// It returns an Augmenting Order (Taken nodes in decreasing Finish time)
  /// that starts with Source and ends with Target.
  std::vector<uint64_t> findAugmentingDAG() {
    // We use a stack based implemenation of DFS to avoid recursion.
    // Defining DFS data structures:
    // A pair (NodeIdx, EdgeIdx) at the top of the Stack denotes that
    //  - we are currently visiting Nodes[NodeIdx] and
    //  - the next edge to scan is Edges[NodeIdx][EdgeIdx]
    typedef std::pair<uint64_t, uint64_t> StackItemType;
    std::stack<StackItemType> Stack;
    std::vector<uint64_t> AugmentingOrder;

    // Phase 0: Initialize Node attributes and Time for DFS run
    for (auto &Node : Nodes) {
      Node.Discovery = 0;
      Node.Finish = 0;
      Node.NumCalls = 0;
      Node.Taken = false;
    }
    uint64_t Time = 0;
    // Mark Target as Taken
    // Taken attribute will be propagated backwards from Target towards Source
    Nodes[Target].Taken = true;

    // Phase 1: Start DFS traversal from Source
    Stack.emplace(Source, 0);
    Nodes[Source].Discovery = ++Time;
    while (!Stack.empty()) {
      auto NodeIdx = Stack.top().first;
      auto EdgeIdx = Stack.top().second;

      // If we haven't scanned all edges out of NodeIdx, continue scanning
      if (EdgeIdx < Edges[NodeIdx].size()) {
        auto &Edge = Edges[NodeIdx][EdgeIdx];
        auto &Dst = Nodes[Edge.Dst];
        Stack.top().second++;

        if (Edge.OnShortestPath) {
          // If we haven't seen Edge.Dst so far, continue DFS search there
          if (Dst.Discovery == 0 && Dst.NumCalls < MaxDfsCalls) {
            Dst.Discovery = ++Time;
            Stack.emplace(Edge.Dst, 0);
            Dst.NumCalls++;
          } else if (Dst.Taken && Dst.Finish != 0) {
            // Else, if Edge.Dst already have a path to Target, so that NodeIdx
            Nodes[NodeIdx].Taken = true;
          }
        }
      } else {
        // If we are done scanning all edge out of NodeIdx
        Stack.pop();
        // If we haven't found a path from NodeIdx to Target, forget about it
        if (!Nodes[NodeIdx].Taken) {
          Nodes[NodeIdx].Discovery = 0;
        } else {
          // If we have found a path from NodeIdx to Target, then finish NodeIdx
          // and propagate Taken flag to DFS parent unless at the Source
          Nodes[NodeIdx].Finish = ++Time;
          // NodeIdx == Source if and only if the stack is empty
          if (NodeIdx != Source) {
            assert(!Stack.empty() && "empty stack while running dfs");
            Nodes[Stack.top().first].Taken = true;
          }
          AugmentingOrder.push_back(NodeIdx);
        }
      }
    }
    // Nodes are collected decreasing Finish time, so the order is reversed
    std::reverse(AugmentingOrder.begin(), AugmentingOrder.end());

    // Phase 2: Extract all forward (DAG) edges and fill in AugmentingEdges
    for (size_t Src : AugmentingOrder) {
      AugmentingEdges[Src].clear();
      for (auto &Edge : Edges[Src]) {
        uint64_t Dst = Edge.Dst;
        if (Edge.OnShortestPath && Nodes[Src].Taken && Nodes[Dst].Taken &&
            Nodes[Dst].Finish < Nodes[Src].Finish) {
          AugmentingEdges[Src].push_back(&Edge);
        }
      }
      assert((Src == Target || !AugmentingEdges[Src].empty()) &&
             "incorrectly constructed augmenting edges");
    }

    return AugmentingOrder;
  }

  /// Update the current flow along the given (acyclic) subgraph specified by
  /// the vertex order, AugmentingOrder. The objective is to send as much flow
  /// as possible while evenly distributing flow among successors of each node.
  /// After the update at least one edge is saturated.
  bool augmentFlowAlongDAG(const std::vector<uint64_t> &AugmentingOrder) {
    // Phase 0: Initialization
    for (uint64_t Src : AugmentingOrder) {
      Nodes[Src].FracFlow = 0;
      Nodes[Src].IntFlow = 0;
      for (auto &Edge : AugmentingEdges[Src]) {
        Edge->AugmentedFlow = 0;
      }
    }

    // Phase 1: Send a unit of fractional flow along the DAG
    uint64_t MaxFlowAmount = INF;
    Nodes[Source].FracFlow = 1.0;
    for (uint64_t Src : AugmentingOrder) {
      assert((Src == Target || Nodes[Src].FracFlow > 0.0) &&
             "incorrectly computed fractional flow");
      // Distribute flow evenly among successors of Src
      uint64_t Degree = AugmentingEdges[Src].size();
      for (auto &Edge : AugmentingEdges[Src]) {
        double EdgeFlow = Nodes[Src].FracFlow / Degree;
        Nodes[Edge->Dst].FracFlow += EdgeFlow;
        if (Edge->Capacity == INF)
          continue;
        uint64_t MaxIntFlow = double(Edge->Capacity - Edge->Flow) / EdgeFlow;
        MaxFlowAmount = std::min(MaxFlowAmount, MaxIntFlow);
      }
    }
    // Stop early if we cannot send any (integral) flow from Source to Target
    if (MaxFlowAmount == 0)
      return false;

    // Phase 2: Send an integral flow of MaxFlowAmount
    Nodes[Source].IntFlow = MaxFlowAmount;
    for (uint64_t Src : AugmentingOrder) {
      if (Src == Target)
        break;
      // Distribute flow evenly among successors of Src, rounding up to make
      // sure all flow is sent
      uint64_t Degree = AugmentingEdges[Src].size();
      // We are guaranteeed that Node[Src].IntFlow <= SuccFlow * Degree
      uint64_t SuccFlow = (Nodes[Src].IntFlow + Degree - 1) / Degree;
      for (auto &Edge : AugmentingEdges[Src]) {
        uint64_t Dst = Edge->Dst;
        uint64_t EdgeFlow = std::min(Nodes[Src].IntFlow, SuccFlow);
        EdgeFlow = std::min(EdgeFlow, uint64_t(Edge->Capacity - Edge->Flow));
        Nodes[Dst].IntFlow += EdgeFlow;
        Nodes[Src].IntFlow -= EdgeFlow;
        Edge->AugmentedFlow += EdgeFlow;
      }
    }
    assert(Nodes[Target].IntFlow <= MaxFlowAmount);
    Nodes[Target].IntFlow = 0;

    // Phase 3: Send excess flow back traversing the nodes backwards.
    // Because of rounding, not all flow can be sent along the edges of Src.
    // Hence, sending the remaining flow back to maintain flow conservation
    for (size_t Idx = AugmentingOrder.size() - 1; Idx > 0; Idx--) {
      uint64_t Src = AugmentingOrder[Idx - 1];
      // Try to send excess flow back along each edge.
      // Make sure we only send back flow we just augmented (AugmentedFlow).
      for (auto &Edge : AugmentingEdges[Src]) {
        uint64_t Dst = Edge->Dst;
        if (Nodes[Dst].IntFlow == 0)
          continue;
        uint64_t EdgeFlow = std::min(Nodes[Dst].IntFlow, Edge->AugmentedFlow);
        Nodes[Dst].IntFlow -= EdgeFlow;
        Nodes[Src].IntFlow += EdgeFlow;
        Edge->AugmentedFlow -= EdgeFlow;
      }
    }

    // Phase 4: Update flow values along all edges
    bool HasSaturatedEdges = false;
    for (uint64_t Src : AugmentingOrder) {
      // Verify that we have sent all the excess flow from the node
      assert(Src == Source || Nodes[Src].IntFlow == 0);
      for (auto &Edge : AugmentingEdges[Src]) {
        assert(uint64_t(Edge->Capacity - Edge->Flow) >= Edge->AugmentedFlow);
        // Update flow values along the edge and its reverse copy
        auto &RevEdge = Edges[Edge->Dst][Edge->RevEdgeIndex];
        Edge->Flow += Edge->AugmentedFlow;
        RevEdge.Flow -= Edge->AugmentedFlow;
        if (Edge->Capacity == Edge->Flow && Edge->AugmentedFlow > 0)
          HasSaturatedEdges = true;
      }
    }

    // The augmentation is successful iff at least one edge becomes saturated
    return HasSaturatedEdges;
  }

  /// Identify candidate (shortest) edges for augmentation.
  void identifyShortestEdges(uint64_t PathCapacity) {
    assert(PathCapacity > 0 && "found an incorrect augmenting DAG");
    // To make sure the augmentation DAG contains only edges with large residual
    // capacity, we prune all edges whose capacity is below a fraction of
    // the capacity of the augmented path.
    // (All edges of the path itself are always in the DAG)
    uint64_t MinCapacity = std::max(PathCapacity / 2, uint64_t(1));

    // Decide which edges are on a shortest path from Source to Target
    for (size_t Src = 0; Src < Nodes.size(); Src++) {
      // An edge cannot be augmenting if the endpoint has large distance
      if (Nodes[Src].Distance > Nodes[Target].Distance)
        continue;

      for (auto &Edge : Edges[Src]) {
        uint64_t Dst = Edge.Dst;
        Edge.OnShortestPath =
            Src != Target && Dst != Source &&
            Nodes[Dst].Distance <= Nodes[Target].Distance &&
            Nodes[Dst].Distance == Nodes[Src].Distance + Edge.Cost &&
            Edge.Capacity > Edge.Flow &&
            uint64_t(Edge.Capacity - Edge.Flow) >= MinCapacity;
      }
    }
  }

  /// Maximum number of DFS iterations for DAG finding.
  static constexpr uint64_t MaxDfsCalls = 10;

  /// A node in a flow network.
  struct Node {
    /// The cost of the cheapest path from the source to the current node.
    int64_t Distance;
    /// The node preceding the current one in the path.
    uint64_t ParentNode;
    /// The index of the edge between ParentNode and the current node.
    uint64_t ParentEdgeIndex;
    /// An indicator of whether the current node is in a queue.
    bool Taken;

    /// Data fields utilized in DAG-augmentation:
    /// Fractional flow.
    double FracFlow;
    /// Integral flow.
    uint64_t IntFlow;
    /// Discovery time.
    uint64_t Discovery;
    /// Finish time.
    uint64_t Finish;
    /// NumCalls.
    uint64_t NumCalls;
  };

  /// An edge in a flow network.
  struct Edge {
    /// The cost of the edge.
    int64_t Cost;
    /// The capacity of the edge.
    int64_t Capacity;
    /// The current flow on the edge.
    int64_t Flow;
    /// The destination node of the edge.
    uint64_t Dst;
    /// The index of the reverse edge between Dst and the current node.
    uint64_t RevEdgeIndex;

    /// Data fields utilized in DAG-augmentation:
    /// Whether the edge is currently on a shortest path from Source to Target.
    bool OnShortestPath;
    /// Extra flow along the edge.
    uint64_t AugmentedFlow;
  };

  /// The set of network nodes.
  std::vector<Node> Nodes;
  /// The set of network edges.
  std::vector<std::vector<Edge>> Edges;
  /// Source node of the flow.
  uint64_t Source;
  /// Target (sink) node of the flow.
  uint64_t Target;
  /// Augmenting edges.
  std::vector<std::vector<Edge *>> AugmentingEdges;
  /// Params for flow computation.
  const ProfiParams &Params;
};

/// A post-processing adjustment of the control flow. It applies two steps by
/// rerouting some flow and making it more realistic:
///
/// - First, it removes all isolated components ("islands") with a positive flow
///   that are unreachable from the entry block. For every such component, we
///   find the shortest from the entry to an exit passing through the component,
///   and increase the flow by one unit along the path.
///
/// - Second, it identifies all "unknown subgraphs" consisting of basic blocks
///   with no sampled counts. Then it rebalnces the flow that goes through such
///   a subgraph so that each branch is taken with probability 50%.
///   An unknown subgraph is such that for every two nodes u and v:
///     - u dominates v and u is not unknown;
///     - v post-dominates u; and
///     - all inner-nodes of all (u,v)-paths are unknown.
///
class FlowAdjuster {
public:
  FlowAdjuster(const ProfiParams &Params, FlowFunction &Func)
      : Params(Params), Func(Func) {}

  /// Apply the post-processing.
  void run() {
    if (Params.JoinIslands) {
      // Adjust the flow to get rid of isolated components
      joinIsolatedComponents();
    }

    if (Params.RebalanceUnknown) {
      // Rebalance the flow inside unknown subgraphs
      rebalanceUnknownSubgraphs();
    }
  }

private:
  void joinIsolatedComponents() {
    // Find blocks that are reachable from the source
    auto Visited = BitVector(NumBlocks(), false);
    findReachable(Func.Entry, Visited);

    // Iterate over all non-reachable blocks and adjust their weights
    for (uint64_t I = 0; I < NumBlocks(); I++) {
      auto &Block = Func.Blocks[I];
      if (Block.Flow > 0 && !Visited[I]) {
        // Find a path from the entry to an exit passing through the block I
        auto Path = findShortestPath(I);
        // Increase the flow along the path
        assert(Path.size() > 0 && Path[0]->Source == Func.Entry &&
               "incorrectly computed path adjusting control flow");
        Func.Blocks[Func.Entry].Flow += 1;
        for (auto &Jump : Path) {
          Jump->Flow += 1;
          Func.Blocks[Jump->Target].Flow += 1;
          // Update reachability
          findReachable(Jump->Target, Visited);
        }
      }
    }
  }

  /// Run BFS from a given block along the jumps with a positive flow and mark
  /// all reachable blocks.
  void findReachable(uint64_t Src, BitVector &Visited) {
    if (Visited[Src])
      return;
    std::queue<uint64_t> Queue;
    Queue.push(Src);
    Visited[Src] = true;
    while (!Queue.empty()) {
      Src = Queue.front();
      Queue.pop();
      for (auto *Jump : Func.Blocks[Src].SuccJumps) {
        uint64_t Dst = Jump->Target;
        if (Jump->Flow > 0 && !Visited[Dst]) {
          Queue.push(Dst);
          Visited[Dst] = true;
        }
      }
    }
  }

  /// Find the shortest path from the entry block to an exit block passing
  /// through a given block.
  std::vector<FlowJump *> findShortestPath(uint64_t BlockIdx) {
    // A path from the entry block to BlockIdx
    auto ForwardPath = findShortestPath(Func.Entry, BlockIdx);
    // A path from BlockIdx to an exit block
    auto BackwardPath = findShortestPath(BlockIdx, AnyExitBlock);

    // Concatenate the two paths
    std::vector<FlowJump *> Result;
    Result.insert(Result.end(), ForwardPath.begin(), ForwardPath.end());
    Result.insert(Result.end(), BackwardPath.begin(), BackwardPath.end());
    return Result;
  }

  /// Apply the Dijkstra algorithm to find the shortest path from a given
  /// Source to a given Target block.
  /// If Target == -1, then the path ends at an exit block.
  std::vector<FlowJump *> findShortestPath(uint64_t Source, uint64_t Target) {
    // Quit early, if possible
    if (Source == Target)
      return std::vector<FlowJump *>();
    if (Func.Blocks[Source].isExit() && Target == AnyExitBlock)
      return std::vector<FlowJump *>();

    // Initialize data structures
    auto Distance = std::vector<int64_t>(NumBlocks(), INF);
    auto Parent = std::vector<FlowJump *>(NumBlocks(), nullptr);
    Distance[Source] = 0;
    std::set<std::pair<uint64_t, uint64_t>> Queue;
    Queue.insert(std::make_pair(Distance[Source], Source));

    // Run the Dijkstra algorithm
    while (!Queue.empty()) {
      uint64_t Src = Queue.begin()->second;
      Queue.erase(Queue.begin());
      // If we found a solution, quit early
      if (Src == Target ||
          (Func.Blocks[Src].isExit() && Target == AnyExitBlock))
        break;

      for (auto *Jump : Func.Blocks[Src].SuccJumps) {
        uint64_t Dst = Jump->Target;
        int64_t JumpDist = jumpDistance(Jump);
        if (Distance[Dst] > Distance[Src] + JumpDist) {
          Queue.erase(std::make_pair(Distance[Dst], Dst));

          Distance[Dst] = Distance[Src] + JumpDist;
          Parent[Dst] = Jump;

          Queue.insert(std::make_pair(Distance[Dst], Dst));
        }
      }
    }
    // If Target is not provided, find the closest exit block
    if (Target == AnyExitBlock) {
      for (uint64_t I = 0; I < NumBlocks(); I++) {
        if (Func.Blocks[I].isExit() && Parent[I] != nullptr) {
          if (Target == AnyExitBlock || Distance[Target] > Distance[I]) {
            Target = I;
          }
        }
      }
    }
    assert(Parent[Target] != nullptr && "a path does not exist");

    // Extract the constructed path
    std::vector<FlowJump *> Result;
    uint64_t Now = Target;
    while (Now != Source) {
      assert(Now == Parent[Now]->Target && "incorrect parent jump");
      Result.push_back(Parent[Now]);
      Now = Parent[Now]->Source;
    }
    // Reverse the path, since it is extracted from Target to Source
    std::reverse(Result.begin(), Result.end());
    return Result;
  }

  /// A distance of a path for a given jump.
  /// In order to incite the path to use blocks/jumps with large positive flow,
  /// and avoid changing branch probability of outgoing edges drastically,
  /// set the jump distance so as:
  ///   - to minimize the number of unlikely jumps used and subject to that,
  ///   - to minimize the number of Flow == 0 jumps used and subject to that,
  ///   - minimizes total multiplicative Flow increase for the remaining edges.
  /// To capture this objective with integer distances, we round off fractional
  /// parts to a multiple of 1 / BaseDistance.
  int64_t jumpDistance(FlowJump *Jump) const {
    if (Jump->IsUnlikely)
      return Params.CostUnlikely;
    uint64_t BaseDistance =
        std::max(FlowAdjuster::MinBaseDistance,
                 std::min(Func.Blocks[Func.Entry].Flow,
                          Params.CostUnlikely / (2 * (NumBlocks() + 1))));
    if (Jump->Flow > 0)
      return BaseDistance + BaseDistance / Jump->Flow;
    return 2 * BaseDistance * (NumBlocks() + 1);
  };

  uint64_t NumBlocks() const { return Func.Blocks.size(); }

  /// Rebalance unknown subgraphs so that the flow is split evenly across the
  /// outgoing branches of every block of the subgraph. The method iterates over
  /// blocks with known weight and identifies unknown subgraphs rooted at the
  /// blocks. Then it verifies if flow rebalancing is feasible and applies it.
  void rebalanceUnknownSubgraphs() {
    // Try to find unknown subgraphs from each block
    for (const FlowBlock &SrcBlock : Func.Blocks) {
      // Verify if rebalancing rooted at SrcBlock is feasible
      if (!canRebalanceAtRoot(&SrcBlock))
        continue;

      // Find an unknown subgraphs starting at SrcBlock. Along the way,
      // fill in known destinations and intermediate unknown blocks.
      std::vector<FlowBlock *> UnknownBlocks;
      std::vector<FlowBlock *> KnownDstBlocks;
      findUnknownSubgraph(&SrcBlock, KnownDstBlocks, UnknownBlocks);

      // Verify if rebalancing of the subgraph is feasible. If the search is
      // successful, find the unique destination block (which can be null)
      FlowBlock *DstBlock = nullptr;
      if (!canRebalanceSubgraph(&SrcBlock, KnownDstBlocks, UnknownBlocks,
                                DstBlock))
        continue;

      // We cannot rebalance subgraphs containing cycles among unknown blocks
      if (!isAcyclicSubgraph(&SrcBlock, DstBlock, UnknownBlocks))
        continue;

      // Rebalance the flow
      rebalanceUnknownSubgraph(&SrcBlock, DstBlock, UnknownBlocks);
    }
  }

  /// Verify if rebalancing rooted at a given block is possible.
  bool canRebalanceAtRoot(const FlowBlock *SrcBlock) {
    // Do not attempt to find unknown subgraphs from an unknown or a
    // zero-flow block
    if (SrcBlock->HasUnknownWeight || SrcBlock->Flow == 0)
      return false;

    // Do not attempt to process subgraphs from a block w/o unknown sucessors
    bool HasUnknownSuccs = false;
    for (auto *Jump : SrcBlock->SuccJumps) {
      if (Func.Blocks[Jump->Target].HasUnknownWeight) {
        HasUnknownSuccs = true;
        break;
      }
    }
    if (!HasUnknownSuccs)
      return false;

    return true;
  }

  /// Find an unknown subgraph starting at block SrcBlock. The method sets
  /// identified destinations, KnownDstBlocks, and intermediate UnknownBlocks.
  void findUnknownSubgraph(const FlowBlock *SrcBlock,
                           std::vector<FlowBlock *> &KnownDstBlocks,
                           std::vector<FlowBlock *> &UnknownBlocks) {
    // Run BFS from SrcBlock and make sure all paths are going through unknown
    // blocks and end at a known DstBlock
    auto Visited = BitVector(NumBlocks(), false);
    std::queue<uint64_t> Queue;

    Queue.push(SrcBlock->Index);
    Visited[SrcBlock->Index] = true;
    while (!Queue.empty()) {
      auto &Block = Func.Blocks[Queue.front()];
      Queue.pop();
      // Process blocks reachable from Block
      for (auto *Jump : Block.SuccJumps) {
        // If Jump can be ignored, skip it
        if (ignoreJump(SrcBlock, nullptr, Jump))
          continue;

        uint64_t Dst = Jump->Target;
        // If Dst has been visited, skip Jump
        if (Visited[Dst])
          continue;
        // Process block Dst
        Visited[Dst] = true;
        if (!Func.Blocks[Dst].HasUnknownWeight) {
          KnownDstBlocks.push_back(&Func.Blocks[Dst]);
        } else {
          Queue.push(Dst);
          UnknownBlocks.push_back(&Func.Blocks[Dst]);
        }
      }
    }
  }

  /// Verify if rebalancing of the subgraph is feasible. If the checks are
  /// successful, set the unique destination block, DstBlock (can be null).
  bool canRebalanceSubgraph(const FlowBlock *SrcBlock,
                            const std::vector<FlowBlock *> &KnownDstBlocks,
                            const std::vector<FlowBlock *> &UnknownBlocks,
                            FlowBlock *&DstBlock) {
    // If the list of unknown blocks is empty, we don't need rebalancing
    if (UnknownBlocks.empty())
      return false;

    // If there are multiple known sinks, we can't rebalance
    if (KnownDstBlocks.size() > 1)
      return false;
    DstBlock = KnownDstBlocks.empty() ? nullptr : KnownDstBlocks.front();

    // Verify sinks of the subgraph
    for (auto *Block : UnknownBlocks) {
      if (Block->SuccJumps.empty()) {
        // If there are multiple (known and unknown) sinks, we can't rebalance
        if (DstBlock != nullptr)
          return false;
        continue;
      }
      size_t NumIgnoredJumps = 0;
      for (auto *Jump : Block->SuccJumps) {
        if (ignoreJump(SrcBlock, DstBlock, Jump))
          NumIgnoredJumps++;
      }
      // If there is a non-sink block in UnknownBlocks with all jumps ignored,
      // then we can't rebalance
      if (NumIgnoredJumps == Block->SuccJumps.size())
        return false;
    }

    return true;
  }

  /// Decide whether the Jump is ignored while processing an unknown subgraphs
  /// rooted at basic block SrcBlock with the destination block, DstBlock.
  bool ignoreJump(const FlowBlock *SrcBlock, const FlowBlock *DstBlock,
                  const FlowJump *Jump) {
    // Ignore unlikely jumps with zero flow
    if (Jump->IsUnlikely && Jump->Flow == 0)
      return true;

    auto JumpSource = &Func.Blocks[Jump->Source];
    auto JumpTarget = &Func.Blocks[Jump->Target];

    // Do not ignore jumps coming into DstBlock
    if (DstBlock != nullptr && JumpTarget == DstBlock)
      return false;

    // Ignore jumps out of SrcBlock to known blocks
    if (!JumpTarget->HasUnknownWeight && JumpSource == SrcBlock)
      return true;

    // Ignore jumps to known blocks with zero flow
    if (!JumpTarget->HasUnknownWeight && JumpTarget->Flow == 0)
      return true;

    return false;
  }

  /// Verify if the given unknown subgraph is acyclic, and if yes, reorder
  /// UnknownBlocks in the topological order (so that all jumps are "forward").
  bool isAcyclicSubgraph(const FlowBlock *SrcBlock, const FlowBlock *DstBlock,
                         std::vector<FlowBlock *> &UnknownBlocks) {
    // Extract local in-degrees in the considered subgraph
    auto LocalInDegree = std::vector<uint64_t>(NumBlocks(), 0);
    auto fillInDegree = [&](const FlowBlock *Block) {
      for (auto *Jump : Block->SuccJumps) {
        if (ignoreJump(SrcBlock, DstBlock, Jump))
          continue;
        LocalInDegree[Jump->Target]++;
      }
    };
    fillInDegree(SrcBlock);
    for (auto *Block : UnknownBlocks) {
      fillInDegree(Block);
    }
    // A loop containing SrcBlock
    if (LocalInDegree[SrcBlock->Index] > 0)
      return false;

    std::vector<FlowBlock *> AcyclicOrder;
    std::queue<uint64_t> Queue;
    Queue.push(SrcBlock->Index);
    while (!Queue.empty()) {
      FlowBlock *Block = &Func.Blocks[Queue.front()];
      Queue.pop();
      // Stop propagation once we reach DstBlock, if any
      if (DstBlock != nullptr && Block == DstBlock)
        break;

      // Keep an acyclic order of unknown blocks
      if (Block->HasUnknownWeight && Block != SrcBlock)
        AcyclicOrder.push_back(Block);

      // Add to the queue all successors with zero local in-degree
      for (auto *Jump : Block->SuccJumps) {
        if (ignoreJump(SrcBlock, DstBlock, Jump))
          continue;
        uint64_t Dst = Jump->Target;
        LocalInDegree[Dst]--;
        if (LocalInDegree[Dst] == 0) {
          Queue.push(Dst);
        }
      }
    }

    // If there is a cycle in the subgraph, AcyclicOrder contains only a subset
    // of all blocks
    if (UnknownBlocks.size() != AcyclicOrder.size())
      return false;
    UnknownBlocks = AcyclicOrder;
    return true;
  }

  /// Rebalance a given subgraph rooted at SrcBlock, ending at DstBlock and
  /// having UnknownBlocks intermediate blocks.
  void rebalanceUnknownSubgraph(const FlowBlock *SrcBlock,
                                const FlowBlock *DstBlock,
                                const std::vector<FlowBlock *> &UnknownBlocks) {
    assert(SrcBlock->Flow > 0 && "zero-flow block in unknown subgraph");

    // Ditribute flow from the source block
    uint64_t BlockFlow = 0;
    // SrcBlock's flow is the sum of outgoing flows along non-ignored jumps
    for (auto *Jump : SrcBlock->SuccJumps) {
      if (ignoreJump(SrcBlock, DstBlock, Jump))
        continue;
      BlockFlow += Jump->Flow;
    }
    rebalanceBlock(SrcBlock, DstBlock, SrcBlock, BlockFlow);

    // Ditribute flow from the remaining blocks
    for (auto *Block : UnknownBlocks) {
      assert(Block->HasUnknownWeight && "incorrect unknown subgraph");
      uint64_t BlockFlow = 0;
      // Block's flow is the sum of incoming flows
      for (auto *Jump : Block->PredJumps) {
        BlockFlow += Jump->Flow;
      }
      Block->Flow = BlockFlow;
      rebalanceBlock(SrcBlock, DstBlock, Block, BlockFlow);
    }
  }

  /// Redistribute flow for a block in a subgraph rooted at SrcBlock,
  /// and ending at DstBlock.
  void rebalanceBlock(const FlowBlock *SrcBlock, const FlowBlock *DstBlock,
                      const FlowBlock *Block, uint64_t BlockFlow) {
    // Process all successor jumps and update corresponding flow values
    size_t BlockDegree = 0;
    for (auto *Jump : Block->SuccJumps) {
      if (ignoreJump(SrcBlock, DstBlock, Jump))
        continue;
      BlockDegree++;
    }
    // If all successor jumps of the block are ignored, skip it
    if (DstBlock == nullptr && BlockDegree == 0)
      return;
    assert(BlockDegree > 0 && "all outgoing jumps are ignored");

    // Each of the Block's successors gets the following amount of flow.
    // Rounding the value up so that all flow is propagated
    uint64_t SuccFlow = (BlockFlow + BlockDegree - 1) / BlockDegree;
    for (auto *Jump : Block->SuccJumps) {
      if (ignoreJump(SrcBlock, DstBlock, Jump))
        continue;
      uint64_t Flow = std::min(SuccFlow, BlockFlow);
      Jump->Flow = Flow;
      BlockFlow -= Flow;
    }
    assert(BlockFlow == 0 && "not all flow is propagated");
  }

  /// A constant indicating an arbitrary exit block of a function.
  static constexpr uint64_t AnyExitBlock = uint64_t(-1);
  /// Minimum BaseDistance for the jump distance values in island joining.
  static constexpr uint64_t MinBaseDistance = 10000;

  /// Params for flow computation.
  const ProfiParams &Params;
  /// The function.
  FlowFunction &Func;
};

std::pair<int64_t, int64_t> assignBlockCosts(const ProfiParams &Params,
                                             const FlowBlock &Block);
std::pair<int64_t, int64_t> assignJumpCosts(const ProfiParams &Params,
                                            const FlowJump &Jump);

/// Initializing flow network for a given function.
///
/// Every block is split into two nodes that are responsible for (i) an
/// incoming flow, (ii) an outgoing flow; they penalize an increase or a
/// reduction of the block weight.
void initializeNetwork(const ProfiParams &Params, MinCostMaxFlow &Network,
                       FlowFunction &Func) {
  uint64_t NumBlocks = Func.Blocks.size();
  assert(NumBlocks > 1 && "Too few blocks in a function");
  uint64_t NumJumps = Func.Jumps.size();
  assert(NumJumps > 0 && "Too few jumps in a function");

  // Introducing dummy source/sink pairs to allow flow circulation.
  // The nodes corresponding to blocks of the function have indices in
  // the range [0 .. 2 * NumBlocks); the dummy sources/sinks are indexed by the
  // next four values.
  uint64_t S = 2 * NumBlocks;
  uint64_t T = S + 1;
  uint64_t S1 = S + 2;
  uint64_t T1 = S + 3;

  Network.initialize(2 * NumBlocks + 4, S1, T1);

  // Initialize nodes of the flow network
  for (uint64_t B = 0; B < NumBlocks; B++) {
    auto &Block = Func.Blocks[B];

    // Split every block into two auxiliary nodes to allow
    // increase/reduction of the block count.
    uint64_t Bin = 2 * B;
    uint64_t Bout = 2 * B + 1;

    // Edges from S and to T
    if (Block.isEntry()) {
      Network.addEdge(S, Bin, 0);
    } else if (Block.isExit()) {
      Network.addEdge(Bout, T, 0);
    }

    // Assign costs for increasing/decreasing the block counts
    auto [AuxCostInc, AuxCostDec] = assignBlockCosts(Params, Block);

    // Add the corresponding edges to the network
    Network.addEdge(Bin, Bout, AuxCostInc);
    if (Block.Weight > 0) {
      Network.addEdge(Bout, Bin, Block.Weight, AuxCostDec);
      Network.addEdge(S1, Bout, Block.Weight, 0);
      Network.addEdge(Bin, T1, Block.Weight, 0);
    }
  }

  // Initialize edges of the flow network
  for (uint64_t J = 0; J < NumJumps; J++) {
    auto &Jump = Func.Jumps[J];

    // Get the endpoints corresponding to the jump
    uint64_t Jin = 2 * Jump.Source + 1;
    uint64_t Jout = 2 * Jump.Target;

    // Assign costs for increasing/decreasing the jump counts
    auto [AuxCostInc, AuxCostDec] = assignJumpCosts(Params, Jump);

    // Add the corresponding edges to the network
    Network.addEdge(Jin, Jout, AuxCostInc);
    if (Jump.Weight > 0) {
      Network.addEdge(Jout, Jin, Jump.Weight, AuxCostDec);
      Network.addEdge(S1, Jout, Jump.Weight, 0);
      Network.addEdge(Jin, T1, Jump.Weight, 0);
    }
  }

  // Make sure we have a valid flow circulation
  Network.addEdge(T, S, 0);
}

/// Assign costs for increasing/decreasing the block counts.
std::pair<int64_t, int64_t> assignBlockCosts(const ProfiParams &Params,
                                             const FlowBlock &Block) {
  // Modifying the weight of an unlikely block is expensive
  if (Block.IsUnlikely)
    return std::make_pair(Params.CostUnlikely, Params.CostUnlikely);

  // Assign default values for the costs
  int64_t CostInc = Params.CostBlockInc;
  int64_t CostDec = Params.CostBlockDec;
  // Update the costs depending on the block metadata
  if (Block.HasUnknownWeight) {
    CostInc = Params.CostBlockUnknownInc;
    CostDec = 0;
  } else {
    // Increasing the count for "cold" blocks with zero initial count is more
    // expensive than for "hot" ones
    if (Block.Weight == 0)
      CostInc = Params.CostBlockZeroInc;
    // Modifying the count of the entry block is expensive
    if (Block.isEntry()) {
      CostInc = Params.CostBlockEntryInc;
      CostDec = Params.CostBlockEntryDec;
    }
  }
  return std::make_pair(CostInc, CostDec);
}

/// Assign costs for increasing/decreasing the jump counts.
std::pair<int64_t, int64_t> assignJumpCosts(const ProfiParams &Params,
                                            const FlowJump &Jump) {
  // Modifying the weight of an unlikely jump is expensive
  if (Jump.IsUnlikely)
    return std::make_pair(Params.CostUnlikely, Params.CostUnlikely);

  // Assign default values for the costs
  int64_t CostInc = Params.CostJumpInc;
  int64_t CostDec = Params.CostJumpDec;
  // Update the costs depending on the block metadata
  if (Jump.Source + 1 == Jump.Target) {
    // Adjusting the fall-through branch
    CostInc = Params.CostJumpFTInc;
    CostDec = Params.CostJumpFTDec;
  }
  if (Jump.HasUnknownWeight) {
    // The cost is different for fall-through and non-fall-through branches
    if (Jump.Source + 1 == Jump.Target)
      CostInc = Params.CostJumpUnknownFTInc;
    else
      CostInc = Params.CostJumpUnknownInc;
    CostDec = 0;
  } else {
    assert(Jump.Weight > 0 && "found zero-weight jump with a positive weight");
  }
  return std::make_pair(CostInc, CostDec);
}

/// Extract resulting block and edge counts from the flow network.
void extractWeights(const ProfiParams &Params, MinCostMaxFlow &Network,
                    FlowFunction &Func) {
  uint64_t NumBlocks = Func.Blocks.size();
  uint64_t NumJumps = Func.Jumps.size();

  // Extract resulting jump counts
  for (uint64_t J = 0; J < NumJumps; J++) {
    auto &Jump = Func.Jumps[J];
    uint64_t SrcOut = 2 * Jump.Source + 1;
    uint64_t DstIn = 2 * Jump.Target;

    int64_t Flow = 0;
    int64_t AuxFlow = Network.getFlow(SrcOut, DstIn);
    if (Jump.Source != Jump.Target)
      Flow = int64_t(Jump.Weight) + AuxFlow;
    else
      Flow = int64_t(Jump.Weight) + (AuxFlow > 0 ? AuxFlow : 0);

    Jump.Flow = Flow;
    assert(Flow >= 0 && "negative jump flow");
  }

  // Extract resulting block counts
  auto InFlow = std::vector<uint64_t>(NumBlocks, 0);
  auto OutFlow = std::vector<uint64_t>(NumBlocks, 0);
  for (auto &Jump : Func.Jumps) {
    InFlow[Jump.Target] += Jump.Flow;
    OutFlow[Jump.Source] += Jump.Flow;
  }
  for (uint64_t B = 0; B < NumBlocks; B++) {
    auto &Block = Func.Blocks[B];
    Block.Flow = std::max(OutFlow[B], InFlow[B]);
  }
}

#ifndef NDEBUG
/// Verify that the provided block/jump weights are as expected.
void verifyInput(const FlowFunction &Func) {
  // Verify entry and exit blocks
  assert(Func.Entry == 0 && Func.Blocks[0].isEntry());
  size_t NumExitBlocks = 0;
  for (size_t I = 1; I < Func.Blocks.size(); I++) {
    assert(!Func.Blocks[I].isEntry() && "multiple entry blocks");
    if (Func.Blocks[I].isExit())
      NumExitBlocks++;
  }
  assert(NumExitBlocks > 0 && "cannot find exit blocks");

  // Verify that there are no parallel edges
  for (auto &Block : Func.Blocks) {
    std::unordered_set<uint64_t> UniqueSuccs;
    for (auto &Jump : Block.SuccJumps) {
      auto It = UniqueSuccs.insert(Jump->Target);
      assert(It.second && "input CFG contains parallel edges");
    }
  }
  // Verify CFG jumps
  for (auto &Block : Func.Blocks) {
    assert((!Block.isEntry() || !Block.isExit()) &&
           "a block cannot be an entry and an exit");
  }
  // Verify input block weights
  for (auto &Block : Func.Blocks) {
    assert((!Block.HasUnknownWeight || Block.Weight == 0 || Block.isEntry()) &&
           "non-zero weight of a block w/o weight except for an entry");
  }
  // Verify input jump weights
  for (auto &Jump : Func.Jumps) {
    assert((!Jump.HasUnknownWeight || Jump.Weight == 0) &&
           "non-zero weight of a jump w/o weight");
  }
}

/// Verify that the computed flow values satisfy flow conservation rules.
void verifyOutput(const FlowFunction &Func) {
  const uint64_t NumBlocks = Func.Blocks.size();
  auto InFlow = std::vector<uint64_t>(NumBlocks, 0);
  auto OutFlow = std::vector<uint64_t>(NumBlocks, 0);
  for (const auto &Jump : Func.Jumps) {
    InFlow[Jump.Target] += Jump.Flow;
    OutFlow[Jump.Source] += Jump.Flow;
  }

  uint64_t TotalInFlow = 0;
  uint64_t TotalOutFlow = 0;
  for (uint64_t I = 0; I < NumBlocks; I++) {
    auto &Block = Func.Blocks[I];
    if (Block.isEntry()) {
      TotalInFlow += Block.Flow;
      assert(Block.Flow == OutFlow[I] && "incorrectly computed control flow");
    } else if (Block.isExit()) {
      TotalOutFlow += Block.Flow;
      assert(Block.Flow == InFlow[I] && "incorrectly computed control flow");
    } else {
      assert(Block.Flow == OutFlow[I] && "incorrectly computed control flow");
      assert(Block.Flow == InFlow[I] && "incorrectly computed control flow");
    }
  }
  assert(TotalInFlow == TotalOutFlow && "incorrectly computed control flow");

  // Verify that there are no isolated flow components
  // One could modify FlowFunction to hold edges indexed by the sources, which
  // will avoid a creation of the object
  auto PositiveFlowEdges = std::vector<std::vector<uint64_t>>(NumBlocks);
  for (const auto &Jump : Func.Jumps) {
    if (Jump.Flow > 0) {
      PositiveFlowEdges[Jump.Source].push_back(Jump.Target);
    }
  }

  // Run BFS from the source along edges with positive flow
  std::queue<uint64_t> Queue;
  auto Visited = BitVector(NumBlocks, false);
  Queue.push(Func.Entry);
  Visited[Func.Entry] = true;
  while (!Queue.empty()) {
    uint64_t Src = Queue.front();
    Queue.pop();
    for (uint64_t Dst : PositiveFlowEdges[Src]) {
      if (!Visited[Dst]) {
        Queue.push(Dst);
        Visited[Dst] = true;
      }
    }
  }

  // Verify that every block that has a positive flow is reached from the source
  // along edges with a positive flow
  for (uint64_t I = 0; I < NumBlocks; I++) {
    auto &Block = Func.Blocks[I];
    assert((Visited[I] || Block.Flow == 0) && "an isolated flow component");
  }
}
#endif

} // end of anonymous namespace

/// Apply the profile inference algorithm for a given function and provided
/// profi options
void llvm::applyFlowInference(const ProfiParams &Params, FlowFunction &Func) {
  // Check if the function has samples and assign initial flow values
  bool HasSamples = false;
  for (FlowBlock &Block : Func.Blocks) {
    if (Block.Weight > 0)
      HasSamples = true;
    Block.Flow = Block.Weight;
  }
  for (FlowJump &Jump : Func.Jumps) {
    if (Jump.Weight > 0)
      HasSamples = true;
    Jump.Flow = Jump.Weight;
  }

  // Quit early for functions with a single block or ones w/o samples
  if (Func.Blocks.size() <= 1 || !HasSamples)
    return;

#ifndef NDEBUG
  // Verify the input data
  verifyInput(Func);
#endif

  // Create and apply an inference network model
  auto InferenceNetwork = MinCostMaxFlow(Params);
  initializeNetwork(Params, InferenceNetwork, Func);
  InferenceNetwork.run();

  // Extract flow values for every block and every edge
  extractWeights(Params, InferenceNetwork, Func);

  // Post-processing adjustments to the flow
  auto Adjuster = FlowAdjuster(Params, Func);
  Adjuster.run();

#ifndef NDEBUG
  // Verify the result
  verifyOutput(Func);
#endif
}

/// Apply the profile inference algorithm for a given flow function
void llvm::applyFlowInference(FlowFunction &Func) {
  ProfiParams Params;
  // Set the params from the command-line flags.
  Params.EvenFlowDistribution = SampleProfileEvenFlowDistribution;
  Params.RebalanceUnknown = SampleProfileRebalanceUnknown;
  Params.JoinIslands = SampleProfileJoinIslands;
  Params.CostBlockInc = SampleProfileProfiCostBlockInc;
  Params.CostBlockDec = SampleProfileProfiCostBlockDec;
  Params.CostBlockEntryInc = SampleProfileProfiCostBlockEntryInc;
  Params.CostBlockEntryDec = SampleProfileProfiCostBlockEntryDec;
  Params.CostBlockZeroInc = SampleProfileProfiCostBlockZeroInc;
  Params.CostBlockUnknownInc = SampleProfileProfiCostBlockUnknownInc;

  applyFlowInference(Params, Func);
}
