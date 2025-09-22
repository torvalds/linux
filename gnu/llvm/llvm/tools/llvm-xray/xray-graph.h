//===-- xray-graph.h - XRay Function Call Graph Renderer --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generate a DOT file to represent the function call graph encountered in
// the trace.
//
//===----------------------------------------------------------------------===//

#ifndef XRAY_GRAPH_H
#define XRAY_GRAPH_H

#include <string>
#include <vector>

#include "func-id-helper.h"
#include "xray-color-helper.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/Graph.h"
#include "llvm/XRay/Trace.h"
#include "llvm/XRay/XRayRecord.h"

namespace llvm {
namespace xray {

/// A class encapsulating the logic related to analyzing XRay traces, producting
/// Graphs from them and then exporting those graphs for review.
class GraphRenderer {
public:
  /// An enum for enumerating the various statistics gathered on latencies
  enum class StatType { NONE, COUNT, MIN, MED, PCT90, PCT99, MAX, SUM };

  /// An inner struct for common timing statistics information
  struct TimeStat {
    int64_t Count;
    double Min;
    double Median;
    double Pct90;
    double Pct99;
    double Max;
    double Sum;

    std::string getString(StatType T) const;
    double getDouble(StatType T) const;
  };
  using TimestampT = uint64_t;

  /// An inner struct for storing edge attributes for our graph. Here the
  /// attributes are mainly function call statistics.
  ///
  /// FIXME: expand to contain more information eg call latencies.
  struct CallStats {
    TimeStat S;
    std::vector<TimestampT> Timings;
  };

  /// An Inner Struct for storing vertex attributes, at the moment just
  /// SymbolNames, however in future we could store bulk function statistics.
  ///
  /// FIXME: Store more attributes based on instrumentation map.
  struct FunctionStats {
    std::string SymbolName;
    TimeStat S = {};
  };

  struct FunctionAttr {
    int32_t FuncId;
    uint64_t TSC;
  };

  using FunctionStack = SmallVector<FunctionAttr, 4>;

  using PerThreadFunctionStackMap = DenseMap<uint32_t, FunctionStack>;

  class GraphT : public Graph<FunctionStats, CallStats, int32_t> {
  public:
    TimeStat GraphEdgeMax = {};
    TimeStat GraphVertexMax = {};
  };

  GraphT G;
  using VertexIdentifier = typename decltype(G)::VertexIdentifier;
  using EdgeIdentifier = decltype(G)::EdgeIdentifier;

  /// Use a Map to store the Function stack for each thread whilst building the
  /// graph.
  ///
  /// FIXME: Perhaps we can Build this into LatencyAccountant? or vise versa?
  PerThreadFunctionStackMap PerThreadFunctionStack;

  /// Usefull object for getting human readable Symbol Names.
  FuncIdConversionHelper FuncIdHelper;
  bool DeduceSiblingCalls = false;
  TimestampT CurrentMaxTSC = 0;

  /// A private function to help implement the statistic generation functions;
  template <typename U>
  void getStats(U begin, U end, GraphRenderer::TimeStat &S);
  void updateMaxStats(const TimeStat &S, TimeStat &M);

  /// Calculates latency statistics for each edge and stores the data in the
  /// Graph
  void calculateEdgeStatistics();

  /// Calculates latency statistics for each vertex and stores the data in the
  /// Graph
  void calculateVertexStatistics();

  /// Normalises latency statistics for each edge and vertex by CycleFrequency;
  void normalizeStatistics(double CycleFrequency);

  /// An object to color gradients
  ColorHelper CHelper;

public:
  /// Takes in a reference to a FuncIdHelper in order to have ready access to
  /// Symbol names.
  explicit GraphRenderer(const FuncIdConversionHelper &FuncIdHelper, bool DSC)
      : FuncIdHelper(FuncIdHelper), DeduceSiblingCalls(DSC),
        CHelper(ColorHelper::SequentialScheme::OrRd) {
    G[0] = {};
  }

  /// Process an Xray record and expand the graph.
  ///
  /// This Function will return true on success, or false if records are not
  /// presented in per-thread call-tree DFS order. (That is for each thread the
  /// Records should be in order runtime on an ideal system.)
  ///
  /// FIXME: Make this more robust against small irregularities.
  Error accountRecord(const XRayRecord &Record);

  const PerThreadFunctionStackMap &getPerThreadFunctionStack() const {
    return PerThreadFunctionStack;
  }

  class Factory {
  public:
    bool KeepGoing;
    bool DeduceSiblingCalls;
    std::string InstrMap;
    ::llvm::xray::Trace Trace;
    Expected<GraphRenderer> getGraphRenderer();
  };

  /// Output the Embedded graph in DOT format on \p OS, labeling the edges by
  /// \p T
  void exportGraphAsDOT(raw_ostream &OS, StatType EdgeLabel = StatType::NONE,
                        StatType EdgeColor = StatType::NONE,
                        StatType VertexLabel = StatType::NONE,
                        StatType VertexColor = StatType::NONE);

  /// Get a reference to the internal graph.
  const GraphT &getGraph() { return G; }
};

/// Vector Sum of TimeStats
inline GraphRenderer::TimeStat operator+(const GraphRenderer::TimeStat &A,
                                         const GraphRenderer::TimeStat &B) {
  return {A.Count + B.Count, A.Min + B.Min,     A.Median + B.Median,
          A.Pct90 + B.Pct90, A.Pct99 + B.Pct99, A.Max + B.Max,
          A.Sum + B.Sum};
}

/// Vector Difference of Timestats
inline GraphRenderer::TimeStat operator-(const GraphRenderer::TimeStat &A,
                                         const GraphRenderer::TimeStat &B) {

  return {A.Count - B.Count, A.Min - B.Min,     A.Median - B.Median,
          A.Pct90 - B.Pct90, A.Pct99 - B.Pct99, A.Max - B.Max,
          A.Sum - B.Sum};
}

/// Scalar Diference of TimeStat and double
inline GraphRenderer::TimeStat operator/(const GraphRenderer::TimeStat &A,
                                         double B) {

  return {static_cast<int64_t>(A.Count / B),
          A.Min / B,
          A.Median / B,
          A.Pct90 / B,
          A.Pct99 / B,
          A.Max / B,
          A.Sum / B};
}

/// Scalar product of TimeStat and Double
inline GraphRenderer::TimeStat operator*(const GraphRenderer::TimeStat &A,
                                         double B) {
  return {static_cast<int64_t>(A.Count * B),
          A.Min * B,
          A.Median * B,
          A.Pct90 * B,
          A.Pct99 * B,
          A.Max * B,
          A.Sum * B};
}

/// Scalar product of double TimeStat
inline GraphRenderer::TimeStat operator*(double A,
                                         const GraphRenderer::TimeStat &B) {
  return B * A;
}

/// Hadamard Product of TimeStats
inline GraphRenderer::TimeStat operator*(const GraphRenderer::TimeStat &A,
                                         const GraphRenderer::TimeStat &B) {
  return {A.Count * B.Count, A.Min * B.Min,     A.Median * B.Median,
          A.Pct90 * B.Pct90, A.Pct99 * B.Pct99, A.Max * B.Max,
          A.Sum * B.Sum};
}

/// Hadamard Division of TimeStats
inline GraphRenderer::TimeStat operator/(const GraphRenderer::TimeStat &A,
                                         const GraphRenderer::TimeStat &B) {
  return {A.Count / B.Count, A.Min / B.Min,     A.Median / B.Median,
          A.Pct90 / B.Pct90, A.Pct99 / B.Pct99, A.Max / B.Max,
          A.Sum / B.Sum};
}
} // namespace xray
} // namespace llvm

#endif // XRAY_GRAPH_H
