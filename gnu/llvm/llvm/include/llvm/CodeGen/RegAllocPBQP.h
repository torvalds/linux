//===- RegAllocPBQP.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the PBQPBuilder interface, for classes which build PBQP
// instances to represent register allocation problems, and the RegAllocPBQP
// interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCPBQP_H
#define LLVM_CODEGEN_REGALLOCPBQP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/CodeGen/PBQP/CostAllocator.h"
#include "llvm/CodeGen/PBQP/Graph.h"
#include "llvm/CodeGen/PBQP/Math.h"
#include "llvm/CodeGen/PBQP/ReductionRules.h"
#include "llvm/CodeGen/PBQP/Solution.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <set>
#include <vector>

namespace llvm {

class FunctionPass;
class LiveIntervals;
class MachineBlockFrequencyInfo;
class MachineFunction;
class raw_ostream;

namespace PBQP {
namespace RegAlloc {

/// Spill option index.
inline unsigned getSpillOptionIdx() { return 0; }

/// Metadata to speed allocatability test.
///
/// Keeps track of the number of infinities in each row and column.
class MatrixMetadata {
public:
  MatrixMetadata(const Matrix& M)
    : UnsafeRows(new bool[M.getRows() - 1]()),
      UnsafeCols(new bool[M.getCols() - 1]()) {
    unsigned* ColCounts = new unsigned[M.getCols() - 1]();

    for (unsigned i = 1; i < M.getRows(); ++i) {
      unsigned RowCount = 0;
      for (unsigned j = 1; j < M.getCols(); ++j) {
        if (M[i][j] == std::numeric_limits<PBQPNum>::infinity()) {
          ++RowCount;
          ++ColCounts[j - 1];
          UnsafeRows[i - 1] = true;
          UnsafeCols[j - 1] = true;
        }
      }
      WorstRow = std::max(WorstRow, RowCount);
    }
    unsigned WorstColCountForCurRow =
      *std::max_element(ColCounts, ColCounts + M.getCols() - 1);
    WorstCol = std::max(WorstCol, WorstColCountForCurRow);
    delete[] ColCounts;
  }

  MatrixMetadata(const MatrixMetadata &) = delete;
  MatrixMetadata &operator=(const MatrixMetadata &) = delete;

  unsigned getWorstRow() const { return WorstRow; }
  unsigned getWorstCol() const { return WorstCol; }
  const bool* getUnsafeRows() const { return UnsafeRows.get(); }
  const bool* getUnsafeCols() const { return UnsafeCols.get(); }

private:
  unsigned WorstRow = 0;
  unsigned WorstCol = 0;
  std::unique_ptr<bool[]> UnsafeRows;
  std::unique_ptr<bool[]> UnsafeCols;
};

/// Holds a vector of the allowed physical regs for a vreg.
class AllowedRegVector {
  friend hash_code hash_value(const AllowedRegVector &);

public:
  AllowedRegVector() = default;
  AllowedRegVector(AllowedRegVector &&) = default;

  AllowedRegVector(const std::vector<MCRegister> &OptVec)
      : NumOpts(OptVec.size()), Opts(new MCRegister[NumOpts]) {
    std::copy(OptVec.begin(), OptVec.end(), Opts.get());
  }

  unsigned size() const { return NumOpts; }
  MCRegister operator[](size_t I) const { return Opts[I]; }

  bool operator==(const AllowedRegVector &Other) const {
    if (NumOpts != Other.NumOpts)
      return false;
    return std::equal(Opts.get(), Opts.get() + NumOpts, Other.Opts.get());
  }

  bool operator!=(const AllowedRegVector &Other) const {
    return !(*this == Other);
  }

private:
  unsigned NumOpts = 0;
  std::unique_ptr<MCRegister[]> Opts;
};

inline hash_code hash_value(const AllowedRegVector &OptRegs) {
  MCRegister *OStart = OptRegs.Opts.get();
  MCRegister *OEnd = OptRegs.Opts.get() + OptRegs.NumOpts;
  return hash_combine(OptRegs.NumOpts,
                      hash_combine_range(OStart, OEnd));
}

/// Holds graph-level metadata relevant to PBQP RA problems.
class GraphMetadata {
private:
  using AllowedRegVecPool = ValuePool<AllowedRegVector>;

public:
  using AllowedRegVecRef = AllowedRegVecPool::PoolRef;

  GraphMetadata(MachineFunction &MF,
                LiveIntervals &LIS,
                MachineBlockFrequencyInfo &MBFI)
    : MF(MF), LIS(LIS), MBFI(MBFI) {}

  MachineFunction &MF;
  LiveIntervals &LIS;
  MachineBlockFrequencyInfo &MBFI;

  void setNodeIdForVReg(Register VReg, GraphBase::NodeId NId) {
    VRegToNodeId[VReg.id()] = NId;
  }

  GraphBase::NodeId getNodeIdForVReg(Register VReg) const {
    auto VRegItr = VRegToNodeId.find(VReg);
    if (VRegItr == VRegToNodeId.end())
      return GraphBase::invalidNodeId();
    return VRegItr->second;
  }

  AllowedRegVecRef getAllowedRegs(AllowedRegVector Allowed) {
    return AllowedRegVecs.getValue(std::move(Allowed));
  }

private:
  DenseMap<Register, GraphBase::NodeId> VRegToNodeId;
  AllowedRegVecPool AllowedRegVecs;
};

/// Holds solver state and other metadata relevant to each PBQP RA node.
class NodeMetadata {
public:
  using AllowedRegVector = RegAlloc::AllowedRegVector;

  // The node's reduction state. The order in this enum is important,
  // as it is assumed nodes can only progress up (i.e. towards being
  // optimally reducible) when reducing the graph.
  using ReductionState = enum {
    Unprocessed,
    NotProvablyAllocatable,
    ConservativelyAllocatable,
    OptimallyReducible
  };

  NodeMetadata() = default;

  NodeMetadata(const NodeMetadata &Other)
      : RS(Other.RS), NumOpts(Other.NumOpts), DeniedOpts(Other.DeniedOpts),
        OptUnsafeEdges(new unsigned[NumOpts]), VReg(Other.VReg),
        AllowedRegs(Other.AllowedRegs)
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
        ,
        everConservativelyAllocatable(Other.everConservativelyAllocatable)
#endif
  {
    if (NumOpts > 0) {
      std::copy(&Other.OptUnsafeEdges[0], &Other.OptUnsafeEdges[NumOpts],
                &OptUnsafeEdges[0]);
    }
  }

  NodeMetadata(NodeMetadata &&) = default;
  NodeMetadata& operator=(NodeMetadata &&) = default;

  void setVReg(Register VReg) { this->VReg = VReg; }
  Register getVReg() const { return VReg; }

  void setAllowedRegs(GraphMetadata::AllowedRegVecRef AllowedRegs) {
    this->AllowedRegs = std::move(AllowedRegs);
  }
  const AllowedRegVector& getAllowedRegs() const { return *AllowedRegs; }

  void setup(const Vector& Costs) {
    NumOpts = Costs.getLength() - 1;
    OptUnsafeEdges = std::unique_ptr<unsigned[]>(new unsigned[NumOpts]());
  }

  ReductionState getReductionState() const { return RS; }
  void setReductionState(ReductionState RS) {
    assert(RS >= this->RS && "A node's reduction state can not be downgraded");
    this->RS = RS;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    // Remember this state to assert later that a non-infinite register
    // option was available.
    if (RS == ConservativelyAllocatable)
      everConservativelyAllocatable = true;
#endif
  }

  void handleAddEdge(const MatrixMetadata& MD, bool Transpose) {
    DeniedOpts += Transpose ? MD.getWorstRow() : MD.getWorstCol();
    const bool* UnsafeOpts =
      Transpose ? MD.getUnsafeCols() : MD.getUnsafeRows();
    for (unsigned i = 0; i < NumOpts; ++i)
      OptUnsafeEdges[i] += UnsafeOpts[i];
  }

  void handleRemoveEdge(const MatrixMetadata& MD, bool Transpose) {
    DeniedOpts -= Transpose ? MD.getWorstRow() : MD.getWorstCol();
    const bool* UnsafeOpts =
      Transpose ? MD.getUnsafeCols() : MD.getUnsafeRows();
    for (unsigned i = 0; i < NumOpts; ++i)
      OptUnsafeEdges[i] -= UnsafeOpts[i];
  }

  bool isConservativelyAllocatable() const {
    return (DeniedOpts < NumOpts) ||
      (std::find(&OptUnsafeEdges[0], &OptUnsafeEdges[NumOpts], 0) !=
       &OptUnsafeEdges[NumOpts]);
  }

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  bool wasConservativelyAllocatable() const {
    return everConservativelyAllocatable;
  }
#endif

private:
  ReductionState RS = Unprocessed;
  unsigned NumOpts = 0;
  unsigned DeniedOpts = 0;
  std::unique_ptr<unsigned[]> OptUnsafeEdges;
  Register VReg;
  GraphMetadata::AllowedRegVecRef AllowedRegs;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  bool everConservativelyAllocatable = false;
#endif
};

class RegAllocSolverImpl {
private:
  using RAMatrix = MDMatrix<MatrixMetadata>;

public:
  using RawVector = PBQP::Vector;
  using RawMatrix = PBQP::Matrix;
  using Vector = PBQP::Vector;
  using Matrix = RAMatrix;
  using CostAllocator = PBQP::PoolCostAllocator<Vector, Matrix>;

  using NodeId = GraphBase::NodeId;
  using EdgeId = GraphBase::EdgeId;

  using NodeMetadata = RegAlloc::NodeMetadata;
  struct EdgeMetadata {};
  using GraphMetadata = RegAlloc::GraphMetadata;

  using Graph = PBQP::Graph<RegAllocSolverImpl>;

  RegAllocSolverImpl(Graph &G) : G(G) {}

  Solution solve() {
    G.setSolver(*this);
    Solution S;
    setup();
    S = backpropagate(G, reduce());
    G.unsetSolver();
    return S;
  }

  void handleAddNode(NodeId NId) {
    assert(G.getNodeCosts(NId).getLength() > 1 &&
           "PBQP Graph should not contain single or zero-option nodes");
    G.getNodeMetadata(NId).setup(G.getNodeCosts(NId));
  }

  void handleRemoveNode(NodeId NId) {}
  void handleSetNodeCosts(NodeId NId, const Vector& newCosts) {}

  void handleAddEdge(EdgeId EId) {
    handleReconnectEdge(EId, G.getEdgeNode1Id(EId));
    handleReconnectEdge(EId, G.getEdgeNode2Id(EId));
  }

  void handleDisconnectEdge(EdgeId EId, NodeId NId) {
    NodeMetadata& NMd = G.getNodeMetadata(NId);
    const MatrixMetadata& MMd = G.getEdgeCosts(EId).getMetadata();
    NMd.handleRemoveEdge(MMd, NId == G.getEdgeNode2Id(EId));
    promote(NId, NMd);
  }

  void handleReconnectEdge(EdgeId EId, NodeId NId) {
    NodeMetadata& NMd = G.getNodeMetadata(NId);
    const MatrixMetadata& MMd = G.getEdgeCosts(EId).getMetadata();
    NMd.handleAddEdge(MMd, NId == G.getEdgeNode2Id(EId));
  }

  void handleUpdateCosts(EdgeId EId, const Matrix& NewCosts) {
    NodeId N1Id = G.getEdgeNode1Id(EId);
    NodeId N2Id = G.getEdgeNode2Id(EId);
    NodeMetadata& N1Md = G.getNodeMetadata(N1Id);
    NodeMetadata& N2Md = G.getNodeMetadata(N2Id);
    bool Transpose = N1Id != G.getEdgeNode1Id(EId);

    // Metadata are computed incrementally. First, update them
    // by removing the old cost.
    const MatrixMetadata& OldMMd = G.getEdgeCosts(EId).getMetadata();
    N1Md.handleRemoveEdge(OldMMd, Transpose);
    N2Md.handleRemoveEdge(OldMMd, !Transpose);

    // And update now the metadata with the new cost.
    const MatrixMetadata& MMd = NewCosts.getMetadata();
    N1Md.handleAddEdge(MMd, Transpose);
    N2Md.handleAddEdge(MMd, !Transpose);

    // As the metadata may have changed with the update, the nodes may have
    // become ConservativelyAllocatable or OptimallyReducible.
    promote(N1Id, N1Md);
    promote(N2Id, N2Md);
  }

private:
  void promote(NodeId NId, NodeMetadata& NMd) {
    if (G.getNodeDegree(NId) == 3) {
      // This node is becoming optimally reducible.
      moveToOptimallyReducibleNodes(NId);
    } else if (NMd.getReductionState() ==
               NodeMetadata::NotProvablyAllocatable &&
               NMd.isConservativelyAllocatable()) {
      // This node just became conservatively allocatable.
      moveToConservativelyAllocatableNodes(NId);
    }
  }

  void removeFromCurrentSet(NodeId NId) {
    switch (G.getNodeMetadata(NId).getReductionState()) {
    case NodeMetadata::Unprocessed: break;
    case NodeMetadata::OptimallyReducible:
      assert(OptimallyReducibleNodes.find(NId) !=
             OptimallyReducibleNodes.end() &&
             "Node not in optimally reducible set.");
      OptimallyReducibleNodes.erase(NId);
      break;
    case NodeMetadata::ConservativelyAllocatable:
      assert(ConservativelyAllocatableNodes.find(NId) !=
             ConservativelyAllocatableNodes.end() &&
             "Node not in conservatively allocatable set.");
      ConservativelyAllocatableNodes.erase(NId);
      break;
    case NodeMetadata::NotProvablyAllocatable:
      assert(NotProvablyAllocatableNodes.find(NId) !=
             NotProvablyAllocatableNodes.end() &&
             "Node not in not-provably-allocatable set.");
      NotProvablyAllocatableNodes.erase(NId);
      break;
    }
  }

  void moveToOptimallyReducibleNodes(NodeId NId) {
    removeFromCurrentSet(NId);
    OptimallyReducibleNodes.insert(NId);
    G.getNodeMetadata(NId).setReductionState(
      NodeMetadata::OptimallyReducible);
  }

  void moveToConservativelyAllocatableNodes(NodeId NId) {
    removeFromCurrentSet(NId);
    ConservativelyAllocatableNodes.insert(NId);
    G.getNodeMetadata(NId).setReductionState(
      NodeMetadata::ConservativelyAllocatable);
  }

  void moveToNotProvablyAllocatableNodes(NodeId NId) {
    removeFromCurrentSet(NId);
    NotProvablyAllocatableNodes.insert(NId);
    G.getNodeMetadata(NId).setReductionState(
      NodeMetadata::NotProvablyAllocatable);
  }

  void setup() {
    // Set up worklists.
    for (auto NId : G.nodeIds()) {
      if (G.getNodeDegree(NId) < 3)
        moveToOptimallyReducibleNodes(NId);
      else if (G.getNodeMetadata(NId).isConservativelyAllocatable())
        moveToConservativelyAllocatableNodes(NId);
      else
        moveToNotProvablyAllocatableNodes(NId);
    }
  }

  // Compute a reduction order for the graph by iteratively applying PBQP
  // reduction rules. Locally optimal rules are applied whenever possible (R0,
  // R1, R2). If no locally-optimal rules apply then any conservatively
  // allocatable node is reduced. Finally, if no conservatively allocatable
  // node exists then the node with the lowest spill-cost:degree ratio is
  // selected.
  std::vector<GraphBase::NodeId> reduce() {
    assert(!G.empty() && "Cannot reduce empty graph.");

    using NodeId = GraphBase::NodeId;
    std::vector<NodeId> NodeStack;

    // Consume worklists.
    while (true) {
      if (!OptimallyReducibleNodes.empty()) {
        NodeSet::iterator NItr = OptimallyReducibleNodes.begin();
        NodeId NId = *NItr;
        OptimallyReducibleNodes.erase(NItr);
        NodeStack.push_back(NId);
        switch (G.getNodeDegree(NId)) {
        case 0:
          break;
        case 1:
          applyR1(G, NId);
          break;
        case 2:
          applyR2(G, NId);
          break;
        default: llvm_unreachable("Not an optimally reducible node.");
        }
      } else if (!ConservativelyAllocatableNodes.empty()) {
        // Conservatively allocatable nodes will never spill. For now just
        // take the first node in the set and push it on the stack. When we
        // start optimizing more heavily for register preferencing, it may
        // would be better to push nodes with lower 'expected' or worst-case
        // register costs first (since early nodes are the most
        // constrained).
        NodeSet::iterator NItr = ConservativelyAllocatableNodes.begin();
        NodeId NId = *NItr;
        ConservativelyAllocatableNodes.erase(NItr);
        NodeStack.push_back(NId);
        G.disconnectAllNeighborsFromNode(NId);
      } else if (!NotProvablyAllocatableNodes.empty()) {
        NodeSet::iterator NItr = llvm::min_element(NotProvablyAllocatableNodes,
                                                   SpillCostComparator(G));
        NodeId NId = *NItr;
        NotProvablyAllocatableNodes.erase(NItr);
        NodeStack.push_back(NId);
        G.disconnectAllNeighborsFromNode(NId);
      } else
        break;
    }

    return NodeStack;
  }

  class SpillCostComparator {
  public:
    SpillCostComparator(const Graph& G) : G(G) {}

    bool operator()(NodeId N1Id, NodeId N2Id) {
      PBQPNum N1SC = G.getNodeCosts(N1Id)[0];
      PBQPNum N2SC = G.getNodeCosts(N2Id)[0];
      if (N1SC == N2SC)
        return G.getNodeDegree(N1Id) < G.getNodeDegree(N2Id);
      return N1SC < N2SC;
    }

  private:
    const Graph& G;
  };

  Graph& G;
  using NodeSet = std::set<NodeId>;
  NodeSet OptimallyReducibleNodes;
  NodeSet ConservativelyAllocatableNodes;
  NodeSet NotProvablyAllocatableNodes;
};

class PBQPRAGraph : public PBQP::Graph<RegAllocSolverImpl> {
private:
  using BaseT = PBQP::Graph<RegAllocSolverImpl>;

public:
  PBQPRAGraph(GraphMetadata Metadata) : BaseT(std::move(Metadata)) {}

  /// Dump this graph to dbgs().
  void dump() const;

  /// Dump this graph to an output stream.
  /// @param OS Output stream to print on.
  void dump(raw_ostream &OS) const;

  /// Print a representation of this graph in DOT format.
  /// @param OS Output stream to print on.
  void printDot(raw_ostream &OS) const;
};

inline Solution solve(PBQPRAGraph& G) {
  if (G.empty())
    return Solution();
  RegAllocSolverImpl RegAllocSolver(G);
  return RegAllocSolver.solve();
}

} // end namespace RegAlloc
} // end namespace PBQP

/// Create a PBQP register allocator instance.
FunctionPass *
createPBQPRegisterAllocator(char *customPassID = nullptr);

} // end namespace llvm

#endif // LLVM_CODEGEN_REGALLOCPBQP_H
