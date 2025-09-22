//===-- ObjectFileMinidump.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ObjectFileMinidump.h"

#include "MinidumpFileBuilder.h"

#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "llvm/Support/FileSystem.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ObjectFileMinidump)

void ObjectFileMinidump::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), GetPluginDescriptionStatic(), CreateInstance,
      CreateMemoryInstance, GetModuleSpecifications, SaveCore);
}

void ObjectFileMinidump::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ObjectFile *ObjectFileMinidump::CreateInstance(
    const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
    lldb::offset_t data_offset, const lldb_private::FileSpec *file,
    lldb::offset_t offset, lldb::offset_t length) {
  return nullptr;
}

ObjectFile *ObjectFileMinidump::CreateMemoryInstance(
    const lldb::ModuleSP &module_sp, WritableDataBufferSP data_sp,
    const ProcessSP &process_sp, lldb::addr_t header_addr) {
  return nullptr;
}

size_t ObjectFileMinidump::GetModuleSpecifications(
    const lldb_private::FileSpec &file, lldb::DataBufferSP &data_sp,
    lldb::offset_t data_offset, lldb::offset_t file_offset,
    lldb::offset_t length, lldb_private::ModuleSpecList &specs) {
  specs.Clear();
  return 0;
}

bool ObjectFileMinidump::SaveCore(const lldb::ProcessSP &process_sp,
                                  const lldb_private::SaveCoreOptions &options,
                                  lldb_private::Status &error) {
  // Output file and process_sp are both checked in PluginManager::SaveCore.
  assert(options.GetOutputFile().has_value());
  assert(process_sp);

  // Minidump defaults to stacks only.
  SaveCoreStyle core_style = options.GetStyle();
  if (core_style == SaveCoreStyle::eSaveCoreUnspecified)
    core_style = SaveCoreStyle::eSaveCoreStackOnly;

  llvm::Expected<lldb::FileUP> maybe_core_file = FileSystem::Instance().Open(
      options.GetOutputFile().value(),
      File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate);
  if (!maybe_core_file) {
    error = maybe_core_file.takeError();
    return false;
  }
  MinidumpFileBuilder builder(std::move(maybe_core_file.get()), process_sp);

  Log *log = GetLog(LLDBLog::Object);
  error = builder.AddHeaderAndCalculateDirectories();
  if (error.Fail()) {
    LLDB_LOGF(log, "AddHeaderAndCalculateDirectories failed: %s",
              error.AsCString());
    return false;
  };
  error = builder.AddSystemInfo();
  if (error.Fail()) {
    LLDB_LOGF(log, "AddSystemInfo failed: %s", error.AsCString());
    return false;
  }

  error = builder.AddModuleList();
  if (error.Fail()) {
    LLDB_LOGF(log, "AddModuleList failed: %s", error.AsCString());
    return false;
  }
  error = builder.AddMiscInfo();
  if (error.Fail()) {
    LLDB_LOGF(log, "AddMiscInfo failed: %s", error.AsCString());
    return false;
  }

  error = builder.AddThreadList();
  if (error.Fail()) {
    LLDB_LOGF(log, "AddThreadList failed: %s", error.AsCString());
    return false;
  }

  error = builder.AddLinuxFileStreams();
  if (error.Fail()) {
    LLDB_LOGF(log, "AddLinuxFileStreams failed: %s", error.AsCString());
    return false;
  }

  // Add any exceptions but only if there are any in any threads.
  error = builder.AddExceptions();
  if (error.Fail()) {
    LLDB_LOGF(log, "AddExceptions failed: %s", error.AsCString());
    return false;
  }

  // Note: add memory HAS to be the last thing we do. It can overflow into 64b
  // land and many RVA's only support 32b
  error = builder.AddMemoryList(core_style);
  if (error.Fail()) {
    LLDB_LOGF(log, "AddMemoryList failed: %s", error.AsCString());
    return false;
  }

  error = builder.DumpFile();
  if (error.Fail()) {
    LLDB_LOGF(log, "DumpFile failed: %s", error.AsCString());
    return false;
  }

  return true;
}
