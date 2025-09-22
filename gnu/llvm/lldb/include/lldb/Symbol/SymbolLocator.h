//===-- SymbolLocator.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_SYMBOLLOCATOR_H
#define LLDB_SYMBOL_SYMBOLLOCATOR_H

#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/UUID.h"

namespace lldb_private {

class SymbolLocator : public PluginInterface {
public:
  SymbolLocator() = default;

  /// Locate the symbol file for the given UUID on a background thread. This
  /// function returns immediately. Under the hood it uses the debugger's
  /// thread pool to call DownloadObjectAndSymbolFile. If a symbol file is
  /// found, this will notify all target which contain the module with the
  /// given UUID.
  static void DownloadSymbolFileAsync(const UUID &uuid);
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_SYMBOLFILELOCATOR_H
