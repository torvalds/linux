//===-- SymbolVendorPECOFF.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_PECOFF_SYMBOLVENDORPECOFF_H
#define LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_PECOFF_SYMBOLVENDORPECOFF_H

#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/lldb-private.h"

class SymbolVendorPECOFF : public lldb_private::SymbolVendor {
public:
  // Constructors and Destructors
  SymbolVendorPECOFF(const lldb::ModuleSP &module_sp);

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "PE-COFF"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::SymbolVendor *
  CreateInstance(const lldb::ModuleSP &module_sp,
                 lldb_private::Stream *feedback_strm);

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_PECOFF_SYMBOLVENDORPECOFF_H
