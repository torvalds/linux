//===-- ProcessLauncherWindows.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/ProcessLauncherWindows.h"
#include "lldb/Host/HostProcess.h"
#include "lldb/Host/ProcessLaunchInfo.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Program.h"

#include <string>
#include <vector>

using namespace lldb;
using namespace lldb_private;

static void CreateEnvironmentBuffer(const Environment &env,
                                    std::vector<char> &buffer) {
  // The buffer is a list of null-terminated UTF-16 strings, followed by an
  // extra L'\0' (two bytes of 0).  An empty environment must have one
  // empty string, followed by an extra L'\0'.
  for (const auto &KV : env) {
    std::wstring warg;
    if (llvm::ConvertUTF8toWide(Environment::compose(KV), warg)) {
      buffer.insert(
          buffer.end(), reinterpret_cast<const char *>(warg.c_str()),
          reinterpret_cast<const char *>(warg.c_str() + warg.size() + 1));
    }
  }
  // One null wchar_t (to end the block) is two null bytes
  buffer.push_back(0);
  buffer.push_back(0);
  // Insert extra two bytes, just in case the environment was empty.
  buffer.push_back(0);
  buffer.push_back(0);
}

static bool GetFlattenedWindowsCommandString(Args args, std::wstring &command) {
  if (args.empty())
    return false;

  std::vector<llvm::StringRef> args_ref;
  for (auto &entry : args.entries())
    args_ref.push_back(entry.ref());

  llvm::ErrorOr<std::wstring> result =
      llvm::sys::flattenWindowsCommandLine(args_ref);
  if (result.getError())
    return false;

  command = *result;
  return true;
}

HostProcess
ProcessLauncherWindows::LaunchProcess(const ProcessLaunchInfo &launch_info,
                                      Status &error) {
  error.Clear();

  std::string executable;
  std::vector<char> environment;
  STARTUPINFO startupinfo = {};
  PROCESS_INFORMATION pi = {};

  HANDLE stdin_handle = GetStdioHandle(launch_info, STDIN_FILENO);
  HANDLE stdout_handle = GetStdioHandle(launch_info, STDOUT_FILENO);
  HANDLE stderr_handle = GetStdioHandle(launch_info, STDERR_FILENO);

  startupinfo.cb = sizeof(startupinfo);
  startupinfo.dwFlags |= STARTF_USESTDHANDLES;
  startupinfo.hStdError =
      stderr_handle ? stderr_handle : ::GetStdHandle(STD_ERROR_HANDLE);
  startupinfo.hStdInput =
      stdin_handle ? stdin_handle : ::GetStdHandle(STD_INPUT_HANDLE);
  startupinfo.hStdOutput =
      stdout_handle ? stdout_handle : ::GetStdHandle(STD_OUTPUT_HANDLE);

  const char *hide_console_var =
      getenv("LLDB_LAUNCH_INFERIORS_WITHOUT_CONSOLE");
  if (hide_console_var &&
      llvm::StringRef(hide_console_var).equals_insensitive("true")) {
    startupinfo.dwFlags |= STARTF_USESHOWWINDOW;
    startupinfo.wShowWindow = SW_HIDE;
  }

  DWORD flags = CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT;
  if (launch_info.GetFlags().Test(eLaunchFlagDebug))
    flags |= DEBUG_ONLY_THIS_PROCESS;

  if (launch_info.GetFlags().Test(eLaunchFlagDisableSTDIO))
    flags &= ~CREATE_NEW_CONSOLE;

  LPVOID env_block = nullptr;
  ::CreateEnvironmentBuffer(launch_info.GetEnvironment(), environment);
  env_block = environment.data();

  executable = launch_info.GetExecutableFile().GetPath();
  std::wstring wcommandLine;
  GetFlattenedWindowsCommandString(launch_info.GetArguments(), wcommandLine);

  std::wstring wexecutable, wworkingDirectory;
  llvm::ConvertUTF8toWide(executable, wexecutable);
  llvm::ConvertUTF8toWide(launch_info.GetWorkingDirectory().GetPath(),
                          wworkingDirectory);
  // If the command line is empty, it's best to pass a null pointer to tell
  // CreateProcessW to use the executable name as the command line.  If the
  // command line is not empty, its contents may be modified by CreateProcessW.
  WCHAR *pwcommandLine = wcommandLine.empty() ? nullptr : &wcommandLine[0];

  BOOL result = ::CreateProcessW(
      wexecutable.c_str(), pwcommandLine, NULL, NULL, TRUE, flags, env_block,
      wworkingDirectory.size() == 0 ? NULL : wworkingDirectory.c_str(),
      &startupinfo, &pi);

  if (!result) {
    // Call GetLastError before we make any other system calls.
    error.SetError(::GetLastError(), eErrorTypeWin32);
    // Note that error 50 ("The request is not supported") will occur if you
    // try debug a 64-bit inferior from a 32-bit LLDB.
  }

  if (result) {
    // Do not call CloseHandle on pi.hProcess, since we want to pass that back
    // through the HostProcess.
    ::CloseHandle(pi.hThread);
  }

  if (stdin_handle)
    ::CloseHandle(stdin_handle);
  if (stdout_handle)
    ::CloseHandle(stdout_handle);
  if (stderr_handle)
    ::CloseHandle(stderr_handle);

  if (!result)
    return HostProcess();

  return HostProcess(pi.hProcess);
}

HANDLE
ProcessLauncherWindows::GetStdioHandle(const ProcessLaunchInfo &launch_info,
                                       int fd) {
  const FileAction *action = launch_info.GetFileActionForFD(fd);
  if (action == nullptr)
    return NULL;
  SECURITY_ATTRIBUTES secattr = {};
  secattr.nLength = sizeof(SECURITY_ATTRIBUTES);
  secattr.bInheritHandle = TRUE;

  llvm::StringRef path = action->GetPath();
  DWORD access = 0;
  DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  DWORD create = 0;
  DWORD flags = 0;
  if (fd == STDIN_FILENO) {
    access = GENERIC_READ;
    create = OPEN_EXISTING;
    flags = FILE_ATTRIBUTE_READONLY;
  }
  if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
    access = GENERIC_WRITE;
    create = CREATE_ALWAYS;
    if (fd == STDERR_FILENO)
      flags = FILE_FLAG_WRITE_THROUGH;
  }

  std::wstring wpath;
  llvm::ConvertUTF8toWide(path, wpath);
  HANDLE result = ::CreateFileW(wpath.c_str(), access, share, &secattr, create,
                                flags, NULL);
  return (result == INVALID_HANDLE_VALUE) ? NULL : result;
}
