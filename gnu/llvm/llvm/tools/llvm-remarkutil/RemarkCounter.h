//===- RemarkCounter.h ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic tool to count remarks based on properties
//
//===----------------------------------------------------------------------===//
#ifndef TOOLS_LLVM_REMARKCOUNTER_H
#define TOOLS_LLVM_REMARKCOUNTER_H
#include "RemarkUtilHelpers.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Support/Regex.h"

namespace llvm {
namespace remarks {

/// Collect remarks by counting the existance of a remark or by looking through
/// the keys and summing through the total count.
enum class CountBy { REMARK, ARGUMENT };

/// Summarize the count by either emitting one count for the remark file, or
/// grouping the count by source file or by function name.
enum class GroupBy {
  TOTAL,
  PER_SOURCE,
  PER_FUNCTION,
  PER_FUNCTION_WITH_DEBUG_LOC
};

/// Convert \p GroupBy to a std::string.
inline std::string groupByToStr(GroupBy GroupBy) {
  switch (GroupBy) {
  default:
    return "Total";
  case GroupBy::PER_FUNCTION:
    return "Function";
  case GroupBy::PER_FUNCTION_WITH_DEBUG_LOC:
    return "FuctionWithDebugLoc";
  case GroupBy::PER_SOURCE:
    return "Source";
  }
}

/// Filter object which can be either a string or a regex to match with the
/// remark properties.
struct FilterMatcher {
  Regex FilterRE;
  std::string FilterStr;
  bool IsRegex;
  FilterMatcher(std::string Filter, bool IsRegex) : IsRegex(IsRegex) {
    if (IsRegex)
      FilterRE = Regex(Filter);
    else
      FilterStr = Filter;
  }

  bool match(StringRef StringToMatch) const {
    if (IsRegex)
      return FilterRE.match(StringToMatch);
    return FilterStr == StringToMatch.trim().str();
  }
};

/// Filter out remarks based on remark properties based on name, pass name,
/// argument and type.
struct Filters {
  std::optional<FilterMatcher> RemarkNameFilter;
  std::optional<FilterMatcher> PassNameFilter;
  std::optional<FilterMatcher> ArgFilter;
  std::optional<Type> RemarkTypeFilter;
  /// Returns a filter object if all the arguments provided are valid regex
  /// types otherwise return an error.
  static Expected<Filters>
  createRemarkFilter(std::optional<FilterMatcher> RemarkNameFilter,
                     std::optional<FilterMatcher> PassNameFilter,
                     std::optional<FilterMatcher> ArgFilter,
                     std::optional<Type> RemarkTypeFilter) {
    Filters Filter;
    Filter.RemarkNameFilter = std::move(RemarkNameFilter);
    Filter.PassNameFilter = std::move(PassNameFilter);
    Filter.ArgFilter = std::move(ArgFilter);
    Filter.RemarkTypeFilter = std::move(RemarkTypeFilter);
    if (auto E = Filter.regexArgumentsValid())
      return std::move(E);
    return std::move(Filter);
  }
  /// Returns true if \p Remark satisfies all the provided filters.
  bool filterRemark(const Remark &Remark);

private:
  /// Check if arguments can be parsed as valid regex types.
  Error regexArgumentsValid();
};

/// Convert Regex string error to an error object.
inline Error checkRegex(const Regex &Regex) {
  std::string Error;
  if (!Regex.isValid(Error))
    return createStringError(make_error_code(std::errc::invalid_argument),
                             Twine("Regex: ", Error));
  return Error::success();
}

/// Abstract counter class used to define the general required methods for
/// counting a remark.
struct Counter {
  GroupBy Group = GroupBy::TOTAL;
  Counter() = default;
  Counter(enum GroupBy GroupBy) : Group(GroupBy) {}
  /// Obtain the field for collecting remark info based on how we are
  /// collecting. Remarks are grouped by FunctionName, Source, Source and
  /// Function or collect by file.
  std::optional<std::string> getGroupByKey(const Remark &Remark);

  /// Collect count information from \p Remark organized based on \p Group
  /// property.
  virtual void collect(const Remark &) = 0;
  /// Output the final count to the file \p OutputFileName
  virtual Error print(StringRef OutputFileName) = 0;
  virtual ~Counter() = default;
};

/// Count remarks based on the provided \p Keys argument and summing up the
/// value for each matching key organized by source, function or reporting a
/// total for the specified remark file.
/// Reporting count grouped by source:
///
///  | source        | key1 | key2 | key3 |
///  |---------------|------|------|------|
///  | path/to/file1 | 0    | 1    | 3    |
///  | path/to/file2 | 1    | 0    | 2    |
///  | path/to/file3 | 2    | 3    | 1    |
///
/// Reporting count grouped by function:
///
///  | Function      | key1 | key2 | key3 |
///  |---------------|------|------|------|
///  | function1     | 0    | 1    | 3    |
///  | function2     | 1    | 0    | 2    |
///  | function3     | 2    | 3    | 1    |
struct ArgumentCounter : Counter {
  /// The internal object to keep the count for the remarks. The first argument
  /// corresponds to the property we are collecting for this can be either a
  /// source or function. The second argument is a row of integers where each
  /// item in the row is the count for a specified key.
  std::map<std::string, SmallVector<unsigned, 4>> CountByKeysMap;
  /// A set of all the remark argument found in the remark file. The second
  /// argument is the index of each of those arguments which can be used in
  /// `CountByKeysMap` to fill count information for that argument.
  MapVector<StringRef, unsigned> ArgumentSetIdxMap;
  /// Create an argument counter. If the provided \p Arguments represent a regex
  /// vector then we need to check that the provided regular expressions are
  /// valid if not we return an Error.
  static Expected<ArgumentCounter>
  createArgumentCounter(GroupBy Group, ArrayRef<FilterMatcher> Arguments,
                        StringRef Buffer, Filters &Filter) {
    ArgumentCounter AC;
    AC.Group = Group;
    for (auto &Arg : Arguments) {
      if (Arg.IsRegex) {
        if (auto E = checkRegex(Arg.FilterRE))
          return std::move(E);
      }
    }
    if (auto E = AC.getAllMatchingArgumentsInRemark(Buffer, Arguments, Filter))
      return std::move(E);
    return AC;
  }

  /// Update the internal count map based on the remark integer arguments that
  /// correspond the the user specified argument keys to collect for.
  void collect(const Remark &) override;

  /// Print a CSV table consisting of an index which is specified by \p
  /// `Group` and can be a function name, source file name or function name
  /// with the full source path and columns of user specified remark arguments
  /// to collect the count for.
  Error print(StringRef OutputFileName) override;

private:
  /// collect all the arguments that match the list of \p Arguments provided by
  /// parsing through \p Buffer of remarks and filling \p ArgumentSetIdxMap
  /// acting as a row for for all the keys that we are interested in collecting
  /// information for.
  Error getAllMatchingArgumentsInRemark(StringRef Buffer,
                                        ArrayRef<FilterMatcher> Arguments,
                                        Filters &Filter);
};

/// Collect remarks based by counting the existance of individual remarks. The
/// reported table will be structured based on the provided \p Group argument
/// by reporting count for functions, source or total count for the provided
/// remark file.
struct RemarkCounter : Counter {
  std::map<std::string, unsigned> CountedByRemarksMap;
  RemarkCounter(GroupBy Group) : Counter(Group) {}

  /// Advance the internal map count broken by \p Group when
  /// seeing \p Remark.
  void collect(const Remark &) override;

  /// Print a CSV table consisting of an index which is specified by \p
  /// `Group` and can be a function name, source file name or function name
  /// with the full source path and a counts column corresponding to the count
  /// of each individual remark at th index.
  Error print(StringRef OutputFileName) override;
};
} // namespace remarks

} // namespace llvm
#endif // TOOLS_LLVM_REMARKCOUNTER_H
