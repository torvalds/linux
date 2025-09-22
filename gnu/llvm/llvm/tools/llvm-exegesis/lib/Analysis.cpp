//===-- Analysis.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Analysis.h"
#include "BenchmarkResult.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/FormatVariadic.h"
#include <limits>
#include <vector>

namespace llvm {
namespace exegesis {

static const char kCsvSep = ',';

namespace {

enum EscapeTag { kEscapeCsv, kEscapeHtml, kEscapeHtmlString };

template <EscapeTag Tag> void writeEscaped(raw_ostream &OS, const StringRef S);

template <> void writeEscaped<kEscapeCsv>(raw_ostream &OS, const StringRef S) {
  if (!S.contains(kCsvSep)) {
    OS << S;
  } else {
    // Needs escaping.
    OS << '"';
    for (const char C : S) {
      if (C == '"')
        OS << "\"\"";
      else
        OS << C;
    }
    OS << '"';
  }
}

template <> void writeEscaped<kEscapeHtml>(raw_ostream &OS, const StringRef S) {
  for (const char C : S) {
    if (C == '<')
      OS << "&lt;";
    else if (C == '>')
      OS << "&gt;";
    else if (C == '&')
      OS << "&amp;";
    else
      OS << C;
  }
}

template <>
void writeEscaped<kEscapeHtmlString>(raw_ostream &OS, const StringRef S) {
  for (const char C : S) {
    if (C == '"')
      OS << "\\\"";
    else
      OS << C;
  }
}

} // namespace

template <EscapeTag Tag>
static void
writeClusterId(raw_ostream &OS,
               const BenchmarkClustering::ClusterId &CID) {
  if (CID.isNoise())
    writeEscaped<Tag>(OS, "[noise]");
  else if (CID.isError())
    writeEscaped<Tag>(OS, "[error]");
  else
    OS << CID.getId();
}

template <EscapeTag Tag>
static void writeMeasurementValue(raw_ostream &OS, const double Value) {
  // Given Value, if we wanted to serialize it to a string,
  // how many base-10 digits will we need to store, max?
  static constexpr auto MaxDigitCount =
      std::numeric_limits<decltype(Value)>::max_digits10;
  // Also, we will need a decimal separator.
  static constexpr auto DecimalSeparatorLen = 1; // '.' e.g.
  // So how long of a string will the serialization produce, max?
  static constexpr auto SerializationLen = MaxDigitCount + DecimalSeparatorLen;

  // WARNING: when changing the format, also adjust the small-size estimate ^.
  static constexpr StringLiteral SimpleFloatFormat = StringLiteral("{0:F}");

  writeEscaped<Tag>(
      OS, formatv(SimpleFloatFormat.data(), Value).sstr<SerializationLen>());
}

template <typename EscapeTag, EscapeTag Tag>
void Analysis::writeSnippet(raw_ostream &OS, ArrayRef<uint8_t> Bytes,
                            const char *Separator) const {
  SmallVector<std::string, 3> Lines;
  // Parse the asm snippet and print it.
  while (!Bytes.empty()) {
    MCInst MI;
    uint64_t MISize = 0;
    if (!DisasmHelper_->decodeInst(MI, MISize, Bytes)) {
      writeEscaped<Tag>(OS, join(Lines, Separator));
      writeEscaped<Tag>(OS, Separator);
      writeEscaped<Tag>(OS, "[error decoding asm snippet]");
      return;
    }
    SmallString<128> InstPrinterStr; // FIXME: magic number.
    raw_svector_ostream OSS(InstPrinterStr);
    DisasmHelper_->printInst(&MI, OSS);
    Bytes = Bytes.drop_front(MISize);
    Lines.emplace_back(InstPrinterStr.str().trim());
  }
  writeEscaped<Tag>(OS, join(Lines, Separator));
}

// Prints a row representing an instruction, along with scheduling info and
// point coordinates (measurements).
void Analysis::printInstructionRowCsv(const size_t PointId,
                                      raw_ostream &OS) const {
  const Benchmark &Point = Clustering_.getPoints()[PointId];
  writeClusterId<kEscapeCsv>(OS, Clustering_.getClusterIdForPoint(PointId));
  OS << kCsvSep;
  writeSnippet<EscapeTag, kEscapeCsv>(OS, Point.AssembledSnippet, "; ");
  OS << kCsvSep;
  writeEscaped<kEscapeCsv>(OS, Point.Key.Config);
  OS << kCsvSep;
  assert(!Point.Key.Instructions.empty());
  const MCInst &MCI = Point.keyInstruction();
  unsigned SchedClassId;
  std::tie(SchedClassId, std::ignore) = ResolvedSchedClass::resolveSchedClassId(
      State_.getSubtargetInfo(), State_.getInstrInfo(), MCI);
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  const MCSchedClassDesc *const SCDesc =
      State_.getSubtargetInfo().getSchedModel().getSchedClassDesc(SchedClassId);
  writeEscaped<kEscapeCsv>(OS, SCDesc->Name);
#else
  OS << SchedClassId;
#endif
  for (const auto &Measurement : Point.Measurements) {
    OS << kCsvSep;
    writeMeasurementValue<kEscapeCsv>(OS, Measurement.PerInstructionValue);
  }
  OS << "\n";
}

Analysis::Analysis(const LLVMState &State,
                   const BenchmarkClustering &Clustering,
                   double AnalysisInconsistencyEpsilon,
                   bool AnalysisDisplayUnstableOpcodes)
    : Clustering_(Clustering), State_(State),
      AnalysisInconsistencyEpsilonSquared_(AnalysisInconsistencyEpsilon *
                                           AnalysisInconsistencyEpsilon),
      AnalysisDisplayUnstableOpcodes_(AnalysisDisplayUnstableOpcodes) {
  if (Clustering.getPoints().empty())
    return;

  DisasmHelper_ = std::make_unique<DisassemblerHelper>(State);
}

template <>
Error Analysis::run<Analysis::PrintClusters>(raw_ostream &OS) const {
  if (Clustering_.getPoints().empty())
    return Error::success();

  // Write the header.
  OS << "cluster_id" << kCsvSep << "opcode_name" << kCsvSep << "config"
     << kCsvSep << "sched_class";
  for (const auto &Measurement : Clustering_.getPoints().front().Measurements) {
    OS << kCsvSep;
    writeEscaped<kEscapeCsv>(OS, Measurement.Key);
  }
  OS << "\n";

  // Write the points.
  for (const auto &ClusterIt : Clustering_.getValidClusters()) {
    for (const size_t PointId : ClusterIt.PointIndices) {
      printInstructionRowCsv(PointId, OS);
    }
    OS << "\n\n";
  }
  return Error::success();
}

Analysis::ResolvedSchedClassAndPoints::ResolvedSchedClassAndPoints(
    ResolvedSchedClass &&RSC)
    : RSC(std::move(RSC)) {}

std::vector<Analysis::ResolvedSchedClassAndPoints>
Analysis::makePointsPerSchedClass() const {
  std::vector<ResolvedSchedClassAndPoints> Entries;
  // Maps SchedClassIds to index in result.
  std::unordered_map<unsigned, size_t> SchedClassIdToIndex;
  const auto &Points = Clustering_.getPoints();
  for (size_t PointId = 0, E = Points.size(); PointId < E; ++PointId) {
    const Benchmark &Point = Points[PointId];
    if (!Point.Error.empty())
      continue;
    assert(!Point.Key.Instructions.empty());
    // FIXME: we should be using the tuple of classes for instructions in the
    // snippet as key.
    const MCInst &MCI = Point.keyInstruction();
    unsigned SchedClassId;
    bool WasVariant;
    std::tie(SchedClassId, WasVariant) =
        ResolvedSchedClass::resolveSchedClassId(State_.getSubtargetInfo(),
                                                State_.getInstrInfo(), MCI);
    const auto IndexIt = SchedClassIdToIndex.find(SchedClassId);
    if (IndexIt == SchedClassIdToIndex.end()) {
      // Create a new entry.
      SchedClassIdToIndex.emplace(SchedClassId, Entries.size());
      ResolvedSchedClassAndPoints Entry(ResolvedSchedClass(
          State_.getSubtargetInfo(), SchedClassId, WasVariant));
      Entry.PointIds.push_back(PointId);
      Entries.push_back(std::move(Entry));
    } else {
      // Append to the existing entry.
      Entries[IndexIt->second].PointIds.push_back(PointId);
    }
  }
  return Entries;
}

// Parallel benchmarks repeat the same opcode multiple times. Just show this
// opcode and show the whole snippet only on hover.
static void writeParallelSnippetHtml(raw_ostream &OS,
                                 const std::vector<MCInst> &Instructions,
                                 const MCInstrInfo &InstrInfo) {
  if (Instructions.empty())
    return;
  writeEscaped<kEscapeHtml>(OS, InstrInfo.getName(Instructions[0].getOpcode()));
  if (Instructions.size() > 1)
    OS << " (x" << Instructions.size() << ")";
}

// Latency tries to find a serial path. Just show the opcode path and show the
// whole snippet only on hover.
static void writeLatencySnippetHtml(raw_ostream &OS,
                                    const std::vector<MCInst> &Instructions,
                                    const MCInstrInfo &InstrInfo) {
  bool First = true;
  for (const MCInst &Instr : Instructions) {
    if (First)
      First = false;
    else
      OS << " &rarr; ";
    writeEscaped<kEscapeHtml>(OS, InstrInfo.getName(Instr.getOpcode()));
  }
}

void Analysis::printPointHtml(const Benchmark &Point, raw_ostream &OS) const {
  OS << "<li><span class=\"mono\" title=\"";
  writeSnippet<EscapeTag, kEscapeHtmlString>(OS, Point.AssembledSnippet, "\n");
  OS << "\">";
  switch (Point.Mode) {
  case Benchmark::Latency:
    writeLatencySnippetHtml(OS, Point.Key.Instructions, State_.getInstrInfo());
    break;
  case Benchmark::Uops:
  case Benchmark::InverseThroughput:
    writeParallelSnippetHtml(OS, Point.Key.Instructions, State_.getInstrInfo());
    break;
  default:
    llvm_unreachable("invalid mode");
  }
  OS << "</span> <span class=\"mono\">";
  writeEscaped<kEscapeHtml>(OS, Point.Key.Config);
  OS << "</span></li>";
}

void Analysis::printSchedClassClustersHtml(
    const std::vector<SchedClassCluster> &Clusters,
    const ResolvedSchedClass &RSC, raw_ostream &OS) const {
  const auto &Points = Clustering_.getPoints();
  OS << "<table class=\"sched-class-clusters\">";
  OS << "<tr><th>ClusterId</th><th>Opcode/Config</th>";
  assert(!Clusters.empty());
  for (const auto &Measurement :
       Points[Clusters[0].getPointIds()[0]].Measurements) {
    OS << "<th>";
    writeEscaped<kEscapeHtml>(OS, Measurement.Key);
    OS << "</th>";
  }
  OS << "</tr>";
  for (const SchedClassCluster &Cluster : Clusters) {
    OS << "<tr class=\""
       << (Cluster.measurementsMatch(State_.getSubtargetInfo(), RSC,
                                     Clustering_,
                                     AnalysisInconsistencyEpsilonSquared_)
               ? "good-cluster"
               : "bad-cluster")
       << "\"><td>";
    writeClusterId<kEscapeHtml>(OS, Cluster.id());
    OS << "</td><td><ul>";
    for (const size_t PointId : Cluster.getPointIds()) {
      printPointHtml(Points[PointId], OS);
    }
    OS << "</ul></td>";
    for (const auto &Stats : Cluster.getCentroid().getStats()) {
      OS << "<td class=\"measurement\">";
      writeMeasurementValue<kEscapeHtml>(OS, Stats.avg());
      OS << "<br><span class=\"minmax\">[";
      writeMeasurementValue<kEscapeHtml>(OS, Stats.min());
      OS << ";";
      writeMeasurementValue<kEscapeHtml>(OS, Stats.max());
      OS << "]</span></td>";
    }
    OS << "</tr>";
  }
  OS << "</table>";
}

void Analysis::SchedClassCluster::addPoint(
    size_t PointId, const BenchmarkClustering &Clustering) {
  PointIds.push_back(PointId);
  const auto &Point = Clustering.getPoints()[PointId];
  if (ClusterId.isUndef())
    ClusterId = Clustering.getClusterIdForPoint(PointId);
  assert(ClusterId == Clustering.getClusterIdForPoint(PointId));

  Centroid.addPoint(Point.Measurements);
}

bool Analysis::SchedClassCluster::measurementsMatch(
    const MCSubtargetInfo &STI, const ResolvedSchedClass &RSC,
    const BenchmarkClustering &Clustering,
    const double AnalysisInconsistencyEpsilonSquared_) const {
  assert(!Clustering.getPoints().empty());
  const Benchmark::ModeE Mode = Clustering.getPoints()[0].Mode;

  if (!Centroid.validate(Mode))
    return false;

  const std::vector<BenchmarkMeasure> ClusterCenterPoint =
      Centroid.getAsPoint();

  const std::vector<BenchmarkMeasure> SchedClassPoint =
      RSC.getAsPoint(Mode, STI, Centroid.getStats());
  if (SchedClassPoint.empty())
    return false; // In Uops mode validate() may not be enough.

  assert(ClusterCenterPoint.size() == SchedClassPoint.size() &&
         "Expected measured/sched data dimensions to match.");

  return Clustering.isNeighbour(ClusterCenterPoint, SchedClassPoint,
                                AnalysisInconsistencyEpsilonSquared_);
}

void Analysis::printSchedClassDescHtml(const ResolvedSchedClass &RSC,
                                       raw_ostream &OS) const {
  OS << "<table class=\"sched-class-desc\">";
  OS << "<tr><th>Valid</th><th>Variant</th><th>NumMicroOps</th><th>Latency</"
        "th><th>RThroughput</th><th>WriteProcRes</th><th title=\"This is the "
        "idealized unit resource (port) pressure assuming ideal "
        "distribution\">Idealized Resource Pressure</th></tr>";
  if (RSC.SCDesc->isValid()) {
    const auto &SI = State_.getSubtargetInfo();
    const auto &SM = SI.getSchedModel();
    OS << "<tr><td>&#10004;</td>";
    OS << "<td>" << (RSC.WasVariant ? "&#10004;" : "&#10005;") << "</td>";
    OS << "<td>" << RSC.SCDesc->NumMicroOps << "</td>";
    // Latencies.
    OS << "<td><ul>";
    for (int I = 0, E = RSC.SCDesc->NumWriteLatencyEntries; I < E; ++I) {
      const auto *const Entry = SI.getWriteLatencyEntry(RSC.SCDesc, I);
      OS << "<li>" << Entry->Cycles;
      if (RSC.SCDesc->NumWriteLatencyEntries > 1) {
        // Dismabiguate if more than 1 latency.
        OS << " (WriteResourceID " << Entry->WriteResourceID << ")";
      }
      OS << "</li>";
    }
    OS << "</ul></td>";
    // inverse throughput.
    OS << "<td>";
    writeMeasurementValue<kEscapeHtml>(
        OS, MCSchedModel::getReciprocalThroughput(SI, *RSC.SCDesc));
    OS << "</td>";
    // WriteProcRes.
    OS << "<td><ul>";
    for (const auto &WPR : RSC.NonRedundantWriteProcRes) {
      OS << "<li><span class=\"mono\">";
      writeEscaped<kEscapeHtml>(OS,
                                SM.getProcResource(WPR.ProcResourceIdx)->Name);
      OS << "</span>: " << WPR.ReleaseAtCycle << "</li>";
    }
    OS << "</ul></td>";
    // Idealized port pressure.
    OS << "<td><ul>";
    for (const auto &Pressure : RSC.IdealizedProcResPressure) {
      OS << "<li><span class=\"mono\">";
      writeEscaped<kEscapeHtml>(
          OS, SI.getSchedModel().getProcResource(Pressure.first)->Name);
      OS << "</span>: ";
      writeMeasurementValue<kEscapeHtml>(OS, Pressure.second);
      OS << "</li>";
    }
    OS << "</ul></td>";
    OS << "</tr>";
  } else {
    OS << "<tr><td>&#10005;</td><td></td><td></td></tr>";
  }
  OS << "</table>";
}

void Analysis::printClusterRawHtml(const BenchmarkClustering::ClusterId &Id,
                                   StringRef display_name,
                                   raw_ostream &OS) const {
  const auto &Points = Clustering_.getPoints();
  const auto &Cluster = Clustering_.getCluster(Id);
  if (Cluster.PointIndices.empty())
    return;

  OS << "<div class=\"inconsistency\"><p>" << display_name << " Cluster ("
     << Cluster.PointIndices.size() << " points)</p>";
  OS << "<table class=\"sched-class-clusters\">";
  // Table Header.
  OS << "<tr><th>ClusterId</th><th>Opcode/Config</th>";
  for (const auto &Measurement : Points[Cluster.PointIndices[0]].Measurements) {
    OS << "<th>";
    writeEscaped<kEscapeHtml>(OS, Measurement.Key);
    OS << "</th>";
  }
  OS << "</tr>";

  // Point data.
  for (const auto &PointId : Cluster.PointIndices) {
    OS << "<tr class=\"bad-cluster\"><td>" << display_name << "</td><td><ul>";
    printPointHtml(Points[PointId], OS);
    OS << "</ul></td>";
    for (const auto &Measurement : Points[PointId].Measurements) {
      OS << "<td class=\"measurement\">";
      writeMeasurementValue<kEscapeHtml>(OS, Measurement.PerInstructionValue);
    }
    OS << "</tr>";
  }
  OS << "</table>";

  OS << "</div>";

} // namespace exegesis

static constexpr const char kHtmlHead[] = R"(
<head>
<title>llvm-exegesis Analysis Results</title>
<style>
body {
  font-family: sans-serif
}
span.sched-class-name {
  font-weight: bold;
  font-family: monospace;
}
span.opcode {
  font-family: monospace;
}
span.config {
  font-family: monospace;
}
div.inconsistency {
  margin-top: 50px;
}
table {
  margin-left: 50px;
  border-collapse: collapse;
}
table, table tr,td,th {
  border: 1px solid #444;
}
table ul {
  padding-left: 0px;
  margin: 0px;
  list-style-type: none;
}
table.sched-class-clusters td {
  padding-left: 10px;
  padding-right: 10px;
  padding-top: 10px;
  padding-bottom: 10px;
}
table.sched-class-desc td {
  padding-left: 10px;
  padding-right: 10px;
  padding-top: 2px;
  padding-bottom: 2px;
}
span.mono {
  font-family: monospace;
}
td.measurement {
  text-align: center;
}
tr.good-cluster td.measurement {
  color: #292
}
tr.bad-cluster td.measurement {
  color: #922
}
tr.good-cluster td.measurement span.minmax {
  color: #888;
}
tr.bad-cluster td.measurement span.minmax {
  color: #888;
}
</style>
</head>
)";

template <>
Error Analysis::run<Analysis::PrintSchedClassInconsistencies>(
    raw_ostream &OS) const {
  const auto &FirstPoint = Clustering_.getPoints()[0];
  // Print the header.
  OS << "<!DOCTYPE html><html>" << kHtmlHead << "<body>";
  OS << "<h1><span class=\"mono\">llvm-exegesis</span> Analysis Results</h1>";
  OS << "<h3>Triple: <span class=\"mono\">";
  writeEscaped<kEscapeHtml>(OS, FirstPoint.LLVMTriple);
  OS << "</span></h3><h3>Cpu: <span class=\"mono\">";
  writeEscaped<kEscapeHtml>(OS, FirstPoint.CpuName);
  OS << "</span></h3>";
  OS << "<h3>Epsilon: <span class=\"mono\">"
     << format("%0.2f", std::sqrt(AnalysisInconsistencyEpsilonSquared_))
     << "</span></h3>";

  const auto &SI = State_.getSubtargetInfo();
  for (const auto &RSCAndPoints : makePointsPerSchedClass()) {
    if (!RSCAndPoints.RSC.SCDesc)
      continue;
    // Bucket sched class points into sched class clusters.
    std::vector<SchedClassCluster> SchedClassClusters;
    for (const size_t PointId : RSCAndPoints.PointIds) {
      const auto &ClusterId = Clustering_.getClusterIdForPoint(PointId);
      if (!ClusterId.isValid())
        continue; // Ignore noise and errors. FIXME: take noise into account ?
      if (ClusterId.isUnstable() ^ AnalysisDisplayUnstableOpcodes_)
        continue; // Either display stable or unstable clusters only.
      auto SchedClassClusterIt =
          find_if(SchedClassClusters, [ClusterId](const SchedClassCluster &C) {
            return C.id() == ClusterId;
          });
      if (SchedClassClusterIt == SchedClassClusters.end()) {
        SchedClassClusters.emplace_back();
        SchedClassClusterIt = std::prev(SchedClassClusters.end());
      }
      SchedClassClusterIt->addPoint(PointId, Clustering_);
    }

    // Print any scheduling class that has at least one cluster that does not
    // match the checked-in data.
    if (all_of(SchedClassClusters, [this, &RSCAndPoints,
                                    &SI](const SchedClassCluster &C) {
          return C.measurementsMatch(SI, RSCAndPoints.RSC, Clustering_,
                                     AnalysisInconsistencyEpsilonSquared_);
        }))
      continue; // Nothing weird.

    OS << "<div class=\"inconsistency\"><p>Sched Class <span "
          "class=\"sched-class-name\">";
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    writeEscaped<kEscapeHtml>(OS, RSCAndPoints.RSC.SCDesc->Name);
#else
    OS << RSCAndPoints.RSC.SchedClassId;
#endif
    OS << "</span> contains instructions whose performance characteristics do"
          " not match that of LLVM:</p>";
    printSchedClassClustersHtml(SchedClassClusters, RSCAndPoints.RSC, OS);
    OS << "<p>llvm SchedModel data:</p>";
    printSchedClassDescHtml(RSCAndPoints.RSC, OS);
    OS << "</div>";
  }

  printClusterRawHtml(BenchmarkClustering::ClusterId::noise(),
                      "[noise]", OS);

  OS << "</body></html>";
  return Error::success();
}

} // namespace exegesis
} // namespace llvm
