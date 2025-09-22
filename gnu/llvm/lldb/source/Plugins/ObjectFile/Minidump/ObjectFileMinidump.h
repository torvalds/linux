//===-- ObjectFileMinidump.h ---------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Placeholder plugin for the save core functionality.
///
/// ObjectFileMinidump is created only to be able to save minidump core files
/// from existing processes with the ObjectFileMinidump::SaveCore function.
/// Minidump files are not ObjectFile objects, but they are core files and
/// currently LLDB's ObjectFile plug-ins handle emitting core files. If the
/// core file saving ever moves into a new plug-in type within LLDB, this code
/// should move as well, but for now this is the best place architecturally.
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_MINIDUMP_OBJECTFILEMINIDUMP_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_MINIDUMP_OBJECTFILEMINIDUMP_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"

class ObjectFileMinidump : public lldb_private::PluginInterface {
public:
  // Static Functions
  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "minidump"; }
  static const char *GetPluginDescriptionStatic() {
    return "Minidump object file.";
  }

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  static lldb_private::ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length);

  static lldb_private::ObjectFile *CreateMemoryInstance(
      const lldb::ModuleSP &module_sp, lldb::WritableDataBufferSP data_sp,
      const lldb::ProcessSP &process_sp, lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  // Saves dump in Minidump file format
  static bool SaveCore(const lldb::ProcessSP &process_sp,
                       const lldb_private::SaveCoreOptions &options,
                       lldb_private::Status &error);

private:
  ObjectFileMinidump() = default;
};

#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_MINIDUMP_OBJECTFILEMINIDUMP_H
