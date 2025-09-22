//===- CoverageSummaryInfo.h - Coverage summary for function/file ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These structures are used to represent code coverage metrics
// for functions/files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_COV_COVERAGESUMMARYINFO_H
#define LLVM_COV_COVERAGESUMMARYINFO_H

#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

/// Provides information about region coverage for a function/file.
class RegionCoverageInfo {
  /// The number of regions that were executed at least once.
  size_t Covered;

  /// The total number of regions in a function/file.
  size_t NumRegions;

public:
  RegionCoverageInfo() : Covered(0), NumRegions(0) {}

  RegionCoverageInfo(size_t Covered, size_t NumRegions)
      : Covered(Covered), NumRegions(NumRegions) {
    assert(Covered <= NumRegions && "Covered regions over-counted");
  }

  RegionCoverageInfo &operator+=(const RegionCoverageInfo &RHS) {
    Covered += RHS.Covered;
    NumRegions += RHS.NumRegions;
    return *this;
  }

  void merge(const RegionCoverageInfo &RHS) {
    Covered = std::max(Covered, RHS.Covered);
    NumRegions = std::max(NumRegions, RHS.NumRegions);
  }

  size_t getCovered() const { return Covered; }

  size_t getNumRegions() const { return NumRegions; }

  bool isFullyCovered() const { return Covered == NumRegions; }

  double getPercentCovered() const {
    assert(Covered <= NumRegions && "Covered regions over-counted");
    if (NumRegions == 0)
      return 0.0;
    return double(Covered) / double(NumRegions) * 100.0;
  }
};

/// Provides information about line coverage for a function/file.
class LineCoverageInfo {
  /// The number of lines that were executed at least once.
  size_t Covered;

  /// The total number of lines in a function/file.
  size_t NumLines;

public:
  LineCoverageInfo() : Covered(0), NumLines(0) {}

  LineCoverageInfo(size_t Covered, size_t NumLines)
      : Covered(Covered), NumLines(NumLines) {
    assert(Covered <= NumLines && "Covered lines over-counted");
  }

  LineCoverageInfo &operator+=(const LineCoverageInfo &RHS) {
    Covered += RHS.Covered;
    NumLines += RHS.NumLines;
    return *this;
  }

  void merge(const LineCoverageInfo &RHS) {
    Covered = std::max(Covered, RHS.Covered);
    NumLines = std::max(NumLines, RHS.NumLines);
  }

  size_t getCovered() const { return Covered; }

  size_t getNumLines() const { return NumLines; }

  bool isFullyCovered() const { return Covered == NumLines; }

  double getPercentCovered() const {
    assert(Covered <= NumLines && "Covered lines over-counted");
    if (NumLines == 0)
      return 0.0;
    return double(Covered) / double(NumLines) * 100.0;
  }
};

/// Provides information about branches coverage for a function/file.
class BranchCoverageInfo {
  /// The number of branches that were executed at least once.
  size_t Covered;

  /// The total number of branches in a function/file.
  size_t NumBranches;

public:
  BranchCoverageInfo() : Covered(0), NumBranches(0) {}

  BranchCoverageInfo(size_t Covered, size_t NumBranches)
      : Covered(Covered), NumBranches(NumBranches) {
    assert(Covered <= NumBranches && "Covered branches over-counted");
  }

  BranchCoverageInfo &operator+=(const BranchCoverageInfo &RHS) {
    Covered += RHS.Covered;
    NumBranches += RHS.NumBranches;
    return *this;
  }

  void merge(const BranchCoverageInfo &RHS) {
    Covered = std::max(Covered, RHS.Covered);
    NumBranches = std::max(NumBranches, RHS.NumBranches);
  }

  size_t getCovered() const { return Covered; }

  size_t getNumBranches() const { return NumBranches; }

  bool isFullyCovered() const { return Covered == NumBranches; }

  double getPercentCovered() const {
    assert(Covered <= NumBranches && "Covered branches over-counted");
    if (NumBranches == 0)
      return 0.0;
    return double(Covered) / double(NumBranches) * 100.0;
  }
};

/// Provides information about MC/DC coverage for a function/file.
class MCDCCoverageInfo {
  /// The number of Independence Pairs that were covered.
  size_t CoveredPairs;

  /// The total number of Independence Pairs in a function/file.
  size_t NumPairs;

public:
  MCDCCoverageInfo() : CoveredPairs(0), NumPairs(0) {}

  MCDCCoverageInfo(size_t CoveredPairs, size_t NumPairs)
      : CoveredPairs(CoveredPairs), NumPairs(NumPairs) {
    assert(CoveredPairs <= NumPairs && "Covered pairs over-counted");
  }

  MCDCCoverageInfo &operator+=(const MCDCCoverageInfo &RHS) {
    CoveredPairs += RHS.CoveredPairs;
    NumPairs += RHS.NumPairs;
    return *this;
  }

  void merge(const MCDCCoverageInfo &RHS) {
    CoveredPairs = std::max(CoveredPairs, RHS.CoveredPairs);
    NumPairs = std::max(NumPairs, RHS.NumPairs);
  }

  size_t getCoveredPairs() const { return CoveredPairs; }

  size_t getNumPairs() const { return NumPairs; }

  bool isFullyCovered() const { return CoveredPairs == NumPairs; }

  double getPercentCovered() const {
    assert(CoveredPairs <= NumPairs && "Covered pairs over-counted");
    if (NumPairs == 0)
      return 0.0;
    return double(CoveredPairs) / double(NumPairs) * 100.0;
  }
};

/// Provides information about function coverage for a file.
class FunctionCoverageInfo {
  /// The number of functions that were executed.
  size_t Executed;

  /// The total number of functions in this file.
  size_t NumFunctions;

public:
  FunctionCoverageInfo() : Executed(0), NumFunctions(0) {}

  FunctionCoverageInfo(size_t Executed, size_t NumFunctions)
      : Executed(Executed), NumFunctions(NumFunctions) {}

  FunctionCoverageInfo &operator+=(const FunctionCoverageInfo &RHS) {
    Executed += RHS.Executed;
    NumFunctions += RHS.NumFunctions;
    return *this;
  }

  void addFunction(bool Covered) {
    if (Covered)
      ++Executed;
    ++NumFunctions;
  }

  size_t getExecuted() const { return Executed; }

  size_t getNumFunctions() const { return NumFunctions; }

  bool isFullyCovered() const { return Executed == NumFunctions; }

  double getPercentCovered() const {
    assert(Executed <= NumFunctions && "Covered functions over-counted");
    if (NumFunctions == 0)
      return 0.0;
    return double(Executed) / double(NumFunctions) * 100.0;
  }
};

/// A summary of function's code coverage.
struct FunctionCoverageSummary {
  std::string Name;
  uint64_t ExecutionCount;
  RegionCoverageInfo RegionCoverage;
  LineCoverageInfo LineCoverage;
  BranchCoverageInfo BranchCoverage;
  MCDCCoverageInfo MCDCCoverage;

  FunctionCoverageSummary(const std::string &Name)
      : Name(Name), ExecutionCount(0) {}

  FunctionCoverageSummary(const std::string &Name, uint64_t ExecutionCount,
                          const RegionCoverageInfo &RegionCoverage,
                          const LineCoverageInfo &LineCoverage,
                          const BranchCoverageInfo &BranchCoverage,
                          const MCDCCoverageInfo &MCDCCoverage)
      : Name(Name), ExecutionCount(ExecutionCount),
        RegionCoverage(RegionCoverage), LineCoverage(LineCoverage),
        BranchCoverage(BranchCoverage), MCDCCoverage(MCDCCoverage) {}

  /// Compute the code coverage summary for the given function coverage
  /// mapping record.
  static FunctionCoverageSummary get(const coverage::CoverageMapping &CM,
                                     const coverage::FunctionRecord &Function);

  /// Compute the code coverage summary for an instantiation group \p Group,
  /// given a list of summaries for each instantiation in \p Summaries.
  static FunctionCoverageSummary
  get(const coverage::InstantiationGroup &Group,
      ArrayRef<FunctionCoverageSummary> Summaries);
};

/// A summary of file's code coverage.
struct FileCoverageSummary {
  StringRef Name;
  RegionCoverageInfo RegionCoverage;
  LineCoverageInfo LineCoverage;
  BranchCoverageInfo BranchCoverage;
  MCDCCoverageInfo MCDCCoverage;
  FunctionCoverageInfo FunctionCoverage;
  FunctionCoverageInfo InstantiationCoverage;

  FileCoverageSummary() = default;
  FileCoverageSummary(StringRef Name) : Name(Name) {}

  FileCoverageSummary &operator+=(const FileCoverageSummary &RHS) {
    RegionCoverage += RHS.RegionCoverage;
    LineCoverage += RHS.LineCoverage;
    FunctionCoverage += RHS.FunctionCoverage;
    BranchCoverage += RHS.BranchCoverage;
    MCDCCoverage += RHS.MCDCCoverage;
    InstantiationCoverage += RHS.InstantiationCoverage;
    return *this;
  }

  void addFunction(const FunctionCoverageSummary &Function) {
    RegionCoverage += Function.RegionCoverage;
    LineCoverage += Function.LineCoverage;
    BranchCoverage += Function.BranchCoverage;
    MCDCCoverage += Function.MCDCCoverage;
    FunctionCoverage.addFunction(/*Covered=*/Function.ExecutionCount > 0);
  }

  void addInstantiation(const FunctionCoverageSummary &Function) {
    InstantiationCoverage.addFunction(/*Covered=*/Function.ExecutionCount > 0);
  }
};

/// A cache for demangled symbols.
struct DemangleCache {
  StringMap<std::string> DemangledNames;

  /// Demangle \p Sym if possible. Otherwise, just return \p Sym.
  StringRef demangle(StringRef Sym) const {
    const auto DemangledName = DemangledNames.find(Sym);
    if (DemangledName == DemangledNames.end())
      return Sym;
    return DemangledName->getValue();
  }
};

} // namespace llvm

#endif // LLVM_COV_COVERAGESUMMARYINFO_H
