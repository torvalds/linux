//===-- SymbolLocatorDefault.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolLocatorDefault.h"

#include <cstring>
#include <optional>

#include "Plugins/ObjectFile/wasm/ObjectFileWasm.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Progress.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "lldb/Utility/UUID.h"

#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ThreadPool.h"

#if defined(__FreeBSD__)
#include <sys/sysctl.h>
#endif

// From MacOSX system header "mach/machine.h"
typedef int cpu_type_t;
typedef int cpu_subtype_t;

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(SymbolLocatorDefault)

SymbolLocatorDefault::SymbolLocatorDefault() : SymbolLocator() {}

void SymbolLocatorDefault::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), GetPluginDescriptionStatic(), CreateInstance,
      LocateExecutableObjectFile, LocateExecutableSymbolFile,
      DownloadObjectAndSymbolFile);
}

void SymbolLocatorDefault::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolLocatorDefault::GetPluginDescriptionStatic() {
  return "Default symbol locator.";
}

SymbolLocator *SymbolLocatorDefault::CreateInstance() {
  return new SymbolLocatorDefault();
}

std::optional<ModuleSpec> SymbolLocatorDefault::LocateExecutableObjectFile(
    const ModuleSpec &module_spec) {
  const FileSpec &exec_fspec = module_spec.GetFileSpec();
  const ArchSpec *arch = module_spec.GetArchitecturePtr();
  const UUID *uuid = module_spec.GetUUIDPtr();
  LLDB_SCOPED_TIMERF(
      "LocateExecutableObjectFile (file = %s, arch = %s, uuid = %p)",
      exec_fspec ? exec_fspec.GetFilename().AsCString("<NULL>") : "<NULL>",
      arch ? arch->GetArchitectureName() : "<NULL>", (const void *)uuid);

  ModuleSpecList module_specs;
  ModuleSpec matched_module_spec;
  if (exec_fspec &&
      ObjectFile::GetModuleSpecifications(exec_fspec, 0, 0, module_specs) &&
      module_specs.FindMatchingModuleSpec(module_spec, matched_module_spec)) {
    ModuleSpec result;
    result.GetFileSpec() = exec_fspec;
    return result;
  }

  return {};
}

// Keep "symbols.enable-external-lookup" description in sync with this function.
std::optional<FileSpec> SymbolLocatorDefault::LocateExecutableSymbolFile(
    const ModuleSpec &module_spec, const FileSpecList &default_search_paths) {

  FileSpec symbol_file_spec = module_spec.GetSymbolFileSpec();
  if (symbol_file_spec.IsAbsolute() &&
      FileSystem::Instance().Exists(symbol_file_spec))
    return symbol_file_spec;

  Progress progress(
      "Locating external symbol file",
      module_spec.GetFileSpec().GetFilename().AsCString("<Unknown>"));

  FileSpecList debug_file_search_paths = default_search_paths;

  // Add module directory.
  FileSpec module_file_spec = module_spec.GetFileSpec();
  // We keep the unresolved pathname if it fails.
  FileSystem::Instance().ResolveSymbolicLink(module_file_spec,
                                             module_file_spec);

  ConstString file_dir = module_file_spec.GetDirectory();
  {
    FileSpec file_spec(file_dir.AsCString("."));
    FileSystem::Instance().Resolve(file_spec);
    debug_file_search_paths.AppendIfUnique(file_spec);
  }

  if (ModuleList::GetGlobalModuleListProperties().GetEnableExternalLookup()) {

    // Add current working directory.
    {
      FileSpec file_spec(".");
      FileSystem::Instance().Resolve(file_spec);
      debug_file_search_paths.AppendIfUnique(file_spec);
    }

#ifndef _WIN32
#if defined(__NetBSD__)
    // Add /usr/libdata/debug directory.
    {
      FileSpec file_spec("/usr/libdata/debug");
      FileSystem::Instance().Resolve(file_spec);
      debug_file_search_paths.AppendIfUnique(file_spec);
    }
#else
    // Add /usr/lib/debug directory.
    {
      FileSpec file_spec("/usr/lib/debug");
      FileSystem::Instance().Resolve(file_spec);
      debug_file_search_paths.AppendIfUnique(file_spec);
    }
#if defined(__FreeBSD__)
    // Add $LOCALBASE/lib/debug directory, where LOCALBASE is
    // usually /usr/local, but may be adjusted by the end user.
    {
      int mib[2];
      char buf[PATH_MAX];
      size_t len = PATH_MAX;

      mib[0] = CTL_USER;
      mib[1] = USER_LOCALBASE;
      if (::sysctl(mib, 2, buf, &len, NULL, 0) == 0) {
        FileSpec file_spec("/lib/debug");
        file_spec.PrependPathComponent(llvm::StringRef(buf));
        FileSystem::Instance().Resolve(file_spec);
        debug_file_search_paths.AppendIfUnique(file_spec);
      }
    }
#endif // __FreeBSD__
#endif
#endif // _WIN32
  }

  std::string uuid_str;
  const UUID &module_uuid = module_spec.GetUUID();
  if (module_uuid.IsValid()) {
    // Some debug files are stored in the .build-id directory like this:
    //   /usr/lib/debug/.build-id/ff/e7fe727889ad82bb153de2ad065b2189693315.debug
    uuid_str = module_uuid.GetAsString("");
    std::transform(uuid_str.begin(), uuid_str.end(), uuid_str.begin(),
                   ::tolower);
    uuid_str.insert(2, 1, '/');
    uuid_str = uuid_str + ".debug";
  }

  size_t num_directories = debug_file_search_paths.GetSize();
  for (size_t idx = 0; idx < num_directories; ++idx) {
    FileSpec dirspec = debug_file_search_paths.GetFileSpecAtIndex(idx);
    FileSystem::Instance().Resolve(dirspec);
    if (!FileSystem::Instance().IsDirectory(dirspec))
      continue;

    std::vector<std::string> files;
    std::string dirname = dirspec.GetPath();

    if (!uuid_str.empty())
      files.push_back(dirname + "/.build-id/" + uuid_str);
    if (symbol_file_spec.GetFilename()) {
      files.push_back(dirname + "/" +
                      symbol_file_spec.GetFilename().GetCString());
      files.push_back(dirname + "/.debug/" +
                      symbol_file_spec.GetFilename().GetCString());

      // Some debug files may stored in the module directory like this:
      //   /usr/lib/debug/usr/lib/library.so.debug
      if (!file_dir.IsEmpty())
        files.push_back(dirname + file_dir.AsCString() + "/" +
                        symbol_file_spec.GetFilename().GetCString());
    }

    const uint32_t num_files = files.size();
    for (size_t idx_file = 0; idx_file < num_files; ++idx_file) {
      const std::string &filename = files[idx_file];
      FileSpec file_spec(filename);
      FileSystem::Instance().Resolve(file_spec);

      if (llvm::sys::fs::equivalent(file_spec.GetPath(),
                                    module_file_spec.GetPath()))
        continue;

      if (FileSystem::Instance().Exists(file_spec)) {
        lldb_private::ModuleSpecList specs;
        const size_t num_specs =
            ObjectFile::GetModuleSpecifications(file_spec, 0, 0, specs);
        ModuleSpec mspec;
        bool valid_mspec = false;
        if (num_specs == 2) {
          // Special case to handle both i386 and i686 from ObjectFilePECOFF
          ModuleSpec mspec2;
          if (specs.GetModuleSpecAtIndex(0, mspec) &&
              specs.GetModuleSpecAtIndex(1, mspec2) &&
              mspec.GetArchitecture().GetTriple().isCompatibleWith(
                  mspec2.GetArchitecture().GetTriple())) {
            valid_mspec = true;
          }
        }
        if (!valid_mspec) {
          assert(num_specs <= 1 &&
                 "Symbol Vendor supports only a single architecture");
          if (num_specs == 1) {
            if (specs.GetModuleSpecAtIndex(0, mspec)) {
              valid_mspec = true;
            }
          }
        }
        if (valid_mspec) {
          // Skip the uuids check if module_uuid is invalid. For example,
          // this happens for *.dwp files since at the moment llvm-dwp
          // doesn't output build ids, nor does binutils dwp.
          if (!module_uuid.IsValid() || module_uuid == mspec.GetUUID())
            return file_spec;
        }
      }
    }
  }

  return {};
}

bool SymbolLocatorDefault::DownloadObjectAndSymbolFile(ModuleSpec &module_spec,
                                                       Status &error,
                                                       bool force_lookup,
                                                       bool copy_executable) {
  return false;
}
