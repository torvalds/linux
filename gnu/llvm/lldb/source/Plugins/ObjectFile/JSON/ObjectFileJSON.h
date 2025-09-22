//===-- ObjectFileJSON.h -------------------------------------- -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_JSON_OBJECTFILEJSON_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_JSON_OBJECTFILEJSON_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"
#include "llvm/Support/JSON.h"

namespace lldb_private {

class ObjectFileJSON : public ObjectFile {
public:
  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "JSON"; }

  static const char *GetPluginDescriptionStatic() {
    return "JSON object file reader.";
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

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // LLVM RTTI support
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjectFile::isA(ClassID);
  }
  static bool classof(const ObjectFile *obj) { return obj->isA(&ID); }

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

  Type CalculateType() override { return m_type; }

  Strata CalculateStrata() override { return eStrataUser; }

  static bool MagicBytesMatch(lldb::DataBufferSP data_sp, lldb::addr_t offset,
                              lldb::addr_t length);

  struct Header {
    std::string triple;
    std::string uuid;
    std::optional<ObjectFile::Type> type;
  };

  struct Body {
    std::vector<JSONSection> sections;
    std::vector<JSONSymbol> symbols;
  };

private:
  ArchSpec m_arch;
  UUID m_uuid;
  ObjectFile::Type m_type;
  std::optional<uint64_t> m_size;
  std::vector<JSONSymbol> m_symbols;
  std::vector<JSONSection> m_sections;

  ObjectFileJSON(const lldb::ModuleSP &module_sp, lldb::DataBufferSP &data_sp,
                 lldb::offset_t data_offset, const FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length, ArchSpec arch,
                 UUID uuid, Type type, std::vector<JSONSymbol> symbols,
                 std::vector<JSONSection> sections);
};

bool fromJSON(const llvm::json::Value &value, ObjectFileJSON::Header &header,
              llvm::json::Path path);

bool fromJSON(const llvm::json::Value &value, ObjectFileJSON::Body &body,
              llvm::json::Path path);

} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_JSON_OBJECTFILEJSON_H
