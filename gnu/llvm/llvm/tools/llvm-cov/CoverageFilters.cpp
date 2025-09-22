//===- CoverageFilters.cpp - Function coverage mapping filters ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These classes provide filtering for function coverage mapping records.
//
//===----------------------------------------------------------------------===//

#include "CoverageFilters.h"
#include "CoverageSummaryInfo.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SpecialCaseList.h"

using namespace llvm;

bool NameCoverageFilter::matches(
    const coverage::CoverageMapping &,
    const coverage::FunctionRecord &Function) const {
  StringRef FuncName = Function.Name;
  return FuncName.contains(Name);
}

bool NameRegexCoverageFilter::matches(
    const coverage::CoverageMapping &,
    const coverage::FunctionRecord &Function) const {
  return llvm::Regex(Regex).match(Function.Name);
}

bool NameRegexCoverageFilter::matchesFilename(StringRef Filename) const {
  return llvm::Regex(Regex).match(Filename);
}

bool NameAllowlistCoverageFilter::matches(
    const coverage::CoverageMapping &,
    const coverage::FunctionRecord &Function) const {
  return Allowlist.inSection("llvmcov", "allowlist_fun", Function.Name);
}

bool RegionCoverageFilter::matches(
    const coverage::CoverageMapping &CM,
    const coverage::FunctionRecord &Function) const {
  return PassesThreshold(FunctionCoverageSummary::get(CM, Function)
                             .RegionCoverage.getPercentCovered());
}

bool LineCoverageFilter::matches(
    const coverage::CoverageMapping &CM,
    const coverage::FunctionRecord &Function) const {
  return PassesThreshold(FunctionCoverageSummary::get(CM, Function)
                             .LineCoverage.getPercentCovered());
}

void CoverageFilters::push_back(std::unique_ptr<CoverageFilter> Filter) {
  Filters.push_back(std::move(Filter));
}

bool CoverageFilters::matches(const coverage::CoverageMapping &CM,
                              const coverage::FunctionRecord &Function) const {
  for (const auto &Filter : Filters) {
    if (Filter->matches(CM, Function))
      return true;
  }
  return false;
}

bool CoverageFilters::matchesFilename(StringRef Filename) const {
  for (const auto &Filter : Filters) {
    if (Filter->matchesFilename(Filename))
      return true;
  }
  return false;
}

bool CoverageFiltersMatchAll::matches(
    const coverage::CoverageMapping &CM,
    const coverage::FunctionRecord &Function) const {
  for (const auto &Filter : Filters) {
    if (!Filter->matches(CM, Function))
      return false;
  }
  return true;
}
