//===-- SymbolFilePDB.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_PDB_SYMBOLFILEPDB_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_PDB_SYMBOLFILEPDB_H

#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/UserID.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include <optional>

class PDBASTParser;

class SymbolFilePDB : public lldb_private::SymbolFileCommon {
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

  // Static Functions
  static void Initialize();

  static void Terminate();

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  static llvm::StringRef GetPluginNameStatic() { return "pdb"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::SymbolFile *
  CreateInstance(lldb::ObjectFileSP objfile_sp);

  // Constructors and Destructors
  SymbolFilePDB(lldb::ObjectFileSP objfile_sp);

  ~SymbolFilePDB() override;

  uint32_t CalculateAbilities() override;

  void InitializeObject() override;

  // Compile Unit function calls

  lldb::LanguageType
  ParseLanguage(lldb_private::CompileUnit &comp_unit) override;

  size_t ParseFunctions(lldb_private::CompileUnit &comp_unit) override;

  bool ParseLineTable(lldb_private::CompileUnit &comp_unit) override;

  bool ParseDebugMacros(lldb_private::CompileUnit &comp_unit) override;

  bool ParseSupportFiles(lldb_private::CompileUnit &comp_unit,
                         lldb_private::SupportFileList &support_files) override;

  size_t ParseTypes(lldb_private::CompileUnit &comp_unit) override;

  bool ParseImportedModules(
      const lldb_private::SymbolContext &sc,
      std::vector<lldb_private::SourceModule> &imported_modules) override;

  size_t ParseBlocksRecursive(lldb_private::Function &func) override;

  size_t
  ParseVariablesForContext(const lldb_private::SymbolContext &sc) override;

  lldb_private::Type *ResolveTypeUID(lldb::user_id_t type_uid) override;
  std::optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override;

  bool CompleteType(lldb_private::CompilerType &compiler_type) override;

  lldb_private::CompilerDecl GetDeclForUID(lldb::user_id_t uid) override;

  lldb_private::CompilerDeclContext
  GetDeclContextForUID(lldb::user_id_t uid) override;

  lldb_private::CompilerDeclContext
  GetDeclContextContainingUID(lldb::user_id_t uid) override;

  void
  ParseDeclsForContext(lldb_private::CompilerDeclContext decl_ctx) override;

  uint32_t ResolveSymbolContext(const lldb_private::Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                lldb_private::SymbolContext &sc) override;

  uint32_t ResolveSymbolContext(
      const lldb_private::SourceLocationSpec &src_location_spec,
      lldb::SymbolContextItem resolve_scope,
      lldb_private::SymbolContextList &sc_list) override;

  void
  FindGlobalVariables(lldb_private::ConstString name,
                      const lldb_private::CompilerDeclContext &parent_decl_ctx,
                      uint32_t max_matches,
                      lldb_private::VariableList &variables) override;

  void FindGlobalVariables(const lldb_private::RegularExpression &regex,
                           uint32_t max_matches,
                           lldb_private::VariableList &variables) override;

  void FindFunctions(const lldb_private::Module::LookupInfo &lookup_info,
                     const lldb_private::CompilerDeclContext &parent_decl_ctx,
                     bool include_inlines,
                     lldb_private::SymbolContextList &sc_list) override;

  void FindFunctions(const lldb_private::RegularExpression &regex,
                     bool include_inlines,
                     lldb_private::SymbolContextList &sc_list) override;

  void GetMangledNamesForFunction(
      const std::string &scope_qualified_name,
      std::vector<lldb_private::ConstString> &mangled_names) override;

  void AddSymbols(lldb_private::Symtab &symtab) override;
  void FindTypes(const lldb_private::TypeQuery &match,
                 lldb_private::TypeResults &results) override;
  void FindTypesByRegex(const lldb_private::RegularExpression &regex,
                        uint32_t max_matches, lldb_private::TypeMap &types);

  void GetTypes(lldb_private::SymbolContextScope *sc_scope,
                lldb::TypeClass type_mask,
                lldb_private::TypeList &type_list) override;

  llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language) override;

  lldb_private::CompilerDeclContext
  FindNamespace(lldb_private::ConstString name,
                const lldb_private::CompilerDeclContext &parent_decl_ctx,
                bool only_root_namespaces) override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  llvm::pdb::IPDBSession &GetPDBSession();

  const llvm::pdb::IPDBSession &GetPDBSession() const;

  void DumpClangAST(lldb_private::Stream &s) override;

private:
  struct SecContribInfo {
    uint32_t Offset;
    uint32_t Size;
    uint32_t CompilandId;
  };
  using SecContribsMap = std::map<uint32_t, std::vector<SecContribInfo>>;

  uint32_t CalculateNumCompileUnits() override;

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  lldb::CompUnitSP ParseCompileUnitForUID(uint32_t id,
                                          uint32_t index = UINT32_MAX);

  bool ParseCompileUnitLineTable(lldb_private::CompileUnit &comp_unit,
                                 uint32_t match_line);

  void BuildSupportFileIdToSupportFileIndexMap(
      const llvm::pdb::PDBSymbolCompiland &pdb_compiland,
      llvm::DenseMap<uint32_t, uint32_t> &index_map) const;

  void FindTypesByName(llvm::StringRef name,
                       const lldb_private::CompilerDeclContext &parent_decl_ctx,
                       uint32_t max_matches, lldb_private::TypeMap &types);

  std::string GetMangledForPDBData(const llvm::pdb::PDBSymbolData &pdb_data);

  lldb::VariableSP
  ParseVariableForPDBData(const lldb_private::SymbolContext &sc,
                          const llvm::pdb::PDBSymbolData &pdb_data);

  size_t ParseVariables(const lldb_private::SymbolContext &sc,
                        const llvm::pdb::PDBSymbol &pdb_data,
                        lldb_private::VariableList *variable_list = nullptr);

  lldb::CompUnitSP
  GetCompileUnitContainsAddress(const lldb_private::Address &so_addr);

  typedef std::vector<lldb_private::Type *> TypeCollection;

  void GetTypesForPDBSymbol(const llvm::pdb::PDBSymbol &pdb_symbol,
                            uint32_t type_mask,
                            TypeCollection &type_collection);

  lldb_private::Function *
  ParseCompileUnitFunctionForPDBFunc(const llvm::pdb::PDBSymbolFunc &pdb_func,
                                     lldb_private::CompileUnit &comp_unit);

  void GetCompileUnitIndex(const llvm::pdb::PDBSymbolCompiland &pdb_compiland,
                           uint32_t &index);

  PDBASTParser *GetPDBAstParser();

  std::unique_ptr<llvm::pdb::PDBSymbolCompiland>
  GetPDBCompilandByUID(uint32_t uid);

  lldb_private::Mangled
  GetMangledForPDBFunc(const llvm::pdb::PDBSymbolFunc &pdb_func);

  bool ResolveFunction(const llvm::pdb::PDBSymbolFunc &pdb_func,
                       bool include_inlines,
                       lldb_private::SymbolContextList &sc_list);

  bool ResolveFunction(uint32_t uid, bool include_inlines,
                       lldb_private::SymbolContextList &sc_list);

  void CacheFunctionNames();

  bool DeclContextMatchesThisSymbolFile(
      const lldb_private::CompilerDeclContext &decl_ctx);

  uint32_t GetCompilandId(const llvm::pdb::PDBSymbolData &data);

  llvm::DenseMap<uint32_t, lldb::CompUnitSP> m_comp_units;
  llvm::DenseMap<uint32_t, lldb::TypeSP> m_types;
  llvm::DenseMap<uint32_t, lldb::VariableSP> m_variables;
  llvm::DenseMap<uint64_t, std::string> m_public_names;

  SecContribsMap m_sec_contribs;

  std::vector<lldb::TypeSP> m_builtin_types;
  std::unique_ptr<llvm::pdb::IPDBSession> m_session_up;
  std::unique_ptr<llvm::pdb::PDBSymbolExe> m_global_scope_up;

  lldb_private::UniqueCStringMap<uint32_t> m_func_full_names;
  lldb_private::UniqueCStringMap<uint32_t> m_func_base_names;
  lldb_private::UniqueCStringMap<uint32_t> m_func_method_names;
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_PDB_SYMBOLFILEPDB_H
