//===-- SymbolVendorMacOSX.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_MACOSX_SYMBOLVENDORMACOSX_H
#define LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_MACOSX_SYMBOLVENDORMACOSX_H

#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/lldb-private.h"

class SymbolVendorMacOSX : public lldb_private::SymbolVendor {
public:
  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "macosx"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::SymbolVendor *
  CreateInstance(const lldb::ModuleSP &module_sp,
                 lldb_private::Stream *feedback_strm);

  // Constructors and Destructors
  SymbolVendorMacOSX(const lldb::ModuleSP &module_sp);

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_MACOSX_SYMBOLVENDORMACOSX_H
