//===- CodeExpansions.h - Record expansions for CodeExpander --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Record the expansions to use in a CodeExpander.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringMap.h"

#ifndef LLVM_UTILS_TABLEGEN_CODEEXPANSIONS_H
#define LLVM_UTILS_TABLEGEN_CODEEXPANSIONS_H
namespace llvm {
class CodeExpansions {
public:
  using const_iterator = StringMap<std::string>::const_iterator;

protected:
  StringMap<std::string> Expansions;

public:
  void declare(StringRef Name, StringRef Expansion) {
    // Duplicates are not inserted. The expansion refers to different
    // MachineOperands using the same virtual register.
    Expansions.try_emplace(Name, Expansion);
  }

  void redeclare(StringRef Name, StringRef Expansion) {
    Expansions[Name] = Expansion;
  }

  std::string lookup(StringRef Variable) const {
    return Expansions.lookup(Variable);
  }

  const_iterator begin() const { return Expansions.begin(); }
  const_iterator end() const { return Expansions.end(); }
  const_iterator find(StringRef Variable) const {
    return Expansions.find(Variable);
  }
};
} // end namespace llvm
#endif // ifndef LLVM_UTILS_TABLEGEN_CODEEXPANSIONS_H
