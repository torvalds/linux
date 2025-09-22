//===-- ObjectFilePlaceholder.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_OBJECTFILE_PLACEHOLDER_OBJECTFILEPLACEHOLDER_H
#define LLDB_SOURCE_PLUGINS_OBJECTFILE_PLACEHOLDER_OBJECTFILEPLACEHOLDER_H

#include "lldb/Symbol/ObjectFile.h"

#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-private.h"

/// A minimal ObjectFile implementation providing a dummy object file for the
/// cases when the real module binary is not available. This allows the module
/// to show up in "image list" and symbols to be added to it.
class ObjectFilePlaceholder : public lldb_private::ObjectFile {
public:
  // Static Functions
  static void Initialize() {}

  static void Terminate() {}

  static llvm::StringRef GetPluginNameStatic() { return "placeholder"; }

  ObjectFilePlaceholder(const lldb::ModuleSP &module_sp,
                        const lldb_private::ModuleSpec &module_spec,
                        lldb::addr_t base, lldb::addr_t size);

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
  bool ParseHeader() override { return true; }
  Type CalculateType() override { return eTypeUnknown; }
  Strata CalculateStrata() override { return eStrataUnknown; }
  uint32_t GetDependentModules(lldb_private::FileSpecList &file_list) override {
    return 0;
  }
  bool IsExecutable() const override { return false; }
  lldb_private::ArchSpec GetArchitecture() override { return m_arch; }
  lldb_private::UUID GetUUID() override { return m_uuid; }
  void ParseSymtab(lldb_private::Symtab &symtab) override {}
  bool IsStripped() override { return true; }
  lldb::ByteOrder GetByteOrder() const override {
    return m_arch.GetByteOrder();
  }

  uint32_t GetAddressByteSize() const override {
    return m_arch.GetAddressByteSize();
  }

  lldb_private::Address GetBaseAddress() override;

  void CreateSections(lldb_private::SectionList &unified_section_list) override;

  bool SetLoadAddress(lldb_private::Target &target, lldb::addr_t value,
                      bool value_is_offset) override;

  void Dump(lldb_private::Stream *s) override;

  lldb::addr_t GetBaseImageAddress() const { return m_base; }

private:
  lldb_private::ArchSpec m_arch;
  lldb_private::UUID m_uuid;
  lldb::addr_t m_base;
  lldb::addr_t m_size;
};

#endif // LLDB_SOURCE_PLUGINS_OBJECTFILE_PLACEHOLDER_OBJECTFILEPLACEHOLDER_H
