//===-- Module.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_MODULE_H
#define LLDB_CORE_MODULE_H

#include "lldb/Core/Address.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContextScope.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/PathMappingList.h"
#include "lldb/Target/Statistics.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UUID.h"
#include "lldb/Utility/XcodeSDK.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Chrono.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lldb_private {
class CompilerDeclContext;
class Function;
class Log;
class ObjectFile;
class RegularExpression;
class SectionList;
class Stream;
class Symbol;
class SymbolContext;
class SymbolContextList;
class SymbolFile;
class Symtab;
class Target;
class TypeList;
class TypeMap;
class VariableList;

/// Options used by Module::FindFunctions. This cannot be a nested class
/// because it must be forward-declared in ModuleList.h.
struct ModuleFunctionSearchOptions {
  /// Include the symbol table.
  bool include_symbols = false;
  /// Include inlined functions.
  bool include_inlines = false;
};

/// \class Module Module.h "lldb/Core/Module.h"
/// A class that describes an executable image and its associated
///        object and symbol files.
///
/// The module is designed to be able to select a single slice of an
/// executable image as it would appear on disk and during program execution.
///
/// Modules control when and if information is parsed according to which
/// accessors are called. For example the object file (ObjectFile)
/// representation will only be parsed if the object file is requested using
/// the Module::GetObjectFile() is called. The debug symbols will only be
/// parsed if the symbol file (SymbolFile) is requested using the
/// Module::GetSymbolFile() method.
///
/// The module will parse more detailed information as more queries are made.
class Module : public std::enable_shared_from_this<Module>,
               public SymbolContextScope {
public:
  class LookupInfo;
  // Static functions that can track the lifetime of module objects. This is
  // handy because we might have Module objects that are in shared pointers
  // that aren't in the global module list (from ModuleList). If this is the
  // case we need to know about it. The modules in the global list maintained
  // by these functions can be viewed using the "target modules list" command
  // using the "--global" (-g for short).
  static size_t GetNumberAllocatedModules();

  static Module *GetAllocatedModuleAtIndex(size_t idx);

  static std::recursive_mutex &GetAllocationModuleCollectionMutex();

  /// Construct with file specification and architecture.
  ///
  /// Clients that wish to share modules with other targets should use
  /// ModuleList::GetSharedModule().
  ///
  /// \param[in] file_spec
  ///     The file specification for the on disk representation of
  ///     this executable image.
  ///
  /// \param[in] arch
  ///     The architecture to set as the current architecture in
  ///     this module.
  ///
  /// \param[in] object_name
  ///     The name of an object in a module used to extract a module
  ///     within a module (.a files and modules that contain multiple
  ///     architectures).
  ///
  /// \param[in] object_offset
  ///     The offset within an existing module used to extract a
  ///     module within a module (.a files and modules that contain
  ///     multiple architectures).
  Module(
      const FileSpec &file_spec, const ArchSpec &arch,
      ConstString object_name = ConstString(), lldb::offset_t object_offset = 0,
      const llvm::sys::TimePoint<> &object_mod_time = llvm::sys::TimePoint<>());

  Module(const ModuleSpec &module_spec);

  template <typename ObjFilePlugin, typename... Args>
  static lldb::ModuleSP CreateModuleFromObjectFile(Args &&...args) {
    // Must create a module and place it into a shared pointer before we can
    // create an object file since it has a std::weak_ptr back to the module,
    // so we need to control the creation carefully in this static function
    lldb::ModuleSP module_sp(new Module());
    module_sp->m_objfile_sp =
        std::make_shared<ObjFilePlugin>(module_sp, std::forward<Args>(args)...);
    module_sp->m_did_load_objfile.store(true, std::memory_order_relaxed);

    // Once we get the object file, set module ArchSpec to the one we get from
    // the object file. If the object file does not have an architecture, we
    // consider the creation a failure.
    ArchSpec arch = module_sp->m_objfile_sp->GetArchitecture();
    if (!arch)
      return nullptr;
    module_sp->m_arch = arch;

    // Also copy the object file's FileSpec.
    module_sp->m_file = module_sp->m_objfile_sp->GetFileSpec();
    return module_sp;
  }

  /// Destructor.
  ~Module() override;

  bool MatchesModuleSpec(const ModuleSpec &module_ref);

  /// Set the load address for all sections in a module to be the file address
  /// plus \a slide.
  ///
  /// Many times a module will be loaded in a target with a constant offset
  /// applied to all top level sections. This function can set the load
  /// address for all top level sections to be the section file address +
  /// offset.
  ///
  /// \param[in] target
  ///     The target in which to apply the section load addresses.
  ///
  /// \param[in] value
  ///     if \a value_is_offset is true, then value is the offset to
  ///     apply to all file addresses for all top level sections in
  ///     the object file as each section load address is being set.
  ///     If \a value_is_offset is false, then "value" is the new
  ///     absolute base address for the image.
  ///
  /// \param[in] value_is_offset
  ///     If \b true, then \a value is an offset to apply to each
  ///     file address of each top level section.
  ///     If \b false, then \a value is the image base address that
  ///     will be used to rigidly slide all loadable sections.
  ///
  /// \param[out] changed
  ///     If any section load addresses were changed in \a target,
  ///     then \a changed will be set to \b true. Else \a changed
  ///     will be set to false. This allows this function to be
  ///     called multiple times on the same module for the same
  ///     target. If the module hasn't moved, then \a changed will
  ///     be false and no module updated notification will need to
  ///     be sent out.
  ///
  /// \return
  ///     /b True if any sections were successfully loaded in \a target,
  ///     /b false otherwise.
  bool SetLoadAddress(Target &target, lldb::addr_t value, bool value_is_offset,
                      bool &changed);

  /// \copydoc SymbolContextScope::CalculateSymbolContext(SymbolContext*)
  ///
  /// \see SymbolContextScope
  void CalculateSymbolContext(SymbolContext *sc) override;

  lldb::ModuleSP CalculateSymbolContextModule() override;

  void
  GetDescription(llvm::raw_ostream &s,
                 lldb::DescriptionLevel level = lldb::eDescriptionLevelFull);

  /// Get the module path and object name.
  ///
  /// Modules can refer to object files. In this case the specification is
  /// simple and would return the path to the file:
  ///
  ///     "/usr/lib/foo.dylib"
  ///
  /// Modules can be .o files inside of a BSD archive (.a file). In this case,
  /// the object specification will look like:
  ///
  ///     "/usr/lib/foo.a(bar.o)"
  ///
  /// There are many places where logging wants to log this fully qualified
  /// specification, so we centralize this functionality here.
  ///
  /// \return
  ///     The object path + object name if there is one.
  std::string GetSpecificationDescription() const;

  /// Dump a description of this object to a Stream.
  ///
  /// Dump a description of the contents of this object to the supplied stream
  /// \a s. The dumped content will be only what has been loaded or parsed up
  /// to this point at which this function is called, so this is a good way to
  /// see what has been parsed in a module.
  ///
  /// \param[in] s
  ///     The stream to which to dump the object description.
  void Dump(Stream *s);

  /// \copydoc SymbolContextScope::DumpSymbolContext(Stream*)
  ///
  /// \see SymbolContextScope
  void DumpSymbolContext(Stream *s) override;

  /// Find a symbol in the object file's symbol table.
  ///
  /// \param[in] name
  ///     The name of the symbol that we are looking for.
  ///
  /// \param[in] symbol_type
  ///     If set to eSymbolTypeAny, find a symbol of any type that
  ///     has a name that matches \a name. If set to any other valid
  ///     SymbolType enumeration value, then search only for
  ///     symbols that match \a symbol_type.
  ///
  /// \return
  ///     Returns a valid symbol pointer if a symbol was found,
  ///     nullptr otherwise.
  const Symbol *FindFirstSymbolWithNameAndType(
      ConstString name, lldb::SymbolType symbol_type = lldb::eSymbolTypeAny);

  void FindSymbolsWithNameAndType(ConstString name,
                                  lldb::SymbolType symbol_type,
                                  SymbolContextList &sc_list);

  void FindSymbolsMatchingRegExAndType(
      const RegularExpression &regex, lldb::SymbolType symbol_type,
      SymbolContextList &sc_list,
      Mangled::NamePreference mangling_preference = Mangled::ePreferDemangled);

  /// Find a function symbols in the object file's symbol table.
  ///
  /// \param[in] name
  ///     The name of the symbol that we are looking for.
  ///
  /// \param[in] name_type_mask
  ///     A mask that has one or more bitwise OR'ed values from the
  ///     lldb::FunctionNameType enumeration type that indicate what
  ///     kind of names we are looking for.
  ///
  /// \param[out] sc_list
  ///     A list to append any matching symbol contexts to.
  void FindFunctionSymbols(ConstString name, uint32_t name_type_mask,
                           SymbolContextList &sc_list);

  /// Find compile units by partial or full path.
  ///
  /// Finds all compile units that match \a path in all of the modules and
  /// returns the results in \a sc_list.
  ///
  /// \param[in] path
  ///     The name of the function we are looking for.
  ///
  /// \param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  void FindCompileUnits(const FileSpec &path, SymbolContextList &sc_list);

  /// Find functions by lookup info.
  ///
  /// If the function is an inlined function, it will have a block,
  /// representing the inlined function, and the function will be the
  /// containing function.  If it is not inlined, then the block will be NULL.
  ///
  /// \param[in] lookup_info
  ///     The lookup info of the function we are looking for.
  ///
  /// \param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  void FindFunctions(const LookupInfo &lookup_info,
                     const CompilerDeclContext &parent_decl_ctx,
                     const ModuleFunctionSearchOptions &options,
                     SymbolContextList &sc_list);

  /// Find functions by name.
  ///
  /// If the function is an inlined function, it will have a block,
  /// representing the inlined function, and the function will be the
  /// containing function.  If it is not inlined, then the block will be NULL.
  ///
  /// \param[in] name
  ///     The name of the function we are looking for.
  ///
  /// \param[in] name_type_mask
  ///     A bit mask of bits that indicate what kind of names should
  ///     be used when doing the lookup. Bits include fully qualified
  ///     names, base names, C++ methods, or ObjC selectors.
  ///     See FunctionNameType for more details.
  ///
  /// \param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  void FindFunctions(ConstString name,
                     const CompilerDeclContext &parent_decl_ctx,
                     lldb::FunctionNameType name_type_mask,
                     const ModuleFunctionSearchOptions &options,
                     SymbolContextList &sc_list);

  /// Find functions by compiler context.
  void FindFunctions(llvm::ArrayRef<CompilerContext> compiler_ctx,
                     lldb::FunctionNameType name_type_mask,
                     const ModuleFunctionSearchOptions &options,
                     SymbolContextList &sc_list);

  /// Find functions by name.
  ///
  /// If the function is an inlined function, it will have a block,
  /// representing the inlined function, and the function will be the
  /// containing function.  If it is not inlined, then the block will be NULL.
  ///
  /// \param[in] regex
  ///     A regular expression to use when matching the name.
  ///
  /// \param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  void FindFunctions(const RegularExpression &regex,
                     const ModuleFunctionSearchOptions &options,
                     SymbolContextList &sc_list);

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
  ///	    Optional filter function. Addresses within this function will be
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

  /// Find global and static variables by name.
  ///
  /// \param[in] name
  ///     The name of the global or static variable we are looking
  ///     for.
  ///
  /// \param[in] parent_decl_ctx
  ///     If valid, a decl context that results must exist within
  ///
  /// \param[in] max_matches
  ///     Allow the number of matches to be limited to \a
  ///     max_matches. Specify UINT32_MAX to get all possible matches.
  ///
  /// \param[in] variable_list
  ///     A list of variables that gets the matches appended to.
  ///
  void FindGlobalVariables(ConstString name,
                           const CompilerDeclContext &parent_decl_ctx,
                           size_t max_matches, VariableList &variable_list);

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
  ///
  void FindGlobalVariables(const RegularExpression &regex, size_t max_matches,
                           VariableList &variable_list);

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
  void FindTypes(const TypeQuery &query, TypeResults &results);

  /// Get const accessor for the module architecture.
  ///
  /// \return
  ///     A const reference to the architecture object.
  const ArchSpec &GetArchitecture() const;

  /// Get const accessor for the module file specification.
  ///
  /// This function returns the file for the module on the host system that is
  /// running LLDB. This can differ from the path on the platform since we
  /// might be doing remote debugging.
  ///
  /// \return
  ///     A const reference to the file specification object.
  const FileSpec &GetFileSpec() const { return m_file; }

  /// Get accessor for the module platform file specification.
  ///
  /// Platform file refers to the path of the module as it is known on the
  /// remote system on which it is being debugged. For local debugging this is
  /// always the same as Module::GetFileSpec(). But remote debugging might
  /// mention a file "/usr/lib/liba.dylib" which might be locally downloaded
  /// and cached. In this case the platform file could be something like:
  /// "/tmp/lldb/platform-cache/remote.host.computer/usr/lib/liba.dylib" The
  /// file could also be cached in a local developer kit directory.
  ///
  /// \return
  ///     A const reference to the file specification object.
  const FileSpec &GetPlatformFileSpec() const {
    if (m_platform_file)
      return m_platform_file;
    return m_file;
  }

  void SetPlatformFileSpec(const FileSpec &file) { m_platform_file = file; }

  const FileSpec &GetRemoteInstallFileSpec() const {
    return m_remote_install_file;
  }

  void SetRemoteInstallFileSpec(const FileSpec &file) {
    m_remote_install_file = file;
  }

  const FileSpec &GetSymbolFileFileSpec() const { return m_symfile_spec; }

  void PreloadSymbols();

  void SetSymbolFileFileSpec(const FileSpec &file);

  const llvm::sys::TimePoint<> &GetModificationTime() const {
    return m_mod_time;
  }

  const llvm::sys::TimePoint<> &GetObjectModificationTime() const {
    return m_object_mod_time;
  }

  /// This callback will be called by SymbolFile implementations when
  /// parsing a compile unit that contains SDK information.
  /// \param sysroot will be added to the path remapping dictionary.
  void RegisterXcodeSDK(llvm::StringRef sdk, llvm::StringRef sysroot);

  /// Tells whether this module is capable of being the main executable for a
  /// process.
  ///
  /// \return
  ///     \b true if it is, \b false otherwise.
  bool IsExecutable();

  /// Tells whether this module has been loaded in the target passed in. This
  /// call doesn't distinguish between whether the module is loaded by the
  /// dynamic loader, or by a "target module add" type call.
  ///
  /// \param[in] target
  ///    The target to check whether this is loaded in.
  ///
  /// \return
  ///     \b true if it is, \b false otherwise.
  bool IsLoadedInTarget(Target *target);

  bool LoadScriptingResourceInTarget(Target *target, Status &error,
                                     Stream &feedback_stream);

  /// Get the number of compile units for this module.
  ///
  /// \return
  ///     The number of compile units that the symbol vendor plug-in
  ///     finds.
  size_t GetNumCompileUnits();

  lldb::CompUnitSP GetCompileUnitAtIndex(size_t idx);

  ConstString GetObjectName() const;

  uint64_t GetObjectOffset() const { return m_object_offset; }

  /// Get the object file representation for the current architecture.
  ///
  /// If the object file has not been located or parsed yet, this function
  /// will find the best ObjectFile plug-in that can parse Module::m_file.
  ///
  /// \return
  ///     If Module::m_file does not exist, or no plug-in was found
  ///     that can parse the file, or the object file doesn't contain
  ///     the current architecture in Module::m_arch, nullptr will be
  ///     returned, else a valid object file interface will be
  ///     returned. The returned pointer is owned by this object and
  ///     remains valid as long as the object is around.
  virtual ObjectFile *GetObjectFile();

  /// Get the unified section list for the module. This is the section list
  /// created by the module's object file and any debug info and symbol files
  /// created by the symbol vendor.
  ///
  /// If the symbol vendor has not been loaded yet, this function will return
  /// the section list for the object file.
  ///
  /// \return
  ///     Unified module section list.
  virtual SectionList *GetSectionList();

  /// Notify the module that the file addresses for the Sections have been
  /// updated.
  ///
  /// If the Section file addresses for a module are updated, this method
  /// should be called.  Any parts of the module, object file, or symbol file
  /// that has cached those file addresses must invalidate or update its
  /// cache.
  virtual void SectionFileAddressesChanged();

  /// Returns a reference to the UnwindTable for this Module
  ///
  /// The UnwindTable contains FuncUnwinders objects for any function in this
  /// Module.  If a FuncUnwinders object hasn't been created yet (i.e. the
  /// function has yet to be unwound in a stack walk), it will be created when
  /// requested.  Specifically, we do not create FuncUnwinders objects for
  /// functions until they are needed.
  ///
  /// \return
  ///     Returns the unwind table for this module. If this object has no
  ///     associated object file, an empty UnwindTable is returned.
  UnwindTable &GetUnwindTable();

  llvm::VersionTuple GetVersion();

  /// Load an object file from memory.
  ///
  /// If available, the size of the object file in memory may be passed to
  /// avoid additional round trips to process memory. If the size is not
  /// provided, a default value is used. This value should be large enough to
  /// enable the ObjectFile plugins to read the header of the object file
  /// without going back to the process.
  ///
  /// \return
  ///     The object file loaded from memory or nullptr, if the operation
  ///     failed (see the `error` for more information in that case).
  ObjectFile *GetMemoryObjectFile(const lldb::ProcessSP &process_sp,
                                  lldb::addr_t header_addr, Status &error,
                                  size_t size_to_read = 512);

  /// Get the module's symbol file
  ///
  /// If the symbol file has already been loaded, this function returns it. All
  /// arguments are ignored. If the symbol file has not been located yet, and
  /// the can_create argument is false, the function returns nullptr. If
  /// can_create is true, this function will find the best SymbolFile plug-in
  /// that can use the current object file. feedback_strm, if not null, is used
  /// to report the details of the search process.
  virtual SymbolFile *GetSymbolFile(bool can_create = true,
                                    Stream *feedback_strm = nullptr);

  Symtab *GetSymtab();

  /// Get a reference to the UUID value contained in this object.
  ///
  /// If the executable image file doesn't not have a UUID value built into
  /// the file format, an MD5 checksum of the entire file, or slice of the
  /// file for the current architecture should be used.
  ///
  /// \return
  ///     A const pointer to the internal copy of the UUID value in
  ///     this module if this module has a valid UUID value, NULL
  ///     otherwise.
  const lldb_private::UUID &GetUUID();

  /// A debugging function that will cause everything in a module to
  /// be parsed.
  ///
  /// All compile units will be parsed, along with all globals and static
  /// variables and all functions for those compile units. All types, scopes,
  /// local variables, static variables, global variables, and line tables
  /// will be parsed. This can be used prior to dumping a module to see a
  /// complete list of the resulting debug information that gets parsed, or as
  /// a debug function to ensure that the module can consume all of the debug
  /// data the symbol vendor provides.
  void ParseAllDebugSymbols();

  bool ResolveFileAddress(lldb::addr_t vm_addr, Address &so_addr);

  /// Resolve the symbol context for the given address.
  ///
  /// Tries to resolve the matching symbol context based on a lookup from the
  /// current symbol vendor.  If the lazy lookup fails, an attempt is made to
  /// parse the eh_frame section to handle stripped symbols.  If this fails,
  /// an attempt is made to resolve the symbol to the previous address to
  /// handle the case of a function with a tail call.
  ///
  /// Use properties of the modified SymbolContext to inspect any resolved
  /// target, module, compilation unit, symbol, function, function block or
  /// line entry.  Use the return value to determine which of these properties
  /// have been modified.
  ///
  /// \param[in] so_addr
  ///     A load address to resolve.
  ///
  /// \param[in] resolve_scope
  ///     The scope that should be resolved (see SymbolContext::Scope).
  ///     A combination of flags from the enumeration SymbolContextItem
  ///     requesting a resolution depth.  Note that the flags that are
  ///     actually resolved may be a superset of the requested flags.
  ///     For instance, eSymbolContextSymbol requires resolution of
  ///     eSymbolContextModule, and eSymbolContextFunction requires
  ///     eSymbolContextSymbol.
  ///
  /// \param[out] sc
  ///     The SymbolContext that is modified based on symbol resolution.
  ///
  /// \param[in] resolve_tail_call_address
  ///     Determines if so_addr should resolve to a symbol in the case
  ///     of a function whose last instruction is a call.  In this case,
  ///     the PC can be one past the address range of the function.
  ///
  /// \return
  ///     The scope that has been resolved (see SymbolContext::Scope).
  ///
  /// \see SymbolContext::Scope
  uint32_t ResolveSymbolContextForAddress(
      const Address &so_addr, lldb::SymbolContextItem resolve_scope,
      SymbolContext &sc, bool resolve_tail_call_address = false);

  /// Resolve items in the symbol context for a given file and line.
  ///
  /// Tries to resolve \a file_path and \a line to a list of matching symbol
  /// contexts.
  ///
  /// The line table entries contains addresses that can be used to further
  /// resolve the values in each match: the function, block, symbol. Care
  /// should be taken to minimize the amount of information that is requested
  /// to only what is needed -- typically the module, compile unit, line table
  /// and line table entry are sufficient.
  ///
  /// \param[in] file_path
  ///     A path to a source file to match. If \a file_path does not
  ///     specify a directory, then this query will match all files
  ///     whose base filename matches. If \a file_path does specify
  ///     a directory, the fullpath to the file must match.
  ///
  /// \param[in] line
  ///     The source line to match, or zero if just the compile unit
  ///     should be resolved.
  ///
  /// \param[in] check_inlines
  ///     Check for inline file and line number matches. This option
  ///     should be used sparingly as it will cause all line tables
  ///     for every compile unit to be parsed and searched for
  ///     matching inline file entries.
  ///
  /// \param[in] resolve_scope
  ///     The scope that should be resolved (see
  ///     SymbolContext::Scope).
  ///
  /// \param[out] sc_list
  ///     A symbol context list that gets matching symbols contexts
  ///     appended to.
  ///
  /// \return
  ///     The number of matches that were added to \a sc_list.
  ///
  /// \see SymbolContext::Scope
  uint32_t ResolveSymbolContextForFilePath(
      const char *file_path, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list);

  /// Resolve items in the symbol context for a given file and line.
  ///
  /// Tries to resolve \a file_spec and \a line to a list of matching symbol
  /// contexts.
  ///
  /// The line table entries contains addresses that can be used to further
  /// resolve the values in each match: the function, block, symbol. Care
  /// should be taken to minimize the amount of information that is requested
  /// to only what is needed -- typically the module, compile unit, line table
  /// and line table entry are sufficient.
  ///
  /// \param[in] file_spec
  ///     A file spec to a source file to match. If \a file_path does
  ///     not specify a directory, then this query will match all
  ///     files whose base filename matches. If \a file_path does
  ///     specify a directory, the fullpath to the file must match.
  ///
  /// \param[in] line
  ///     The source line to match, or zero if just the compile unit
  ///     should be resolved.
  ///
  /// \param[in] check_inlines
  ///     Check for inline file and line number matches. This option
  ///     should be used sparingly as it will cause all line tables
  ///     for every compile unit to be parsed and searched for
  ///     matching inline file entries.
  ///
  /// \param[in] resolve_scope
  ///     The scope that should be resolved (see
  ///     SymbolContext::Scope).
  ///
  /// \param[out] sc_list
  ///     A symbol context list that gets filled in with all of the
  ///     matches.
  ///
  /// \return
  ///     A integer that contains SymbolContext::Scope bits set for
  ///     each item that was successfully resolved.
  ///
  /// \see SymbolContext::Scope
  uint32_t ResolveSymbolContextsForFileSpec(
      const FileSpec &file_spec, uint32_t line, bool check_inlines,
      lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list);

  void SetFileSpecAndObjectName(const FileSpec &file, ConstString object_name);

  bool GetIsDynamicLinkEditor();

  llvm::Expected<lldb::TypeSystemSP>
  GetTypeSystemForLanguage(lldb::LanguageType language);

  /// Call \p callback for each \p TypeSystem in this \p Module.
  /// Return true from callback to keep iterating, false to stop iterating.
  void ForEachTypeSystem(llvm::function_ref<bool(lldb::TypeSystemSP)> callback);

  // Special error functions that can do printf style formatting that will
  // prepend the message with something appropriate for this module (like the
  // architecture, path and object name (if any)). This centralizes code so
  // that everyone doesn't need to format their error and log messages on their
  // own and keeps the output a bit more consistent.
  template <typename... Args>
  void LogMessage(Log *log, const char *format, Args &&...args) {
    LogMessage(log, llvm::formatv(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void LogMessageVerboseBacktrace(Log *log, const char *format,
                                  Args &&...args) {
    LogMessageVerboseBacktrace(
        log, llvm::formatv(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void ReportWarning(const char *format, Args &&...args) {
    ReportWarning(llvm::formatv(format, std::forward<Args>(args)...));
  }

  template <typename... Args>
  void ReportError(const char *format, Args &&...args) {
    ReportError(llvm::formatv(format, std::forward<Args>(args)...));
  }

  // Only report an error once when the module is first detected to be modified
  // so we don't spam the console with many messages.
  template <typename... Args>
  void ReportErrorIfModifyDetected(const char *format, Args &&...args) {
    ReportErrorIfModifyDetected(
        llvm::formatv(format, std::forward<Args>(args)...));
  }

  void ReportWarningOptimization(std::optional<lldb::user_id_t> debugger_id);

  void
  ReportWarningUnsupportedLanguage(lldb::LanguageType language,
                                   std::optional<lldb::user_id_t> debugger_id);

  // Return true if the file backing this module has changed since the module
  // was originally created  since we saved the initial file modification time
  // when the module first gets created.
  bool FileHasChanged() const;

  // SymbolFile and ObjectFile member objects should lock the
  // module mutex to avoid deadlocks.
  std::recursive_mutex &GetMutex() const { return m_mutex; }

  PathMappingList &GetSourceMappingList() { return m_source_mappings; }

  const PathMappingList &GetSourceMappingList() const {
    return m_source_mappings;
  }

  /// Finds a source file given a file spec using the module source path
  /// remappings (if any).
  ///
  /// Tries to resolve \a orig_spec by checking the module source path
  /// remappings. It makes sure the file exists, so this call can be expensive
  /// if the remappings are on a network file system, so use this function
  /// sparingly (not in a tight debug info parsing loop).
  ///
  /// \param[in] orig_spec
  ///     The original source file path to try and remap.
  ///
  /// \param[out] new_spec
  ///     The newly remapped filespec that is guaranteed to exist.
  ///
  /// \return
  ///     /b true if \a orig_spec was successfully located and
  ///     \a new_spec is filled in with an existing file spec,
  ///     \b false otherwise.
  bool FindSourceFile(const FileSpec &orig_spec, FileSpec &new_spec) const;

  /// Remaps a source file given \a path into \a new_path.
  ///
  /// Remaps \a path if any source remappings match. This function does NOT
  /// stat the file system so it can be used in tight loops where debug info
  /// is being parsed.
  ///
  /// \param[in] path
  ///     The original source file path to try and remap.
  ///
  /// \return
  ///     The newly remapped filespec that is may or may not exist if
  ///     \a path was successfully located.
  std::optional<std::string> RemapSourceFile(llvm::StringRef path) const;
  bool RemapSourceFile(const char *, std::string &) const = delete;

  /// Update the ArchSpec to a more specific variant.
  bool MergeArchitecture(const ArchSpec &arch_spec);

  /// Accessor for the symbol table parse time metric.
  ///
  /// The value is returned as a reference to allow it to be updated by the
  /// ElapsedTime RAII object.
  StatsDuration &GetSymtabParseTime() { return m_symtab_parse_time; }

  /// Accessor for the symbol table index time metric.
  ///
  /// The value is returned as a reference to allow it to be updated by the
  /// ElapsedTime RAII object.
  StatsDuration &GetSymtabIndexTime() { return m_symtab_index_time; }

  /// \class LookupInfo Module.h "lldb/Core/Module.h"
  /// A class that encapsulates name lookup information.
  ///
  /// Users can type a wide variety of partial names when setting breakpoints
  /// by name or when looking for functions by name. The SymbolFile object is
  /// only required to implement name lookup for function basenames and for
  /// fully mangled names. This means if the user types in a partial name, we
  /// must reduce this to a name lookup that will work with all SymbolFile
  /// objects. So we might reduce a name lookup to look for a basename, and then
  /// prune out any results that don't match.
  ///
  /// The "m_name" member variable represents the name as it was typed by the
  /// user. "m_lookup_name" will be the name we actually search for through
  /// the symbol or objects files. Lanaguage is included in case we need to
  /// filter results by language at a later date. The "m_name_type_mask"
  /// member variable tells us what kinds of names we are looking for and can
  /// help us prune out unwanted results.
  ///
  /// Function lookups are done in Module.cpp, ModuleList.cpp and in
  /// BreakpointResolverName.cpp and they all now use this class to do lookups
  /// correctly.
  class LookupInfo {
  public:
    LookupInfo() = default;

    LookupInfo(ConstString name, lldb::FunctionNameType name_type_mask,
               lldb::LanguageType language);

    ConstString GetName() const { return m_name; }

    void SetName(ConstString name) { m_name = name; }

    ConstString GetLookupName() const { return m_lookup_name; }

    void SetLookupName(ConstString name) { m_lookup_name = name; }

    lldb::FunctionNameType GetNameTypeMask() const { return m_name_type_mask; }

    void SetNameTypeMask(lldb::FunctionNameType mask) {
      m_name_type_mask = mask;
    }

    lldb::LanguageType GetLanguageType() const { return m_language; }

    bool NameMatchesLookupInfo(
        ConstString function_name,
        lldb::LanguageType language_type = lldb::eLanguageTypeUnknown) const;

    void Prune(SymbolContextList &sc_list, size_t start_idx) const;

  protected:
    /// What the user originally typed
    ConstString m_name;

    /// The actual name will lookup when calling in the object or symbol file
    ConstString m_lookup_name;

    /// Limit matches to only be for this language
    lldb::LanguageType m_language = lldb::eLanguageTypeUnknown;

    /// One or more bits from lldb::FunctionNameType that indicate what kind of
    /// names we are looking for
    lldb::FunctionNameType m_name_type_mask = lldb::eFunctionNameTypeNone;

    ///< If \b true, then demangled names that match will need to contain
    ///< "m_name" in order to be considered a match
    bool m_match_name_after_lookup = false;
  };

  /// Get a unique hash for this module.
  ///
  /// The hash should be enough to identify the file on disk and the
  /// architecture of the file. If the module represents an object inside of a
  /// file, then the hash should include the object name and object offset to
  /// ensure a unique hash. Some examples:
  /// - just a regular object file (mach-o, elf, coff, etc) should create a hash
  /// - a universal mach-o file that contains to multiple architectures,
  ///   each architecture slice should have a unique hash even though they come
  ///   from the same file
  /// - a .o file inside of a BSD archive. Each .o file will have an object name
  ///   and object offset that should produce a unique hash. The object offset
  ///   is needed as BSD archive files can contain multiple .o files that have
  ///   the same name.
  uint32_t Hash();

  /// Get a unique cache key for the current module.
  ///
  /// The cache key must be unique for a file on disk and not change if the file
  /// is updated. This allows cache data to use this key as a prefix and as
  /// files are modified in disk, we will overwrite the cache files. If one file
  /// can contain multiple files, like a universal mach-o file or like a BSD
  /// archive, the cache key must contain enough information to differentiate
  /// these different files.
  std::string GetCacheKey();

  /// Get the global index file cache.
  ///
  /// LLDB can cache data for a module between runs. This cache directory can be
  /// used to stored data that previously was manually created each time you debug.
  /// Examples include debug information indexes, symbol tables, symbol table
  /// indexes, and more.
  ///
  /// \returns
  ///   If caching is enabled in the lldb settings, return a pointer to the data
  ///   file cache. If caching is not enabled, return NULL.
  static DataFileCache *GetIndexCache();
protected:
  // Member Variables
  mutable std::recursive_mutex m_mutex; ///< A mutex to keep this object happy
                                        /// in multi-threaded environments.

  /// The modification time for this module when it was created.
  llvm::sys::TimePoint<> m_mod_time;

  ArchSpec m_arch; ///< The architecture for this module.
  UUID m_uuid; ///< Each module is assumed to have a unique identifier to help
               /// match it up to debug symbols.
  FileSpec m_file; ///< The file representation on disk for this module (if
                   /// there is one).
  FileSpec m_platform_file; ///< The path to the module on the platform on which
                            /// it is being debugged
  FileSpec m_remote_install_file; ///< If set when debugging on remote
                                  /// platforms, this module will be installed
                                  /// at this location
  FileSpec m_symfile_spec;   ///< If this path is valid, then this is the file
                             /// that _will_ be used as the symbol file for this
                             /// module
  ConstString m_object_name; ///< The name an object within this module that is
                             /// selected, or empty of the module is represented
                             /// by \a m_file.
  uint64_t m_object_offset = 0;
  llvm::sys::TimePoint<> m_object_mod_time;

  /// DataBuffer containing the module image, if it was provided at
  /// construction time. Otherwise the data will be retrieved by mapping
  /// one of the FileSpec members above.
  lldb::DataBufferSP m_data_sp;

  lldb::ObjectFileSP m_objfile_sp; ///< A shared pointer to the object file
                                   /// parser for this module as it may or may
                                   /// not be shared with the SymbolFile
  std::optional<UnwindTable> m_unwind_table; ///< Table of FuncUnwinders
                                             /// objects created for this
                                             /// Module's functions
  lldb::SymbolVendorUP
      m_symfile_up; ///< A pointer to the symbol vendor for this module.
  std::vector<lldb::SymbolVendorUP>
      m_old_symfiles; ///< If anyone calls Module::SetSymbolFileFileSpec() and
                      /// changes the symbol file,
  ///< we need to keep all old symbol files around in case anyone has type
  /// references to them
  TypeSystemMap m_type_system_map; ///< A map of any type systems associated
                                   /// with this module
  /// Module specific source remappings for when you have debug info for a
  /// module that doesn't match where the sources currently are.
  PathMappingList m_source_mappings =
      ModuleList::GetGlobalModuleListProperties().GetSymlinkMappings();

  lldb::SectionListUP m_sections_up; ///< Unified section list for module that
                                     /// is used by the ObjectFile and
                                     /// ObjectFile instances for the debug info

  std::atomic<bool> m_did_load_objfile{false};
  std::atomic<bool> m_did_load_symfile{false};
  std::atomic<bool> m_did_set_uuid{false};
  mutable bool m_file_has_changed : 1,
      m_first_file_changed_log : 1; /// See if the module was modified after it
                                    /// was initially opened.
  /// We store a symbol table parse time duration here because we might have
  /// an object file and a symbol file which both have symbol tables. The parse
  /// time for the symbol tables can be aggregated here.
  StatsDuration m_symtab_parse_time;
  /// We store a symbol named index time duration here because we might have
  /// an object file and a symbol file which both have symbol tables. The parse
  /// time for the symbol tables can be aggregated here.
  StatsDuration m_symtab_index_time;

  std::once_flag m_optimization_warning;
  std::once_flag m_language_warning;

  void SymbolIndicesToSymbolContextList(Symtab *symtab,
                                        std::vector<uint32_t> &symbol_indexes,
                                        SymbolContextList &sc_list);

  bool SetArchitecture(const ArchSpec &new_arch);

  void SetUUID(const lldb_private::UUID &uuid);

  SectionList *GetUnifiedSectionList();

  friend class ModuleList;
  friend class ObjectFile;
  friend class SymbolFile;

private:
  Module(); // Only used internally by CreateJITModule ()

  Module(const Module &) = delete;
  const Module &operator=(const Module &) = delete;

  void LogMessage(Log *log, const llvm::formatv_object_base &payload);
  void LogMessageVerboseBacktrace(Log *log,
                                  const llvm::formatv_object_base &payload);
  void ReportWarning(const llvm::formatv_object_base &payload);
  void ReportError(const llvm::formatv_object_base &payload);
  void ReportErrorIfModifyDetected(const llvm::formatv_object_base &payload);
};

} // namespace lldb_private

#endif // LLDB_CORE_MODULE_H
