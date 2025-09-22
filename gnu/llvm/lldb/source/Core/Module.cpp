//===-- Module.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"

#include "lldb/Core/AddressRange.h"
#include "lldb/Core/AddressResolverFileLine.h"
#include "lldb/Core/DataFileCache.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/SearchFilter.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/SymbolLocator.h"
#include "lldb/Symbol/SymbolVendor.h"
#include "lldb/Symbol/Symtab.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegularExpression.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#endif

#include "Plugins/Language/CPlusPlus/CPlusPlusLanguage.h"
#include "Plugins/Language/ObjC/ObjCLanguage.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>
#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <type_traits>
#include <utility>

namespace lldb_private {
class CompilerDeclContext;
}
namespace lldb_private {
class VariableList;
}

using namespace lldb;
using namespace lldb_private;

// Shared pointers to modules track module lifetimes in targets and in the
// global module, but this collection will track all module objects that are
// still alive
typedef std::vector<Module *> ModuleCollection;

static ModuleCollection &GetModuleCollection() {
  // This module collection needs to live past any module, so we could either
  // make it a shared pointer in each module or just leak is.  Since it is only
  // an empty vector by the time all the modules have gone away, we just leak
  // it for now.  If we decide this is a big problem we can introduce a
  // Finalize method that will tear everything down in a predictable order.

  static ModuleCollection *g_module_collection = nullptr;
  if (g_module_collection == nullptr)
    g_module_collection = new ModuleCollection();

  return *g_module_collection;
}

std::recursive_mutex &Module::GetAllocationModuleCollectionMutex() {
  // NOTE: The mutex below must be leaked since the global module list in
  // the ModuleList class will get torn at some point, and we can't know if it
  // will tear itself down before the "g_module_collection_mutex" below will.
  // So we leak a Mutex object below to safeguard against that

  static std::recursive_mutex *g_module_collection_mutex = nullptr;
  if (g_module_collection_mutex == nullptr)
    g_module_collection_mutex = new std::recursive_mutex; // NOTE: known leak
  return *g_module_collection_mutex;
}

size_t Module::GetNumberAllocatedModules() {
  std::lock_guard<std::recursive_mutex> guard(
      GetAllocationModuleCollectionMutex());
  return GetModuleCollection().size();
}

Module *Module::GetAllocatedModuleAtIndex(size_t idx) {
  std::lock_guard<std::recursive_mutex> guard(
      GetAllocationModuleCollectionMutex());
  ModuleCollection &modules = GetModuleCollection();
  if (idx < modules.size())
    return modules[idx];
  return nullptr;
}

Module::Module(const ModuleSpec &module_spec)
    : m_file_has_changed(false), m_first_file_changed_log(false) {
  // Scope for locker below...
  {
    std::lock_guard<std::recursive_mutex> guard(
        GetAllocationModuleCollectionMutex());
    GetModuleCollection().push_back(this);
  }

  Log *log(GetLog(LLDBLog::Object | LLDBLog::Modules));
  if (log != nullptr)
    LLDB_LOGF(log, "%p Module::Module((%s) '%s%s%s%s')",
              static_cast<void *>(this),
              module_spec.GetArchitecture().GetArchitectureName(),
              module_spec.GetFileSpec().GetPath().c_str(),
              module_spec.GetObjectName().IsEmpty() ? "" : "(",
              module_spec.GetObjectName().AsCString(""),
              module_spec.GetObjectName().IsEmpty() ? "" : ")");

  auto data_sp = module_spec.GetData();
  lldb::offset_t file_size = 0;
  if (data_sp)
    file_size = data_sp->GetByteSize();

  // First extract all module specifications from the file using the local file
  // path. If there are no specifications, then don't fill anything in
  ModuleSpecList modules_specs;
  if (ObjectFile::GetModuleSpecifications(
          module_spec.GetFileSpec(), 0, file_size, modules_specs, data_sp) == 0)
    return;

  // Now make sure that one of the module specifications matches what we just
  // extract. We might have a module specification that specifies a file
  // "/usr/lib/dyld" with UUID XXX, but we might have a local version of
  // "/usr/lib/dyld" that has
  // UUID YYY and we don't want those to match. If they don't match, just don't
  // fill any ivars in so we don't accidentally grab the wrong file later since
  // they don't match...
  ModuleSpec matching_module_spec;
  if (!modules_specs.FindMatchingModuleSpec(module_spec,
                                            matching_module_spec)) {
    if (log) {
      LLDB_LOGF(log, "Found local object file but the specs didn't match");
    }
    return;
  }

  // Set m_data_sp if it was initially provided in the ModuleSpec. Note that
  // we cannot use the data_sp variable here, because it will have been
  // modified by GetModuleSpecifications().
  if (auto module_spec_data_sp = module_spec.GetData()) {
    m_data_sp = module_spec_data_sp;
    m_mod_time = {};
  } else {
    if (module_spec.GetFileSpec())
      m_mod_time =
          FileSystem::Instance().GetModificationTime(module_spec.GetFileSpec());
    else if (matching_module_spec.GetFileSpec())
      m_mod_time = FileSystem::Instance().GetModificationTime(
          matching_module_spec.GetFileSpec());
  }

  // Copy the architecture from the actual spec if we got one back, else use
  // the one that was specified
  if (matching_module_spec.GetArchitecture().IsValid())
    m_arch = matching_module_spec.GetArchitecture();
  else if (module_spec.GetArchitecture().IsValid())
    m_arch = module_spec.GetArchitecture();

  // Copy the file spec over and use the specified one (if there was one) so we
  // don't use a path that might have gotten resolved a path in
  // 'matching_module_spec'
  if (module_spec.GetFileSpec())
    m_file = module_spec.GetFileSpec();
  else if (matching_module_spec.GetFileSpec())
    m_file = matching_module_spec.GetFileSpec();

  // Copy the platform file spec over
  if (module_spec.GetPlatformFileSpec())
    m_platform_file = module_spec.GetPlatformFileSpec();
  else if (matching_module_spec.GetPlatformFileSpec())
    m_platform_file = matching_module_spec.GetPlatformFileSpec();

  // Copy the symbol file spec over
  if (module_spec.GetSymbolFileSpec())
    m_symfile_spec = module_spec.GetSymbolFileSpec();
  else if (matching_module_spec.GetSymbolFileSpec())
    m_symfile_spec = matching_module_spec.GetSymbolFileSpec();

  // Copy the object name over
  if (matching_module_spec.GetObjectName())
    m_object_name = matching_module_spec.GetObjectName();
  else
    m_object_name = module_spec.GetObjectName();

  // Always trust the object offset (file offset) and object modification time
  // (for mod time in a BSD static archive) of from the matching module
  // specification
  m_object_offset = matching_module_spec.GetObjectOffset();
  m_object_mod_time = matching_module_spec.GetObjectModificationTime();
}

Module::Module(const FileSpec &file_spec, const ArchSpec &arch,
               ConstString object_name, lldb::offset_t object_offset,
               const llvm::sys::TimePoint<> &object_mod_time)
    : m_mod_time(FileSystem::Instance().GetModificationTime(file_spec)),
      m_arch(arch), m_file(file_spec), m_object_name(object_name),
      m_object_offset(object_offset), m_object_mod_time(object_mod_time),
      m_file_has_changed(false), m_first_file_changed_log(false) {
  // Scope for locker below...
  {
    std::lock_guard<std::recursive_mutex> guard(
        GetAllocationModuleCollectionMutex());
    GetModuleCollection().push_back(this);
  }

  Log *log(GetLog(LLDBLog::Object | LLDBLog::Modules));
  if (log != nullptr)
    LLDB_LOGF(log, "%p Module::Module((%s) '%s%s%s%s')",
              static_cast<void *>(this), m_arch.GetArchitectureName(),
              m_file.GetPath().c_str(), m_object_name.IsEmpty() ? "" : "(",
              m_object_name.AsCString(""), m_object_name.IsEmpty() ? "" : ")");
}

Module::Module() : m_file_has_changed(false), m_first_file_changed_log(false) {
  std::lock_guard<std::recursive_mutex> guard(
      GetAllocationModuleCollectionMutex());
  GetModuleCollection().push_back(this);
}

Module::~Module() {
  // Lock our module down while we tear everything down to make sure we don't
  // get any access to the module while it is being destroyed
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  // Scope for locker below...
  {
    std::lock_guard<std::recursive_mutex> guard(
        GetAllocationModuleCollectionMutex());
    ModuleCollection &modules = GetModuleCollection();
    ModuleCollection::iterator end = modules.end();
    ModuleCollection::iterator pos = std::find(modules.begin(), end, this);
    assert(pos != end);
    modules.erase(pos);
  }
  Log *log(GetLog(LLDBLog::Object | LLDBLog::Modules));
  if (log != nullptr)
    LLDB_LOGF(log, "%p Module::~Module((%s) '%s%s%s%s')",
              static_cast<void *>(this), m_arch.GetArchitectureName(),
              m_file.GetPath().c_str(), m_object_name.IsEmpty() ? "" : "(",
              m_object_name.AsCString(""), m_object_name.IsEmpty() ? "" : ")");
  // Release any auto pointers before we start tearing down our member
  // variables since the object file and symbol files might need to make
  // function calls back into this module object. The ordering is important
  // here because symbol files can require the module object file. So we tear
  // down the symbol file first, then the object file.
  m_sections_up.reset();
  m_symfile_up.reset();
  m_objfile_sp.reset();
}

ObjectFile *Module::GetMemoryObjectFile(const lldb::ProcessSP &process_sp,
                                        lldb::addr_t header_addr, Status &error,
                                        size_t size_to_read) {
  if (m_objfile_sp) {
    error.SetErrorString("object file already exists");
  } else {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (process_sp) {
      m_did_load_objfile = true;
      std::shared_ptr<DataBufferHeap> data_sp =
          std::make_shared<DataBufferHeap>(size_to_read, 0);
      Status readmem_error;
      const size_t bytes_read =
          process_sp->ReadMemory(header_addr, data_sp->GetBytes(),
                                 data_sp->GetByteSize(), readmem_error);
      if (bytes_read < size_to_read)
        data_sp->SetByteSize(bytes_read);
      if (data_sp->GetByteSize() > 0) {
        m_objfile_sp = ObjectFile::FindPlugin(shared_from_this(), process_sp,
                                              header_addr, data_sp);
        if (m_objfile_sp) {
          StreamString s;
          s.Printf("0x%16.16" PRIx64, header_addr);
          m_object_name.SetString(s.GetString());

          // Once we get the object file, update our module with the object
          // file's architecture since it might differ in vendor/os if some
          // parts were unknown.
          m_arch = m_objfile_sp->GetArchitecture();

          // Augment the arch with the target's information in case
          // we are unable to extract the os/environment from memory.
          m_arch.MergeFrom(process_sp->GetTarget().GetArchitecture());
        } else {
          error.SetErrorString("unable to find suitable object file plug-in");
        }
      } else {
        error.SetErrorStringWithFormat("unable to read header from memory: %s",
                                       readmem_error.AsCString());
      }
    } else {
      error.SetErrorString("invalid process");
    }
  }
  return m_objfile_sp.get();
}

const lldb_private::UUID &Module::GetUUID() {
  if (!m_did_set_uuid.load()) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (!m_did_set_uuid.load()) {
      ObjectFile *obj_file = GetObjectFile();

      if (obj_file != nullptr) {
        m_uuid = obj_file->GetUUID();
        m_did_set_uuid = true;
      }
    }
  }
  return m_uuid;
}

void Module::SetUUID(const lldb_private::UUID &uuid) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (!m_did_set_uuid) {
    m_uuid = uuid;
    m_did_set_uuid = true;
  } else {
    lldbassert(0 && "Attempting to overwrite the existing module UUID");
  }
}

llvm::Expected<TypeSystemSP>
Module::GetTypeSystemForLanguage(LanguageType language) {
  return m_type_system_map.GetTypeSystemForLanguage(language, this, true);
}

void Module::ForEachTypeSystem(
    llvm::function_ref<bool(lldb::TypeSystemSP)> callback) {
  m_type_system_map.ForEach(callback);
}

void Module::ParseAllDebugSymbols() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  size_t num_comp_units = GetNumCompileUnits();
  if (num_comp_units == 0)
    return;

  SymbolFile *symbols = GetSymbolFile();

  for (size_t cu_idx = 0; cu_idx < num_comp_units; cu_idx++) {
    SymbolContext sc;
    sc.module_sp = shared_from_this();
    sc.comp_unit = symbols->GetCompileUnitAtIndex(cu_idx).get();
    if (!sc.comp_unit)
      continue;

    symbols->ParseVariablesForContext(sc);

    symbols->ParseFunctions(*sc.comp_unit);

    sc.comp_unit->ForeachFunction([&sc, &symbols](const FunctionSP &f) {
      symbols->ParseBlocksRecursive(*f);

      // Parse the variables for this function and all its blocks
      sc.function = f.get();
      symbols->ParseVariablesForContext(sc);
      return false;
    });

    // Parse all types for this compile unit
    symbols->ParseTypes(*sc.comp_unit);
  }
}

void Module::CalculateSymbolContext(SymbolContext *sc) {
  sc->module_sp = shared_from_this();
}

ModuleSP Module::CalculateSymbolContextModule() { return shared_from_this(); }

void Module::DumpSymbolContext(Stream *s) {
  s->Printf(", Module{%p}", static_cast<void *>(this));
}

size_t Module::GetNumCompileUnits() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (SymbolFile *symbols = GetSymbolFile())
    return symbols->GetNumCompileUnits();
  return 0;
}

CompUnitSP Module::GetCompileUnitAtIndex(size_t index) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  size_t num_comp_units = GetNumCompileUnits();
  CompUnitSP cu_sp;

  if (index < num_comp_units) {
    if (SymbolFile *symbols = GetSymbolFile())
      cu_sp = symbols->GetCompileUnitAtIndex(index);
  }
  return cu_sp;
}

bool Module::ResolveFileAddress(lldb::addr_t vm_addr, Address &so_addr) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  SectionList *section_list = GetSectionList();
  if (section_list)
    return so_addr.ResolveAddressUsingFileSections(vm_addr, section_list);
  return false;
}

uint32_t Module::ResolveSymbolContextForAddress(
    const Address &so_addr, lldb::SymbolContextItem resolve_scope,
    SymbolContext &sc, bool resolve_tail_call_address) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  uint32_t resolved_flags = 0;

  // Clear the result symbol context in case we don't find anything, but don't
  // clear the target
  sc.Clear(false);

  // Get the section from the section/offset address.
  SectionSP section_sp(so_addr.GetSection());

  // Make sure the section matches this module before we try and match anything
  if (section_sp && section_sp->GetModule().get() == this) {
    // If the section offset based address resolved itself, then this is the
    // right module.
    sc.module_sp = shared_from_this();
    resolved_flags |= eSymbolContextModule;

    SymbolFile *symfile = GetSymbolFile();
    if (!symfile)
      return resolved_flags;

    // Resolve the compile unit, function, block, line table or line entry if
    // requested.
    if (resolve_scope & eSymbolContextCompUnit ||
        resolve_scope & eSymbolContextFunction ||
        resolve_scope & eSymbolContextBlock ||
        resolve_scope & eSymbolContextLineEntry ||
        resolve_scope & eSymbolContextVariable) {
      symfile->SetLoadDebugInfoEnabled();
      resolved_flags |=
          symfile->ResolveSymbolContext(so_addr, resolve_scope, sc);
    }

    // Resolve the symbol if requested, but don't re-look it up if we've
    // already found it.
    if (resolve_scope & eSymbolContextSymbol &&
        !(resolved_flags & eSymbolContextSymbol)) {
      Symtab *symtab = symfile->GetSymtab();
      if (symtab && so_addr.IsSectionOffset()) {
        Symbol *matching_symbol = nullptr;

        symtab->ForEachSymbolContainingFileAddress(
            so_addr.GetFileAddress(),
            [&matching_symbol](Symbol *symbol) -> bool {
              if (symbol->GetType() != eSymbolTypeInvalid) {
                matching_symbol = symbol;
                return false; // Stop iterating
              }
              return true; // Keep iterating
            });
        sc.symbol = matching_symbol;
        if (!sc.symbol && resolve_scope & eSymbolContextFunction &&
            !(resolved_flags & eSymbolContextFunction)) {
          bool verify_unique = false; // No need to check again since
                                      // ResolveSymbolContext failed to find a
                                      // symbol at this address.
          if (ObjectFile *obj_file = sc.module_sp->GetObjectFile())
            sc.symbol =
                obj_file->ResolveSymbolForAddress(so_addr, verify_unique);
        }

        if (sc.symbol) {
          if (sc.symbol->IsSynthetic()) {
            // We have a synthetic symbol so lets check if the object file from
            // the symbol file in the symbol vendor is different than the
            // object file for the module, and if so search its symbol table to
            // see if we can come up with a better symbol. For example dSYM
            // files on MacOSX have an unstripped symbol table inside of them.
            ObjectFile *symtab_objfile = symtab->GetObjectFile();
            if (symtab_objfile && symtab_objfile->IsStripped()) {
              ObjectFile *symfile_objfile = symfile->GetObjectFile();
              if (symfile_objfile != symtab_objfile) {
                Symtab *symfile_symtab = symfile_objfile->GetSymtab();
                if (symfile_symtab) {
                  Symbol *symbol =
                      symfile_symtab->FindSymbolContainingFileAddress(
                          so_addr.GetFileAddress());
                  if (symbol && !symbol->IsSynthetic()) {
                    sc.symbol = symbol;
                  }
                }
              }
            }
          }
          resolved_flags |= eSymbolContextSymbol;
        }
      }
    }

    // For function symbols, so_addr may be off by one.  This is a convention
    // consistent with FDE row indices in eh_frame sections, but requires extra
    // logic here to permit symbol lookup for disassembly and unwind.
    if (resolve_scope & eSymbolContextSymbol &&
        !(resolved_flags & eSymbolContextSymbol) && resolve_tail_call_address &&
        so_addr.IsSectionOffset()) {
      Address previous_addr = so_addr;
      previous_addr.Slide(-1);

      bool do_resolve_tail_call_address = false; // prevent recursion
      const uint32_t flags = ResolveSymbolContextForAddress(
          previous_addr, resolve_scope, sc, do_resolve_tail_call_address);
      if (flags & eSymbolContextSymbol) {
        AddressRange addr_range;
        if (sc.GetAddressRange(eSymbolContextFunction | eSymbolContextSymbol, 0,
                               false, addr_range)) {
          if (addr_range.GetBaseAddress().GetSection() ==
              so_addr.GetSection()) {
            // If the requested address is one past the address range of a
            // function (i.e. a tail call), or the decremented address is the
            // start of a function (i.e. some forms of trampoline), indicate
            // that the symbol has been resolved.
            if (so_addr.GetOffset() ==
                    addr_range.GetBaseAddress().GetOffset() ||
                so_addr.GetOffset() == addr_range.GetBaseAddress().GetOffset() +
                                           addr_range.GetByteSize()) {
              resolved_flags |= flags;
            }
          } else {
            sc.symbol =
                nullptr; // Don't trust the symbol if the sections didn't match.
          }
        }
      }
    }
  }
  return resolved_flags;
}

uint32_t Module::ResolveSymbolContextForFilePath(
    const char *file_path, uint32_t line, bool check_inlines,
    lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) {
  FileSpec file_spec(file_path);
  return ResolveSymbolContextsForFileSpec(file_spec, line, check_inlines,
                                          resolve_scope, sc_list);
}

uint32_t Module::ResolveSymbolContextsForFileSpec(
    const FileSpec &file_spec, uint32_t line, bool check_inlines,
    lldb::SymbolContextItem resolve_scope, SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  LLDB_SCOPED_TIMERF("Module::ResolveSymbolContextForFilePath (%s:%u, "
                     "check_inlines = %s, resolve_scope = 0x%8.8x)",
                     file_spec.GetPath().c_str(), line,
                     check_inlines ? "yes" : "no", resolve_scope);

  const uint32_t initial_count = sc_list.GetSize();

  if (SymbolFile *symbols = GetSymbolFile()) {
    // TODO: Handle SourceLocationSpec column information
    SourceLocationSpec location_spec(file_spec, line, /*column=*/std::nullopt,
                                     check_inlines, /*exact_match=*/false);

    symbols->ResolveSymbolContext(location_spec, resolve_scope, sc_list);
  }

  return sc_list.GetSize() - initial_count;
}

void Module::FindGlobalVariables(ConstString name,
                                 const CompilerDeclContext &parent_decl_ctx,
                                 size_t max_matches, VariableList &variables) {
  if (SymbolFile *symbols = GetSymbolFile())
    symbols->FindGlobalVariables(name, parent_decl_ctx, max_matches, variables);
}

void Module::FindGlobalVariables(const RegularExpression &regex,
                                 size_t max_matches, VariableList &variables) {
  SymbolFile *symbols = GetSymbolFile();
  if (symbols)
    symbols->FindGlobalVariables(regex, max_matches, variables);
}

void Module::FindCompileUnits(const FileSpec &path,
                              SymbolContextList &sc_list) {
  const size_t num_compile_units = GetNumCompileUnits();
  SymbolContext sc;
  sc.module_sp = shared_from_this();
  for (size_t i = 0; i < num_compile_units; ++i) {
    sc.comp_unit = GetCompileUnitAtIndex(i).get();
    if (sc.comp_unit) {
      if (FileSpec::Match(path, sc.comp_unit->GetPrimaryFile()))
        sc_list.Append(sc);
    }
  }
}

Module::LookupInfo::LookupInfo(ConstString name,
                               FunctionNameType name_type_mask,
                               LanguageType language)
    : m_name(name), m_lookup_name(), m_language(language) {
  const char *name_cstr = name.GetCString();
  llvm::StringRef basename;
  llvm::StringRef context;

  if (name_type_mask & eFunctionNameTypeAuto) {
    if (CPlusPlusLanguage::IsCPPMangledName(name_cstr))
      m_name_type_mask = eFunctionNameTypeFull;
    else if ((language == eLanguageTypeUnknown ||
              Language::LanguageIsObjC(language)) &&
             ObjCLanguage::IsPossibleObjCMethodName(name_cstr))
      m_name_type_mask = eFunctionNameTypeFull;
    else if (Language::LanguageIsC(language)) {
      m_name_type_mask = eFunctionNameTypeFull;
    } else {
      if ((language == eLanguageTypeUnknown ||
           Language::LanguageIsObjC(language)) &&
          ObjCLanguage::IsPossibleObjCSelector(name_cstr))
        m_name_type_mask |= eFunctionNameTypeSelector;

      CPlusPlusLanguage::MethodName cpp_method(name);
      basename = cpp_method.GetBasename();
      if (basename.empty()) {
        if (CPlusPlusLanguage::ExtractContextAndIdentifier(name_cstr, context,
                                                           basename))
          m_name_type_mask |= (eFunctionNameTypeMethod | eFunctionNameTypeBase);
        else
          m_name_type_mask |= eFunctionNameTypeFull;
      } else {
        m_name_type_mask |= (eFunctionNameTypeMethod | eFunctionNameTypeBase);
      }
    }
  } else {
    m_name_type_mask = name_type_mask;
    if (name_type_mask & eFunctionNameTypeMethod ||
        name_type_mask & eFunctionNameTypeBase) {
      // If they've asked for a CPP method or function name and it can't be
      // that, we don't even need to search for CPP methods or names.
      CPlusPlusLanguage::MethodName cpp_method(name);
      if (cpp_method.IsValid()) {
        basename = cpp_method.GetBasename();

        if (!cpp_method.GetQualifiers().empty()) {
          // There is a "const" or other qualifier following the end of the
          // function parens, this can't be a eFunctionNameTypeBase
          m_name_type_mask &= ~(eFunctionNameTypeBase);
          if (m_name_type_mask == eFunctionNameTypeNone)
            return;
        }
      } else {
        // If the CPP method parser didn't manage to chop this up, try to fill
        // in the base name if we can. If a::b::c is passed in, we need to just
        // look up "c", and then we'll filter the result later.
        CPlusPlusLanguage::ExtractContextAndIdentifier(name_cstr, context,
                                                       basename);
      }
    }

    if (name_type_mask & eFunctionNameTypeSelector) {
      if (!ObjCLanguage::IsPossibleObjCSelector(name_cstr)) {
        m_name_type_mask &= ~(eFunctionNameTypeSelector);
        if (m_name_type_mask == eFunctionNameTypeNone)
          return;
      }
    }

    // Still try and get a basename in case someone specifies a name type mask
    // of eFunctionNameTypeFull and a name like "A::func"
    if (basename.empty()) {
      if (name_type_mask & eFunctionNameTypeFull &&
          !CPlusPlusLanguage::IsCPPMangledName(name_cstr)) {
        CPlusPlusLanguage::MethodName cpp_method(name);
        basename = cpp_method.GetBasename();
        if (basename.empty())
          CPlusPlusLanguage::ExtractContextAndIdentifier(name_cstr, context,
                                                         basename);
      }
    }
  }

  if (!basename.empty()) {
    // The name supplied was a partial C++ path like "a::count". In this case
    // we want to do a lookup on the basename "count" and then make sure any
    // matching results contain "a::count" so that it would match "b::a::count"
    // and "a::count". This is why we set "match_name_after_lookup" to true
    m_lookup_name.SetString(basename);
    m_match_name_after_lookup = true;
  } else {
    // The name is already correct, just use the exact name as supplied, and we
    // won't need to check if any matches contain "name"
    m_lookup_name = name;
    m_match_name_after_lookup = false;
  }
}

bool Module::LookupInfo::NameMatchesLookupInfo(
    ConstString function_name, LanguageType language_type) const {
  // We always keep unnamed symbols
  if (!function_name)
    return true;

  // If we match exactly, we can return early
  if (m_name == function_name)
    return true;

  // If function_name is mangled, we'll need to demangle it.
  // In the pathologial case where the function name "looks" mangled but is
  // actually demangled (e.g. a method named _Zonk), this operation should be
  // relatively inexpensive since no demangling is actually occuring. See
  // Mangled::SetValue for more context.
  const bool function_name_may_be_mangled =
      Mangled::GetManglingScheme(function_name) != Mangled::eManglingSchemeNone;
  ConstString demangled_function_name = function_name;
  if (function_name_may_be_mangled) {
    Mangled mangled_function_name(function_name);
    demangled_function_name = mangled_function_name.GetDemangledName();
  }

  // If the symbol has a language, then let the language make the match.
  // Otherwise just check that the demangled function name contains the
  // demangled user-provided name.
  if (Language *language = Language::FindPlugin(language_type))
    return language->DemangledNameContainsPath(m_name, demangled_function_name);

  llvm::StringRef function_name_ref = demangled_function_name;
  return function_name_ref.contains(m_name);
}

void Module::LookupInfo::Prune(SymbolContextList &sc_list,
                               size_t start_idx) const {
  if (m_match_name_after_lookup && m_name) {
    SymbolContext sc;
    size_t i = start_idx;
    while (i < sc_list.GetSize()) {
      if (!sc_list.GetContextAtIndex(i, sc))
        break;

      bool keep_it =
          NameMatchesLookupInfo(sc.GetFunctionName(), sc.GetLanguage());
      if (keep_it)
        ++i;
      else
        sc_list.RemoveContextAtIndex(i);
    }
  }

  // If we have only full name matches we might have tried to set breakpoint on
  // "func" and specified eFunctionNameTypeFull, but we might have found
  // "a::func()", "a::b::func()", "c::func()", "func()" and "func". Only
  // "func()" and "func" should end up matching.
  if (m_name_type_mask == eFunctionNameTypeFull) {
    SymbolContext sc;
    size_t i = start_idx;
    while (i < sc_list.GetSize()) {
      if (!sc_list.GetContextAtIndex(i, sc))
        break;
      // Make sure the mangled and demangled names don't match before we try to
      // pull anything out
      ConstString mangled_name(sc.GetFunctionName(Mangled::ePreferMangled));
      ConstString full_name(sc.GetFunctionName());
      if (mangled_name != m_name && full_name != m_name) {
        CPlusPlusLanguage::MethodName cpp_method(full_name);
        if (cpp_method.IsValid()) {
          if (cpp_method.GetContext().empty()) {
            if (cpp_method.GetBasename().compare(m_name) != 0) {
              sc_list.RemoveContextAtIndex(i);
              continue;
            }
          } else {
            std::string qualified_name;
            llvm::StringRef anon_prefix("(anonymous namespace)");
            if (cpp_method.GetContext() == anon_prefix)
              qualified_name = cpp_method.GetBasename().str();
            else
              qualified_name = cpp_method.GetScopeQualifiedName();
            if (qualified_name != m_name.GetCString()) {
              sc_list.RemoveContextAtIndex(i);
              continue;
            }
          }
        }
      }
      ++i;
    }
  }
}

void Module::FindFunctions(const Module::LookupInfo &lookup_info,
                           const CompilerDeclContext &parent_decl_ctx,
                           const ModuleFunctionSearchOptions &options,
                           SymbolContextList &sc_list) {
  // Find all the functions (not symbols, but debug information functions...
  if (SymbolFile *symbols = GetSymbolFile()) {
    symbols->FindFunctions(lookup_info, parent_decl_ctx,
                           options.include_inlines, sc_list);
    // Now check our symbol table for symbols that are code symbols if
    // requested
    if (options.include_symbols) {
      if (Symtab *symtab = symbols->GetSymtab()) {
        symtab->FindFunctionSymbols(lookup_info.GetLookupName(),
                                    lookup_info.GetNameTypeMask(), sc_list);
      }
    }
  }
}

void Module::FindFunctions(ConstString name,
                           const CompilerDeclContext &parent_decl_ctx,
                           FunctionNameType name_type_mask,
                           const ModuleFunctionSearchOptions &options,
                           SymbolContextList &sc_list) {
  const size_t old_size = sc_list.GetSize();
  LookupInfo lookup_info(name, name_type_mask, eLanguageTypeUnknown);
  FindFunctions(lookup_info, parent_decl_ctx, options, sc_list);
  if (name_type_mask & eFunctionNameTypeAuto) {
    const size_t new_size = sc_list.GetSize();
    if (old_size < new_size)
      lookup_info.Prune(sc_list, old_size);
  }
}

void Module::FindFunctions(llvm::ArrayRef<CompilerContext> compiler_ctx,
                           FunctionNameType name_type_mask,
                           const ModuleFunctionSearchOptions &options,
                           SymbolContextList &sc_list) {
  if (compiler_ctx.empty() ||
      compiler_ctx.back().kind != CompilerContextKind::Function)
    return;
  ConstString name = compiler_ctx.back().name;
  SymbolContextList unfiltered;
  FindFunctions(name, CompilerDeclContext(), name_type_mask, options,
                unfiltered);
  // Filter by context.
  for (auto &sc : unfiltered)
    if (sc.function && compiler_ctx.equals(sc.function->GetCompilerContext()))
      sc_list.Append(sc);
}

void Module::FindFunctions(const RegularExpression &regex,
                           const ModuleFunctionSearchOptions &options,
                           SymbolContextList &sc_list) {
  const size_t start_size = sc_list.GetSize();

  if (SymbolFile *symbols = GetSymbolFile()) {
    symbols->FindFunctions(regex, options.include_inlines, sc_list);

    // Now check our symbol table for symbols that are code symbols if
    // requested
    if (options.include_symbols) {
      Symtab *symtab = symbols->GetSymtab();
      if (symtab) {
        std::vector<uint32_t> symbol_indexes;
        symtab->AppendSymbolIndexesMatchingRegExAndType(
            regex, eSymbolTypeAny, Symtab::eDebugAny, Symtab::eVisibilityAny,
            symbol_indexes);
        const size_t num_matches = symbol_indexes.size();
        if (num_matches) {
          SymbolContext sc(this);
          const size_t end_functions_added_index = sc_list.GetSize();
          size_t num_functions_added_to_sc_list =
              end_functions_added_index - start_size;
          if (num_functions_added_to_sc_list == 0) {
            // No functions were added, just symbols, so we can just append
            // them
            for (size_t i = 0; i < num_matches; ++i) {
              sc.symbol = symtab->SymbolAtIndex(symbol_indexes[i]);
              SymbolType sym_type = sc.symbol->GetType();
              if (sc.symbol && (sym_type == eSymbolTypeCode ||
                                sym_type == eSymbolTypeResolver))
                sc_list.Append(sc);
            }
          } else {
            typedef std::map<lldb::addr_t, uint32_t> FileAddrToIndexMap;
            FileAddrToIndexMap file_addr_to_index;
            for (size_t i = start_size; i < end_functions_added_index; ++i) {
              const SymbolContext &sc = sc_list[i];
              if (sc.block)
                continue;
              file_addr_to_index[sc.function->GetAddressRange()
                                     .GetBaseAddress()
                                     .GetFileAddress()] = i;
            }

            FileAddrToIndexMap::const_iterator end = file_addr_to_index.end();
            // Functions were added so we need to merge symbols into any
            // existing function symbol contexts
            for (size_t i = start_size; i < num_matches; ++i) {
              sc.symbol = symtab->SymbolAtIndex(symbol_indexes[i]);
              SymbolType sym_type = sc.symbol->GetType();
              if (sc.symbol && sc.symbol->ValueIsAddress() &&
                  (sym_type == eSymbolTypeCode ||
                   sym_type == eSymbolTypeResolver)) {
                FileAddrToIndexMap::const_iterator pos =
                    file_addr_to_index.find(
                        sc.symbol->GetAddressRef().GetFileAddress());
                if (pos == end)
                  sc_list.Append(sc);
                else
                  sc_list[pos->second].symbol = sc.symbol;
              }
            }
          }
        }
      }
    }
  }
}

void Module::FindAddressesForLine(const lldb::TargetSP target_sp,
                                  const FileSpec &file, uint32_t line,
                                  Function *function,
                                  std::vector<Address> &output_local,
                                  std::vector<Address> &output_extern) {
  SearchFilterByModule filter(target_sp, m_file);

  // TODO: Handle SourceLocationSpec column information
  SourceLocationSpec location_spec(file, line, /*column=*/std::nullopt,
                                   /*check_inlines=*/true,
                                   /*exact_match=*/false);
  AddressResolverFileLine resolver(location_spec);
  resolver.ResolveAddress(filter);

  for (size_t n = 0; n < resolver.GetNumberOfAddresses(); n++) {
    Address addr = resolver.GetAddressRangeAtIndex(n).GetBaseAddress();
    Function *f = addr.CalculateSymbolContextFunction();
    if (f && f == function)
      output_local.push_back(addr);
    else
      output_extern.push_back(addr);
  }
}

void Module::FindTypes(const TypeQuery &query, TypeResults &results) {
  if (SymbolFile *symbols = GetSymbolFile())
    symbols->FindTypes(query, results);
}

static Debugger::DebuggerList
DebuggersOwningModuleRequestingInterruption(Module &module) {
  Debugger::DebuggerList requestors =
      Debugger::DebuggersRequestingInterruption();
  Debugger::DebuggerList interruptors;
  if (requestors.empty())
    return interruptors;

  for (auto debugger_sp : requestors) {
    if (!debugger_sp->InterruptRequested())
      continue;
    if (debugger_sp->GetTargetList()
        .AnyTargetContainsModule(module))
      interruptors.push_back(debugger_sp);
  }
  return interruptors;
}

SymbolFile *Module::GetSymbolFile(bool can_create, Stream *feedback_strm) {
  if (!m_did_load_symfile.load()) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (!m_did_load_symfile.load() && can_create) {
      Debugger::DebuggerList interruptors =
          DebuggersOwningModuleRequestingInterruption(*this);
      if (!interruptors.empty()) {
        for (auto debugger_sp : interruptors) {
          REPORT_INTERRUPTION(*(debugger_sp.get()),
                              "Interrupted fetching symbols for module {0}",
                              this->GetFileSpec());
        }
        return nullptr;
      }
      ObjectFile *obj_file = GetObjectFile();
      if (obj_file != nullptr) {
        LLDB_SCOPED_TIMER();
        m_symfile_up.reset(
            SymbolVendor::FindPlugin(shared_from_this(), feedback_strm));
        m_did_load_symfile = true;
        if (m_unwind_table)
          m_unwind_table->Update();
      }
    }
  }
  return m_symfile_up ? m_symfile_up->GetSymbolFile() : nullptr;
}

Symtab *Module::GetSymtab() {
  if (SymbolFile *symbols = GetSymbolFile())
    return symbols->GetSymtab();
  return nullptr;
}

void Module::SetFileSpecAndObjectName(const FileSpec &file,
                                      ConstString object_name) {
  // Container objects whose paths do not specify a file directly can call this
  // function to correct the file and object names.
  m_file = file;
  m_mod_time = FileSystem::Instance().GetModificationTime(file);
  m_object_name = object_name;
}

const ArchSpec &Module::GetArchitecture() const { return m_arch; }

std::string Module::GetSpecificationDescription() const {
  std::string spec(GetFileSpec().GetPath());
  if (m_object_name) {
    spec += '(';
    spec += m_object_name.GetCString();
    spec += ')';
  }
  return spec;
}

void Module::GetDescription(llvm::raw_ostream &s,
                            lldb::DescriptionLevel level) {
  if (level >= eDescriptionLevelFull) {
    if (m_arch.IsValid())
      s << llvm::formatv("({0}) ", m_arch.GetArchitectureName());
  }

  if (level == eDescriptionLevelBrief) {
    const char *filename = m_file.GetFilename().GetCString();
    if (filename)
      s << filename;
  } else {
    char path[PATH_MAX];
    if (m_file.GetPath(path, sizeof(path)))
      s << path;
  }

  const char *object_name = m_object_name.GetCString();
  if (object_name)
    s << llvm::formatv("({0})", object_name);
}

bool Module::FileHasChanged() const {
  // We have provided the DataBuffer for this module to avoid accessing the
  // filesystem. We never want to reload those files.
  if (m_data_sp)
    return false;
  if (!m_file_has_changed)
    m_file_has_changed =
        (FileSystem::Instance().GetModificationTime(m_file) != m_mod_time);
  return m_file_has_changed;
}

void Module::ReportWarningOptimization(
    std::optional<lldb::user_id_t> debugger_id) {
  ConstString file_name = GetFileSpec().GetFilename();
  if (file_name.IsEmpty())
    return;

  StreamString ss;
  ss << file_name
     << " was compiled with optimization - stepping may behave "
        "oddly; variables may not be available.";
  Debugger::ReportWarning(std::string(ss.GetString()), debugger_id,
                          &m_optimization_warning);
}

void Module::ReportWarningUnsupportedLanguage(
    LanguageType language, std::optional<lldb::user_id_t> debugger_id) {
  StreamString ss;
  ss << "This version of LLDB has no plugin for the language \""
     << Language::GetNameForLanguageType(language)
     << "\". "
        "Inspection of frame variables will be limited.";
  Debugger::ReportWarning(std::string(ss.GetString()), debugger_id,
                          &m_language_warning);
}

void Module::ReportErrorIfModifyDetected(
    const llvm::formatv_object_base &payload) {
  if (!m_first_file_changed_log) {
    if (FileHasChanged()) {
      m_first_file_changed_log = true;
      StreamString strm;
      strm.PutCString("the object file ");
      GetDescription(strm.AsRawOstream(), lldb::eDescriptionLevelFull);
      strm.PutCString(" has been modified\n");
      strm.PutCString(payload.str());
      strm.PutCString("The debug session should be aborted as the original "
                      "debug information has been overwritten.");
      Debugger::ReportError(std::string(strm.GetString()));
    }
  }
}

void Module::ReportError(const llvm::formatv_object_base &payload) {
  StreamString strm;
  GetDescription(strm.AsRawOstream(), lldb::eDescriptionLevelBrief);
  strm.PutChar(' ');
  strm.PutCString(payload.str());
  Debugger::ReportError(strm.GetString().str());
}

void Module::ReportWarning(const llvm::formatv_object_base &payload) {
  StreamString strm;
  GetDescription(strm.AsRawOstream(), lldb::eDescriptionLevelFull);
  strm.PutChar(' ');
  strm.PutCString(payload.str());
  Debugger::ReportWarning(std::string(strm.GetString()));
}

void Module::LogMessage(Log *log, const llvm::formatv_object_base &payload) {
  StreamString log_message;
  GetDescription(log_message.AsRawOstream(), lldb::eDescriptionLevelFull);
  log_message.PutCString(": ");
  log_message.PutCString(payload.str());
  log->PutCString(log_message.GetData());
}

void Module::LogMessageVerboseBacktrace(
    Log *log, const llvm::formatv_object_base &payload) {
  StreamString log_message;
  GetDescription(log_message.AsRawOstream(), lldb::eDescriptionLevelFull);
  log_message.PutCString(": ");
  log_message.PutCString(payload.str());
  if (log->GetVerbose()) {
    std::string back_trace;
    llvm::raw_string_ostream stream(back_trace);
    llvm::sys::PrintStackTrace(stream);
    log_message.PutCString(back_trace);
  }
  log->PutCString(log_message.GetData());
}

void Module::Dump(Stream *s) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  // s->Printf("%.*p: ", (int)sizeof(void*) * 2, this);
  s->Indent();
  s->Printf("Module %s%s%s%s\n", m_file.GetPath().c_str(),
            m_object_name ? "(" : "",
            m_object_name ? m_object_name.GetCString() : "",
            m_object_name ? ")" : "");

  s->IndentMore();

  ObjectFile *objfile = GetObjectFile();
  if (objfile)
    objfile->Dump(s);

  if (SymbolFile *symbols = GetSymbolFile())
    symbols->Dump(*s);

  s->IndentLess();
}

ConstString Module::GetObjectName() const { return m_object_name; }

ObjectFile *Module::GetObjectFile() {
  if (!m_did_load_objfile.load()) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (!m_did_load_objfile.load()) {
      LLDB_SCOPED_TIMERF("Module::GetObjectFile () module = %s",
                         GetFileSpec().GetFilename().AsCString(""));
      lldb::offset_t data_offset = 0;
      lldb::offset_t file_size = 0;

      if (m_data_sp)
        file_size = m_data_sp->GetByteSize();
      else if (m_file)
        file_size = FileSystem::Instance().GetByteSize(m_file);

      if (file_size > m_object_offset) {
        m_did_load_objfile = true;
        // FindPlugin will modify its data_sp argument. Do not let it
        // modify our m_data_sp member.
        auto data_sp = m_data_sp;
        m_objfile_sp = ObjectFile::FindPlugin(
            shared_from_this(), &m_file, m_object_offset,
            file_size - m_object_offset, data_sp, data_offset);
        if (m_objfile_sp) {
          // Once we get the object file, update our module with the object
          // file's architecture since it might differ in vendor/os if some
          // parts were unknown.  But since the matching arch might already be
          // more specific than the generic COFF architecture, only merge in
          // those values that overwrite unspecified unknown values.
          m_arch.MergeFrom(m_objfile_sp->GetArchitecture());
        } else {
          ReportError("failed to load objfile for {0}\nDebugging will be "
                      "degraded for this module.",
                      GetFileSpec().GetPath().c_str());
        }
      }
    }
  }
  return m_objfile_sp.get();
}

SectionList *Module::GetSectionList() {
  // Populate m_sections_up with sections from objfile.
  if (!m_sections_up) {
    ObjectFile *obj_file = GetObjectFile();
    if (obj_file != nullptr)
      obj_file->CreateSections(*GetUnifiedSectionList());
  }
  return m_sections_up.get();
}

void Module::SectionFileAddressesChanged() {
  ObjectFile *obj_file = GetObjectFile();
  if (obj_file)
    obj_file->SectionFileAddressesChanged();
  if (SymbolFile *symbols = GetSymbolFile())
    symbols->SectionFileAddressesChanged();
}

UnwindTable &Module::GetUnwindTable() {
  if (!m_unwind_table) {
    if (!m_symfile_spec)
      SymbolLocator::DownloadSymbolFileAsync(GetUUID());
    m_unwind_table.emplace(*this);
  }
  return *m_unwind_table;
}

SectionList *Module::GetUnifiedSectionList() {
  if (!m_sections_up)
    m_sections_up = std::make_unique<SectionList>();
  return m_sections_up.get();
}

const Symbol *Module::FindFirstSymbolWithNameAndType(ConstString name,
                                                     SymbolType symbol_type) {
  LLDB_SCOPED_TIMERF(
      "Module::FindFirstSymbolWithNameAndType (name = %s, type = %i)",
      name.AsCString(), symbol_type);
  if (Symtab *symtab = GetSymtab())
    return symtab->FindFirstSymbolWithNameAndType(
        name, symbol_type, Symtab::eDebugAny, Symtab::eVisibilityAny);
  return nullptr;
}
void Module::SymbolIndicesToSymbolContextList(
    Symtab *symtab, std::vector<uint32_t> &symbol_indexes,
    SymbolContextList &sc_list) {
  // No need to protect this call using m_mutex all other method calls are
  // already thread safe.

  size_t num_indices = symbol_indexes.size();
  if (num_indices > 0) {
    SymbolContext sc;
    CalculateSymbolContext(&sc);
    for (size_t i = 0; i < num_indices; i++) {
      sc.symbol = symtab->SymbolAtIndex(symbol_indexes[i]);
      if (sc.symbol)
        sc_list.Append(sc);
    }
  }
}

void Module::FindFunctionSymbols(ConstString name, uint32_t name_type_mask,
                                 SymbolContextList &sc_list) {
  LLDB_SCOPED_TIMERF("Module::FindSymbolsFunctions (name = %s, mask = 0x%8.8x)",
                     name.AsCString(), name_type_mask);
  if (Symtab *symtab = GetSymtab())
    symtab->FindFunctionSymbols(name, name_type_mask, sc_list);
}

void Module::FindSymbolsWithNameAndType(ConstString name,
                                        SymbolType symbol_type,
                                        SymbolContextList &sc_list) {
  // No need to protect this call using m_mutex all other method calls are
  // already thread safe.
  if (Symtab *symtab = GetSymtab()) {
    std::vector<uint32_t> symbol_indexes;
    symtab->FindAllSymbolsWithNameAndType(name, symbol_type, symbol_indexes);
    SymbolIndicesToSymbolContextList(symtab, symbol_indexes, sc_list);
  }
}

void Module::FindSymbolsMatchingRegExAndType(
    const RegularExpression &regex, SymbolType symbol_type,
    SymbolContextList &sc_list, Mangled::NamePreference mangling_preference) {
  // No need to protect this call using m_mutex all other method calls are
  // already thread safe.
  LLDB_SCOPED_TIMERF(
      "Module::FindSymbolsMatchingRegExAndType (regex = %s, type = %i)",
      regex.GetText().str().c_str(), symbol_type);
  if (Symtab *symtab = GetSymtab()) {
    std::vector<uint32_t> symbol_indexes;
    symtab->FindAllSymbolsMatchingRexExAndType(
        regex, symbol_type, Symtab::eDebugAny, Symtab::eVisibilityAny,
        symbol_indexes, mangling_preference);
    SymbolIndicesToSymbolContextList(symtab, symbol_indexes, sc_list);
  }
}

void Module::PreloadSymbols() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  SymbolFile *sym_file = GetSymbolFile();
  if (!sym_file)
    return;

  // Load the object file symbol table and any symbols from the SymbolFile that
  // get appended using SymbolFile::AddSymbols(...).
  if (Symtab *symtab = sym_file->GetSymtab())
    symtab->PreloadSymbols();

  // Now let the symbol file preload its data and the symbol table will be
  // available without needing to take the module lock.
  sym_file->PreloadSymbols();
}

void Module::SetSymbolFileFileSpec(const FileSpec &file) {
  if (!FileSystem::Instance().Exists(file))
    return;
  if (m_symfile_up) {
    // Remove any sections in the unified section list that come from the
    // current symbol vendor.
    SectionList *section_list = GetSectionList();
    SymbolFile *symbol_file = GetSymbolFile();
    if (section_list && symbol_file) {
      ObjectFile *obj_file = symbol_file->GetObjectFile();
      // Make sure we have an object file and that the symbol vendor's objfile
      // isn't the same as the module's objfile before we remove any sections
      // for it...
      if (obj_file) {
        // Check to make sure we aren't trying to specify the file we already
        // have
        if (obj_file->GetFileSpec() == file) {
          // We are being told to add the exact same file that we already have
          // we don't have to do anything.
          return;
        }

        // Cleare the current symtab as we are going to replace it with a new
        // one
        obj_file->ClearSymtab();

        // The symbol file might be a directory bundle ("/tmp/a.out.dSYM")
        // instead of a full path to the symbol file within the bundle
        // ("/tmp/a.out.dSYM/Contents/Resources/DWARF/a.out"). So we need to
        // check this
        if (FileSystem::Instance().IsDirectory(file)) {
          std::string new_path(file.GetPath());
          std::string old_path(obj_file->GetFileSpec().GetPath());
          if (llvm::StringRef(old_path).starts_with(new_path)) {
            // We specified the same bundle as the symbol file that we already
            // have
            return;
          }
        }

        if (obj_file != m_objfile_sp.get()) {
          size_t num_sections = section_list->GetNumSections(0);
          for (size_t idx = num_sections; idx > 0; --idx) {
            lldb::SectionSP section_sp(
                section_list->GetSectionAtIndex(idx - 1));
            if (section_sp->GetObjectFile() == obj_file) {
              section_list->DeleteSection(idx - 1);
            }
          }
        }
      }
    }
    // Keep all old symbol files around in case there are any lingering type
    // references in any SBValue objects that might have been handed out.
    m_old_symfiles.push_back(std::move(m_symfile_up));
  }
  m_symfile_spec = file;
  m_symfile_up.reset();
  m_did_load_symfile = false;
}

bool Module::IsExecutable() {
  if (GetObjectFile() == nullptr)
    return false;
  else
    return GetObjectFile()->IsExecutable();
}

bool Module::IsLoadedInTarget(Target *target) {
  ObjectFile *obj_file = GetObjectFile();
  if (obj_file) {
    SectionList *sections = GetSectionList();
    if (sections != nullptr) {
      size_t num_sections = sections->GetSize();
      for (size_t sect_idx = 0; sect_idx < num_sections; sect_idx++) {
        SectionSP section_sp = sections->GetSectionAtIndex(sect_idx);
        if (section_sp->GetLoadBaseAddress(target) != LLDB_INVALID_ADDRESS) {
          return true;
        }
      }
    }
  }
  return false;
}

bool Module::LoadScriptingResourceInTarget(Target *target, Status &error,
                                           Stream &feedback_stream) {
  if (!target) {
    error.SetErrorString("invalid destination Target");
    return false;
  }

  LoadScriptFromSymFile should_load =
      target->TargetProperties::GetLoadScriptFromSymbolFile();

  if (should_load == eLoadScriptFromSymFileFalse)
    return false;

  Debugger &debugger = target->GetDebugger();
  const ScriptLanguage script_language = debugger.GetScriptLanguage();
  if (script_language != eScriptLanguageNone) {

    PlatformSP platform_sp(target->GetPlatform());

    if (!platform_sp) {
      error.SetErrorString("invalid Platform");
      return false;
    }

    FileSpecList file_specs = platform_sp->LocateExecutableScriptingResources(
        target, *this, feedback_stream);

    const uint32_t num_specs = file_specs.GetSize();
    if (num_specs) {
      ScriptInterpreter *script_interpreter = debugger.GetScriptInterpreter();
      if (script_interpreter) {
        for (uint32_t i = 0; i < num_specs; ++i) {
          FileSpec scripting_fspec(file_specs.GetFileSpecAtIndex(i));
          if (scripting_fspec &&
              FileSystem::Instance().Exists(scripting_fspec)) {
            if (should_load == eLoadScriptFromSymFileWarn) {
              feedback_stream.Printf(
                  "warning: '%s' contains a debug script. To run this script "
                  "in "
                  "this debug session:\n\n    command script import "
                  "\"%s\"\n\n"
                  "To run all discovered debug scripts in this session:\n\n"
                  "    settings set target.load-script-from-symbol-file "
                  "true\n",
                  GetFileSpec().GetFileNameStrippingExtension().GetCString(),
                  scripting_fspec.GetPath().c_str());
              return false;
            }
            StreamString scripting_stream;
            scripting_fspec.Dump(scripting_stream.AsRawOstream());
            LoadScriptOptions options;
            bool did_load = script_interpreter->LoadScriptingModule(
                scripting_stream.GetData(), options, error);
            if (!did_load)
              return false;
          }
        }
      } else {
        error.SetErrorString("invalid ScriptInterpreter");
        return false;
      }
    }
  }
  return true;
}

bool Module::SetArchitecture(const ArchSpec &new_arch) {
  if (!m_arch.IsValid()) {
    m_arch = new_arch;
    return true;
  }
  return m_arch.IsCompatibleMatch(new_arch);
}

bool Module::SetLoadAddress(Target &target, lldb::addr_t value,
                            bool value_is_offset, bool &changed) {
  ObjectFile *object_file = GetObjectFile();
  if (object_file != nullptr) {
    changed = object_file->SetLoadAddress(target, value, value_is_offset);
    return true;
  } else {
    changed = false;
  }
  return false;
}

bool Module::MatchesModuleSpec(const ModuleSpec &module_ref) {
  const UUID &uuid = module_ref.GetUUID();

  if (uuid.IsValid()) {
    // If the UUID matches, then nothing more needs to match...
    return (uuid == GetUUID());
  }

  const FileSpec &file_spec = module_ref.GetFileSpec();
  if (!FileSpec::Match(file_spec, m_file) &&
      !FileSpec::Match(file_spec, m_platform_file))
    return false;

  const FileSpec &platform_file_spec = module_ref.GetPlatformFileSpec();
  if (!FileSpec::Match(platform_file_spec, GetPlatformFileSpec()))
    return false;

  const ArchSpec &arch = module_ref.GetArchitecture();
  if (arch.IsValid()) {
    if (!m_arch.IsCompatibleMatch(arch))
      return false;
  }

  ConstString object_name = module_ref.GetObjectName();
  if (object_name) {
    if (object_name != GetObjectName())
      return false;
  }
  return true;
}

bool Module::FindSourceFile(const FileSpec &orig_spec,
                            FileSpec &new_spec) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (auto remapped = m_source_mappings.FindFile(orig_spec)) {
    new_spec = *remapped;
    return true;
  }
  return false;
}

std::optional<std::string> Module::RemapSourceFile(llvm::StringRef path) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (auto remapped = m_source_mappings.RemapPath(path))
    return remapped->GetPath();
  return {};
}

void Module::RegisterXcodeSDK(llvm::StringRef sdk_name,
                              llvm::StringRef sysroot) {
  auto sdk_path_or_err =
      HostInfo::GetSDKRoot(HostInfo::SDKOptions{sdk_name.str()});

  if (!sdk_path_or_err) {
    Debugger::ReportError("Error while searching for Xcode SDK: " +
                          toString(sdk_path_or_err.takeError()));
    return;
  }

  auto sdk_path = *sdk_path_or_err;
  if (sdk_path.empty())
    return;
  // If the SDK changed for a previously registered source path, update it.
  // This could happend with -fdebug-prefix-map, otherwise it's unlikely.
  if (!m_source_mappings.Replace(sysroot, sdk_path, true))
    // In the general case, however, append it to the list.
    m_source_mappings.Append(sysroot, sdk_path, false);
}

bool Module::MergeArchitecture(const ArchSpec &arch_spec) {
  if (!arch_spec.IsValid())
    return false;
  LLDB_LOGF(GetLog(LLDBLog::Object | LLDBLog::Modules),
            "module has arch %s, merging/replacing with arch %s",
            m_arch.GetTriple().getTriple().c_str(),
            arch_spec.GetTriple().getTriple().c_str());
  if (!m_arch.IsCompatibleMatch(arch_spec)) {
    // The new architecture is different, we just need to replace it.
    return SetArchitecture(arch_spec);
  }

  // Merge bits from arch_spec into "merged_arch" and set our architecture.
  ArchSpec merged_arch(m_arch);
  merged_arch.MergeFrom(arch_spec);
  // SetArchitecture() is a no-op if m_arch is already valid.
  m_arch = ArchSpec();
  return SetArchitecture(merged_arch);
}

llvm::VersionTuple Module::GetVersion() {
  if (ObjectFile *obj_file = GetObjectFile())
    return obj_file->GetVersion();
  return llvm::VersionTuple();
}

bool Module::GetIsDynamicLinkEditor() {
  ObjectFile *obj_file = GetObjectFile();

  if (obj_file)
    return obj_file->GetIsDynamicLinkEditor();

  return false;
}

uint32_t Module::Hash() {
  std::string identifier;
  llvm::raw_string_ostream id_strm(identifier);
  id_strm << m_arch.GetTriple().str() << '-' << m_file.GetPath();
  if (m_object_name)
    id_strm << '(' << m_object_name << ')';
  if (m_object_offset > 0)
    id_strm << m_object_offset;
  const auto mtime = llvm::sys::toTimeT(m_object_mod_time);
  if (mtime > 0)
    id_strm << mtime;
  return llvm::djbHash(id_strm.str());
}

std::string Module::GetCacheKey() {
  std::string key;
  llvm::raw_string_ostream strm(key);
  strm << m_arch.GetTriple().str() << '-' << m_file.GetFilename();
  if (m_object_name)
    strm << '(' << m_object_name << ')';
  strm << '-' << llvm::format_hex(Hash(), 10);
  return strm.str();
}

DataFileCache *Module::GetIndexCache() {
  if (!ModuleList::GetGlobalModuleListProperties().GetEnableLLDBIndexCache())
    return nullptr;
  // NOTE: intentional leak so we don't crash if global destructor chain gets
  // called as other threads still use the result of this function
  static DataFileCache *g_data_file_cache =
      new DataFileCache(ModuleList::GetGlobalModuleListProperties()
                            .GetLLDBIndexCachePath()
                            .GetPath());
  return g_data_file_cache;
}
