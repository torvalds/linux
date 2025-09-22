//===- KillTheDoctor - Prevent Dr. Watson from stopping tests ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program provides an extremely hacky way to stop Dr. Watson from starting
// due to unhandled exceptions in child processes.
//
// This simply starts the program named in the first positional argument with
// the arguments following it under a debugger. All this debugger does is catch
// any unhandled exceptions thrown in the child process and close the program
// (and hopefully tells someone about it).
//
// This also provides another really hacky method to prevent assert dialog boxes
// from popping up. When --no-user32 is passed, if any process loads user32.dll,
// we assume it is trying to call MessageBoxEx and terminate it. The proper way
// to do this would be to actually set a break point, but there's quite a bit
// of code involved to get the address of MessageBoxEx in the remote process's
// address space due to Address space layout randomization (ASLR). This can be
// added if it's ever actually needed.
//
// If the subprocess exits for any reason other than successful termination, -1
// is returned. If the process exits normally the value it returned is returned.
//
// I hate Windows.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WindowsError.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/type_traits.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <map>
#include <string>
#include <system_error>

// These includes must be last.
#include <windows.h>
#include <winerror.h>
#include <dbghelp.h>
#include <psapi.h>

using namespace llvm;

#undef max

namespace {
  cl::opt<std::string> ProgramToRun(cl::Positional,
    cl::desc("<program to run>"));
  cl::list<std::string>  Argv(cl::ConsumeAfter,
    cl::desc("<program arguments>..."));
  cl::opt<bool> TraceExecution("x",
    cl::desc("Print detailed output about what is being run to stderr."));
  cl::opt<unsigned> Timeout("t", cl::init(0),
    cl::desc("Set maximum runtime in seconds. Defaults to infinite."));
  cl::opt<bool> NoUser32("no-user32",
    cl::desc("Terminate process if it loads user32.dll."));

  StringRef ToolName;

  template <typename HandleType>
  class ScopedHandle {
    typedef typename HandleType::handle_type handle_type;

    handle_type Handle;

  public:
    ScopedHandle()
      : Handle(HandleType::GetInvalidHandle()) {}

    explicit ScopedHandle(handle_type handle)
      : Handle(handle) {}

    ~ScopedHandle() {
      HandleType::Destruct(Handle);
    }

    ScopedHandle& operator=(handle_type handle) {
      // Cleanup current handle.
      if (!HandleType::isValid(Handle))
        HandleType::Destruct(Handle);
      Handle = handle;
      return *this;
    }

    operator bool() const {
      return HandleType::isValid(Handle);
    }

    operator handle_type() {
      return Handle;
    }
  };

  // This implements the most common handle in the Windows API.
  struct CommonHandle {
    typedef HANDLE handle_type;

    static handle_type GetInvalidHandle() {
      return INVALID_HANDLE_VALUE;
    }

    static void Destruct(handle_type Handle) {
      ::CloseHandle(Handle);
    }

    static bool isValid(handle_type Handle) {
      return Handle != GetInvalidHandle();
    }
  };

  struct FileMappingHandle {
    typedef HANDLE handle_type;

    static handle_type GetInvalidHandle() {
      return NULL;
    }

    static void Destruct(handle_type Handle) {
      ::CloseHandle(Handle);
    }

    static bool isValid(handle_type Handle) {
      return Handle != GetInvalidHandle();
    }
  };

  struct MappedViewOfFileHandle {
    typedef LPVOID handle_type;

    static handle_type GetInvalidHandle() {
      return NULL;
    }

    static void Destruct(handle_type Handle) {
      ::UnmapViewOfFile(Handle);
    }

    static bool isValid(handle_type Handle) {
      return Handle != GetInvalidHandle();
    }
  };

  struct ProcessHandle : CommonHandle {};
  struct ThreadHandle  : CommonHandle {};
  struct TokenHandle   : CommonHandle {};
  struct FileHandle    : CommonHandle {};

  typedef ScopedHandle<FileMappingHandle>       FileMappingScopedHandle;
  typedef ScopedHandle<MappedViewOfFileHandle>  MappedViewOfFileScopedHandle;
  typedef ScopedHandle<ProcessHandle>           ProcessScopedHandle;
  typedef ScopedHandle<ThreadHandle>            ThreadScopedHandle;
  typedef ScopedHandle<TokenHandle>             TokenScopedHandle;
  typedef ScopedHandle<FileHandle>              FileScopedHandle;
}

static std::error_code windows_error(DWORD E) { return mapWindowsError(E); }

static std::error_code GetFileNameFromHandle(HANDLE FileHandle,
                                             std::string &Name) {
  char Filename[MAX_PATH+1];
  bool Success = false;
  Name.clear();

  // Get the file size.
  LARGE_INTEGER FileSize;
  Success = ::GetFileSizeEx(FileHandle, &FileSize);

  if (!Success)
    return windows_error(::GetLastError());

  // Create a file mapping object.
  FileMappingScopedHandle FileMapping(
    ::CreateFileMappingA(FileHandle,
                         NULL,
                         PAGE_READONLY,
                         0,
                         1,
                         NULL));

  if (!FileMapping)
    return windows_error(::GetLastError());

  // Create a file mapping to get the file name.
  MappedViewOfFileScopedHandle MappedFile(
    ::MapViewOfFile(FileMapping, FILE_MAP_READ, 0, 0, 1));

  if (!MappedFile)
    return windows_error(::GetLastError());

  Success = ::GetMappedFileNameA(::GetCurrentProcess(), MappedFile, Filename,
                                 std::size(Filename) - 1);

  if (!Success)
    return windows_error(::GetLastError());
  else {
    Name = Filename;
    return std::error_code();
  }
}

/// Find program using shell lookup rules.
/// @param Program This is either an absolute path, relative path, or simple a
///        program name. Look in PATH for any programs that match. If no
///        extension is present, try all extensions in PATHEXT.
/// @return If ec == errc::success, The absolute path to the program. Otherwise
///         the return value is undefined.
static std::string FindProgram(const std::string &Program,
                               std::error_code &ec) {
  char PathName[MAX_PATH + 1];
  typedef SmallVector<StringRef, 12> pathext_t;
  pathext_t pathext;
  // Check for the program without an extension (in case it already has one).
  pathext.push_back("");
  SplitString(std::getenv("PATHEXT"), pathext, ";");

  for (pathext_t::iterator i = pathext.begin(), e = pathext.end(); i != e; ++i){
    SmallString<5> ext;
    for (std::size_t ii = 0, e = i->size(); ii != e; ++ii)
      ext.push_back(::tolower((*i)[ii]));
    LPCSTR Extension = NULL;
    if (ext.size() && ext[0] == '.')
      Extension = ext.c_str();
    DWORD length = ::SearchPathA(NULL, Program.c_str(), Extension,
                                 std::size(PathName), PathName, NULL);
    if (length == 0)
      ec = windows_error(::GetLastError());
    else if (length > std::size(PathName)) {
      // This may have been the file, return with error.
      ec = windows_error(ERROR_BUFFER_OVERFLOW);
      break;
    } else {
      // We found the path! Return it.
      ec = std::error_code();
      break;
    }
  }

  // Make sure PathName is valid.
  PathName[MAX_PATH] = 0;
  return PathName;
}

static StringRef ExceptionCodeToString(DWORD ExceptionCode) {
  switch(ExceptionCode) {
  case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
  case EXCEPTION_BREAKPOINT: return "EXCEPTION_BREAKPOINT";
  case EXCEPTION_DATATYPE_MISALIGNMENT:
    return "EXCEPTION_DATATYPE_MISALIGNMENT";
  case EXCEPTION_FLT_DENORMAL_OPERAND: return "EXCEPTION_FLT_DENORMAL_OPERAND";
  case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
  case EXCEPTION_FLT_INEXACT_RESULT: return "EXCEPTION_FLT_INEXACT_RESULT";
  case EXCEPTION_FLT_INVALID_OPERATION:
    return "EXCEPTION_FLT_INVALID_OPERATION";
  case EXCEPTION_FLT_OVERFLOW: return "EXCEPTION_FLT_OVERFLOW";
  case EXCEPTION_FLT_STACK_CHECK: return "EXCEPTION_FLT_STACK_CHECK";
  case EXCEPTION_FLT_UNDERFLOW: return "EXCEPTION_FLT_UNDERFLOW";
  case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
  case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
  case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
  case EXCEPTION_INT_OVERFLOW: return "EXCEPTION_INT_OVERFLOW";
  case EXCEPTION_INVALID_DISPOSITION: return "EXCEPTION_INVALID_DISPOSITION";
  case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
  case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
  case EXCEPTION_SINGLE_STEP: return "EXCEPTION_SINGLE_STEP";
  case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
  default: return "<unknown>";
  }
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.

  ToolName = argv[0];

  cl::ParseCommandLineOptions(argc, argv, "Dr. Watson Assassin.\n");
  if (ProgramToRun.size() == 0) {
    cl::PrintHelpMessage();
    return -1;
  }

  if (Timeout > std::numeric_limits<uint32_t>::max() / 1000) {
    errs() << ToolName << ": Timeout value too large, must be less than: "
                       << std::numeric_limits<uint32_t>::max() / 1000
                       << '\n';
    return -1;
  }

  std::string CommandLine(ProgramToRun);

  std::error_code ec;
  ProgramToRun = FindProgram(ProgramToRun, ec);
  if (ec) {
    errs() << ToolName << ": Failed to find program: '" << CommandLine
           << "': " << ec.message() << '\n';
    return -1;
  }

  if (TraceExecution)
    errs() << ToolName << ": Found Program: " << ProgramToRun << '\n';

  for (const std::string &Arg : Argv) {
    CommandLine.push_back(' ');
    CommandLine.append(Arg);
  }

  if (TraceExecution)
    errs() << ToolName << ": Program Image Path: " << ProgramToRun << '\n'
           << ToolName << ": Command Line: " << CommandLine << '\n';

  STARTUPINFOA StartupInfo;
  PROCESS_INFORMATION ProcessInfo;
  std::memset(&StartupInfo, 0, sizeof(StartupInfo));
  StartupInfo.cb = sizeof(StartupInfo);
  std::memset(&ProcessInfo, 0, sizeof(ProcessInfo));

  // Set error mode to not display any message boxes. The child process inherits
  // this.
  ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  ::_set_error_mode(_OUT_TO_STDERR);

  BOOL success = ::CreateProcessA(ProgramToRun.c_str(),
                                  const_cast<LPSTR>(CommandLine.c_str()),
                                  NULL,
                                  NULL,
                                  FALSE,
                                  DEBUG_PROCESS,
                                  NULL,
                                  NULL,
                                  &StartupInfo,
                                  &ProcessInfo);
  if (!success) {
    errs() << ToolName << ": Failed to run program: '" << ProgramToRun << "': "
           << std::error_code(windows_error(::GetLastError())).message()
           << '\n';
    return -1;
  }

  // Make sure ::CloseHandle is called on exit.
  std::map<DWORD, HANDLE> ProcessIDToHandle;

  DEBUG_EVENT DebugEvent;
  std::memset(&DebugEvent, 0, sizeof(DebugEvent));
  DWORD dwContinueStatus = DBG_CONTINUE;

  // Run the program under the debugger until either it exits, or throws an
  // exception.
  if (TraceExecution)
    errs() << ToolName << ": Debugging...\n";

  while(true) {
    DWORD TimeLeft = INFINITE;
    if (Timeout > 0) {
      FILETIME CreationTime, ExitTime, KernelTime, UserTime;
      ULARGE_INTEGER a, b;
      success = ::GetProcessTimes(ProcessInfo.hProcess,
                                  &CreationTime,
                                  &ExitTime,
                                  &KernelTime,
                                  &UserTime);
      if (!success) {
        ec = windows_error(::GetLastError());

        errs() << ToolName << ": Failed to get process times: "
               << ec.message() << '\n';
        return -1;
      }
      a.LowPart = KernelTime.dwLowDateTime;
      a.HighPart = KernelTime.dwHighDateTime;
      b.LowPart = UserTime.dwLowDateTime;
      b.HighPart = UserTime.dwHighDateTime;
      // Convert 100-nanosecond units to milliseconds.
      uint64_t TotalTimeMiliseconds = (a.QuadPart + b.QuadPart) / 10000;
      // Handle the case where the process has been running for more than 49
      // days.
      if (TotalTimeMiliseconds > std::numeric_limits<uint32_t>::max()) {
        errs() << ToolName << ": Timeout Failed: Process has been running for"
                              "more than 49 days.\n";
        return -1;
      }

      // We check with > instead of using Timeleft because if
      // TotalTimeMiliseconds is greater than Timeout * 1000, TimeLeft would
      // underflow.
      if (TotalTimeMiliseconds > (Timeout * 1000)) {
        errs() << ToolName << ": Process timed out.\n";
        ::TerminateProcess(ProcessInfo.hProcess, -1);
        // Otherwise other stuff starts failing...
        return -1;
      }

      TimeLeft = (Timeout * 1000) - static_cast<uint32_t>(TotalTimeMiliseconds);
    }
    success = WaitForDebugEvent(&DebugEvent, TimeLeft);

    if (!success) {
      DWORD LastError = ::GetLastError();
      ec = windows_error(LastError);

      if (LastError == ERROR_SEM_TIMEOUT || LastError == WSAETIMEDOUT) {
        errs() << ToolName << ": Process timed out.\n";
        ::TerminateProcess(ProcessInfo.hProcess, -1);
        // Otherwise other stuff starts failing...
        return -1;
      }

      errs() << ToolName << ": Failed to wait for debug event in program: '"
             << ProgramToRun << "': " << ec.message() << '\n';
      return -1;
    }

    switch(DebugEvent.dwDebugEventCode) {
    case CREATE_PROCESS_DEBUG_EVENT:
      // Make sure we remove the handle on exit.
      if (TraceExecution)
        errs() << ToolName << ": Debug Event: CREATE_PROCESS_DEBUG_EVENT\n";
      ProcessIDToHandle[DebugEvent.dwProcessId] =
        DebugEvent.u.CreateProcessInfo.hProcess;
      ::CloseHandle(DebugEvent.u.CreateProcessInfo.hFile);
      break;
    case EXIT_PROCESS_DEBUG_EVENT: {
        if (TraceExecution)
          errs() << ToolName << ": Debug Event: EXIT_PROCESS_DEBUG_EVENT\n";

        // If this is the process we originally created, exit with its exit
        // code.
        if (DebugEvent.dwProcessId == ProcessInfo.dwProcessId)
          return DebugEvent.u.ExitProcess.dwExitCode;

        // Otherwise cleanup any resources we have for it.
        std::map<DWORD, HANDLE>::iterator ExitingProcess =
          ProcessIDToHandle.find(DebugEvent.dwProcessId);
        if (ExitingProcess == ProcessIDToHandle.end()) {
          errs() << ToolName << ": Got unknown process id!\n";
          return -1;
        }
        ::CloseHandle(ExitingProcess->second);
        ProcessIDToHandle.erase(ExitingProcess);
      }
      break;
    case CREATE_THREAD_DEBUG_EVENT:
      ::CloseHandle(DebugEvent.u.CreateThread.hThread);
      break;
    case LOAD_DLL_DEBUG_EVENT: {
        // Cleanup the file handle.
        FileScopedHandle DLLFile(DebugEvent.u.LoadDll.hFile);
        std::string DLLName;
        ec = GetFileNameFromHandle(DLLFile, DLLName);
        if (ec) {
          DLLName = "<failed to get file name from file handle> : ";
          DLLName += ec.message();
        }
        if (TraceExecution) {
          errs() << ToolName << ": Debug Event: LOAD_DLL_DEBUG_EVENT\n";
          errs().indent(ToolName.size()) << ": DLL Name : " << DLLName << '\n';
        }

        if (NoUser32 && sys::path::stem(DLLName) == "user32") {
          // Program is loading user32.dll, in the applications we are testing,
          // this only happens if an assert has fired. By now the message has
          // already been printed, so simply close the program.
          errs() << ToolName << ": user32.dll loaded!\n";
          errs().indent(ToolName.size())
                 << ": This probably means that assert was called. Closing "
                    "program to prevent message box from popping up.\n";
          dwContinueStatus = DBG_CONTINUE;
          ::TerminateProcess(ProcessIDToHandle[DebugEvent.dwProcessId], -1);
          return -1;
        }
      }
      break;
    case EXCEPTION_DEBUG_EVENT: {
        // Close the application if this exception will not be handled by the
        // child application.
        if (TraceExecution)
          errs() << ToolName << ": Debug Event: EXCEPTION_DEBUG_EVENT\n";

        EXCEPTION_DEBUG_INFO  &Exception = DebugEvent.u.Exception;
        if (Exception.dwFirstChance > 0) {
          if (TraceExecution) {
            errs().indent(ToolName.size()) << ": Debug Info : ";
            errs() << "First chance exception at "
                   << Exception.ExceptionRecord.ExceptionAddress
                   << ", exception code: "
                   << ExceptionCodeToString(
                        Exception.ExceptionRecord.ExceptionCode)
                   << " (" << Exception.ExceptionRecord.ExceptionCode << ")\n";
          }
          dwContinueStatus = DBG_EXCEPTION_NOT_HANDLED;
        } else {
          errs() << ToolName << ": Unhandled exception in: " << ProgramToRun
                 << "!\n";
                 errs().indent(ToolName.size()) << ": location: ";
                 errs() << Exception.ExceptionRecord.ExceptionAddress
                        << ", exception code: "
                        << ExceptionCodeToString(
                            Exception.ExceptionRecord.ExceptionCode)
                        << " (" << Exception.ExceptionRecord.ExceptionCode
                        << ")\n";
          dwContinueStatus = DBG_CONTINUE;
          ::TerminateProcess(ProcessIDToHandle[DebugEvent.dwProcessId], -1);
          return -1;
        }
      }
      break;
    default:
      // Do nothing.
      if (TraceExecution)
        errs() << ToolName << ": Debug Event: <unknown>\n";
      break;
    }

    success = ContinueDebugEvent(DebugEvent.dwProcessId,
                                 DebugEvent.dwThreadId,
                                 dwContinueStatus);
    if (!success) {
      ec = windows_error(::GetLastError());
      errs() << ToolName << ": Failed to continue debugging program: '"
             << ProgramToRun << "': " << ec.message() << '\n';
      return -1;
    }

    dwContinueStatus = DBG_CONTINUE;
  }

  assert(0 && "Fell out of debug loop. This shouldn't be possible!");
  return -1;
}
