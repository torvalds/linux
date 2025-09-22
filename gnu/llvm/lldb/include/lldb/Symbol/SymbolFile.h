//===-- SymbolFile.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_SYMBOLFILE_H
#define LLDB_SYMBOL_SYMBOLFILE_H

#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/SourceLocationSpec.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SourceModule.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Statistics.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/XcodeSDK.h"
#include "lldb/lldb-private.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/Errc.h"

#include <mutex>
#include <optional>
#include <unordered_map>

#if defined(LLDB_CONFIGURATION_DEBUG)
#define ASSERT_MODULE_LOCK(expr) (expr->AssertModuleLock())
#else
#define ASSERT_MODULE_LOCK(expr) ((void)0)
#endif

namespace lldb_private {

/// Provides public interface for all SymbolFiles. Any protected
/// virtual members should go into SymbolFileCommon; most SymbolFile
/// implementations should inherit from SymbolFileCommon to override
/// the behaviors except SymbolFileOnDemand which inherits
/// public interfaces from SymbolFile and forward to underlying concrete
/// SymbolFile implementation.
class SymbolFile : public PluginInterface {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  /// \{
  virtual bool isA(const void *ClassID) const { return ClassID == &ID; }
  static bool classof(const SymbolFile *obj) { return obj->isA(&ID); }
  /// \}

  // Symbol file ability bits.
  //
  // Each symbol file can claim to support one or more symbol file abilities.
  // These get returned from SymbolFile::GetAbilities(). These help us to
  // determine which plug-in will be best to load the debug information found
  // in files.
  enum Abilities {
    CompileUnits = (1u << 0),
    LineTables = (1u << 1),
    Functions = (1u << 2),
    Blocks = (1u << 3),
    GlobalVariables = (1u << 4),
    LocalVariables = (1u << 5),
    VariableTypes = (1u << 6),
    kAllAbilities = ((1u << 7) - 1u)
  };

  static SymbolFile *FindPlugin(lldb::ObjectFileSP objfile_sp);

  // Constructors and Destructors
  SymbolFile() = default;

  ~SymbolFile() override = default;

  /// SymbolFileOnDemand class overrides this to return the underlying
  /// backing SymbolFile implementation that loads on-demand.
  virtual SymbolFile *GetBackingSymbolFile() { return this; }

  /// Get a mask of what this symbol file supports for the object file
  /// that it was constructed with.
  ///
  /// Each symbol file gets to respond with a mask of abilities that
  /// it supports for each object file. This happens when we are
  /// trying to figure out which symbol file plug-in will get used
  /// for a given object file. The plug-in that responds with the
  /// best mix of "SymbolFile::Abilities" bits set, will get chosen to
  /// be the symbol file parser. This allows each plug-in to check for
  /// sections that contain data a symbol file plug-in would need. For
  /// example the DWARF plug-in requires DWARF sections in a file that
  /// contain debug information. If the DWARF plug-in doesn't find
  /// these sections, it won't respond with many ability bits set, and
  /// we will probably fall back to the symbol table SymbolFile plug-in
  /// which uses any information in the symbol table. Also, plug-ins
  /// might check for some specific symbols in a symbol table in the
  /// case where the symbol table contains debug information (STABS
  /// and COFF). Not a lot of work should happen in these functions
  /// as the plug-in might not get selected due to another plug-in
  /// having more abilities. Any initialization work should be saved
  /// for "void SymbolFile::InitializeObject()" which will get called
  /// on the SymbolFile object with the best set of abilities.
  ///
  /// \return
  ///     A uint32_t mask containing bits from the SymbolFile::Abilities
  ///     enumeration. Any bits that are set represent an ability that
  ///     this symbol plug-in can parse from the object file.
  virtual uint32_t GetAbilities() = 0;
  virtual uint32_t CalculateAbilities() = 0;

  /// Symbols file subclasses should override this to return the Module that
  /// owns the TypeSystem that this symbol file modifies type information in.
  virtual std::recursive_mutex &GetModuleMutex() const;

  /// Initialize the SymbolFile object.
  ///
  /// The SymbolFile object with the best set of abilities (detected
  /// in "uint32_t SymbolFile::GetAbilities()) will have this function
  /// called if it is chosen to parse an object file. More complete
  /// initialization can happen in this function which will get called
  /// prior to any other functions in the SymbolFile protocol.
  virtual void InitializeObject() {}

  /// Whether debug info will be loaded or not.
  ///
  /// It will be true for most implementations except SymbolFileOnDemand.
  virtual bool GetLoadDebugInfoEnabled() { return true; }

  /// Specify debug info should be loaded.
  ///
  /// It will be no-op for most implementations except SymbolFileOnDemand.
  virtual void SetLoadDebugInfoEnabled() {}

  // Compile Unit function calls
  // Approach 1 - iterator
  virtual uint32_t GetNumCompileUnits() = 0;
  virtual lldb::CompUnitSP GetCompileUnitAtIndex(uint32_t idx) = 0;

  virtual Symtab *GetSymtab() = 0;

  virtual lldb::LanguageType ParseLanguage(CompileUnit &comp_unit) = 0;
  /// Return the Xcode SDK comp_unit was compiled against.
  virtual XcodeSDK ParseXcodeSDK(CompileUnit &comp_unit) { return {}; }

  /// This function exists because SymbolFileDWARFDebugMap may extra compile
  /// units which aren't exposed as "real" compile units. In every other
  /// case this function should behave identically as ParseLanguage.
  virtual llvm::SmallSet<lldb::LanguageType, 4>
  ParseAllLanguages(CompileUnit &comp_unit) {
    llvm::SmallSet<lldb::LanguageType, 4> langs;
    langs.insert(ParseLanguage(comp_unit));
    return langs;
  }

  virtual size_t ParseFunctions(CompileUnit &comp_unit) = 0;
  virtual bool ParseLineTable(CompileUnit &comp_unit) = 0;
  virtual bool ParseDebugMacros(CompileUnit &comp_unit) = 0;

  /// Apply a lambda to each external lldb::Module referenced by this
  /// \p comp_unit. Recursively also descends into the referenced external
  /// modules of any encountered compilation unit.
  ///
  /// This function can be used to traverse Clang -gmodules debug
  /// information, which is stored in DWARF files separate from the
  /// object files.
  ///
  /// \param comp_unit
  ///     When this SymbolFile consists of multiple auxilliary
  ///     SymbolFiles, for example, a Darwin debug map that references
  ///     multiple .o files, comp_unit helps choose the auxilliary
  ///     file. In most other cases comp_unit's symbol file is
  ///     identical with *this.
  ///
  /// \param[in] lambda
  ///     The lambda that should be applied to every function. The lambda can
  ///     return true if the iteration should be aborted earlier.
  ///
  /// \param visited_symbol_files
  ///     A set of SymbolFiles that were already visited to avoid
  ///     visiting one file more than once.
  ///
  /// \return
  ///     If the lambda early-exited, this function returns true to
  ///     propagate the early exit.
  virtual bool ForEachExternalModule(
      lldb_private::CompileUnit &comp_unit,
      llvm::DenseSet<lldb_private::SymbolFile *> &visited_symbol_files,
      llvm::function_ref<bool(Module &)> lambda) {
    return false;
  }
  virtual bool ParseSupportFiles(CompileUnit &comp_unit,
                                 SupportFileList &support_files) = 0;
  virtual size_t ParseTypes(CompileUnit &comp_unit) = 0;
  virtual bool ParseIsOptimized(CompileUnit &comp_unit) { return false; }

  virtual bool
  ParseImportedModules(const SymbolContext &sc,
                       std::vector<SourceModule> &imported_modules) = 0;
  virtual size_t ParseBlocksRecursive(Function &func) = 0;
  virtual size_t ParseVariablesForContext(const SymbolContext &sc) = 0;
  virtual Type *ResolveTypeUID(lldb::user_id_t type_uid) = 0;

  /// The characteristics of an array type.
  struct ArrayInfo {
    int64_t first_index = 0;
    llvm::SmallVector<uint64_t, 1> element_orders;
    uint32_t byte_stride = 0;
    uint32_t bit_stride = 0;
  };
  /// If \c type_uid points to an array type, return its characteristics.
  /// To support variable-length array types, this function takes an
  /// optional \p ExecutionContext. If \c exe_ctx is non-null, the
  /// dynamic characteristics for that context are returned.
  virtual std::optional<ArrayInfo>
  GetDynamicArrayInfoForUID(lldb::user_id_t type_uid,
                            const lldb_private::ExecutionContext *exe_ctx) = 0;

  virtual bool CompleteType(CompilerType &compiler_type) = 0;
  virtual void ParseDeclsForContext(CompilerDeclContext decl_ctx) {}
  virtual CompilerDecl GetDeclForUID(lldb::user_id_t uid) { return {}; }
  virtual CompilerDeclContext GetDeclContextForUID(lldb::user_id_t uid) {
    return {};
  }
  virtual CompilerDeclContext GetDeclContextContainingUID(lldb::user_id_t uid) {
    return {};
  }
  virtual std::vector<CompilerContext>
  GetCompilerContextForUID(lldb::user_id_t uid) {
    return {};
  }
  virtual uint32_t ResolveSymbolContext(const Address &so_addr,
                                        lldb::SymbolContextItem resolve_scope,
                                        SymbolContext &sc) = 0;

  /// Get an error that describes why variables might be missing for a given
  /// symbol context.
  ///
  /// If there is an error in the debug information that prevents variables from
  /// being fetched, this error will get filled in. If there is no debug
  /// informaiton, no error should be returned. But if there is debug
  /// information and something prevents the variables from being available a
  /// valid error should be returned. Valid cases include:
  /// - compiler option that removes variables (-gline-tables-only)
  /// - missing external files
  ///   - .dwo files in fission are not accessible or missing
  ///   - .o files on darwin when not using dSYM files that are not accessible
  ///     or missing
  /// - mismatched exteral files
  ///   - .dwo files in fission where the DWO ID doesn't match
  ///   - .o files on darwin when modification timestamp doesn't match
  /// - corrupted debug info
  ///
  /// \param[in] frame
  ///   The stack frame to use as a basis for the context to check. The frame
  ///   address can be used if there is not debug info due to it not being able
  ///   to be loaded, or if there is a debug info context, like a compile unit,
  ///   or function, it can be used to track down more information on why
  ///   variables are missing.
  ///
  /// \returns
  ///   An error specifying why there should have been debug info with variable
  ///   information but the variables were not able to be resolved.
  Status GetFrameVariableError(StackFrame &frame) {
    Status err = CalculateFrameVariableError(frame);
    if (err.Fail())
      SetDebugInfoHadFrameVariableErrors();
    return err;
  }

  /// Subclasses will override this function to for GetFrameVariableError().
  ///
  /// This allows GetFrameVariableError() to set the member variable
  /// m_debug_info_had_variable_errors correctly without users having to do it
  /// manually which is error prone.
  virtual Status CalculateFrameVariableError(StackFrame &frame) {
    return Status();
  }
  virtual uint32_t
  ResolveSymbolContext(const SourceLocationSpec &src_location_spec,
                       lldb::SymbolContextItem resolve_scope,
                       SymbolContextList &sc_list);

  virtual void DumpClangAST(Stream &s) {}
  virtual void FindGlobalVariables(ConstString name,
                                   const CompilerDeclContext &parent_decl_ctx,
                                   uint32_t max_matches,
                                   VariableList &variables);
  virtual void FindGlobalVariables(const RegularExpression &regex,
                                   uint32_t max_matches,
                                   VariableList &variables);
  virtual void FindFunctions(const Module::LookupInfo &lookup_info,
                             const CompilerDeclContext &parent_decl_ctx,
                             bool include_inlines, SymbolContextList &sc_list);
  virtual void FindFunctions(const RegularExpression &regex,
                             bool include_inlines, SymbolContextList &sc_list);

  /// Find types using a type-matching object that contains all search
  /// parameters.
  ///
  /// \see lldb_private::TypeQuery
  ///
  /// \param[in] query
  ///     A type matching object that contains all of the details of the type
  ///     search.
  ///
  /// \param[in] results
  ///     Any matching types will be populated into the \a results object using
  ///     TypeMap::InsertUnique(...).
  virtual void FindTypes(const TypeQuery &query, TypeResults &results) {}

  virtual void
  GetMangledNamesForFunction(const std::string &scope_qualified_name,
                             std::vector<ConstString> &mangled_names);

  virtual void GetTypes(lldb_private::SymbolContextScope *sc_scope,
                        lldb::TypeClass type_mask,
                        lldb_private::TypeList &type_list) = 0;

  virtual void PreloadSymbols();

  virtual llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language) = 0;

  /// Finds a namespace of name \ref name and whose parent
  /// context is \ref parent_decl_ctx.
  ///
  /// If \code{.cpp} !parent_decl_ctx.IsValid() \endcode
  /// then this function will consider all namespaces that
  /// match the name. If \ref only_root_namespaces is
  /// true, only consider in the search those DIEs that
  /// represent top-level namespaces.
  virtual CompilerDeclContext
  FindNamespace(ConstString name, const CompilerDeclContext &parent_decl_ctx,
                bool only_root_namespaces = false) {
    return CompilerDeclContext();
  }

  virtual ObjectFile *GetObjectFile() = 0;
  virtual const ObjectFile *GetObjectFile() const = 0;
  virtual ObjectFile *GetMainObjectFile() = 0;

  virtual std::vector<std::unique_ptr<CallEdge>>
  ParseCallEdgesInFunction(UserID func_id) {
    return {};
  }

  virtual void AddSymbols(Symtab &symtab) {}

  /// Notify the SymbolFile that the file addresses in the Sections
  /// for this module have been changed.
  virtual void SectionFileAddressesChanged() = 0;

  struct RegisterInfoResolver {
    virtual ~RegisterInfoResolver(); // anchor

    virtual const RegisterInfo *ResolveName(llvm::StringRef name) const = 0;
    virtual const RegisterInfo *ResolveNumber(lldb::RegisterKind kind,
                                              uint32_t number) const = 0;
  };
  virtual lldb::UnwindPlanSP
  GetUnwindPlan(const Address &address, const RegisterInfoResolver &resolver) {
    return nullptr;
  }

  /// Return the number of stack bytes taken up by the parameters to this
  /// function.
  virtual llvm::Expected<lldb::addr_t> GetParameterStackSize(Symbol &symbol) {
    return llvm::createStringError(make_error_code(llvm::errc::not_supported),
                                   "Operation not supported.");
  }

  virtual void Dump(Stream &s) = 0;

  /// Metrics gathering functions

  /// Return the size in bytes of all loaded debug information or total possible
  /// debug info in the symbol file.
  ///
  /// If the debug information is contained in sections of an ObjectFile, then
  /// this call should add the size of all sections that contain debug
  /// information. Symbols the symbol tables are not considered debug
  /// information for this call to make it easy and quick for this number to be
  /// calculated. If the symbol file is all debug information, the size of the
  /// entire file should be returned. The default implementation of this
  /// function will iterate over all sections in a module and add up their
  /// debug info only section byte sizes.
  ///
  /// \param load_all_debug_info
  ///   If true, force loading any symbol files if they are not yet loaded and
  ///   add to the total size. Default to false.
  ///
  /// \returns
  ///   Total currently loaded debug info size in bytes
  virtual uint64_t GetDebugInfoSize(bool load_all_debug_info = false) = 0;

  /// Return the time taken to parse the debug information.
  ///
  /// \returns 0.0 if no information has been parsed or if there is
  /// no computational cost to parsing the debug information.
  virtual StatsDuration::Duration GetDebugInfoParseTime() { return {}; }

  /// Return the time it took to index the debug information in the object
  /// file.
  ///
  /// \returns 0.0 if the file doesn't need to be indexed or if it
  /// hasn't been indexed yet, or a valid duration if it has.
  virtual StatsDuration::Duration GetDebugInfoIndexTime() { return {}; }

  /// Get the additional modules that this symbol file uses to parse debug info.
  ///
  /// Some debug info is stored in stand alone object files that are represented
  /// by unique modules that will show up in the statistics module list. Return
  /// a list of modules that are not in the target module list that this symbol
  /// file is currently using so that they can be tracked and assoicated with
  /// the module in the statistics.
  virtual ModuleList GetDebugInfoModules() { return ModuleList(); }

  /// Accessors for the bool that indicates if the debug info index was loaded
  /// from, or saved to the module index cache.
  ///
  /// In statistics it is handy to know if a module's debug info was loaded from
  /// or saved to the cache. When the debug info index is loaded from the cache
  /// startup times can be faster. When the cache is enabled and the debug info
  /// index is saved to the cache, debug sessions can be slower. These accessors
  /// can be accessed by the statistics and emitted to help track these costs.
  /// \{
  virtual bool GetDebugInfoIndexWasLoadedFromCache() const = 0;
  virtual void SetDebugInfoIndexWasLoadedFromCache() = 0;
  virtual bool GetDebugInfoIndexWasSavedToCache() const = 0;
  virtual void SetDebugInfoIndexWasSavedToCache() = 0;
  /// \}

  /// Accessors for the bool that indicates if there was debug info, but errors
  /// stopped variables from being able to be displayed correctly. See
  /// GetFrameVariableError() for details on what are considered errors.
  virtual bool GetDebugInfoHadFrameVariableErrors() const = 0;
  virtual void SetDebugInfoHadFrameVariableErrors() = 0;

  /// Return true if separate debug info files are supported and this function
  /// succeeded, false otherwise.
  ///
  /// \param[out] d
  ///     If this function succeeded, then this will be a dictionary that
  ///     contains the keys "type", "symfile", and "separate-debug-info-files".
  ///     "type" can be used to assume the structure of each object in
  ///     "separate-debug-info-files".
  /// \param errors_only
  ///     If true, then only return separate debug info files that encountered
  ///     errors during loading. If false, then return all expected separate
  ///     debug info files, regardless of whether they were successfully loaded.
  virtual bool GetSeparateDebugInfo(StructuredData::Dictionary &d,
                                    bool errors_only) {
    return false;
  };

  virtual lldb::TypeSP
  MakeType(lldb::user_id_t uid, ConstString name,
           std::optional<uint64_t> byte_size, SymbolContextScope *context,
           lldb::user_id_t encoding_uid,
           Type::EncodingDataType encoding_uid_type, const Declaration &decl,
           const CompilerType &compiler_qual_type,
           Type::ResolveState compiler_type_resolve_state,
           uint32_t opaque_payload = 0) = 0;

  virtual lldb::TypeSP CopyType(const lldb::TypeSP &other_type) = 0;

  /// Returns a map of compilation unit to the compile option arguments
  /// associated with that compilation unit.
  std::unordered_map<lldb::CompUnitSP, Args> GetCompileOptions() {
    std::unordered_map<lldb::CompUnitSP, Args> args;
    GetCompileOptions(args);
    return args;
  }

protected:
  void AssertModuleLock();

  virtual void GetCompileOptions(
      std::unordered_map<lldb::CompUnitSP, lldb_private::Args> &args) {}

private:
  SymbolFile(const SymbolFile &) = delete;
  const SymbolFile &operator=(const SymbolFile &) = delete;
};

/// Containing protected virtual methods for child classes to override.
/// Most actual SymbolFile implementations should inherit from this class.
class SymbolFileCommon : public SymbolFile {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  /// \{
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || SymbolFile::isA(ClassID);
  }
  static bool classof(const SymbolFileCommon *obj) { return obj->isA(&ID); }
  /// \}

  // Constructors and Destructors
  SymbolFileCommon(lldb::ObjectFileSP objfile_sp)
      : m_objfile_sp(std::move(objfile_sp)) {}

  ~SymbolFileCommon() override = default;

  uint32_t GetAbilities() override {
    if (!m_calculated_abilities) {
      m_abilities = CalculateAbilities();
      m_calculated_abilities = true;
    }
    return m_abilities;
  }

  Symtab *GetSymtab() override;

  ObjectFile *GetObjectFile() override { return m_objfile_sp.get(); }
  const ObjectFile *GetObjectFile() const override {
    return m_objfile_sp.get();
  }
  ObjectFile *GetMainObjectFile() override;

  /// Notify the SymbolFile that the file addresses in the Sections
  /// for this module have been changed.
  void SectionFileAddressesChanged() override;

  // Compile Unit function calls
  // Approach 1 - iterator
  uint32_t GetNumCompileUnits() override;
  lldb::CompUnitSP GetCompileUnitAtIndex(uint32_t idx) override;

  llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language) override;

  void Dump(Stream &s) override;

  uint64_t GetDebugInfoSize(bool load_all_debug_info = false) override;

  bool GetDebugInfoIndexWasLoadedFromCache() const override {
    return m_index_was_loaded_from_cache;
  }
  void SetDebugInfoIndexWasLoadedFromCache() override {
    m_index_was_loaded_from_cache = true;
  }
  bool GetDebugInfoIndexWasSavedToCache() const override {
    return m_index_was_saved_to_cache;
  }
  void SetDebugInfoIndexWasSavedToCache() override {
    m_index_was_saved_to_cache = true;
  }
  bool GetDebugInfoHadFrameVariableErrors() const override {
    return m_debug_info_had_variable_errors;
  }
  void SetDebugInfoHadFrameVariableErrors() override {
     m_debug_info_had_variable_errors = true;
  }

  /// This function is used to create types that belong to a SymbolFile. The
  /// symbol file will own a strong reference to the type in an internal type
  /// list.
  lldb::TypeSP MakeType(lldb::user_id_t uid, ConstString name,
                        std::optional<uint64_t> byte_size,
                        SymbolContextScope *context,
                        lldb::user_id_t encoding_uid,
                        Type::EncodingDataType encoding_uid_type,
                        const Declaration &decl,
                        const CompilerType &compiler_qual_type,
                        Type::ResolveState compiler_type_resolve_state,
                        uint32_t opaque_payload = 0) override {
     lldb::TypeSP type_sp (new Type(
         uid, this, name, byte_size, context, encoding_uid,
         encoding_uid_type, decl, compiler_qual_type,
         compiler_type_resolve_state, opaque_payload));
     m_type_list.Insert(type_sp);
     return type_sp;
  }

  lldb::TypeSP CopyType(const lldb::TypeSP &other_type) override {
     // Make sure the real symbol file matches when copying types.
     if (GetBackingSymbolFile() != other_type->GetSymbolFile())
      return lldb::TypeSP();
     lldb::TypeSP type_sp(new Type(*other_type));
     m_type_list.Insert(type_sp);
     return type_sp;
  }

protected:
  virtual uint32_t CalculateNumCompileUnits() = 0;
  virtual lldb::CompUnitSP ParseCompileUnitAtIndex(uint32_t idx) = 0;
  virtual TypeList &GetTypeList() { return m_type_list; }
  void SetCompileUnitAtIndex(uint32_t idx, const lldb::CompUnitSP &cu_sp);

  lldb::ObjectFileSP m_objfile_sp; // Keep a reference to the object file in
                                   // case it isn't the same as the module
                                   // object file (debug symbols in a separate
                                   // file)
  std::optional<std::vector<lldb::CompUnitSP>> m_compile_units;
  TypeList m_type_list;
  uint32_t m_abilities = 0;
  bool m_calculated_abilities = false;
  bool m_index_was_loaded_from_cache = false;
  bool m_index_was_saved_to_cache = false;
  /// Set to true if any variable feteching errors have been found when calling
  /// GetFrameVariableError(). This will be emitted in the "statistics dump"
  /// information for a module.
  bool m_debug_info_had_variable_errors = false;

private:
  SymbolFileCommon(const SymbolFileCommon &) = delete;
  const SymbolFileCommon &operator=(const SymbolFileCommon &) = delete;

  /// Do not use m_symtab directly, as it may be freed. Use GetSymtab()
  /// to access it instead.
  Symtab *m_symtab = nullptr;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_SYMBOLFILE_H
