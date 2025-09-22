//===-- Analysis.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Analysis output for benchmark results.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_ANALYSIS_H
#define LLVM_TOOLS_LLVM_EXEGESIS_ANALYSIS_H

#include "Clustering.h"
#include "DisassemblerHelper.h"
#include "SchedClassResolution.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>

namespace llvm {
namespace exegesis {

// A helper class to analyze benchmark results for a target.
class Analysis {
public:
  Analysis(const LLVMState &State,
           const BenchmarkClustering &Clustering,
           double AnalysisInconsistencyEpsilon,
           bool AnalysisDisplayUnstableOpcodes);

  // Prints a csv of instructions for each cluster.
  struct PrintClusters {};
  // Find potential errors in the scheduling information given measurements.
  struct PrintSchedClassInconsistencies {};

  template <typename Pass> Error run(raw_ostream &OS) const;

private:
  using ClusterId = BenchmarkClustering::ClusterId;

  // Represents the intersection of a sched class and a cluster.
  class SchedClassCluster {
  public:
    const BenchmarkClustering::ClusterId &id() const {
      return ClusterId;
    }

    const std::vector<size_t> &getPointIds() const { return PointIds; }

    void addPoint(size_t PointId,
                  const BenchmarkClustering &Clustering);

    // Return the cluster centroid.
    const SchedClassClusterCentroid &getCentroid() const { return Centroid; }

    // Returns true if the cluster representative measurements match that of SC.
    bool
    measurementsMatch(const MCSubtargetInfo &STI, const ResolvedSchedClass &SC,
                      const BenchmarkClustering &Clustering,
                      const double AnalysisInconsistencyEpsilonSquared_) const;

  private:
    BenchmarkClustering::ClusterId ClusterId;
    std::vector<size_t> PointIds;
    // Measurement stats for the points in the SchedClassCluster.
    SchedClassClusterCentroid Centroid;
  };

  void printInstructionRowCsv(size_t PointId, raw_ostream &OS) const;

  void printClusterRawHtml(const BenchmarkClustering::ClusterId &Id,
                           StringRef display_name, raw_ostream &OS) const;

  void printPointHtml(const Benchmark &Point, raw_ostream &OS) const;

  void
  printSchedClassClustersHtml(const std::vector<SchedClassCluster> &Clusters,
                              const ResolvedSchedClass &SC,
                              raw_ostream &OS) const;
  void printSchedClassDescHtml(const ResolvedSchedClass &SC,
                               raw_ostream &OS) const;

  // A pair of (Sched Class, indices of points that belong to the sched
  // class).
  struct ResolvedSchedClassAndPoints {
    explicit ResolvedSchedClassAndPoints(ResolvedSchedClass &&RSC);

    ResolvedSchedClass RSC;
    std::vector<size_t> PointIds;
  };

  // Builds a list of ResolvedSchedClassAndPoints.
  std::vector<ResolvedSchedClassAndPoints> makePointsPerSchedClass() const;

  template <typename EscapeTag, EscapeTag Tag>
  void writeSnippet(raw_ostream &OS, ArrayRef<uint8_t> Bytes,
                    const char *Separator) const;

  const BenchmarkClustering &Clustering_;
  const LLVMState &State_;
  std::unique_ptr<DisassemblerHelper> DisasmHelper_;
  const double AnalysisInconsistencyEpsilonSquared_;
  const bool AnalysisDisplayUnstableOpcodes_;
};

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_CLUSTERING_H
