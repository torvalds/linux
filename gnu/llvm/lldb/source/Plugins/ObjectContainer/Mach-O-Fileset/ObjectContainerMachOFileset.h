//===-- ObjectContainerMachOFileset.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_MACH_O_FILESET_OBJECTCONTAINERMADCHOFILESET_H
#define LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_MACH_O_FILESET_OBJECTCONTAINERMADCHOFILESET_H

#include "lldb/Host/SafeMachO.h"
#include "lldb/Symbol/ObjectContainer.h"
#include "lldb/Utility/FileSpec.h"

namespace lldb_private {

class ObjectContainerMachOFileset : public lldb_private::ObjectContainer {
public:
  ObjectContainerMachOFileset(const lldb::ModuleSP &module_sp,
                              lldb::DataBufferSP &data_sp,
                              lldb::offset_t data_offset,
                              const lldb_private::FileSpec *file,
                              lldb::offset_t offset, lldb::offset_t length);

  ObjectContainerMachOFileset(const lldb::ModuleSP &module_sp,
                              lldb::WritableDataBufferSP data_sp,
                              const lldb::ProcessSP &process_sp,
                              lldb::addr_t header_addr);

  ~ObjectContainerMachOFileset() override;

  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "mach-o-fileset"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
    return "Mach-O Fileset container reader.";
  }

  static lldb_private::ObjectContainer *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length);

  static lldb_private::ObjectContainer *CreateMemoryInstance(
      const lldb::ModuleSP &module_sp, lldb::WritableDataBufferSP data_sp,
      const lldb::ProcessSP &process_sp, lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  static bool MagicBytesMatch(const lldb_private::DataExtractor &data);
  static bool MagicBytesMatch(lldb::DataBufferSP data_sp,
                              lldb::addr_t data_offset,
                              lldb::addr_t data_length);

  bool ParseHeader() override;

  size_t GetNumObjects() const override { return m_entries.size(); }

  lldb::ObjectFileSP GetObjectFile(const lldb_private::FileSpec *file) override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  struct Entry {
    Entry(uint64_t vmaddr, uint64_t fileoff, std::string id)
        : vmaddr(vmaddr), fileoff(fileoff), id(id) {}
    uint64_t vmaddr;
    uint64_t fileoff;
    std::string id;
  };

  Entry *FindEntry(llvm::StringRef id);

private:
  static bool ParseHeader(lldb_private::DataExtractor &data,
                          const lldb_private::FileSpec &file,
                          lldb::offset_t file_offset,
                          std::vector<Entry> &entries);

  std::vector<Entry> m_entries;
  lldb::ProcessWP m_process_wp;
  const lldb::addr_t m_memory_addr;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_OBJECTCONTAINER_MACH_O_FILESET_OBJECTCONTAINERMADCHOFILESET_H
