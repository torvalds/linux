//===-- CompileUnit.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_COMPILEUNIT_H
#define LLDB_SYMBOL_COMPILEUNIT_H

#include "lldb/Core/ModuleChild.h"
#include "lldb/Core/SourceLocationSpec.h"
#include "lldb/Symbol/DebugMacros.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SourceModule.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/UserID.h"
#include "lldb/lldb-enumerations.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace lldb_private {
/// \class CompileUnit CompileUnit.h "lldb/Symbol/CompileUnit.h"
/// A class that describes a compilation unit.
///
/// A representation of a compilation unit, or compiled source file.
/// The UserID of the compile unit is specified by the SymbolFile plug-in and
/// can have any value as long as the value is unique within the Module that
/// owns this compile units.
///
/// Each compile unit has a list of functions, global and static variables,
/// support file list (include files and inlined source files), and a line
/// table.
class CompileUnit : public std::enable_shared_from_this<CompileUnit>,
                    public ModuleChild,
                    public UserID,
                    public SymbolContextScope {
public:
  /// Construct with a module, path, UID and language.
  ///
  /// Initialize the compile unit given the owning \a module, a path to
  /// convert into a FileSpec, the SymbolFile plug-in supplied \a uid, and the
  /// source language type.
  ///
  /// \param[in] module_sp
  ///     The parent module that owns this compile unit. This value
  ///     must be a valid pointer value.
  ///
  /// \param[in] user_data
  ///     User data where the SymbolFile parser can store data.
  ///
  /// \param[in] pathname
  ///     The path to the source file for this compile unit.
  ///
  /// \param[in] uid
  ///     The user ID of the compile unit. This value is supplied by
  ///     the SymbolFile plug-in and should be a value that allows
  ///     the SymbolFile plug-in to easily locate and parse additional
  ///     information for the compile unit.
  ///
  /// \param[in] language
  ///     A language enumeration type that describes the main language
  ///     of this compile unit.
  ///
  /// \param[in] is_optimized
  ///     A value that can initialized with eLazyBoolYes, eLazyBoolNo
  ///     or eLazyBoolCalculate. If set to eLazyBoolCalculate, then
  ///     an extra call into SymbolVendor will be made to calculate if
  ///     the compile unit is optimized will be made when
  ///     CompileUnit::GetIsOptimized() is called.
  ///
  /// \see lldb::LanguageType
  CompileUnit(const lldb::ModuleSP &module_sp, void *user_data,
              const char *pathname, lldb::user_id_t uid,
              lldb::LanguageType language, lldb_private::LazyBool is_optimized);

  /// Construct with a module, file spec, UID and language.
  ///
  /// Initialize the compile unit given the owning \a module, a path to
  /// convert into a FileSpec, the SymbolFile plug-in supplied \a uid, and the
  /// source language type.
  ///
  /// \param[in] module_sp
  ///     The parent module that owns this compile unit. This value
  ///     must be a valid pointer value.
  ///
  /// \param[in] user_data
  ///     User data where the SymbolFile parser can store data.
  ///
  /// \param[in] support_file_sp
  ///     The file specification for the source file of this compile
  ///     unit.
  ///
  /// \param[in] uid
  ///     The user ID of the compile unit. This value is supplied by
  ///     the SymbolFile plug-in and should be a value that allows
  ///     the plug-in to easily locate and parse
  ///     additional information for the compile unit.
  ///
  /// \param[in] language
  ///     A language enumeration type that describes the main language
  ///     of this compile unit.
  ///
  /// \param[in] is_optimized
  ///     A value that can initialized with eLazyBoolYes, eLazyBoolNo
  ///     or eLazyBoolCalculate. If set to eLazyBoolCalculate, then
  ///     an extra call into SymbolVendor will be made to calculate if
  ///     the compile unit is optimized will be made when
  ///     CompileUnit::GetIsOptimized() is called.
  ///
  /// \param[in] support_files
  ///     An rvalue list of already parsed support files.
  /// \see lldb::LanguageType
  CompileUnit(const lldb::ModuleSP &module_sp, void *user_data,
              lldb::SupportFileSP support_file_sp, lldb::user_id_t uid,
              lldb::LanguageType language, lldb_private::LazyBool is_optimized,
              SupportFileList &&support_files = {});

  /// Add a function to this compile unit.
  ///
  /// Typically called by the SymbolFile plug-ins as they partially parse the
  /// debug information.
  ///
  /// \param[in] function_sp
  ///     A shared pointer to the Function object.
  void AddFunction(lldb::FunctionSP &function_sp);

  /// \copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  ///
  /// \see SymbolContextScope
  void CalculateSymbolContext(SymbolContext *sc) override;

  lldb::ModuleSP CalculateSymbolContextModule() override;

  CompileUnit *CalculateSymbolContextCompileUnit() override;

  /// \copydoc SymbolContextScope::DumpSymbolContext(Stream*)
  ///
  /// \see SymbolContextScope
  void DumpSymbolContext(Stream *s) override;

  lldb::LanguageType GetLanguage();

  void SetLanguage(lldb::LanguageType language) {
    m_flags.Set(flagsParsedLanguage);
    m_language = language;
  }

  void GetDescription(Stream *s, lldb::DescriptionLevel level) const;

  /// Apply a lambda to each function in this compile unit.
  ///
  /// This provides raw access to the function shared pointer list and will not
  /// cause the SymbolFile plug-in to parse any unparsed functions.
  ///
  /// \note Prefer using FindFunctionByUID over this if possible.
  ///
  /// \param[in] lambda
  ///     The lambda that should be applied to every function. The lambda can
  ///     return true if the iteration should be aborted earlier.
  void ForeachFunction(
      llvm::function_ref<bool(const lldb::FunctionSP &)> lambda) const;

  /// Find a function in the compile unit based on the predicate matching_lambda
  ///
  /// \param[in] matching_lambda
  ///     A predicate that will be used within FindFunction to evaluate each
  ///     FunctionSP in m_functions_by_uid. When the predicate returns true
  ///     FindFunction will return the corresponding FunctionSP.
  ///
  /// \return
  ///   The first FunctionSP that the matching_lambda prediate returns true for.
  lldb::FunctionSP FindFunction(
      llvm::function_ref<bool(const lldb::FunctionSP &)> matching_lambda);

  /// Dump the compile unit contents to the stream \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// \param[in] show_context
  ///     If \b true, variables will dump their symbol context
  ///     information.
  void Dump(Stream *s, bool show_context) const;

  /// Find the line entry by line and optional inlined file spec.
  ///
  /// Finds the first line entry that has an index greater than \a start_idx
  /// that matches \a line. If \a file_spec_ptr is NULL, then the search
  /// matches line entries whose file matches the file for the compile unit.
  /// If \a file_spec_ptr is not NULL, line entries must match the specified
  /// file spec (for inlined line table entries).
  ///
  /// Multiple calls to this function can find all entries that match a given
  /// file and line by starting with \a start_idx equal to zero, and calling
  /// this function back with the return value + 1.
  ///
  /// \param[in] start_idx
  ///     The zero based index at which to start looking for matches.
  ///
  /// \param[in] line
  ///     The line number to search for.
  ///
  /// \param[in] file_spec_ptr
  ///     If non-NULL search for entries that match this file spec,
  ///     else if NULL, search for line entries that match the compile
  ///     unit file.
  ///
  /// \param[in] exact
  ///     If \b true match only if there is a line table entry for this line
  ///     number.
  ///     If \b false, find the line table entry equal to or after this line
  ///     number.
  ///
  /// \param[out] line_entry
  ///     If non-NULL, a copy of the line entry that was found.
  ///
  /// \return
  ///     The zero based index of a matching line entry, or UINT32_MAX
  ///     if no matching line entry is found.
  uint32_t FindLineEntry(uint32_t start_idx, uint32_t line,
                         const FileSpec *file_spec_ptr, bool exact,
                         LineEntry *line_entry);

  /// Return the primary source spec associated with this compile unit.
  const FileSpec &GetPrimaryFile() const {
    return m_primary_support_file_sp->GetSpecOnly();
  }

  /// Return the primary source file associated with this compile unit.
  lldb::SupportFileSP GetPrimarySupportFile() const {
    return m_primary_support_file_sp;
  }

  /// Get the line table for the compile unit.
  ///
  /// Called by clients and the SymbolFile plug-in. The SymbolFile plug-ins
  /// use this function to determine if the line table has be parsed yet.
  /// Clients use this function to get the line table from a compile unit.
  ///
  /// \return
  ///     The line table object pointer, or NULL if this line table
  ///     hasn't been parsed yet.
  LineTable *GetLineTable();

  DebugMacros *GetDebugMacros();

  /// Apply a lambda to each external lldb::Module referenced by this
  /// compilation unit. Recursively also descends into the referenced external
  /// modules of any encountered compilation unit.
  ///
  /// \param visited_symbol_files
  ///     A set of SymbolFiles that were already visited to avoid
  ///     visiting one file more than once.
  ///
  /// \param[in] lambda
  ///     The lambda that should be applied to every function. The lambda can
  ///     return true if the iteration should be aborted earlier.
  ///
  /// \return
  ///     If the lambda early-exited, this function returns true to
  ///     propagate the early exit.
  virtual bool ForEachExternalModule(
      llvm::DenseSet<lldb_private::SymbolFile *> &visited_symbol_files,
      llvm::function_ref<bool(Module &)> lambda);

  /// Get the compile unit's support file list.
  ///
  /// The support file list is used by the line table, and any objects that
  /// have valid Declaration objects.
  ///
  /// \return
  ///     A support file list object.
  const SupportFileList &GetSupportFiles();

  /// Used by plugins that parse the support file list.
  SupportFileList &GetSupportFileList() {
    m_flags.Set(flagsParsedSupportFiles);
    return m_support_files;
  }

  /// Get the compile unit's imported module list.
  ///
  /// This reports all the imports that the compile unit made, including the
  /// current module.
  ///
  /// \return
  ///     A list of imported modules.
  const std::vector<SourceModule> &GetImportedModules();

  /// Get the SymbolFile plug-in user data.
  ///
  /// SymbolFile plug-ins can store user data to internal state or objects to
  /// quickly allow them to parse more information for a given object.
  ///
  /// \return
  ///     The user data stored with the CompileUnit when it was
  ///     constructed.
  void *GetUserData() const;

  /// Get the variable list for a compile unit.
  ///
  /// Called by clients to get the variable list for a compile unit. The
  /// variable list will contain all global and static variables that were
  /// defined at the compile unit level.
  ///
  /// \param[in] can_create
  ///     If \b true, the variable list will be parsed on demand. If
  ///     \b false, the current variable list will be returned even
  ///     if it contains a NULL VariableList object (typically
  ///     called by dumping routines that want to display only what
  ///     has currently been parsed).
  ///
  /// \return
  ///     A shared pointer to a variable list, that can contain NULL
  ///     VariableList pointer if there are no global or static
  ///     variables.
  lldb::VariableListSP GetVariableList(bool can_create);

  /// Finds a function by user ID.
  ///
  /// Typically used by SymbolFile plug-ins when partially parsing the debug
  /// information to see if the function has been parsed yet.
  ///
  /// \param[in] uid
  ///     The user ID of the function to find. This value is supplied
  ///     by the SymbolFile plug-in and should be a value that
  ///     allows the plug-in to easily locate and parse additional
  ///     information in the function.
  ///
  /// \return
  ///     A shared pointer to the function object that might contain
  ///     a NULL Function pointer.
  lldb::FunctionSP FindFunctionByUID(lldb::user_id_t uid);

  /// Set the line table for the compile unit.
  ///
  /// Called by the SymbolFile plug-in when if first parses the line table and
  /// hands ownership of the line table to this object. The compile unit owns
  /// the line table object and will delete the object when it is deleted.
  ///
  /// \param[in] line_table
  ///     A line table object pointer that this object now owns.
  void SetLineTable(LineTable *line_table);

  void SetDebugMacros(const DebugMacrosSP &debug_macros);

  /// Set accessor for the variable list.
  ///
  /// Called by the SymbolFile plug-ins after they have parsed the variable
  /// lists and are ready to hand ownership of the list over to this object.
  ///
  /// \param[in] variable_list_sp
  ///     A shared pointer to a VariableList.
  void SetVariableList(lldb::VariableListSP &variable_list_sp);

  /// Resolve symbol contexts by file and line.
  ///
  /// Given a file in \a src_location_spec, find all instances and
  /// append them to the supplied symbol context list \a sc_list.
  ///
  /// \param[in] src_location_spec
  ///     The \a src_location_spec containing the \a file_spec, the line and the
  ///     column of the symbol to look for. Also hold the inlines and
  ///     exact_match flags.
  ///
  ///     If check_inlines is \b true, this function will also match any inline
  ///     file and line matches. If \b false, the compile unit's
  ///     file specification must match \a file_spec for any matches
  ///     to be returned.
  ///
  ///     If exact_match is \b true, only resolve the context if \a line and \a
  ///     column exists in the line table. If \b false, resolve the context to
  ///     the closest line greater than \a line in the line table.
  ///
  /// \param[in] resolve_scope
  ///     For each matching line entry, this bitfield indicates what
  ///     values within each SymbolContext that gets added to \a
  ///     sc_list will be resolved. See the SymbolContext::Scope
  ///     enumeration for a list of all available bits that can be
  ///     resolved. Only SymbolContext entries that can be resolved
  ///     using a LineEntry base address will be able to be resolved.
  ///
  /// \param[out] sc_list
  ///     A SymbolContext list class that will get any matching
  ///     entries appended to.
  ///
  /// \see enum SymbolContext::Scope
  void ResolveSymbolContext(const SourceLocationSpec &src_location_spec,
                            lldb::SymbolContextItem resolve_scope,
                            SymbolContextList &sc_list);

  /// Get whether compiler optimizations were enabled for this compile unit
  ///
  /// "optimized" means that the debug experience may be difficult for the
  /// user to understand.  Variables may not be available when the developer
  /// would expect them, stepping through the source lines in the function may
  /// appear strange, etc.
  ///
  /// \return
  ///     Returns 'true' if this compile unit was compiled with
  ///     optimization.  'false' indicates that either the optimization
  ///     is unknown, or this compile unit was built without optimization.
  bool GetIsOptimized();

  /// Returns the number of functions in this compile unit
  size_t GetNumFunctions() const { return m_functions_by_uid.size(); }

protected:
  /// User data for the SymbolFile parser to store information into.
  void *m_user_data;
  /// The programming language enumeration value.
  lldb::LanguageType m_language;
  /// Compile unit flags that help with partial parsing.
  Flags m_flags;
  /// Maps UIDs to functions.
  llvm::DenseMap<lldb::user_id_t, lldb::FunctionSP> m_functions_by_uid;
  /// All modules, including the current module, imported by this
  /// compile unit.
  std::vector<SourceModule> m_imported_modules;
  /// The primary file associated with this compile unit.
  lldb::SupportFileSP m_primary_support_file_sp;
  /// Files associated with this compile unit's line table and declarations.
  SupportFileList m_support_files;
  /// Line table that will get parsed on demand.
  std::unique_ptr<LineTable> m_line_table_up;
  /// Debug macros that will get parsed on demand.
  DebugMacrosSP m_debug_macros_sp;
  /// Global and static variable list that will get parsed on demand.
  lldb::VariableListSP m_variables;
  /// eLazyBoolYes if this compile unit was compiled with
  /// optimization.
  lldb_private::LazyBool m_is_optimized;

private:
  enum {
    flagsParsedAllFunctions =
        (1u << 0), ///< Have we already parsed all our functions
    flagsParsedVariables =
        (1u << 1), ///< Have we already parsed globals and statics?
    flagsParsedSupportFiles = (1u << 2), ///< Have we already parsed the support
                                         ///files for this compile unit?
    flagsParsedLineTable =
        (1u << 3),                   ///< Have we parsed the line table already?
    flagsParsedLanguage = (1u << 4), ///< Have we parsed the language already?
    flagsParsedImportedModules =
        (1u << 5), ///< Have we parsed the imported modules already?
    flagsParsedDebugMacros =
        (1u << 6) ///< Have we parsed the debug macros already?
  };

  CompileUnit(const CompileUnit &) = delete;
  const CompileUnit &operator=(const CompileUnit &) = delete;
  const char *GetCachedLanguage() const;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_COMPILEUNIT_H
