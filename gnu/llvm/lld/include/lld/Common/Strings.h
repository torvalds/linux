//===- Strings.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_STRINGS_H
#define LLD_STRINGS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/GlobPattern.h"
#include <string>
#include <vector>

namespace lld {

llvm::SmallVector<uint8_t, 0> parseHex(llvm::StringRef s);
bool isValidCIdentifier(llvm::StringRef s);

// Write the contents of the a buffer to a file
void saveBuffer(llvm::StringRef buffer, const llvm::Twine &path);

// A single pattern to match against. A pattern can either be double-quoted
// text that should be matched exactly after removing the quoting marks or a
// glob pattern in the sense of GlobPattern.
class SingleStringMatcher {
public:
  // Create a StringPattern from Pattern to be matched exactly regardless
  // of globbing characters if ExactMatch is true.
  SingleStringMatcher(llvm::StringRef Pattern);

  // Match s against this pattern, exactly if ExactMatch is true.
  bool match(llvm::StringRef s) const;

  // Returns true for pattern "*" which will match all inputs.
  bool isTrivialMatchAll() const {
    return !ExactMatch && GlobPatternMatcher.isTrivialMatchAll();
  }

private:
  // Whether to do an exact match regardless of wildcard characters.
  bool ExactMatch;

  // GlobPattern object if not doing an exact match.
  llvm::GlobPattern GlobPatternMatcher;

  // StringRef to match exactly if doing an exact match.
  llvm::StringRef ExactPattern;
};

// This class represents multiple patterns to match against. A pattern can
// either be a double-quoted text that should be matched exactly after removing
// the quoted marks or a glob pattern.
class StringMatcher {
private:
  // Patterns to match against.
  std::vector<SingleStringMatcher> patterns;

public:
  StringMatcher() = default;

  // Matcher for a single pattern.
  StringMatcher(llvm::StringRef Pattern)
      : patterns({SingleStringMatcher(Pattern)}) {}

  // Add a new pattern to the existing ones to match against.
  void addPattern(SingleStringMatcher Matcher) { patterns.push_back(Matcher); }

  bool empty() const { return patterns.empty(); }

  // Match s against the patterns.
  bool match(llvm::StringRef s) const;
};

} // namespace lld

#endif
