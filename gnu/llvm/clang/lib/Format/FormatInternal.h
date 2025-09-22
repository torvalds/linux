//===--- FormatInternal.h - Format C++ code ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares Format APIs to be used internally by the
/// formatting library implementation.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_FORMATINTERNAL_H
#define LLVM_CLANG_LIB_FORMAT_FORMATINTERNAL_H

#include "clang/Basic/LLVM.h"
#include "clang/Format/Format.h"
#include "clang/Tooling/Core/Replacement.h"
#include <utility>

namespace clang {
namespace format {
namespace internal {

/// Reformats the given \p Ranges in the code fragment \p Code.
///
/// A fragment of code could conceptually be surrounded by other code that might
/// constrain how that fragment is laid out.
/// For example, consider the fragment of code between 'R"(' and ')"',
/// exclusive, in the following code:
///
/// void outer(int x) {
///   string inner = R"(name: data
///                     ^ FirstStartColumn
///     value: {
///       x: 1
///     ^ NextStartColumn
///     }
///   )";
///   ^ LastStartColumn
/// }
///
/// The outer code can influence the inner fragment as follows:
///   * \p FirstStartColumn specifies the column at which \p Code starts.
///   * \p NextStartColumn specifies the additional indent dictated by the
///     surrounding code. It is applied to the rest of the lines of \p Code.
///   * \p LastStartColumn specifies the column at which the last line of
///     \p Code should end, in case the last line is an empty line.
///
///     In the case where the last line of the fragment contains content,
///     the fragment ends at the end of that content and \p LastStartColumn is
///     not taken into account, for example in:
///
///     void block() {
///       string inner = R"(name: value)";
///     }
///
/// Each range is extended on either end to its next bigger logic unit, i.e.
/// everything that might influence its formatting or might be influenced by its
/// formatting.
///
/// Returns a pair P, where:
///   * P.first are the ``Replacements`` necessary to make all \p Ranges comply
///     with \p Style.
///   * P.second is the penalty induced by formatting the fragment \p Code.
///     If the formatting of the fragment doesn't have a notion of penalty,
///     returns 0.
///
/// If ``Status`` is non-null, its value will be populated with the status of
/// this formatting attempt. See \c FormattingAttemptStatus.
std::pair<tooling::Replacements, unsigned>
reformat(const FormatStyle &Style, StringRef Code,
         ArrayRef<tooling::Range> Ranges, unsigned FirstStartColumn,
         unsigned NextStartColumn, unsigned LastStartColumn, StringRef FileName,
         FormattingAttemptStatus *Status);

} // namespace internal
} // namespace format
} // namespace clang

#endif
