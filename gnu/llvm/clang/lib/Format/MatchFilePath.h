//===--- MatchFilePath.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_MATCHFILEPATH_H
#define LLVM_CLANG_LIB_FORMAT_MATCHFILEPATH_H

#include "llvm/ADT/StringRef.h"

namespace clang {
namespace format {

bool matchFilePath(llvm::StringRef Pattern, llvm::StringRef FilePath);

} // end namespace format
} // end namespace clang

#endif
