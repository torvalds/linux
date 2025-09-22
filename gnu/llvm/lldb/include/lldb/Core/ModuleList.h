//===-- ModuleList.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_MODULELIST_H
#define LLDB_CORE_MODULELIST_H

#include "lldb/Core/Address.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Iterable.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/RWMutex.h"

#include <functional>
#include <list>
#include <mutex>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace lldb_private {
class ConstString;
class FileSpecList;
class Function;
class Log;
class Module;
class RegularExpression;
class Stream;
class SymbolContext;
class SymbolContextList;
class SymbolFile;
class Target;
class TypeList;
class UUID;
class VariableList;
struct ModuleFunctionSearchOptions;

static constexpr OptionEnumValueElement g_auto_download_enum_values[] = {
    {
        lldb::eSymbolDownloadOff,
        "off",
        "Disable automatically downloading symbols.",
    },
    {
        lldb::eSymbolDownloadBackground,
        "background",
        "Download symbols in the background for images as they appear in the "
        "backtrace.",
    },
    {
        lldb::eSymbolDownloadForeground,
        "foreground",
        "Download symbols in the foreground for images as they appear in the "
        "backtrace.",
    },
};

class ModuleListProperties : public Properties {
  mutable llvm::sys::RWMutex m_symlink_paths_mutex;
  PathMappingList m_symlink_paths;

  void UpdateSymlinkMappings();

public:
  ModuleListProperties();

  FileSpec GetClangModulesCachePath() const;
  bool SetClangModulesCachePath(const FileSpec &path);
  bool GetEnableExternalLookup() const;
  bool SetEnableExternalLookup(bool new_value);
  bool GetEnableLLDBIndexCache() const;
  bool SetEnableLLDBIndexCache(bool new_value);
  uint64_t GetLLDBIndexCacheMaxByteSize();
  uint64_t GetLLDBIndexCacheMaxPercent();
  uint64_t GetLLDBIndexCacheExpirationDays();
  FileSpec GetLLDBIndexCachePath() const;
  bool SetLLDBIndexCachePath(const FileSpec &path);

  bool GetLoadSymbolOnDemand();

  lldb::SymbolDownload GetSymbolAutoDownload() const;

  PathMappingList GetSymlinkMappings() const;
};

/// \class ModuleList ModuleList.h "lldb/Core/ModuleList.h"
/// A collection class for Module objects.
///
/// Modules in the module collection class are stored as reference counted
/// shared pointers to Module objects.
class ModuleList {
public:
  class Notifier {
  public:
    virtual ~Notifier() = default;

    virtual void NotifyModuleAdded(const ModuleList &module_list,
                                   const lldb::ModuleSP &module_sp) = 0;
    virtual void NotifyModuleRemoved(const ModuleList &module_list,
                                     const lldb::ModuleSP &module_sp) = 0;
    virtual void NotifyModuleUpdated(const ModuleList &module_list,
                                     const lldb::ModuleSP &old_module_sp,
                                     const lldb::ModuleSP &new_module_sp) = 0;
    virtual void NotifyWillClearList(const ModuleList &module_list) = 0;

    virtual void NotifyModulesRemoved(lldb_private::ModuleList &module_list) = 0;
  };

  /// Default constructor.
  ///
  /// Creates an empty list of Module objects.
  ModuleList();

  /// Copy Constructor.
  ///
  /// Creates a new module list object with a copy of the modules from \a rhs.
  ///
  /// \param[in] rhs
  ///     Another module list object.
  ModuleList(const ModuleList &rhs);

  ModuleList(ModuleList::Notifier *notifier);

  /// Destructor.
  ~ModuleList();

  /// Assignment operator.
  ///
  /// Copies the module list from \a rhs into this list.
  ///
  /// \param[in] rhs
  ///     Another module list object.
  ///
  /// \return
  ///     A const reference to this object.
  const ModuleList &operator=(const ModuleList &rhs);

  /// Append a module to the module list.
  ///
  /// \param[in] module_sp
  ///     A shared pointer to a module to add to this collection.
  ///
  /// \param[in] notify
  ///     If true, and a notifier function is set, the notifier function
  ///     will be called.  Defaults to true.
  ///
  ///     When this ModuleList is the Target's ModuleList, the notifier
  ///     function is Target::ModulesDidLoad -- the call to
  ///     ModulesDidLoad may be deferred when adding multiple Modules
  ///     to the Target, but it must be called at the end,
  ///     before resuming execution.
  void Append(const lldb::ModuleSP &module_sp, bool notify = true);

  /// Append a module to the module list and remove any equivalent modules.
  /// Equivalent modules are ones whose file, platform file and architecture
  /// matches.
  ///
  /// Replaces the module to the collection.
  ///
  /// \param[in] module_sp
  ///     A shared pointer to a module to replace in this collection.
  ///
  /// \param[in] old_modules
  ///     Optional pointer to a vector which, if provided, will have shared
  ///     pointers to the replaced module(s) appended to it.
  void ReplaceEquivalent(
      const lldb::ModuleSP &module_sp,
      llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules = nullptr);

  /// Append a module to the module list, if it is not already there.
  ///
  /// \param[in] notify
  ///     If true, and a notifier function is set, the notifier function
  ///     will be called.  Defaults to true.
  ///
  ///     When this ModuleList is the Target's ModuleList, the notifier
  ///     function is Target::ModulesDidLoad -- the call to
  ///     ModulesDidLoad may be deferred when adding multiple Modules
  ///     to the Target, but it must be called at the end,
  ///     before resuming execution.
  bool AppendIfNeeded(const lldb::ModuleSP &new_module, bool notify = true);

  void Append(const ModuleList &module_list);

  bool AppendIfNeeded(const ModuleList &module_list);

  bool ReplaceModule(const lldb::ModuleSP &old_module_sp,
                     const lldb::ModuleSP &new_module_sp);

  /// Clear the object's state.
  ///
  /// Clears the list of modules and releases a reference to each module
  /// object and if the reference count goes to zero, the module will be
  /// deleted.
  void Clear();

  /// Clear the object's state.
  ///
  /// Clears the list of modules and releases a reference to each module
  /// object and if the reference count goes to zero, the module will be
  /// deleted. Also release all memory that might be held by any collection
  /// classes (like std::vector)
  void Destroy();

  /// Dump the description of each module contained in this list.
  ///
  /// Dump the description of each module contained in this list to the
  /// supplied stream \a s.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  ///
  /// \see Module::Dump(Stream *) const
  void Dump(Stream *s) const;

  void LogUUIDAndPaths(Log *log, const char *prefix_cstr);

  std::recursive_mutex &GetMutex() const { return m_modules_mutex; }

  size_t GetIndexForModule(const Module *module) const;

  /// Get the module shared pointer for the module at index \a idx.
  ///
  /// \param[in] idx
  ///     An index into this module collection.
  ///
  /// \return
  ///     A shared pointer to a Module which can contain NULL if
  ///     \a idx is out of range.
  ///
  /// \see ModuleList::GetSize()
  lldb::ModuleSP GetModuleAtIndex(size_t idx) const;

  /// Get the module shared pointer for the module at index \a idx without
  /// acquiring the ModuleList mutex.  This MUST already have been acquired
  /// with ModuleList::GetMutex and locked for this call to be safe.
  ///
  /// \param[in] idx
  ///     An index into this module collection.
  ///
  /// \return
  ///     A shared pointer to a Module which can contain NULL if
  ///     \a idx is out of range.
  ///
  /// \see ModuleList::GetSize()
  lldb::ModuleSP GetModuleAtIndexUnlocked(size_t idx) const;

  /// Get the module pointer for the module at index \a idx.
  ///
  /// \param[in] idx
  ///     An index into this module collection.
  ///
  /// \return
  ///     A pointer to a Module which can by nullptr if \a idx is out
  ///     of range.
  ///
  /// \see ModuleList::GetSize()
  Module *GetModulePointerAtIndex(size_t idx) const;

  /// Find compile units by partial or full path.
  ///
  /// Finds all compile units that match \a path in all of the modules and
  /// returns the results in \a sc_list.
  ///
  /// \param[in] path
  ///     The name of the compile unit we are looking for.
  ///
  /// \param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  void FindCompileUnits(const FileSpec &path, SymbolContextList &sc_list) const;

  /// \see Module::FindFunctions ()
  void FindFunctions(ConstString name, lldb::FunctionNameType name_type_mask,
                     const ModuleFunctionSearchOptions &options,
                     SymbolContextList &sc_list) const;

  /// \see Module::FindFunctionSymbols ()
  void FindFunctionSymbols(ConstString name,
                           lldb::FunctionNameType name_type_mask,
                           SymbolContextList &sc_list);

  /// \see Module::FindFunctions ()
  void FindFunctions(const RegularExpression &name,
                     const ModuleFunctionSearchOptions &options,
                     SymbolContextList &sc_list);

  /// Find global and static variables by name.
  ///
  /// \param[in] name
  ///     The name of the global or static variable we are looking
  ///     for.
  ///
  /// \param[in] max_matches
  ///     Allow the number of matches to be limited to \a
  ///     max_matches. Specify UINT32_MAX to get all possible matches.
  ///
  /// \param[in] variable_list
  ///     A list of variables that gets the matches appended to.
  void FindGlobalVariables(ConstString name, size_t max_matches,
                           VariableList &variable_list) const;

  /// Find global and static variables by regular expression.
  ///
  /// \param[in] regex
  ///     A regular expression to use when matching the name.
  ///
  /// \param[in] max_matches
  ///     Allow the number of matches to be limited to \a
  ///     max_matches. Specify UINT32_MAX to get all possible matches.
  ///
  /// \param[in] variable_list
  ///     A list of variables that gets the matches appended to.
  void FindGlobalVariables(const RegularExpression &regex, size_t max_matches,
                           VariableList &variable_list) const;

  /// Finds the first module whose file specification matches \a file_spec.
  ///
  /// \param[in] module_spec
  ///     A file specification object to match against the Module's
  ///     file specifications. If \a file_spec does not have
  ///     directory information, matches will occur by matching only
  ///     the basename of any modules in this list. If this value is
  ///     NULL, then file specifications won't be compared when
  ///     searching for matching modules.
  ///
  /// \param[out] matching_module_list
  ///     A module list that gets filled in with any modules that
  ///     match the search criteria.
  void FindModules(const ModuleSpec &module_spec,
                   ModuleList &matching_module_list) const;

  lldb::ModuleSP FindModule(const Module *module_ptr) const;

  // Find a module by UUID
  //
  // The UUID value for a module is extracted from the ObjectFile and is the
  // MD5 checksum, or a smarter object file equivalent, so finding modules by
  // UUID values is very efficient and accurate.
  lldb::ModuleSP FindModule(const UUID &uuid) const;

  lldb::ModuleSP FindFirstModule(const ModuleSpec &module_spec) const;

  void FindSymbolsWithNameAndType(ConstString name,
                                  lldb::SymbolType symbol_type,
                                  SymbolContextList &sc_list) const;

  void FindSymbolsMatchingRegExAndType(const RegularExpression &regex,
                                       lldb::SymbolType symbol_type,
                                       SymbolContextList &sc_list) const;

  /// Find types using a type-matching object that contains all search
  /// parameters.
  ///
  /// \param[in] search_first
  ///     If non-null, this module will be searched before any other
  ///     modules.
  ///
  /// \param[in] query
  ///     A type matching object that contains all of the details of the type
  ///     search.
  ///
  /// \param[in] results
  ///     Any matching types will be populated into the \a results object using
  ///     TypeMap::InsertUnique(...).
  void FindTypes(Module *search_first, const TypeQuery &query,
                 lldb_private::TypeResults &results) const;

  bool FindSourceFile(const FileSpec &orig_spec, FileSpec &new_spec) const;

  /// Find addresses by file/line
  ///
  /// \param[in] target_sp
  ///     The target the addresses are desired for.
  ///
  /// \param[in] file
  ///     Source file to locate.
  ///
  /// \param[in] line
  ///     Source line to locate.
  ///
  /// \param[in] function
  ///     Optional filter function. Addresses within this function will be
  ///     added to the 'local' list. All others will be added to the 'extern'
  ///     list.
  ///
  /// \param[out] output_local
  ///     All matching addresses within 'function'
  ///
  /// \param[out] output_extern
  ///     All matching addresses not within 'function'
  void FindAddressesForLine(const lldb::TargetSP target_sp,
                            const FileSpec &file, uint32_t line,
                            Function *function,
                            std::vector<Address> &output_local,
                            std::vector<Address> &output_extern);

  /// Remove a module from the module list.
  ///
  /// \param[in] module_sp
  ///     A shared pointer to a module to remove from this collection.
  ///
  /// \param[in] notify
  ///     If true, and a notifier function is set, the notifier function
  ///     will be called.  Defaults to true.
  ///
  ///     When this ModuleList is the Target's ModuleList, the notifier
  ///     function is Target::ModulesDidUnload -- the call to
  ///     ModulesDidUnload may be deferred when removing multiple Modules
  ///     from the Target, but it must be called at the end,
  ///     before resuming execution.
  bool Remove(const lldb::ModuleSP &module_sp, bool notify = true);

  size_t Remove(ModuleList &module_list);

  bool RemoveIfOrphaned(const Module *module_ptr);

  size_t RemoveOrphans(bool mandatory);

  bool ResolveFileAddress(lldb::addr_t vm_addr, Address &so_addr) const;

  /// \copydoc Module::ResolveSymbolContextForAddress (const Address
  /// &,uint32_t,SymbolContext&)
  uint32_t ResolveSymbolContextForAddress(const Address &so_addr,
                                          lldb::SymbolContextItem resolve_scope,
                                          SymbolContext &sc) const;

  /// \copydoc Module::ResolveSymbolContextForFilePath (const char
  /// *,uint32_t,bool,uint32_t,SymbolContextList&)
  uint32_t ResolveSymbolContextForFilePath(
      const char *file_path, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) const;

  /// \copydoc Module::ResolveSymbolContextsForFileSpec (const FileSpec
  /// &,uint32_t,bool,uint32_t,SymbolContextList&)
  uint32_t ResolveSymbolContextsForFileSpec(
      const FileSpec &file_spec, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) const;

  /// Gets the size of the module list.
  ///
  /// \return
  ///     The number of modules in the module list.
  size_t GetSize() const;
  bool IsEmpty() const { return !GetSize(); }

  bool LoadScriptingResourcesInTarget(Target *target, std::list<Status> &errors,
                                      Stream &feedback_stream,
                                      bool continue_on_error = true);

  static ModuleListProperties &GetGlobalModuleListProperties();

  static bool ModuleIsInCache(const Module *module_ptr);

  static Status
  GetSharedModule(const ModuleSpec &module_spec, lldb::ModuleSP &module_sp,
                  const FileSpecList *module_search_paths_ptr,
                  llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules,
                  bool *did_create_ptr, bool always_create = false);

  static bool RemoveSharedModule(lldb::ModuleSP &module_sp);

  static void FindSharedModules(const ModuleSpec &module_spec,
                                ModuleList &matching_module_list);

  static lldb::ModuleSP FindSharedModule(const UUID &uuid);

  static size_t RemoveOrphanSharedModules(bool mandatory);

  static bool RemoveSharedModuleIfOrphaned(const Module *module_ptr);

  /// Applies 'callback' to each module in this ModuleList.
  /// If 'callback' returns false, iteration terminates.
  /// The 'module_sp' passed to 'callback' is guaranteed to
  /// be non-null.
  ///
  /// This function is thread-safe.
  void ForEach(std::function<bool(const lldb::ModuleSP &module_sp)> const
                   &callback) const;

  /// Returns true if 'callback' returns true for one of the modules
  /// in this ModuleList.
  ///
  /// This function is thread-safe.
  bool AnyOf(
      std::function<bool(lldb_private::Module &module)> const &callback) const;

  /// Atomically swaps the contents of this module list with \a other.
  void Swap(ModuleList &other);

protected:
  // Class typedefs.
  typedef std::vector<lldb::ModuleSP>
      collection; ///< The module collection type.

  void AppendImpl(const lldb::ModuleSP &module_sp, bool use_notifier = true);

  bool RemoveImpl(const lldb::ModuleSP &module_sp, bool use_notifier = true);

  collection::iterator RemoveImpl(collection::iterator pos,
                                  bool use_notifier = true);

  void ClearImpl(bool use_notifier = true);

  // Member variables.
  collection m_modules; ///< The collection of modules.
  mutable std::recursive_mutex m_modules_mutex;

  Notifier *m_notifier = nullptr;

public:
  typedef LockingAdaptedIterable<collection, lldb::ModuleSP, vector_adapter,
                                 std::recursive_mutex>
      ModuleIterable;
  ModuleIterable Modules() const {
    return ModuleIterable(m_modules, GetMutex());
  }

  typedef AdaptedIterable<collection, lldb::ModuleSP, vector_adapter>
      ModuleIterableNoLocking;
  ModuleIterableNoLocking ModulesNoLocking() const {
    return ModuleIterableNoLocking(m_modules);
  }
};

} // namespace lldb_private

#endif // LLDB_CORE_MODULELIST_H
