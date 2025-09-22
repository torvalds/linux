//===--- MatchFilePath.cpp - Match file path with pattern -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the functionality of matching a file path name to
/// a pattern, similar to the POSIX fnmatch() function.
///
//===----------------------------------------------------------------------===//

#include "MatchFilePath.h"

using namespace llvm;

namespace clang {
namespace format {

// Check whether `FilePath` matches `Pattern` based on POSIX 2.13.1, 2.13.2, and
// Rule 1 of 2.13.3.
bool matchFilePath(StringRef Pattern, StringRef FilePath) {
  assert(!Pattern.empty());
  assert(!FilePath.empty());

  // No match if `Pattern` ends with a non-meta character not equal to the last
  // character of `FilePath`.
  if (const auto C = Pattern.back(); !strchr("?*]", C) && C != FilePath.back())
    return false;

  constexpr auto Separator = '/';
  const auto EOP = Pattern.size();  // End of `Pattern`.
  const auto End = FilePath.size(); // End of `FilePath`.
  unsigned I = 0;                   // Index to `Pattern`.

  for (unsigned J = 0; J < End; ++J) {
    if (I == EOP)
      return false;

    switch (const auto F = FilePath[J]; Pattern[I]) {
    case '\\':
      if (++I == EOP || F != Pattern[I])
        return false;
      break;
    case '?':
      if (F == Separator)
        return false;
      break;
    case '*': {
      while (++I < EOP && Pattern[I] == '*') { // Skip consecutive stars.
      }
      const auto K = FilePath.find(Separator, J); // Index of next `Separator`.
      const bool NoMoreSeparatorsInFilePath = K == StringRef::npos;
      if (I == EOP) // `Pattern` ends with a star.
        return NoMoreSeparatorsInFilePath;
      // `Pattern` ends with a lone backslash.
      if (Pattern[I] == '\\' && ++I == EOP)
        return false;
      // The star is followed by a (possibly escaped) `Separator`.
      if (Pattern[I] == Separator) {
        if (NoMoreSeparatorsInFilePath)
          return false;
        J = K; // Skip to next `Separator` in `FilePath`.
        break;
      }
      // Recurse.
      for (auto Pat = Pattern.substr(I); J < End && FilePath[J] != Separator;
           ++J) {
        if (matchFilePath(Pat, FilePath.substr(J)))
          return true;
      }
      return false;
    }
    case '[':
      // Skip e.g. `[!]`.
      if (I + 3 < EOP || (I + 3 == EOP && Pattern[I + 1] != '!')) {
        // Skip unpaired `[`, brackets containing slashes, and `[]`.
        if (const auto K = Pattern.find_first_of("]/", I + 1);
            K != StringRef::npos && Pattern[K] == ']' && K > I + 1) {
          if (F == Separator)
            return false;
          ++I; // After the `[`.
          bool Negated = false;
          if (Pattern[I] == '!') {
            Negated = true;
            ++I; // After the `!`.
          }
          bool Match = false;
          do {
            if (I + 2 < K && Pattern[I + 1] == '-') {
              Match = Pattern[I] <= F && F <= Pattern[I + 2];
              I += 3; // After the range, e.g. `A-Z`.
            } else {
              Match = F == Pattern[I++];
            }
          } while (!Match && I < K);
          if (Negated ? Match : !Match)
            return false;
          I = K + 1; // After the `]`.
          continue;
        }
      }
      [[fallthrough]]; // Match `[` literally.
    default:
      if (F != Pattern[I])
        return false;
    }

    ++I;
  }

  // Match trailing stars with null strings.
  while (I < EOP && Pattern[I] == '*')
    ++I;

  return I == EOP;
}

} // namespace format
} // namespace clang
