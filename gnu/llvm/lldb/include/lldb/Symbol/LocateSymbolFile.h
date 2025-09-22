//===-- LocateSymbolFile.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_LOCATESYMBOLFILE_H
#define LLDB_SYMBOL_LOCATESYMBOLFILE_H

#include <cstdint>

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-forward.h"

namespace lldb_private {

class ArchSpec;
class ModuleSpec;
class UUID;

class Symbols {
public:
  /// Locate the symbol file for the given UUID on a background thread. This
  /// function returns immediately. Under the hood it uses the debugger's
  /// thread pool to call DownloadObjectAndSymbolFile. If a symbol file is
  /// found, this will notify all target which contain the module with the
  /// given UUID.
  static void DownloadSymbolFileAsync(const UUID &uuid);
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_LOCATESYMBOLFILE_H
