//===-- Clustering.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Clustering.h"
#include "Error.h"
#include "SchedClassResolution.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

namespace llvm {
namespace exegesis {

// The clustering problem has the following characteristics:
//  (A) - Low dimension (dimensions are typically proc resource units,
//    typically < 10).
//  (B) - Number of points : ~thousands (points are measurements of an MCInst)
//  (C) - Number of clusters: ~tens.
//  (D) - The number of clusters is not known /a priory/.
//  (E) - The amount of noise is relatively small.
// The problem is rather small. In terms of algorithms, (D) disqualifies
// k-means and makes algorithms such as DBSCAN[1] or OPTICS[2] more applicable.
//
// We've used DBSCAN here because it's simple to implement. This is a pretty
// straightforward and inefficient implementation of the pseudocode in [2].
//
// [1] https://en.wikipedia.org/wiki/DBSCAN
// [2] https://en.wikipedia.org/wiki/OPTICS_algorithm

// Finds the points at distance less than sqrt(EpsilonSquared) of Q (not
// including Q).
void BenchmarkClustering::rangeQuery(
    const size_t Q, std::vector<size_t> &Neighbors) const {
  Neighbors.clear();
  Neighbors.reserve(Points_.size() - 1); // The Q itself isn't a neighbor.
  const auto &QMeasurements = Points_[Q].Measurements;
  for (size_t P = 0, NumPoints = Points_.size(); P < NumPoints; ++P) {
    if (P == Q)
      continue;
    const auto &PMeasurements = Points_[P].Measurements;
    if (PMeasurements.empty()) // Error point.
      continue;
    if (isNeighbour(PMeasurements, QMeasurements,
                    AnalysisClusteringEpsilonSquared_)) {
      Neighbors.push_back(P);
    }
  }
}

// Given a set of points, checks that all the points are neighbours
// up to AnalysisClusteringEpsilon. This is O(2*N).
bool BenchmarkClustering::areAllNeighbours(
    ArrayRef<size_t> Pts) const {
  // First, get the centroid of this group of points. This is O(N).
  SchedClassClusterCentroid G;
  for (size_t P : Pts) {
    assert(P < Points_.size());
    ArrayRef<BenchmarkMeasure> Measurements = Points_[P].Measurements;
    if (Measurements.empty()) // Error point.
      continue;
    G.addPoint(Measurements);
  }
  const std::vector<BenchmarkMeasure> Centroid = G.getAsPoint();

  // Since we will be comparing with the centroid, we need to halve the epsilon.
  double AnalysisClusteringEpsilonHalvedSquared =
      AnalysisClusteringEpsilonSquared_ / 4.0;

  // And now check that every point is a neighbour of the centroid. Also O(N).
  return all_of(
      Pts, [this, &Centroid, AnalysisClusteringEpsilonHalvedSquared](size_t P) {
        assert(P < Points_.size());
        const auto &PMeasurements = Points_[P].Measurements;
        if (PMeasurements.empty()) // Error point.
          return true;             // Pretend that error point is a neighbour.
        return isNeighbour(PMeasurements, Centroid,
                           AnalysisClusteringEpsilonHalvedSquared);
      });
}

BenchmarkClustering::BenchmarkClustering(
    const std::vector<Benchmark> &Points,
    const double AnalysisClusteringEpsilonSquared)
    : Points_(Points),
      AnalysisClusteringEpsilonSquared_(AnalysisClusteringEpsilonSquared),
      NoiseCluster_(ClusterId::noise()), ErrorCluster_(ClusterId::error()) {}

Error BenchmarkClustering::validateAndSetup() {
  ClusterIdForPoint_.resize(Points_.size());
  // Mark erroneous measurements out.
  // All points must have the same number of dimensions, in the same order.
  const std::vector<BenchmarkMeasure> *LastMeasurement = nullptr;
  for (size_t P = 0, NumPoints = Points_.size(); P < NumPoints; ++P) {
    const auto &Point = Points_[P];
    if (!Point.Error.empty()) {
      ClusterIdForPoint_[P] = ClusterId::error();
      ErrorCluster_.PointIndices.push_back(P);
      continue;
    }
    const auto *CurMeasurement = &Point.Measurements;
    if (LastMeasurement) {
      if (LastMeasurement->size() != CurMeasurement->size()) {
        return make_error<ClusteringError>(
            "inconsistent measurement dimensions");
      }
      for (size_t I = 0, E = LastMeasurement->size(); I < E; ++I) {
        if (LastMeasurement->at(I).Key != CurMeasurement->at(I).Key) {
          return make_error<ClusteringError>(
              "inconsistent measurement dimensions keys");
        }
      }
    }
    LastMeasurement = CurMeasurement;
  }
  if (LastMeasurement) {
    NumDimensions_ = LastMeasurement->size();
  }
  return Error::success();
}

void BenchmarkClustering::clusterizeDbScan(const size_t MinPts) {
  std::vector<size_t> Neighbors; // Persistent buffer to avoid allocs.
  for (size_t P = 0, NumPoints = Points_.size(); P < NumPoints; ++P) {
    if (!ClusterIdForPoint_[P].isUndef())
      continue; // Previously processed in inner loop.
    rangeQuery(P, Neighbors);
    if (Neighbors.size() + 1 < MinPts) { // Density check.
      // The region around P is not dense enough to create a new cluster, mark
      // as noise for now.
      ClusterIdForPoint_[P] = ClusterId::noise();
      continue;
    }

    // Create a new cluster, add P.
    Clusters_.emplace_back(ClusterId::makeValid(Clusters_.size()));
    Cluster &CurrentCluster = Clusters_.back();
    ClusterIdForPoint_[P] = CurrentCluster.Id; /* Label initial point */
    CurrentCluster.PointIndices.push_back(P);

    // Process P's neighbors.
    SetVector<size_t, std::deque<size_t>> ToProcess;
    ToProcess.insert(Neighbors.begin(), Neighbors.end());
    while (!ToProcess.empty()) {
      // Retrieve a point from the set.
      const size_t Q = *ToProcess.begin();
      ToProcess.erase(ToProcess.begin());

      if (ClusterIdForPoint_[Q].isNoise()) {
        // Change noise point to border point.
        ClusterIdForPoint_[Q] = CurrentCluster.Id;
        CurrentCluster.PointIndices.push_back(Q);
        continue;
      }
      if (!ClusterIdForPoint_[Q].isUndef()) {
        continue; // Previously processed.
      }
      // Add Q to the current custer.
      ClusterIdForPoint_[Q] = CurrentCluster.Id;
      CurrentCluster.PointIndices.push_back(Q);
      // And extend to the neighbors of Q if the region is dense enough.
      rangeQuery(Q, Neighbors);
      if (Neighbors.size() + 1 >= MinPts) {
        ToProcess.insert(Neighbors.begin(), Neighbors.end());
      }
    }
  }
  // assert(Neighbors.capacity() == (Points_.size() - 1));
  // ^ True, but it is not quaranteed to be true in all the cases.

  // Add noisy points to noise cluster.
  for (size_t P = 0, NumPoints = Points_.size(); P < NumPoints; ++P) {
    if (ClusterIdForPoint_[P].isNoise()) {
      NoiseCluster_.PointIndices.push_back(P);
    }
  }
}

void BenchmarkClustering::clusterizeNaive(
    const MCSubtargetInfo &SubtargetInfo, const MCInstrInfo &InstrInfo) {
  // Given an instruction Opcode, which sched class id's are represented,
  // and which are the benchmarks for each sched class?
  std::vector<SmallMapVector<unsigned, SmallVector<size_t, 1>, 1>>
      OpcodeToSchedClassesToPoints;
  const unsigned NumOpcodes = InstrInfo.getNumOpcodes();
  OpcodeToSchedClassesToPoints.resize(NumOpcodes);
  size_t NumClusters = 0;
  for (size_t P = 0, NumPoints = Points_.size(); P < NumPoints; ++P) {
    const Benchmark &Point = Points_[P];
    const MCInst &MCI = Point.keyInstruction();
    unsigned SchedClassId;
    std::tie(SchedClassId, std::ignore) =
        ResolvedSchedClass::resolveSchedClassId(SubtargetInfo, InstrInfo, MCI);
    const unsigned Opcode = MCI.getOpcode();
    assert(Opcode < NumOpcodes && "NumOpcodes is incorrect (too small)");
    auto &Points = OpcodeToSchedClassesToPoints[Opcode][SchedClassId];
    if (Points.empty()) // If we previously have not seen any points of
      ++NumClusters;    // this opcode's sched class, then new cluster begins.
    Points.emplace_back(P);
  }
  assert(NumClusters <= NumOpcodes &&
         "can't see more opcodes than there are total opcodes");
  assert(NumClusters <= Points_.size() &&
         "can't see more opcodes than there are total points");

  Clusters_.reserve(NumClusters); // We already know how many clusters there is.
  for (const auto &SchedClassesOfOpcode : OpcodeToSchedClassesToPoints) {
    if (SchedClassesOfOpcode.empty())
      continue;
    for (ArrayRef<size_t> PointsOfSchedClass :
         make_second_range(SchedClassesOfOpcode)) {
      if (PointsOfSchedClass.empty())
        continue;
      // Create a new cluster.
      Clusters_.emplace_back(ClusterId::makeValid(
          Clusters_.size(),
          /*IsUnstable=*/!areAllNeighbours(PointsOfSchedClass)));
      Cluster &CurrentCluster = Clusters_.back();
      // Mark points as belonging to the new cluster.
      for (size_t P : PointsOfSchedClass)
        ClusterIdForPoint_[P] = CurrentCluster.Id;
      // And add all the points of this opcode's sched class to the new cluster.
      CurrentCluster.PointIndices.reserve(PointsOfSchedClass.size());
      CurrentCluster.PointIndices.assign(PointsOfSchedClass.begin(),
                                         PointsOfSchedClass.end());
      assert(CurrentCluster.PointIndices.size() == PointsOfSchedClass.size());
    }
  }
  assert(Clusters_.size() == NumClusters);
}

// Given an instruction Opcode, we can make benchmarks (measurements) of the
// instruction characteristics/performance. Then, to facilitate further analysis
// we group the benchmarks with *similar* characteristics into clusters.
// Now, this is all not entirely deterministic. Some instructions have variable
// characteristics, depending on their arguments. And thus, if we do several
// benchmarks of the same instruction Opcode, we may end up with *different*
// performance characteristics measurements. And when we then do clustering,
// these several benchmarks of the same instruction Opcode may end up being
// clustered into *different* clusters. This is not great for further analysis.
// We shall find every opcode with benchmarks not in just one cluster, and move
// *all* the benchmarks of said Opcode into one new unstable cluster per Opcode.
void BenchmarkClustering::stabilize(unsigned NumOpcodes) {
  // Given an instruction Opcode and Config, in which clusters do benchmarks of
  // this instruction lie? Normally, they all should be in the same cluster.
  struct OpcodeAndConfig {
    explicit OpcodeAndConfig(const Benchmark &IB)
        : Opcode(IB.keyInstruction().getOpcode()), Config(&IB.Key.Config) {}
    unsigned Opcode;
    const std::string *Config;

    auto Tie() const -> auto { return std::tie(Opcode, *Config); }

    bool operator<(const OpcodeAndConfig &O) const { return Tie() < O.Tie(); }
    bool operator!=(const OpcodeAndConfig &O) const { return Tie() != O.Tie(); }
  };
  std::map<OpcodeAndConfig, SmallSet<ClusterId, 1>> OpcodeConfigToClusterIDs;
  // Populate OpcodeConfigToClusterIDs and UnstableOpcodes data structures.
  assert(ClusterIdForPoint_.size() == Points_.size() && "size mismatch");
  for (auto Point : zip(Points_, ClusterIdForPoint_)) {
    const ClusterId &ClusterIdOfPoint = std::get<1>(Point);
    if (!ClusterIdOfPoint.isValid())
      continue; // Only process fully valid clusters.
    const OpcodeAndConfig Key(std::get<0>(Point));
    SmallSet<ClusterId, 1> &ClusterIDsOfOpcode = OpcodeConfigToClusterIDs[Key];
    ClusterIDsOfOpcode.insert(ClusterIdOfPoint);
  }

  for (const auto &OpcodeConfigToClusterID : OpcodeConfigToClusterIDs) {
    const SmallSet<ClusterId, 1> &ClusterIDs = OpcodeConfigToClusterID.second;
    const OpcodeAndConfig &Key = OpcodeConfigToClusterID.first;
    // We only care about unstable instructions.
    if (ClusterIDs.size() < 2)
      continue;

    // Create a new unstable cluster, one per Opcode.
    Clusters_.emplace_back(ClusterId::makeValidUnstable(Clusters_.size()));
    Cluster &UnstableCluster = Clusters_.back();
    // We will find *at least* one point in each of these clusters.
    UnstableCluster.PointIndices.reserve(ClusterIDs.size());

    // Go through every cluster which we recorded as containing benchmarks
    // of this UnstableOpcode. NOTE: we only recorded valid clusters.
    for (const ClusterId &CID : ClusterIDs) {
      assert(CID.isValid() &&
             "We only recorded valid clusters, not noise/error clusters.");
      Cluster &OldCluster = Clusters_[CID.getId()]; // Valid clusters storage.
      // Within each cluster, go through each point, and either move it to the
      // new unstable cluster, or 'keep' it.
      // In this case, we'll reshuffle OldCluster.PointIndices vector
      // so that all the points that are *not* for UnstableOpcode are first,
      // and the rest of the points is for the UnstableOpcode.
      const auto it = std::stable_partition(
          OldCluster.PointIndices.begin(), OldCluster.PointIndices.end(),
          [this, &Key](size_t P) {
            return OpcodeAndConfig(Points_[P]) != Key;
          });
      assert(std::distance(it, OldCluster.PointIndices.end()) > 0 &&
             "Should have found at least one bad point");
      // Mark to-be-moved points as belonging to the new cluster.
      for (size_t P : make_range(it, OldCluster.PointIndices.end()))
        ClusterIdForPoint_[P] = UnstableCluster.Id;
      // Actually append to-be-moved points to the new cluster.
      UnstableCluster.PointIndices.insert(UnstableCluster.PointIndices.end(),
                                          it, OldCluster.PointIndices.end());
      // And finally, remove "to-be-moved" points from the old cluster.
      OldCluster.PointIndices.erase(it, OldCluster.PointIndices.end());
      // Now, the old cluster may end up being empty, but let's just keep it
      // in whatever state it ended up. Purging empty clusters isn't worth it.
    };
    assert(UnstableCluster.PointIndices.size() > 1 &&
           "New unstable cluster should end up with more than one point.");
    assert(UnstableCluster.PointIndices.size() >= ClusterIDs.size() &&
           "New unstable cluster should end up with no less points than there "
           "was clusters");
  }
}

Expected<BenchmarkClustering> BenchmarkClustering::create(
    const std::vector<Benchmark> &Points, const ModeE Mode,
    const size_t DbscanMinPts, const double AnalysisClusteringEpsilon,
    const MCSubtargetInfo *SubtargetInfo, const MCInstrInfo *InstrInfo) {
  BenchmarkClustering Clustering(
      Points, AnalysisClusteringEpsilon * AnalysisClusteringEpsilon);
  if (auto Error = Clustering.validateAndSetup()) {
    return std::move(Error);
  }
  if (Clustering.ErrorCluster_.PointIndices.size() == Points.size()) {
    return Clustering; // Nothing to cluster.
  }

  if (Mode == ModeE::Dbscan) {
    Clustering.clusterizeDbScan(DbscanMinPts);

    if (InstrInfo)
      Clustering.stabilize(InstrInfo->getNumOpcodes());
  } else /*if(Mode == ModeE::Naive)*/ {
    if (!SubtargetInfo || !InstrInfo)
      return make_error<Failure>("'naive' clustering mode requires "
                                 "SubtargetInfo and InstrInfo to be present");
    Clustering.clusterizeNaive(*SubtargetInfo, *InstrInfo);
  }

  return Clustering;
}

void SchedClassClusterCentroid::addPoint(ArrayRef<BenchmarkMeasure> Point) {
  if (Representative.empty())
    Representative.resize(Point.size());
  assert(Representative.size() == Point.size() &&
         "All points should have identical dimensions.");

  for (auto I : zip(Representative, Point))
    std::get<0>(I).push(std::get<1>(I));
}

std::vector<BenchmarkMeasure> SchedClassClusterCentroid::getAsPoint() const {
  std::vector<BenchmarkMeasure> ClusterCenterPoint(Representative.size());
  for (auto I : zip(ClusterCenterPoint, Representative))
    std::get<0>(I).PerInstructionValue = std::get<1>(I).avg();
  return ClusterCenterPoint;
}

bool SchedClassClusterCentroid::validate(
    Benchmark::ModeE Mode) const {
  size_t NumMeasurements = Representative.size();
  switch (Mode) {
  case Benchmark::Latency:
    if (NumMeasurements != 1) {
      errs()
          << "invalid number of measurements in latency mode: expected 1, got "
          << NumMeasurements << "\n";
      return false;
    }
    break;
  case Benchmark::Uops:
    // Can have many measurements.
    break;
  case Benchmark::InverseThroughput:
    if (NumMeasurements != 1) {
      errs() << "invalid number of measurements in inverse throughput "
                "mode: expected 1, got "
             << NumMeasurements << "\n";
      return false;
    }
    break;
  default:
    llvm_unreachable("unimplemented measurement matching mode");
    return false;
  }

  return true; // All good.
}

} // namespace exegesis
} // namespace llvm
