//===-- ObjectFileBreakpad.h ---------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_BREAKPAD_OBJECTFILEBREAKPAD_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_BREAKPAD_OBJECTFILEBREAKPAD_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {
namespace breakpad {

class ObjectFileBreakpad : public ObjectFile {
public:
  // Static Functions
  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "breakpad"; }
  static const char *GetPluginDescriptionStatic() {
    return "Breakpad object file reader.";
  }

  static ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
                 lldb::offset_t data_offset, const FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length);

  static ObjectFile *CreateMemoryInstance(const lldb::ModuleSP &module_sp,
                                          lldb::WritableDataBufferSP data_sp,
                                          const lldb::ProcessSP &process_sp,
                                          lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        ModuleSpecList &specs);

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // LLVM RTTI support
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjectFile::isA(ClassID);
  }
  static bool classof(const ObjectFile *obj) { return obj->isA(&ID); }

  // ObjectFile Protocol.

  bool ParseHeader() override;

  lldb::ByteOrder GetByteOrder() const override {
    return m_arch.GetByteOrder();
  }

  bool IsExecutable() const override { return false; }

  uint32_t GetAddressByteSize() const override {
    return m_arch.GetAddressByteSize();
  }

  AddressClass GetAddressClass(lldb::addr_t file_addr) override {
    return AddressClass::eInvalid;
  }

  void ParseSymtab(lldb_private::Symtab &symtab) override;

  bool IsStripped() override { return false; }

  void CreateSections(SectionList &unified_section_list) override;

  void Dump(Stream *s) override {}

  ArchSpec GetArchitecture() override { return m_arch; }

  UUID GetUUID() override { return m_uuid; }

  uint32_t GetDependentModules(FileSpecList &files) override { return 0; }

  Type CalculateType() override { return eTypeDebugInfo; }

  Strata CalculateStrata() override { return eStrataUser; }

private:
  ArchSpec m_arch;
  UUID m_uuid;

  ObjectFileBreakpad(const lldb::ModuleSP &module_sp,
                     lldb::DataBufferSP &data_sp, lldb::offset_t data_offset,
                     const FileSpec *file, lldb::offset_t offset,
                     lldb::offset_t length, ArchSpec arch, UUID uuid);
};

} // namespace breakpad
} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_BREAKPAD_OBJECTFILEBREAKPAD_H
