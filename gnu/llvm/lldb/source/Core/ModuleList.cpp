//===-- ModuleList.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/ModuleList.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Interpreter/OptionValueFileSpec.h"
#include "lldb/Interpreter/OptionValueFileSpecList.h"
#include "lldb/Interpreter/OptionValueProperties.h"
#include "lldb/Interpreter/Property.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-defines.h"

#if defined(_WIN32)
#include "lldb/Host/windows/PosixApi.h"
#endif

#include "clang/Driver/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace lldb_private {
class Function;
}
namespace lldb_private {
class RegularExpression;
}
namespace lldb_private {
class Stream;
}
namespace lldb_private {
class SymbolFile;
}
namespace lldb_private {
class Target;
}

using namespace lldb;
using namespace lldb_private;

namespace {

#define LLDB_PROPERTIES_modulelist
#include "CoreProperties.inc"

enum {
#define LLDB_PROPERTIES_modulelist
#include "CorePropertiesEnum.inc"
};

} // namespace

ModuleListProperties::ModuleListProperties() {
  m_collection_sp = std::make_shared<OptionValueProperties>("symbols");
  m_collection_sp->Initialize(g_modulelist_properties);
  m_collection_sp->SetValueChangedCallback(ePropertySymLinkPaths,
                                           [this] { UpdateSymlinkMappings(); });

  llvm::SmallString<128> path;
  if (clang::driver::Driver::getDefaultModuleCachePath(path)) {
    lldbassert(SetClangModulesCachePath(FileSpec(path)));
  }

  path.clear();
  if (llvm::sys::path::cache_directory(path)) {
    llvm::sys::path::append(path, "lldb");
    llvm::sys::path::append(path, "IndexCache");
    lldbassert(SetLLDBIndexCachePath(FileSpec(path)));
  }

}

bool ModuleListProperties::GetEnableExternalLookup() const {
  const uint32_t idx = ePropertyEnableExternalLookup;
  return GetPropertyAtIndexAs<bool>(
      idx, g_modulelist_properties[idx].default_uint_value != 0);
}

bool ModuleListProperties::SetEnableExternalLookup(bool new_value) {
  return SetPropertyAtIndex(ePropertyEnableExternalLookup, new_value);
}

SymbolDownload ModuleListProperties::GetSymbolAutoDownload() const {
  // Backward compatibility alias.
  if (GetPropertyAtIndexAs<bool>(ePropertyEnableBackgroundLookup, false))
    return eSymbolDownloadBackground;

  const uint32_t idx = ePropertyAutoDownload;
  return GetPropertyAtIndexAs<lldb::SymbolDownload>(
      idx, static_cast<lldb::SymbolDownload>(
               g_modulelist_properties[idx].default_uint_value));
}

FileSpec ModuleListProperties::GetClangModulesCachePath() const {
  const uint32_t idx = ePropertyClangModulesCachePath;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

bool ModuleListProperties::SetClangModulesCachePath(const FileSpec &path) {
  const uint32_t idx = ePropertyClangModulesCachePath;
  return SetPropertyAtIndex(idx, path);
}

FileSpec ModuleListProperties::GetLLDBIndexCachePath() const {
  const uint32_t idx = ePropertyLLDBIndexCachePath;
  return GetPropertyAtIndexAs<FileSpec>(idx, {});
}

bool ModuleListProperties::SetLLDBIndexCachePath(const FileSpec &path) {
  const uint32_t idx = ePropertyLLDBIndexCachePath;
  return SetPropertyAtIndex(idx, path);
}

bool ModuleListProperties::GetEnableLLDBIndexCache() const {
  const uint32_t idx = ePropertyEnableLLDBIndexCache;
  return GetPropertyAtIndexAs<bool>(
      idx, g_modulelist_properties[idx].default_uint_value != 0);
}

bool ModuleListProperties::SetEnableLLDBIndexCache(bool new_value) {
  return SetPropertyAtIndex(ePropertyEnableLLDBIndexCache, new_value);
}

uint64_t ModuleListProperties::GetLLDBIndexCacheMaxByteSize() {
  const uint32_t idx = ePropertyLLDBIndexCacheMaxByteSize;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_modulelist_properties[idx].default_uint_value);
}

uint64_t ModuleListProperties::GetLLDBIndexCacheMaxPercent() {
  const uint32_t idx = ePropertyLLDBIndexCacheMaxPercent;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_modulelist_properties[idx].default_uint_value);
}

uint64_t ModuleListProperties::GetLLDBIndexCacheExpirationDays() {
  const uint32_t idx = ePropertyLLDBIndexCacheExpirationDays;
  return GetPropertyAtIndexAs<uint64_t>(
      idx, g_modulelist_properties[idx].default_uint_value);
}

void ModuleListProperties::UpdateSymlinkMappings() {
  FileSpecList list =
      GetPropertyAtIndexAs<FileSpecList>(ePropertySymLinkPaths, {});
  llvm::sys::ScopedWriter lock(m_symlink_paths_mutex);
  const bool notify = false;
  m_symlink_paths.Clear(notify);
  for (auto symlink : list) {
    FileSpec resolved;
    Status status = FileSystem::Instance().Readlink(symlink, resolved);
    if (status.Success())
      m_symlink_paths.Append(symlink.GetPath(), resolved.GetPath(), notify);
  }
}

PathMappingList ModuleListProperties::GetSymlinkMappings() const {
  llvm::sys::ScopedReader lock(m_symlink_paths_mutex);
  return m_symlink_paths;
}

bool ModuleListProperties::GetLoadSymbolOnDemand() {
  const uint32_t idx = ePropertyLoadSymbolOnDemand;
  return GetPropertyAtIndexAs<bool>(
      idx, g_modulelist_properties[idx].default_uint_value != 0);
}

ModuleList::ModuleList() : m_modules(), m_modules_mutex() {}

ModuleList::ModuleList(const ModuleList &rhs) : m_modules(), m_modules_mutex() {
  std::lock_guard<std::recursive_mutex> lhs_guard(m_modules_mutex);
  std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_modules_mutex);
  m_modules = rhs.m_modules;
}

ModuleList::ModuleList(ModuleList::Notifier *notifier)
    : m_modules(), m_modules_mutex(), m_notifier(notifier) {}

const ModuleList &ModuleList::operator=(const ModuleList &rhs) {
  if (this != &rhs) {
    std::lock(m_modules_mutex, rhs.m_modules_mutex);
    std::lock_guard<std::recursive_mutex> lhs_guard(m_modules_mutex,
                                                    std::adopt_lock);
    std::lock_guard<std::recursive_mutex> rhs_guard(rhs.m_modules_mutex,
                                                    std::adopt_lock);
    m_modules = rhs.m_modules;
  }
  return *this;
}

ModuleList::~ModuleList() = default;

void ModuleList::AppendImpl(const ModuleSP &module_sp, bool use_notifier) {
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    // We are required to keep the first element of the Module List as the
    // executable module.  So check here and if the first module is NOT an 
    // but the new one is, we insert this module at the beginning, rather than 
    // at the end.
    // We don't need to do any of this if the list is empty:
    if (m_modules.empty()) {
      m_modules.push_back(module_sp);
    } else {
      // Since producing the ObjectFile may take some work, first check the 0th
      // element, and only if that's NOT an executable look at the incoming
      // ObjectFile.  That way in the normal case we only look at the element
      // 0 ObjectFile. 
      const bool elem_zero_is_executable 
          = m_modules[0]->GetObjectFile()->GetType() 
              == ObjectFile::Type::eTypeExecutable;
      lldb_private::ObjectFile *obj = module_sp->GetObjectFile();
      if (!elem_zero_is_executable && obj 
          && obj->GetType() == ObjectFile::Type::eTypeExecutable) {
        m_modules.insert(m_modules.begin(), module_sp);
      } else {
        m_modules.push_back(module_sp);
      }
    }
    if (use_notifier && m_notifier)
      m_notifier->NotifyModuleAdded(*this, module_sp);
  }
}

void ModuleList::Append(const ModuleSP &module_sp, bool notify) {
  AppendImpl(module_sp, notify);
}

void ModuleList::ReplaceEquivalent(
    const ModuleSP &module_sp,
    llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules) {
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);

    // First remove any equivalent modules. Equivalent modules are modules
    // whose path, platform path and architecture match.
    ModuleSpec equivalent_module_spec(module_sp->GetFileSpec(),
                                      module_sp->GetArchitecture());
    equivalent_module_spec.GetPlatformFileSpec() =
        module_sp->GetPlatformFileSpec();

    size_t idx = 0;
    while (idx < m_modules.size()) {
      ModuleSP test_module_sp(m_modules[idx]);
      if (test_module_sp->MatchesModuleSpec(equivalent_module_spec)) {
        if (old_modules)
          old_modules->push_back(test_module_sp);
        RemoveImpl(m_modules.begin() + idx);
      } else {
        ++idx;
      }
    }
    // Now add the new module to the list
    Append(module_sp);
  }
}

bool ModuleList::AppendIfNeeded(const ModuleSP &new_module, bool notify) {
  if (new_module) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    for (const ModuleSP &module_sp : m_modules) {
      if (module_sp.get() == new_module.get())
        return false; // Already in the list
    }
    // Only push module_sp on the list if it wasn't already in there.
    Append(new_module, notify);
    return true;
  }
  return false;
}

void ModuleList::Append(const ModuleList &module_list) {
  for (auto pos : module_list.m_modules)
    Append(pos);
}

bool ModuleList::AppendIfNeeded(const ModuleList &module_list) {
  bool any_in = false;
  for (auto pos : module_list.m_modules) {
    if (AppendIfNeeded(pos))
      any_in = true;
  }
  return any_in;
}

bool ModuleList::RemoveImpl(const ModuleSP &module_sp, bool use_notifier) {
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      if (pos->get() == module_sp.get()) {
        m_modules.erase(pos);
        if (use_notifier && m_notifier)
          m_notifier->NotifyModuleRemoved(*this, module_sp);
        return true;
      }
    }
  }
  return false;
}

ModuleList::collection::iterator
ModuleList::RemoveImpl(ModuleList::collection::iterator pos,
                       bool use_notifier) {
  ModuleSP module_sp(*pos);
  collection::iterator retval = m_modules.erase(pos);
  if (use_notifier && m_notifier)
    m_notifier->NotifyModuleRemoved(*this, module_sp);
  return retval;
}

bool ModuleList::Remove(const ModuleSP &module_sp, bool notify) {
  return RemoveImpl(module_sp, notify);
}

bool ModuleList::ReplaceModule(const lldb::ModuleSP &old_module_sp,
                               const lldb::ModuleSP &new_module_sp) {
  if (!RemoveImpl(old_module_sp, false))
    return false;
  AppendImpl(new_module_sp, false);
  if (m_notifier)
    m_notifier->NotifyModuleUpdated(*this, old_module_sp, new_module_sp);
  return true;
}

bool ModuleList::RemoveIfOrphaned(const Module *module_ptr) {
  if (module_ptr) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      if (pos->get() == module_ptr) {
        if (pos->use_count() == 1) {
          pos = RemoveImpl(pos);
          return true;
        } else
          return false;
      }
    }
  }
  return false;
}

size_t ModuleList::RemoveOrphans(bool mandatory) {
  std::unique_lock<std::recursive_mutex> lock(m_modules_mutex, std::defer_lock);

  if (mandatory) {
    lock.lock();
  } else {
    // Not mandatory, remove orphans if we can get the mutex
    if (!lock.try_lock())
      return 0;
  }
  size_t remove_count = 0;
  // Modules might hold shared pointers to other modules, so removing one
  // module might make other modules orphans. Keep removing modules until
  // there are no further modules that can be removed.
  bool made_progress = true;
  while (made_progress) {
    // Keep track if we make progress this iteration.
    made_progress = false;
    collection::iterator pos = m_modules.begin();
    while (pos != m_modules.end()) {
      if (pos->use_count() == 1) {
        pos = RemoveImpl(pos);
        ++remove_count;
        // We did make progress.
        made_progress = true;
      } else {
        ++pos;
      }
    }
  }
  return remove_count;
}

size_t ModuleList::Remove(ModuleList &module_list) {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  size_t num_removed = 0;
  collection::iterator pos, end = module_list.m_modules.end();
  for (pos = module_list.m_modules.begin(); pos != end; ++pos) {
    if (Remove(*pos, false /* notify */))
      ++num_removed;
  }
  if (m_notifier)
    m_notifier->NotifyModulesRemoved(module_list);
  return num_removed;
}

void ModuleList::Clear() { ClearImpl(); }

void ModuleList::Destroy() { ClearImpl(); }

void ModuleList::ClearImpl(bool use_notifier) {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  if (use_notifier && m_notifier)
    m_notifier->NotifyWillClearList(*this);
  m_modules.clear();
}

Module *ModuleList::GetModulePointerAtIndex(size_t idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  if (idx < m_modules.size())
    return m_modules[idx].get();
  return nullptr;
}

ModuleSP ModuleList::GetModuleAtIndex(size_t idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  return GetModuleAtIndexUnlocked(idx);
}

ModuleSP ModuleList::GetModuleAtIndexUnlocked(size_t idx) const {
  ModuleSP module_sp;
  if (idx < m_modules.size())
    module_sp = m_modules[idx];
  return module_sp;
}

void ModuleList::FindFunctions(ConstString name,
                               FunctionNameType name_type_mask,
                               const ModuleFunctionSearchOptions &options,
                               SymbolContextList &sc_list) const {
  const size_t old_size = sc_list.GetSize();

  if (name_type_mask & eFunctionNameTypeAuto) {
    Module::LookupInfo lookup_info(name, name_type_mask, eLanguageTypeUnknown);

    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    for (const ModuleSP &module_sp : m_modules) {
      module_sp->FindFunctions(lookup_info, CompilerDeclContext(), options,
                               sc_list);
    }

    const size_t new_size = sc_list.GetSize();

    if (old_size < new_size)
      lookup_info.Prune(sc_list, old_size);
  } else {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    for (const ModuleSP &module_sp : m_modules) {
      module_sp->FindFunctions(name, CompilerDeclContext(), name_type_mask,
                               options, sc_list);
    }
  }
}

void ModuleList::FindFunctionSymbols(ConstString name,
                                     lldb::FunctionNameType name_type_mask,
                                     SymbolContextList &sc_list) {
  const size_t old_size = sc_list.GetSize();

  if (name_type_mask & eFunctionNameTypeAuto) {
    Module::LookupInfo lookup_info(name, name_type_mask, eLanguageTypeUnknown);

    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    for (const ModuleSP &module_sp : m_modules) {
      module_sp->FindFunctionSymbols(lookup_info.GetLookupName(),
                                     lookup_info.GetNameTypeMask(), sc_list);
    }

    const size_t new_size = sc_list.GetSize();

    if (old_size < new_size)
      lookup_info.Prune(sc_list, old_size);
  } else {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    for (const ModuleSP &module_sp : m_modules) {
      module_sp->FindFunctionSymbols(name, name_type_mask, sc_list);
    }
  }
}

void ModuleList::FindFunctions(const RegularExpression &name,
                               const ModuleFunctionSearchOptions &options,
                               SymbolContextList &sc_list) {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules)
    module_sp->FindFunctions(name, options, sc_list);
}

void ModuleList::FindCompileUnits(const FileSpec &path,
                                  SymbolContextList &sc_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules)
    module_sp->FindCompileUnits(path, sc_list);
}

void ModuleList::FindGlobalVariables(ConstString name, size_t max_matches,
                                     VariableList &variable_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules) {
    module_sp->FindGlobalVariables(name, CompilerDeclContext(), max_matches,
                                   variable_list);
  }
}

void ModuleList::FindGlobalVariables(const RegularExpression &regex,
                                     size_t max_matches,
                                     VariableList &variable_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules)
    module_sp->FindGlobalVariables(regex, max_matches, variable_list);
}

void ModuleList::FindSymbolsWithNameAndType(ConstString name,
                                            SymbolType symbol_type,
                                            SymbolContextList &sc_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules)
    module_sp->FindSymbolsWithNameAndType(name, symbol_type, sc_list);
}

void ModuleList::FindSymbolsMatchingRegExAndType(
    const RegularExpression &regex, lldb::SymbolType symbol_type,
    SymbolContextList &sc_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules)
    module_sp->FindSymbolsMatchingRegExAndType(regex, symbol_type, sc_list);
}

void ModuleList::FindModules(const ModuleSpec &module_spec,
                             ModuleList &matching_module_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules) {
    if (module_sp->MatchesModuleSpec(module_spec))
      matching_module_list.Append(module_sp);
  }
}

ModuleSP ModuleList::FindModule(const Module *module_ptr) const {
  ModuleSP module_sp;

  // Scope for "locker"
  {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();

    for (pos = m_modules.begin(); pos != end; ++pos) {
      if ((*pos).get() == module_ptr) {
        module_sp = (*pos);
        break;
      }
    }
  }
  return module_sp;
}

ModuleSP ModuleList::FindModule(const UUID &uuid) const {
  ModuleSP module_sp;

  if (uuid.IsValid()) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();

    for (pos = m_modules.begin(); pos != end; ++pos) {
      if ((*pos)->GetUUID() == uuid) {
        module_sp = (*pos);
        break;
      }
    }
  }
  return module_sp;
}

void ModuleList::FindTypes(Module *search_first, const TypeQuery &query,
                           TypeResults &results) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  if (search_first) {
    search_first->FindTypes(query, results);
    if (results.Done(query))
      return;
  }
  for (const auto &module_sp : m_modules) {
    if (search_first != module_sp.get()) {
      module_sp->FindTypes(query, results);
      if (results.Done(query))
        return;
    }
  }
}

bool ModuleList::FindSourceFile(const FileSpec &orig_spec,
                                FileSpec &new_spec) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules) {
    if (module_sp->FindSourceFile(orig_spec, new_spec))
      return true;
  }
  return false;
}

void ModuleList::FindAddressesForLine(const lldb::TargetSP target_sp,
                                      const FileSpec &file, uint32_t line,
                                      Function *function,
                                      std::vector<Address> &output_local,
                                      std::vector<Address> &output_extern) {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules) {
    module_sp->FindAddressesForLine(target_sp, file, line, function,
                                    output_local, output_extern);
  }
}

ModuleSP ModuleList::FindFirstModule(const ModuleSpec &module_spec) const {
  ModuleSP module_sp;
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  collection::const_iterator pos, end = m_modules.end();
  for (pos = m_modules.begin(); pos != end; ++pos) {
    ModuleSP module_sp(*pos);
    if (module_sp->MatchesModuleSpec(module_spec))
      return module_sp;
  }
  return module_sp;
}

size_t ModuleList::GetSize() const {
  size_t size = 0;
  {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    size = m_modules.size();
  }
  return size;
}

void ModuleList::Dump(Stream *s) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules)
    module_sp->Dump(s);
}

void ModuleList::LogUUIDAndPaths(Log *log, const char *prefix_cstr) {
  if (log != nullptr) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, begin = m_modules.begin(),
                                    end = m_modules.end();
    for (pos = begin; pos != end; ++pos) {
      Module *module = pos->get();
      const FileSpec &module_file_spec = module->GetFileSpec();
      LLDB_LOGF(log, "%s[%u] %s (%s) \"%s\"", prefix_cstr ? prefix_cstr : "",
                (uint32_t)std::distance(begin, pos),
                module->GetUUID().GetAsString().c_str(),
                module->GetArchitecture().GetArchitectureName(),
                module_file_spec.GetPath().c_str());
    }
  }
}

bool ModuleList::ResolveFileAddress(lldb::addr_t vm_addr,
                                    Address &so_addr) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules) {
    if (module_sp->ResolveFileAddress(vm_addr, so_addr))
      return true;
  }

  return false;
}

uint32_t
ModuleList::ResolveSymbolContextForAddress(const Address &so_addr,
                                           SymbolContextItem resolve_scope,
                                           SymbolContext &sc) const {
  // The address is already section offset so it has a module
  uint32_t resolved_flags = 0;
  ModuleSP module_sp(so_addr.GetModule());
  if (module_sp) {
    resolved_flags =
        module_sp->ResolveSymbolContextForAddress(so_addr, resolve_scope, sc);
  } else {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos, end = m_modules.end();
    for (pos = m_modules.begin(); pos != end; ++pos) {
      resolved_flags =
          (*pos)->ResolveSymbolContextForAddress(so_addr, resolve_scope, sc);
      if (resolved_flags != 0)
        break;
    }
  }

  return resolved_flags;
}

uint32_t ModuleList::ResolveSymbolContextForFilePath(
    const char *file_path, uint32_t line, bool check_inlines,
    SymbolContextItem resolve_scope, SymbolContextList &sc_list) const {
  FileSpec file_spec(file_path);
  return ResolveSymbolContextsForFileSpec(file_spec, line, check_inlines,
                                          resolve_scope, sc_list);
}

uint32_t ModuleList::ResolveSymbolContextsForFileSpec(
    const FileSpec &file_spec, uint32_t line, bool check_inlines,
    SymbolContextItem resolve_scope, SymbolContextList &sc_list) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const ModuleSP &module_sp : m_modules) {
    module_sp->ResolveSymbolContextsForFileSpec(file_spec, line, check_inlines,
                                                resolve_scope, sc_list);
  }

  return sc_list.GetSize();
}

size_t ModuleList::GetIndexForModule(const Module *module) const {
  if (module) {
    std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
    collection::const_iterator pos;
    collection::const_iterator begin = m_modules.begin();
    collection::const_iterator end = m_modules.end();
    for (pos = begin; pos != end; ++pos) {
      if ((*pos).get() == module)
        return std::distance(begin, pos);
    }
  }
  return LLDB_INVALID_INDEX32;
}

namespace {
struct SharedModuleListInfo {
  ModuleList module_list;
  ModuleListProperties module_list_properties;
};
}
static SharedModuleListInfo &GetSharedModuleListInfo()
{
  static SharedModuleListInfo *g_shared_module_list_info = nullptr;
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    // NOTE: Intentionally leak the module list so a program doesn't have to
    // cleanup all modules and object files as it exits. This just wastes time
    // doing a bunch of cleanup that isn't required.
    if (g_shared_module_list_info == nullptr)
      g_shared_module_list_info = new SharedModuleListInfo();
  });
  return *g_shared_module_list_info;
}

static ModuleList &GetSharedModuleList() {
  return GetSharedModuleListInfo().module_list;
}

ModuleListProperties &ModuleList::GetGlobalModuleListProperties() {
  return GetSharedModuleListInfo().module_list_properties;
}

bool ModuleList::ModuleIsInCache(const Module *module_ptr) {
  if (module_ptr) {
    ModuleList &shared_module_list = GetSharedModuleList();
    return shared_module_list.FindModule(module_ptr).get() != nullptr;
  }
  return false;
}

void ModuleList::FindSharedModules(const ModuleSpec &module_spec,
                                   ModuleList &matching_module_list) {
  GetSharedModuleList().FindModules(module_spec, matching_module_list);
}

lldb::ModuleSP ModuleList::FindSharedModule(const UUID &uuid) {
  return GetSharedModuleList().FindModule(uuid);
}

size_t ModuleList::RemoveOrphanSharedModules(bool mandatory) {
  return GetSharedModuleList().RemoveOrphans(mandatory);
}

Status
ModuleList::GetSharedModule(const ModuleSpec &module_spec, ModuleSP &module_sp,
                            const FileSpecList *module_search_paths_ptr,
                            llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules,
                            bool *did_create_ptr, bool always_create) {
  ModuleList &shared_module_list = GetSharedModuleList();
  std::lock_guard<std::recursive_mutex> guard(
      shared_module_list.m_modules_mutex);
  char path[PATH_MAX];

  Status error;

  module_sp.reset();

  if (did_create_ptr)
    *did_create_ptr = false;

  const UUID *uuid_ptr = module_spec.GetUUIDPtr();
  const FileSpec &module_file_spec = module_spec.GetFileSpec();
  const ArchSpec &arch = module_spec.GetArchitecture();

  // Make sure no one else can try and get or create a module while this
  // function is actively working on it by doing an extra lock on the global
  // mutex list.
  if (!always_create) {
    ModuleList matching_module_list;
    shared_module_list.FindModules(module_spec, matching_module_list);
    const size_t num_matching_modules = matching_module_list.GetSize();

    if (num_matching_modules > 0) {
      for (size_t module_idx = 0; module_idx < num_matching_modules;
           ++module_idx) {
        module_sp = matching_module_list.GetModuleAtIndex(module_idx);

        // Make sure the file for the module hasn't been modified
        if (module_sp->FileHasChanged()) {
          if (old_modules)
            old_modules->push_back(module_sp);

          Log *log = GetLog(LLDBLog::Modules);
          if (log != nullptr)
            LLDB_LOGF(
                log, "%p '%s' module changed: removing from global module list",
                static_cast<void *>(module_sp.get()),
                module_sp->GetFileSpec().GetFilename().GetCString());

          shared_module_list.Remove(module_sp);
          module_sp.reset();
        } else {
          // The module matches and the module was not modified from when it
          // was last loaded.
          return error;
        }
      }
    }
  }

  if (module_sp)
    return error;

  module_sp = std::make_shared<Module>(module_spec);
  // Make sure there are a module and an object file since we can specify a
  // valid file path with an architecture that might not be in that file. By
  // getting the object file we can guarantee that the architecture matches
  if (module_sp->GetObjectFile()) {
    // If we get in here we got the correct arch, now we just need to verify
    // the UUID if one was given
    if (uuid_ptr && *uuid_ptr != module_sp->GetUUID()) {
      module_sp.reset();
    } else {
      if (module_sp->GetObjectFile() &&
          module_sp->GetObjectFile()->GetType() ==
              ObjectFile::eTypeStubLibrary) {
        module_sp.reset();
      } else {
        if (did_create_ptr) {
          *did_create_ptr = true;
        }

        shared_module_list.ReplaceEquivalent(module_sp, old_modules);
        return error;
      }
    }
  } else {
    module_sp.reset();
  }

  if (module_search_paths_ptr) {
    const auto num_directories = module_search_paths_ptr->GetSize();
    for (size_t idx = 0; idx < num_directories; ++idx) {
      auto search_path_spec = module_search_paths_ptr->GetFileSpecAtIndex(idx);
      FileSystem::Instance().Resolve(search_path_spec);
      namespace fs = llvm::sys::fs;
      if (!FileSystem::Instance().IsDirectory(search_path_spec))
        continue;
      search_path_spec.AppendPathComponent(
          module_spec.GetFileSpec().GetFilename().GetStringRef());
      if (!FileSystem::Instance().Exists(search_path_spec))
        continue;

      auto resolved_module_spec(module_spec);
      resolved_module_spec.GetFileSpec() = search_path_spec;
      module_sp = std::make_shared<Module>(resolved_module_spec);
      if (module_sp->GetObjectFile()) {
        // If we get in here we got the correct arch, now we just need to
        // verify the UUID if one was given
        if (uuid_ptr && *uuid_ptr != module_sp->GetUUID()) {
          module_sp.reset();
        } else {
          if (module_sp->GetObjectFile()->GetType() ==
              ObjectFile::eTypeStubLibrary) {
            module_sp.reset();
          } else {
            if (did_create_ptr)
              *did_create_ptr = true;

            shared_module_list.ReplaceEquivalent(module_sp, old_modules);
            return Status();
          }
        }
      } else {
        module_sp.reset();
      }
    }
  }

  // Either the file didn't exist where at the path, or no path was given, so
  // we now have to use more extreme measures to try and find the appropriate
  // module.

  // Fixup the incoming path in case the path points to a valid file, yet the
  // arch or UUID (if one was passed in) don't match.
  ModuleSpec located_binary_modulespec =
      PluginManager::LocateExecutableObjectFile(module_spec);

  // Don't look for the file if it appears to be the same one we already
  // checked for above...
  if (located_binary_modulespec.GetFileSpec() != module_file_spec) {
    if (!FileSystem::Instance().Exists(
            located_binary_modulespec.GetFileSpec())) {
      located_binary_modulespec.GetFileSpec().GetPath(path, sizeof(path));
      if (path[0] == '\0')
        module_file_spec.GetPath(path, sizeof(path));
      // How can this check ever be true? This branch it is false, and we
      // haven't modified file_spec.
      if (FileSystem::Instance().Exists(
              located_binary_modulespec.GetFileSpec())) {
        std::string uuid_str;
        if (uuid_ptr && uuid_ptr->IsValid())
          uuid_str = uuid_ptr->GetAsString();

        if (arch.IsValid()) {
          if (!uuid_str.empty())
            error.SetErrorStringWithFormat(
                "'%s' does not contain the %s architecture and UUID %s", path,
                arch.GetArchitectureName(), uuid_str.c_str());
          else
            error.SetErrorStringWithFormat(
                "'%s' does not contain the %s architecture.", path,
                arch.GetArchitectureName());
        }
      } else {
        error.SetErrorStringWithFormat("'%s' does not exist", path);
      }
      if (error.Fail())
        module_sp.reset();
      return error;
    }

    // Make sure no one else can try and get or create a module while this
    // function is actively working on it by doing an extra lock on the global
    // mutex list.
    ModuleSpec platform_module_spec(module_spec);
    platform_module_spec.GetFileSpec() =
        located_binary_modulespec.GetFileSpec();
    platform_module_spec.GetPlatformFileSpec() =
        located_binary_modulespec.GetFileSpec();
    platform_module_spec.GetSymbolFileSpec() =
        located_binary_modulespec.GetSymbolFileSpec();
    ModuleList matching_module_list;
    shared_module_list.FindModules(platform_module_spec, matching_module_list);
    if (!matching_module_list.IsEmpty()) {
      module_sp = matching_module_list.GetModuleAtIndex(0);

      // If we didn't have a UUID in mind when looking for the object file,
      // then we should make sure the modification time hasn't changed!
      if (platform_module_spec.GetUUIDPtr() == nullptr) {
        auto file_spec_mod_time = FileSystem::Instance().GetModificationTime(
            located_binary_modulespec.GetFileSpec());
        if (file_spec_mod_time != llvm::sys::TimePoint<>()) {
          if (file_spec_mod_time != module_sp->GetModificationTime()) {
            if (old_modules)
              old_modules->push_back(module_sp);
            shared_module_list.Remove(module_sp);
            module_sp.reset();
          }
        }
      }
    }

    if (!module_sp) {
      module_sp = std::make_shared<Module>(platform_module_spec);
      // Make sure there are a module and an object file since we can specify a
      // valid file path with an architecture that might not be in that file.
      // By getting the object file we can guarantee that the architecture
      // matches
      if (module_sp && module_sp->GetObjectFile()) {
        if (module_sp->GetObjectFile()->GetType() ==
            ObjectFile::eTypeStubLibrary) {
          module_sp.reset();
        } else {
          if (did_create_ptr)
            *did_create_ptr = true;

          shared_module_list.ReplaceEquivalent(module_sp, old_modules);
        }
      } else {
        located_binary_modulespec.GetFileSpec().GetPath(path, sizeof(path));

        if (located_binary_modulespec.GetFileSpec()) {
          if (arch.IsValid())
            error.SetErrorStringWithFormat(
                "unable to open %s architecture in '%s'",
                arch.GetArchitectureName(), path);
          else
            error.SetErrorStringWithFormat("unable to open '%s'", path);
        } else {
          std::string uuid_str;
          if (uuid_ptr && uuid_ptr->IsValid())
            uuid_str = uuid_ptr->GetAsString();

          if (!uuid_str.empty())
            error.SetErrorStringWithFormat(
                "cannot locate a module for UUID '%s'", uuid_str.c_str());
          else
            error.SetErrorString("cannot locate a module");
        }
      }
    }
  }

  return error;
}

bool ModuleList::RemoveSharedModule(lldb::ModuleSP &module_sp) {
  return GetSharedModuleList().Remove(module_sp);
}

bool ModuleList::RemoveSharedModuleIfOrphaned(const Module *module_ptr) {
  return GetSharedModuleList().RemoveIfOrphaned(module_ptr);
}

bool ModuleList::LoadScriptingResourcesInTarget(Target *target,
                                                std::list<Status> &errors,
                                                Stream &feedback_stream,
                                                bool continue_on_error) {
  if (!target)
    return false;
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (auto module : m_modules) {
    Status error;
    if (module) {
      if (!module->LoadScriptingResourceInTarget(target, error,
                                                 feedback_stream)) {
        if (error.Fail() && error.AsCString()) {
          error.SetErrorStringWithFormat("unable to load scripting data for "
                                         "module %s - error reported was %s",
                                         module->GetFileSpec()
                                             .GetFileNameStrippingExtension()
                                             .GetCString(),
                                         error.AsCString());
          errors.push_back(error);

          if (!continue_on_error)
            return false;
        }
      }
    }
  }
  return errors.empty();
}

void ModuleList::ForEach(
    std::function<bool(const ModuleSP &module_sp)> const &callback) const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const auto &module_sp : m_modules) {
    assert(module_sp != nullptr);
    // If the callback returns false, then stop iterating and break out
    if (!callback(module_sp))
      break;
  }
}

bool ModuleList::AnyOf(
    std::function<bool(lldb_private::Module &module_sp)> const &callback)
    const {
  std::lock_guard<std::recursive_mutex> guard(m_modules_mutex);
  for (const auto &module_sp : m_modules) {
    assert(module_sp != nullptr);
    if (callback(*module_sp))
      return true;
  }

  return false;
}


void ModuleList::Swap(ModuleList &other) {
  // scoped_lock locks both mutexes at once.
  std::scoped_lock<std::recursive_mutex, std::recursive_mutex> lock(
      m_modules_mutex, other.m_modules_mutex);
  m_modules.swap(other.m_modules);
}
