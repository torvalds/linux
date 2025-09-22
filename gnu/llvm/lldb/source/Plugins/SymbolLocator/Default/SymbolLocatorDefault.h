//===-- SymbolLocatorDefault.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLLOCATOR_DEFAULT_SYMBOLLOCATORDEFAULT_H
#define LLDB_SOURCE_PLUGINS_SYMBOLLOCATOR_DEFAULT_SYMBOLLOCATORDEFAULT_H

#include "lldb/Symbol/SymbolLocator.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class SymbolLocatorDefault : public SymbolLocator {
public:
  SymbolLocatorDefault();

  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "Default"; }
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

  // Locate the object and symbol file given a module specification.
  //
  // Locating the file can try to download the file from a corporate build
  // repository, or using any other means necessary to locate both the
  // unstripped object file and the debug symbols. The force_lookup argument
  // controls whether the external program is called unconditionally to find
  // the symbol file, or if the user's settings are checked to see if they've
  // enabled the external program before calling.
  static bool DownloadObjectAndSymbolFile(ModuleSpec &module_spec,
                                          Status &error, bool force_lookup,
                                          bool copy_executable);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLLOCATOR_DEFAULT_SYMBOLLOCATORDEFAULT_H
