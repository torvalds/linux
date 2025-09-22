//===-- PlatformDarwinKernel.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWINKERNEL_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWINKERNEL_H

#include "PlatformDarwin.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/UUID.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private-enumerations.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/FileSystem.h"

#include <vector>

namespace lldb_private {
class ArchSpec;
class Debugger;
class FileSpecList;
class ModuleSpec;
class Process;
class Stream;

#if defined(__APPLE__) // This Plugin uses the Mac-specific
                       // source/Host/macosx/cfcpp utilities

class PlatformDarwinKernel : public PlatformDarwin {
public:
  static lldb::PlatformSP CreateInstance(bool force, const ArchSpec *arch);

  static void DebuggerInitialize(Debugger &debugger);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "darwin-kernel"; }

  static llvm::StringRef GetDescriptionStatic();

  PlatformDarwinKernel(LazyBool is_ios_debug_session);

  ~PlatformDarwinKernel() override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  llvm::StringRef GetDescription() override { return GetDescriptionStatic(); }

  void GetStatus(Stream &strm) override;

  Status GetSharedModule(const ModuleSpec &module_spec, Process *process,
                         lldb::ModuleSP &module_sp,
                         const FileSpecList *module_search_paths_ptr,
                         llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules,
                         bool *did_create_ptr) override;

  std::vector<ArchSpec>
  GetSupportedArchitectures(const ArchSpec &process_host_arch) override;

  bool SupportsModules() override { return false; }

  void CalculateTrapHandlerSymbolNames() override;

protected:
  // Map from kext bundle ID ("com.apple.filesystems.exfat") to FileSpec for
  // the kext bundle on the host
  // ("/System/Library/Extensions/exfat.kext/Contents/Info.plist").
  typedef std::multimap<ConstString, FileSpec> BundleIDToKextMap;
  typedef BundleIDToKextMap::iterator BundleIDToKextIterator;

  typedef std::vector<FileSpec> KernelBinaryCollection;

  // Array of directories that were searched for kext bundles (used only for
  // reporting to user).
  typedef std::vector<FileSpec> DirectoriesSearchedCollection;
  typedef DirectoriesSearchedCollection::iterator DirectoriesSearchedIterator;

  // Populate m_search_directories and m_search_directories_no_recursing
  // vectors of directories.
  void CollectKextAndKernelDirectories();

  void GetUserSpecifiedDirectoriesToSearch();

  static void AddRootSubdirsToSearchPaths(PlatformDarwinKernel *thisp,
                                          const std::string &dir);

  void AddSDKSubdirsToSearchPaths(const std::string &dir);

  static FileSystem::EnumerateDirectoryResult
  FindKDKandSDKDirectoriesInDirectory(void *baton, llvm::sys::fs::file_type ft,
                                      llvm::StringRef path);

  void SearchForKextsAndKernelsRecursively();

  static FileSystem::EnumerateDirectoryResult
  GetKernelsAndKextsInDirectoryWithRecursion(void *baton,
                                             llvm::sys::fs::file_type ft,
                                             llvm::StringRef path);

  static FileSystem::EnumerateDirectoryResult
  GetKernelsAndKextsInDirectoryNoRecursion(void *baton,
                                           llvm::sys::fs::file_type ft,
                                           llvm::StringRef path);

  static FileSystem::EnumerateDirectoryResult
  GetKernelsAndKextsInDirectoryHelper(void *baton, llvm::sys::fs::file_type ft,
                                      llvm::StringRef path, bool recurse);

  static std::vector<FileSpec>
  SearchForExecutablesRecursively(const std::string &dir);

  static void AddKextToMap(PlatformDarwinKernel *thisp,
                           const FileSpec &file_spec);

  // Returns true if there is a .dSYM bundle next to the kext, or next to the
  // binary inside the kext.
  static bool KextHasdSYMSibling(const FileSpec &kext_bundle_filepath);

  // Returns true if there is a .dSYM bundle next to the kernel
  static bool KernelHasdSYMSibling(const FileSpec &kernel_filepath);

  // Returns true if there is a .dSYM bundle with NO kernel binary next to it
  static bool
  KerneldSYMHasNoSiblingBinary(const FileSpec &kernel_dsym_filepath);

  // Given a dsym_bundle argument ('.../foo.dSYM'), return a FileSpec
  // with the binary inside it ('.../foo.dSYM/Contents/Resources/DWARF/foo').
  // A dSYM bundle may have multiple DWARF binaries in them, so a vector
  // of matches is returned.
  static std::vector<FileSpec>
  GetDWARFBinaryInDSYMBundle(const FileSpec &dsym_bundle);

  Status GetSharedModuleKext(const ModuleSpec &module_spec, Process *process,
                             lldb::ModuleSP &module_sp,
                             const FileSpecList *module_search_paths_ptr,
                             llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules,
                             bool *did_create_ptr);

  Status GetSharedModuleKernel(
      const ModuleSpec &module_spec, Process *process,
      lldb::ModuleSP &module_sp, const FileSpecList *module_search_paths_ptr,
      llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules, bool *did_create_ptr);

  Status ExamineKextForMatchingUUID(const FileSpec &kext_bundle_path,
                                    const UUID &uuid, const ArchSpec &arch,
                                    lldb::ModuleSP &exe_module_sp);

  bool LoadPlatformBinaryAndSetup(Process *process, lldb::addr_t addr,
                                  bool notify) override;

  void UpdateKextandKernelsLocalScan();

  // Most of the ivars are assembled under FileSystem::EnumerateDirectory calls
  // where the function being called for each file/directory must be static.
  // We'll pass a this pointer as a baton and access the ivars directly.
  // Toss-up whether this should just be a struct at this point.

public:
  /// Multimap of CFBundleID to FileSpec on local filesystem, kexts with dSYMs
  /// next to them.
  BundleIDToKextMap m_name_to_kext_path_map_with_dsyms;

  /// Multimap of CFBundleID to FileSpec on local filesystem, kexts without
  /// dSYMs next to them.
  BundleIDToKextMap m_name_to_kext_path_map_without_dsyms;

  /// List of directories we search for kexts/kernels.
  DirectoriesSearchedCollection m_search_directories;

  /// List of directories we search for kexts/kernels, no recursion.
  DirectoriesSearchedCollection m_search_directories_no_recursing;

  /// List of kernel binaries we found on local filesystem, without dSYMs next
  /// to them.
  KernelBinaryCollection m_kernel_binaries_with_dsyms;

  /// List of kernel binaries we found on local filesystem, with dSYMs next to
  /// them.
  KernelBinaryCollection m_kernel_binaries_without_dsyms;

  /// List of kernel dsyms with no binaries next to them.
  KernelBinaryCollection m_kernel_dsyms_no_binaries;

  /// List of kernel .dSYM.yaa files.
  KernelBinaryCollection m_kernel_dsyms_yaas;

  LazyBool m_ios_debug_session;

  std::once_flag m_kext_scan_flag;

  PlatformDarwinKernel(const PlatformDarwinKernel &) = delete;
  const PlatformDarwinKernel &operator=(const PlatformDarwinKernel &) = delete;
};

#else // __APPLE__

// Since DynamicLoaderDarwinKernel is compiled in for all systems, and relies
// on PlatformDarwinKernel for the plug-in name, we compile just the plug-in
// name in here to avoid issues. We are tracking an internal bug to resolve
// this issue by either not compiling in DynamicLoaderDarwinKernel for
// non-apple builds, or to make PlatformDarwinKernel build on all systems.
//
// PlatformDarwinKernel is currently not compiled on other platforms due to the
// use of the Mac-specific source/Host/macosx/cfcpp utilities.

class PlatformDarwinKernel {
public:
  static llvm::StringRef GetPluginNameStatic() { return "darwin-kernel"; }
};

#endif // __APPLE__

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWINKERNEL_H
