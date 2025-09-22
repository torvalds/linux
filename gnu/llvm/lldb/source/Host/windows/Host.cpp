//===-- source/Host/windows/Host.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/AutoHandle.h"
#include "lldb/Host/windows/windows.h"
#include <cstdio>

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/ProcessLaunchInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/ProcessInfo.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/StructuredData.h"

#include "llvm/Support/ConvertUTF.h"

// Windows includes
#include <tlhelp32.h>

using namespace lldb;
using namespace lldb_private;

static bool GetTripleForProcess(const FileSpec &executable,
                                llvm::Triple &triple) {
  // Open the PE File as a binary file, and parse just enough information to
  // determine the machine type.
  auto imageBinaryP = FileSystem::Instance().Open(
      executable, File::eOpenOptionReadOnly, lldb::eFilePermissionsUserRead);
  if (!imageBinaryP)
    return llvm::errorToBool(imageBinaryP.takeError());
  File &imageBinary = *imageBinaryP.get();
  imageBinary.SeekFromStart(0x3c);
  int32_t peOffset = 0;
  uint32_t peHead = 0;
  uint16_t machineType = 0;
  size_t readSize = sizeof(peOffset);
  imageBinary.Read(&peOffset, readSize);
  imageBinary.SeekFromStart(peOffset);
  imageBinary.Read(&peHead, readSize);
  if (peHead != 0x00004550) // "PE\0\0", little-endian
    return false;           // Status: Can't find PE header
  readSize = 2;
  imageBinary.Read(&machineType, readSize);
  triple.setVendor(llvm::Triple::PC);
  triple.setOS(llvm::Triple::Win32);
  triple.setArch(llvm::Triple::UnknownArch);
  if (machineType == 0x8664)
    triple.setArch(llvm::Triple::x86_64);
  else if (machineType == 0x14c)
    triple.setArch(llvm::Triple::x86);
  else if (machineType == 0x1c4)
    triple.setArch(llvm::Triple::arm);
  else if (machineType == 0xaa64)
    triple.setArch(llvm::Triple::aarch64);

  return true;
}

static bool GetExecutableForProcess(const AutoHandle &handle,
                                    std::string &path) {
  // Get the process image path.  MAX_PATH isn't long enough, paths can
  // actually be up to 32KB.
  std::vector<wchar_t> buffer(PATH_MAX);
  DWORD dwSize = buffer.size();
  if (!::QueryFullProcessImageNameW(handle.get(), 0, &buffer[0], &dwSize))
    return false;
  return llvm::convertWideToUTF8(buffer.data(), path);
}

static void GetProcessExecutableAndTriple(const AutoHandle &handle,
                                          ProcessInstanceInfo &process) {
  // We may not have permissions to read the path from the process.  So start
  // off by setting the executable file to whatever Toolhelp32 gives us, and
  // then try to enhance this with more detailed information, but fail
  // gracefully.
  std::string executable;
  llvm::Triple triple;
  triple.setVendor(llvm::Triple::PC);
  triple.setOS(llvm::Triple::Win32);
  triple.setArch(llvm::Triple::UnknownArch);
  if (GetExecutableForProcess(handle, executable)) {
    FileSpec executableFile(executable.c_str());
    process.SetExecutableFile(executableFile, true);
    GetTripleForProcess(executableFile, triple);
  }
  process.SetArchitecture(ArchSpec(triple));

  // TODO(zturner): Add the ability to get the process user name.
}

lldb::thread_t Host::GetCurrentThread() {
  return lldb::thread_t(::GetCurrentThread());
}

void Host::Kill(lldb::pid_t pid, int signo) {
  AutoHandle handle(::OpenProcess(PROCESS_TERMINATE, FALSE, pid), nullptr);
  if (handle.IsValid())
    ::TerminateProcess(handle.get(), 1);
}

const char *Host::GetSignalAsCString(int signo) { return NULL; }

FileSpec Host::GetModuleFileSpecForHostAddress(const void *host_addr) {
  FileSpec module_filespec;

  HMODULE hmodule = NULL;
  if (!::GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           (LPCTSTR)host_addr, &hmodule))
    return module_filespec;

  std::vector<wchar_t> buffer(PATH_MAX);
  DWORD chars_copied = 0;
  do {
    chars_copied = ::GetModuleFileNameW(hmodule, &buffer[0], buffer.size());
    if (chars_copied == buffer.size() &&
        ::GetLastError() == ERROR_INSUFFICIENT_BUFFER)
      buffer.resize(buffer.size() * 2);
  } while (chars_copied >= buffer.size());
  std::string path;
  if (!llvm::convertWideToUTF8(buffer.data(), path))
    return module_filespec;
  module_filespec.SetFile(path, FileSpec::Style::native);
  return module_filespec;
}

uint32_t Host::FindProcessesImpl(const ProcessInstanceInfoMatch &match_info,
                                 ProcessInstanceInfoList &process_infos) {
  process_infos.clear();

  AutoHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot.IsValid())
    return 0;

  PROCESSENTRY32W pe = {};
  pe.dwSize = sizeof(PROCESSENTRY32W);
  if (Process32FirstW(snapshot.get(), &pe)) {
    do {
      AutoHandle handle(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                      pe.th32ProcessID),
                        nullptr);

      ProcessInstanceInfo process;
      std::string exeFile;
      llvm::convertWideToUTF8(pe.szExeFile, exeFile);
      process.SetExecutableFile(FileSpec(exeFile), true);
      process.SetProcessID(pe.th32ProcessID);
      process.SetParentProcessID(pe.th32ParentProcessID);
      GetProcessExecutableAndTriple(handle, process);

      if (match_info.MatchAllProcesses() || match_info.Matches(process))
        process_infos.push_back(process);
    } while (Process32NextW(snapshot.get(), &pe));
  }
  return process_infos.size();
}

bool Host::GetProcessInfo(lldb::pid_t pid, ProcessInstanceInfo &process_info) {
  process_info.Clear();

  AutoHandle handle(
      ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid),
      nullptr);
  if (!handle.IsValid())
    return false;

  process_info.SetProcessID(pid);
  GetProcessExecutableAndTriple(handle, process_info);

  // Need to read the PEB to get parent process and command line arguments.

  AutoHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot.IsValid())
    return false;

  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(PROCESSENTRY32W);
  if (Process32FirstW(snapshot.get(), &pe)) {
    do {
      if (pe.th32ProcessID == pid) {
        process_info.SetParentProcessID(pe.th32ParentProcessID);
        return true;
      }
    } while (Process32NextW(snapshot.get(), &pe));
  }

  return false;
}

llvm::Expected<HostThread> Host::StartMonitoringChildProcess(
    const Host::MonitorChildProcessCallback &callback, lldb::pid_t pid) {
  return HostThread();
}

Status Host::ShellExpandArguments(ProcessLaunchInfo &launch_info) {
  Status error;
  if (launch_info.GetFlags().Test(eLaunchFlagShellExpandArguments)) {
    FileSpec expand_tool_spec = HostInfo::GetSupportExeDir();
    if (!expand_tool_spec) {
      error.SetErrorString("could not find support executable directory for "
                           "the lldb-argdumper tool");
      return error;
    }
    expand_tool_spec.AppendPathComponent("lldb-argdumper.exe");
    if (!FileSystem::Instance().Exists(expand_tool_spec)) {
      error.SetErrorString("could not find the lldb-argdumper tool");
      return error;
    }

    std::string quoted_cmd_string;
    launch_info.GetArguments().GetQuotedCommandString(quoted_cmd_string);
    std::replace(quoted_cmd_string.begin(), quoted_cmd_string.end(), '\\', '/');
    StreamString expand_command;

    expand_command.Printf("\"%s\" %s", expand_tool_spec.GetPath().c_str(),
                          quoted_cmd_string.c_str());

    int status;
    std::string output;
    std::string command = expand_command.GetString().str();
    Status e =
        RunShellCommand(command.c_str(), launch_info.GetWorkingDirectory(),
                        &status, nullptr, &output, std::chrono::seconds(10));

    if (e.Fail())
      return e;

    if (status != 0) {
      error.SetErrorStringWithFormat("lldb-argdumper exited with error %d",
                                     status);
      return error;
    }

    auto data_sp = StructuredData::ParseJSON(output);
    if (!data_sp) {
      error.SetErrorString("invalid JSON");
      return error;
    }

    auto dict_sp = data_sp->GetAsDictionary();
    if (!dict_sp) {
      error.SetErrorString("invalid JSON");
      return error;
    }

    auto args_sp = dict_sp->GetObjectForDotSeparatedPath("arguments");
    if (!args_sp) {
      error.SetErrorString("invalid JSON");
      return error;
    }

    auto args_array_sp = args_sp->GetAsArray();
    if (!args_array_sp) {
      error.SetErrorString("invalid JSON");
      return error;
    }

    launch_info.GetArguments().Clear();

    for (size_t i = 0; i < args_array_sp->GetSize(); i++) {
      auto item_sp = args_array_sp->GetItemAtIndex(i);
      if (!item_sp)
        continue;
      auto str_sp = item_sp->GetAsString();
      if (!str_sp)
        continue;

      launch_info.GetArguments().AppendArgument(str_sp->GetValue());
    }
  }

  return error;
}

Environment Host::GetEnvironment() {
  Environment env;
  // The environment block on Windows is a contiguous buffer of NULL terminated
  // strings, where the end of the environment block is indicated by two
  // consecutive NULLs.
  LPWCH environment_block = ::GetEnvironmentStringsW();
  while (*environment_block != L'\0') {
    std::string current_var;
    auto current_var_size = wcslen(environment_block) + 1;
    if (!llvm::convertWideToUTF8(environment_block, current_var)) {
      environment_block += current_var_size;
      continue;
    }
    if (current_var[0] != '=')
      env.insert(current_var);

    environment_block += current_var_size;
  }
  return env;
}
