//===- ReductionRules.h - Reduction Rules -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Reduction Rules.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_PBQP_REDUCTIONRULES_H
#define LLVM_CODEGEN_PBQP_REDUCTIONRULES_H

#include "Graph.h"
#include "Math.h"
#include "Solution.h"
#include <cassert>
#include <limits>

namespace llvm {
namespace PBQP {

  /// Reduce a node of degree one.
  ///
  /// Propagate costs from the given node, which must be of degree one, to its
  /// neighbor. Notify the problem domain.
  template <typename GraphT>
  void applyR1(GraphT &G, typename GraphT::NodeId NId) {
    using NodeId = typename GraphT::NodeId;
    using EdgeId = typename GraphT::EdgeId;
    using Vector = typename GraphT::Vector;
    using Matrix = typename GraphT::Matrix;
    using RawVector = typename GraphT::RawVector;

    assert(G.getNodeDegree(NId) == 1 &&
           "R1 applied to node with degree != 1.");

    EdgeId EId = *G.adjEdgeIds(NId).begin();
    NodeId MId = G.getEdgeOtherNodeId(EId, NId);

    const Matrix &ECosts = G.getEdgeCosts(EId);
    const Vector &XCosts = G.getNodeCosts(NId);
    RawVector YCosts = G.getNodeCosts(MId);

    // Duplicate a little to avoid transposing matrices.
    if (NId == G.getEdgeNode1Id(EId)) {
      for (unsigned j = 0; j < YCosts.getLength(); ++j) {
        PBQPNum Min = ECosts[0][j] + XCosts[0];
        for (unsigned i = 1; i < XCosts.getLength(); ++i) {
          PBQPNum C = ECosts[i][j] + XCosts[i];
          if (C < Min)
            Min = C;
        }
        YCosts[j] += Min;
      }
    } else {
      for (unsigned i = 0; i < YCosts.getLength(); ++i) {
        PBQPNum Min = ECosts[i][0] + XCosts[0];
        for (unsigned j = 1; j < XCosts.getLength(); ++j) {
          PBQPNum C = ECosts[i][j] + XCosts[j];
          if (C < Min)
            Min = C;
        }
        YCosts[i] += Min;
      }
    }
    G.setNodeCosts(MId, YCosts);
    G.disconnectEdge(EId, MId);
  }

  template <typename GraphT>
  void applyR2(GraphT &G, typename GraphT::NodeId NId) {
    using NodeId = typename GraphT::NodeId;
    using EdgeId = typename GraphT::EdgeId;
    using Vector = typename GraphT::Vector;
    using Matrix = typename GraphT::Matrix;
    using RawMatrix = typename GraphT::RawMatrix;

    assert(G.getNodeDegree(NId) == 2 &&
           "R2 applied to node with degree != 2.");

    const Vector &XCosts = G.getNodeCosts(NId);

    typename GraphT::AdjEdgeItr AEItr = G.adjEdgeIds(NId).begin();
    EdgeId YXEId = *AEItr,
           ZXEId = *(++AEItr);

    NodeId YNId = G.getEdgeOtherNodeId(YXEId, NId),
           ZNId = G.getEdgeOtherNodeId(ZXEId, NId);

    bool FlipEdge1 = (G.getEdgeNode1Id(YXEId) == NId),
         FlipEdge2 = (G.getEdgeNode1Id(ZXEId) == NId);

    const Matrix *YXECosts = FlipEdge1 ?
      new Matrix(G.getEdgeCosts(YXEId).transpose()) :
      &G.getEdgeCosts(YXEId);

    const Matrix *ZXECosts = FlipEdge2 ?
      new Matrix(G.getEdgeCosts(ZXEId).transpose()) :
      &G.getEdgeCosts(ZXEId);

    unsigned XLen = XCosts.getLength(),
      YLen = YXECosts->getRows(),
      ZLen = ZXECosts->getRows();

    RawMatrix Delta(YLen, ZLen);

    for (unsigned i = 0; i < YLen; ++i) {
      for (unsigned j = 0; j < ZLen; ++j) {
        PBQPNum Min = (*YXECosts)[i][0] + (*ZXECosts)[j][0] + XCosts[0];
        for (unsigned k = 1; k < XLen; ++k) {
          PBQPNum C = (*YXECosts)[i][k] + (*ZXECosts)[j][k] + XCosts[k];
          if (C < Min) {
            Min = C;
          }
        }
        Delta[i][j] = Min;
      }
    }

    if (FlipEdge1)
      delete YXECosts;

    if (FlipEdge2)
      delete ZXECosts;

    EdgeId YZEId = G.findEdge(YNId, ZNId);

    if (YZEId == G.invalidEdgeId()) {
      YZEId = G.addEdge(YNId, ZNId, Delta);
    } else {
      const Matrix &YZECosts = G.getEdgeCosts(YZEId);
      if (YNId == G.getEdgeNode1Id(YZEId)) {
        G.updateEdgeCosts(YZEId, Delta + YZECosts);
      } else {
        G.updateEdgeCosts(YZEId, Delta.transpose() + YZECosts);
      }
    }

    G.disconnectEdge(YXEId, YNId);
    G.disconnectEdge(ZXEId, ZNId);

    // TODO: Try to normalize newly added/modified edge.
  }

#ifndef NDEBUG
  // Does this Cost vector have any register options ?
  template <typename VectorT>
  bool hasRegisterOptions(const VectorT &V) {
    unsigned VL = V.getLength();

    // An empty or spill only cost vector does not provide any register option.
    if (VL <= 1)
      return false;

    // If there are registers in the cost vector, but all of them have infinite
    // costs, then ... there is no available register.
    for (unsigned i = 1; i < VL; ++i)
      if (V[i] != std::numeric_limits<PBQP::PBQPNum>::infinity())
        return true;

    return false;
  }
#endif

  // Find a solution to a fully reduced graph by backpropagation.
  //
  // Given a graph and a reduction order, pop each node from the reduction
  // order and greedily compute a minimum solution based on the node costs, and
  // the dependent costs due to previously solved nodes.
  //
  // Note - This does not return the graph to its original (pre-reduction)
  //        state: the existing solvers destructively alter the node and edge
  //        costs. Given that, the backpropagate function doesn't attempt to
  //        replace the edges either, but leaves the graph in its reduced
  //        state.
  template <typename GraphT, typename StackT>
  Solution backpropagate(GraphT& G, StackT stack) {
    using NodeId = GraphBase::NodeId;
    using Matrix = typename GraphT::Matrix;
    using RawVector = typename GraphT::RawVector;

    Solution s;

    while (!stack.empty()) {
      NodeId NId = stack.back();
      stack.pop_back();

      RawVector v = G.getNodeCosts(NId);

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
      // Although a conservatively allocatable node can be allocated to a register,
      // spilling it may provide a lower cost solution. Assert here that spilling
      // is done by choice, not because there were no register available.
      if (G.getNodeMetadata(NId).wasConservativelyAllocatable())
        assert(hasRegisterOptions(v) && "A conservatively allocatable node "
                                        "must have available register options");
#endif

      for (auto EId : G.adjEdgeIds(NId)) {
        const Matrix& edgeCosts = G.getEdgeCosts(EId);
        if (NId == G.getEdgeNode1Id(EId)) {
          NodeId mId = G.getEdgeNode2Id(EId);
          v += edgeCosts.getColAsVector(s.getSelection(mId));
        } else {
          NodeId mId = G.getEdgeNode1Id(EId);
          v += edgeCosts.getRowAsVector(s.getSelection(mId));
        }
      }

      s.setSelection(NId, v.minIndex());
    }

    return s;
  }

} // end namespace PBQP
} // end namespace llvm

#endif // LLVM_CODEGEN_PBQP_REDUCTIONRULES_H
