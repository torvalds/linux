//===-- Platform.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_PLATFORM_H
#define LLDB_TARGET_PLATFORM_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/UserSettingsController.h"
#include "lldb/Host/File.h"
#include "lldb/Interpreter/Options.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/Utility/Timeout.h"
#include "lldb/Utility/UserIDResolver.h"
#include "lldb/lldb-private-forward.h"
#include "lldb/lldb-public.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/VersionTuple.h"

namespace lldb_private {

class ProcessInstanceInfo;
class ProcessInstanceInfoMatch;
typedef std::vector<ProcessInstanceInfo> ProcessInstanceInfoList;

class ModuleCache;
enum MmapFlags { eMmapFlagsPrivate = 1, eMmapFlagsAnon = 2 };

class PlatformProperties : public Properties {
public:
  PlatformProperties();

  static llvm::StringRef GetSettingName();

  bool GetUseModuleCache() const;
  bool SetUseModuleCache(bool use_module_cache);

  FileSpec GetModuleCacheDirectory() const;
  bool SetModuleCacheDirectory(const FileSpec &dir_spec);

private:
  void SetDefaultModuleCacheDirectory(const FileSpec &dir_spec);
};

typedef llvm::SmallVector<lldb::addr_t, 6> MmapArgList;

/// \class Platform Platform.h "lldb/Target/Platform.h"
/// A plug-in interface definition class for debug platform that
/// includes many platform abilities such as:
///     \li getting platform information such as supported architectures,
///         supported binary file formats and more
///     \li launching new processes
///     \li attaching to existing processes
///     \li download/upload files
///     \li execute shell commands
///     \li listing and getting info for existing processes
///     \li attaching and possibly debugging the platform's kernel
class Platform : public PluginInterface {
public:
  /// Default Constructor
  Platform(bool is_host_platform);

  /// The destructor is virtual since this class is designed to be inherited
  /// from by the plug-in instance.
  ~Platform() override;

  static void Initialize();

  static void Terminate();

  static PlatformProperties &GetGlobalPlatformProperties();

  /// Get the native host platform plug-in.
  ///
  /// There should only be one of these for each host that LLDB runs upon that
  /// should be statically compiled in and registered using preprocessor
  /// macros or other similar build mechanisms in a
  /// PlatformSubclass::Initialize() function.
  ///
  /// This platform will be used as the default platform when launching or
  /// attaching to processes unless another platform is specified.
  static lldb::PlatformSP GetHostPlatform();

  static const char *GetHostPlatformName();

  static void SetHostPlatform(const lldb::PlatformSP &platform_sp);

  static lldb::PlatformSP Create(llvm::StringRef name);

  /// Augments the triple either with information from platform or the host
  /// system (if platform is null).
  static ArchSpec GetAugmentedArchSpec(Platform *platform,
                                       llvm::StringRef triple);

  /// Set the target's executable based off of the existing architecture
  /// information in \a target given a path to an executable \a exe_file.
  ///
  /// Each platform knows the architectures that it supports and can select
  /// the correct architecture slice within \a exe_file by inspecting the
  /// architecture in \a target. If the target had an architecture specified,
  /// then in can try and obey that request and optionally fail if the
  /// architecture doesn't match up. If no architecture is specified, the
  /// platform should select the default architecture from \a exe_file. Any
  /// application bundles or executable wrappers can also be inspected for the
  /// actual application binary within the bundle that should be used.
  ///
  /// \return
  ///     Returns \b true if this Platform plug-in was able to find
  ///     a suitable executable, \b false otherwise.
  virtual Status ResolveExecutable(const ModuleSpec &module_spec,
                                   lldb::ModuleSP &exe_module_sp,
                                   const FileSpecList *module_search_paths_ptr);

  /// Find a symbol file given a symbol file module specification.
  ///
  /// Each platform might have tricks to find symbol files for an executable
  /// given information in a symbol file ModuleSpec. Some platforms might also
  /// support symbol files that are bundles and know how to extract the right
  /// symbol file given a bundle.
  ///
  /// \param[in] target
  ///     The target in which we are trying to resolve the symbol file.
  ///     The target has a list of modules that we might be able to
  ///     use in order to help find the right symbol file. If the
  ///     "m_file" or "m_platform_file" entries in the \a sym_spec
  ///     are filled in, then we might be able to locate a module in
  ///     the target, extract its UUID and locate a symbol file.
  ///     If just the "m_uuid" is specified, then we might be able
  ///     to find the module in the target that matches that UUID
  ///     and pair the symbol file along with it. If just "m_symbol_file"
  ///     is specified, we can use a variety of tricks to locate the
  ///     symbols in an SDK, PDK, or other development kit location.
  ///
  /// \param[in] sym_spec
  ///     A module spec that describes some information about the
  ///     symbol file we are trying to resolve. The ModuleSpec might
  ///     contain the following:
  ///     m_file - A full or partial path to an executable from the
  ///              target (might be empty).
  ///     m_platform_file - Another executable hint that contains
  ///                       the path to the file as known on the
  ///                       local/remote platform.
  ///     m_symbol_file - A full or partial path to a symbol file
  ///                     or symbol bundle that should be used when
  ///                     trying to resolve the symbol file.
  ///     m_arch - The architecture we are looking for when resolving
  ///              the symbol file.
  ///     m_uuid - The UUID of the executable and symbol file. This
  ///              can often be used to match up an executable with
  ///              a symbol file, or resolve an symbol file in a
  ///              symbol file bundle.
  ///
  /// \param[out] sym_file
  ///     The resolved symbol file spec if the returned error
  ///     indicates success.
  ///
  /// \return
  ///     Returns an error that describes success or failure.
  virtual Status ResolveSymbolFile(Target &target, const ModuleSpec &sym_spec,
                                   FileSpec &sym_file);

  /// Resolves the FileSpec to a (possibly) remote path. Remote platforms must
  /// override this to resolve to a path on the remote side.
  virtual bool ResolveRemotePath(const FileSpec &platform_path,
                                 FileSpec &resolved_platform_path);

  /// Get the OS version from a connected platform.
  ///
  /// Some platforms might not be connected to a remote platform, but can
  /// figure out the OS version for a process. This is common for simulator
  /// platforms that will run native programs on the current host, but the
  /// simulator might be simulating a different OS. The \a process parameter
  /// might be specified to help to determine the OS version.
  virtual llvm::VersionTuple GetOSVersion(Process *process = nullptr);

  bool SetOSVersion(llvm::VersionTuple os_version);

  std::optional<std::string> GetOSBuildString();

  std::optional<std::string> GetOSKernelDescription();

  // Returns the name of the platform
  llvm::StringRef GetName() { return GetPluginName(); }

  virtual const char *GetHostname();

  virtual ConstString GetFullNameForDylib(ConstString basename);

  virtual llvm::StringRef GetDescription() = 0;

  /// Report the current status for this platform.
  ///
  /// The returned string usually involves returning the OS version (if
  /// available), and any SDK directory that might be being used for local
  /// file caching, and if connected a quick blurb about what this platform is
  /// connected to.
  virtual void GetStatus(Stream &strm);

  // Subclasses must be able to fetch the current OS version
  //
  // Remote classes must be connected for this to succeed. Local subclasses
  // don't need to override this function as it will just call the
  // HostInfo::GetOSVersion().
  virtual bool GetRemoteOSVersion() { return false; }

  virtual std::optional<std::string> GetRemoteOSBuildString() {
    return std::nullopt;
  }

  virtual std::optional<std::string> GetRemoteOSKernelDescription() {
    return std::nullopt;
  }

  // Remote Platform subclasses need to override this function
  virtual ArchSpec GetRemoteSystemArchitecture() {
    return ArchSpec(); // Return an invalid architecture
  }

  virtual FileSpec GetRemoteWorkingDirectory() { return m_working_dir; }

  virtual bool SetRemoteWorkingDirectory(const FileSpec &working_dir);

  virtual UserIDResolver &GetUserIDResolver();

  /// Locate a file for a platform.
  ///
  /// The default implementation of this function will return the same file
  /// patch in \a local_file as was in \a platform_file.
  ///
  /// \param[in] platform_file
  ///     The platform file path to locate and cache locally.
  ///
  /// \param[in] uuid_ptr
  ///     If we know the exact UUID of the file we are looking for, it
  ///     can be specified. If it is not specified, we might now know
  ///     the exact file. The UUID is usually some sort of MD5 checksum
  ///     for the file and is sometimes known by dynamic linkers/loaders.
  ///     If the UUID is known, it is best to supply it to platform
  ///     file queries to ensure we are finding the correct file, not
  ///     just a file at the correct path.
  ///
  /// \param[out] local_file
  ///     A locally cached version of the platform file. For platforms
  ///     that describe the current host computer, this will just be
  ///     the same file. For remote platforms, this file might come from
  ///     and SDK directory, or might need to be sync'ed over to the
  ///     current machine for efficient debugging access.
  ///
  /// \return
  ///     An error object.
  virtual Status GetFileWithUUID(const FileSpec &platform_file,
                                 const UUID *uuid_ptr, FileSpec &local_file);

  // Locate the scripting resource given a module specification.
  //
  // Locating the file should happen only on the local computer or using the
  // current computers global settings.
  virtual FileSpecList
  LocateExecutableScriptingResources(Target *target, Module &module,
                                     Stream &feedback_stream);

  /// \param[in] module_spec
  ///     The ModuleSpec of a binary to find.
  ///
  /// \param[in] process
  ///     A Process.
  ///
  /// \param[out] module_sp
  ///     A Module that matches the ModuleSpec, if one is found.
  ///
  /// \param[in] module_search_paths_ptr
  ///     Locations to possibly look for a binary that matches the ModuleSpec.
  ///
  /// \param[out] old_modules
  ///     Existing Modules in the Process' Target image list which match
  ///     the FileSpec.
  ///
  /// \param[out] did_create_ptr
  ///     Optional boolean, nullptr may be passed for this argument.
  ///     If this method is returning a *new* ModuleSP, this
  ///     will be set to true.
  ///     If this method is returning a ModuleSP that is already in the
  ///     Target's image list, it will be false.
  ///
  /// \return
  ///     The Status object for any errors found while searching for
  ///     the binary.
  virtual Status GetSharedModule(
      const ModuleSpec &module_spec, Process *process,
      lldb::ModuleSP &module_sp, const FileSpecList *module_search_paths_ptr,
      llvm::SmallVectorImpl<lldb::ModuleSP> *old_modules, bool *did_create_ptr);

  void CallLocateModuleCallbackIfSet(const ModuleSpec &module_spec,
                                     lldb::ModuleSP &module_sp,
                                     FileSpec &symbol_file_spec,
                                     bool *did_create_ptr);

  virtual bool GetModuleSpec(const FileSpec &module_file_spec,
                             const ArchSpec &arch, ModuleSpec &module_spec);

  virtual Status ConnectRemote(Args &args);

  virtual Status DisconnectRemote();

  /// Get the platform's supported architectures in the order in which they
  /// should be searched.
  ///
  /// \param[in] process_host_arch
  ///     The process host architecture if it's known. An invalid ArchSpec
  ///     represents that the process host architecture is unknown.
  virtual std::vector<ArchSpec>
  GetSupportedArchitectures(const ArchSpec &process_host_arch) = 0;

  virtual size_t GetSoftwareBreakpointTrapOpcode(Target &target,
                                                 BreakpointSite *bp_site);

  /// Launch a new process on a platform, not necessarily for debugging, it
  /// could be just for running the process.
  virtual Status LaunchProcess(ProcessLaunchInfo &launch_info);

  /// Perform expansion of the command-line for this launch info This can
  /// potentially involve wildcard expansion
  /// environment variable replacement, and whatever other
  /// argument magic the platform defines as part of its typical
  /// user experience
  virtual Status ShellExpandArguments(ProcessLaunchInfo &launch_info);

  /// Kill process on a platform.
  virtual Status KillProcess(const lldb::pid_t pid);

  /// Lets a platform answer if it is compatible with a given architecture and
  /// the target triple contained within.
  virtual bool IsCompatibleArchitecture(const ArchSpec &arch,
                                        const ArchSpec &process_host_arch,
                                        ArchSpec::MatchType match,
                                        ArchSpec *compatible_arch_ptr);

  /// Not all platforms will support debugging a process by spawning somehow
  /// halted for a debugger (specified using the "eLaunchFlagDebug" launch
  /// flag) and then attaching. If your platform doesn't support this,
  /// override this function and return false.
  virtual bool CanDebugProcess() { return true; }

  /// Subclasses do not need to implement this function as it uses the
  /// Platform::LaunchProcess() followed by Platform::Attach (). Remote
  /// platforms will want to subclass this function in order to be able to
  /// intercept STDIO and possibly launch a separate process that will debug
  /// the debuggee.
  virtual lldb::ProcessSP DebugProcess(ProcessLaunchInfo &launch_info,
                                       Debugger &debugger, Target &target,
                                       Status &error);

  virtual lldb::ProcessSP ConnectProcess(llvm::StringRef connect_url,
                                         llvm::StringRef plugin_name,
                                         Debugger &debugger, Target *target,
                                         Status &error);

  virtual lldb::ProcessSP
  ConnectProcessSynchronous(llvm::StringRef connect_url,
                            llvm::StringRef plugin_name, Debugger &debugger,
                            Stream &stream, Target *target, Status &error);

  /// Attach to an existing process using a process ID.
  ///
  /// Each platform subclass needs to implement this function and attempt to
  /// attach to the process with the process ID of \a pid. The platform
  /// subclass should return an appropriate ProcessSP subclass that is
  /// attached to the process, or an empty shared pointer with an appropriate
  /// error.
  ///
  /// \return
  ///     An appropriate ProcessSP containing a valid shared pointer
  ///     to the default Process subclass for the platform that is
  ///     attached to the process, or an empty shared pointer with an
  ///     appropriate error fill into the \a error object.
  virtual lldb::ProcessSP Attach(ProcessAttachInfo &attach_info,
                                 Debugger &debugger,
                                 Target *target, // Can be nullptr, if nullptr
                                                 // create a new target, else
                                                 // use existing one
                                 Status &error) = 0;

  /// Attach to an existing process by process name.
  ///
  /// This function is not meant to be overridden by Process subclasses. It
  /// will first call Process::WillAttach (const char *) and if that returns
  /// \b true, Process::DoAttach (const char *) will be called to actually do
  /// the attach. If DoAttach returns \b true, then Process::DidAttach() will
  /// be called.
  ///
  /// \param[in] process_name
  ///     A process name to match against the current process list.
  ///
  /// \return
  ///     Returns \a pid if attaching was successful, or
  ///     LLDB_INVALID_PROCESS_ID if attaching fails.
  //        virtual lldb::ProcessSP
  //        Attach (const char *process_name,
  //                bool wait_for_launch,
  //                Status &error) = 0;

  // The base class Platform will take care of the host platform. Subclasses
  // will need to fill in the remote case.
  virtual uint32_t FindProcesses(const ProcessInstanceInfoMatch &match_info,
                                 ProcessInstanceInfoList &proc_infos);

  ProcessInstanceInfoList GetAllProcesses();

  virtual bool GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &proc_info);

  // Set a breakpoint on all functions that can end up creating a thread for
  // this platform. This is needed when running expressions and also for
  // process control.
  virtual lldb::BreakpointSP SetThreadCreationBreakpoint(Target &target);

  // Given a target, find the local SDK directory if one exists on the current
  // host.
  virtual lldb_private::ConstString
  GetSDKDirectory(lldb_private::Target &target) {
    return lldb_private::ConstString();
  }

  const std::string &GetRemoteURL() const { return m_remote_url; }

  bool IsHost() const {
    return m_is_host; // Is this the default host platform?
  }

  bool IsRemote() const { return !m_is_host; }

  virtual bool IsConnected() const {
    // Remote subclasses should override this function
    return IsHost();
  }

  const ArchSpec &GetSystemArchitecture();

  void SetSystemArchitecture(const ArchSpec &arch) {
    m_system_arch = arch;
    if (IsHost())
      m_os_version_set_while_connected = m_system_arch.IsValid();
  }

  /// If the triple contains not specify the vendor, os, and environment
  /// parts, we "augment" these using information from the platform and return
  /// the resulting ArchSpec object.
  ArchSpec GetAugmentedArchSpec(llvm::StringRef triple);

  // Used for column widths
  size_t GetMaxUserIDNameLength() const { return m_max_uid_name_len; }

  // Used for column widths
  size_t GetMaxGroupIDNameLength() const { return m_max_gid_name_len; }

  const std::string &GetSDKRootDirectory() const { return m_sdk_sysroot; }

  void SetSDKRootDirectory(std::string dir) { m_sdk_sysroot = std::move(dir); }

  const std::string &GetSDKBuild() const { return m_sdk_build; }

  void SetSDKBuild(std::string sdk_build) {
    m_sdk_build = std::move(sdk_build);
  }

  // Override this to return true if your platform supports Clang modules. You
  // may also need to override AddClangModuleCompilationOptions to pass the
  // right Clang flags for your platform.
  virtual bool SupportsModules() { return false; }

  // Appends the platform-specific options required to find the modules for the
  // current platform.
  virtual void
  AddClangModuleCompilationOptions(Target *target,
                                   std::vector<std::string> &options);

  FileSpec GetWorkingDirectory();

  bool SetWorkingDirectory(const FileSpec &working_dir);

  // There may be modules that we don't want to find by default for operations
  // like "setting breakpoint by name". The platform will return "true" from
  // this call if the passed in module happens to be one of these.

  virtual bool
  ModuleIsExcludedForUnconstrainedSearches(Target &target,
                                           const lldb::ModuleSP &module_sp) {
    return false;
  }

  virtual Status MakeDirectory(const FileSpec &file_spec, uint32_t permissions);

  virtual Status GetFilePermissions(const FileSpec &file_spec,
                                    uint32_t &file_permissions);

  virtual Status SetFilePermissions(const FileSpec &file_spec,
                                    uint32_t file_permissions);

  virtual lldb::user_id_t OpenFile(const FileSpec &file_spec,
                                   File::OpenOptions flags, uint32_t mode,
                                   Status &error);

  virtual bool CloseFile(lldb::user_id_t fd, Status &error);

  virtual lldb::user_id_t GetFileSize(const FileSpec &file_spec);

  virtual void AutoCompleteDiskFileOrDirectory(CompletionRequest &request,
                                               bool only_dir) {}

  virtual uint64_t ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                            uint64_t dst_len, Status &error);

  virtual uint64_t WriteFile(lldb::user_id_t fd, uint64_t offset,
                             const void *src, uint64_t src_len, Status &error);

  virtual Status GetFile(const FileSpec &source, const FileSpec &destination);

  virtual Status PutFile(const FileSpec &source, const FileSpec &destination,
                         uint32_t uid = UINT32_MAX, uint32_t gid = UINT32_MAX);

  virtual Status
  CreateSymlink(const FileSpec &src,  // The name of the link is in src
                const FileSpec &dst); // The symlink points to dst

  /// Install a file or directory to the remote system.
  ///
  /// Install is similar to Platform::PutFile(), but it differs in that if an
  /// application/framework/shared library is installed on a remote platform
  /// and the remote platform requires something to be done to register the
  /// application/framework/shared library, then this extra registration can
  /// be done.
  ///
  /// \param[in] src
  ///     The source file/directory to install on the remote system.
  ///
  /// \param[in] dst
  ///     The destination file/directory where \a src will be installed.
  ///     If \a dst has no filename specified, then its filename will
  ///     be set from \a src. It \a dst has no directory specified, it
  ///     will use the platform working directory. If \a dst has a
  ///     directory specified, but the directory path is relative, the
  ///     platform working directory will be prepended to the relative
  ///     directory.
  ///
  /// \return
  ///     An error object that describes anything that went wrong.
  virtual Status Install(const FileSpec &src, const FileSpec &dst);

  virtual Environment GetEnvironment();

  virtual bool GetFileExists(const lldb_private::FileSpec &file_spec);

  virtual Status Unlink(const FileSpec &file_spec);

  virtual MmapArgList GetMmapArgumentList(const ArchSpec &arch,
                                          lldb::addr_t addr,
                                          lldb::addr_t length,
                                          unsigned prot, unsigned flags,
                                          lldb::addr_t fd, lldb::addr_t offset);

  virtual bool GetSupportsRSync() { return m_supports_rsync; }

  virtual void SetSupportsRSync(bool flag) { m_supports_rsync = flag; }

  virtual const char *GetRSyncOpts() { return m_rsync_opts.c_str(); }

  virtual void SetRSyncOpts(const char *opts) { m_rsync_opts.assign(opts); }

  virtual const char *GetRSyncPrefix() { return m_rsync_prefix.c_str(); }

  virtual void SetRSyncPrefix(const char *prefix) {
    m_rsync_prefix.assign(prefix);
  }

  virtual bool GetSupportsSSH() { return m_supports_ssh; }

  virtual void SetSupportsSSH(bool flag) { m_supports_ssh = flag; }

  virtual const char *GetSSHOpts() { return m_ssh_opts.c_str(); }

  virtual void SetSSHOpts(const char *opts) { m_ssh_opts.assign(opts); }

  virtual bool GetIgnoresRemoteHostname() { return m_ignores_remote_hostname; }

  virtual void SetIgnoresRemoteHostname(bool flag) {
    m_ignores_remote_hostname = flag;
  }

  virtual lldb_private::OptionGroupOptions *
  GetConnectionOptions(CommandInterpreter &interpreter) {
    return nullptr;
  }

  virtual lldb_private::Status RunShellCommand(
      llvm::StringRef command,
      const FileSpec &working_dir, // Pass empty FileSpec to use the current
                                   // working directory
      int *status_ptr, // Pass nullptr if you don't want the process exit status
      int *signo_ptr,  // Pass nullptr if you don't want the signal that caused
                       // the process to exit
      std::string
          *command_output, // Pass nullptr if you don't want the command output
      const Timeout<std::micro> &timeout);

  virtual lldb_private::Status RunShellCommand(
      llvm::StringRef shell, llvm::StringRef command,
      const FileSpec &working_dir, // Pass empty FileSpec to use the current
                                   // working directory
      int *status_ptr, // Pass nullptr if you don't want the process exit status
      int *signo_ptr,  // Pass nullptr if you don't want the signal that caused
                       // the process to exit
      std::string
          *command_output, // Pass nullptr if you don't want the command output
      const Timeout<std::micro> &timeout);

  virtual void SetLocalCacheDirectory(const char *local);

  virtual const char *GetLocalCacheDirectory();

  virtual std::string GetPlatformSpecificConnectionInformation() { return ""; }

  virtual llvm::ErrorOr<llvm::MD5::MD5Result>
  CalculateMD5(const FileSpec &file_spec);

  virtual uint32_t GetResumeCountForLaunchInfo(ProcessLaunchInfo &launch_info) {
    return 1;
  }

  virtual const lldb::UnixSignalsSP &GetRemoteUnixSignals();

  lldb::UnixSignalsSP GetUnixSignals();

  /// Locate a queue name given a thread's qaddr
  ///
  /// On a system using libdispatch ("Grand Central Dispatch") style queues, a
  /// thread may be associated with a GCD queue or not, and a queue may be
  /// associated with multiple threads. The process/thread must provide a way
  /// to find the "dispatch_qaddr" for each thread, and from that
  /// dispatch_qaddr this Platform method will locate the queue name and
  /// provide that.
  ///
  /// \param[in] process
  ///     A process is required for reading memory.
  ///
  /// \param[in] dispatch_qaddr
  ///     The dispatch_qaddr for this thread.
  ///
  /// \return
  ///     The name of the queue, if there is one.  An empty string
  ///     means that this thread is not associated with a dispatch
  ///     queue.
  virtual std::string
  GetQueueNameForThreadQAddress(Process *process, lldb::addr_t dispatch_qaddr) {
    return "";
  }

  /// Locate a queue ID given a thread's qaddr
  ///
  /// On a system using libdispatch ("Grand Central Dispatch") style queues, a
  /// thread may be associated with a GCD queue or not, and a queue may be
  /// associated with multiple threads. The process/thread must provide a way
  /// to find the "dispatch_qaddr" for each thread, and from that
  /// dispatch_qaddr this Platform method will locate the queue ID and provide
  /// that.
  ///
  /// \param[in] process
  ///     A process is required for reading memory.
  ///
  /// \param[in] dispatch_qaddr
  ///     The dispatch_qaddr for this thread.
  ///
  /// \return
  ///     The queue_id for this thread, if this thread is associated
  ///     with a dispatch queue.  Else LLDB_INVALID_QUEUE_ID is returned.
  virtual lldb::queue_id_t
  GetQueueIDForThreadQAddress(Process *process, lldb::addr_t dispatch_qaddr) {
    return LLDB_INVALID_QUEUE_ID;
  }

  /// Provide a list of trap handler function names for this platform
  ///
  /// The unwinder needs to treat trap handlers specially -- the stack frame
  /// may not be aligned correctly for a trap handler (the kernel often won't
  /// perturb the stack pointer, or won't re-align it properly, in the process
  /// of calling the handler) and the frame above the handler needs to be
  /// treated by the unwinder's "frame 0" rules instead of its "middle of the
  /// stack frame" rules.
  ///
  /// In a user process debugging scenario, the list of trap handlers is
  /// typically just "_sigtramp".
  ///
  /// The Platform base class provides the m_trap_handlers ivar but it does
  /// not populate it.  Subclasses should add the names of the asynchronous
  /// signal handler routines as needed.  For most Unix platforms, add
  /// _sigtramp.
  ///
  /// \return
  ///     A list of symbol names.  The list may be empty.
  virtual const std::vector<ConstString> &GetTrapHandlerSymbolNames();

  /// Try to get a specific unwind plan for a named trap handler.
  /// The default is not to have specific unwind plans for trap handlers.
  ///
  /// \param[in] triple
  ///     Triple of the current target.
  ///
  /// \param[in] name
  ///     Name of the trap handler function.
  ///
  /// \return
  ///     A specific unwind plan for that trap handler, or an empty
  ///     shared pointer. The latter means there is no specific plan,
  ///     unwind as normal.
  virtual lldb::UnwindPlanSP
  GetTrapHandlerUnwindPlan(const llvm::Triple &triple, ConstString name) {
    return {};
  }

  /// Find a support executable that may not live within in the standard
  /// locations related to LLDB.
  ///
  /// Executable might exist within the Platform SDK directories, or in
  /// standard tool directories within the current IDE that is running LLDB.
  ///
  /// \param[in] basename
  ///     The basename of the executable to locate in the current
  ///     platform.
  ///
  /// \return
  ///     A FileSpec pointing to the executable on disk, or an invalid
  ///     FileSpec if the executable cannot be found.
  virtual FileSpec LocateExecutable(const char *basename) { return FileSpec(); }

  /// Allow the platform to set preferred memory cache line size. If non-zero
  /// (and the user has not set cache line size explicitly), this value will
  /// be used as the cache line size for memory reads.
  virtual uint32_t GetDefaultMemoryCacheLineSize() { return 0; }

  /// Load a shared library into this process.
  ///
  /// Try and load a shared library into the current process. This call might
  /// fail in the dynamic loader plug-in says it isn't safe to try and load
  /// shared libraries at the moment.
  ///
  /// \param[in] process
  ///     The process to load the image.
  ///
  /// \param[in] local_file
  ///     The file spec that points to the shared library that you want
  ///     to load if the library is located on the host. The library will
  ///     be copied over to the location specified by remote_file or into
  ///     the current working directory with the same filename if the
  ///     remote_file isn't specified.
  ///
  /// \param[in] remote_file
  ///     If local_file is specified then the location where the library
  ///     should be copied over from the host. If local_file isn't
  ///     specified, then the path for the shared library on the target
  ///     what you want to load.
  ///
  /// \param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to load the shared library.
  ///
  /// \return
  ///     A token that represents the shared library that can be
  ///     later used to unload the shared library. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  uint32_t LoadImage(lldb_private::Process *process,
                     const lldb_private::FileSpec &local_file,
                     const lldb_private::FileSpec &remote_file,
                     lldb_private::Status &error);

  /// Load a shared library specified by base name into this process,
  /// looking by hand along a set of paths.
  ///
  /// \param[in] process
  ///     The process to load the image.
  ///
  /// \param[in] library_name
  ///     The name of the library to look for.  If library_name is an
  ///     absolute path, the basename will be extracted and searched for
  ///     along the paths.  This emulates the behavior of the loader when
  ///     given an install name and a set (e.g. DYLD_LIBRARY_PATH provided) of
  ///     alternate paths.
  ///
  /// \param[in] paths
  ///     The list of paths to use to search for the library.  First
  ///     match wins.
  ///
  /// \param[out] error
  ///     An error object that gets filled in with any errors that
  ///     might occur when trying to load the shared library.
  ///
  /// \param[out] loaded_path
  ///      If non-null, the path to the dylib that was successfully loaded
  ///      is stored in this path.
  ///
  /// \return
  ///     A token that represents the shared library which can be
  ///     passed to UnloadImage. A value of
  ///     LLDB_INVALID_IMAGE_TOKEN will be returned if the shared
  ///     library can't be opened.
  uint32_t LoadImageUsingPaths(lldb_private::Process *process,
                               const lldb_private::FileSpec &library_name,
                               const std::vector<std::string> &paths,
                               lldb_private::Status &error,
                               lldb_private::FileSpec *loaded_path);

  virtual uint32_t DoLoadImage(lldb_private::Process *process,
                               const lldb_private::FileSpec &remote_file,
                               const std::vector<std::string> *paths,
                               lldb_private::Status &error,
                               lldb_private::FileSpec *loaded_path = nullptr);

  virtual Status UnloadImage(lldb_private::Process *process,
                             uint32_t image_token);

  /// Connect to all processes waiting for a debugger to attach
  ///
  /// If the platform have a list of processes waiting for a debugger to
  /// connect to them then connect to all of these pending processes.
  ///
  /// \param[in] debugger
  ///     The debugger used for the connect.
  ///
  /// \param[out] error
  ///     If an error occurred during the connect then this object will
  ///     contain the error message.
  ///
  /// \return
  ///     The number of processes we are successfully connected to.
  virtual size_t ConnectToWaitingProcesses(lldb_private::Debugger &debugger,
                                           lldb_private::Status &error);

  /// Gather all of crash informations into a structured data dictionary.
  ///
  /// If the platform have a crashed process with crash information entries,
  /// gather all the entries into an structured data dictionary or return a
  /// nullptr. This dictionary is generic and extensible, as it contains an
  /// array for each different type of crash information.
  ///
  /// \param[in] process
  ///     The crashed process.
  ///
  /// \return
  ///     A structured data dictionary containing at each entry, the crash
  ///     information type as the entry key and the matching  an array as the
  ///     entry value. \b nullptr if not implemented or  if the process has no
  ///     crash information entry. \b error if an error occured.
  virtual llvm::Expected<StructuredData::DictionarySP>
  FetchExtendedCrashInformation(lldb_private::Process &process) {
    return nullptr;
  }

  /// Detect a binary in memory that will determine which Platform and
  /// DynamicLoader should be used in this target/process, and update
  /// the Platform/DynamicLoader.
  /// The binary will be loaded into the Target, or will be registered with
  /// the DynamicLoader so that it will be loaded at a later stage.  Returns
  /// true to indicate that this is a platform binary and has been
  /// loaded/registered, no further action should be taken by the caller.
  ///
  /// \param[in] process
  ///     Process read memory from, a Process must be provided.
  ///
  /// \param[in] addr
  ///     Address of a binary in memory.
  ///
  /// \param[in] notify
  ///     Whether ModulesDidLoad should be called, if a binary is loaded.
  ///     Caller may prefer to call ModulesDidLoad for multiple binaries
  ///     that were loaded at the same time.
  ///
  /// \return
  ///     Returns true if the binary was loaded in the target (or will be
  ///     via a DynamicLoader).  Returns false if the binary was not
  ///     loaded/registered, and the caller must load it into the target.
  virtual bool LoadPlatformBinaryAndSetup(Process *process, lldb::addr_t addr,
                                          bool notify) {
    return false;
  }

  virtual CompilerType GetSiginfoType(const llvm::Triple &triple);

  virtual Args GetExtraStartupCommands();

  typedef std::function<Status(const ModuleSpec &module_spec,
                               FileSpec &module_file_spec,
                               FileSpec &symbol_file_spec)>
      LocateModuleCallback;

  /// Set locate module callback. This allows users to implement their own
  /// module cache system. For example, to leverage artifacts of build system,
  /// to bypass pulling files from remote platform, or to search symbol files
  /// from symbol servers.
  void SetLocateModuleCallback(LocateModuleCallback callback);

  LocateModuleCallback GetLocateModuleCallback() const;

protected:
  /// Create a list of ArchSpecs with the given OS and a architectures. The
  /// vendor field is left as an "unspecified unknown".
  static std::vector<ArchSpec>
  CreateArchList(llvm::ArrayRef<llvm::Triple::ArchType> archs,
                 llvm::Triple::OSType os);

  /// Private implementation of connecting to a process. If the stream is set
  /// we connect synchronously.
  lldb::ProcessSP DoConnectProcess(llvm::StringRef connect_url,
                                   llvm::StringRef plugin_name,
                                   Debugger &debugger, Stream *stream,
                                   Target *target, Status &error);
  bool m_is_host;
  // Set to true when we are able to actually set the OS version while being
  // connected. For remote platforms, we might set the version ahead of time
  // before we actually connect and this version might change when we actually
  // connect to a remote platform. For the host platform this will be set to
  // the once we call HostInfo::GetOSVersion().
  bool m_os_version_set_while_connected;
  bool m_system_arch_set_while_connected;
  std::string
      m_sdk_sysroot; // the root location of where the SDK files are all located
  std::string m_sdk_build;
  FileSpec m_working_dir; // The working directory which is used when installing
                          // modules that have no install path set
  std::string m_remote_url;
  std::string m_hostname;
  llvm::VersionTuple m_os_version;
  ArchSpec
      m_system_arch; // The architecture of the kernel or the remote platform
  typedef std::map<uint32_t, ConstString> IDToNameMap;
  // Mutex for modifying Platform data structures that should only be used for
  // non-reentrant code
  std::mutex m_mutex;
  size_t m_max_uid_name_len;
  size_t m_max_gid_name_len;
  bool m_supports_rsync;
  std::string m_rsync_opts;
  std::string m_rsync_prefix;
  bool m_supports_ssh;
  std::string m_ssh_opts;
  bool m_ignores_remote_hostname;
  std::string m_local_cache_directory;
  std::vector<ConstString> m_trap_handlers;
  bool m_calculated_trap_handlers;
  const std::unique_ptr<ModuleCache> m_module_cache;
  LocateModuleCallback m_locate_module_callback;

  /// Ask the Platform subclass to fill in the list of trap handler names
  ///
  /// For most Unix user process environments, this will be a single function
  /// name, _sigtramp.  More specialized environments may have additional
  /// handler names.  The unwinder code needs to know when a trap handler is
  /// on the stack because the unwind rules for the frame that caused the trap
  /// are different.
  ///
  /// The base class Platform ivar m_trap_handlers should be updated by the
  /// Platform subclass when this method is called.  If there are no
  /// predefined trap handlers, this method may be a no-op.
  virtual void CalculateTrapHandlerSymbolNames() = 0;

  Status GetCachedExecutable(ModuleSpec &module_spec, lldb::ModuleSP &module_sp,
                             const FileSpecList *module_search_paths_ptr);

  virtual Status DownloadModuleSlice(const FileSpec &src_file_spec,
                                     const uint64_t src_offset,
                                     const uint64_t src_size,
                                     const FileSpec &dst_file_spec);

  virtual Status DownloadSymbolFile(const lldb::ModuleSP &module_sp,
                                    const FileSpec &dst_file_spec);

  virtual const char *GetCacheHostname();

private:
  typedef std::function<Status(const ModuleSpec &)> ModuleResolver;

  Status GetRemoteSharedModule(const ModuleSpec &module_spec, Process *process,
                               lldb::ModuleSP &module_sp,
                               const ModuleResolver &module_resolver,
                               bool *did_create_ptr);

  bool GetCachedSharedModule(const ModuleSpec &module_spec,
                             lldb::ModuleSP &module_sp, bool *did_create_ptr);

  FileSpec GetModuleCacheRoot();
};

class PlatformList {
public:
  PlatformList() = default;

  ~PlatformList() = default;

  void Append(const lldb::PlatformSP &platform_sp, bool set_selected) {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    m_platforms.push_back(platform_sp);
    if (set_selected)
      m_selected_platform_sp = m_platforms.back();
  }

  size_t GetSize() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    return m_platforms.size();
  }

  lldb::PlatformSP GetAtIndex(uint32_t idx) {
    lldb::PlatformSP platform_sp;
    {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      if (idx < m_platforms.size())
        platform_sp = m_platforms[idx];
    }
    return platform_sp;
  }

  /// Select the active platform.
  ///
  /// In order to debug remotely, other platform's can be remotely connected
  /// to and set as the selected platform for any subsequent debugging. This
  /// allows connection to remote targets and allows the ability to discover
  /// process info, launch and attach to remote processes.
  lldb::PlatformSP GetSelectedPlatform() {
    std::lock_guard<std::recursive_mutex> guard(m_mutex);
    if (!m_selected_platform_sp && !m_platforms.empty())
      m_selected_platform_sp = m_platforms.front();

    return m_selected_platform_sp;
  }

  void SetSelectedPlatform(const lldb::PlatformSP &platform_sp) {
    if (platform_sp) {
      std::lock_guard<std::recursive_mutex> guard(m_mutex);
      const size_t num_platforms = m_platforms.size();
      for (size_t idx = 0; idx < num_platforms; ++idx) {
        if (m_platforms[idx].get() == platform_sp.get()) {
          m_selected_platform_sp = m_platforms[idx];
          return;
        }
      }
      m_platforms.push_back(platform_sp);
      m_selected_platform_sp = m_platforms.back();
    }
  }

  lldb::PlatformSP GetOrCreate(llvm::StringRef name);
  lldb::PlatformSP GetOrCreate(const ArchSpec &arch,
                               const ArchSpec &process_host_arch,
                               ArchSpec *platform_arch_ptr, Status &error);
  lldb::PlatformSP GetOrCreate(const ArchSpec &arch,
                               const ArchSpec &process_host_arch,
                               ArchSpec *platform_arch_ptr);

  /// Get the platform for the given list of architectures.
  ///
  /// The algorithm works a follows:
  ///
  /// 1. Returns the selected platform if it matches any of the architectures.
  /// 2. Returns the host platform if it matches any of the architectures.
  /// 3. Returns the platform that matches all the architectures.
  ///
  /// If none of the above apply, this function returns a default platform. The
  /// candidates output argument differentiates between either no platforms
  /// supporting the given architecture or multiple platforms supporting the
  /// given architecture.
  lldb::PlatformSP GetOrCreate(llvm::ArrayRef<ArchSpec> archs,
                               const ArchSpec &process_host_arch,
                               std::vector<lldb::PlatformSP> &candidates);

  lldb::PlatformSP Create(llvm::StringRef name);

  /// Detect a binary in memory that will determine which Platform and
  /// DynamicLoader should be used in this target/process, and update
  /// the Platform/DynamicLoader.
  /// The binary will be loaded into the Target, or will be registered with
  /// the DynamicLoader so that it will be loaded at a later stage.  Returns
  /// true to indicate that this is a platform binary and has been
  /// loaded/registered, no further action should be taken by the caller.
  ///
  /// \param[in] process
  ///     Process read memory from, a Process must be provided.
  ///
  /// \param[in] addr
  ///     Address of a binary in memory.
  ///
  /// \param[in] notify
  ///     Whether ModulesDidLoad should be called, if a binary is loaded.
  ///     Caller may prefer to call ModulesDidLoad for multiple binaries
  ///     that were loaded at the same time.
  ///
  /// \return
  ///     Returns true if the binary was loaded in the target (or will be
  ///     via a DynamicLoader).  Returns false if the binary was not
  ///     loaded/registered, and the caller must load it into the target.
  bool LoadPlatformBinaryAndSetup(Process *process, lldb::addr_t addr,
                                  bool notify);

protected:
  typedef std::vector<lldb::PlatformSP> collection;
  mutable std::recursive_mutex m_mutex;
  collection m_platforms;
  lldb::PlatformSP m_selected_platform_sp;

private:
  PlatformList(const PlatformList &) = delete;
  const PlatformList &operator=(const PlatformList &) = delete;
};

class OptionGroupPlatformRSync : public lldb_private::OptionGroup {
public:
  OptionGroupPlatformRSync() = default;

  ~OptionGroupPlatformRSync() override = default;

  lldb_private::Status
  SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                 ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  // Instance variables to hold the values for command options.

  bool m_rsync;
  std::string m_rsync_opts;
  std::string m_rsync_prefix;
  bool m_ignores_remote_hostname;

private:
  OptionGroupPlatformRSync(const OptionGroupPlatformRSync &) = delete;
  const OptionGroupPlatformRSync &
  operator=(const OptionGroupPlatformRSync &) = delete;
};

class OptionGroupPlatformSSH : public lldb_private::OptionGroup {
public:
  OptionGroupPlatformSSH() = default;

  ~OptionGroupPlatformSSH() override = default;

  lldb_private::Status
  SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                 ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  // Instance variables to hold the values for command options.

  bool m_ssh;
  std::string m_ssh_opts;

private:
  OptionGroupPlatformSSH(const OptionGroupPlatformSSH &) = delete;
  const OptionGroupPlatformSSH &
  operator=(const OptionGroupPlatformSSH &) = delete;
};

class OptionGroupPlatformCaching : public lldb_private::OptionGroup {
public:
  OptionGroupPlatformCaching() = default;

  ~OptionGroupPlatformCaching() override = default;

  lldb_private::Status
  SetOptionValue(uint32_t option_idx, llvm::StringRef option_value,
                 ExecutionContext *execution_context) override;

  void OptionParsingStarting(ExecutionContext *execution_context) override;

  llvm::ArrayRef<OptionDefinition> GetDefinitions() override;

  // Instance variables to hold the values for command options.

  std::string m_cache_dir;

private:
  OptionGroupPlatformCaching(const OptionGroupPlatformCaching &) = delete;
  const OptionGroupPlatformCaching &
  operator=(const OptionGroupPlatformCaching &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_PLATFORM_H
