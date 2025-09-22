//===-- NameMatches.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLDB_UTILITY_NAMEMATCHES_H
#define LLDB_UTILITY_NAMEMATCHES_H

#include "llvm/ADT/StringRef.h"

namespace lldb_private {

enum class NameMatch {
  Ignore,
  Equals,
  Contains,
  StartsWith,
  EndsWith,
  RegularExpression
};

bool NameMatches(llvm::StringRef name, NameMatch match_type,
                 llvm::StringRef match);
}

#endif
