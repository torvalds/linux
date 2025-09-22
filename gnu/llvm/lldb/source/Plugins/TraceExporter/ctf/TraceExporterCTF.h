//===-- TraceExporterCTF.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TRACE_EXPORTER_CTF_H
#define LLDB_SOURCE_PLUGINS_TRACE_EXPORTER_CTF_H

#include "lldb/Target/TraceExporter.h"

namespace lldb_private {
namespace ctf {

/// Trace Exporter Plugin that can produce traces in Chrome Trace Format.
/// Still in development.
class TraceExporterCTF : public TraceExporter {
public:
  ~TraceExporterCTF() override = default;

  /// PluginInterface protocol
  /// \{
  static llvm::Expected<lldb::TraceExporterUP> CreateInstance();

  llvm::StringRef GetPluginName() override {
    return GetPluginNameStatic();
  }

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "ctf"; }
  /// \}
};

} // namespace ctf
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TRACE_EXPORTER_CTF_H
