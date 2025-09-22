//===- CodeLayout.cpp - Implementation of code layout algorithms ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The file implements "cache-aware" layout algorithms of basic blocks and
// functions in a binary.
//
// The algorithm tries to find a layout of nodes (basic blocks) of a given CFG
// optimizing jump locality and thus processor I-cache utilization. This is
// achieved via increasing the number of fall-through jumps and co-locating
// frequently executed nodes together. The name follows the underlying
// optimization problem, Extended-TSP, which is a generalization of classical
// (maximum) Traveling Salesmen Problem.
//
// The algorithm is a greedy heuristic that works with chains (ordered lists)
// of basic blocks. Initially all chains are isolated basic blocks. On every
// iteration, we pick a pair of chains whose merging yields the biggest increase
// in the ExtTSP score, which models how i-cache "friendly" a specific chain is.
// A pair of chains giving the maximum gain is merged into a new chain. The
// procedure stops when there is only one chain left, or when merging does not
// increase ExtTSP. In the latter case, the remaining chains are sorted by
// density in the decreasing order.
//
// An important aspect is the way two chains are merged. Unlike earlier
// algorithms (e.g., based on the approach of Pettis-Hansen), two
// chains, X and Y, are first split into three, X1, X2, and Y. Then we
// consider all possible ways of gluing the three chains (e.g., X1YX2, X1X2Y,
// X2X1Y, X2YX1, YX1X2, YX2X1) and choose the one producing the largest score.
// This improves the quality of the final result (the search space is larger)
// while keeping the implementation sufficiently fast.
//
// Reference:
//   * A. Newell and S. Pupyrev, Improved Basic Block Reordering,
//     IEEE Transactions on Computers, 2020
//     https://arxiv.org/abs/1809.04676
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CodeLayout.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

#include <cmath>
#include <set>

using namespace llvm;
using namespace llvm::codelayout;

#define DEBUG_TYPE "code-layout"

namespace llvm {
cl::opt<bool> EnableExtTspBlockPlacement(
    "enable-ext-tsp-block-placement", cl::Hidden, cl::init(false),
    cl::desc("Enable machine block placement based on the ext-tsp model, "
             "optimizing I-cache utilization."));

cl::opt<bool> ApplyExtTspWithoutProfile(
    "ext-tsp-apply-without-profile",
    cl::desc("Whether to apply ext-tsp placement for instances w/o profile"),
    cl::init(true), cl::Hidden);
} // namespace llvm

// Algorithm-specific params for Ext-TSP. The values are tuned for the best
// performance of large-scale front-end bound binaries.
static cl::opt<double> ForwardWeightCond(
    "ext-tsp-forward-weight-cond", cl::ReallyHidden, cl::init(0.1),
    cl::desc("The weight of conditional forward jumps for ExtTSP value"));

static cl::opt<double> ForwardWeightUncond(
    "ext-tsp-forward-weight-uncond", cl::ReallyHidden, cl::init(0.1),
    cl::desc("The weight of unconditional forward jumps for ExtTSP value"));

static cl::opt<double> BackwardWeightCond(
    "ext-tsp-backward-weight-cond", cl::ReallyHidden, cl::init(0.1),
    cl::desc("The weight of conditional backward jumps for ExtTSP value"));

static cl::opt<double> BackwardWeightUncond(
    "ext-tsp-backward-weight-uncond", cl::ReallyHidden, cl::init(0.1),
    cl::desc("The weight of unconditional backward jumps for ExtTSP value"));

static cl::opt<double> FallthroughWeightCond(
    "ext-tsp-fallthrough-weight-cond", cl::ReallyHidden, cl::init(1.0),
    cl::desc("The weight of conditional fallthrough jumps for ExtTSP value"));

static cl::opt<double> FallthroughWeightUncond(
    "ext-tsp-fallthrough-weight-uncond", cl::ReallyHidden, cl::init(1.05),
    cl::desc("The weight of unconditional fallthrough jumps for ExtTSP value"));

static cl::opt<unsigned> ForwardDistance(
    "ext-tsp-forward-distance", cl::ReallyHidden, cl::init(1024),
    cl::desc("The maximum distance (in bytes) of a forward jump for ExtTSP"));

static cl::opt<unsigned> BackwardDistance(
    "ext-tsp-backward-distance", cl::ReallyHidden, cl::init(640),
    cl::desc("The maximum distance (in bytes) of a backward jump for ExtTSP"));

// The maximum size of a chain created by the algorithm. The size is bounded
// so that the algorithm can efficiently process extremely large instances.
static cl::opt<unsigned>
    MaxChainSize("ext-tsp-max-chain-size", cl::ReallyHidden, cl::init(512),
                 cl::desc("The maximum size of a chain to create"));

// The maximum size of a chain for splitting. Larger values of the threshold
// may yield better quality at the cost of worsen run-time.
static cl::opt<unsigned> ChainSplitThreshold(
    "ext-tsp-chain-split-threshold", cl::ReallyHidden, cl::init(128),
    cl::desc("The maximum size of a chain to apply splitting"));

// The maximum ratio between densities of two chains for merging.
static cl::opt<double> MaxMergeDensityRatio(
    "ext-tsp-max-merge-density-ratio", cl::ReallyHidden, cl::init(100),
    cl::desc("The maximum ratio between densities of two chains for merging"));

// Algorithm-specific options for CDSort.
static cl::opt<unsigned> CacheEntries("cdsort-cache-entries", cl::ReallyHidden,
                                      cl::desc("The size of the cache"));

static cl::opt<unsigned> CacheSize("cdsort-cache-size", cl::ReallyHidden,
                                   cl::desc("The size of a line in the cache"));

static cl::opt<unsigned>
    CDMaxChainSize("cdsort-max-chain-size", cl::ReallyHidden,
                   cl::desc("The maximum size of a chain to create"));

static cl::opt<double> DistancePower(
    "cdsort-distance-power", cl::ReallyHidden,
    cl::desc("The power exponent for the distance-based locality"));

static cl::opt<double> FrequencyScale(
    "cdsort-frequency-scale", cl::ReallyHidden,
    cl::desc("The scale factor for the frequency-based locality"));

namespace {

// Epsilon for comparison of doubles.
constexpr double EPS = 1e-8;

// Compute the Ext-TSP score for a given jump.
double jumpExtTSPScore(uint64_t JumpDist, uint64_t JumpMaxDist, uint64_t Count,
                       double Weight) {
  if (JumpDist > JumpMaxDist)
    return 0;
  double Prob = 1.0 - static_cast<double>(JumpDist) / JumpMaxDist;
  return Weight * Prob * Count;
}

// Compute the Ext-TSP score for a jump between a given pair of blocks,
// using their sizes, (estimated) addresses and the jump execution count.
double extTSPScore(uint64_t SrcAddr, uint64_t SrcSize, uint64_t DstAddr,
                   uint64_t Count, bool IsConditional) {
  // Fallthrough
  if (SrcAddr + SrcSize == DstAddr) {
    return jumpExtTSPScore(0, 1, Count,
                           IsConditional ? FallthroughWeightCond
                                         : FallthroughWeightUncond);
  }
  // Forward
  if (SrcAddr + SrcSize < DstAddr) {
    const uint64_t Dist = DstAddr - (SrcAddr + SrcSize);
    return jumpExtTSPScore(Dist, ForwardDistance, Count,
                           IsConditional ? ForwardWeightCond
                                         : ForwardWeightUncond);
  }
  // Backward
  const uint64_t Dist = SrcAddr + SrcSize - DstAddr;
  return jumpExtTSPScore(Dist, BackwardDistance, Count,
                         IsConditional ? BackwardWeightCond
                                       : BackwardWeightUncond);
}

/// A type of merging two chains, X and Y. The former chain is split into
/// X1 and X2 and then concatenated with Y in the order specified by the type.
enum class MergeTypeT : int { X_Y, Y_X, X1_Y_X2, Y_X2_X1, X2_X1_Y };

/// The gain of merging two chains, that is, the Ext-TSP score of the merge
/// together with the corresponding merge 'type' and 'offset'.
struct MergeGainT {
  explicit MergeGainT() = default;
  explicit MergeGainT(double Score, size_t MergeOffset, MergeTypeT MergeType)
      : Score(Score), MergeOffset(MergeOffset), MergeType(MergeType) {}

  double score() const { return Score; }

  size_t mergeOffset() const { return MergeOffset; }

  MergeTypeT mergeType() const { return MergeType; }

  void setMergeType(MergeTypeT Ty) { MergeType = Ty; }

  // Returns 'true' iff Other is preferred over this.
  bool operator<(const MergeGainT &Other) const {
    return (Other.Score > EPS && Other.Score > Score + EPS);
  }

  // Update the current gain if Other is preferred over this.
  void updateIfLessThan(const MergeGainT &Other) {
    if (*this < Other)
      *this = Other;
  }

private:
  double Score{-1.0};
  size_t MergeOffset{0};
  MergeTypeT MergeType{MergeTypeT::X_Y};
};

struct JumpT;
struct ChainT;
struct ChainEdge;

/// A node in the graph, typically corresponding to a basic block in the CFG or
/// a function in the call graph.
struct NodeT {
  NodeT(const NodeT &) = delete;
  NodeT(NodeT &&) = default;
  NodeT &operator=(const NodeT &) = delete;
  NodeT &operator=(NodeT &&) = default;

  explicit NodeT(size_t Index, uint64_t Size, uint64_t Count)
      : Index(Index), Size(Size), ExecutionCount(Count) {}

  bool isEntry() const { return Index == 0; }

  // Check if Other is a successor of the node.
  bool isSuccessor(const NodeT *Other) const;

  // The total execution count of outgoing jumps.
  uint64_t outCount() const;

  // The total execution count of incoming jumps.
  uint64_t inCount() const;

  // The original index of the node in graph.
  size_t Index{0};
  // The index of the node in the current chain.
  size_t CurIndex{0};
  // The size of the node in the binary.
  uint64_t Size{0};
  // The execution count of the node in the profile data.
  uint64_t ExecutionCount{0};
  // The current chain of the node.
  ChainT *CurChain{nullptr};
  // The offset of the node in the current chain.
  mutable uint64_t EstimatedAddr{0};
  // Forced successor of the node in the graph.
  NodeT *ForcedSucc{nullptr};
  // Forced predecessor of the node in the graph.
  NodeT *ForcedPred{nullptr};
  // Outgoing jumps from the node.
  std::vector<JumpT *> OutJumps;
  // Incoming jumps to the node.
  std::vector<JumpT *> InJumps;
};

/// An arc in the graph, typically corresponding to a jump between two nodes.
struct JumpT {
  JumpT(const JumpT &) = delete;
  JumpT(JumpT &&) = default;
  JumpT &operator=(const JumpT &) = delete;
  JumpT &operator=(JumpT &&) = default;

  explicit JumpT(NodeT *Source, NodeT *Target, uint64_t ExecutionCount)
      : Source(Source), Target(Target), ExecutionCount(ExecutionCount) {}

  // Source node of the jump.
  NodeT *Source;
  // Target node of the jump.
  NodeT *Target;
  // Execution count of the arc in the profile data.
  uint64_t ExecutionCount{0};
  // Whether the jump corresponds to a conditional branch.
  bool IsConditional{false};
  // The offset of the jump from the source node.
  uint64_t Offset{0};
};

/// A chain (ordered sequence) of nodes in the graph.
struct ChainT {
  ChainT(const ChainT &) = delete;
  ChainT(ChainT &&) = default;
  ChainT &operator=(const ChainT &) = delete;
  ChainT &operator=(ChainT &&) = default;

  explicit ChainT(uint64_t Id, NodeT *Node)
      : Id(Id), ExecutionCount(Node->ExecutionCount), Size(Node->Size),
        Nodes(1, Node) {}

  size_t numBlocks() const { return Nodes.size(); }

  double density() const { return ExecutionCount / Size; }

  bool isEntry() const { return Nodes[0]->Index == 0; }

  bool isCold() const {
    for (NodeT *Node : Nodes) {
      if (Node->ExecutionCount > 0)
        return false;
    }
    return true;
  }

  ChainEdge *getEdge(ChainT *Other) const {
    for (const auto &[Chain, ChainEdge] : Edges) {
      if (Chain == Other)
        return ChainEdge;
    }
    return nullptr;
  }

  void removeEdge(ChainT *Other) {
    auto It = Edges.begin();
    while (It != Edges.end()) {
      if (It->first == Other) {
        Edges.erase(It);
        return;
      }
      It++;
    }
  }

  void addEdge(ChainT *Other, ChainEdge *Edge) {
    Edges.push_back(std::make_pair(Other, Edge));
  }

  void merge(ChainT *Other, std::vector<NodeT *> MergedBlocks) {
    Nodes = std::move(MergedBlocks);
    // Update the chain's data.
    ExecutionCount += Other->ExecutionCount;
    Size += Other->Size;
    Id = Nodes[0]->Index;
    // Update the node's data.
    for (size_t Idx = 0; Idx < Nodes.size(); Idx++) {
      Nodes[Idx]->CurChain = this;
      Nodes[Idx]->CurIndex = Idx;
    }
  }

  void mergeEdges(ChainT *Other);

  void clear() {
    Nodes.clear();
    Nodes.shrink_to_fit();
    Edges.clear();
    Edges.shrink_to_fit();
  }

  // Unique chain identifier.
  uint64_t Id;
  // Cached ext-tsp score for the chain.
  double Score{0};
  // The total execution count of the chain. Since the execution count of
  // a basic block is uint64_t, using doubles here to avoid overflow.
  double ExecutionCount{0};
  // The total size of the chain.
  uint64_t Size{0};
  // Nodes of the chain.
  std::vector<NodeT *> Nodes;
  // Adjacent chains and corresponding edges (lists of jumps).
  std::vector<std::pair<ChainT *, ChainEdge *>> Edges;
};

/// An edge in the graph representing jumps between two chains.
/// When nodes are merged into chains, the edges are combined too so that
/// there is always at most one edge between a pair of chains.
struct ChainEdge {
  ChainEdge(const ChainEdge &) = delete;
  ChainEdge(ChainEdge &&) = default;
  ChainEdge &operator=(const ChainEdge &) = delete;
  ChainEdge &operator=(ChainEdge &&) = delete;

  explicit ChainEdge(JumpT *Jump)
      : SrcChain(Jump->Source->CurChain), DstChain(Jump->Target->CurChain),
        Jumps(1, Jump) {}

  ChainT *srcChain() const { return SrcChain; }

  ChainT *dstChain() const { return DstChain; }

  bool isSelfEdge() const { return SrcChain == DstChain; }

  const std::vector<JumpT *> &jumps() const { return Jumps; }

  void appendJump(JumpT *Jump) { Jumps.push_back(Jump); }

  void moveJumps(ChainEdge *Other) {
    Jumps.insert(Jumps.end(), Other->Jumps.begin(), Other->Jumps.end());
    Other->Jumps.clear();
    Other->Jumps.shrink_to_fit();
  }

  void changeEndpoint(ChainT *From, ChainT *To) {
    if (From == SrcChain)
      SrcChain = To;
    if (From == DstChain)
      DstChain = To;
  }

  bool hasCachedMergeGain(ChainT *Src, ChainT *Dst) const {
    return Src == SrcChain ? CacheValidForward : CacheValidBackward;
  }

  MergeGainT getCachedMergeGain(ChainT *Src, ChainT *Dst) const {
    return Src == SrcChain ? CachedGainForward : CachedGainBackward;
  }

  void setCachedMergeGain(ChainT *Src, ChainT *Dst, MergeGainT MergeGain) {
    if (Src == SrcChain) {
      CachedGainForward = MergeGain;
      CacheValidForward = true;
    } else {
      CachedGainBackward = MergeGain;
      CacheValidBackward = true;
    }
  }

  void invalidateCache() {
    CacheValidForward = false;
    CacheValidBackward = false;
  }

  void setMergeGain(MergeGainT Gain) { CachedGain = Gain; }

  MergeGainT getMergeGain() const { return CachedGain; }

  double gain() const { return CachedGain.score(); }

private:
  // Source chain.
  ChainT *SrcChain{nullptr};
  // Destination chain.
  ChainT *DstChain{nullptr};
  // Original jumps in the binary with corresponding execution counts.
  std::vector<JumpT *> Jumps;
  // Cached gain value for merging the pair of chains.
  MergeGainT CachedGain;

  // Cached gain values for merging the pair of chains. Since the gain of
  // merging (Src, Dst) and (Dst, Src) might be different, we store both values
  // here and a flag indicating which of the options results in a higher gain.
  // Cached gain values.
  MergeGainT CachedGainForward;
  MergeGainT CachedGainBackward;
  // Whether the cached value must be recomputed.
  bool CacheValidForward{false};
  bool CacheValidBackward{false};
};

bool NodeT::isSuccessor(const NodeT *Other) const {
  for (JumpT *Jump : OutJumps)
    if (Jump->Target == Other)
      return true;
  return false;
}

uint64_t NodeT::outCount() const {
  uint64_t Count = 0;
  for (JumpT *Jump : OutJumps)
    Count += Jump->ExecutionCount;
  return Count;
}

uint64_t NodeT::inCount() const {
  uint64_t Count = 0;
  for (JumpT *Jump : InJumps)
    Count += Jump->ExecutionCount;
  return Count;
}

void ChainT::mergeEdges(ChainT *Other) {
  // Update edges adjacent to chain Other.
  for (const auto &[DstChain, DstEdge] : Other->Edges) {
    ChainT *TargetChain = DstChain == Other ? this : DstChain;
    ChainEdge *CurEdge = getEdge(TargetChain);
    if (CurEdge == nullptr) {
      DstEdge->changeEndpoint(Other, this);
      this->addEdge(TargetChain, DstEdge);
      if (DstChain != this && DstChain != Other)
        DstChain->addEdge(this, DstEdge);
    } else {
      CurEdge->moveJumps(DstEdge);
    }
    // Cleanup leftover edge.
    if (DstChain != Other)
      DstChain->removeEdge(Other);
  }
}

using NodeIter = std::vector<NodeT *>::const_iterator;
static std::vector<NodeT *> EmptyList;

/// A wrapper around three concatenated vectors (chains) of nodes; it is used
/// to avoid extra instantiation of the vectors.
struct MergedNodesT {
  MergedNodesT(NodeIter Begin1, NodeIter End1,
               NodeIter Begin2 = EmptyList.begin(),
               NodeIter End2 = EmptyList.end(),
               NodeIter Begin3 = EmptyList.begin(),
               NodeIter End3 = EmptyList.end())
      : Begin1(Begin1), End1(End1), Begin2(Begin2), End2(End2), Begin3(Begin3),
        End3(End3) {}

  template <typename F> void forEach(const F &Func) const {
    for (auto It = Begin1; It != End1; It++)
      Func(*It);
    for (auto It = Begin2; It != End2; It++)
      Func(*It);
    for (auto It = Begin3; It != End3; It++)
      Func(*It);
  }

  std::vector<NodeT *> getNodes() const {
    std::vector<NodeT *> Result;
    Result.reserve(std::distance(Begin1, End1) + std::distance(Begin2, End2) +
                   std::distance(Begin3, End3));
    Result.insert(Result.end(), Begin1, End1);
    Result.insert(Result.end(), Begin2, End2);
    Result.insert(Result.end(), Begin3, End3);
    return Result;
  }

  const NodeT *getFirstNode() const { return *Begin1; }

private:
  NodeIter Begin1;
  NodeIter End1;
  NodeIter Begin2;
  NodeIter End2;
  NodeIter Begin3;
  NodeIter End3;
};

/// A wrapper around two concatenated vectors (chains) of jumps.
struct MergedJumpsT {
  MergedJumpsT(const std::vector<JumpT *> *Jumps1,
               const std::vector<JumpT *> *Jumps2 = nullptr) {
    assert(!Jumps1->empty() && "cannot merge empty jump list");
    JumpArray[0] = Jumps1;
    JumpArray[1] = Jumps2;
  }

  template <typename F> void forEach(const F &Func) const {
    for (auto Jumps : JumpArray)
      if (Jumps != nullptr)
        for (JumpT *Jump : *Jumps)
          Func(Jump);
  }

private:
  std::array<const std::vector<JumpT *> *, 2> JumpArray{nullptr, nullptr};
};

/// Merge two chains of nodes respecting a given 'type' and 'offset'.
///
/// If MergeType == 0, then the result is a concatenation of two chains.
/// Otherwise, the first chain is cut into two sub-chains at the offset,
/// and merged using all possible ways of concatenating three chains.
MergedNodesT mergeNodes(const std::vector<NodeT *> &X,
                        const std::vector<NodeT *> &Y, size_t MergeOffset,
                        MergeTypeT MergeType) {
  // Split the first chain, X, into X1 and X2.
  NodeIter BeginX1 = X.begin();
  NodeIter EndX1 = X.begin() + MergeOffset;
  NodeIter BeginX2 = X.begin() + MergeOffset;
  NodeIter EndX2 = X.end();
  NodeIter BeginY = Y.begin();
  NodeIter EndY = Y.end();

  // Construct a new chain from the three existing ones.
  switch (MergeType) {
  case MergeTypeT::X_Y:
    return MergedNodesT(BeginX1, EndX2, BeginY, EndY);
  case MergeTypeT::Y_X:
    return MergedNodesT(BeginY, EndY, BeginX1, EndX2);
  case MergeTypeT::X1_Y_X2:
    return MergedNodesT(BeginX1, EndX1, BeginY, EndY, BeginX2, EndX2);
  case MergeTypeT::Y_X2_X1:
    return MergedNodesT(BeginY, EndY, BeginX2, EndX2, BeginX1, EndX1);
  case MergeTypeT::X2_X1_Y:
    return MergedNodesT(BeginX2, EndX2, BeginX1, EndX1, BeginY, EndY);
  }
  llvm_unreachable("unexpected chain merge type");
}

/// The implementation of the ExtTSP algorithm.
class ExtTSPImpl {
public:
  ExtTSPImpl(ArrayRef<uint64_t> NodeSizes, ArrayRef<uint64_t> NodeCounts,
             ArrayRef<EdgeCount> EdgeCounts)
      : NumNodes(NodeSizes.size()) {
    initialize(NodeSizes, NodeCounts, EdgeCounts);
  }

  /// Run the algorithm and return an optimized ordering of nodes.
  std::vector<uint64_t> run() {
    // Pass 1: Merge nodes with their mutually forced successors
    mergeForcedPairs();

    // Pass 2: Merge pairs of chains while improving the ExtTSP objective
    mergeChainPairs();

    // Pass 3: Merge cold nodes to reduce code size
    mergeColdChains();

    // Collect nodes from all chains
    return concatChains();
  }

private:
  /// Initialize the algorithm's data structures.
  void initialize(const ArrayRef<uint64_t> &NodeSizes,
                  const ArrayRef<uint64_t> &NodeCounts,
                  const ArrayRef<EdgeCount> &EdgeCounts) {
    // Initialize nodes.
    AllNodes.reserve(NumNodes);
    for (uint64_t Idx = 0; Idx < NumNodes; Idx++) {
      uint64_t Size = std::max<uint64_t>(NodeSizes[Idx], 1ULL);
      uint64_t ExecutionCount = NodeCounts[Idx];
      // The execution count of the entry node is set to at least one.
      if (Idx == 0 && ExecutionCount == 0)
        ExecutionCount = 1;
      AllNodes.emplace_back(Idx, Size, ExecutionCount);
    }

    // Initialize jumps between the nodes.
    SuccNodes.resize(NumNodes);
    PredNodes.resize(NumNodes);
    std::vector<uint64_t> OutDegree(NumNodes, 0);
    AllJumps.reserve(EdgeCounts.size());
    for (auto Edge : EdgeCounts) {
      ++OutDegree[Edge.src];
      // Ignore self-edges.
      if (Edge.src == Edge.dst)
        continue;

      SuccNodes[Edge.src].push_back(Edge.dst);
      PredNodes[Edge.dst].push_back(Edge.src);
      if (Edge.count > 0) {
        NodeT &PredNode = AllNodes[Edge.src];
        NodeT &SuccNode = AllNodes[Edge.dst];
        AllJumps.emplace_back(&PredNode, &SuccNode, Edge.count);
        SuccNode.InJumps.push_back(&AllJumps.back());
        PredNode.OutJumps.push_back(&AllJumps.back());
        // Adjust execution counts.
        PredNode.ExecutionCount = std::max(PredNode.ExecutionCount, Edge.count);
        SuccNode.ExecutionCount = std::max(SuccNode.ExecutionCount, Edge.count);
      }
    }
    for (JumpT &Jump : AllJumps) {
      assert(OutDegree[Jump.Source->Index] > 0 &&
             "incorrectly computed out-degree of the block");
      Jump.IsConditional = OutDegree[Jump.Source->Index] > 1;
    }

    // Initialize chains.
    AllChains.reserve(NumNodes);
    HotChains.reserve(NumNodes);
    for (NodeT &Node : AllNodes) {
      // Create a chain.
      AllChains.emplace_back(Node.Index, &Node);
      Node.CurChain = &AllChains.back();
      if (Node.ExecutionCount > 0)
        HotChains.push_back(&AllChains.back());
    }

    // Initialize chain edges.
    AllEdges.reserve(AllJumps.size());
    for (NodeT &PredNode : AllNodes) {
      for (JumpT *Jump : PredNode.OutJumps) {
        assert(Jump->ExecutionCount > 0 && "incorrectly initialized jump");
        NodeT *SuccNode = Jump->Target;
        ChainEdge *CurEdge = PredNode.CurChain->getEdge(SuccNode->CurChain);
        // This edge is already present in the graph.
        if (CurEdge != nullptr) {
          assert(SuccNode->CurChain->getEdge(PredNode.CurChain) != nullptr);
          CurEdge->appendJump(Jump);
          continue;
        }
        // This is a new edge.
        AllEdges.emplace_back(Jump);
        PredNode.CurChain->addEdge(SuccNode->CurChain, &AllEdges.back());
        SuccNode->CurChain->addEdge(PredNode.CurChain, &AllEdges.back());
      }
    }
  }

  /// For a pair of nodes, A and B, node B is the forced successor of A,
  /// if (i) all jumps (based on profile) from A goes to B and (ii) all jumps
  /// to B are from A. Such nodes should be adjacent in the optimal ordering;
  /// the method finds and merges such pairs of nodes.
  void mergeForcedPairs() {
    // Find forced pairs of blocks.
    for (NodeT &Node : AllNodes) {
      if (SuccNodes[Node.Index].size() == 1 &&
          PredNodes[SuccNodes[Node.Index][0]].size() == 1 &&
          SuccNodes[Node.Index][0] != 0) {
        size_t SuccIndex = SuccNodes[Node.Index][0];
        Node.ForcedSucc = &AllNodes[SuccIndex];
        AllNodes[SuccIndex].ForcedPred = &Node;
      }
    }

    // There might be 'cycles' in the forced dependencies, since profile
    // data isn't 100% accurate. Typically this is observed in loops, when the
    // loop edges are the hottest successors for the basic blocks of the loop.
    // Break the cycles by choosing the node with the smallest index as the
    // head. This helps to keep the original order of the loops, which likely
    // have already been rotated in the optimized manner.
    for (NodeT &Node : AllNodes) {
      if (Node.ForcedSucc == nullptr || Node.ForcedPred == nullptr)
        continue;

      NodeT *SuccNode = Node.ForcedSucc;
      while (SuccNode != nullptr && SuccNode != &Node) {
        SuccNode = SuccNode->ForcedSucc;
      }
      if (SuccNode == nullptr)
        continue;
      // Break the cycle.
      AllNodes[Node.ForcedPred->Index].ForcedSucc = nullptr;
      Node.ForcedPred = nullptr;
    }

    // Merge nodes with their fallthrough successors.
    for (NodeT &Node : AllNodes) {
      if (Node.ForcedPred == nullptr && Node.ForcedSucc != nullptr) {
        const NodeT *CurBlock = &Node;
        while (CurBlock->ForcedSucc != nullptr) {
          const NodeT *NextBlock = CurBlock->ForcedSucc;
          mergeChains(Node.CurChain, NextBlock->CurChain, 0, MergeTypeT::X_Y);
          CurBlock = NextBlock;
        }
      }
    }
  }

  /// Merge pairs of chains while improving the ExtTSP objective.
  void mergeChainPairs() {
    /// Deterministically compare pairs of chains.
    auto compareChainPairs = [](const ChainT *A1, const ChainT *B1,
                                const ChainT *A2, const ChainT *B2) {
      return std::make_tuple(A1->Id, B1->Id) < std::make_tuple(A2->Id, B2->Id);
    };

    while (HotChains.size() > 1) {
      ChainT *BestChainPred = nullptr;
      ChainT *BestChainSucc = nullptr;
      MergeGainT BestGain;
      // Iterate over all pairs of chains.
      for (ChainT *ChainPred : HotChains) {
        // Get candidates for merging with the current chain.
        for (const auto &[ChainSucc, Edge] : ChainPred->Edges) {
          // Ignore loop edges.
          if (Edge->isSelfEdge())
            continue;
          // Skip the merge if the combined chain violates the maximum specified
          // size.
          if (ChainPred->numBlocks() + ChainSucc->numBlocks() >= MaxChainSize)
            continue;
          // Don't merge the chains if they have vastly different densities.
          // Skip the merge if the ratio between the densities exceeds
          // MaxMergeDensityRatio. Smaller values of the option result in fewer
          // merges, and hence, more chains.
          const double ChainPredDensity = ChainPred->density();
          const double ChainSuccDensity = ChainSucc->density();
          assert(ChainPredDensity > 0.0 && ChainSuccDensity > 0.0 &&
                 "incorrectly computed chain densities");
          auto [MinDensity, MaxDensity] =
              std::minmax(ChainPredDensity, ChainSuccDensity);
          const double Ratio = MaxDensity / MinDensity;
          if (Ratio > MaxMergeDensityRatio)
            continue;

          // Compute the gain of merging the two chains.
          MergeGainT CurGain = getBestMergeGain(ChainPred, ChainSucc, Edge);
          if (CurGain.score() <= EPS)
            continue;

          if (BestGain < CurGain ||
              (std::abs(CurGain.score() - BestGain.score()) < EPS &&
               compareChainPairs(ChainPred, ChainSucc, BestChainPred,
                                 BestChainSucc))) {
            BestGain = CurGain;
            BestChainPred = ChainPred;
            BestChainSucc = ChainSucc;
          }
        }
      }

      // Stop merging when there is no improvement.
      if (BestGain.score() <= EPS)
        break;

      // Merge the best pair of chains.
      mergeChains(BestChainPred, BestChainSucc, BestGain.mergeOffset(),
                  BestGain.mergeType());
    }
  }

  /// Merge remaining nodes into chains w/o taking jump counts into
  /// consideration. This allows to maintain the original node order in the
  /// absence of profile data.
  void mergeColdChains() {
    for (size_t SrcBB = 0; SrcBB < NumNodes; SrcBB++) {
      // Iterating in reverse order to make sure original fallthrough jumps are
      // merged first; this might be beneficial for code size.
      size_t NumSuccs = SuccNodes[SrcBB].size();
      for (size_t Idx = 0; Idx < NumSuccs; Idx++) {
        size_t DstBB = SuccNodes[SrcBB][NumSuccs - Idx - 1];
        ChainT *SrcChain = AllNodes[SrcBB].CurChain;
        ChainT *DstChain = AllNodes[DstBB].CurChain;
        if (SrcChain != DstChain && !DstChain->isEntry() &&
            SrcChain->Nodes.back()->Index == SrcBB &&
            DstChain->Nodes.front()->Index == DstBB &&
            SrcChain->isCold() == DstChain->isCold()) {
          mergeChains(SrcChain, DstChain, 0, MergeTypeT::X_Y);
        }
      }
    }
  }

  /// Compute the Ext-TSP score for a given node order and a list of jumps.
  double extTSPScore(const MergedNodesT &Nodes,
                     const MergedJumpsT &Jumps) const {
    uint64_t CurAddr = 0;
    Nodes.forEach([&](const NodeT *Node) {
      Node->EstimatedAddr = CurAddr;
      CurAddr += Node->Size;
    });

    double Score = 0;
    Jumps.forEach([&](const JumpT *Jump) {
      const NodeT *SrcBlock = Jump->Source;
      const NodeT *DstBlock = Jump->Target;
      Score += ::extTSPScore(SrcBlock->EstimatedAddr, SrcBlock->Size,
                             DstBlock->EstimatedAddr, Jump->ExecutionCount,
                             Jump->IsConditional);
    });
    return Score;
  }

  /// Compute the gain of merging two chains.
  ///
  /// The function considers all possible ways of merging two chains and
  /// computes the one having the largest increase in ExtTSP objective. The
  /// result is a pair with the first element being the gain and the second
  /// element being the corresponding merging type.
  MergeGainT getBestMergeGain(ChainT *ChainPred, ChainT *ChainSucc,
                              ChainEdge *Edge) const {
    if (Edge->hasCachedMergeGain(ChainPred, ChainSucc))
      return Edge->getCachedMergeGain(ChainPred, ChainSucc);

    assert(!Edge->jumps().empty() && "trying to merge chains w/o jumps");
    // Precompute jumps between ChainPred and ChainSucc.
    ChainEdge *EdgePP = ChainPred->getEdge(ChainPred);
    MergedJumpsT Jumps(&Edge->jumps(), EdgePP ? &EdgePP->jumps() : nullptr);

    // This object holds the best chosen gain of merging two chains.
    MergeGainT Gain = MergeGainT();

    /// Given a merge offset and a list of merge types, try to merge two chains
    /// and update Gain with a better alternative.
    auto tryChainMerging = [&](size_t Offset,
                               const std::vector<MergeTypeT> &MergeTypes) {
      // Skip merging corresponding to concatenation w/o splitting.
      if (Offset == 0 || Offset == ChainPred->Nodes.size())
        return;
      // Skip merging if it breaks Forced successors.
      NodeT *Node = ChainPred->Nodes[Offset - 1];
      if (Node->ForcedSucc != nullptr)
        return;
      // Apply the merge, compute the corresponding gain, and update the best
      // value, if the merge is beneficial.
      for (const MergeTypeT &MergeType : MergeTypes) {
        Gain.updateIfLessThan(
            computeMergeGain(ChainPred, ChainSucc, Jumps, Offset, MergeType));
      }
    };

    // Try to concatenate two chains w/o splitting.
    Gain.updateIfLessThan(
        computeMergeGain(ChainPred, ChainSucc, Jumps, 0, MergeTypeT::X_Y));

    // Attach (a part of) ChainPred before the first node of ChainSucc.
    for (JumpT *Jump : ChainSucc->Nodes.front()->InJumps) {
      const NodeT *SrcBlock = Jump->Source;
      if (SrcBlock->CurChain != ChainPred)
        continue;
      size_t Offset = SrcBlock->CurIndex + 1;
      tryChainMerging(Offset, {MergeTypeT::X1_Y_X2, MergeTypeT::X2_X1_Y});
    }

    // Attach (a part of) ChainPred after the last node of ChainSucc.
    for (JumpT *Jump : ChainSucc->Nodes.back()->OutJumps) {
      const NodeT *DstBlock = Jump->Target;
      if (DstBlock->CurChain != ChainPred)
        continue;
      size_t Offset = DstBlock->CurIndex;
      tryChainMerging(Offset, {MergeTypeT::X1_Y_X2, MergeTypeT::Y_X2_X1});
    }

    // Try to break ChainPred in various ways and concatenate with ChainSucc.
    if (ChainPred->Nodes.size() <= ChainSplitThreshold) {
      for (size_t Offset = 1; Offset < ChainPred->Nodes.size(); Offset++) {
        // Do not split the chain along a fall-through jump. One of the two
        // loops above may still "break" such a jump whenever it results in a
        // new fall-through.
        const NodeT *BB = ChainPred->Nodes[Offset - 1];
        const NodeT *BB2 = ChainPred->Nodes[Offset];
        if (BB->isSuccessor(BB2))
          continue;

        // In practice, applying X2_Y_X1 merging almost never provides benefits;
        // thus, we exclude it from consideration to reduce the search space.
        tryChainMerging(Offset, {MergeTypeT::X1_Y_X2, MergeTypeT::Y_X2_X1,
                                 MergeTypeT::X2_X1_Y});
      }
    }

    Edge->setCachedMergeGain(ChainPred, ChainSucc, Gain);
    return Gain;
  }

  /// Compute the score gain of merging two chains, respecting a given
  /// merge 'type' and 'offset'.
  ///
  /// The two chains are not modified in the method.
  MergeGainT computeMergeGain(const ChainT *ChainPred, const ChainT *ChainSucc,
                              const MergedJumpsT &Jumps, size_t MergeOffset,
                              MergeTypeT MergeType) const {
    MergedNodesT MergedNodes =
        mergeNodes(ChainPred->Nodes, ChainSucc->Nodes, MergeOffset, MergeType);

    // Do not allow a merge that does not preserve the original entry point.
    if ((ChainPred->isEntry() || ChainSucc->isEntry()) &&
        !MergedNodes.getFirstNode()->isEntry())
      return MergeGainT();

    // The gain for the new chain.
    double NewScore = extTSPScore(MergedNodes, Jumps);
    double CurScore = ChainPred->Score;
    return MergeGainT(NewScore - CurScore, MergeOffset, MergeType);
  }

  /// Merge chain From into chain Into, update the list of active chains,
  /// adjacency information, and the corresponding cached values.
  void mergeChains(ChainT *Into, ChainT *From, size_t MergeOffset,
                   MergeTypeT MergeType) {
    assert(Into != From && "a chain cannot be merged with itself");

    // Merge the nodes.
    MergedNodesT MergedNodes =
        mergeNodes(Into->Nodes, From->Nodes, MergeOffset, MergeType);
    Into->merge(From, MergedNodes.getNodes());

    // Merge the edges.
    Into->mergeEdges(From);
    From->clear();

    // Update cached ext-tsp score for the new chain.
    ChainEdge *SelfEdge = Into->getEdge(Into);
    if (SelfEdge != nullptr) {
      MergedNodes = MergedNodesT(Into->Nodes.begin(), Into->Nodes.end());
      MergedJumpsT MergedJumps(&SelfEdge->jumps());
      Into->Score = extTSPScore(MergedNodes, MergedJumps);
    }

    // Remove the chain from the list of active chains.
    llvm::erase(HotChains, From);

    // Invalidate caches.
    for (auto EdgeIt : Into->Edges)
      EdgeIt.second->invalidateCache();
  }

  /// Concatenate all chains into the final order.
  std::vector<uint64_t> concatChains() {
    // Collect non-empty chains.
    std::vector<const ChainT *> SortedChains;
    for (ChainT &Chain : AllChains) {
      if (!Chain.Nodes.empty())
        SortedChains.push_back(&Chain);
    }

    // Sorting chains by density in the decreasing order.
    std::sort(SortedChains.begin(), SortedChains.end(),
              [&](const ChainT *L, const ChainT *R) {
                // Place the entry point at the beginning of the order.
                if (L->isEntry() != R->isEntry())
                  return L->isEntry();

                // Compare by density and break ties by chain identifiers.
                return std::make_tuple(-L->density(), L->Id) <
                       std::make_tuple(-R->density(), R->Id);
              });

    // Collect the nodes in the order specified by their chains.
    std::vector<uint64_t> Order;
    Order.reserve(NumNodes);
    for (const ChainT *Chain : SortedChains)
      for (NodeT *Node : Chain->Nodes)
        Order.push_back(Node->Index);
    return Order;
  }

private:
  /// The number of nodes in the graph.
  const size_t NumNodes;

  /// Successors of each node.
  std::vector<std::vector<uint64_t>> SuccNodes;

  /// Predecessors of each node.
  std::vector<std::vector<uint64_t>> PredNodes;

  /// All nodes (basic blocks) in the graph.
  std::vector<NodeT> AllNodes;

  /// All jumps between the nodes.
  std::vector<JumpT> AllJumps;

  /// All chains of nodes.
  std::vector<ChainT> AllChains;

  /// All edges between the chains.
  std::vector<ChainEdge> AllEdges;

  /// Active chains. The vector gets updated at runtime when chains are merged.
  std::vector<ChainT *> HotChains;
};

/// The implementation of the Cache-Directed Sort (CDSort) algorithm for
/// ordering functions represented by a call graph.
class CDSortImpl {
public:
  CDSortImpl(const CDSortConfig &Config, ArrayRef<uint64_t> NodeSizes,
             ArrayRef<uint64_t> NodeCounts, ArrayRef<EdgeCount> EdgeCounts,
             ArrayRef<uint64_t> EdgeOffsets)
      : Config(Config), NumNodes(NodeSizes.size()) {
    initialize(NodeSizes, NodeCounts, EdgeCounts, EdgeOffsets);
  }

  /// Run the algorithm and return an ordered set of function clusters.
  std::vector<uint64_t> run() {
    // Merge pairs of chains while improving the objective.
    mergeChainPairs();

    // Collect nodes from all the chains.
    return concatChains();
  }

private:
  /// Initialize the algorithm's data structures.
  void initialize(const ArrayRef<uint64_t> &NodeSizes,
                  const ArrayRef<uint64_t> &NodeCounts,
                  const ArrayRef<EdgeCount> &EdgeCounts,
                  const ArrayRef<uint64_t> &EdgeOffsets) {
    // Initialize nodes.
    AllNodes.reserve(NumNodes);
    for (uint64_t Node = 0; Node < NumNodes; Node++) {
      uint64_t Size = std::max<uint64_t>(NodeSizes[Node], 1ULL);
      uint64_t ExecutionCount = NodeCounts[Node];
      AllNodes.emplace_back(Node, Size, ExecutionCount);
      TotalSamples += ExecutionCount;
      if (ExecutionCount > 0)
        TotalSize += Size;
    }

    // Initialize jumps between the nodes.
    SuccNodes.resize(NumNodes);
    PredNodes.resize(NumNodes);
    AllJumps.reserve(EdgeCounts.size());
    for (size_t I = 0; I < EdgeCounts.size(); I++) {
      auto [Pred, Succ, Count] = EdgeCounts[I];
      // Ignore recursive calls.
      if (Pred == Succ)
        continue;

      SuccNodes[Pred].push_back(Succ);
      PredNodes[Succ].push_back(Pred);
      if (Count > 0) {
        NodeT &PredNode = AllNodes[Pred];
        NodeT &SuccNode = AllNodes[Succ];
        AllJumps.emplace_back(&PredNode, &SuccNode, Count);
        AllJumps.back().Offset = EdgeOffsets[I];
        SuccNode.InJumps.push_back(&AllJumps.back());
        PredNode.OutJumps.push_back(&AllJumps.back());
        // Adjust execution counts.
        PredNode.ExecutionCount = std::max(PredNode.ExecutionCount, Count);
        SuccNode.ExecutionCount = std::max(SuccNode.ExecutionCount, Count);
      }
    }

    // Initialize chains.
    AllChains.reserve(NumNodes);
    for (NodeT &Node : AllNodes) {
      // Adjust execution counts.
      Node.ExecutionCount = std::max(Node.ExecutionCount, Node.inCount());
      Node.ExecutionCount = std::max(Node.ExecutionCount, Node.outCount());
      // Create chain.
      AllChains.emplace_back(Node.Index, &Node);
      Node.CurChain = &AllChains.back();
    }

    // Initialize chain edges.
    AllEdges.reserve(AllJumps.size());
    for (NodeT &PredNode : AllNodes) {
      for (JumpT *Jump : PredNode.OutJumps) {
        NodeT *SuccNode = Jump->Target;
        ChainEdge *CurEdge = PredNode.CurChain->getEdge(SuccNode->CurChain);
        // This edge is already present in the graph.
        if (CurEdge != nullptr) {
          assert(SuccNode->CurChain->getEdge(PredNode.CurChain) != nullptr);
          CurEdge->appendJump(Jump);
          continue;
        }
        // This is a new edge.
        AllEdges.emplace_back(Jump);
        PredNode.CurChain->addEdge(SuccNode->CurChain, &AllEdges.back());
        SuccNode->CurChain->addEdge(PredNode.CurChain, &AllEdges.back());
      }
    }
  }

  /// Merge pairs of chains while there is an improvement in the objective.
  void mergeChainPairs() {
    // Create a priority queue containing all edges ordered by the merge gain.
    auto GainComparator = [](ChainEdge *L, ChainEdge *R) {
      return std::make_tuple(-L->gain(), L->srcChain()->Id, L->dstChain()->Id) <
             std::make_tuple(-R->gain(), R->srcChain()->Id, R->dstChain()->Id);
    };
    std::set<ChainEdge *, decltype(GainComparator)> Queue(GainComparator);

    // Insert the edges into the queue.
    [[maybe_unused]] size_t NumActiveChains = 0;
    for (NodeT &Node : AllNodes) {
      if (Node.ExecutionCount == 0)
        continue;
      ++NumActiveChains;
      for (const auto &[_, Edge] : Node.CurChain->Edges) {
        // Ignore self-edges.
        if (Edge->isSelfEdge())
          continue;
        // Ignore already processed edges.
        if (Edge->gain() != -1.0)
          continue;

        // Compute the gain of merging the two chains.
        MergeGainT Gain = getBestMergeGain(Edge);
        Edge->setMergeGain(Gain);

        if (Edge->gain() > EPS)
          Queue.insert(Edge);
      }
    }

    // Merge the chains while the gain of merging is positive.
    while (!Queue.empty()) {
      // Extract the best (top) edge for merging.
      ChainEdge *BestEdge = *Queue.begin();
      Queue.erase(Queue.begin());
      ChainT *BestSrcChain = BestEdge->srcChain();
      ChainT *BestDstChain = BestEdge->dstChain();

      // Remove outdated edges from the queue.
      for (const auto &[_, ChainEdge] : BestSrcChain->Edges)
        Queue.erase(ChainEdge);
      for (const auto &[_, ChainEdge] : BestDstChain->Edges)
        Queue.erase(ChainEdge);

      // Merge the best pair of chains.
      MergeGainT BestGain = BestEdge->getMergeGain();
      mergeChains(BestSrcChain, BestDstChain, BestGain.mergeOffset(),
                  BestGain.mergeType());
      --NumActiveChains;

      // Insert newly created edges into the queue.
      for (const auto &[_, Edge] : BestSrcChain->Edges) {
        // Ignore loop edges.
        if (Edge->isSelfEdge())
          continue;
        if (Edge->srcChain()->numBlocks() + Edge->dstChain()->numBlocks() >
            Config.MaxChainSize)
          continue;

        // Compute the gain of merging the two chains.
        MergeGainT Gain = getBestMergeGain(Edge);
        Edge->setMergeGain(Gain);

        if (Edge->gain() > EPS)
          Queue.insert(Edge);
      }
    }

    LLVM_DEBUG(dbgs() << "Cache-directed function sorting reduced the number"
                      << " of chains from " << NumNodes << " to "
                      << NumActiveChains << "\n");
  }

  /// Compute the gain of merging two chains.
  ///
  /// The function considers all possible ways of merging two chains and
  /// computes the one having the largest increase in ExtTSP objective. The
  /// result is a pair with the first element being the gain and the second
  /// element being the corresponding merging type.
  MergeGainT getBestMergeGain(ChainEdge *Edge) const {
    assert(!Edge->jumps().empty() && "trying to merge chains w/o jumps");
    // Precompute jumps between ChainPred and ChainSucc.
    MergedJumpsT Jumps(&Edge->jumps());
    ChainT *SrcChain = Edge->srcChain();
    ChainT *DstChain = Edge->dstChain();

    // This object holds the best currently chosen gain of merging two chains.
    MergeGainT Gain = MergeGainT();

    /// Given a list of merge types, try to merge two chains and update Gain
    /// with a better alternative.
    auto tryChainMerging = [&](const std::vector<MergeTypeT> &MergeTypes) {
      // Apply the merge, compute the corresponding gain, and update the best
      // value, if the merge is beneficial.
      for (const MergeTypeT &MergeType : MergeTypes) {
        MergeGainT NewGain =
            computeMergeGain(SrcChain, DstChain, Jumps, MergeType);

        // When forward and backward gains are the same, prioritize merging that
        // preserves the original order of the functions in the binary.
        if (std::abs(Gain.score() - NewGain.score()) < EPS) {
          if ((MergeType == MergeTypeT::X_Y && SrcChain->Id < DstChain->Id) ||
              (MergeType == MergeTypeT::Y_X && SrcChain->Id > DstChain->Id)) {
            Gain = NewGain;
          }
        } else if (NewGain.score() > Gain.score() + EPS) {
          Gain = NewGain;
        }
      }
    };

    // Try to concatenate two chains w/o splitting.
    tryChainMerging({MergeTypeT::X_Y, MergeTypeT::Y_X});

    return Gain;
  }

  /// Compute the score gain of merging two chains, respecting a given type.
  ///
  /// The two chains are not modified in the method.
  MergeGainT computeMergeGain(ChainT *ChainPred, ChainT *ChainSucc,
                              const MergedJumpsT &Jumps,
                              MergeTypeT MergeType) const {
    // This doesn't depend on the ordering of the nodes
    double FreqGain = freqBasedLocalityGain(ChainPred, ChainSucc);

    // Merge offset is always 0, as the chains are not split.
    size_t MergeOffset = 0;
    auto MergedBlocks =
        mergeNodes(ChainPred->Nodes, ChainSucc->Nodes, MergeOffset, MergeType);
    double DistGain = distBasedLocalityGain(MergedBlocks, Jumps);

    double GainScore = DistGain + Config.FrequencyScale * FreqGain;
    // Scale the result to increase the importance of merging short chains.
    if (GainScore >= 0.0)
      GainScore /= std::min(ChainPred->Size, ChainSucc->Size);

    return MergeGainT(GainScore, MergeOffset, MergeType);
  }

  /// Compute the change of the frequency locality after merging the chains.
  double freqBasedLocalityGain(ChainT *ChainPred, ChainT *ChainSucc) const {
    auto missProbability = [&](double ChainDensity) {
      double PageSamples = ChainDensity * Config.CacheSize;
      if (PageSamples >= TotalSamples)
        return 0.0;
      double P = PageSamples / TotalSamples;
      return pow(1.0 - P, static_cast<double>(Config.CacheEntries));
    };

    // Cache misses on the chains before merging.
    double CurScore =
        ChainPred->ExecutionCount * missProbability(ChainPred->density()) +
        ChainSucc->ExecutionCount * missProbability(ChainSucc->density());

    // Cache misses on the merged chain
    double MergedCounts = ChainPred->ExecutionCount + ChainSucc->ExecutionCount;
    double MergedSize = ChainPred->Size + ChainSucc->Size;
    double MergedDensity = static_cast<double>(MergedCounts) / MergedSize;
    double NewScore = MergedCounts * missProbability(MergedDensity);

    return CurScore - NewScore;
  }

  /// Compute the distance locality for a jump / call.
  double distScore(uint64_t SrcAddr, uint64_t DstAddr, uint64_t Count) const {
    uint64_t Dist = SrcAddr <= DstAddr ? DstAddr - SrcAddr : SrcAddr - DstAddr;
    double D = Dist == 0 ? 0.1 : static_cast<double>(Dist);
    return static_cast<double>(Count) * std::pow(D, -Config.DistancePower);
  }

  /// Compute the change of the distance locality after merging the chains.
  double distBasedLocalityGain(const MergedNodesT &Nodes,
                               const MergedJumpsT &Jumps) const {
    uint64_t CurAddr = 0;
    Nodes.forEach([&](const NodeT *Node) {
      Node->EstimatedAddr = CurAddr;
      CurAddr += Node->Size;
    });

    double CurScore = 0;
    double NewScore = 0;
    Jumps.forEach([&](const JumpT *Jump) {
      uint64_t SrcAddr = Jump->Source->EstimatedAddr + Jump->Offset;
      uint64_t DstAddr = Jump->Target->EstimatedAddr;
      NewScore += distScore(SrcAddr, DstAddr, Jump->ExecutionCount);
      CurScore += distScore(0, TotalSize, Jump->ExecutionCount);
    });
    return NewScore - CurScore;
  }

  /// Merge chain From into chain Into, update the list of active chains,
  /// adjacency information, and the corresponding cached values.
  void mergeChains(ChainT *Into, ChainT *From, size_t MergeOffset,
                   MergeTypeT MergeType) {
    assert(Into != From && "a chain cannot be merged with itself");

    // Merge the nodes.
    MergedNodesT MergedNodes =
        mergeNodes(Into->Nodes, From->Nodes, MergeOffset, MergeType);
    Into->merge(From, MergedNodes.getNodes());

    // Merge the edges.
    Into->mergeEdges(From);
    From->clear();
  }

  /// Concatenate all chains into the final order.
  std::vector<uint64_t> concatChains() {
    // Collect chains and calculate density stats for their sorting.
    std::vector<const ChainT *> SortedChains;
    DenseMap<const ChainT *, double> ChainDensity;
    for (ChainT &Chain : AllChains) {
      if (!Chain.Nodes.empty()) {
        SortedChains.push_back(&Chain);
        // Using doubles to avoid overflow of ExecutionCounts.
        double Size = 0;
        double ExecutionCount = 0;
        for (NodeT *Node : Chain.Nodes) {
          Size += static_cast<double>(Node->Size);
          ExecutionCount += static_cast<double>(Node->ExecutionCount);
        }
        assert(Size > 0 && "a chain of zero size");
        ChainDensity[&Chain] = ExecutionCount / Size;
      }
    }

    // Sort chains by density in the decreasing order.
    std::sort(SortedChains.begin(), SortedChains.end(),
              [&](const ChainT *L, const ChainT *R) {
                const double DL = ChainDensity[L];
                const double DR = ChainDensity[R];
                // Compare by density and break ties by chain identifiers.
                return std::make_tuple(-DL, L->Id) <
                       std::make_tuple(-DR, R->Id);
              });

    // Collect the nodes in the order specified by their chains.
    std::vector<uint64_t> Order;
    Order.reserve(NumNodes);
    for (const ChainT *Chain : SortedChains)
      for (NodeT *Node : Chain->Nodes)
        Order.push_back(Node->Index);
    return Order;
  }

private:
  /// Config for the algorithm.
  const CDSortConfig Config;

  /// The number of nodes in the graph.
  const size_t NumNodes;

  /// Successors of each node.
  std::vector<std::vector<uint64_t>> SuccNodes;

  /// Predecessors of each node.
  std::vector<std::vector<uint64_t>> PredNodes;

  /// All nodes (functions) in the graph.
  std::vector<NodeT> AllNodes;

  /// All jumps (function calls) between the nodes.
  std::vector<JumpT> AllJumps;

  /// All chains of nodes.
  std::vector<ChainT> AllChains;

  /// All edges between the chains.
  std::vector<ChainEdge> AllEdges;

  /// The total number of samples in the graph.
  uint64_t TotalSamples{0};

  /// The total size of the nodes in the graph.
  uint64_t TotalSize{0};
};

} // end of anonymous namespace

std::vector<uint64_t>
codelayout::computeExtTspLayout(ArrayRef<uint64_t> NodeSizes,
                                ArrayRef<uint64_t> NodeCounts,
                                ArrayRef<EdgeCount> EdgeCounts) {
  // Verify correctness of the input data.
  assert(NodeCounts.size() == NodeSizes.size() && "Incorrect input");
  assert(NodeSizes.size() > 2 && "Incorrect input");

  // Apply the reordering algorithm.
  ExtTSPImpl Alg(NodeSizes, NodeCounts, EdgeCounts);
  std::vector<uint64_t> Result = Alg.run();

  // Verify correctness of the output.
  assert(Result.front() == 0 && "Original entry point is not preserved");
  assert(Result.size() == NodeSizes.size() && "Incorrect size of layout");
  return Result;
}

double codelayout::calcExtTspScore(ArrayRef<uint64_t> Order,
                                   ArrayRef<uint64_t> NodeSizes,
                                   ArrayRef<uint64_t> NodeCounts,
                                   ArrayRef<EdgeCount> EdgeCounts) {
  // Estimate addresses of the blocks in memory.
  std::vector<uint64_t> Addr(NodeSizes.size(), 0);
  for (size_t Idx = 1; Idx < Order.size(); Idx++) {
    Addr[Order[Idx]] = Addr[Order[Idx - 1]] + NodeSizes[Order[Idx - 1]];
  }
  std::vector<uint64_t> OutDegree(NodeSizes.size(), 0);
  for (auto Edge : EdgeCounts)
    ++OutDegree[Edge.src];

  // Increase the score for each jump.
  double Score = 0;
  for (auto Edge : EdgeCounts) {
    bool IsConditional = OutDegree[Edge.src] > 1;
    Score += ::extTSPScore(Addr[Edge.src], NodeSizes[Edge.src], Addr[Edge.dst],
                           Edge.count, IsConditional);
  }
  return Score;
}

double codelayout::calcExtTspScore(ArrayRef<uint64_t> NodeSizes,
                                   ArrayRef<uint64_t> NodeCounts,
                                   ArrayRef<EdgeCount> EdgeCounts) {
  std::vector<uint64_t> Order(NodeSizes.size());
  for (size_t Idx = 0; Idx < NodeSizes.size(); Idx++) {
    Order[Idx] = Idx;
  }
  return calcExtTspScore(Order, NodeSizes, NodeCounts, EdgeCounts);
}

std::vector<uint64_t> codelayout::computeCacheDirectedLayout(
    const CDSortConfig &Config, ArrayRef<uint64_t> FuncSizes,
    ArrayRef<uint64_t> FuncCounts, ArrayRef<EdgeCount> CallCounts,
    ArrayRef<uint64_t> CallOffsets) {
  // Verify correctness of the input data.
  assert(FuncCounts.size() == FuncSizes.size() && "Incorrect input");

  // Apply the reordering algorithm.
  CDSortImpl Alg(Config, FuncSizes, FuncCounts, CallCounts, CallOffsets);
  std::vector<uint64_t> Result = Alg.run();
  assert(Result.size() == FuncSizes.size() && "Incorrect size of layout");
  return Result;
}

std::vector<uint64_t> codelayout::computeCacheDirectedLayout(
    ArrayRef<uint64_t> FuncSizes, ArrayRef<uint64_t> FuncCounts,
    ArrayRef<EdgeCount> CallCounts, ArrayRef<uint64_t> CallOffsets) {
  CDSortConfig Config;
  // Populate the config from the command-line options.
  if (CacheEntries.getNumOccurrences() > 0)
    Config.CacheEntries = CacheEntries;
  if (CacheSize.getNumOccurrences() > 0)
    Config.CacheSize = CacheSize;
  if (CDMaxChainSize.getNumOccurrences() > 0)
    Config.MaxChainSize = CDMaxChainSize;
  if (DistancePower.getNumOccurrences() > 0)
    Config.DistancePower = DistancePower;
  if (FrequencyScale.getNumOccurrences() > 0)
    Config.FrequencyScale = FrequencyScale;
  return computeCacheDirectedLayout(Config, FuncSizes, FuncCounts, CallCounts,
                                    CallOffsets);
}
