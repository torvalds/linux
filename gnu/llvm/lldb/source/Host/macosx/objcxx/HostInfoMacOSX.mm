//===-- HostInfoMacOSX.mm ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/macosx/HostInfoMacOSX.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/Args.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Timer.h"

#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

// C++ Includes
#include <optional>
#include <string>

// C inclues
#include <cstdlib>
#include <sys/sysctl.h>
#include <sys/syslimits.h>
#include <sys/types.h>
#include <uuid/uuid.h>

// Objective-C/C++ includes
#include <AvailabilityMacros.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Foundation/Foundation.h>
#include <mach-o/dyld.h>
#if defined(MAC_OS_X_VERSION_MIN_REQUIRED) && \
  MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_12_0
#if __has_include(<mach-o/dyld_introspection.h>)
#include <mach-o/dyld_introspection.h>
#define SDK_HAS_NEW_DYLD_INTROSPECTION_SPIS
#endif
#endif
#include <objc/objc-auto.h>

// These are needed when compiling on systems
// that do not yet have these definitions
#ifndef CPU_SUBTYPE_X86_64_H
#define CPU_SUBTYPE_X86_64_H ((cpu_subtype_t)8)
#endif
#ifndef CPU_TYPE_ARM64
#define CPU_TYPE_ARM64 (CPU_TYPE_ARM | CPU_ARCH_ABI64)
#endif

#ifndef CPU_TYPE_ARM64_32
#define CPU_ARCH_ABI64_32 0x02000000
#define CPU_TYPE_ARM64_32 (CPU_TYPE_ARM | CPU_ARCH_ABI64_32)
#endif

#include <TargetConditionals.h> // for TARGET_OS_TV, TARGET_OS_WATCH

using namespace lldb_private;

std::optional<std::string> HostInfoMacOSX::GetOSBuildString() {
  int mib[2] = {CTL_KERN, KERN_OSVERSION};
  char cstr[PATH_MAX];
  size_t cstr_len = sizeof(cstr);
  if (::sysctl(mib, 2, cstr, &cstr_len, NULL, 0) == 0)
    return std::string(cstr, cstr_len - 1);

  return std::nullopt;
}

static void ParseOSVersion(llvm::VersionTuple &version, NSString *Key) {
  @autoreleasepool {
    NSDictionary *version_info =
      [NSDictionary dictionaryWithContentsOfFile:
       @"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *version_value = [version_info objectForKey: Key];
    const char *version_str = [version_value UTF8String];
    version.tryParse(version_str);
  }
}

llvm::VersionTuple HostInfoMacOSX::GetOSVersion() {
  static llvm::VersionTuple g_version;
  if (g_version.empty())
    ParseOSVersion(g_version, @"ProductVersion");
  return g_version;
}

llvm::VersionTuple HostInfoMacOSX::GetMacCatalystVersion() {
  static llvm::VersionTuple g_version;
  if (g_version.empty())
    ParseOSVersion(g_version, @"iOSSupportVersion");
  return g_version;
}


FileSpec HostInfoMacOSX::GetProgramFileSpec() {
  static FileSpec g_program_filespec;
  if (!g_program_filespec) {
    char program_fullpath[PATH_MAX];
    // If DST is NULL, then return the number of bytes needed.
    uint32_t len = sizeof(program_fullpath);
    int err = _NSGetExecutablePath(program_fullpath, &len);
    if (err == 0)
      g_program_filespec.SetFile(program_fullpath, FileSpec::Style::native);
    else if (err == -1) {
      char *large_program_fullpath = (char *)::malloc(len + 1);

      err = _NSGetExecutablePath(large_program_fullpath, &len);
      if (err == 0)
        g_program_filespec.SetFile(large_program_fullpath,
                                   FileSpec::Style::native);

      ::free(large_program_fullpath);
    }
  }
  return g_program_filespec;
}

bool HostInfoMacOSX::ComputeSupportExeDirectory(FileSpec &file_spec) {
  FileSpec lldb_file_spec = GetShlibDir();
  if (!lldb_file_spec)
    return false;

  std::string raw_path = lldb_file_spec.GetPath();

  size_t framework_pos = raw_path.find("LLDB.framework");
  if (framework_pos != std::string::npos) {
    framework_pos += strlen("LLDB.framework");
#if TARGET_OS_IPHONE
    // Shallow bundle
    raw_path.resize(framework_pos);
#else
    // Normal bundle
    raw_path.resize(framework_pos);
    raw_path.append("/Resources");
#endif
  } else {
    // Find the bin path relative to the lib path where the cmake-based
    // OS X .dylib lives.  This is not going to work if the bin and lib
    // dir are not both in the same dir.
    //
    // It is not going to work to do it by the executable path either,
    // as in the case of a python script, the executable is python, not
    // the lldb driver.
    raw_path.append("/../bin");
    FileSpec support_dir_spec(raw_path);
    FileSystem::Instance().Resolve(support_dir_spec);
    if (!FileSystem::Instance().IsDirectory(support_dir_spec)) {
      Log *log = GetLog(LLDBLog::Host);
      LLDB_LOG(log, "failed to find support directory");
      return false;
    }

    // Get normalization from support_dir_spec.  Note the FileSpec resolve
    // does not remove '..' in the path.
    char *const dir_realpath =
        realpath(support_dir_spec.GetPath().c_str(), NULL);
    if (dir_realpath) {
      raw_path = dir_realpath;
      free(dir_realpath);
    } else {
      raw_path = support_dir_spec.GetPath();
    }
  }

  file_spec.SetDirectory(raw_path);
  return (bool)file_spec.GetDirectory();
}

bool HostInfoMacOSX::ComputeHeaderDirectory(FileSpec &file_spec) {
  FileSpec lldb_file_spec = GetShlibDir();
  if (!lldb_file_spec)
    return false;

  std::string raw_path = lldb_file_spec.GetPath();

  size_t framework_pos = raw_path.find("LLDB.framework");
  if (framework_pos != std::string::npos) {
    framework_pos += strlen("LLDB.framework");
    raw_path.resize(framework_pos);
    raw_path.append("/Headers");
  }
  file_spec.SetDirectory(raw_path);
  return true;
}

bool HostInfoMacOSX::ComputeSystemPluginsDirectory(FileSpec &file_spec) {
  FileSpec lldb_file_spec = GetShlibDir();
  if (!lldb_file_spec)
    return false;

  std::string raw_path = lldb_file_spec.GetPath();

  size_t framework_pos = raw_path.find("LLDB.framework");
  if (framework_pos == std::string::npos)
    return false;

  framework_pos += strlen("LLDB.framework");
  raw_path.resize(framework_pos);
  raw_path.append("/Resources/PlugIns");
  file_spec.SetDirectory(raw_path);
  return true;
}

bool HostInfoMacOSX::ComputeUserPluginsDirectory(FileSpec &file_spec) {
  FileSpec temp_file("~/Library/Application Support/LLDB/PlugIns");
  FileSystem::Instance().Resolve(temp_file);
  file_spec.SetDirectory(temp_file.GetPathAsConstString());
  return true;
}

void HostInfoMacOSX::ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                                    ArchSpec &arch_64) {
  // All apple systems support 32 bit execution.
  uint32_t cputype, cpusubtype;
  uint32_t is_64_bit_capable = false;
  size_t len = sizeof(cputype);
  ArchSpec host_arch;
  // These will tell us about the kernel architecture, which even on a 64
  // bit machine can be 32 bit...
  if (::sysctlbyname("hw.cputype", &cputype, &len, NULL, 0) == 0) {
    len = sizeof(cpusubtype);
    if (::sysctlbyname("hw.cpusubtype", &cpusubtype, &len, NULL, 0) != 0)
      cpusubtype = CPU_TYPE_ANY;

    len = sizeof(is_64_bit_capable);
    ::sysctlbyname("hw.cpu64bit_capable", &is_64_bit_capable, &len, NULL, 0);

    if (cputype == CPU_TYPE_ARM64 && cpusubtype == CPU_SUBTYPE_ARM64E) {
      // The arm64e architecture is a preview. Pretend the host architecture
      // is arm64.
      cpusubtype = CPU_SUBTYPE_ARM64_ALL;
    }

    if (is_64_bit_capable) {
      if (cputype & CPU_ARCH_ABI64) {
        // We have a 64 bit kernel on a 64 bit system
        arch_64.SetArchitecture(eArchTypeMachO, cputype, cpusubtype);
      } else {
        // We have a 64 bit kernel that is returning a 32 bit cputype, the
        // cpusubtype will be correct as if it were for a 64 bit architecture
        arch_64.SetArchitecture(eArchTypeMachO, cputype | CPU_ARCH_ABI64,
                                cpusubtype);
      }

      // Now we need modify the cpusubtype for the 32 bit slices.
      uint32_t cpusubtype32 = cpusubtype;
#if defined(__i386__) || defined(__x86_64__)
      if (cpusubtype == CPU_SUBTYPE_486 || cpusubtype == CPU_SUBTYPE_X86_64_H)
        cpusubtype32 = CPU_SUBTYPE_I386_ALL;
#elif defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
      if (cputype == CPU_TYPE_ARM || cputype == CPU_TYPE_ARM64)
        cpusubtype32 = CPU_SUBTYPE_ARM_V7S;
#endif
      arch_32.SetArchitecture(eArchTypeMachO, cputype & ~(CPU_ARCH_MASK),
                              cpusubtype32);

      if (cputype == CPU_TYPE_ARM ||
          cputype == CPU_TYPE_ARM64 ||
          cputype == CPU_TYPE_ARM64_32) {
// When running on a watch or tv, report the host os correctly
#if defined(TARGET_OS_TV) && TARGET_OS_TV == 1
        arch_32.GetTriple().setOS(llvm::Triple::TvOS);
        arch_64.GetTriple().setOS(llvm::Triple::TvOS);
#elif defined(TARGET_OS_BRIDGE) && TARGET_OS_BRIDGE == 1
        arch_32.GetTriple().setOS(llvm::Triple::BridgeOS);
        arch_64.GetTriple().setOS(llvm::Triple::BridgeOS);
#elif defined(TARGET_OS_WATCHOS) && TARGET_OS_WATCHOS == 1
        arch_32.GetTriple().setOS(llvm::Triple::WatchOS);
        arch_64.GetTriple().setOS(llvm::Triple::WatchOS);
#elif defined(TARGET_OS_XR) && TARGET_OS_XR == 1
        arch_32.GetTriple().setOS(llvm::Triple::XROS);
        arch_64.GetTriple().setOS(llvm::Triple::XROS);
#elif defined(TARGET_OS_OSX) && TARGET_OS_OSX == 1
        arch_32.GetTriple().setOS(llvm::Triple::MacOSX);
        arch_64.GetTriple().setOS(llvm::Triple::MacOSX);
#else
        arch_32.GetTriple().setOS(llvm::Triple::IOS);
        arch_64.GetTriple().setOS(llvm::Triple::IOS);
#endif
      } else {
        arch_32.GetTriple().setOS(llvm::Triple::MacOSX);
        arch_64.GetTriple().setOS(llvm::Triple::MacOSX);
      }
    } else {
      // We have a 32 bit kernel on a 32 bit system
      arch_32.SetArchitecture(eArchTypeMachO, cputype, cpusubtype);
#if defined(TARGET_OS_WATCH) && TARGET_OS_WATCH == 1
      arch_32.GetTriple().setOS(llvm::Triple::WatchOS);
#else
      arch_32.GetTriple().setOS(llvm::Triple::IOS);
#endif
      arch_64.Clear();
    }
  }
}

/// Return and cache $DEVELOPER_DIR if it is set and exists.
static std::string GetEnvDeveloperDir() {
  static std::string g_env_developer_dir;
  static std::once_flag g_once_flag;
  std::call_once(g_once_flag, [&]() {
    if (const char *developer_dir_env_var = getenv("DEVELOPER_DIR")) {
      FileSpec fspec(developer_dir_env_var);
      if (FileSystem::Instance().Exists(fspec))
        g_env_developer_dir = fspec.GetPath();
    }});
  return g_env_developer_dir;
}

FileSpec HostInfoMacOSX::GetXcodeContentsDirectory() {
  static FileSpec g_xcode_contents_path;
  static std::once_flag g_once_flag;
  std::call_once(g_once_flag, [&]() {
    // Try the shlib dir first.
    if (FileSpec fspec = HostInfo::GetShlibDir()) {
      if (FileSystem::Instance().Exists(fspec)) {
        std::string xcode_contents_dir =
            XcodeSDK::FindXcodeContentsDirectoryInPath(fspec.GetPath());
        if (!xcode_contents_dir.empty()) {
          g_xcode_contents_path = FileSpec(xcode_contents_dir);
          return;
        }
      }
    }

    llvm::SmallString<128> env_developer_dir(GetEnvDeveloperDir());
    if (!env_developer_dir.empty()) {
      llvm::sys::path::append(env_developer_dir, "Contents");
      std::string xcode_contents_dir =
          XcodeSDK::FindXcodeContentsDirectoryInPath(env_developer_dir);
      if (!xcode_contents_dir.empty()) {
        g_xcode_contents_path = FileSpec(xcode_contents_dir);
        return;
      }
    }

    auto sdk_path_or_err =
        HostInfo::GetSDKRoot(SDKOptions{XcodeSDK::GetAnyMacOS()});
    if (!sdk_path_or_err) {
      Log *log = GetLog(LLDBLog::Host);
      LLDB_LOG_ERROR(log, sdk_path_or_err.takeError(),
                     "Error while searching for Xcode SDK: {0}");
      return;
    }
    FileSpec fspec(*sdk_path_or_err);
    if (fspec) {
      if (FileSystem::Instance().Exists(fspec)) {
        std::string xcode_contents_dir =
            XcodeSDK::FindXcodeContentsDirectoryInPath(fspec.GetPath());
        if (!xcode_contents_dir.empty()) {
          g_xcode_contents_path = FileSpec(xcode_contents_dir);
          return;
        }
      }
    }
  });
  return g_xcode_contents_path;
}

lldb_private::FileSpec HostInfoMacOSX::GetXcodeDeveloperDirectory() {
  static lldb_private::FileSpec g_developer_directory;
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    if (FileSpec fspec = GetXcodeContentsDirectory()) {
      fspec.AppendPathComponent("Developer");
      if (FileSystem::Instance().Exists(fspec))
        g_developer_directory = fspec;
    }
  });
  return g_developer_directory;
}

static llvm::Expected<std::string>
xcrun(const std::string &sdk, llvm::ArrayRef<llvm::StringRef> arguments,
      llvm::StringRef developer_dir = "") {
  Args args;
  if (!developer_dir.empty()) {
    args.AppendArgument("/usr/bin/env");
    args.AppendArgument("DEVELOPER_DIR=" + developer_dir.str());
  }
  args.AppendArgument("/usr/bin/xcrun");
  args.AppendArgument("--sdk");
  args.AppendArgument(sdk);
  for (auto arg: arguments)
    args.AppendArgument(arg);

  Log *log = GetLog(LLDBLog::Host);
  if (log) {
    std::string cmdstr;
    args.GetCommandString(cmdstr);
    LLDB_LOG(log, "GetXcodeSDK() running shell cmd '{0}'", cmdstr);
  }

  int status = 0;
  int signo = 0;
  std::string output_str;
  // The first time after Xcode was updated or freshly installed,
  // xcrun can take surprisingly long to build up its database.
  auto timeout = std::chrono::seconds(60);
  bool run_in_shell = false;
  lldb_private::Status error = Host::RunShellCommand(
      args, FileSpec(), &status, &signo, &output_str, timeout, run_in_shell);

  // Check that xcrun returned something useful.
  if (error.Fail()) {
    // Catastrophic error.
    LLDB_LOG(log, "xcrun failed to execute: {0}", error);
    return error.ToError();
  }
  if (status != 0) {
    // xcrun didn't find a matching SDK. Not an error, we'll try
    // different spellings.
    LLDB_LOG(log, "xcrun returned exit code {0}", status);
    if (!output_str.empty())
      LLDB_LOG(log, "xcrun output was:\n{0}", output_str);
    return "";
  }
  if (output_str.empty()) {
    LLDB_LOG(log, "xcrun returned no results");
    return "";
  }

  // Convert to a StringRef so we can manipulate the string without modifying
  // the underlying data.
  llvm::StringRef output(output_str);

  // Remove any trailing newline characters.
  output = output.rtrim();

  // Strip any leading newline characters and everything before them.
  const size_t last_newline = output.rfind('\n');
  if (last_newline != llvm::StringRef::npos)
    output = output.substr(last_newline + 1);

  return output.str();
}

static llvm::Expected<std::string> GetXcodeSDK(XcodeSDK sdk) {
  XcodeSDK::Info info = sdk.Parse();
  std::string sdk_name = XcodeSDK::GetCanonicalName(info);
  if (sdk_name.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Unrecognized SDK type: " + sdk.GetString());

  Log *log = GetLog(LLDBLog::Host);

  auto find_sdk =
      [](const std::string &sdk_name) -> llvm::Expected<std::string> {
    llvm::SmallVector<llvm::StringRef, 1> show_sdk_path = {"--show-sdk-path"};
    // Invoke xcrun with the developer dir specified in the environment.
    std::string developer_dir = GetEnvDeveloperDir();
    if (!developer_dir.empty()) {
      // Don't fallback if DEVELOPER_DIR was set.
      return xcrun(sdk_name, show_sdk_path, developer_dir);
    }

    // Invoke xcrun with the shlib dir.
    if (FileSpec fspec = HostInfo::GetShlibDir()) {
      if (FileSystem::Instance().Exists(fspec)) {
        llvm::SmallString<0> shlib_developer_dir(
            XcodeSDK::FindXcodeContentsDirectoryInPath(fspec.GetPath()));
        llvm::sys::path::append(shlib_developer_dir, "Developer");
        if (FileSystem::Instance().Exists(shlib_developer_dir)) {
          auto sdk = xcrun(sdk_name, show_sdk_path, shlib_developer_dir);
          if (!sdk)
            return sdk.takeError();
          if (!sdk->empty())
            return sdk;
        }
      }
    }

    // Invoke xcrun without a developer dir as a last resort.
    return xcrun(sdk_name, show_sdk_path);
  };

  auto path_or_err = find_sdk(sdk_name);
  if (!path_or_err)
    return path_or_err.takeError();
  std::string path = *path_or_err;
  while (path.empty()) {
    // Try an alternate spelling of the name ("macosx10.9internal").
    if (info.type == XcodeSDK::Type::MacOSX && !info.version.empty() &&
        info.internal) {
      llvm::StringRef fixed(sdk_name);
      if (fixed.consume_back(".internal"))
        sdk_name = fixed.str() + "internal";
      path_or_err = find_sdk(sdk_name);
      if (!path_or_err)
        return path_or_err.takeError();
      path = *path_or_err;
      if (!path.empty())
        break;
    }
    LLDB_LOG(log, "Couldn't find SDK {0} on host", sdk_name);

    // Try without the version.
    if (!info.version.empty()) {
      info.version = {};
      sdk_name = XcodeSDK::GetCanonicalName(info);
      path_or_err = find_sdk(sdk_name);
      if (!path_or_err)
        return path_or_err.takeError();
      path = *path_or_err;
      if (!path.empty())
        break;
    }

    LLDB_LOG(log, "Couldn't find any matching SDK on host");
    return "";
  }

  // Whatever is left in output should be a valid path.
  if (!FileSystem::Instance().Exists(path)) {
    LLDB_LOG(log, "SDK returned by xcrun doesn't exist");
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "SDK returned by xcrun doesn't exist");
  }
  return path;
}

namespace {
struct ErrorOrPath {
  std::string str;
  bool is_error;
};
} // namespace

static llvm::Expected<llvm::StringRef>
find_cached_path(llvm::StringMap<ErrorOrPath> &cache, std::mutex &mutex,
                 llvm::StringRef key,
                 std::function<llvm::Expected<std::string>(void)> compute) {
  std::lock_guard<std::mutex> guard(mutex);
  LLDB_SCOPED_TIMER();

  auto it = cache.find(key);
  if (it != cache.end()) {
    if (it->second.is_error)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     it->second.str);
    return it->second.str;
  }
  auto path_or_err = compute();
  if (!path_or_err) {
    std::string error = toString(path_or_err.takeError());
    cache.insert({key, {error, true}});
    return llvm::createStringError(llvm::inconvertibleErrorCode(), error);
  }
  auto it_new = cache.insert({key, {*path_or_err, false}});
  return it_new.first->second.str;
}

llvm::Expected<llvm::StringRef> HostInfoMacOSX::GetSDKRoot(SDKOptions options) {
  static llvm::StringMap<ErrorOrPath> g_sdk_path;
  static std::mutex g_sdk_path_mutex;
  if (!options.XcodeSDKSelection)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "XcodeSDK not specified");
  XcodeSDK sdk = *options.XcodeSDKSelection;
  auto key = sdk.GetString();
  return find_cached_path(g_sdk_path, g_sdk_path_mutex, key, [&](){
    return GetXcodeSDK(sdk);
  });
}

llvm::Expected<llvm::StringRef>
HostInfoMacOSX::FindSDKTool(XcodeSDK sdk, llvm::StringRef tool) {
  static llvm::StringMap<ErrorOrPath> g_tool_path;
  static std::mutex g_tool_path_mutex;
  std::string key;
  llvm::raw_string_ostream(key) << sdk.GetString() << ":" << tool;
  return find_cached_path(
      g_tool_path, g_tool_path_mutex, key,
      [&]() -> llvm::Expected<std::string> {
        std::string sdk_name = XcodeSDK::GetCanonicalName(sdk.Parse());
        if (sdk_name.empty())
          return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                         "Unrecognized SDK type: " +
                                             sdk.GetString());
        llvm::SmallVector<llvm::StringRef, 2> find = {"-find", tool};
        return xcrun(sdk_name, find);
      });
}

namespace {
struct dyld_shared_cache_dylib_text_info {
  uint64_t version; // current version 1
  // following fields all exist in version 1
  uint64_t loadAddressUnslid;
  uint64_t textSegmentSize;
  uuid_t dylibUuid;
  const char *path; // pointer invalid at end of iterations
  // following fields all exist in version 2
  uint64_t textSegmentOffset; // offset from start of cache
};
typedef struct dyld_shared_cache_dylib_text_info
    dyld_shared_cache_dylib_text_info;
}

extern "C" int dyld_shared_cache_iterate_text(
    const uuid_t cacheUuid,
    void (^callback)(const dyld_shared_cache_dylib_text_info *info));
extern "C" uint8_t *_dyld_get_shared_cache_range(size_t *length);
extern "C" bool _dyld_get_shared_cache_uuid(uuid_t uuid);

namespace {
class SharedCacheInfo {
public:
  const UUID &GetUUID() const { return m_uuid; }
  const llvm::StringMap<SharedCacheImageInfo> &GetImages() const {
    return m_images;
  }

  SharedCacheInfo();

private:
  bool CreateSharedCacheInfoWithInstrospectionSPIs();

  llvm::StringMap<SharedCacheImageInfo> m_images;
  UUID m_uuid;
};
}

bool SharedCacheInfo::CreateSharedCacheInfoWithInstrospectionSPIs() {
#if defined(SDK_HAS_NEW_DYLD_INTROSPECTION_SPIS)
  dyld_process_t dyld_process = dyld_process_create_for_current_task();
  if (!dyld_process)
    return false;

  dyld_process_snapshot_t snapshot =
      dyld_process_snapshot_create_for_process(dyld_process, nullptr);
  if (!snapshot)
    return false;

  auto on_exit =
      llvm::make_scope_exit([&]() { dyld_process_snapshot_dispose(snapshot); });

  dyld_shared_cache_t shared_cache =
      dyld_process_snapshot_get_shared_cache(snapshot);
  if (!shared_cache)
    return false;

  dyld_shared_cache_for_each_image(shared_cache, ^(dyld_image_t image) {
    __block uint64_t minVmAddr = UINT64_MAX;
    __block uint64_t maxVmAddr = 0;
    uuid_t uuidStore;
    __block uuid_t *uuid = &uuidStore;

    dyld_image_for_each_segment_info(
        image,
        ^(const char *segmentName, uint64_t vmAddr, uint64_t vmSize, int perm) {
          minVmAddr = std::min(minVmAddr, vmAddr);
          maxVmAddr = std::max(maxVmAddr, vmAddr + vmSize);
          dyld_image_copy_uuid(image, uuid);
        });
    assert(minVmAddr != UINT_MAX);
    assert(maxVmAddr != 0);
    m_images[dyld_image_get_installname(image)] = SharedCacheImageInfo{
        UUID(uuid, 16), std::make_shared<DataBufferUnowned>(
                            (uint8_t *)minVmAddr, maxVmAddr - minVmAddr)};
  });
  return true;
#endif
  return false;
}

SharedCacheInfo::SharedCacheInfo() {
  if (CreateSharedCacheInfoWithInstrospectionSPIs())
    return;

  size_t shared_cache_size;
  uint8_t *shared_cache_start =
      _dyld_get_shared_cache_range(&shared_cache_size);
  uuid_t dsc_uuid;
  _dyld_get_shared_cache_uuid(dsc_uuid);
  m_uuid = UUID(dsc_uuid);

  dyld_shared_cache_iterate_text(
      dsc_uuid, ^(const dyld_shared_cache_dylib_text_info *info) {
        m_images[info->path] = SharedCacheImageInfo{
            UUID(info->dylibUuid, 16),
            std::make_shared<DataBufferUnowned>(
                shared_cache_start + info->textSegmentOffset,
                shared_cache_size - info->textSegmentOffset)};
      });
}

SharedCacheImageInfo
HostInfoMacOSX::GetSharedCacheImageInfo(llvm::StringRef image_name) {
  static SharedCacheInfo g_shared_cache_info;
  return g_shared_cache_info.GetImages().lookup(image_name);
}
