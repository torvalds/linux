//===--- LoopHint.h - Types for LoopHint ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_PARSE_LOOPHINT_H
#define LLVM_CLANG_PARSE_LOOPHINT_H

#include "clang/Basic/SourceLocation.h"

namespace clang {

class Expr;
struct IdentifierLoc;

/// Loop optimization hint for loop and unroll pragmas.
struct LoopHint {
  // Source range of the directive.
  SourceRange Range;
  // Identifier corresponding to the name of the pragma.  "loop" for
  // "#pragma clang loop" directives and "unroll" for "#pragma unroll"
  // hints.
  IdentifierLoc *PragmaNameLoc = nullptr;
  // Name of the loop hint.  Examples: "unroll", "vectorize".  In the
  // "#pragma unroll" and "#pragma nounroll" cases, this is identical to
  // PragmaNameLoc.
  IdentifierLoc *OptionLoc = nullptr;
  // Identifier for the hint state argument.  If null, then the state is
  // default value such as for "#pragma unroll".
  IdentifierLoc *StateLoc = nullptr;
  // Expression for the hint argument if it exists, null otherwise.
  Expr *ValueExpr = nullptr;

  LoopHint() = default;
};

} // end namespace clang

#endif // LLVM_CLANG_PARSE_LOOPHINT_H
