//===-- SymbolFileNativePDB.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_SYMBOLFILENATIVEPDB_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_SYMBOLFILENATIVEPDB_H

#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolFile.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include "CompileUnitIndex.h"
#include "PdbIndex.h"
#include "PdbAstBuilder.h"
#include <optional>

namespace clang {
class TagDecl;
}

namespace llvm {
namespace codeview {
class ClassRecord;
class EnumRecord;
class ModifierRecord;
class PointerRecord;
struct UnionRecord;
} // namespace codeview
} // namespace llvm

namespace lldb_private {

namespace npdb {

class SymbolFileNativePDB : public SymbolFileCommon {
  friend class UdtRecordCompleter;

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

  static void DebuggerInitialize(Debugger &debugger);

  static llvm::StringRef GetPluginNameStatic() { return "native-pdb"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static SymbolFile *CreateInstance(lldb::ObjectFileSP objfile_sp);

  // Constructors and Destructors
  SymbolFileNativePDB(lldb::ObjectFileSP objfile_sp);

  ~SymbolFileNativePDB() override;

  uint32_t CalculateAbilities() override;

  void InitializeObject() override;

  uint64_t GetDebugInfoSize(bool load_all_debug_info = false) override;

  // Compile Unit function calls

  void
  ParseDeclsForContext(lldb_private::CompilerDeclContext decl_ctx) override;

  lldb::LanguageType
  ParseLanguage(lldb_private::CompileUnit &comp_unit) override;

  size_t ParseFunctions(lldb_private::CompileUnit &comp_unit) override;

  bool ParseLineTable(lldb_private::CompileUnit &comp_unit) override;

  bool ParseDebugMacros(lldb_private::CompileUnit &comp_unit) override;

  bool ParseSupportFiles(lldb_private::CompileUnit &comp_unit,
                         SupportFileList &support_files) override;
  size_t ParseTypes(lldb_private::CompileUnit &comp_unit) override;

  bool ParseImportedModules(
      const SymbolContext &sc,
      std::vector<lldb_private::SourceModule> &imported_modules) override;

  size_t ParseBlocksRecursive(Function &func) override;

  void FindGlobalVariables(ConstString name,
                           const CompilerDeclContext &parent_decl_ctx,
                           uint32_t max_matches,
                           VariableList &variables) override;

  size_t ParseVariablesForContext(const SymbolContext &sc) override;

  void AddSymbols(Symtab &symtab) override;

  CompilerDecl GetDeclForUID(lldb::user_id_t uid) override;
  CompilerDeclContext GetDeclContextForUID(lldb::user_id_t uid) override;
  CompilerDeclContext GetDeclContextContainingUID(lldb::user_id_t uid) override;
  Type *ResolveTypeUID(lldb::user_id_t type_uid) override;
  std::optional<ArrayInfo> GetDynamicArrayInfoForUID(
      lldb::user_id_t type_uid,
      const lldb_private::ExecutionContext *exe_ctx) override;

  bool CompleteType(CompilerType &compiler_type) override;
  uint32_t ResolveSymbolContext(const Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContext &sc) override;
  uint32_t ResolveSymbolContext(const SourceLocationSpec &src_location_spec,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContextList &sc_list) override;

  void GetTypes(SymbolContextScope *sc_scope, lldb::TypeClass type_mask,
                TypeList &type_list) override;

  void FindFunctions(const Module::LookupInfo &lookup_info,
                     const CompilerDeclContext &parent_decl_ctx,
                     bool include_inlines, SymbolContextList &sc_list) override;

  void FindFunctions(const RegularExpression &regex, bool include_inlines,
                     SymbolContextList &sc_list) override;

  std::optional<PdbCompilandSymId> FindSymbolScope(PdbCompilandSymId id);

  void FindTypes(const lldb_private::TypeQuery &match,
                 lldb_private::TypeResults &results) override;

  llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language) override;

  CompilerDeclContext FindNamespace(ConstString name,
                                    const CompilerDeclContext &parent_decl_ctx,
                                    bool only_root_namespaces) override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  llvm::pdb::PDBFile &GetPDBFile() { return m_index->pdb(); }
  const llvm::pdb::PDBFile &GetPDBFile() const { return m_index->pdb(); }

  PdbIndex &GetIndex() { return *m_index; };

  void DumpClangAST(Stream &s) override;

  std::optional<llvm::codeview::TypeIndex>
  GetParentType(llvm::codeview::TypeIndex ti);

private:
  struct LineTableEntryComparator {
    bool operator()(const lldb_private::LineTable::Entry &lhs,
                    const lldb_private::LineTable::Entry &rhs) const {
      return lhs.file_addr < rhs.file_addr;
    }
  };

  // From address range relative to function base to source line number.
  using RangeSourceLineVector =
      lldb_private::RangeDataVector<uint32_t, uint32_t, int32_t>;
  // InlineSite contains information in a S_INLINESITE record.
  struct InlineSite {
    PdbCompilandSymId parent_id;
    std::shared_ptr<InlineFunctionInfo> inline_function_info;
    RangeSourceLineVector ranges;
    std::vector<lldb_private::LineTable::Entry> line_entries;
    InlineSite(PdbCompilandSymId parent_id) : parent_id(parent_id){};
  };

  void BuildParentMap();

  uint32_t CalculateNumCompileUnits() override;

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  void FindTypesByName(llvm::StringRef name, uint32_t max_matches,
                       TypeMap &types);

  lldb::TypeSP CreateModifierType(PdbTypeSymId type_id,
                                  const llvm::codeview::ModifierRecord &mr,
                                  CompilerType ct);
  lldb::TypeSP CreatePointerType(PdbTypeSymId type_id,
                                 const llvm::codeview::PointerRecord &pr,
                                 CompilerType ct);
  lldb::TypeSP CreateSimpleType(llvm::codeview::TypeIndex ti, CompilerType ct);
  lldb::TypeSP CreateTagType(PdbTypeSymId type_id,
                             const llvm::codeview::ClassRecord &cr,
                             CompilerType ct);
  lldb::TypeSP CreateTagType(PdbTypeSymId type_id,
                             const llvm::codeview::EnumRecord &er,
                             CompilerType ct);
  lldb::TypeSP CreateTagType(PdbTypeSymId type_id,
                             const llvm::codeview::UnionRecord &ur,
                             CompilerType ct);
  lldb::TypeSP CreateArrayType(PdbTypeSymId type_id,
                               const llvm::codeview::ArrayRecord &ar,
                               CompilerType ct);
  lldb::TypeSP CreateFunctionType(PdbTypeSymId type_id,
                                  const llvm::codeview::MemberFunctionRecord &pr,
                                  CompilerType ct);
  lldb::TypeSP CreateProcedureType(PdbTypeSymId type_id,
                                   const llvm::codeview::ProcedureRecord &pr,
                                   CompilerType ct);
  lldb::TypeSP CreateClassStructUnion(PdbTypeSymId type_id,
                                      const llvm::codeview::TagRecord &record,
                                      size_t size, CompilerType ct);

  lldb::FunctionSP GetOrCreateFunction(PdbCompilandSymId func_id,
                                       CompileUnit &comp_unit);
  lldb::CompUnitSP GetOrCreateCompileUnit(const CompilandIndexItem &cci);
  lldb::TypeSP GetOrCreateType(PdbTypeSymId type_id);
  lldb::TypeSP GetOrCreateType(llvm::codeview::TypeIndex ti);
  lldb::VariableSP GetOrCreateGlobalVariable(PdbGlobalSymId var_id);
  Block &GetOrCreateBlock(PdbCompilandSymId block_id);
  lldb::VariableSP GetOrCreateLocalVariable(PdbCompilandSymId scope_id,
                                            PdbCompilandSymId var_id,
                                            bool is_param);
  lldb::TypeSP GetOrCreateTypedef(PdbGlobalSymId id);

  lldb::FunctionSP CreateFunction(PdbCompilandSymId func_id,
                                  CompileUnit &comp_unit);
  Block &CreateBlock(PdbCompilandSymId block_id);
  lldb::VariableSP CreateLocalVariable(PdbCompilandSymId scope_id,
                                       PdbCompilandSymId var_id, bool is_param);
  lldb::TypeSP CreateTypedef(PdbGlobalSymId id);
  lldb::CompUnitSP CreateCompileUnit(const CompilandIndexItem &cci);
  lldb::TypeSP CreateType(PdbTypeSymId type_id, CompilerType ct);
  lldb::TypeSP CreateAndCacheType(PdbTypeSymId type_id);
  lldb::VariableSP CreateGlobalVariable(PdbGlobalSymId var_id);
  lldb::VariableSP CreateConstantSymbol(PdbGlobalSymId var_id,
                                        const llvm::codeview::CVSymbol &cvs);
  size_t ParseVariablesForCompileUnit(CompileUnit &comp_unit,
                                      VariableList &variables);
  size_t ParseVariablesForBlock(PdbCompilandSymId block_id);

  llvm::Expected<uint32_t> GetFileIndex(const CompilandIndexItem &cii,
                                        uint32_t file_id);

  size_t ParseSymbolArrayInScope(
      PdbCompilandSymId parent,
      llvm::function_ref<bool(llvm::codeview::SymbolKind, PdbCompilandSymId)>
          fn);

  void ParseInlineSite(PdbCompilandSymId inline_site_id, Address func_addr);

  llvm::BumpPtrAllocator m_allocator;

  lldb::addr_t m_obj_load_address = 0;
  bool m_done_full_type_scan = false;
  // UID for anonymous union and anonymous struct as they don't have entities in
  // pdb debug info.
  lldb::user_id_t anonymous_id = LLDB_INVALID_UID - 1;

  std::unique_ptr<llvm::pdb::PDBFile> m_file_up;
  std::unique_ptr<PdbIndex> m_index;

  llvm::DenseMap<lldb::user_id_t, lldb::VariableSP> m_global_vars;
  llvm::DenseMap<lldb::user_id_t, lldb::VariableSP> m_local_variables;
  llvm::DenseMap<lldb::user_id_t, lldb::BlockSP> m_blocks;
  llvm::DenseMap<lldb::user_id_t, lldb::FunctionSP> m_functions;
  llvm::DenseMap<lldb::user_id_t, lldb::CompUnitSP> m_compilands;
  llvm::DenseMap<lldb::user_id_t, lldb::TypeSP> m_types;
  llvm::DenseMap<lldb::user_id_t, std::shared_ptr<InlineSite>> m_inline_sites;
  llvm::DenseMap<llvm::codeview::TypeIndex, llvm::codeview::TypeIndex>
      m_parent_types;
};

} // namespace npdb
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_NATIVEPDB_SYMBOLFILENATIVEPDB_H
