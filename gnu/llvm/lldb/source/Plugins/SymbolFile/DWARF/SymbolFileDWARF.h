//===-- SymbolFileDWARF.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARF_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARF_H

#include <list>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/Threading.h"

#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Expression/DWARFExpressionList.h"
#include "lldb/Symbol/DebugMacros.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Target/Statistics.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Flags.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-private.h"

#include "DWARFContext.h"
#include "DWARFDataExtractor.h"
#include "DWARFDefines.h"
#include "DWARFIndex.h"
#include "UniqueDWARFASTType.h"

class DWARFASTParserClang;

namespace llvm {
class DWARFDebugAbbrev;
} // namespace llvm

namespace lldb_private::plugin {
namespace dwarf {
// Forward Declarations for this DWARF plugin
class DebugMapModule;
class DWARFCompileUnit;
class DWARFDebugAranges;
class DWARFDebugInfo;
class DWARFDebugInfoEntry;
class DWARFDebugLine;
class DWARFDebugRanges;
class DWARFDeclContext;
class DWARFFormValue;
class DWARFTypeUnit;
class SymbolFileDWARFDebugMap;
class SymbolFileDWARFDwo;
class SymbolFileDWARFDwp;

#define DIE_IS_BEING_PARSED ((lldb_private::Type *)1)

class SymbolFileDWARF : public SymbolFileCommon {
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

  friend class SymbolFileDWARFDebugMap;
  friend class SymbolFileDWARFDwo;
  friend class DebugMapModule;
  friend class DWARFCompileUnit;
  friend class DWARFDIE;
  friend class DWARFASTParser;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static void DebuggerInitialize(Debugger &debugger);

  static llvm::StringRef GetPluginNameStatic() { return "dwarf"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static SymbolFile *CreateInstance(lldb::ObjectFileSP objfile_sp);

  // Constructors and Destructors

  SymbolFileDWARF(lldb::ObjectFileSP objfile_sp, SectionList *dwo_section_list);

  ~SymbolFileDWARF() override;

  uint32_t CalculateAbilities() override;

  void InitializeObject() override;

  // Compile Unit function calls

  lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) override;

  XcodeSDK ParseXcodeSDK(CompileUnit &comp_unit) override;

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

  std::optional<ArrayInfo>
  GetDynamicArrayInfoForUID(lldb::user_id_t type_uid,
                            const ExecutionContext *exe_ctx) override;

  bool CompleteType(CompilerType &compiler_type) override;

  Type *ResolveType(const DWARFDIE &die, bool assert_not_being_parsed = true,
                    bool resolve_function_context = false);

  CompilerDecl GetDeclForUID(lldb::user_id_t uid) override;

  CompilerDeclContext GetDeclContextForUID(lldb::user_id_t uid) override;

  CompilerDeclContext GetDeclContextContainingUID(lldb::user_id_t uid) override;

  std::vector<CompilerContext>
  GetCompilerContextForUID(lldb::user_id_t uid) override;

  void ParseDeclsForContext(CompilerDeclContext decl_ctx) override;

  uint32_t ResolveSymbolContext(const Address &so_addr,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContext &sc) override;

  Status CalculateFrameVariableError(StackFrame &frame) override;

  uint32_t ResolveSymbolContext(const SourceLocationSpec &src_location_spec,
                                lldb::SymbolContextItem resolve_scope,
                                SymbolContextList &sc_list) override;

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

  void
  GetMangledNamesForFunction(const std::string &scope_qualified_name,
                             std::vector<ConstString> &mangled_names) override;

  uint64_t GetDebugInfoSize(bool load_all_debug_info = false) override;

  void FindTypes(const lldb_private::TypeQuery &match,
                 lldb_private::TypeResults &results) override;

  void GetTypes(SymbolContextScope *sc_scope, lldb::TypeClass type_mask,
                TypeList &type_list) override;

  llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language) override;

  CompilerDeclContext FindNamespace(ConstString name,
                                    const CompilerDeclContext &parent_decl_ctx,
                                    bool only_root_namespaces) override;

  void PreloadSymbols() override;

  std::recursive_mutex &GetModuleMutex() const override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  llvm::DWARFDebugAbbrev *DebugAbbrev();

  DWARFDebugInfo &DebugInfo();

  DWARFDebugRanges *GetDebugRanges();

  static bool SupportedVersion(uint16_t version);

  DWARFDIE
  GetDeclContextDIEContainingDIE(const DWARFDIE &die);

  bool HasForwardDeclForCompilerType(const CompilerType &compiler_type);

  CompileUnit *GetCompUnitForDWARFCompUnit(DWARFCompileUnit &dwarf_cu);

  virtual void GetObjCMethods(ConstString class_name,
                              llvm::function_ref<bool(DWARFDIE die)> callback);

  bool Supports_DW_AT_APPLE_objc_complete_type(DWARFUnit *cu);

  DebugMacrosSP ParseDebugMacros(lldb::offset_t *offset);

  static DWARFDIE GetParentSymbolContextDIE(const DWARFDIE &die);

  lldb::ModuleSP GetExternalModule(ConstString name);

  typedef std::map<ConstString, lldb::ModuleSP> ExternalTypeModuleMap;

  /// Return the list of Clang modules imported by this SymbolFile.
  const ExternalTypeModuleMap &getExternalTypeModules() const {
    return m_external_type_modules;
  }

  /// Given a DIERef, find the correct SymbolFileDWARF.
  ///
  /// A DIERef contains a file index that can uniquely identify a N_OSO file for
  /// DWARF in .o files on mac, or a .dwo or .dwp file index for split DWARF.
  /// Calling this function will find the correct symbol file to use so that
  /// further lookups can be done on the correct symbol file so that the DIE
  /// offset makes sense in the DIERef.
  virtual SymbolFileDWARF *GetDIERefSymbolFile(const DIERef &die_ref);

  virtual DWARFDIE GetDIE(const DIERef &die_ref);

  DWARFDIE GetDIE(lldb::user_id_t uid);

  std::shared_ptr<SymbolFileDWARFDwo>
  GetDwoSymbolFileForCompileUnit(DWARFUnit &dwarf_cu,
                                 const DWARFDebugInfoEntry &cu_die);

  /// If this is a DWARF object with a single CU, return its DW_AT_dwo_id.
  std::optional<uint64_t> GetDWOId();

  /// Given a DWO DWARFUnit, find the corresponding skeleton DWARFUnit
  /// in the main symbol file. DWP files can have their DWARFUnits
  /// parsed without the skeleton compile units having been parsed, so
  /// sometimes we need to find the skeleton compile unit for a DWO
  /// DWARFUnit so we can fill in this link. Currently unless the
  /// skeleton compile unit has been parsed _and_ the Unit DIE has been
  /// parsed, the DWO unit will not have a backward link setup correctly
  /// which was causing crashes due to an assertion that was firing
  /// in SymbolFileDWARF::GetCompUnitForDWARFCompUnit().
  DWARFUnit *GetSkeletonUnit(DWARFUnit *dwo_unit);

  static bool DIEInDeclContext(const CompilerDeclContext &parent_decl_ctx,
                               const DWARFDIE &die,
                               bool only_root_namespaces = false);

  std::vector<std::unique_ptr<CallEdge>>
  ParseCallEdgesInFunction(UserID func_id) override;

  void Dump(Stream &s) override;

  void DumpClangAST(Stream &s) override;

  /// List separate dwo files.
  bool GetSeparateDebugInfo(StructuredData::Dictionary &d,
                            bool errors_only) override;

  DWARFContext &GetDWARFContext() { return m_context; }

  const std::shared_ptr<SymbolFileDWARFDwo> &GetDwpSymbolFile();

  FileSpec GetFile(DWARFUnit &unit, size_t file_idx);

  static llvm::Expected<lldb::TypeSystemSP> GetTypeSystem(DWARFUnit &unit);

  static DWARFASTParser *GetDWARFParser(DWARFUnit &unit);

  // CompilerDecl related functions

  static CompilerDecl GetDecl(const DWARFDIE &die);

  static CompilerDeclContext GetDeclContext(const DWARFDIE &die);

  static CompilerDeclContext GetContainingDeclContext(const DWARFDIE &die);

  static lldb::LanguageType LanguageTypeFromDWARF(uint64_t val);

  static lldb::LanguageType GetLanguage(DWARFUnit &unit);
  /// Same as GetLanguage() but reports all C++ versions as C++ (no version).
  static lldb::LanguageType GetLanguageFamily(DWARFUnit &unit);

  StatsDuration::Duration GetDebugInfoParseTime() override {
    return m_parse_time;
  }
  StatsDuration::Duration GetDebugInfoIndexTime() override;

  StatsDuration &GetDebugInfoParseTimeRef() { return m_parse_time; }

  virtual lldb::offset_t
  GetVendorDWARFOpcodeSize(const DataExtractor &data,
                           const lldb::offset_t data_offset,
                           const uint8_t op) const {
    return LLDB_INVALID_OFFSET;
  }

  virtual bool ParseVendorDWARFOpcode(uint8_t op, const DataExtractor &opcodes,
                                      lldb::offset_t &offset,
                                      std::vector<Value> &stack) const {
    return false;
  }

  ConstString ConstructFunctionDemangledName(const DWARFDIE &die);

  std::optional<uint64_t> GetFileIndex() const { return m_file_index; }
  void SetFileIndex(std::optional<uint64_t> file_index) {
    m_file_index = file_index;
  }

  typedef llvm::DenseMap<const DWARFDebugInfoEntry *, Type *> DIEToTypePtr;

  virtual DIEToTypePtr &GetDIEToType() { return m_die_to_type; }

  virtual llvm::DenseMap<lldb::opaque_compiler_type_t, DIERef> &
  GetForwardDeclCompilerTypeToDIE();

  typedef llvm::DenseMap<const DWARFDebugInfoEntry *, lldb::VariableSP>
      DIEToVariableSP;

  virtual DIEToVariableSP &GetDIEToVariable() { return m_die_to_variable_sp; }

  virtual UniqueDWARFASTTypeMap &GetUniqueDWARFASTTypeMap();

  bool ClassOrStructIsVirtual(const DWARFDIE &die);

  SymbolFileDWARFDebugMap *GetDebugMapSymfile();

  virtual DWARFDIE FindDefinitionDIE(const DWARFDIE &die);

  virtual lldb::TypeSP FindCompleteObjCDefinitionTypeForDIE(
      const DWARFDIE &die, ConstString type_name, bool must_be_implementation);

  Type *ResolveTypeUID(lldb::user_id_t type_uid) override;

  Type *ResolveTypeUID(const DWARFDIE &die, bool assert_not_being_parsed);

  Type *ResolveTypeUID(const DIERef &die_ref);

  /// Returns the DWARFIndex for this symbol, if it exists.
  DWARFIndex *getIndex() { return m_index.get(); }

protected:
  SymbolFileDWARF(const SymbolFileDWARF &) = delete;
  const SymbolFileDWARF &operator=(const SymbolFileDWARF &) = delete;

  virtual void LoadSectionData(lldb::SectionType sect_type,
                               DWARFDataExtractor &data);

  bool DeclContextMatchesThisSymbolFile(const CompilerDeclContext &decl_ctx);

  uint32_t CalculateNumCompileUnits() override;

  lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t index) override;

  TypeList &GetTypeList() override;

  lldb::CompUnitSP ParseCompileUnit(DWARFCompileUnit &dwarf_cu);

  virtual DWARFCompileUnit *GetDWARFCompileUnit(CompileUnit *comp_unit);

  DWARFUnit *GetNextUnparsedDWARFCompileUnit(DWARFUnit *prev_cu);

  bool GetFunction(const DWARFDIE &die, SymbolContext &sc);

  Function *ParseFunction(CompileUnit &comp_unit, const DWARFDIE &die);

  size_t ParseBlocksRecursive(CompileUnit &comp_unit, Block *parent_block,
                              const DWARFDIE &die,
                              lldb::addr_t subprogram_low_pc, uint32_t depth);

  size_t ParseTypes(const SymbolContext &sc, const DWARFDIE &die,
                    bool parse_siblings, bool parse_children);

  lldb::TypeSP ParseType(const SymbolContext &sc, const DWARFDIE &die,
                         bool *type_is_new);

  bool ParseSupportFiles(DWARFUnit &dwarf_cu, const lldb::ModuleSP &module,
                         SupportFileList &support_files);

  lldb::VariableSP ParseVariableDIE(const SymbolContext &sc,
                                    const DWARFDIE &die,
                                    const lldb::addr_t func_low_pc);
  lldb::VariableSP ParseVariableDIECached(const SymbolContext &sc,
                                          const DWARFDIE &die);

  void ParseAndAppendGlobalVariable(const SymbolContext &sc,
                                    const DWARFDIE &die,
                                    VariableList &cc_variable_list);

  size_t ParseVariablesInFunctionContext(const SymbolContext &sc,
                                         const DWARFDIE &die,
                                         const lldb::addr_t func_low_pc);

  size_t ParseVariablesInFunctionContextRecursive(const SymbolContext &sc,
                                                  const DWARFDIE &die,
                                                  lldb::addr_t func_low_pc,
                                                  DIEArray &accumulator);

  size_t PopulateBlockVariableList(VariableList &variable_list,
                                   const SymbolContext &sc,
                                   llvm::ArrayRef<DIERef> variable_dies,
                                   lldb::addr_t func_low_pc);

  DIEArray MergeBlockAbstractParameters(const DWARFDIE &block_die,
                                        DIEArray &&variable_dies);

  // Given a die_offset, figure out the symbol context representing that die.
  bool ResolveFunction(const DWARFDIE &die, bool include_inlines,
                       SymbolContextList &sc_list);

  /// Resolve functions and (possibly) blocks for the given file address and a
  /// compile unit. The compile unit comes from the sc argument and it must be
  /// set. The results of the lookup (if any) are written back to the symbol
  /// context.
  void ResolveFunctionAndBlock(lldb::addr_t file_vm_addr, bool lookup_block,
                               SymbolContext &sc);

  Symbol *GetObjCClassSymbol(ConstString objc_class_name);

  lldb::TypeSP GetTypeForDIE(const DWARFDIE &die,
                             bool resolve_function_context = false);

  void SetDebugMapModule(const lldb::ModuleSP &module_sp) {
    m_debug_map_module_wp = module_sp;
  }

  DWARFDIE
  FindBlockContainingSpecification(const DIERef &func_die_ref,
                                   dw_offset_t spec_block_die_offset);

  DWARFDIE
  FindBlockContainingSpecification(const DWARFDIE &die,
                                   dw_offset_t spec_block_die_offset);

  bool ClassContainsSelector(const DWARFDIE &class_die, ConstString selector);

  /// Parse call site entries (DW_TAG_call_site), including any nested call site
  /// parameters (DW_TAG_call_site_parameter).
  std::vector<std::unique_ptr<CallEdge>>
  CollectCallEdges(lldb::ModuleSP module, DWARFDIE function_die);

  /// If this symbol file is linked to by a debug map (see
  /// SymbolFileDWARFDebugMap), and \p file_addr is a file address relative to
  /// an object file, adjust \p file_addr so that it is relative to the main
  /// binary. Returns the adjusted address, or \p file_addr if no adjustment is
  /// needed, on success and LLDB_INVALID_ADDRESS otherwise.
  lldb::addr_t FixupAddress(lldb::addr_t file_addr);

  bool FixupAddress(Address &addr);

  typedef llvm::SetVector<Type *> TypeSet;

  void GetTypes(const DWARFDIE &die, dw_offset_t min_die_offset,
                dw_offset_t max_die_offset, uint32_t type_mask,
                TypeSet &type_set);

  typedef RangeDataVector<lldb::addr_t, lldb::addr_t, Variable *>
      GlobalVariableMap;

  GlobalVariableMap &GetGlobalAranges();

  void UpdateExternalModuleListIfNeeded();

  void BuildCuTranslationTable();
  std::optional<uint32_t> GetDWARFUnitIndex(uint32_t cu_idx);

  void FindDwpSymbolFile();

  const SupportFileList *GetTypeUnitSupportFiles(DWARFTypeUnit &tu);

  void InitializeFirstCodeAddressRecursive(const SectionList &section_list);

  void InitializeFirstCodeAddress();

  void
  GetCompileOptions(std::unordered_map<lldb::CompUnitSP, Args> &args) override;

  lldb::ModuleWP m_debug_map_module_wp;
  SymbolFileDWARFDebugMap *m_debug_map_symfile;

  llvm::once_flag m_dwp_symfile_once_flag;
  std::shared_ptr<SymbolFileDWARFDwo> m_dwp_symfile;

  DWARFContext m_context;

  llvm::once_flag m_info_once_flag;
  std::unique_ptr<DWARFDebugInfo> m_info;

  std::unique_ptr<llvm::DWARFDebugAbbrev> m_abbr;
  std::unique_ptr<GlobalVariableMap> m_global_aranges_up;

  typedef std::unordered_map<lldb::offset_t, DebugMacrosSP> DebugMacrosMap;
  DebugMacrosMap m_debug_macros_map;

  ExternalTypeModuleMap m_external_type_modules;
  std::unique_ptr<DWARFIndex> m_index;
  bool m_fetched_external_modules : 1;
  LazyBool m_supports_DW_AT_APPLE_objc_complete_type;

  typedef std::set<DIERef> DIERefSet;
  typedef llvm::StringMap<DIERefSet> NameToOffsetMap;
  NameToOffsetMap m_function_scope_qualified_name_map;
  std::unique_ptr<DWARFDebugRanges> m_ranges;
  UniqueDWARFASTTypeMap m_unique_ast_type_map;
  // A map from DIE to lldb_private::Type. For record type, the key might be
  // either declaration DIE or definition DIE.
  DIEToTypePtr m_die_to_type;
  DIEToVariableSP m_die_to_variable_sp;
  // A map from CompilerType to the struct/class/union/enum DIE (might be a
  // declaration or a definition) that is used to construct it.
  llvm::DenseMap<lldb::opaque_compiler_type_t, DIERef>
      m_forward_decl_compiler_type_to_die;
  llvm::DenseMap<dw_offset_t, std::unique_ptr<SupportFileList>>
      m_type_unit_support_files;
  std::vector<uint32_t> m_lldb_cu_to_dwarf_unit;
  /// DWARF does not provide a good way for traditional (concatenating) linkers
  /// to invalidate debug info describing dead-stripped code. These linkers will
  /// keep the debug info but resolve any addresses referring to such code as
  /// zero (BFD) or a small positive integer (zero + relocation addend -- GOLD).
  /// Try to filter out this debug info by comparing it to the lowest code
  /// address in the module.
  lldb::addr_t m_first_code_address = LLDB_INVALID_ADDRESS;
  StatsDuration m_parse_time;
  std::atomic_flag m_dwo_warning_issued = ATOMIC_FLAG_INIT;
  /// If this DWARF file a .DWO file or a DWARF .o file on mac when
  /// no dSYM file is being used, this file index will be set to a
  /// valid value that can be used in DIERef objects which will contain
  /// an index that identifies the .DWO or .o file.
  std::optional<uint64_t> m_file_index;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_SYMBOLFILEDWARF_H
