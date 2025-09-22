//===-- SymbolLocatorDebugSymbols.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolLocatorDebugSymbols.h"

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
#include "lldb/Host/HostInfo.h"
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

#include "Host/macosx/cfcpp/CFCBundle.h"
#include "Host/macosx/cfcpp/CFCData.h"
#include "Host/macosx/cfcpp/CFCReleaser.h"
#include "Host/macosx/cfcpp/CFCString.h"

#include "mach/machine.h"

#include <CoreFoundation/CoreFoundation.h>

#include <cstring>
#include <dirent.h>
#include <dlfcn.h>
#include <optional>
#include <pwd.h>

using namespace lldb;
using namespace lldb_private;

static CFURLRef (*g_dlsym_DBGCopyFullDSYMURLForUUID)(
    CFUUIDRef uuid, CFURLRef exec_url) = nullptr;
static CFDictionaryRef (*g_dlsym_DBGCopyDSYMPropertyLists)(CFURLRef dsym_url) =
    nullptr;

LLDB_PLUGIN_DEFINE(SymbolLocatorDebugSymbols)

SymbolLocatorDebugSymbols::SymbolLocatorDebugSymbols() : SymbolLocator() {}

void SymbolLocatorDebugSymbols::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), GetPluginDescriptionStatic(), CreateInstance,
      LocateExecutableObjectFile, LocateExecutableSymbolFile,
      DownloadObjectAndSymbolFile, FindSymbolFileInBundle);
}

void SymbolLocatorDebugSymbols::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolLocatorDebugSymbols::GetPluginDescriptionStatic() {
  return "DebugSymbols symbol locator.";
}

SymbolLocator *SymbolLocatorDebugSymbols::CreateInstance() {
  return new SymbolLocatorDebugSymbols();
}

std::optional<ModuleSpec> SymbolLocatorDebugSymbols::LocateExecutableObjectFile(
    const ModuleSpec &module_spec) {
  Log *log = GetLog(LLDBLog::Host);
  if (!ModuleList::GetGlobalModuleListProperties().GetEnableExternalLookup()) {
    LLDB_LOGF(log, "Spotlight lookup for .dSYM bundles is disabled.");
    return {};
  }
  ModuleSpec return_module_spec;
  return_module_spec = module_spec;
  return_module_spec.GetFileSpec().Clear();
  return_module_spec.GetSymbolFileSpec().Clear();

  const UUID *uuid = module_spec.GetUUIDPtr();
  const ArchSpec *arch = module_spec.GetArchitecturePtr();

  int items_found = 0;

  if (g_dlsym_DBGCopyFullDSYMURLForUUID == nullptr ||
      g_dlsym_DBGCopyDSYMPropertyLists == nullptr) {
    void *handle = dlopen(
        "/System/Library/PrivateFrameworks/DebugSymbols.framework/DebugSymbols",
        RTLD_LAZY | RTLD_LOCAL);
    if (handle) {
      g_dlsym_DBGCopyFullDSYMURLForUUID =
          (CFURLRef(*)(CFUUIDRef, CFURLRef))dlsym(handle,
                                                  "DBGCopyFullDSYMURLForUUID");
      g_dlsym_DBGCopyDSYMPropertyLists = (CFDictionaryRef(*)(CFURLRef))dlsym(
          handle, "DBGCopyDSYMPropertyLists");
    }
  }

  if (g_dlsym_DBGCopyFullDSYMURLForUUID == nullptr ||
      g_dlsym_DBGCopyDSYMPropertyLists == nullptr) {
    return {};
  }

  if (uuid && uuid->IsValid()) {
    // Try and locate the dSYM file using DebugSymbols first
    llvm::ArrayRef<uint8_t> module_uuid = uuid->GetBytes();
    if (module_uuid.size() == 16) {
      CFCReleaser<CFUUIDRef> module_uuid_ref(::CFUUIDCreateWithBytes(
          NULL, module_uuid[0], module_uuid[1], module_uuid[2], module_uuid[3],
          module_uuid[4], module_uuid[5], module_uuid[6], module_uuid[7],
          module_uuid[8], module_uuid[9], module_uuid[10], module_uuid[11],
          module_uuid[12], module_uuid[13], module_uuid[14], module_uuid[15]));

      if (module_uuid_ref.get()) {
        CFCReleaser<CFURLRef> exec_url;
        const FileSpec *exec_fspec = module_spec.GetFileSpecPtr();
        if (exec_fspec) {
          char exec_cf_path[PATH_MAX];
          if (exec_fspec->GetPath(exec_cf_path, sizeof(exec_cf_path)))
            exec_url.reset(::CFURLCreateFromFileSystemRepresentation(
                NULL, (const UInt8 *)exec_cf_path, strlen(exec_cf_path),
                FALSE));
        }

        CFCReleaser<CFURLRef> dsym_url(g_dlsym_DBGCopyFullDSYMURLForUUID(
            module_uuid_ref.get(), exec_url.get()));
        char path[PATH_MAX];

        if (dsym_url.get()) {
          if (::CFURLGetFileSystemRepresentation(
                  dsym_url.get(), true, (UInt8 *)path, sizeof(path) - 1)) {
            LLDB_LOGF(log,
                      "DebugSymbols framework returned dSYM path of %s for "
                      "UUID %s -- looking for the dSYM",
                      path, uuid->GetAsString().c_str());
            FileSpec dsym_filespec(path);
            if (path[0] == '~')
              FileSystem::Instance().Resolve(dsym_filespec);

            if (FileSystem::Instance().IsDirectory(dsym_filespec)) {
              dsym_filespec = PluginManager::FindSymbolFileInBundle(
                  dsym_filespec, uuid, arch);
              ++items_found;
            } else {
              ++items_found;
            }
            return_module_spec.GetSymbolFileSpec() = dsym_filespec;
          }

          bool success = false;
          if (log) {
            if (::CFURLGetFileSystemRepresentation(
                    dsym_url.get(), true, (UInt8 *)path, sizeof(path) - 1)) {
              LLDB_LOGF(log,
                        "DebugSymbols framework returned dSYM path of %s for "
                        "UUID %s -- looking for an exec file",
                        path, uuid->GetAsString().c_str());
            }
          }

          CFCReleaser<CFDictionaryRef> dict(
              g_dlsym_DBGCopyDSYMPropertyLists(dsym_url.get()));
          CFDictionaryRef uuid_dict = NULL;
          if (dict.get()) {
            CFCString uuid_cfstr(uuid->GetAsString().c_str());
            uuid_dict = static_cast<CFDictionaryRef>(
                ::CFDictionaryGetValue(dict.get(), uuid_cfstr.get()));
          }

          // Check to see if we have the file on the local filesystem.
          if (FileSystem::Instance().Exists(module_spec.GetFileSpec())) {
            ModuleSpec exe_spec;
            exe_spec.GetFileSpec() = module_spec.GetFileSpec();
            exe_spec.GetUUID() = module_spec.GetUUID();
            ModuleSP module_sp;
            module_sp.reset(new Module(exe_spec));
            if (module_sp && module_sp->GetObjectFile() &&
                module_sp->MatchesModuleSpec(exe_spec)) {
              success = true;
              return_module_spec.GetFileSpec() = module_spec.GetFileSpec();
              LLDB_LOGF(log, "using original binary filepath %s for UUID %s",
                        module_spec.GetFileSpec().GetPath().c_str(),
                        uuid->GetAsString().c_str());
              ++items_found;
            }
          }

          // Check if the requested image is in our shared cache.
          if (!success) {
            SharedCacheImageInfo image_info = HostInfo::GetSharedCacheImageInfo(
                module_spec.GetFileSpec().GetPath());

            // If we found it and it has the correct UUID, let's proceed with
            // creating a module from the memory contents.
            if (image_info.uuid && (!module_spec.GetUUID() ||
                                    module_spec.GetUUID() == image_info.uuid)) {
              success = true;
              return_module_spec.GetFileSpec() = module_spec.GetFileSpec();
              LLDB_LOGF(log,
                        "using binary from shared cache for filepath %s for "
                        "UUID %s",
                        module_spec.GetFileSpec().GetPath().c_str(),
                        uuid->GetAsString().c_str());
              ++items_found;
            }
          }

          // Use the DBGSymbolRichExecutable filepath if present
          if (!success && uuid_dict) {
            CFStringRef exec_cf_path =
                static_cast<CFStringRef>(::CFDictionaryGetValue(
                    uuid_dict, CFSTR("DBGSymbolRichExecutable")));
            if (exec_cf_path && ::CFStringGetFileSystemRepresentation(
                                    exec_cf_path, path, sizeof(path))) {
              LLDB_LOGF(log, "plist bundle has exec path of %s for UUID %s",
                        path, uuid->GetAsString().c_str());
              ++items_found;
              FileSpec exec_filespec(path);
              if (path[0] == '~')
                FileSystem::Instance().Resolve(exec_filespec);
              if (FileSystem::Instance().Exists(exec_filespec)) {
                success = true;
                return_module_spec.GetFileSpec() = exec_filespec;
              }
            }
          }

          // Look next to the dSYM for the binary file.
          if (!success) {
            if (::CFURLGetFileSystemRepresentation(
                    dsym_url.get(), true, (UInt8 *)path, sizeof(path) - 1)) {
              char *dsym_extension_pos = ::strstr(path, ".dSYM");
              if (dsym_extension_pos) {
                *dsym_extension_pos = '\0';
                LLDB_LOGF(log,
                          "Looking for executable binary next to dSYM "
                          "bundle with name with name %s",
                          path);
                FileSpec file_spec(path);
                FileSystem::Instance().Resolve(file_spec);
                ModuleSpecList module_specs;
                ModuleSpec matched_module_spec;
                using namespace llvm::sys::fs;
                switch (get_file_type(file_spec.GetPath())) {

                case file_type::directory_file: // Bundle directory?
                {
                  CFCBundle bundle(path);
                  CFCReleaser<CFURLRef> bundle_exe_url(
                      bundle.CopyExecutableURL());
                  if (bundle_exe_url.get()) {
                    if (::CFURLGetFileSystemRepresentation(bundle_exe_url.get(),
                                                           true, (UInt8 *)path,
                                                           sizeof(path) - 1)) {
                      FileSpec bundle_exe_file_spec(path);
                      FileSystem::Instance().Resolve(bundle_exe_file_spec);
                      if (ObjectFile::GetModuleSpecifications(
                              bundle_exe_file_spec, 0, 0, module_specs) &&
                          module_specs.FindMatchingModuleSpec(
                              module_spec, matched_module_spec))

                      {
                        ++items_found;
                        return_module_spec.GetFileSpec() = bundle_exe_file_spec;
                        LLDB_LOGF(log,
                                  "Executable binary %s next to dSYM is "
                                  "compatible; using",
                                  path);
                      }
                    }
                  }
                } break;

                case file_type::fifo_file:      // Forget pipes
                case file_type::socket_file:    // We can't process socket files
                case file_type::file_not_found: // File doesn't exist...
                case file_type::status_error:
                  break;

                case file_type::type_unknown:
                case file_type::regular_file:
                case file_type::symlink_file:
                case file_type::block_file:
                case file_type::character_file:
                  if (ObjectFile::GetModuleSpecifications(file_spec, 0, 0,
                                                          module_specs) &&
                      module_specs.FindMatchingModuleSpec(module_spec,
                                                          matched_module_spec))

                  {
                    ++items_found;
                    return_module_spec.GetFileSpec() = file_spec;
                    LLDB_LOGF(log,
                              "Executable binary %s next to dSYM is "
                              "compatible; using",
                              path);
                  }
                  break;
                }
              }
            }
          }
        }
      }
    }
  }

  if (items_found)
    return return_module_spec;

  return {};
}

std::optional<FileSpec> SymbolLocatorDebugSymbols::FindSymbolFileInBundle(
    const FileSpec &dsym_bundle_fspec, const UUID *uuid, const ArchSpec *arch) {
  std::string dsym_bundle_path = dsym_bundle_fspec.GetPath();
  llvm::SmallString<128> buffer(dsym_bundle_path);
  llvm::sys::path::append(buffer, "Contents", "Resources", "DWARF");

  std::error_code EC;
  llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs =
      FileSystem::Instance().GetVirtualFileSystem();
  llvm::vfs::recursive_directory_iterator Iter(*vfs, buffer.str(), EC);
  llvm::vfs::recursive_directory_iterator End;
  for (; Iter != End && !EC; Iter.increment(EC)) {
    llvm::ErrorOr<llvm::vfs::Status> Status = vfs->status(Iter->path());
    if (Status->isDirectory())
      continue;

    FileSpec dsym_fspec(Iter->path());
    ModuleSpecList module_specs;
    if (ObjectFile::GetModuleSpecifications(dsym_fspec, 0, 0, module_specs)) {
      ModuleSpec spec;
      for (size_t i = 0; i < module_specs.GetSize(); ++i) {
        bool got_spec = module_specs.GetModuleSpecAtIndex(i, spec);
        assert(got_spec); // The call has side-effects so can't be inlined.
        UNUSED_IF_ASSERT_DISABLED(got_spec);
        if ((uuid == nullptr ||
             (spec.GetUUIDPtr() && spec.GetUUID() == *uuid)) &&
            (arch == nullptr ||
             (spec.GetArchitecturePtr() &&
              spec.GetArchitecture().IsCompatibleMatch(*arch)))) {
          return dsym_fspec;
        }
      }
    }
  }

  return {};
}

static bool FileAtPathContainsArchAndUUID(const FileSpec &file_fspec,
                                          const ArchSpec *arch,
                                          const lldb_private::UUID *uuid) {
  ModuleSpecList module_specs;
  if (ObjectFile::GetModuleSpecifications(file_fspec, 0, 0, module_specs)) {
    ModuleSpec spec;
    for (size_t i = 0; i < module_specs.GetSize(); ++i) {
      bool got_spec = module_specs.GetModuleSpecAtIndex(i, spec);
      UNUSED_IF_ASSERT_DISABLED(got_spec);
      assert(got_spec);
      if ((uuid == nullptr || (spec.GetUUIDPtr() && spec.GetUUID() == *uuid)) &&
          (arch == nullptr ||
           (spec.GetArchitecturePtr() &&
            spec.GetArchitecture().IsCompatibleMatch(*arch)))) {
        return true;
      }
    }
  }
  return false;
}

// Given a binary exec_fspec, and a ModuleSpec with an architecture/uuid,
// return true if there is a matching dSYM bundle next to the exec_fspec,
// and return that value in dsym_fspec.
// If there is a .dSYM.yaa compressed archive next to the exec_fspec,
// call through PluginManager::DownloadObjectAndSymbolFile to download the
// expanded/uncompressed dSYM and return that filepath in dsym_fspec.
static bool LookForDsymNextToExecutablePath(const ModuleSpec &mod_spec,
                                            const FileSpec &exec_fspec,
                                            FileSpec &dsym_fspec) {
  ConstString filename = exec_fspec.GetFilename();
  FileSpec dsym_directory = exec_fspec;
  dsym_directory.RemoveLastPathComponent();

  std::string dsym_filename = filename.AsCString();
  dsym_filename += ".dSYM";
  dsym_directory.AppendPathComponent(dsym_filename);
  dsym_directory.AppendPathComponent("Contents");
  dsym_directory.AppendPathComponent("Resources");
  dsym_directory.AppendPathComponent("DWARF");

  if (FileSystem::Instance().Exists(dsym_directory)) {

    // See if the binary name exists in the dSYM DWARF
    // subdir.
    dsym_fspec = dsym_directory;
    dsym_fspec.AppendPathComponent(filename.AsCString());
    if (FileSystem::Instance().Exists(dsym_fspec) &&
        FileAtPathContainsArchAndUUID(dsym_fspec, mod_spec.GetArchitecturePtr(),
                                      mod_spec.GetUUIDPtr())) {
      return true;
    }

    // See if we have "../CF.framework" - so we'll look for
    // CF.framework.dSYM/Contents/Resources/DWARF/CF
    // We need to drop the last suffix after '.' to match
    // 'CF' in the DWARF subdir.
    std::string binary_name(filename.AsCString());
    auto last_dot = binary_name.find_last_of('.');
    if (last_dot != std::string::npos) {
      binary_name.erase(last_dot);
      dsym_fspec = dsym_directory;
      dsym_fspec.AppendPathComponent(binary_name);
      if (FileSystem::Instance().Exists(dsym_fspec) &&
          FileAtPathContainsArchAndUUID(dsym_fspec,
                                        mod_spec.GetArchitecturePtr(),
                                        mod_spec.GetUUIDPtr())) {
        return true;
      }
    }
  }

  // See if we have a .dSYM.yaa next to this executable path.
  FileSpec dsym_yaa_fspec = exec_fspec;
  dsym_yaa_fspec.RemoveLastPathComponent();
  std::string dsym_yaa_filename = filename.AsCString();
  dsym_yaa_filename += ".dSYM.yaa";
  dsym_yaa_fspec.AppendPathComponent(dsym_yaa_filename);

  if (FileSystem::Instance().Exists(dsym_yaa_fspec)) {
    ModuleSpec mutable_mod_spec = mod_spec;
    Status error;
    if (PluginManager::DownloadObjectAndSymbolFile(mutable_mod_spec, error,
                                                   true) &&
        FileSystem::Instance().Exists(mutable_mod_spec.GetSymbolFileSpec())) {
      dsym_fspec = mutable_mod_spec.GetSymbolFileSpec();
      return true;
    }
  }

  return false;
}

// Given a ModuleSpec with a FileSpec and optionally uuid/architecture
// filled in, look for a .dSYM bundle next to that binary.  Returns true
// if a .dSYM bundle is found, and that path is returned in the dsym_fspec
// FileSpec.
//
// This routine looks a few directory layers above the given exec_path -
// exec_path might be /System/Library/Frameworks/CF.framework/CF and the
// dSYM might be /System/Library/Frameworks/CF.framework.dSYM.
//
// If there is a .dSYM.yaa compressed archive found next to the binary,
// we'll call DownloadObjectAndSymbolFile to expand it into a plain .dSYM
static bool LocateDSYMInVincinityOfExecutable(const ModuleSpec &module_spec,
                                              FileSpec &dsym_fspec) {
  Log *log = GetLog(LLDBLog::Host);
  const FileSpec &exec_fspec = module_spec.GetFileSpec();
  if (exec_fspec) {
    if (::LookForDsymNextToExecutablePath(module_spec, exec_fspec,
                                          dsym_fspec)) {
      if (log) {
        LLDB_LOGF(log, "dSYM with matching UUID & arch found at %s",
                  dsym_fspec.GetPath().c_str());
      }
      return true;
    } else {
      FileSpec parent_dirs = exec_fspec;

      // Remove the binary name from the FileSpec
      parent_dirs.RemoveLastPathComponent();

      // Add a ".dSYM" name to each directory component of the path,
      // stripping off components.  e.g. we may have a binary like
      // /S/L/F/Foundation.framework/Versions/A/Foundation and
      // /S/L/F/Foundation.framework.dSYM
      //
      // so we'll need to start with
      // /S/L/F/Foundation.framework/Versions/A, add the .dSYM part to the
      // "A", and if that doesn't exist, strip off the "A" and try it again
      // with "Versions", etc., until we find a dSYM bundle or we've
      // stripped off enough path components that there's no need to
      // continue.

      for (int i = 0; i < 4; i++) {
        // Does this part of the path have a "." character - could it be a
        // bundle's top level directory?
        const char *fn = parent_dirs.GetFilename().AsCString();
        if (fn == nullptr)
          break;
        if (::strchr(fn, '.') != nullptr) {
          if (::LookForDsymNextToExecutablePath(module_spec, parent_dirs,
                                                dsym_fspec)) {
            if (log) {
              LLDB_LOGF(log, "dSYM with matching UUID & arch found at %s",
                        dsym_fspec.GetPath().c_str());
            }
            return true;
          }
        }
        parent_dirs.RemoveLastPathComponent();
      }
    }
  }
  dsym_fspec.Clear();
  return false;
}

static int LocateMacOSXFilesUsingDebugSymbols(const ModuleSpec &module_spec,
                                              ModuleSpec &return_module_spec) {
  Log *log = GetLog(LLDBLog::Host);
  if (!ModuleList::GetGlobalModuleListProperties().GetEnableExternalLookup()) {
    LLDB_LOGF(log, "Spotlight lookup for .dSYM bundles is disabled.");
    return 0;
  }

  return_module_spec = module_spec;
  return_module_spec.GetFileSpec().Clear();
  return_module_spec.GetSymbolFileSpec().Clear();

  const UUID *uuid = module_spec.GetUUIDPtr();
  const ArchSpec *arch = module_spec.GetArchitecturePtr();

  int items_found = 0;

  if (g_dlsym_DBGCopyFullDSYMURLForUUID == nullptr ||
      g_dlsym_DBGCopyDSYMPropertyLists == nullptr) {
    void *handle = dlopen(
        "/System/Library/PrivateFrameworks/DebugSymbols.framework/DebugSymbols",
        RTLD_LAZY | RTLD_LOCAL);
    if (handle) {
      g_dlsym_DBGCopyFullDSYMURLForUUID =
          (CFURLRef(*)(CFUUIDRef, CFURLRef))dlsym(handle,
                                                  "DBGCopyFullDSYMURLForUUID");
      g_dlsym_DBGCopyDSYMPropertyLists = (CFDictionaryRef(*)(CFURLRef))dlsym(
          handle, "DBGCopyDSYMPropertyLists");
    }
  }

  if (g_dlsym_DBGCopyFullDSYMURLForUUID == nullptr ||
      g_dlsym_DBGCopyDSYMPropertyLists == nullptr) {
    return items_found;
  }

  if (uuid && uuid->IsValid()) {
    // Try and locate the dSYM file using DebugSymbols first
    llvm::ArrayRef<uint8_t> module_uuid = uuid->GetBytes();
    if (module_uuid.size() == 16) {
      CFCReleaser<CFUUIDRef> module_uuid_ref(::CFUUIDCreateWithBytes(
          NULL, module_uuid[0], module_uuid[1], module_uuid[2], module_uuid[3],
          module_uuid[4], module_uuid[5], module_uuid[6], module_uuid[7],
          module_uuid[8], module_uuid[9], module_uuid[10], module_uuid[11],
          module_uuid[12], module_uuid[13], module_uuid[14], module_uuid[15]));

      if (module_uuid_ref.get()) {
        CFCReleaser<CFURLRef> exec_url;
        const FileSpec *exec_fspec = module_spec.GetFileSpecPtr();
        if (exec_fspec) {
          char exec_cf_path[PATH_MAX];
          if (exec_fspec->GetPath(exec_cf_path, sizeof(exec_cf_path)))
            exec_url.reset(::CFURLCreateFromFileSystemRepresentation(
                NULL, (const UInt8 *)exec_cf_path, strlen(exec_cf_path),
                FALSE));
        }

        CFCReleaser<CFURLRef> dsym_url(g_dlsym_DBGCopyFullDSYMURLForUUID(
            module_uuid_ref.get(), exec_url.get()));
        char path[PATH_MAX];

        if (dsym_url.get()) {
          if (::CFURLGetFileSystemRepresentation(
                  dsym_url.get(), true, (UInt8 *)path, sizeof(path) - 1)) {
            LLDB_LOGF(log,
                      "DebugSymbols framework returned dSYM path of %s for "
                      "UUID %s -- looking for the dSYM",
                      path, uuid->GetAsString().c_str());
            FileSpec dsym_filespec(path);
            if (path[0] == '~')
              FileSystem::Instance().Resolve(dsym_filespec);

            if (FileSystem::Instance().IsDirectory(dsym_filespec)) {
              dsym_filespec = PluginManager::FindSymbolFileInBundle(
                  dsym_filespec, uuid, arch);
              ++items_found;
            } else {
              ++items_found;
            }
            return_module_spec.GetSymbolFileSpec() = dsym_filespec;
          }

          bool success = false;
          if (log) {
            if (::CFURLGetFileSystemRepresentation(
                    dsym_url.get(), true, (UInt8 *)path, sizeof(path) - 1)) {
              LLDB_LOGF(log,
                        "DebugSymbols framework returned dSYM path of %s for "
                        "UUID %s -- looking for an exec file",
                        path, uuid->GetAsString().c_str());
            }
          }

          CFCReleaser<CFDictionaryRef> dict(
              g_dlsym_DBGCopyDSYMPropertyLists(dsym_url.get()));
          CFDictionaryRef uuid_dict = NULL;
          if (dict.get()) {
            CFCString uuid_cfstr(uuid->GetAsString().c_str());
            uuid_dict = static_cast<CFDictionaryRef>(
                ::CFDictionaryGetValue(dict.get(), uuid_cfstr.get()));
          }

          // Check to see if we have the file on the local filesystem.
          if (FileSystem::Instance().Exists(module_spec.GetFileSpec())) {
            ModuleSpec exe_spec;
            exe_spec.GetFileSpec() = module_spec.GetFileSpec();
            exe_spec.GetUUID() = module_spec.GetUUID();
            ModuleSP module_sp;
            module_sp.reset(new Module(exe_spec));
            if (module_sp && module_sp->GetObjectFile() &&
                module_sp->MatchesModuleSpec(exe_spec)) {
              success = true;
              return_module_spec.GetFileSpec() = module_spec.GetFileSpec();
              LLDB_LOGF(log, "using original binary filepath %s for UUID %s",
                        module_spec.GetFileSpec().GetPath().c_str(),
                        uuid->GetAsString().c_str());
              ++items_found;
            }
          }

          // Check if the requested image is in our shared cache.
          if (!success) {
            SharedCacheImageInfo image_info = HostInfo::GetSharedCacheImageInfo(
                module_spec.GetFileSpec().GetPath());

            // If we found it and it has the correct UUID, let's proceed with
            // creating a module from the memory contents.
            if (image_info.uuid && (!module_spec.GetUUID() ||
                                    module_spec.GetUUID() == image_info.uuid)) {
              success = true;
              return_module_spec.GetFileSpec() = module_spec.GetFileSpec();
              LLDB_LOGF(log,
                        "using binary from shared cache for filepath %s for "
                        "UUID %s",
                        module_spec.GetFileSpec().GetPath().c_str(),
                        uuid->GetAsString().c_str());
              ++items_found;
            }
          }

          // Use the DBGSymbolRichExecutable filepath if present
          if (!success && uuid_dict) {
            CFStringRef exec_cf_path =
                static_cast<CFStringRef>(::CFDictionaryGetValue(
                    uuid_dict, CFSTR("DBGSymbolRichExecutable")));
            if (exec_cf_path && ::CFStringGetFileSystemRepresentation(
                                    exec_cf_path, path, sizeof(path))) {
              LLDB_LOGF(log, "plist bundle has exec path of %s for UUID %s",
                        path, uuid->GetAsString().c_str());
              ++items_found;
              FileSpec exec_filespec(path);
              if (path[0] == '~')
                FileSystem::Instance().Resolve(exec_filespec);
              if (FileSystem::Instance().Exists(exec_filespec)) {
                success = true;
                return_module_spec.GetFileSpec() = exec_filespec;
              }
            }
          }

          // Look next to the dSYM for the binary file.
          if (!success) {
            if (::CFURLGetFileSystemRepresentation(
                    dsym_url.get(), true, (UInt8 *)path, sizeof(path) - 1)) {
              char *dsym_extension_pos = ::strstr(path, ".dSYM");
              if (dsym_extension_pos) {
                *dsym_extension_pos = '\0';
                LLDB_LOGF(log,
                          "Looking for executable binary next to dSYM "
                          "bundle with name with name %s",
                          path);
                FileSpec file_spec(path);
                FileSystem::Instance().Resolve(file_spec);
                ModuleSpecList module_specs;
                ModuleSpec matched_module_spec;
                using namespace llvm::sys::fs;
                switch (get_file_type(file_spec.GetPath())) {

                case file_type::directory_file: // Bundle directory?
                {
                  CFCBundle bundle(path);
                  CFCReleaser<CFURLRef> bundle_exe_url(
                      bundle.CopyExecutableURL());
                  if (bundle_exe_url.get()) {
                    if (::CFURLGetFileSystemRepresentation(bundle_exe_url.get(),
                                                           true, (UInt8 *)path,
                                                           sizeof(path) - 1)) {
                      FileSpec bundle_exe_file_spec(path);
                      FileSystem::Instance().Resolve(bundle_exe_file_spec);
                      if (ObjectFile::GetModuleSpecifications(
                              bundle_exe_file_spec, 0, 0, module_specs) &&
                          module_specs.FindMatchingModuleSpec(
                              module_spec, matched_module_spec))

                      {
                        ++items_found;
                        return_module_spec.GetFileSpec() = bundle_exe_file_spec;
                        LLDB_LOGF(log,
                                  "Executable binary %s next to dSYM is "
                                  "compatible; using",
                                  path);
                      }
                    }
                  }
                } break;

                case file_type::fifo_file:      // Forget pipes
                case file_type::socket_file:    // We can't process socket files
                case file_type::file_not_found: // File doesn't exist...
                case file_type::status_error:
                  break;

                case file_type::type_unknown:
                case file_type::regular_file:
                case file_type::symlink_file:
                case file_type::block_file:
                case file_type::character_file:
                  if (ObjectFile::GetModuleSpecifications(file_spec, 0, 0,
                                                          module_specs) &&
                      module_specs.FindMatchingModuleSpec(module_spec,
                                                          matched_module_spec))

                  {
                    ++items_found;
                    return_module_spec.GetFileSpec() = file_spec;
                    LLDB_LOGF(log,
                              "Executable binary %s next to dSYM is "
                              "compatible; using",
                              path);
                  }
                  break;
                }
              }
            }
          }
        }
      }
    }
  }

  return items_found;
}

std::optional<FileSpec> SymbolLocatorDebugSymbols::LocateExecutableSymbolFile(
    const ModuleSpec &module_spec, const FileSpecList &default_search_paths) {
  const FileSpec *exec_fspec = module_spec.GetFileSpecPtr();
  const ArchSpec *arch = module_spec.GetArchitecturePtr();
  const UUID *uuid = module_spec.GetUUIDPtr();

  LLDB_SCOPED_TIMERF(
      "LocateExecutableSymbolFileDsym (file = %s, arch = %s, uuid = %p)",
      exec_fspec ? exec_fspec->GetFilename().AsCString("<NULL>") : "<NULL>",
      arch ? arch->GetArchitectureName() : "<NULL>", (const void *)uuid);

  Progress progress(
      "Locating external symbol file",
      module_spec.GetFileSpec().GetFilename().AsCString("<Unknown>"));

  FileSpec symbol_fspec;
  ModuleSpec dsym_module_spec;
  // First try and find the dSYM in the same directory as the executable or in
  // an appropriate parent directory
  if (!LocateDSYMInVincinityOfExecutable(module_spec, symbol_fspec)) {
    // We failed to easily find the dSYM above, so use DebugSymbols
    LocateMacOSXFilesUsingDebugSymbols(module_spec, dsym_module_spec);
  } else {
    dsym_module_spec.GetSymbolFileSpec() = symbol_fspec;
  }

  return dsym_module_spec.GetSymbolFileSpec();
}

static bool GetModuleSpecInfoFromUUIDDictionary(CFDictionaryRef uuid_dict,
                                                ModuleSpec &module_spec,
                                                Status &error,
                                                const std::string &command) {
  Log *log = GetLog(LLDBLog::Host);
  bool success = false;
  if (uuid_dict != NULL && CFGetTypeID(uuid_dict) == CFDictionaryGetTypeID()) {
    std::string str;
    CFStringRef cf_str;
    CFDictionaryRef cf_dict;

    cf_str = (CFStringRef)CFDictionaryGetValue((CFDictionaryRef)uuid_dict,
                                               CFSTR("DBGError"));
    if (cf_str && CFGetTypeID(cf_str) == CFStringGetTypeID()) {
      if (CFCString::FileSystemRepresentation(cf_str, str)) {
        std::string errorstr = command;
        errorstr += ":\n";
        errorstr += str;
        error.SetErrorString(errorstr);
      }
    }

    cf_str = (CFStringRef)CFDictionaryGetValue(
        (CFDictionaryRef)uuid_dict, CFSTR("DBGSymbolRichExecutable"));
    if (cf_str && CFGetTypeID(cf_str) == CFStringGetTypeID()) {
      if (CFCString::FileSystemRepresentation(cf_str, str)) {
        module_spec.GetFileSpec().SetFile(str.c_str(), FileSpec::Style::native);
        FileSystem::Instance().Resolve(module_spec.GetFileSpec());
        LLDB_LOGF(log,
                  "From dsymForUUID plist: Symbol rich executable is at '%s'",
                  str.c_str());
      }
    }

    cf_str = (CFStringRef)CFDictionaryGetValue((CFDictionaryRef)uuid_dict,
                                               CFSTR("DBGDSYMPath"));
    if (cf_str && CFGetTypeID(cf_str) == CFStringGetTypeID()) {
      if (CFCString::FileSystemRepresentation(cf_str, str)) {
        module_spec.GetSymbolFileSpec().SetFile(str.c_str(),
                                                FileSpec::Style::native);
        FileSystem::Instance().Resolve(module_spec.GetFileSpec());
        success = true;
        LLDB_LOGF(log, "From dsymForUUID plist: dSYM is at '%s'", str.c_str());
      }
    }

    std::string DBGBuildSourcePath;
    std::string DBGSourcePath;

    // If DBGVersion 1 or DBGVersion missing, ignore DBGSourcePathRemapping.
    // If DBGVersion 2, strip last two components of path remappings from
    //                  entries to fix an issue with a specific set of
    //                  DBGSourcePathRemapping entries that lldb worked
    //                  with.
    // If DBGVersion 3, trust & use the source path remappings as-is.
    //
    cf_dict = (CFDictionaryRef)CFDictionaryGetValue(
        (CFDictionaryRef)uuid_dict, CFSTR("DBGSourcePathRemapping"));
    if (cf_dict && CFGetTypeID(cf_dict) == CFDictionaryGetTypeID()) {
      // If we see DBGVersion with a value of 2 or higher, this is a new style
      // DBGSourcePathRemapping dictionary
      bool new_style_source_remapping_dictionary = false;
      bool do_truncate_remapping_names = false;
      std::string original_DBGSourcePath_value = DBGSourcePath;
      cf_str = (CFStringRef)CFDictionaryGetValue((CFDictionaryRef)uuid_dict,
                                                 CFSTR("DBGVersion"));
      if (cf_str && CFGetTypeID(cf_str) == CFStringGetTypeID()) {
        std::string version;
        CFCString::FileSystemRepresentation(cf_str, version);
        if (!version.empty() && isdigit(version[0])) {
          int version_number = atoi(version.c_str());
          if (version_number > 1) {
            new_style_source_remapping_dictionary = true;
          }
          if (version_number == 2) {
            do_truncate_remapping_names = true;
          }
        }
      }

      CFIndex kv_pair_count = CFDictionaryGetCount((CFDictionaryRef)uuid_dict);
      if (kv_pair_count > 0) {
        CFStringRef *keys =
            (CFStringRef *)malloc(kv_pair_count * sizeof(CFStringRef));
        CFStringRef *values =
            (CFStringRef *)malloc(kv_pair_count * sizeof(CFStringRef));
        if (keys != nullptr && values != nullptr) {
          CFDictionaryGetKeysAndValues((CFDictionaryRef)uuid_dict,
                                       (const void **)keys,
                                       (const void **)values);
        }
        for (CFIndex i = 0; i < kv_pair_count; i++) {
          DBGBuildSourcePath.clear();
          DBGSourcePath.clear();
          if (keys[i] && CFGetTypeID(keys[i]) == CFStringGetTypeID()) {
            CFCString::FileSystemRepresentation(keys[i], DBGBuildSourcePath);
          }
          if (values[i] && CFGetTypeID(values[i]) == CFStringGetTypeID()) {
            CFCString::FileSystemRepresentation(values[i], DBGSourcePath);
          }
          if (!DBGBuildSourcePath.empty() && !DBGSourcePath.empty()) {
            // In the "old style" DBGSourcePathRemapping dictionary, the
            // DBGSourcePath values (the "values" half of key-value path pairs)
            // were wrong.  Ignore them and use the universal DBGSourcePath
            // string from earlier.
            if (new_style_source_remapping_dictionary &&
                !original_DBGSourcePath_value.empty()) {
              DBGSourcePath = original_DBGSourcePath_value;
            }
            if (DBGSourcePath[0] == '~') {
              FileSpec resolved_source_path(DBGSourcePath.c_str());
              FileSystem::Instance().Resolve(resolved_source_path);
              DBGSourcePath = resolved_source_path.GetPath();
            }
            // With version 2 of DBGSourcePathRemapping, we can chop off the
            // last two filename parts from the source remapping and get a more
            // general source remapping that still works. Add this as another
            // option in addition to the full source path remap.
            module_spec.GetSourceMappingList().Append(DBGBuildSourcePath,
                                                      DBGSourcePath, true);
            if (do_truncate_remapping_names) {
              FileSpec build_path(DBGBuildSourcePath.c_str());
              FileSpec source_path(DBGSourcePath.c_str());
              build_path.RemoveLastPathComponent();
              build_path.RemoveLastPathComponent();
              source_path.RemoveLastPathComponent();
              source_path.RemoveLastPathComponent();
              module_spec.GetSourceMappingList().Append(
                  build_path.GetPath(), source_path.GetPath(), true);
            }
          }
        }
        if (keys)
          free(keys);
        if (values)
          free(values);
      }
    }

    // If we have a DBGBuildSourcePath + DBGSourcePath pair, append them to the
    // source remappings list.

    cf_str = (CFStringRef)CFDictionaryGetValue((CFDictionaryRef)uuid_dict,
                                               CFSTR("DBGBuildSourcePath"));
    if (cf_str && CFGetTypeID(cf_str) == CFStringGetTypeID()) {
      CFCString::FileSystemRepresentation(cf_str, DBGBuildSourcePath);
    }

    cf_str = (CFStringRef)CFDictionaryGetValue((CFDictionaryRef)uuid_dict,
                                               CFSTR("DBGSourcePath"));
    if (cf_str && CFGetTypeID(cf_str) == CFStringGetTypeID()) {
      CFCString::FileSystemRepresentation(cf_str, DBGSourcePath);
    }

    if (!DBGBuildSourcePath.empty() && !DBGSourcePath.empty()) {
      if (DBGSourcePath[0] == '~') {
        FileSpec resolved_source_path(DBGSourcePath.c_str());
        FileSystem::Instance().Resolve(resolved_source_path);
        DBGSourcePath = resolved_source_path.GetPath();
      }
      module_spec.GetSourceMappingList().Append(DBGBuildSourcePath,
                                                DBGSourcePath, true);
    }
  }
  return success;
}

/// It's expensive to check for the DBGShellCommands defaults setting. Only do
/// it once per lldb run and cache the result.
static llvm::StringRef GetDbgShellCommand() {
  static std::once_flag g_once_flag;
  static std::string g_dbgshell_command;
  std::call_once(g_once_flag, [&]() {
    CFTypeRef defaults_setting = CFPreferencesCopyAppValue(
        CFSTR("DBGShellCommands"), CFSTR("com.apple.DebugSymbols"));
    if (defaults_setting &&
        CFGetTypeID(defaults_setting) == CFStringGetTypeID()) {
      char buffer[PATH_MAX];
      if (CFStringGetCString((CFStringRef)defaults_setting, buffer,
                             sizeof(buffer), kCFStringEncodingUTF8)) {
        g_dbgshell_command = buffer;
      }
    }
    if (defaults_setting) {
      CFRelease(defaults_setting);
    }
  });
  return g_dbgshell_command;
}

/// Get the dsymForUUID executable and cache the result so we don't end up
/// stat'ing the binary over and over.
static FileSpec GetDsymForUUIDExecutable() {
  // The LLDB_APPLE_DSYMFORUUID_EXECUTABLE environment variable is used by the
  // test suite to override the dsymForUUID location. Because we must be able
  // to change the value within a single test, don't bother caching it.
  if (const char *dsymForUUID_env =
          getenv("LLDB_APPLE_DSYMFORUUID_EXECUTABLE")) {
    FileSpec dsymForUUID_executable(dsymForUUID_env);
    FileSystem::Instance().Resolve(dsymForUUID_executable);
    if (FileSystem::Instance().Exists(dsymForUUID_executable))
      return dsymForUUID_executable;
  }

  static std::once_flag g_once_flag;
  static FileSpec g_dsymForUUID_executable;
  std::call_once(g_once_flag, [&]() {
    // Try the DBGShellCommand.
    llvm::StringRef dbgshell_command = GetDbgShellCommand();
    if (!dbgshell_command.empty()) {
      g_dsymForUUID_executable = FileSpec(dbgshell_command);
      FileSystem::Instance().Resolve(g_dsymForUUID_executable);
      if (FileSystem::Instance().Exists(g_dsymForUUID_executable))
        return;
    }

    // Try dsymForUUID in /usr/local/bin
    {
      g_dsymForUUID_executable = FileSpec("/usr/local/bin/dsymForUUID");
      if (FileSystem::Instance().Exists(g_dsymForUUID_executable))
        return;
    }

    // We couldn't find the dsymForUUID binary.
    g_dsymForUUID_executable = {};
  });
  return g_dsymForUUID_executable;
}

bool SymbolLocatorDebugSymbols::DownloadObjectAndSymbolFile(
    ModuleSpec &module_spec, Status &error, bool force_lookup,
    bool copy_executable) {
  const UUID *uuid_ptr = module_spec.GetUUIDPtr();
  const FileSpec *file_spec_ptr = module_spec.GetFileSpecPtr();

  // If \a dbgshell_command is set, the user has specified
  // forced symbol lookup via that command.  We'll get the
  // path back from GetDsymForUUIDExecutable() later.
  llvm::StringRef dbgshell_command = GetDbgShellCommand();

  // If forced lookup isn't set, by the user's \a dbgshell_command or
  // by the \a force_lookup argument, exit this method.
  if (!force_lookup && dbgshell_command.empty())
    return false;

  // We need a UUID or valid existing FileSpec.
  if (!uuid_ptr &&
      (!file_spec_ptr || !FileSystem::Instance().Exists(*file_spec_ptr)))
    return false;

  // We need a dsymForUUID binary or an equivalent executable/script.
  FileSpec dsymForUUID_exe_spec = GetDsymForUUIDExecutable();
  if (!dsymForUUID_exe_spec)
    return false;

  // Create the dsymForUUID command.
  const std::string dsymForUUID_exe_path = dsymForUUID_exe_spec.GetPath();
  const std::string uuid_str = uuid_ptr ? uuid_ptr->GetAsString() : "";

  std::string lookup_arg = uuid_str;
  if (lookup_arg.empty())
    lookup_arg = file_spec_ptr ? file_spec_ptr->GetPath() : "";
  if (lookup_arg.empty())
    return false;

  StreamString command;
  command << dsymForUUID_exe_path << " --ignoreNegativeCache ";
  if (copy_executable)
    command << "--copyExecutable ";
  command << lookup_arg;

  // Log and report progress.
  std::string lookup_desc;
  if (uuid_ptr && file_spec_ptr)
    lookup_desc =
        llvm::formatv("{0} ({1})", file_spec_ptr->GetFilename().GetString(),
                      uuid_ptr->GetAsString());
  else if (uuid_ptr)
    lookup_desc = uuid_ptr->GetAsString();
  else if (file_spec_ptr)
    lookup_desc = file_spec_ptr->GetFilename().GetString();

  Log *log = GetLog(LLDBLog::Host);
  LLDB_LOG(log, "Calling {0} for {1} to find dSYM: {2}", dsymForUUID_exe_path,
           lookup_desc, command.GetString());

  Progress progress("Downloading symbol file for", lookup_desc);

  // Invoke dsymForUUID.
  int exit_status = -1;
  int signo = -1;
  std::string command_output;
  error = Host::RunShellCommand(
      command.GetData(),
      FileSpec(),      // current working directory
      &exit_status,    // Exit status
      &signo,          // Signal int *
      &command_output, // Command output
      std::chrono::seconds(
          640), // Large timeout to allow for long dsym download times
      false);   // Don't run in a shell (we don't need shell expansion)

  if (error.Fail() || exit_status != 0 || command_output.empty()) {
    LLDB_LOGF(log, "'%s' failed (exit status: %d, error: '%s', output: '%s')",
              command.GetData(), exit_status, error.AsCString(),
              command_output.c_str());
    return false;
  }

  CFCData data(
      CFDataCreateWithBytesNoCopy(NULL, (const UInt8 *)command_output.data(),
                                  command_output.size(), kCFAllocatorNull));

  CFCReleaser<CFDictionaryRef> plist(
      (CFDictionaryRef)::CFPropertyListCreateWithData(
          NULL, data.get(), kCFPropertyListImmutable, NULL, NULL));

  if (!plist.get()) {
    LLDB_LOGF(log, "'%s' failed: output is not a valid plist",
              command.GetData());
    return false;
  }

  if (CFGetTypeID(plist.get()) != CFDictionaryGetTypeID()) {
    LLDB_LOGF(log, "'%s' failed: output plist is not a valid CFDictionary",
              command.GetData());
    return false;
  }

  if (!uuid_str.empty()) {
    CFCString uuid_cfstr(uuid_str.c_str());
    CFDictionaryRef uuid_dict =
        (CFDictionaryRef)CFDictionaryGetValue(plist.get(), uuid_cfstr.get());
    return GetModuleSpecInfoFromUUIDDictionary(uuid_dict, module_spec, error,
                                               command.GetData());
  }

  if (const CFIndex num_values = ::CFDictionaryGetCount(plist.get())) {
    std::vector<CFStringRef> keys(num_values, NULL);
    std::vector<CFDictionaryRef> values(num_values, NULL);
    ::CFDictionaryGetKeysAndValues(plist.get(), NULL,
                                   (const void **)&values[0]);
    if (num_values == 1) {
      return GetModuleSpecInfoFromUUIDDictionary(values[0], module_spec, error,
                                                 command.GetData());
    }

    for (CFIndex i = 0; i < num_values; ++i) {
      ModuleSpec curr_module_spec;
      if (GetModuleSpecInfoFromUUIDDictionary(values[i], curr_module_spec,
                                              error, command.GetData())) {
        if (module_spec.GetArchitecture().IsCompatibleMatch(
                curr_module_spec.GetArchitecture())) {
          module_spec = curr_module_spec;
          return true;
        }
      }
    }
  }

  return false;
}
