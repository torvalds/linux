//===-- TraceExporter.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/TraceExporter.h"

#include "lldb/Core/PluginManager.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

static Error createInvalidPlugInError(StringRef plugin_name) {
  return createStringError(
      std::errc::invalid_argument,
      "no trace expoter plug-in matches the specified type: \"%s\"",
      plugin_name.data());
}

Expected<lldb::TraceExporterUP>
TraceExporter::FindPlugin(llvm::StringRef name) {
  if (auto create_callback =
          PluginManager::GetTraceExporterCreateCallback(name))
    return create_callback();

  return createInvalidPlugInError(name);
}
