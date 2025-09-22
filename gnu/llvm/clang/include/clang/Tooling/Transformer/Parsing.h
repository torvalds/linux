//===--- Parsing.h - Parsing library for Transformer ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
///  \file
///  Defines parsing functions for Transformer types.
///  FIXME: Currently, only supports `RangeSelectors` but parsers for other
///  Transformer types are under development.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_TRANSFORMER_PARSING_H
#define LLVM_CLANG_TOOLING_TRANSFORMER_PARSING_H

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Tooling/Transformer/RangeSelector.h"
#include "llvm/Support/Error.h"
#include <functional>

namespace clang {
namespace transformer {

/// Parses a string representation of a \c RangeSelector. The grammar of these
/// strings is closely based on the (sub)grammar of \c RangeSelectors as they'd
/// appear in C++ code. However, this language constrains the set of permissible
/// strings (for node ids) -- it does not support escapes in the
/// string. Additionally, the \c charRange combinator is not supported, because
/// there is no representation of values of type \c CharSourceRange in this
/// (little) language.
llvm::Expected<RangeSelector> parseRangeSelector(llvm::StringRef Input);

} // namespace transformer
} // namespace clang

#endif // LLVM_CLANG_TOOLING_TRANSFORMER_PARSING_H
