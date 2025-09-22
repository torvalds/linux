//===-- SymbolVendorWasm.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_WASM_SYMBOLVENDORWASM_H
#define LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_WASM_SYMBOLVENDORWASM_H

#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/lldb-private.h"

namespace lldb_private {
namespace wasm {

class SymbolVendorWasm : public lldb_private::SymbolVendor {
public:
  SymbolVendorWasm(const lldb::ModuleSP &module_sp);

  static void Initialize();
  static void Terminate();
  static llvm::StringRef GetPluginNameStatic() { return "WASM"; }
  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::SymbolVendor *
  CreateInstance(const lldb::ModuleSP &module_sp,
                 lldb_private::Stream *feedback_strm);

  /// PluginInterface protocol.
  /// \{
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
  /// \}
};

} // namespace wasm
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLVENDOR_WASM_SYMBOLVENDORWASM_H
