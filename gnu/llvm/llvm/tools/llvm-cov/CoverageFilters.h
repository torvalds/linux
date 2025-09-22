//===- CoverageFilters.h - Function coverage mapping filters --------------===//
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

#ifndef LLVM_COV_COVERAGEFILTERS_H
#define LLVM_COV_COVERAGEFILTERS_H

#include "llvm/ADT/StringRef.h"
#include <memory>
#include <vector>

namespace llvm {
class SpecialCaseList;

namespace coverage {
class CoverageMapping;
struct FunctionRecord;
} // namespace coverage

/// Matches specific functions that pass the requirement of this filter.
class CoverageFilter {
public:
  virtual ~CoverageFilter() {}

  /// Return true if the function passes the requirements of this filter.
  virtual bool matches(const coverage::CoverageMapping &CM,
                       const coverage::FunctionRecord &Function) const {
    return true;
  }

  /// Return true if the filename passes the requirements of this filter.
  virtual bool matchesFilename(StringRef Filename) const {
    return true;
  }
};

/// Matches functions that contain a specific string in their name.
class NameCoverageFilter : public CoverageFilter {
  StringRef Name;

public:
  NameCoverageFilter(StringRef Name) : Name(Name) {}

  bool matches(const coverage::CoverageMapping &CM,
               const coverage::FunctionRecord &Function) const override;
};

/// Matches functions whose name matches a certain regular expression.
class NameRegexCoverageFilter : public CoverageFilter {
  StringRef Regex;

public:
  NameRegexCoverageFilter(StringRef Regex) : Regex(Regex) {}

  bool matches(const coverage::CoverageMapping &CM,
               const coverage::FunctionRecord &Function) const override;

  bool matchesFilename(StringRef Filename) const override;
};

/// Matches functions whose name appears in a SpecialCaseList in the
/// allowlist_fun section.
class NameAllowlistCoverageFilter : public CoverageFilter {
  const SpecialCaseList &Allowlist;

public:
  NameAllowlistCoverageFilter(const SpecialCaseList &Allowlist)
      : Allowlist(Allowlist) {}

  bool matches(const coverage::CoverageMapping &CM,
               const coverage::FunctionRecord &Function) const override;
};

/// Matches numbers that pass a certain threshold.
template <typename T> class StatisticThresholdFilter {
public:
  enum Operation { LessThan, GreaterThan };

protected:
  Operation Op;
  T Threshold;

  StatisticThresholdFilter(Operation Op, T Threshold)
      : Op(Op), Threshold(Threshold) {}

  /// Return true if the given number is less than
  /// or greater than the certain threshold.
  bool PassesThreshold(T Value) const {
    switch (Op) {
    case LessThan:
      return Value < Threshold;
    case GreaterThan:
      return Value > Threshold;
    }
    return false;
  }
};

/// Matches functions whose region coverage percentage
/// is above/below a certain percentage.
class RegionCoverageFilter : public CoverageFilter,
                             public StatisticThresholdFilter<double> {
public:
  RegionCoverageFilter(Operation Op, double Threshold)
      : StatisticThresholdFilter(Op, Threshold) {}

  bool matches(const coverage::CoverageMapping &CM,
               const coverage::FunctionRecord &Function) const override;
};

/// Matches functions whose line coverage percentage
/// is above/below a certain percentage.
class LineCoverageFilter : public CoverageFilter,
                           public StatisticThresholdFilter<double> {
public:
  LineCoverageFilter(Operation Op, double Threshold)
      : StatisticThresholdFilter(Op, Threshold) {}

  bool matches(const coverage::CoverageMapping &CM,
               const coverage::FunctionRecord &Function) const override;
};

/// A collection of filters.
/// Matches functions that match any filters contained
/// in an instance of this class.
class CoverageFilters : public CoverageFilter {
protected:
  std::vector<std::unique_ptr<CoverageFilter>> Filters;

public:
  /// Append a filter to this collection.
  void push_back(std::unique_ptr<CoverageFilter> Filter);

  bool empty() const { return Filters.empty(); }

  bool matches(const coverage::CoverageMapping &CM,
               const coverage::FunctionRecord &Function) const override;

  bool matchesFilename(StringRef Filename) const override;
};

/// A collection of filters.
/// Matches functions that match all of the filters contained
/// in an instance of this class.
class CoverageFiltersMatchAll : public CoverageFilters {
public:
  bool matches(const coverage::CoverageMapping &CM,
               const coverage::FunctionRecord &Function) const override;
};

} // namespace llvm

#endif // LLVM_COV_COVERAGEFILTERS_H
