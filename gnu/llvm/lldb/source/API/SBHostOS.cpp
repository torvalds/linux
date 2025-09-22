//===-- SBHostOS.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBHostOS.h"
#include "lldb/API/SBError.h"
#include "lldb/Host/Config.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/HostInfo.h"
#include "lldb/Host/HostNativeThread.h"
#include "lldb/Host/HostThread.h"
#include "lldb/Host/ThreadLauncher.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Instrumentation.h"

#include "Plugins/ExpressionParser/Clang/ClangHost.h"
#if LLDB_ENABLE_PYTHON
#include "Plugins/ScriptInterpreter/Python/ScriptInterpreterPython.h"
#endif

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"

using namespace lldb;
using namespace lldb_private;

SBFileSpec SBHostOS::GetProgramFileSpec() {
  LLDB_INSTRUMENT();

  SBFileSpec sb_filespec;
  sb_filespec.SetFileSpec(HostInfo::GetProgramFileSpec());
  return sb_filespec;
}

SBFileSpec SBHostOS::GetLLDBPythonPath() {
  LLDB_INSTRUMENT();

  return GetLLDBPath(ePathTypePythonDir);
}

SBFileSpec SBHostOS::GetLLDBPath(lldb::PathType path_type) {
  LLDB_INSTRUMENT_VA(path_type);

  FileSpec fspec;
  switch (path_type) {
  case ePathTypeLLDBShlibDir:
    fspec = HostInfo::GetShlibDir();
    break;
  case ePathTypeSupportExecutableDir:
    fspec = HostInfo::GetSupportExeDir();
    break;
  case ePathTypeHeaderDir:
    fspec = HostInfo::GetHeaderDir();
    break;
  case ePathTypePythonDir:
#if LLDB_ENABLE_PYTHON
    fspec = ScriptInterpreterPython::GetPythonDir();
#endif
    break;
  case ePathTypeLLDBSystemPlugins:
    fspec = HostInfo::GetSystemPluginDir();
    break;
  case ePathTypeLLDBUserPlugins:
    fspec = HostInfo::GetUserPluginDir();
    break;
  case ePathTypeLLDBTempSystemDir:
    fspec = HostInfo::GetProcessTempDir();
    break;
  case ePathTypeGlobalLLDBTempSystemDir:
    fspec = HostInfo::GetGlobalTempDir();
    break;
  case ePathTypeClangDir:
    fspec = GetClangResourceDir();
    break;
  }

  SBFileSpec sb_fspec;
  sb_fspec.SetFileSpec(fspec);
  return sb_fspec;
}

SBFileSpec SBHostOS::GetUserHomeDirectory() {
  LLDB_INSTRUMENT();

  FileSpec homedir;
  FileSystem::Instance().GetHomeDirectory(homedir);
  FileSystem::Instance().Resolve(homedir);

  SBFileSpec sb_fspec;
  sb_fspec.SetFileSpec(homedir);

  return sb_fspec;
}

lldb::thread_t SBHostOS::ThreadCreate(const char *name,
                                      lldb::thread_func_t thread_function,
                                      void *thread_arg, SBError *error_ptr) {
  LLDB_INSTRUMENT_VA(name, thread_function, thread_arg, error_ptr);
  return LLDB_INVALID_HOST_THREAD;
}

void SBHostOS::ThreadCreated(const char *name) { LLDB_INSTRUMENT_VA(name); }

bool SBHostOS::ThreadCancel(lldb::thread_t thread, SBError *error_ptr) {
  LLDB_INSTRUMENT_VA(thread, error_ptr);
  return false;
}

bool SBHostOS::ThreadDetach(lldb::thread_t thread, SBError *error_ptr) {
  LLDB_INSTRUMENT_VA(thread, error_ptr);
  return false;
}

bool SBHostOS::ThreadJoin(lldb::thread_t thread, lldb::thread_result_t *result,
                          SBError *error_ptr) {
  LLDB_INSTRUMENT_VA(thread, result, error_ptr);
  return false;
}
