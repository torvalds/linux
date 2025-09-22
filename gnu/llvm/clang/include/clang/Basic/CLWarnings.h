//===--- CLWarnings.h - Maps some cl.exe warning ids  -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_CLWARNINGS_H
#define LLVM_CLANG_BASIC_CLWARNINGS_H

#include <optional>

namespace clang {

namespace diag {
enum class Group;
}

/// For cl.exe warning IDs that cleany map to clang diagnostic groups,
/// returns the corresponding group. Else, returns an empty Optional.
std::optional<diag::Group> diagGroupFromCLWarningID(unsigned);

} // end namespace clang

#endif // LLVM_CLANG_BASIC_CLWARNINGS_H
