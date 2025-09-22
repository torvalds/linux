//===-- SystemInitializerTest.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemInitializerTest.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/Host.h"
#include "lldb/Initialization/SystemInitializerCommon.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Timer.h"
#include "llvm/Support/TargetSelect.h"

#include <string>

#define LLDB_PLUGIN(p) LLDB_PLUGIN_DECLARE(p)
#include "Plugins/Plugins.def"

using namespace lldb_private;

SystemInitializerTest::SystemInitializerTest()
    : SystemInitializerCommon(nullptr) {}
SystemInitializerTest::~SystemInitializerTest() = default;

llvm::Error SystemInitializerTest::Initialize() {
  if (auto e = SystemInitializerCommon::Initialize())
    return e;

  // Initialize LLVM and Clang
  llvm::InitializeAllTargets();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllDisassemblers();

#define LLDB_SCRIPT_PLUGIN(p)
#define LLDB_PLUGIN(p) LLDB_PLUGIN_INITIALIZE(p);
#include "Plugins/Plugins.def"

  // We ignored all the script interpreter earlier, so initialize
  // ScriptInterpreterNone explicitly.
  LLDB_PLUGIN_INITIALIZE(ScriptInterpreterNone);

  // Scan for any system or user LLDB plug-ins
  PluginManager::Initialize();

  // The process settings need to know about installed plug-ins, so the
  // Settings must be initialized AFTER PluginManager::Initialize is called.
  Debugger::SettingsInitialize();

  return llvm::Error::success();
}

void SystemInitializerTest::Terminate() {
  Debugger::SettingsTerminate();

  // Terminate and unload and loaded system or user LLDB plug-ins
  PluginManager::Terminate();

#define LLDB_SCRIPT_PLUGIN(p)
#define LLDB_PLUGIN(p) LLDB_PLUGIN_TERMINATE(p);
#include "Plugins/Plugins.def"

  // We ignored all the script interpreter earlier, so terminate
  // ScriptInterpreterNone explicitly.
  LLDB_PLUGIN_INITIALIZE(ScriptInterpreterNone);

  // Now shutdown the common parts, in reverse order.
  SystemInitializerCommon::Terminate();
}
