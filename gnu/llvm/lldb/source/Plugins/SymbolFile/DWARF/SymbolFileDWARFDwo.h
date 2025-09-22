//===-- SymbolFileDWARFDwo.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARFDWO_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARFDWO_H

#include "SymbolFileDWARF.h"
#include <optional>

namespace lldb_private::plugin {
namespace dwarf {
class SymbolFileDWARFDwo : public SymbolFileDWARF {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  /// \{
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || SymbolFileDWARF::isA(ClassID);
  }
  static bool classof(const SymbolFile *obj) { return obj->isA(&ID); }
  /// \}

  SymbolFileDWARFDwo(SymbolFileDWARF &m_base_symbol_file,
                     lldb::ObjectFileSP objfile, uint32_t id);

  ~SymbolFileDWARFDwo() override = default;

  DWARFCompileUnit *GetDWOCompileUnitForHash(uint64_t hash);

  void GetObjCMethods(ConstString class_name,
                      llvm::function_ref<bool(DWARFDIE die)> callback) override;

  llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language) override;

  DWARFDIE
  GetDIE(const DIERef &die_ref) override;

  lldb::offset_t GetVendorDWARFOpcodeSize(const DataExtractor &data,
                                          const lldb::offset_t data_offset,
                                          const uint8_t op) const override;

  uint64_t GetDebugInfoSize(bool load_all_debug_info = false) override;

  bool ParseVendorDWARFOpcode(uint8_t op, const DataExtractor &opcodes,
                              lldb::offset_t &offset,
                              std::vector<Value> &stack) const override;

  void FindGlobalVariables(ConstString name,
                           const CompilerDeclContext &parent_decl_ctx,
                           uint32_t max_matches,
                           VariableList &variables) override;

  SymbolFileDWARF &GetBaseSymbolFile() const { return m_base_symbol_file; }

  bool GetDebugInfoIndexWasLoadedFromCache() const override;
  void SetDebugInfoIndexWasLoadedFromCache() override;
  bool GetDebugInfoIndexWasSavedToCache() const override;
  void SetDebugInfoIndexWasSavedToCache() override;
  bool GetDebugInfoHadFrameVariableErrors() const override;
  void SetDebugInfoHadFrameVariableErrors() override;

  SymbolFileDWARF *GetDIERefSymbolFile(const DIERef &die_ref) override;

protected:
  DIEToTypePtr &GetDIEToType() override;

  DIEToVariableSP &GetDIEToVariable() override;

  llvm::DenseMap<lldb::opaque_compiler_type_t, DIERef> &
  GetForwardDeclCompilerTypeToDIE() override;

  UniqueDWARFASTTypeMap &GetUniqueDWARFASTTypeMap() override;

  DWARFDIE FindDefinitionDIE(const DWARFDIE &die) override;

  lldb::TypeSP
  FindCompleteObjCDefinitionTypeForDIE(const DWARFDIE &die,
                                       ConstString type_name,
                                       bool must_be_implementation) override;

  /// If this file contains exactly one compile unit, this function will return
  /// it. Otherwise it returns nullptr.
  DWARFCompileUnit *FindSingleCompileUnit();

  SymbolFileDWARF &m_base_symbol_file;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARFDWO_H
