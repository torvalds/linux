//===-- SystemInitializerCommon.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Initialization/SystemInitializerCommon.h"

#include "Plugins/Process/gdb-remote/ProcessGDBRemoteLog.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/Host.h"
#include "lldb/Host/Socket.h"
#include "lldb/Target/Statistics.h"
#include "lldb/Utility/Diagnostics.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Timer.h"
#include "lldb/Version/Version.h"

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) ||       \
    defined(__OpenBSD__)
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#endif

#if defined(_WIN32)
#include "Plugins/Process/Windows/Common/ProcessWindowsLog.h"
#include "lldb/Host/windows/windows.h"
#include <crtdbg.h>
#endif

#include "llvm/Support/TargetSelect.h"

#include <string>

using namespace lldb_private;

SystemInitializerCommon::SystemInitializerCommon(
    HostInfo::SharedLibraryDirectoryHelper *helper)
    : m_shlib_dir_helper(helper) {}

SystemInitializerCommon::~SystemInitializerCommon() = default;

llvm::Error SystemInitializerCommon::Initialize() {
#if defined(_WIN32)
  const char *disable_crash_dialog_var = getenv("LLDB_DISABLE_CRASH_DIALOG");
  if (disable_crash_dialog_var &&
      llvm::StringRef(disable_crash_dialog_var).equals_insensitive("true")) {
    // This will prevent Windows from displaying a dialog box requiring user
    // interaction when
    // LLDB crashes.  This is mostly useful when automating LLDB, for example
    // via the test
    // suite, so that a crash in LLDB does not prevent completion of the test
    // suite.
    ::SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS |
                   SEM_NOGPFAULTERRORBOX);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  }
#endif

  InitializeLldbChannel();

  Diagnostics::Initialize();
  FileSystem::Initialize();
  HostInfo::Initialize(m_shlib_dir_helper);

  llvm::Error error = Socket::Initialize();
  if (error)
    return error;

  LLDB_SCOPED_TIMER();

  process_gdb_remote::ProcessGDBRemoteLog::Initialize();

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) ||       \
    defined(__OpenBSD__)
  ProcessPOSIXLog::Initialize();
#endif
#if defined(_WIN32)
  ProcessWindowsLog::Initialize();
#endif

  return llvm::Error::success();
}

void SystemInitializerCommon::Terminate() {
  LLDB_SCOPED_TIMER();

#if defined(_WIN32)
  ProcessWindowsLog::Terminate();
#endif

  Socket::Terminate();
  HostInfo::Terminate();
  Log::DisableAllLogChannels();
  FileSystem::Terminate();
  Diagnostics::Terminate();
}
