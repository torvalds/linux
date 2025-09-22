//===-- GlobPattern.h - glob pattern matcher implementation -*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a glob pattern matcher.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GLOBPATTERN_H
#define LLVM_SUPPORT_GLOBPATTERN_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include <optional>

namespace llvm {

/// This class implements a glob pattern matcher similar to the one found in
/// bash, but with some key differences. Namely, that \p "*" matches all
/// characters and does not exclude path separators.
///
/// * \p "?" matches a single character.
/// * \p "*" matches zero or more characters.
/// * \p "[<chars>]" matches one character in the bracket. Character ranges,
///   e.g., \p "[a-z]", and negative sets via \p "[^ab]" or \p "[!ab]" are also
///   supported.
/// * \p "{<glob>,...}" matches one of the globs in the list. Nested brace
///   expansions are not supported. If \p MaxSubPatterns is empty then
///   brace expansions are not supported and characters \p "{,}" are treated as
///   literals.
/// * \p "\" escapes the next character so it is treated as a literal.
///
///
/// Some known edge cases are:
/// * \p "]" is allowed as the first character in a character class, i.e.,
///   \p "[]]" is valid and matches the literal \p "]".
/// * The empty character class, i.e., \p "[]", is invalid.
/// * Empty or singleton brace expansions, e.g., \p "{}", \p "{a}", are invalid.
/// * \p "}" and \p "," that are not inside a brace expansion are taken as
///   literals, e.g., \p ",}" is valid but \p "{" is not.
///
///
/// For example, \p "*[/\\]foo.{c,cpp}" will match (unix or windows) paths to
/// all files named \p "foo.c" or \p "foo.cpp".
class GlobPattern {
public:
  /// \param Pat the pattern to match against
  /// \param MaxSubPatterns if provided limit the number of allowed subpatterns
  ///                       created from expanding braces otherwise disable
  ///                       brace expansion
  static Expected<GlobPattern>
  create(StringRef Pat, std::optional<size_t> MaxSubPatterns = {});
  /// \returns \p true if \p S matches this glob pattern
  bool match(StringRef S) const;

  // Returns true for glob pattern "*". Can be used to avoid expensive
  // preparation/acquisition of the input for match().
  bool isTrivialMatchAll() const {
    if (!Prefix.empty())
      return false;
    if (SubGlobs.size() != 1)
      return false;
    return SubGlobs[0].getPat() == "*";
  }

private:
  StringRef Prefix;

  struct SubGlobPattern {
    /// \param Pat the pattern to match against
    static Expected<SubGlobPattern> create(StringRef Pat);
    /// \returns \p true if \p S matches this glob pattern
    bool match(StringRef S) const;
    StringRef getPat() const { return StringRef(Pat.data(), Pat.size()); }

    // Brackets with their end position and matched bytes.
    struct Bracket {
      size_t NextOffset;
      BitVector Bytes;
    };
    SmallVector<Bracket, 0> Brackets;
    SmallVector<char, 0> Pat;
  };
  SmallVector<SubGlobPattern, 1> SubGlobs;
};
}

#endif // LLVM_SUPPORT_GLOBPATTERN_H
