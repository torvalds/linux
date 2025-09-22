//===- CodeExpander.h - Expand variables in a string ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Expand the variables in a string.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEEXPANDER_H
#define LLVM_UTILS_TABLEGEN_CODEEXPANDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class CodeExpansions;
class SMLoc;
class raw_ostream;

/// Emit the given code with all '${foo}' placeholders expanded to their
/// replacements.
///
/// It's an error to use an undefined expansion and expansion-like output that
/// needs to be emitted verbatim can be escaped as '\${foo}'
///
/// The emitted code can be given a custom indent to enable both indentation by
/// an arbitrary amount of whitespace and emission of the code as a comment.
class CodeExpander {
  StringRef Code;
  const CodeExpansions &Expansions;
  const ArrayRef<SMLoc> &Loc;
  bool ShowExpansions;
  StringRef Indent;

public:
  CodeExpander(StringRef Code, const CodeExpansions &Expansions,
               const ArrayRef<SMLoc> &Loc, bool ShowExpansions,
               StringRef Indent = "    ")
      : Code(Code), Expansions(Expansions), Loc(Loc),
        ShowExpansions(ShowExpansions), Indent(Indent) {}

  void emit(raw_ostream &OS) const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const CodeExpander &Expander) {
  Expander.emit(OS);
  return OS;
}
} // end namespace llvm

#endif // ifndef LLVM_UTILS_TABLEGEN_CODEEXPANDER_H
