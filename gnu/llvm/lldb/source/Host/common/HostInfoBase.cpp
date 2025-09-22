//===-- HostInfoBase.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/Config.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostInfoBase.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#include <mutex>
#include <optional>
#include <thread>

using namespace lldb;
using namespace lldb_private;

namespace {
/// Contains the state of the HostInfoBase plugin.
struct HostInfoBaseFields {
  ~HostInfoBaseFields() {
    if (FileSystem::Instance().Exists(m_lldb_process_tmp_dir)) {
      // Remove the LLDB temporary directory if we have one. Set "recurse" to
      // true to all files that were created for the LLDB process can be
      // cleaned up.
      llvm::sys::fs::remove_directories(m_lldb_process_tmp_dir.GetPath());
    }
  }

  llvm::once_flag m_host_triple_once;
  llvm::Triple m_host_triple;

  llvm::once_flag m_host_arch_once;
  ArchSpec m_host_arch_32;
  ArchSpec m_host_arch_64;

  llvm::once_flag m_lldb_so_dir_once;
  FileSpec m_lldb_so_dir;
  llvm::once_flag m_lldb_support_exe_dir_once;
  FileSpec m_lldb_support_exe_dir;
  llvm::once_flag m_lldb_headers_dir_once;
  FileSpec m_lldb_headers_dir;
  llvm::once_flag m_lldb_clang_resource_dir_once;
  FileSpec m_lldb_clang_resource_dir;
  llvm::once_flag m_lldb_system_plugin_dir_once;
  FileSpec m_lldb_system_plugin_dir;
  llvm::once_flag m_lldb_user_plugin_dir_once;
  FileSpec m_lldb_user_plugin_dir;
  llvm::once_flag m_lldb_process_tmp_dir_once;
  FileSpec m_lldb_process_tmp_dir;
  llvm::once_flag m_lldb_global_tmp_dir_once;
  FileSpec m_lldb_global_tmp_dir;
};
} // namespace

static HostInfoBaseFields *g_fields = nullptr;
static HostInfoBase::SharedLibraryDirectoryHelper *g_shlib_dir_helper = nullptr;

void HostInfoBase::Initialize(SharedLibraryDirectoryHelper *helper) {
  g_shlib_dir_helper = helper;
  g_fields = new HostInfoBaseFields();
}

void HostInfoBase::Terminate() {
  g_shlib_dir_helper = nullptr;
  delete g_fields;
  g_fields = nullptr;
}

llvm::Triple HostInfoBase::GetTargetTriple() {
  llvm::call_once(g_fields->m_host_triple_once, []() {
    g_fields->m_host_triple = HostInfo::GetArchitecture().GetTriple();
  });
  return g_fields->m_host_triple;
}

const ArchSpec &HostInfoBase::GetArchitecture(ArchitectureKind arch_kind) {
  llvm::call_once(g_fields->m_host_arch_once, []() {
    HostInfo::ComputeHostArchitectureSupport(g_fields->m_host_arch_32,
                                             g_fields->m_host_arch_64);
  });

  // If an explicit 32 or 64-bit architecture was requested, return that.
  if (arch_kind == eArchKind32)
    return g_fields->m_host_arch_32;
  if (arch_kind == eArchKind64)
    return g_fields->m_host_arch_64;

  // Otherwise prefer the 64-bit architecture if it is valid.
  return (g_fields->m_host_arch_64.IsValid()) ? g_fields->m_host_arch_64
                                              : g_fields->m_host_arch_32;
}

std::optional<HostInfoBase::ArchitectureKind>
HostInfoBase::ParseArchitectureKind(llvm::StringRef kind) {
  return llvm::StringSwitch<std::optional<ArchitectureKind>>(kind)
      .Case(LLDB_ARCH_DEFAULT, eArchKindDefault)
      .Case(LLDB_ARCH_DEFAULT_32BIT, eArchKind32)
      .Case(LLDB_ARCH_DEFAULT_64BIT, eArchKind64)
      .Default(std::nullopt);
}

FileSpec HostInfoBase::GetShlibDir() {
  llvm::call_once(g_fields->m_lldb_so_dir_once, []() {
    if (!HostInfo::ComputeSharedLibraryDirectory(g_fields->m_lldb_so_dir))
      g_fields->m_lldb_so_dir = FileSpec();
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "shlib dir -> `{0}`", g_fields->m_lldb_so_dir);
  });
  return g_fields->m_lldb_so_dir;
}

FileSpec HostInfoBase::GetSupportExeDir() {
  llvm::call_once(g_fields->m_lldb_support_exe_dir_once, []() {
    if (!HostInfo::ComputeSupportExeDirectory(g_fields->m_lldb_support_exe_dir))
      g_fields->m_lldb_support_exe_dir = FileSpec();
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "support exe dir -> `{0}`", g_fields->m_lldb_support_exe_dir);
  });
  return g_fields->m_lldb_support_exe_dir;
}

FileSpec HostInfoBase::GetHeaderDir() {
  llvm::call_once(g_fields->m_lldb_headers_dir_once, []() {
    if (!HostInfo::ComputeHeaderDirectory(g_fields->m_lldb_headers_dir))
      g_fields->m_lldb_headers_dir = FileSpec();
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "header dir -> `{0}`", g_fields->m_lldb_headers_dir);
  });
  return g_fields->m_lldb_headers_dir;
}

FileSpec HostInfoBase::GetSystemPluginDir() {
  llvm::call_once(g_fields->m_lldb_system_plugin_dir_once, []() {
    if (!HostInfo::ComputeSystemPluginsDirectory(
            g_fields->m_lldb_system_plugin_dir))
      g_fields->m_lldb_system_plugin_dir = FileSpec();
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "system plugin dir -> `{0}`",
             g_fields->m_lldb_system_plugin_dir);
  });
  return g_fields->m_lldb_system_plugin_dir;
}

FileSpec HostInfoBase::GetUserPluginDir() {
  llvm::call_once(g_fields->m_lldb_user_plugin_dir_once, []() {
    if (!HostInfo::ComputeUserPluginsDirectory(
            g_fields->m_lldb_user_plugin_dir))
      g_fields->m_lldb_user_plugin_dir = FileSpec();
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "user plugin dir -> `{0}`", g_fields->m_lldb_user_plugin_dir);
  });
  return g_fields->m_lldb_user_plugin_dir;
}

FileSpec HostInfoBase::GetProcessTempDir() {
  llvm::call_once(g_fields->m_lldb_process_tmp_dir_once, []() {
    if (!HostInfo::ComputeProcessTempFileDirectory(
            g_fields->m_lldb_process_tmp_dir))
      g_fields->m_lldb_process_tmp_dir = FileSpec();
    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "process temp dir -> `{0}`",
             g_fields->m_lldb_process_tmp_dir);
  });
  return g_fields->m_lldb_process_tmp_dir;
}

FileSpec HostInfoBase::GetGlobalTempDir() {
  llvm::call_once(g_fields->m_lldb_global_tmp_dir_once, []() {
    if (!HostInfo::ComputeGlobalTempFileDirectory(
            g_fields->m_lldb_global_tmp_dir))
      g_fields->m_lldb_global_tmp_dir = FileSpec();

    Log *log = GetLog(LLDBLog::Host);
    LLDB_LOG(log, "global temp dir -> `{0}`", g_fields->m_lldb_global_tmp_dir);
  });
  return g_fields->m_lldb_global_tmp_dir;
}

ArchSpec HostInfoBase::GetAugmentedArchSpec(llvm::StringRef triple) {
  if (triple.empty())
    return ArchSpec();
  llvm::Triple normalized_triple(llvm::Triple::normalize(triple));
  if (!ArchSpec::ContainsOnlyArch(normalized_triple))
    return ArchSpec(triple);

  if (auto kind = HostInfo::ParseArchitectureKind(triple))
    return HostInfo::GetArchitecture(*kind);

  llvm::Triple host_triple(llvm::sys::getDefaultTargetTriple());

  if (normalized_triple.getVendorName().empty())
    normalized_triple.setVendor(host_triple.getVendor());
  if (normalized_triple.getOSName().empty())
    normalized_triple.setOS(host_triple.getOS());
  if (normalized_triple.getEnvironmentName().empty() &&
      !host_triple.getEnvironmentName().empty())
    normalized_triple.setEnvironment(host_triple.getEnvironment());
  return ArchSpec(normalized_triple);
}

bool HostInfoBase::ComputePathRelativeToLibrary(FileSpec &file_spec,
                                                llvm::StringRef dir) {
  Log *log = GetLog(LLDBLog::Host);

  FileSpec lldb_file_spec = GetShlibDir();
  if (!lldb_file_spec)
    return false;

  std::string raw_path = lldb_file_spec.GetPath();
  LLDB_LOG(
      log,
      "Attempting to derive the path {0} relative to liblldb install path: {1}",
      dir, raw_path);

  // Drop bin (windows) or lib
  llvm::StringRef parent_path = llvm::sys::path::parent_path(raw_path);
  if (parent_path.empty()) {
    LLDB_LOG(log, "Failed to find liblldb within the shared lib path");
    return false;
  }

  raw_path = (parent_path + dir).str();
  LLDB_LOG(log, "Derived the path as: {0}", raw_path);
  file_spec.SetDirectory(raw_path);
  return (bool)file_spec.GetDirectory();
}

bool HostInfoBase::ComputeSharedLibraryDirectory(FileSpec &file_spec) {
  // To get paths related to LLDB we get the path to the executable that
  // contains this function. On MacOSX this will be "LLDB.framework/.../LLDB".
  // On other posix systems, we will get .../lib(64|32)?/liblldb.so.

  FileSpec lldb_file_spec(Host::GetModuleFileSpecForHostAddress(
      reinterpret_cast<void *>(HostInfoBase::ComputeSharedLibraryDirectory)));

  if (g_shlib_dir_helper)
    g_shlib_dir_helper(lldb_file_spec);

  // Remove the filename so that this FileSpec only represents the directory.
  file_spec.SetDirectory(lldb_file_spec.GetDirectory());

  return (bool)file_spec.GetDirectory();
}

bool HostInfoBase::ComputeSupportExeDirectory(FileSpec &file_spec) {
  file_spec = GetShlibDir();
  return bool(file_spec);
}

bool HostInfoBase::ComputeProcessTempFileDirectory(FileSpec &file_spec) {
  FileSpec temp_file_spec;
  if (!HostInfo::ComputeGlobalTempFileDirectory(temp_file_spec))
    return false;

  std::string pid_str{llvm::to_string(Host::GetCurrentProcessID())};
  temp_file_spec.AppendPathComponent(pid_str);
  if (llvm::sys::fs::create_directory(temp_file_spec.GetPath()))
    return false;

  file_spec.SetDirectory(temp_file_spec.GetPathAsConstString());
  return true;
}

bool HostInfoBase::ComputeTempFileBaseDirectory(FileSpec &file_spec) {
  llvm::SmallVector<char, 16> tmpdir;
  llvm::sys::path::system_temp_directory(/*ErasedOnReboot*/ true, tmpdir);
  file_spec = FileSpec(std::string(tmpdir.data(), tmpdir.size()));
  FileSystem::Instance().Resolve(file_spec);
  return true;
}

bool HostInfoBase::ComputeGlobalTempFileDirectory(FileSpec &file_spec) {
  file_spec.Clear();

  FileSpec temp_file_spec;
  if (!HostInfo::ComputeTempFileBaseDirectory(temp_file_spec))
    return false;

  temp_file_spec.AppendPathComponent("lldb");
  if (llvm::sys::fs::create_directory(temp_file_spec.GetPath()))
    return false;

  file_spec.SetDirectory(temp_file_spec.GetPathAsConstString());
  return true;
}

bool HostInfoBase::ComputeHeaderDirectory(FileSpec &file_spec) {
  // TODO(zturner): Figure out how to compute the header directory for all
  // platforms.
  return false;
}

bool HostInfoBase::ComputeSystemPluginsDirectory(FileSpec &file_spec) {
  // TODO(zturner): Figure out how to compute the system plugins directory for
  // all platforms.
  return false;
}

bool HostInfoBase::ComputeUserPluginsDirectory(FileSpec &file_spec) {
  // TODO(zturner): Figure out how to compute the user plugins directory for
  // all platforms.
  return false;
}

void HostInfoBase::ComputeHostArchitectureSupport(ArchSpec &arch_32,
                                                  ArchSpec &arch_64) {
  llvm::Triple triple(llvm::sys::getProcessTriple());

  arch_32.Clear();
  arch_64.Clear();

  switch (triple.getArch()) {
  default:
    arch_32.SetTriple(triple);
    break;

  case llvm::Triple::aarch64:
  case llvm::Triple::ppc64:
  case llvm::Triple::ppc64le:
  case llvm::Triple::x86_64:
  case llvm::Triple::riscv64:
  case llvm::Triple::loongarch64:
    arch_64.SetTriple(triple);
    arch_32.SetTriple(triple.get32BitArchVariant());
    break;

  case llvm::Triple::mips64:
  case llvm::Triple::mips64el:
  case llvm::Triple::sparcv9:
  case llvm::Triple::systemz:
    arch_64.SetTriple(triple);
    break;
  }
}
