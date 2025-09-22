//===--- SortJavaScriptImports.h - Sort ES6 Imports -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a sorter for JavaScript ES6 imports.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_SORTJAVASCRIPTIMPORTS_H
#define LLVM_CLANG_LIB_FORMAT_SORTJAVASCRIPTIMPORTS_H

#include "clang/Format/Format.h"

namespace clang {
namespace format {

// Sort JavaScript ES6 imports/exports in ``Code``. The generated replacements
// only monotonically increase the length of the given code.
tooling::Replacements sortJavaScriptImports(const FormatStyle &Style,
                                            StringRef Code,
                                            ArrayRef<tooling::Range> Ranges,
                                            StringRef FileName);

} // end namespace format
} // end namespace clang

#endif
