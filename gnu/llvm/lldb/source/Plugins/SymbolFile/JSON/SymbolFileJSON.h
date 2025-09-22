//===-- SymbolFileJSON.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_JSON_SYMBOLFILEJSON_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_JSON_SYMBOLFILEJSON_H

#include <map>
#include <optional>
#include <vector>

#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/SymbolFile.h"

namespace lldb_private {

class SymbolFileJSON : public lldb_private::SymbolFileCommon {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  /// \{
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || SymbolFileCommon::isA(ClassID);
  }
  static bool classof(const SymbolFile *obj) { return obj->isA(&ID); }
  /// \}

  SymbolFileJSON(lldb::ObjectFileSP objfile_sp);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "JSON"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::SymbolFile *
  CreateInstance(lldb::ObjectFileSP objfile_sp);

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  uint32_t CalculateAbilities() override;

  lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) override {
    return lldb::eLanguageTypeUnknown;
  }

  size_t ParseFunctions(CompileUnit &comp_unit) override { return 0; }

  bool ParseLineTable(CompileUnit &comp_unit) override { return false; }

  bool ParseDebugMacros(CompileUnit &comp_unit) override { return false; }

  bool ParseSupportFiles(CompileUnit &comp_unit,
                         SupportFileList &support_files) override {
    return false;
  }

  size_t ParseTypes(CompileUnit &cu) override { return 0; }

  bool ParseImportedModules(
      const SymbolContext &sc,
      std::vector<lldb_private::SourceModule> &imported_modules) override {
    return false;
  }

  size_t ParseBlocksRecursive(Function &func) override { return 0; }

  size_t ParseVariablesForContext(const SymbolContext &sc) override {
    return 0;
  }

  uint32_t CalculateNumCompileUnits() override { return 0; }

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  Type *ResolveTypeUID(lldb::user_id_t type_uid) override { return nullptr; }
  std::optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override {
    return std::nullopt;
  }

  bool CompleteType(CompilerType &compiler_type) override { return false; }

  uint32_t ResolveSymbolContext(const lldb_private::Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                lldb_private::SymbolContext &sc) override;

  void GetTypes(lldb_private::SymbolContextScope *sc_scope,
                lldb::TypeClass type_mask,
                lldb_private::TypeList &type_list) override;

  void AddSymbols(Symtab &symtab) override;

private:
  lldb::addr_t GetBaseFileAddress();

  std::vector<std::pair<uint64_t, std::string>> m_symbols;
};
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_JSON_SYMBOLFILEJSON_H
