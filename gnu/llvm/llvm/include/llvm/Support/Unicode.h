//===- llvm/Support/Unicode.h - Unicode character properties  -*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that allow querying certain properties of Unicode
// characters.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_UNICODE_H
#define LLVM_SUPPORT_UNICODE_H

#include "llvm/ADT/SmallString.h"
#include <optional>
#include <string>

namespace llvm {
class StringRef;

namespace sys {
namespace unicode {

enum ColumnWidthErrors {
  ErrorInvalidUTF8 = -2,
  ErrorNonPrintableCharacter = -1
};

/// Determines if a character is likely to be displayed correctly on the
/// terminal. Exact implementation would have to depend on the specific
/// terminal, so we define the semantic that should be suitable for generic case
/// of a terminal capable to output Unicode characters.
///
/// Printable codepoints are those in the categories L, M, N, P, S and Zs
/// \return true if the character is considered printable.
bool isPrintable(int UCS);

// Formatting codepoints are codepoints in the Cf category.
bool isFormatting(int UCS);

/// Gets the number of positions the UTF8-encoded \p Text is likely to occupy
/// when output on a terminal ("character width"). This depends on the
/// implementation of the terminal, and there's no standard definition of
/// character width.
///
/// The implementation defines it in a way that is expected to be compatible
/// with a generic Unicode-capable terminal.
///
/// \return Character width:
///   * ErrorNonPrintableCharacter (-1) if \p Text contains non-printable
///     characters (as identified by isPrintable);
///   * 0 for each non-spacing and enclosing combining mark;
///   * 2 for each CJK character excluding halfwidth forms;
///   * 1 for each of the remaining characters.
int columnWidthUTF8(StringRef Text);

/// Fold input unicode character according the Simple unicode case folding
/// rules.
int foldCharSimple(int C);

/// Maps the name or the alias of a Unicode character to its associated
/// codepoints.
/// The names and aliases are derived from UnicodeData.txt and NameAliases.txt
/// For compatibility with the semantics of named character escape sequences in
/// C++, this mapping does an exact match sensitive to casing and spacing.
/// \return The codepoint of the corresponding character, if any.
std::optional<char32_t> nameToCodepointStrict(StringRef Name);

struct LooseMatchingResult {
  char32_t CodePoint;
  SmallString<64> Name;
};

std::optional<LooseMatchingResult> nameToCodepointLooseMatching(StringRef Name);

struct MatchForCodepointName {
  std::string Name;
  uint32_t Distance = 0;
  char32_t Value = 0;
};

SmallVector<MatchForCodepointName>
nearestMatchesForCodepointName(StringRef Pattern, std::size_t MaxMatchesCount);

} // namespace unicode
} // namespace sys
} // namespace llvm

#endif
