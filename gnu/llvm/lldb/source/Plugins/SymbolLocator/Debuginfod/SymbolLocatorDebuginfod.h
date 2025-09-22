//===-- SymbolLocatorDebuginfod.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLLOCATOR_DEBUGINFOD_SYMBOLLOCATORDEBUGINFOD_H
#define LLDB_SOURCE_PLUGINS_SYMBOLLOCATOR_DEBUGINFOD_SYMBOLLOCATORDEBUGINFOD_H

#include "lldb/Core/Debugger.h"
#include "lldb/Symbol/SymbolLocator.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class SymbolLocatorDebuginfod : public SymbolLocator {
public:
  SymbolLocatorDebuginfod();

  static void Initialize();
  static void Terminate();
  static void DebuggerInitialize(Debugger &debugger);

  static llvm::StringRef GetPluginNameStatic() { return "debuginfod"; }
  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::SymbolLocator *CreateInstance();

  /// PluginInterface protocol.
  /// \{
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
  /// \}

  // Locate the executable file given a module specification.
  //
  // Locating the file should happen only on the local computer or using the
  // current computers global settings.
  static std::optional<ModuleSpec>
  LocateExecutableObjectFile(const ModuleSpec &module_spec);

  // Locate the symbol file given a module specification.
  //
  // Locating the file should happen only on the local computer or using the
  // current computers global settings.
  static std::optional<FileSpec>
  LocateExecutableSymbolFile(const ModuleSpec &module_spec,
                             const FileSpecList &default_search_paths);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLLOCATOR_DEBUGINFOD_SYMBOLLOCATORDEBUGINFOD_H
