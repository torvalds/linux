//===-- ObjectFileJIT.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_OBJECTFILEJIT_H
#define LLDB_EXPRESSION_OBJECTFILEJIT_H

#include "lldb/Core/Address.h"
#include "lldb/Core/Section.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Utility/ArchSpec.h"

namespace lldb_private {

class ObjectFileJITDelegate {
public:
  ObjectFileJITDelegate() = default;
  virtual ~ObjectFileJITDelegate() = default;
  virtual lldb::ByteOrder GetByteOrder() const = 0;
  virtual uint32_t GetAddressByteSize() const = 0;
  virtual void PopulateSymtab(lldb_private::ObjectFile *obj_file,
                              lldb_private::Symtab &symtab) = 0;
  virtual void PopulateSectionList(lldb_private::ObjectFile *obj_file,
                                   lldb_private::SectionList &section_list) = 0;
  virtual ArchSpec GetArchitecture() = 0;
};

class ObjectFileJIT : public ObjectFile {
public:
  ObjectFileJIT(const lldb::ModuleSP &module_sp,
                const lldb::ObjectFileJITDelegateSP &delegate_sp);

  ~ObjectFileJIT() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "jit"; }

  static llvm::StringRef GetPluginDescriptionStatic() {
    return "JIT code object file";
  }

  static lldb_private::ObjectFile *
  CreateInstance(const lldb::ModuleSP &module_sp, lldb::DataBufferSP data_sp,
                 lldb::offset_t data_offset, const lldb_private::FileSpec *file,
                 lldb::offset_t file_offset, lldb::offset_t length);

  static lldb_private::ObjectFile *CreateMemoryInstance(
      const lldb::ModuleSP &module_sp, lldb::WritableDataBufferSP data_sp,
      const lldb::ProcessSP &process_sp, lldb::addr_t header_addr);

  static size_t GetModuleSpecifications(const lldb_private::FileSpec &file,
                                        lldb::DataBufferSP &data_sp,
                                        lldb::offset_t data_offset,
                                        lldb::offset_t file_offset,
                                        lldb::offset_t length,
                                        lldb_private::ModuleSpecList &specs);

  // LLVM RTTI support
  static char ID;
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjectFile::isA(ClassID);
  }
  static bool classof(const ObjectFile *obj) { return obj->isA(&ID); }

  // Member Functions
  bool ParseHeader() override;

  bool SetLoadAddress(lldb_private::Target &target, lldb::addr_t value,
                      bool value_is_offset) override;

  lldb::ByteOrder GetByteOrder() const override;

  bool IsExecutable() const override;

  uint32_t GetAddressByteSize() const override;

  void ParseSymtab(lldb_private::Symtab &symtab) override;

  bool IsStripped() override;

  void CreateSections(lldb_private::SectionList &unified_section_list) override;

  void Dump(lldb_private::Stream *s) override;

  lldb_private::ArchSpec GetArchitecture() override;

  lldb_private::UUID GetUUID() override;

  uint32_t GetDependentModules(lldb_private::FileSpecList &files) override;

  size_t ReadSectionData(lldb_private::Section *section,
                         lldb::offset_t section_offset, void *dst,
                         size_t dst_len) override;

  size_t ReadSectionData(lldb_private::Section *section,
                         lldb_private::DataExtractor &section_data) override;

  lldb_private::Address GetEntryPointAddress() override;

  lldb_private::Address GetBaseAddress() override;

  ObjectFile::Type CalculateType() override;

  ObjectFile::Strata CalculateStrata() override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  lldb::ObjectFileJITDelegateWP m_delegate_wp;
};
} // namespace lldb_private

#endif // LLDB_EXPRESSION_OBJECTFILEJIT_H
