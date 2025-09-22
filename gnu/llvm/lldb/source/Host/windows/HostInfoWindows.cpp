//===-- HostInfoWindows.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/windows.h"

#include <objbase.h>

#include <mutex>
#include <optional>

#include "lldb/Host/windows/HostInfoWindows.h"
#include "lldb/Host/windows/PosixApi.h"
#include "lldb/Utility/UserIDResolver.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

using namespace lldb_private;

namespace {
class WindowsUserIDResolver : public UserIDResolver {
protected:
  std::optional<std::string> DoGetUserName(id_t uid) override {
    return std::nullopt;
  }
  std::optional<std::string> DoGetGroupName(id_t gid) override {
    return std::nullopt;
  }
};
} // namespace

FileSpec HostInfoWindows::m_program_filespec;

void HostInfoWindows::Initialize(SharedLibraryDirectoryHelper *helper) {
  ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  HostInfoBase::Initialize(helper);
}

void HostInfoWindows::Terminate() {
  HostInfoBase::Terminate();
  ::CoUninitialize();
}

size_t HostInfoWindows::GetPageSize() {
  SYSTEM_INFO systemInfo;
  GetNativeSystemInfo(&systemInfo);
  return systemInfo.dwPageSize;
}

llvm::VersionTuple HostInfoWindows::GetOSVersion() {
  OSVERSIONINFOEX info;

  ZeroMemory(&info, sizeof(OSVERSIONINFOEX));
  info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
#pragma warning(push)
#pragma warning(disable : 4996)
  // Starting with Microsoft SDK for Windows 8.1, this function is deprecated
  // in favor of the new Windows Version Helper APIs.  Since we don't specify a
  // minimum SDK version, it's easier to simply disable the warning rather than
  // try to support both APIs.
  if (GetVersionEx((LPOSVERSIONINFO)&info) == 0)
    return llvm::VersionTuple();
#pragma warning(pop)

  return llvm::VersionTuple(info.dwMajorVersion, info.dwMinorVersion,
                            info.wServicePackMajor);
}

std::optional<std::string> HostInfoWindows::GetOSBuildString() {
  llvm::VersionTuple version = GetOSVersion();
  if (version.empty())
    return std::nullopt;

  return "Windows NT " + version.getAsString();
}

std::optional<std::string> HostInfoWindows::GetOSKernelDescription() {
  return GetOSBuildString();
}

bool HostInfoWindows::GetHostname(std::string &s) {
  wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1];
  DWORD dwSize = MAX_COMPUTERNAME_LENGTH + 1;
  if (!::GetComputerNameW(buffer, &dwSize))
    return false;

  // The conversion requires an empty string.
  s.clear();
  return llvm::convertWideToUTF8(buffer, s);
}

FileSpec HostInfoWindows::GetProgramFileSpec() {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, []() {
    std::vector<wchar_t> buffer(PATH_MAX);
    ::GetModuleFileNameW(NULL, buffer.data(), buffer.size());
    std::string path;
    llvm::convertWideToUTF8(buffer.data(), path);
    m_program_filespec.SetFile(path, FileSpec::Style::native);
  });
  return m_program_filespec;
}

FileSpec HostInfoWindows::GetDefaultShell() {
  // Try to retrieve ComSpec from the environment. On the rare occasion
  // that it fails, try a well-known path for ComSpec instead.

  std::string shell;
  if (GetEnvironmentVar("ComSpec", shell))
    return FileSpec(shell);

  return FileSpec("C:\\Windows\\system32\\cmd.exe");
}

bool HostInfoWindows::GetEnvironmentVar(const std::string &var_name,
                                        std::string &var) {
  std::wstring wvar_name;
  if (!llvm::ConvertUTF8toWide(var_name, wvar_name))
    return false;

  if (const wchar_t *wvar = _wgetenv(wvar_name.c_str()))
    return llvm::convertWideToUTF8(wvar, var);
  return false;
}

static llvm::ManagedStatic<WindowsUserIDResolver> g_user_id_resolver;

UserIDResolver &HostInfoWindows::GetUserIDResolver() {
  return *g_user_id_resolver;
}
