//===-- SymbolFileDWARFDebugMap.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARFDEBUGMAP_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARFDEBUGMAP_H

#include "DIERef.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Utility/RangeMap.h"
#include "llvm/Support/Chrono.h"
#include <bitset>
#include <map>
#include <optional>
#include <vector>

#include "UniqueDWARFASTType.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private-enumerations.h"

class DWARFASTParserClang;

namespace lldb_private::plugin {
namespace dwarf {
class SymbolFileDWARF;
class DWARFCompileUnit;
class DWARFDebugAranges;
class DWARFDeclContext;

class SymbolFileDWARFDebugMap : public SymbolFileCommon {
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

  static llvm::StringRef GetPluginNameStatic() { return "dwarf-debugmap"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static SymbolFile *CreateInstance(lldb::ObjectFileSP objfile_sp);

  // Constructors and Destructors
  SymbolFileDWARFDebugMap(lldb::ObjectFileSP objfile_sp);
  ~SymbolFileDWARFDebugMap() override;

  uint32_t CalculateAbilities() override;
  void InitializeObject() override;

  // Compile Unit function calls
  lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) override;
  XcodeSDK ParseXcodeSDK(CompileUnit &comp_unit) override;
  llvm::SmallSet<lldb::LanguageType, 4>
  ParseAllLanguages(CompileUnit &comp_unit) override;
  size_t ParseFunctions(CompileUnit &comp_unit) override;
  bool ParseLineTable(CompileUnit &comp_unit) override;
  bool ParseDebugMacros(CompileUnit &comp_unit) override;

  bool ForEachExternalModule(CompileUnit &, llvm::DenseSet<SymbolFile *> &,
                             llvm::function_ref<bool(Module &)>) override;

  bool ParseSupportFiles(CompileUnit &comp_unit,
                         SupportFileList &support_files) override;

  bool ParseIsOptimized(CompileUnit &comp_unit) override;

  size_t ParseTypes(CompileUnit &comp_unit) override;

  bool
  ParseImportedModules(const SymbolContext &sc,
                       std::vector<SourceModule> &imported_modules) override;
  size_t ParseBlocksRecursive(Function &func) override;
  size_t ParseVariablesForContext(const SymbolContext &sc) override;

  Type *ResolveTypeUID(lldb::user_id_t type_uid) override;
  std::optional<ArrayInfo>
  GetDynamicArrayInfoForUID(lldb::user_id_t type_uid,
                            const ExecutionContext *exe_ctx) override;

  CompilerDeclContext GetDeclContextForUID(lldb::user_id_t uid) override;
  CompilerDeclContext GetDeclContextContainingUID(lldb::user_id_t uid) override;
  std::vector<CompilerContext>
  GetCompilerContextForUID(lldb::user_id_t uid) override;
  void ParseDeclsForContext(CompilerDeclContext decl_ctx) override;

  bool CompleteType(CompilerType &compiler_type) override;
  uint32_t ResolveSymbolContext(const Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContext &sc) override;
  uint32_t ResolveSymbolContext(const SourceLocationSpec &src_location_spec,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContextList &sc_list) override;

  Status CalculateFrameVariableError(StackFrame &frame) override;

  void FindGlobalVariables(ConstString name,
                           const CompilerDeclContext &parent_decl_ctx,
                           uint32_t max_matches,
                           VariableList &variables) override;
  void FindGlobalVariables(const RegularExpression &regex, uint32_t max_matches,
                           VariableList &variables) override;
  void FindFunctions(const Module::LookupInfo &lookup_info,
                     const CompilerDeclContext &parent_decl_ctx,
                     bool include_inlines, SymbolContextList &sc_list) override;
  void FindFunctions(const RegularExpression &regex, bool include_inlines,
                     SymbolContextList &sc_list) override;
  void FindTypes(const lldb_private::TypeQuery &match,
                 lldb_private::TypeResults &results) override;
  CompilerDeclContext FindNamespace(ConstString name,
                                    const CompilerDeclContext &parent_decl_ctx,
                                    bool only_root_namespaces) override;
  void GetTypes(SymbolContextScope *sc_scope, lldb::TypeClass type_mask,
                TypeList &type_list) override;
  std::vector<std::unique_ptr<CallEdge>>
  ParseCallEdgesInFunction(UserID func_id) override;

  void DumpClangAST(Stream &s) override;

  /// List separate oso files.
  bool GetSeparateDebugInfo(StructuredData::Dictionary &d,
                            bool errors_only) override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // Statistics overrides.
  ModuleList GetDebugInfoModules() override;

  void
  GetCompileOptions(std::unordered_map<lldb::CompUnitSP, Args> &args) override;

protected:
  enum { kHaveInitializedOSOs = (1 << 0), kNumFlags };

  friend class DebugMapModule;
  friend class ::DWARFASTParserClang;
  friend class DWARFCompileUnit;
  friend class SymbolFileDWARF;
  struct OSOInfo {
    lldb::ModuleSP module_sp;

    OSOInfo() : module_sp() {}
  };

  typedef std::shared_ptr<OSOInfo> OSOInfoSP;

  typedef RangeDataVector<lldb::addr_t, lldb::addr_t, lldb::addr_t>
      FileRangeMap;

  // Class specific types
  struct CompileUnitInfo {
    FileSpec so_file;
    ConstString oso_path;
    llvm::sys::TimePoint<> oso_mod_time;
    Status oso_load_error;
    OSOInfoSP oso_sp;
    /// The compile units that an object file contains.
    llvm::SmallVector<lldb::CompUnitSP, 2> compile_units_sps;
    /// A map from the compile unit ID to its index in the vector.
    llvm::SmallDenseMap<uint64_t, uint64_t, 2> id_to_index_map;
    uint32_t first_symbol_index = UINT32_MAX;
    uint32_t last_symbol_index = UINT32_MAX;
    uint32_t first_symbol_id = UINT32_MAX;
    uint32_t last_symbol_id = UINT32_MAX;
    FileRangeMap file_range_map;
    bool file_range_map_valid = false;

    CompileUnitInfo() = default;

    const FileRangeMap &GetFileRangeMap(SymbolFileDWARFDebugMap *exe_symfile);
  };

  // Protected Member Functions
  void InitOSO();

  /// This function actually returns the number of object files, which may be
  /// less than the actual number of compile units, since an object file may
  /// contain more than one compile unit. SymbolFileDWARFDebugMap looks up the
  /// number of compile units by reading the nlist symbol table, which
  /// currently, on macOS, only reports one compile unit per object file, and
  /// there's no efficient way to calculate the actual number of compile units
  /// upfront.
  uint32_t CalculateNumCompileUnits() override;

  /// This function actually returns the first compile unit the object file at
  /// the given index contains.
  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  static uint32_t GetOSOIndexFromUserID(lldb::user_id_t uid) {
    std::optional<uint32_t> OsoNum = DIERef(uid).file_index();
    lldbassert(OsoNum && "Invalid OSO Index");
    return *OsoNum;
  }

  static SymbolFileDWARF *GetSymbolFileAsSymbolFileDWARF(SymbolFile *sym_file);

  bool GetFileSpecForSO(uint32_t oso_idx, FileSpec &file_spec);

  CompileUnitInfo *GetCompUnitInfo(const SymbolContext &sc);
  CompileUnitInfo *GetCompUnitInfo(const CompileUnit &comp_unit);

  size_t GetCompUnitInfosForModule(const Module *oso_module,
                                   std::vector<CompileUnitInfo *> &cu_infos);

  Module *GetModuleByCompUnitInfo(CompileUnitInfo *comp_unit_info);

  Module *GetModuleByOSOIndex(uint32_t oso_idx);

  ObjectFile *GetObjectFileByCompUnitInfo(CompileUnitInfo *comp_unit_info);

  ObjectFile *GetObjectFileByOSOIndex(uint32_t oso_idx);

  uint32_t GetCompUnitInfoIndex(const CompileUnitInfo *comp_unit_info);

  SymbolFileDWARF *GetSymbolFile(const SymbolContext &sc);
  SymbolFileDWARF *GetSymbolFile(const CompileUnit &comp_unit);

  SymbolFileDWARF *GetSymbolFileByCompUnitInfo(CompileUnitInfo *comp_unit_info);

  SymbolFileDWARF *GetSymbolFileByOSOIndex(uint32_t oso_idx);

  /// If closure returns \ref IterationAction::Continue, iteration
  /// continues. Otherwise, iteration terminates.
  void
  ForEachSymbolFile(std::function<IterationAction(SymbolFileDWARF *)> closure) {
    for (uint32_t oso_idx = 0, num_oso_idxs = m_compile_unit_infos.size();
         oso_idx < num_oso_idxs; ++oso_idx) {
      if (SymbolFileDWARF *oso_dwarf = GetSymbolFileByOSOIndex(oso_idx)) {
        if (closure(oso_dwarf) == IterationAction::Stop)
          return;
      }
    }
  }

  CompileUnitInfo *GetCompileUnitInfoForSymbolWithIndex(uint32_t symbol_idx,
                                                        uint32_t *oso_idx_ptr);

  CompileUnitInfo *GetCompileUnitInfoForSymbolWithID(lldb::user_id_t symbol_id,
                                                     uint32_t *oso_idx_ptr);

  static int
  SymbolContainsSymbolWithIndex(uint32_t *symbol_idx_ptr,
                                const CompileUnitInfo *comp_unit_info);

  static int SymbolContainsSymbolWithID(lldb::user_id_t *symbol_idx_ptr,
                                        const CompileUnitInfo *comp_unit_info);

  void
  PrivateFindGlobalVariables(ConstString name,
                             const CompilerDeclContext &parent_decl_ctx,
                             const std::vector<uint32_t> &name_symbol_indexes,
                             uint32_t max_matches, VariableList &variables);

  void SetCompileUnit(SymbolFileDWARF *oso_dwarf,
                      const lldb::CompUnitSP &cu_sp);

  /// Returns the compile unit associated with the dwarf compile unit. This may
  /// be one of the extra compile units an object file contains which isn't
  /// reachable by ParseCompileUnitAtIndex(uint32_t).
  lldb::CompUnitSP GetCompileUnit(SymbolFileDWARF *oso_dwarf,
                                  DWARFCompileUnit &dwarf_cu);

  CompileUnitInfo *GetCompileUnitInfo(SymbolFileDWARF *oso_dwarf);

  DWARFDIE FindDefinitionDIE(const DWARFDIE &die);

  bool Supports_DW_AT_APPLE_objc_complete_type(SymbolFileDWARF *skip_dwarf_oso);

  lldb::TypeSP FindCompleteObjCDefinitionTypeForDIE(
      const DWARFDIE &die, ConstString type_name, bool must_be_implementation);

  llvm::DenseMap<lldb::opaque_compiler_type_t, DIERef> &
  GetForwardDeclCompilerTypeToDIE() {
    return m_forward_decl_compiler_type_to_die;
  }

  UniqueDWARFASTTypeMap &GetUniqueDWARFASTTypeMap() {
    return m_unique_ast_type_map;
  }

  // OSOEntry
  class OSOEntry {
  public:
    OSOEntry() = default;

    OSOEntry(uint32_t exe_sym_idx, lldb::addr_t oso_file_addr)
        : m_exe_sym_idx(exe_sym_idx), m_oso_file_addr(oso_file_addr) {}

    uint32_t GetExeSymbolIndex() const { return m_exe_sym_idx; }

    bool operator<(const OSOEntry &rhs) const {
      return m_exe_sym_idx < rhs.m_exe_sym_idx;
    }

    lldb::addr_t GetOSOFileAddress() const { return m_oso_file_addr; }

    void SetOSOFileAddress(lldb::addr_t oso_file_addr) {
      m_oso_file_addr = oso_file_addr;
    }

  protected:
    uint32_t m_exe_sym_idx = UINT32_MAX;
    lldb::addr_t m_oso_file_addr = LLDB_INVALID_ADDRESS;
  };

  typedef RangeDataVector<lldb::addr_t, lldb::addr_t, OSOEntry> DebugMap;

  // Member Variables
  std::bitset<kNumFlags> m_flags;
  std::vector<CompileUnitInfo> m_compile_unit_infos;
  std::vector<uint32_t> m_func_indexes; // Sorted by address
  std::vector<uint32_t> m_glob_indexes;
  std::map<std::pair<ConstString, llvm::sys::TimePoint<>>, OSOInfoSP> m_oso_map;
  // A map from CompilerType to the struct/class/union/enum DIE (might be a
  // declaration or a definition) that is used to construct it.
  llvm::DenseMap<lldb::opaque_compiler_type_t, DIERef>
      m_forward_decl_compiler_type_to_die;
  UniqueDWARFASTTypeMap m_unique_ast_type_map;
  LazyBool m_supports_DW_AT_APPLE_objc_complete_type;
  DebugMap m_debug_map;

  // When an object file from the debug map gets parsed in
  // SymbolFileDWARF, it needs to tell the debug map about the object
  // files addresses by calling this function once for each N_FUN,
  // N_GSYM and N_STSYM and after all entries in the debug map have
  // been matched up, FinalizeOSOFileRanges() should be called.
  bool AddOSOFileRange(CompileUnitInfo *cu_info, lldb::addr_t exe_file_addr,
                       lldb::addr_t exe_byte_size, lldb::addr_t oso_file_addr,
                       lldb::addr_t oso_byte_size);

  // Called after calling AddOSOFileRange() for each object file debug
  // map entry to finalize the info for the unlinked compile unit.
  void FinalizeOSOFileRanges(CompileUnitInfo *cu_info);

  /// Convert \a addr from a .o file address, to an executable address.
  ///
  /// \param[in] addr
  ///     A section offset address from a .o file
  ///
  /// \return
  ///     Returns true if \a addr was converted to be an executable
  ///     section/offset address, false otherwise.
  bool LinkOSOAddress(Address &addr);

  /// Convert a .o file "file address" to an executable "file address".
  ///
  /// \param[in] oso_symfile
  ///     The DWARF symbol file that contains \a oso_file_addr
  ///
  /// \param[in] oso_file_addr
  ///     A .o file "file address" to convert.
  ///
  /// \return
  ///     LLDB_INVALID_ADDRESS if \a oso_file_addr is not in the
  ///     linked executable, otherwise a valid "file address" from the
  ///     linked executable that contains the debug map.
  lldb::addr_t LinkOSOFileAddress(SymbolFileDWARF *oso_symfile,
                                  lldb::addr_t oso_file_addr);

  /// Given a line table full of lines with "file addresses" that are
  /// for a .o file represented by \a oso_symfile, link a new line table
  /// and return it.
  ///
  /// \param[in] oso_symfile
  ///     The DWARF symbol file that produced the \a line_table
  ///
  /// \param[in] line_table
  ///     A pointer to the line table.
  ///
  /// \return
  ///     Returns a valid line table full of linked addresses, or NULL
  ///     if none of the line table addresses exist in the main
  ///     executable.
  LineTable *LinkOSOLineTable(SymbolFileDWARF *oso_symfile,
                              LineTable *line_table);

  size_t AddOSOARanges(SymbolFileDWARF *dwarf2Data,
                       DWARFDebugAranges *debug_aranges);
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARFDEBUGMAP_H
