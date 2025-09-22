//===-- PlatformDarwin.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWIN_H
#define LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWIN_H

#include "Plugins/Platform/POSIX/PlatformPOSIX.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/FileSpecList.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/XcodeSDK.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace lldb_private {
class BreakpointSite;
class Debugger;
class Module;
class ModuleSpec;
class Process;
class ProcessLaunchInfo;
class Stream;
class Target;

class PlatformDarwin : public PlatformPOSIX {
public:
  using PlatformPOSIX::PlatformPOSIX;

  ~PlatformDarwin() override;

  static lldb::PlatformSP CreateInstance(bool force, const ArchSpec *arch);

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "darwin"; }

  static llvm::StringRef GetDescriptionStatic();

  Status PutFile(const FileSpec &source, const FileSpec &destination,
                 uint32_t uid = UINT32_MAX, uint32_t gid = UINT32_MAX) override;

  // Platform functions
  Status ResolveSymbolFile(Target &target, const ModuleSpec &sym_spec,
                           FileSpec &sym_file) override;

  FileSpecList
  LocateExecutableScriptingResources(Target *target, Module &module,
                                     Stream &feedback_stream) override;

  Status GetSharedModule(const ModuleSpec &module_spec, Process *process,
                         lldb::ModuleSP &module_sp,
                         const FileSpecList *module_search_paths_ptr,
                         llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules,
                         bool *did_create_ptr) override;

  size_t GetSoftwareBreakpointTrapOpcode(Target &target,
                                         BreakpointSite *bp_site) override;

  lldb::BreakpointSP SetThreadCreationBreakpoint(Target &target) override;

  bool ModuleIsExcludedForUnconstrainedSearches(
      Target &target, const lldb::ModuleSP &module_sp) override;

  void
  ARMGetSupportedArchitectures(std::vector<ArchSpec> &archs,
                               std::optional<llvm::Triple::OSType> os = {});

  void x86GetSupportedArchitectures(std::vector<ArchSpec> &archs);

  uint32_t GetResumeCountForLaunchInfo(ProcessLaunchInfo &launch_info) override;

  lldb::ProcessSP DebugProcess(ProcessLaunchInfo &launch_info,
                               Debugger &debugger, Target &target,
                               Status &error) override;

  void CalculateTrapHandlerSymbolNames() override;

  llvm::VersionTuple GetOSVersion(Process *process = nullptr) override;

  bool SupportsModules() override { return true; }

  ConstString GetFullNameForDylib(ConstString basename) override;

  FileSpec LocateExecutable(const char *basename) override;

  Status LaunchProcess(ProcessLaunchInfo &launch_info) override;

  Args GetExtraStartupCommands() override;

  static std::tuple<llvm::VersionTuple, llvm::StringRef>
  ParseVersionBuildDir(llvm::StringRef str);

  llvm::Expected<StructuredData::DictionarySP>
  FetchExtendedCrashInformation(Process &process) override;

  /// Return the toolchain directory the current LLDB instance is located in.
  static FileSpec GetCurrentToolchainDirectory();

  /// Return the command line tools directory the current LLDB instance is
  /// located in.
  static FileSpec GetCurrentCommandLineToolsDirectory();

  /// Search each CU associated with the specified 'module' for
  /// the SDK paths the CUs were compiled against. In the presence
  /// of different SDKs, we try to pick the most appropriate one
  /// using \ref XcodeSDK::Merge.
  ///
  /// \param[in] module Module whose debug-info CUs to parse for
  ///                   which SDK they were compiled against.
  ///
  /// \returns If successful, returns a pair of a parsed XcodeSDK
  ///          object and a boolean that is 'true' if we encountered
  ///          a conflicting combination of SDKs when parsing the CUs
  ///          (e.g., a public and internal SDK).
  static llvm::Expected<std::pair<XcodeSDK, bool>>
  GetSDKPathFromDebugInfo(Module &module);

  /// Returns the full path of the most appropriate SDK for the
  /// specified 'module'. This function gets this path by parsing
  /// debug-info (see \ref `GetSDKPathFromDebugInfo`).
  ///
  /// \param[in] module Module whose debug-info to parse for
  ///                   which SDK it was compiled against.
  ///
  /// \returns If successful, returns the full path to an
  ///          Xcode SDK.
  static llvm::Expected<std::string>
  ResolveSDKPathFromDebugInfo(Module &module);

protected:
  static const char *GetCompatibleArch(ArchSpec::Core core, size_t idx);

  struct CrashInfoAnnotations {
    uint64_t version;          // unsigned long
    uint64_t message;          // char *
    uint64_t signature_string; // char *
    uint64_t backtrace;        // char *
    uint64_t message2;         // char *
    uint64_t thread;           // uint64_t
    uint64_t dialog_mode;      // unsigned int
    uint64_t abort_cause;      // unsigned int
  };

  /// Extract the `__crash_info` annotations from each of the target's
  /// modules.
  ///
  /// If the platform have a crashed processes with a `__crash_info` section,
  /// extract the section to gather the messages annotations and the abort
  /// cause.
  ///
  /// \param[in] process
  ///     The crashed process.
  ///
  /// \return
  ///     A  structured data array containing at each entry in each entry, the
  ///     module spec, its UUID, the crash messages and the abort cause.
  ///     \b nullptr if process has no crash information annotations.
  StructuredData::ArraySP ExtractCrashInfoAnnotations(Process &process);

  /// Extract the `Application Specific Information` messages from a crash
  /// report.
  StructuredData::DictionarySP ExtractAppSpecificInfo(Process &process);

  void ReadLibdispatchOffsetsAddress(Process *process);

  void ReadLibdispatchOffsets(Process *process);

  virtual bool CheckLocalSharedCache() const { return IsHost(); }

  struct SDKEnumeratorInfo {
    FileSpec found_path;
    XcodeSDK::Type sdk_type;
  };

  static FileSystem::EnumerateDirectoryResult
  DirectoryEnumerator(void *baton, llvm::sys::fs::file_type file_type,
                      llvm::StringRef path);

  static FileSpec FindSDKInXcodeForModules(XcodeSDK::Type sdk_type,
                                           const FileSpec &sdks_spec);

  static FileSpec GetSDKDirectoryForModules(XcodeSDK::Type sdk_type);

  void
  AddClangModuleCompilationOptionsForSDKType(Target *target,
                                             std::vector<std::string> &options,
                                             XcodeSDK::Type sdk_type);

  Status FindBundleBinaryInExecSearchPaths(
      const ModuleSpec &module_spec, Process *process,
      lldb::ModuleSP &module_sp, const FileSpecList *module_search_paths_ptr,
      llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules, bool *did_create_ptr);

  static std::string FindComponentInPath(llvm::StringRef path,
                                         llvm::StringRef component);

  // The OSType where lldb is running.
  static llvm::Triple::OSType GetHostOSType();

  std::string m_developer_directory;
  llvm::StringMap<std::string> m_sdk_path;
  std::mutex m_sdk_path_mutex;

private:
  PlatformDarwin(const PlatformDarwin &) = delete;
  const PlatformDarwin &operator=(const PlatformDarwin &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_PLATFORM_MACOSX_PLATFORMDARWIN_H
