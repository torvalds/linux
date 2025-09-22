//===- CoverageExporterJson.cpp - Code coverage export --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements export of code coverage data to JSON.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// The json code coverage export follows the following format
// Root: dict => Root Element containing metadata
// -- Data: array => Homogeneous array of one or more export objects
//   -- Export: dict => Json representation of one CoverageMapping
//     -- Files: array => List of objects describing coverage for files
//       -- File: dict => Coverage for a single file
//         -- Branches: array => List of Branches in the file
//           -- Branch: dict => Describes a branch of the file with counters
//         -- MCDC Records: array => List of MCDC records in the file
//           -- MCDC Values: array => List of T/F covered condition values
//         -- Segments: array => List of Segments contained in the file
//           -- Segment: dict => Describes a segment of the file with a counter
//         -- Expansions: array => List of expansion records
//           -- Expansion: dict => Object that descibes a single expansion
//             -- CountedRegion: dict => The region to be expanded
//             -- TargetRegions: array => List of Regions in the expansion
//               -- CountedRegion: dict => Single Region in the expansion
//             -- Branches: array => List of Branches in the expansion
//               -- Branch: dict => Describes a branch in expansion and counters
//         -- Summary: dict => Object summarizing the coverage for this file
//           -- LineCoverage: dict => Object summarizing line coverage
//           -- FunctionCoverage: dict => Object summarizing function coverage
//           -- RegionCoverage: dict => Object summarizing region coverage
//           -- BranchCoverage: dict => Object summarizing branch coverage
//           -- MCDCCoverage: dict => Object summarizing MC/DC coverage
//     -- Functions: array => List of objects describing coverage for functions
//       -- Function: dict => Coverage info for a single function
//         -- Filenames: array => List of filenames that the function relates to
//   -- Summary: dict => Object summarizing the coverage for the entire binary
//     -- LineCoverage: dict => Object summarizing line coverage
//     -- FunctionCoverage: dict => Object summarizing function coverage
//     -- InstantiationCoverage: dict => Object summarizing inst. coverage
//     -- RegionCoverage: dict => Object summarizing region coverage
//     -- BranchCoverage: dict => Object summarizing branch coverage
//     -- MCDCCoverage: dict => Object summarizing MC/DC coverage
//
//===----------------------------------------------------------------------===//

#include "CoverageExporterJson.h"
#include "CoverageReport.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>

/// The semantic version combined as a string.
#define LLVM_COVERAGE_EXPORT_JSON_STR "2.0.1"

/// Unique type identifier for JSON coverage export.
#define LLVM_COVERAGE_EXPORT_JSON_TYPE_STR "llvm.coverage.json.export"

using namespace llvm;

namespace {

// The JSON library accepts int64_t, but profiling counts are stored as uint64_t.
// Therefore we need to explicitly convert from unsigned to signed, since a naive
// cast is implementation-defined behavior when the unsigned value cannot be
// represented as a signed value. We choose to clamp the values to preserve the
// invariant that counts are always >= 0.
int64_t clamp_uint64_to_int64(uint64_t u) {
  return std::min(u, static_cast<uint64_t>(std::numeric_limits<int64_t>::max()));
}

json::Array renderSegment(const coverage::CoverageSegment &Segment) {
  return json::Array({Segment.Line, Segment.Col,
                      clamp_uint64_to_int64(Segment.Count), Segment.HasCount,
                      Segment.IsRegionEntry, Segment.IsGapRegion});
}

json::Array renderRegion(const coverage::CountedRegion &Region) {
  return json::Array({Region.LineStart, Region.ColumnStart, Region.LineEnd,
                      Region.ColumnEnd, clamp_uint64_to_int64(Region.ExecutionCount),
                      Region.FileID, Region.ExpandedFileID,
                      int64_t(Region.Kind)});
}

json::Array renderBranch(const coverage::CountedRegion &Region) {
  return json::Array(
      {Region.LineStart, Region.ColumnStart, Region.LineEnd, Region.ColumnEnd,
       clamp_uint64_to_int64(Region.ExecutionCount),
       clamp_uint64_to_int64(Region.FalseExecutionCount), Region.FileID,
       Region.ExpandedFileID, int64_t(Region.Kind)});
}

json::Array gatherConditions(const coverage::MCDCRecord &Record) {
  json::Array Conditions;
  for (unsigned c = 0; c < Record.getNumConditions(); c++)
    Conditions.push_back(Record.isConditionIndependencePairCovered(c));
  return Conditions;
}

json::Array renderMCDCRecord(const coverage::MCDCRecord &Record) {
  const llvm::coverage::CounterMappingRegion &CMR = Record.getDecisionRegion();
  return json::Array({CMR.LineStart, CMR.ColumnStart, CMR.LineEnd,
                      CMR.ColumnEnd, CMR.ExpandedFileID, int64_t(CMR.Kind),
                      gatherConditions(Record)});
}

json::Array renderRegions(ArrayRef<coverage::CountedRegion> Regions) {
  json::Array RegionArray;
  for (const auto &Region : Regions)
    RegionArray.push_back(renderRegion(Region));
  return RegionArray;
}

json::Array renderBranchRegions(ArrayRef<coverage::CountedRegion> Regions) {
  json::Array RegionArray;
  for (const auto &Region : Regions)
    if (!Region.Folded)
      RegionArray.push_back(renderBranch(Region));
  return RegionArray;
}

json::Array renderMCDCRecords(ArrayRef<coverage::MCDCRecord> Records) {
  json::Array RecordArray;
  for (auto &Record : Records)
    RecordArray.push_back(renderMCDCRecord(Record));
  return RecordArray;
}

std::vector<llvm::coverage::CountedRegion>
collectNestedBranches(const coverage::CoverageMapping &Coverage,
                      ArrayRef<llvm::coverage::ExpansionRecord> Expansions) {
  std::vector<llvm::coverage::CountedRegion> Branches;
  for (const auto &Expansion : Expansions) {
    auto ExpansionCoverage = Coverage.getCoverageForExpansion(Expansion);

    // Recursively collect branches from nested expansions.
    auto NestedExpansions = ExpansionCoverage.getExpansions();
    auto NestedExBranches = collectNestedBranches(Coverage, NestedExpansions);
    append_range(Branches, NestedExBranches);

    // Add branches from this level of expansion.
    auto ExBranches = ExpansionCoverage.getBranches();
    for (auto B : ExBranches)
      if (B.FileID == Expansion.FileID)
        Branches.push_back(B);
  }

  return Branches;
}

json::Object renderExpansion(const coverage::CoverageMapping &Coverage,
                             const coverage::ExpansionRecord &Expansion) {
  std::vector<llvm::coverage::ExpansionRecord> Expansions = {Expansion};
  return json::Object(
      {{"filenames", json::Array(Expansion.Function.Filenames)},
       // Mark the beginning and end of this expansion in the source file.
       {"source_region", renderRegion(Expansion.Region)},
       // Enumerate the coverage information for the expansion.
       {"target_regions", renderRegions(Expansion.Function.CountedRegions)},
       // Enumerate the branch coverage information for the expansion.
       {"branches",
        renderBranchRegions(collectNestedBranches(Coverage, Expansions))}});
}

json::Object renderSummary(const FileCoverageSummary &Summary) {
  return json::Object(
      {{"lines",
        json::Object({{"count", int64_t(Summary.LineCoverage.getNumLines())},
                      {"covered", int64_t(Summary.LineCoverage.getCovered())},
                      {"percent", Summary.LineCoverage.getPercentCovered()}})},
       {"functions",
        json::Object(
            {{"count", int64_t(Summary.FunctionCoverage.getNumFunctions())},
             {"covered", int64_t(Summary.FunctionCoverage.getExecuted())},
             {"percent", Summary.FunctionCoverage.getPercentCovered()}})},
       {"instantiations",
        json::Object(
            {{"count",
              int64_t(Summary.InstantiationCoverage.getNumFunctions())},
             {"covered", int64_t(Summary.InstantiationCoverage.getExecuted())},
             {"percent", Summary.InstantiationCoverage.getPercentCovered()}})},
       {"regions",
        json::Object(
            {{"count", int64_t(Summary.RegionCoverage.getNumRegions())},
             {"covered", int64_t(Summary.RegionCoverage.getCovered())},
             {"notcovered", int64_t(Summary.RegionCoverage.getNumRegions() -
                                    Summary.RegionCoverage.getCovered())},
             {"percent", Summary.RegionCoverage.getPercentCovered()}})},
       {"branches",
        json::Object(
            {{"count", int64_t(Summary.BranchCoverage.getNumBranches())},
             {"covered", int64_t(Summary.BranchCoverage.getCovered())},
             {"notcovered", int64_t(Summary.BranchCoverage.getNumBranches() -
                                    Summary.BranchCoverage.getCovered())},
             {"percent", Summary.BranchCoverage.getPercentCovered()}})},
       {"mcdc",
        json::Object(
            {{"count", int64_t(Summary.MCDCCoverage.getNumPairs())},
             {"covered", int64_t(Summary.MCDCCoverage.getCoveredPairs())},
             {"notcovered", int64_t(Summary.MCDCCoverage.getNumPairs() -
                                    Summary.MCDCCoverage.getCoveredPairs())},
             {"percent", Summary.MCDCCoverage.getPercentCovered()}})}});
}

json::Array renderFileExpansions(const coverage::CoverageMapping &Coverage,
                                 const coverage::CoverageData &FileCoverage,
                                 const FileCoverageSummary &FileReport) {
  json::Array ExpansionArray;
  for (const auto &Expansion : FileCoverage.getExpansions())
    ExpansionArray.push_back(renderExpansion(Coverage, Expansion));
  return ExpansionArray;
}

json::Array renderFileSegments(const coverage::CoverageData &FileCoverage,
                               const FileCoverageSummary &FileReport) {
  json::Array SegmentArray;
  for (const auto &Segment : FileCoverage)
    SegmentArray.push_back(renderSegment(Segment));
  return SegmentArray;
}

json::Array renderFileBranches(const coverage::CoverageData &FileCoverage,
                               const FileCoverageSummary &FileReport) {
  json::Array BranchArray;
  for (const auto &Branch : FileCoverage.getBranches())
    BranchArray.push_back(renderBranch(Branch));
  return BranchArray;
}

json::Array renderFileMCDC(const coverage::CoverageData &FileCoverage,
                           const FileCoverageSummary &FileReport) {
  json::Array MCDCRecordArray;
  for (const auto &Record : FileCoverage.getMCDCRecords())
    MCDCRecordArray.push_back(renderMCDCRecord(Record));
  return MCDCRecordArray;
}

json::Object renderFile(const coverage::CoverageMapping &Coverage,
                        const std::string &Filename,
                        const FileCoverageSummary &FileReport,
                        const CoverageViewOptions &Options) {
  json::Object File({{"filename", Filename}});
  if (!Options.ExportSummaryOnly) {
    // Calculate and render detailed coverage information for given file.
    auto FileCoverage = Coverage.getCoverageForFile(Filename);
    File["segments"] = renderFileSegments(FileCoverage, FileReport);
    File["branches"] = renderFileBranches(FileCoverage, FileReport);
    File["mcdc_records"] = renderFileMCDC(FileCoverage, FileReport);
    if (!Options.SkipExpansions) {
      File["expansions"] =
          renderFileExpansions(Coverage, FileCoverage, FileReport);
    }
  }
  File["summary"] = renderSummary(FileReport);
  return File;
}

json::Array renderFiles(const coverage::CoverageMapping &Coverage,
                        ArrayRef<std::string> SourceFiles,
                        ArrayRef<FileCoverageSummary> FileReports,
                        const CoverageViewOptions &Options) {
  ThreadPoolStrategy S = hardware_concurrency(Options.NumThreads);
  if (Options.NumThreads == 0) {
    // If NumThreads is not specified, create one thread for each input, up to
    // the number of hardware cores.
    S = heavyweight_hardware_concurrency(SourceFiles.size());
    S.Limit = true;
  }
  DefaultThreadPool Pool(S);
  json::Array FileArray;
  std::mutex FileArrayMutex;

  for (unsigned I = 0, E = SourceFiles.size(); I < E; ++I) {
    auto &SourceFile = SourceFiles[I];
    auto &FileReport = FileReports[I];
    Pool.async([&] {
      auto File = renderFile(Coverage, SourceFile, FileReport, Options);
      {
        std::lock_guard<std::mutex> Lock(FileArrayMutex);
        FileArray.push_back(std::move(File));
      }
    });
  }
  Pool.wait();
  return FileArray;
}

json::Array renderFunctions(
    const iterator_range<coverage::FunctionRecordIterator> &Functions) {
  json::Array FunctionArray;
  for (const auto &F : Functions)
    FunctionArray.push_back(
        json::Object({{"name", F.Name},
                      {"count", clamp_uint64_to_int64(F.ExecutionCount)},
                      {"regions", renderRegions(F.CountedRegions)},
                      {"branches", renderBranchRegions(F.CountedBranchRegions)},
                      {"mcdc_records", renderMCDCRecords(F.MCDCRecords)},
                      {"filenames", json::Array(F.Filenames)}}));
  return FunctionArray;
}

} // end anonymous namespace

void CoverageExporterJson::renderRoot(const CoverageFilters &IgnoreFilters) {
  std::vector<std::string> SourceFiles;
  for (StringRef SF : Coverage.getUniqueSourceFiles()) {
    if (!IgnoreFilters.matchesFilename(SF))
      SourceFiles.emplace_back(SF);
  }
  renderRoot(SourceFiles);
}

void CoverageExporterJson::renderRoot(ArrayRef<std::string> SourceFiles) {
  FileCoverageSummary Totals = FileCoverageSummary("Totals");
  auto FileReports = CoverageReport::prepareFileReports(Coverage, Totals,
                                                        SourceFiles, Options);
  auto Files = renderFiles(Coverage, SourceFiles, FileReports, Options);
  // Sort files in order of their names.
  llvm::sort(Files, [](const json::Value &A, const json::Value &B) {
    const json::Object *ObjA = A.getAsObject();
    const json::Object *ObjB = B.getAsObject();
    assert(ObjA != nullptr && "Value A was not an Object");
    assert(ObjB != nullptr && "Value B was not an Object");
    const StringRef FilenameA = *ObjA->getString("filename");
    const StringRef FilenameB = *ObjB->getString("filename");
    return FilenameA.compare(FilenameB) < 0;
  });
  auto Export = json::Object(
      {{"files", std::move(Files)}, {"totals", renderSummary(Totals)}});
  // Skip functions-level information  if necessary.
  if (!Options.ExportSummaryOnly && !Options.SkipFunctions)
    Export["functions"] = renderFunctions(Coverage.getCoveredFunctions());

  auto ExportArray = json::Array({std::move(Export)});

  OS << json::Object({{"version", LLVM_COVERAGE_EXPORT_JSON_STR},
                      {"type", LLVM_COVERAGE_EXPORT_JSON_TYPE_STR},
                      {"data", std::move(ExportArray)}});
}
