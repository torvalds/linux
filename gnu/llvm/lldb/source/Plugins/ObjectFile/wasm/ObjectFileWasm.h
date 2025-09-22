//===-- ObjectFileWasm.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_WASM_OBJECTFILEWASM_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_WASM_OBJECTFILEWASM_H

#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/ArchSpec.h"
#include <optional>

namespace lldb_private {
namespace wasm {

/// Generic Wasm object file reader.
///
/// This class provides a generic wasm32 reader plugin implementing the
/// ObjectFile protocol.
class ObjectFileWasm : public ObjectFile {
public:
  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "wasm"; }
  static const char *GetPluginDescriptionStatic() {
    return "WebAssembly object file reader.";
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

  /// PluginInterface protocol.
  /// \{
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
  /// \}

  /// LLVM RTTI support
  /// \{
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjectFile::isA(ClassID);
  }
  static bool classof(const ObjectFile *obj) { return obj->isA(&ID); }
  /// \}

  /// ObjectFile Protocol.
  /// \{
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

  bool IsStripped() override { return !!GetExternalDebugInfoFileSpec(); }

  void CreateSections(SectionList &unified_section_list) override;

  void Dump(Stream *s) override;

  ArchSpec GetArchitecture() override { return m_arch; }

  UUID GetUUID() override { return m_uuid; }

  uint32_t GetDependentModules(FileSpecList &files) override { return 0; }

  Type CalculateType() override { return eTypeSharedLibrary; }

  Strata CalculateStrata() override { return eStrataUser; }

  bool SetLoadAddress(lldb_private::Target &target, lldb::addr_t value,
                      bool value_is_offset) override;

  lldb_private::Address GetBaseAddress() override {
    return IsInMemory() ? Address(m_memory_addr) : Address(0);
  }
  /// \}

  /// A Wasm module that has external DWARF debug information should contain a
  /// custom section named "external_debug_info", whose payload is an UTF-8
  /// encoded string that points to a Wasm module that contains the debug
  /// information for this module.
  std::optional<FileSpec> GetExternalDebugInfoFileSpec();

private:
  ObjectFileWasm(const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
                 lldb::offset_t data_offset, const FileSpec *file,
                 lldb::offset_t offset, lldb::offset_t length);
  ObjectFileWasm(const lldb::ModuleSP &module_sp,
                 lldb::WritableDataBufferSP header_data_sp,
                 const lldb::ProcessSP &process_sp, lldb::addr_t header_addr);

  /// Wasm section decoding routines.
  /// \{
  bool DecodeNextSection(lldb::offset_t *offset_ptr);
  bool DecodeSections();
  /// \}

  /// Read a range of bytes from the Wasm module.
  DataExtractor ReadImageData(lldb::offset_t offset, uint32_t size);

  typedef struct section_info {
    lldb::offset_t offset;
    uint32_t size;
    uint32_t id;
    ConstString name;
  } section_info_t;

  /// Wasm section header dump routines.
  /// \{
  void DumpSectionHeader(llvm::raw_ostream &ostream, const section_info_t &sh);
  void DumpSectionHeaders(llvm::raw_ostream &ostream);
  /// \}

  std::vector<section_info_t> m_sect_infos;
  ArchSpec m_arch;
  UUID m_uuid;
};

} // namespace wasm
} // namespace lldb_private
#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_WASM_OBJECTFILEWASM_H
