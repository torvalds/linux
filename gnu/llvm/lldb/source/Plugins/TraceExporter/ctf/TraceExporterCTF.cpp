//===-- TraceExporterCTF.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TraceExporterCTF.h"

#include <memory>

#include "CommandObjectThreadTraceExportCTF.h"
#include "lldb/Core/PluginManager.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::ctf;
using namespace llvm;

LLDB_PLUGIN_DEFINE(TraceExporterCTF)

//------------------------------------------------------------------
// PluginInterface protocol
//------------------------------------------------------------------

static CommandObjectSP
GetThreadTraceExportCommand(CommandInterpreter &interpreter) {
  return std::make_shared<CommandObjectThreadTraceExportCTF>(interpreter);
}

void TraceExporterCTF::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Chrome Trace Format Exporter", CreateInstance,
                                GetThreadTraceExportCommand);
}

void TraceExporterCTF::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

Expected<TraceExporterUP> TraceExporterCTF::CreateInstance() {
  return std::make_unique<TraceExporterCTF>();
}
