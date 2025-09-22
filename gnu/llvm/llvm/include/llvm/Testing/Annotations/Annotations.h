//===--- Annotations.h - Annotated source code for tests ---------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TESTING_SUPPORT_ANNOTATIONS_H
#define LLVM_TESTING_SUPPORT_ANNOTATIONS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <tuple>
#include <vector>

namespace llvm {

class raw_ostream;

/// Annotations lets you mark points and ranges inside source code, for tests:
///
///    Annotations Example(R"cpp(
///       int complete() { x.pri^ }         // ^ indicates a point
///       void err() { [["hello" == 42]]; } // [[this is a range]]
///       $definition^class Foo{};          // points can be named: "definition"
///       $(foo)^class Foo{};               // ...or have a payload: "foo"
///       $definition(foo)^class Foo{};     // ...or both
///       $fail(runtime)[[assert(false)]]   // ranges can have names/payloads too
///    )cpp");
///
///    StringRef Code = Example.code();             // annotations stripped.
///    std::vector<size_t> PP = Example.points();   // all unnamed points
///    size_t P = Example.point();                  // there must be exactly one
///    llvm::Range R = Example.range("fail");       // find named ranges
///
/// Points/ranges are coordinated into `code()` which is stripped of
/// annotations.
///
/// Names consist of only alphanumeric characters or '_'.
/// Payloads can contain any character expect '(' and ')'.
///
/// Ranges may be nested (and points can be inside ranges), but there's no way
/// to define general overlapping ranges.
///
/// FIXME: the choice of the marking syntax makes it impossible to represent
///        some of the C++ and Objective C constructs (including common ones
///        like C++ attributes). We can fix this by:
///          1. introducing an escaping mechanism for the special characters,
///          2. making characters for marking points and ranges configurable,
///          3. changing the syntax to something less commonly used,
///          4. ...
class Annotations {
public:
  /// Two offsets pointing to a continuous substring. End is not included, i.e.
  /// represents a half-open range.
  struct Range {
    size_t Begin = 0;
    size_t End = 0;

    friend bool operator==(const Range &L, const Range &R) {
      return std::tie(L.Begin, L.End) == std::tie(R.Begin, R.End);
    }
    friend bool operator!=(const Range &L, const Range &R) { return !(L == R); }
  };

  /// Parses the annotations from Text. Crashes if it's malformed.
  Annotations(llvm::StringRef Text);

  /// The input text with all annotations stripped.
  /// All points and ranges are relative to this stripped text.
  llvm::StringRef code() const { return Code; }

  /// Returns the position of the point marked by ^ (or $name^) in the text.
  /// Crashes if there isn't exactly one.
  size_t point(llvm::StringRef Name = "") const;
  /// Returns the position of the point with \p Name and its payload (if any).
  std::pair<size_t, llvm::StringRef>
  pointWithPayload(llvm::StringRef Name = "") const;
  /// Returns the position of all points marked by ^ (or $name^) in the text.
  /// Order matches the order within the text.
  std::vector<size_t> points(llvm::StringRef Name = "") const;
  /// Returns the positions and payloads (if any) of all points named \p Name
  std::vector<std::pair<size_t, llvm::StringRef>>
  pointsWithPayload(llvm::StringRef Name = "") const;
  /// Returns the mapping of all names of points marked in the text to their
  /// position. Unnamed points are mapped to the empty string. The positions are
  /// sorted.
  /// FIXME Remove this and expose `All` directly (currently used out-of-tree)
  llvm::StringMap<llvm::SmallVector<size_t, 1>> all_points() const;

  /// Returns the location of the range marked by [[ ]] (or $name[[ ]]).
  /// Crashes if there isn't exactly one.
  Range range(llvm::StringRef Name = "") const;
  /// Returns the location and payload of the range marked by [[ ]]
  /// (or $name(payload)[[ ]]). Crashes if there isn't exactly one.
  std::pair<Range, llvm::StringRef>
  rangeWithPayload(llvm::StringRef Name = "") const;
  /// Returns the location of all ranges marked by [[ ]] (or $name[[ ]]).
  /// They are ordered by start position within the text.
  std::vector<Range> ranges(llvm::StringRef Name = "") const;
  /// Returns the location of all ranges marked by [[ ]]
  /// (or $name(payload)[[ ]]).
  /// They are ordered by start position within the text.
  std::vector<std::pair<Range, llvm::StringRef>>
  rangesWithPayload(llvm::StringRef Name = "") const;
  /// Returns the mapping of all names of ranges marked in the text to their
  /// location. Unnamed ranges are mapped to the empty string. The ranges are
  /// sorted by their start position.
  llvm::StringMap<llvm::SmallVector<Range, 1>> all_ranges() const;

private:
  std::string Code;
  /// Either a Point (Only Start) or a Range (Start and End)
  struct Annotation {
    size_t Begin;
    size_t End = -1;
    bool isPoint() const { return End == size_t(-1); }
    llvm::StringRef Name;
    llvm::StringRef Payload;
  };
  std::vector<Annotation> All;
  // Values are the indices into All
  llvm::StringMap<llvm::SmallVector<size_t, 1>> Points;
  llvm::StringMap<llvm::SmallVector<size_t, 1>> Ranges;
};

llvm::raw_ostream &operator<<(llvm::raw_ostream &O,
                              const llvm::Annotations::Range &R);

} // namespace llvm

#endif
