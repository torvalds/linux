//===-- SymbolLocation.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_SYMBOLLOCATION_H
#define LLDB_SYMBOL_SYMBOLLOCATION_H

#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-private.h"

#include <vector>

namespace lldb_private {

/// Stores a function module spec, symbol name and possibly an alternate symbol
/// name.
struct SymbolLocation {
  FileSpec module_spec;
  std::vector<ConstString> symbols;

  // The symbols are regular expressions. In such case all symbols are matched
  // with their trailing @VER symbol version stripped.
  bool symbols_are_regex = false;
};

} // namespace lldb_private
#endif // LLDB_SYMBOL_SYMBOLLOCATION_H
