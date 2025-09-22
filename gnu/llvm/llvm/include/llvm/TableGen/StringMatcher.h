//===- StringMatcher.h - Generate a matcher for input strings ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the StringMatcher class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TABLEGEN_STRINGMATCHER_H
#define LLVM_TABLEGEN_STRINGMATCHER_H

#include "llvm/ADT/StringRef.h"
#include <string>
#include <utility>
#include <vector>

namespace llvm {

class raw_ostream;

/// Given a list of strings and code to execute when they match, output a
/// simple switch tree to classify the input string.
///
/// If a match is found, the code in Matches[i].second is executed; control must
/// not exit this code fragment.  If nothing matches, execution falls through.
class StringMatcher {
public:
  using StringPair = std::pair<std::string, std::string>;

private:
  StringRef StrVariableName;
  const std::vector<StringPair> &Matches;
  raw_ostream &OS;

public:
  StringMatcher(StringRef strVariableName,
                const std::vector<StringPair> &matches, raw_ostream &os)
    : StrVariableName(strVariableName), Matches(matches), OS(os) {}

  void Emit(unsigned Indent = 0, bool IgnoreDuplicates = false) const;

private:
  bool EmitStringMatcherForChar(const std::vector<const StringPair *> &Matches,
                                unsigned CharNo, unsigned IndentCount,
                                bool IgnoreDuplicates) const;
};

} // end namespace llvm

#endif // LLVM_TABLEGEN_STRINGMATCHER_H
