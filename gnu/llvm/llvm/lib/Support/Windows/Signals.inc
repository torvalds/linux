//===- Win32/Signals.cpp - Win32 Signals Implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the Win32 specific implementation of the Signals class.
//
//===----------------------------------------------------------------------===//
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ExitCodes.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/WindowsError.h"
#include <algorithm>
#include <io.h>
#include <signal.h>
#include <stdio.h>

#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

// The Windows.h header must be after LLVM and standard headers.
#include "llvm/Support/Windows/WindowsSupport.h"

#ifdef __MINGW32__
#include <imagehlp.h>
#else
#include <crtdbg.h>
#include <dbghelp.h>
#endif
#include <psapi.h>

#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#elif __MINGW32__
// The version of g++ that comes with MinGW does *not* properly understand
// the ll format specifier for printf. However, MinGW passes the format
// specifiers on to the MSVCRT entirely, and the CRT understands the ll
// specifier. So these warnings are spurious in this case. Since we compile
// with -Wall, this will generate these warnings which should be ignored. So
// we will turn off the warnings for this just file. However, MinGW also does
// not support push and pop for diagnostics, so we have to manually turn it
// back on at the end of the file.
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-extra-args"

#if !defined(__MINGW64_VERSION_MAJOR)
// MinGW.org does not have updated support for the 64-bit versions of the
// DebugHlp APIs. So we will have to load them manually. The structures and
// method signatures were pulled from DbgHelp.h in the Windows Platform SDK,
// and adjusted for brevity.
typedef struct _IMAGEHLP_LINE64 {
  DWORD SizeOfStruct;
  PVOID Key;
  DWORD LineNumber;
  PCHAR FileName;
  DWORD64 Address;
} IMAGEHLP_LINE64, *PIMAGEHLP_LINE64;

typedef struct _IMAGEHLP_SYMBOL64 {
  DWORD SizeOfStruct;
  DWORD64 Address;
  DWORD Size;
  DWORD Flags;
  DWORD MaxNameLength;
  CHAR Name[1];
} IMAGEHLP_SYMBOL64, *PIMAGEHLP_SYMBOL64;

typedef struct _tagADDRESS64 {
  DWORD64 Offset;
  WORD Segment;
  ADDRESS_MODE Mode;
} ADDRESS64, *LPADDRESS64;

typedef struct _KDHELP64 {
  DWORD64 Thread;
  DWORD ThCallbackStack;
  DWORD ThCallbackBStore;
  DWORD NextCallback;
  DWORD FramePointer;
  DWORD64 KiCallUserMode;
  DWORD64 KeUserCallbackDispatcher;
  DWORD64 SystemRangeStart;
  DWORD64 KiUserExceptionDispatcher;
  DWORD64 StackBase;
  DWORD64 StackLimit;
  DWORD64 Reserved[5];
} KDHELP64, *PKDHELP64;

typedef struct _tagSTACKFRAME64 {
  ADDRESS64 AddrPC;
  ADDRESS64 AddrReturn;
  ADDRESS64 AddrFrame;
  ADDRESS64 AddrStack;
  ADDRESS64 AddrBStore;
  PVOID FuncTableEntry;
  DWORD64 Params[4];
  BOOL Far;
  BOOL Virtual;
  DWORD64 Reserved[3];
  KDHELP64 KdHelp;
} STACKFRAME64, *LPSTACKFRAME64;
#endif // !defined(__MINGW64_VERSION_MAJOR)
#endif // __MINGW32__

typedef BOOL(__stdcall *PREAD_PROCESS_MEMORY_ROUTINE64)(
    HANDLE hProcess, DWORD64 qwBaseAddress, PVOID lpBuffer, DWORD nSize,
    LPDWORD lpNumberOfBytesRead);

typedef PVOID(__stdcall *PFUNCTION_TABLE_ACCESS_ROUTINE64)(HANDLE ahProcess,
                                                           DWORD64 AddrBase);

typedef DWORD64(__stdcall *PGET_MODULE_BASE_ROUTINE64)(HANDLE hProcess,
                                                       DWORD64 Address);

typedef DWORD64(__stdcall *PTRANSLATE_ADDRESS_ROUTINE64)(HANDLE hProcess,
                                                         HANDLE hThread,
                                                         LPADDRESS64 lpaddr);

typedef BOOL(WINAPI *fpMiniDumpWriteDump)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                          PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION,
                                          PMINIDUMP_CALLBACK_INFORMATION);
static fpMiniDumpWriteDump fMiniDumpWriteDump;

typedef BOOL(WINAPI *fpStackWalk64)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64,
                                    PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
                                    PFUNCTION_TABLE_ACCESS_ROUTINE64,
                                    PGET_MODULE_BASE_ROUTINE64,
                                    PTRANSLATE_ADDRESS_ROUTINE64);
static fpStackWalk64 fStackWalk64;

typedef DWORD64(WINAPI *fpSymGetModuleBase64)(HANDLE, DWORD64);
static fpSymGetModuleBase64 fSymGetModuleBase64;

typedef BOOL(WINAPI *fpSymGetSymFromAddr64)(HANDLE, DWORD64, PDWORD64,
                                            PIMAGEHLP_SYMBOL64);
static fpSymGetSymFromAddr64 fSymGetSymFromAddr64;

typedef BOOL(WINAPI *fpSymGetLineFromAddr64)(HANDLE, DWORD64, PDWORD,
                                             PIMAGEHLP_LINE64);
static fpSymGetLineFromAddr64 fSymGetLineFromAddr64;

typedef BOOL(WINAPI *fpSymGetModuleInfo64)(HANDLE hProcess, DWORD64 dwAddr,
                                           PIMAGEHLP_MODULE64 ModuleInfo);
static fpSymGetModuleInfo64 fSymGetModuleInfo64;

typedef PVOID(WINAPI *fpSymFunctionTableAccess64)(HANDLE, DWORD64);
static fpSymFunctionTableAccess64 fSymFunctionTableAccess64;

typedef DWORD(WINAPI *fpSymSetOptions)(DWORD);
static fpSymSetOptions fSymSetOptions;

typedef BOOL(WINAPI *fpSymInitialize)(HANDLE, PCSTR, BOOL);
static fpSymInitialize fSymInitialize;

typedef BOOL(WINAPI *fpEnumerateLoadedModules)(HANDLE,
                                               PENUMLOADED_MODULES_CALLBACK64,
                                               PVOID);
static fpEnumerateLoadedModules fEnumerateLoadedModules;

static bool isDebugHelpInitialized() {
  return fStackWalk64 && fSymInitialize && fSymSetOptions && fMiniDumpWriteDump;
}

static bool load64BitDebugHelp(void) {
  HMODULE hLib =
      ::LoadLibraryExA("Dbghelp.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (hLib) {
    fMiniDumpWriteDump = (fpMiniDumpWriteDump)(void *)::GetProcAddress(
        hLib, "MiniDumpWriteDump");
    fStackWalk64 = (fpStackWalk64)(void *)::GetProcAddress(hLib, "StackWalk64");
    fSymGetModuleBase64 = (fpSymGetModuleBase64)(void *)::GetProcAddress(
        hLib, "SymGetModuleBase64");
    fSymGetSymFromAddr64 = (fpSymGetSymFromAddr64)(void *)::GetProcAddress(
        hLib, "SymGetSymFromAddr64");
    fSymGetLineFromAddr64 = (fpSymGetLineFromAddr64)(void *)::GetProcAddress(
        hLib, "SymGetLineFromAddr64");
    fSymGetModuleInfo64 = (fpSymGetModuleInfo64)(void *)::GetProcAddress(
        hLib, "SymGetModuleInfo64");
    fSymFunctionTableAccess64 =
        (fpSymFunctionTableAccess64)(void *)::GetProcAddress(
            hLib, "SymFunctionTableAccess64");
    fSymSetOptions =
        (fpSymSetOptions)(void *)::GetProcAddress(hLib, "SymSetOptions");
    fSymInitialize =
        (fpSymInitialize)(void *)::GetProcAddress(hLib, "SymInitialize");
    fEnumerateLoadedModules =
        (fpEnumerateLoadedModules)(void *)::GetProcAddress(
            hLib, "EnumerateLoadedModules64");
  }
  return isDebugHelpInitialized();
}

using namespace llvm;

// Forward declare.
static LONG WINAPI LLVMUnhandledExceptionFilter(LPEXCEPTION_POINTERS ep);
static BOOL WINAPI LLVMConsoleCtrlHandler(DWORD dwCtrlType);

// The function to call if ctrl-c is pressed.
static void (*InterruptFunction)() = 0;

static std::vector<std::string> *FilesToRemove = NULL;
static bool RegisteredUnhandledExceptionFilter = false;
static bool CleanupExecuted = false;
static PTOP_LEVEL_EXCEPTION_FILTER OldFilter = NULL;

/// The function to call on "SIGPIPE" (one-time use only).
static std::atomic<void (*)()> OneShotPipeSignalFunction(nullptr);

// Windows creates a new thread to execute the console handler when an event
// (such as CTRL/C) occurs.  This causes concurrency issues with the above
// globals which this critical section addresses.
static CRITICAL_SECTION CriticalSection;
static bool CriticalSectionInitialized = false;

static StringRef Argv0;

enum {
#if defined(_M_X64)
  NativeMachineType = IMAGE_FILE_MACHINE_AMD64
#elif defined(_M_ARM64)
  NativeMachineType = IMAGE_FILE_MACHINE_ARM64
#elif defined(_M_IX86)
  NativeMachineType = IMAGE_FILE_MACHINE_I386
#elif defined(_M_ARM)
  NativeMachineType = IMAGE_FILE_MACHINE_ARMNT
#else
  NativeMachineType = IMAGE_FILE_MACHINE_UNKNOWN
#endif
};

static bool printStackTraceWithLLVMSymbolizer(llvm::raw_ostream &OS,
                                              HANDLE hProcess, HANDLE hThread,
                                              STACKFRAME64 &StackFrameOrig,
                                              CONTEXT *ContextOrig) {
  // StackWalk64 modifies the incoming stack frame and context, so copy them.
  STACKFRAME64 StackFrame = StackFrameOrig;

  // Copy the register context so that we don't modify it while we unwind. We
  // could use InitializeContext + CopyContext, but that's only required to get
  // at AVX registers, which typically aren't needed by StackWalk64. Reduce the
  // flag set to indicate that there's less data.
  CONTEXT Context = *ContextOrig;
  Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

  static void *StackTrace[256];
  size_t Depth = 0;
  while (fStackWalk64(NativeMachineType, hProcess, hThread, &StackFrame,
                      &Context, 0, fSymFunctionTableAccess64,
                      fSymGetModuleBase64, 0)) {
    if (StackFrame.AddrFrame.Offset == 0)
      break;
    StackTrace[Depth++] = (void *)(uintptr_t)StackFrame.AddrPC.Offset;
    if (Depth >= std::size(StackTrace))
      break;
  }

  return printSymbolizedStackTrace(Argv0, &StackTrace[0], Depth, OS);
}

namespace {
struct FindModuleData {
  void **StackTrace;
  int Depth;
  const char **Modules;
  intptr_t *Offsets;
  StringSaver *StrPool;
};
} // namespace

static BOOL CALLBACK findModuleCallback(PCSTR ModuleName, DWORD64 ModuleBase,
                                        ULONG ModuleSize, void *VoidData) {
  FindModuleData *Data = (FindModuleData *)VoidData;
  intptr_t Beg = ModuleBase;
  intptr_t End = Beg + ModuleSize;
  for (int I = 0; I < Data->Depth; I++) {
    if (Data->Modules[I])
      continue;
    intptr_t Addr = (intptr_t)Data->StackTrace[I];
    if (Beg <= Addr && Addr < End) {
      Data->Modules[I] = Data->StrPool->save(ModuleName).data();
      Data->Offsets[I] = Addr - Beg;
    }
  }
  return TRUE;
}

static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool) {
  if (!fEnumerateLoadedModules)
    return false;
  FindModuleData Data;
  Data.StackTrace = StackTrace;
  Data.Depth = Depth;
  Data.Modules = Modules;
  Data.Offsets = Offsets;
  Data.StrPool = &StrPool;
  fEnumerateLoadedModules(GetCurrentProcess(), findModuleCallback, &Data);
  return true;
}

static bool printMarkupContext(llvm::raw_ostream &OS,
                               const char *MainExecutableName) {
  return false;
}

static void PrintStackTraceForThread(llvm::raw_ostream &OS, HANDLE hProcess,
                                     HANDLE hThread, STACKFRAME64 &StackFrame,
                                     CONTEXT *Context) {
  // It's possible that DbgHelp.dll hasn't been loaded yet (e.g. if this
  // function is called before the main program called `llvm::InitLLVM`).
  // In this case just return, not stacktrace will be printed.
  if (!isDebugHelpInitialized())
    return;

  // Initialize the symbol handler.
  fSymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
  fSymInitialize(hProcess, NULL, TRUE);

  // Try llvm-symbolizer first. llvm-symbolizer knows how to deal with both PDBs
  // and DWARF, so it should do a good job regardless of what debug info or
  // linker is in use.
  if (printStackTraceWithLLVMSymbolizer(OS, hProcess, hThread, StackFrame,
                                        Context)) {
    return;
  }

  while (true) {
    if (!fStackWalk64(NativeMachineType, hProcess, hThread, &StackFrame,
                      Context, 0, fSymFunctionTableAccess64,
                      fSymGetModuleBase64, 0)) {
      break;
    }

    if (StackFrame.AddrFrame.Offset == 0)
      break;

    using namespace llvm;
    // Print the PC in hexadecimal.
    DWORD64 PC = StackFrame.AddrPC.Offset;
#if defined(_M_X64) || defined(_M_ARM64)
    OS << format("0x%016llX", PC);
#elif defined(_M_IX86) || defined(_M_ARM)
    OS << format("0x%08lX", static_cast<DWORD>(PC));
#endif

    // Verify the PC belongs to a module in this process.
    if (!fSymGetModuleBase64(hProcess, PC)) {
      OS << " <unknown module>\n";
      continue;
    }

    IMAGEHLP_MODULE64 M;
    memset(&M, 0, sizeof(IMAGEHLP_MODULE64));
    M.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
    if (fSymGetModuleInfo64(hProcess, fSymGetModuleBase64(hProcess, PC), &M)) {
      DWORD64 const disp = PC - M.BaseOfImage;
      OS << format(", %s(0x%016llX) + 0x%llX byte(s)",
                   static_cast<char *>(M.ImageName), M.BaseOfImage,
                   static_cast<long long>(disp));
    } else {
      OS << ", <unknown module>";
    }

    // Print the symbol name.
    char buffer[512];
    IMAGEHLP_SYMBOL64 *symbol = reinterpret_cast<IMAGEHLP_SYMBOL64 *>(buffer);
    memset(symbol, 0, sizeof(IMAGEHLP_SYMBOL64));
    symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
    symbol->MaxNameLength = 512 - sizeof(IMAGEHLP_SYMBOL64);

    DWORD64 dwDisp;
    if (!fSymGetSymFromAddr64(hProcess, PC, &dwDisp, symbol)) {
      OS << '\n';
      continue;
    }

    buffer[511] = 0;
    OS << format(", %s() + 0x%llX byte(s)", static_cast<char *>(symbol->Name),
                 static_cast<long long>(dwDisp));

    // Print the source file and line number information.
    IMAGEHLP_LINE64 line = {};
    DWORD dwLineDisp;
    line.SizeOfStruct = sizeof(line);
    if (fSymGetLineFromAddr64(hProcess, PC, &dwLineDisp, &line)) {
      OS << format(", %s, line %lu + 0x%lX byte(s)", line.FileName,
                   line.LineNumber, dwLineDisp);
    }

    OS << '\n';
  }
}

namespace llvm {

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only Win32 specific code
//===          and must not be UNIX code
//===----------------------------------------------------------------------===//

#ifdef _MSC_VER
/// Emulates hitting "retry" from an "abort, retry, ignore" CRT debug report
/// dialog. "retry" raises an exception which ultimately triggers our stack
/// dumper.
static LLVM_ATTRIBUTE_UNUSED int
AvoidMessageBoxHook(int ReportType, char *Message, int *Return) {
  // Set *Return to the retry code for the return value of _CrtDbgReport:
  // http://msdn.microsoft.com/en-us/library/8hyw4sy7(v=vs.71).aspx
  // This may also trigger just-in-time debugging via DebugBreak().
  if (Return)
    *Return = 1;
  // Don't call _CrtDbgReport.
  return TRUE;
}

#endif

extern "C" void HandleAbort(int Sig) {
  if (Sig == SIGABRT) {
    LLVM_BUILTIN_TRAP;
  }
}

static void InitializeThreading() {
  if (CriticalSectionInitialized)
    return;

  // Now's the time to create the critical section. This is the first time
  // through here, and there's only one thread.
  InitializeCriticalSection(&CriticalSection);
  CriticalSectionInitialized = true;
}

static void RegisterHandler() {
  // If we cannot load up the APIs (which would be unexpected as they should
  // exist on every version of Windows we support), we will bail out since
  // there would be nothing to report.
  if (!load64BitDebugHelp()) {
    assert(false && "These APIs should always be available");
    return;
  }

  if (RegisteredUnhandledExceptionFilter) {
    EnterCriticalSection(&CriticalSection);
    return;
  }

  InitializeThreading();

  // Enter it immediately.  Now if someone hits CTRL/C, the console handler
  // can't proceed until the globals are updated.
  EnterCriticalSection(&CriticalSection);

  RegisteredUnhandledExceptionFilter = true;
  OldFilter = SetUnhandledExceptionFilter(LLVMUnhandledExceptionFilter);
  SetConsoleCtrlHandler(LLVMConsoleCtrlHandler, TRUE);

  // IMPORTANT NOTE: Caller must call LeaveCriticalSection(&CriticalSection) or
  // else multi-threading problems will ensue.
}

// The public API
bool sys::RemoveFileOnSignal(StringRef Filename, std::string *ErrMsg) {
  RegisterHandler();

  if (CleanupExecuted) {
    if (ErrMsg)
      *ErrMsg = "Process terminating -- cannot register for removal";
    return true;
  }

  if (FilesToRemove == NULL)
    FilesToRemove = new std::vector<std::string>;

  FilesToRemove->push_back(std::string(Filename));

  LeaveCriticalSection(&CriticalSection);
  return false;
}

// The public API
void sys::DontRemoveFileOnSignal(StringRef Filename) {
  if (FilesToRemove == NULL)
    return;

  RegisterHandler();

  std::vector<std::string>::reverse_iterator I =
      find(reverse(*FilesToRemove), Filename);
  if (I != FilesToRemove->rend())
    FilesToRemove->erase(I.base() - 1);

  LeaveCriticalSection(&CriticalSection);
}

void sys::DisableSystemDialogsOnCrash() {
  // Crash to stack trace handler on abort.
  signal(SIGABRT, HandleAbort);

  // The following functions are not reliably accessible on MinGW.
#ifdef _MSC_VER
  // We're already handling writing a "something went wrong" message.
  _set_abort_behavior(0, _WRITE_ABORT_MSG);
  // Disable Dr. Watson.
  _set_abort_behavior(0, _CALL_REPORTFAULT);
  _CrtSetReportHook(AvoidMessageBoxHook);
#endif

  // Disable standard error dialog box.
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
               SEM_NOOPENFILEERRORBOX);
  _set_error_mode(_OUT_TO_STDERR);
}

/// When an error signal (such as SIGABRT or SIGSEGV) is delivered to the
/// process, print a stack trace and then exit.
void sys::PrintStackTraceOnErrorSignal(StringRef Argv0,
                                       bool DisableCrashReporting) {
  ::Argv0 = Argv0;

  if (DisableCrashReporting || getenv("LLVM_DISABLE_CRASH_REPORT"))
    Process::PreventCoreFiles();

  DisableSystemDialogsOnCrash();
  RegisterHandler();
  LeaveCriticalSection(&CriticalSection);
}
} // namespace llvm

#if defined(__MINGW32__) && !defined(__MINGW64_VERSION_MAJOR)
// Provide a prototype for RtlCaptureContext, mingw32 from mingw.org is
// missing it but mingw-w64 has it.
extern "C" VOID WINAPI RtlCaptureContext(PCONTEXT ContextRecord);
#endif

static void LocalPrintStackTrace(raw_ostream &OS, PCONTEXT C) {
  STACKFRAME64 StackFrame{};
  CONTEXT Context{};
  if (!C) {
    ::RtlCaptureContext(&Context);
    C = &Context;
  }
#if defined(_M_X64)
  StackFrame.AddrPC.Offset = Context.Rip;
  StackFrame.AddrStack.Offset = Context.Rsp;
  StackFrame.AddrFrame.Offset = Context.Rbp;
#elif defined(_M_IX86)
  StackFrame.AddrPC.Offset = Context.Eip;
  StackFrame.AddrStack.Offset = Context.Esp;
  StackFrame.AddrFrame.Offset = Context.Ebp;
#elif defined(_M_ARM64)
  StackFrame.AddrPC.Offset = Context.Pc;
  StackFrame.AddrStack.Offset = Context.Sp;
  StackFrame.AddrFrame.Offset = Context.Fp;
#elif defined(_M_ARM)
  StackFrame.AddrPC.Offset = Context.Pc;
  StackFrame.AddrStack.Offset = Context.Sp;
  StackFrame.AddrFrame.Offset = Context.R11;
#endif
  StackFrame.AddrPC.Mode = AddrModeFlat;
  StackFrame.AddrStack.Mode = AddrModeFlat;
  StackFrame.AddrFrame.Mode = AddrModeFlat;
  PrintStackTraceForThread(OS, GetCurrentProcess(), GetCurrentThread(),
                           StackFrame, C);
}

void llvm::sys::PrintStackTrace(raw_ostream &OS, int Depth) {
  // FIXME: Handle "Depth" parameter to print stack trace upto specified Depth
  LocalPrintStackTrace(OS, nullptr);
}

void llvm::sys::SetInterruptFunction(void (*IF)()) {
  RegisterHandler();
  InterruptFunction = IF;
  LeaveCriticalSection(&CriticalSection);
}

void llvm::sys::SetInfoSignalFunction(void (*Handler)()) {
  // Unimplemented.
}

void llvm::sys::SetOneShotPipeSignalFunction(void (*Handler)()) {
  OneShotPipeSignalFunction.exchange(Handler);
}

void llvm::sys::DefaultOneShotPipeSignalHandler() {
  llvm::sys::Process::Exit(EX_IOERR, /*NoCleanup=*/true);
}

void llvm::sys::CallOneShotPipeSignalHandler() {
  if (auto OldOneShotPipeFunction = OneShotPipeSignalFunction.exchange(nullptr))
    OldOneShotPipeFunction();
}

/// Add a function to be called when a signal is delivered to the process. The
/// handler can have a cookie passed to it to identify what instance of the
/// handler it is.
void llvm::sys::AddSignalHandler(sys::SignalHandlerCallback FnPtr,
                                 void *Cookie) {
  insertSignalHandler(FnPtr, Cookie);
  RegisterHandler();
  LeaveCriticalSection(&CriticalSection);
}

static void Cleanup(bool ExecuteSignalHandlers) {
  if (CleanupExecuted)
    return;

  EnterCriticalSection(&CriticalSection);

  // Prevent other thread from registering new files and directories for
  // removal, should we be executing because of the console handler callback.
  CleanupExecuted = true;

  // FIXME: open files cannot be deleted.
  if (FilesToRemove != NULL)
    while (!FilesToRemove->empty()) {
      llvm::sys::fs::remove(FilesToRemove->back());
      FilesToRemove->pop_back();
    }

  if (ExecuteSignalHandlers)
    llvm::sys::RunSignalHandlers();

  LeaveCriticalSection(&CriticalSection);
}

void llvm::sys::RunInterruptHandlers() {
  // The interrupt handler may be called from an interrupt, but it may also be
  // called manually (such as the case of report_fatal_error with no registered
  // error handler). We must ensure that the critical section is properly
  // initialized.
  InitializeThreading();
  Cleanup(true);
}

/// Find the Windows Registry Key for a given location.
///
/// \returns a valid HKEY if the location exists, else NULL.
static HKEY FindWERKey(const llvm::Twine &RegistryLocation) {
  HKEY Key;
  if (ERROR_SUCCESS != ::RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                       RegistryLocation.str().c_str(), 0,
                                       KEY_QUERY_VALUE | KEY_READ, &Key))
    return NULL;

  return Key;
}

/// Populate ResultDirectory with the value for "DumpFolder" for a given
/// Windows Registry key.
///
/// \returns true if a valid value for DumpFolder exists, false otherwise.
static bool GetDumpFolder(HKEY Key,
                          llvm::SmallVectorImpl<char> &ResultDirectory) {
  using llvm::sys::windows::UTF16ToUTF8;

  if (!Key)
    return false;

  DWORD BufferLengthBytes = 0;

  if (ERROR_SUCCESS != ::RegGetValueW(Key, 0, L"DumpFolder", REG_EXPAND_SZ,
                                      NULL, NULL, &BufferLengthBytes))
    return false;

  SmallVector<wchar_t, MAX_PATH> Buffer(BufferLengthBytes);

  if (ERROR_SUCCESS != ::RegGetValueW(Key, 0, L"DumpFolder", REG_EXPAND_SZ,
                                      NULL, Buffer.data(), &BufferLengthBytes))
    return false;

  DWORD ExpandBufferSize = ::ExpandEnvironmentStringsW(Buffer.data(), NULL, 0);

  if (!ExpandBufferSize)
    return false;

  SmallVector<wchar_t, MAX_PATH> ExpandBuffer(ExpandBufferSize);

  if (ExpandBufferSize != ::ExpandEnvironmentStringsW(Buffer.data(),
                                                      ExpandBuffer.data(),
                                                      ExpandBufferSize))
    return false;

  if (UTF16ToUTF8(ExpandBuffer.data(), ExpandBufferSize - 1, ResultDirectory))
    return false;

  return true;
}

/// Populate ResultType with a valid MINIDUMP_TYPE based on the value of
/// "DumpType" for a given Windows Registry key.
///
/// According to
/// https://msdn.microsoft.com/en-us/library/windows/desktop/bb787181(v=vs.85).aspx
/// valid values for DumpType are:
///   * 0: Custom dump
///   * 1: Mini dump
///   * 2: Full dump
/// If "Custom dump" is specified then the "CustomDumpFlags" field is read
/// containing a bitwise combination of MINIDUMP_TYPE values.
///
/// \returns true if a valid value for ResultType can be set, false otherwise.
static bool GetDumpType(HKEY Key, MINIDUMP_TYPE &ResultType) {
  if (!Key)
    return false;

  DWORD DumpType;
  DWORD TypeSize = sizeof(DumpType);
  if (ERROR_SUCCESS != ::RegGetValueW(Key, NULL, L"DumpType", RRF_RT_REG_DWORD,
                                      NULL, &DumpType, &TypeSize))
    return false;

  switch (DumpType) {
  case 0: {
    DWORD Flags = 0;
    if (ERROR_SUCCESS != ::RegGetValueW(Key, NULL, L"CustomDumpFlags",
                                        RRF_RT_REG_DWORD, NULL, &Flags,
                                        &TypeSize))
      return false;

    ResultType = static_cast<MINIDUMP_TYPE>(Flags);
    break;
  }
  case 1:
    ResultType = MiniDumpNormal;
    break;
  case 2:
    ResultType = MiniDumpWithFullMemory;
    break;
  default:
    return false;
  }
  return true;
}

/// Write a Windows dump file containing process information that can be
/// used for post-mortem debugging.
///
/// \returns zero error code if a mini dump created, actual error code
/// otherwise.
static std::error_code WINAPI
WriteWindowsDumpFile(PMINIDUMP_EXCEPTION_INFORMATION ExceptionInfo) {
  struct ScopedCriticalSection {
    ScopedCriticalSection() { EnterCriticalSection(&CriticalSection); }
    ~ScopedCriticalSection() { LeaveCriticalSection(&CriticalSection); }
  } SCS;

  using namespace llvm;
  using namespace llvm::sys;

  std::string MainExecutableName = fs::getMainExecutable(nullptr, nullptr);
  StringRef ProgramName;

  if (MainExecutableName.empty()) {
    // If we can't get the executable filename,
    // things are in worse shape than we realize
    // and we should just bail out.
    return mapWindowsError(::GetLastError());
  }

  ProgramName = path::filename(MainExecutableName.c_str());

  // The Windows Registry location as specified at
  // https://msdn.microsoft.com/en-us/library/windows/desktop/bb787181%28v=vs.85%29.aspx
  // "Collecting User-Mode Dumps" that may optionally be set to collect crash
  // dumps in a specified location.
  StringRef LocalDumpsRegistryLocation =
      "SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps";

  // The key pointing to the Registry location that may contain global crash
  // dump settings.  This will be NULL if the location can not be found.
  ScopedRegHandle DefaultLocalDumpsKey(FindWERKey(LocalDumpsRegistryLocation));

  // The key pointing to the Registry location that may contain
  // application-specific crash dump settings.  This will be NULL if the
  // location can not be found.
  ScopedRegHandle AppSpecificKey(
      FindWERKey(Twine(LocalDumpsRegistryLocation) + "\\" + ProgramName));

  // Look to see if a dump type is specified in the registry; first with the
  // app-specific key and failing that with the global key.  If none are found
  // default to a normal dump (GetDumpType will return false either if the key
  // is NULL or if there is no valid DumpType value at its location).
  MINIDUMP_TYPE DumpType;
  if (!GetDumpType(AppSpecificKey, DumpType))
    if (!GetDumpType(DefaultLocalDumpsKey, DumpType))
      DumpType = MiniDumpNormal;

  // Look to see if a dump location is specified on the command line.  If not,
  // look to see if a dump location is specified in the registry; first with the
  // app-specific key and failing that with the global key.  If none are found
  // we'll just create the dump file in the default temporary file location
  // (GetDumpFolder will return false either if the key is NULL or if there is
  // no valid DumpFolder value at its location).
  bool ExplicitDumpDirectorySet = true;
  SmallString<MAX_PATH> DumpDirectory(*CrashDiagnosticsDirectory);
  if (DumpDirectory.empty())
    if (!GetDumpFolder(AppSpecificKey, DumpDirectory))
      if (!GetDumpFolder(DefaultLocalDumpsKey, DumpDirectory))
        ExplicitDumpDirectorySet = false;

  int FD;
  SmallString<MAX_PATH> DumpPath;

  if (ExplicitDumpDirectorySet) {
    if (std::error_code EC = fs::create_directories(DumpDirectory))
      return EC;
    if (std::error_code EC = fs::createUniqueFile(
            Twine(DumpDirectory) + "\\" + ProgramName + ".%%%%%%.dmp", FD,
            DumpPath))
      return EC;
  } else if (std::error_code EC =
                 fs::createTemporaryFile(ProgramName, "dmp", FD, DumpPath))
    return EC;

  // Our support functions return a file descriptor but Windows wants a handle.
  ScopedCommonHandle FileHandle(reinterpret_cast<HANDLE>(_get_osfhandle(FD)));

  if (!fMiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(),
                          FileHandle, DumpType, ExceptionInfo, NULL, NULL))
    return mapWindowsError(::GetLastError());

  llvm::errs() << "Wrote crash dump file \"" << DumpPath << "\"\n";
  return std::error_code();
}

void sys::CleanupOnSignal(uintptr_t Context) {
  LPEXCEPTION_POINTERS EP = (LPEXCEPTION_POINTERS)Context;
  // Broken pipe is not a crash.
  //
  // 0xE0000000 is combined with the return code in the exception raised in
  // CrashRecoveryContext::HandleExit().
  unsigned RetCode = EP->ExceptionRecord->ExceptionCode;
  if (RetCode == (0xE0000000 | EX_IOERR))
    return;
  LLVMUnhandledExceptionFilter(EP);
}

static LONG WINAPI LLVMUnhandledExceptionFilter(LPEXCEPTION_POINTERS ep) {
  Cleanup(true);

  // Write out the exception code.
  if (ep && ep->ExceptionRecord)
    llvm::errs() << format("Exception Code: 0x%08X",
                           ep->ExceptionRecord->ExceptionCode)
                 << "\n";

  // We'll automatically write a Minidump file here to help diagnose
  // the nasty sorts of crashes that aren't 100% reproducible from a set of
  // inputs (or in the event that the user is unable or unwilling to provide a
  // reproducible case).
  if (!llvm::sys::Process::AreCoreFilesPrevented()) {
    MINIDUMP_EXCEPTION_INFORMATION ExceptionInfo;
    ExceptionInfo.ThreadId = ::GetCurrentThreadId();
    ExceptionInfo.ExceptionPointers = ep;
    ExceptionInfo.ClientPointers = FALSE;

    if (std::error_code EC = WriteWindowsDumpFile(&ExceptionInfo))
      llvm::errs() << "Could not write crash dump file: " << EC.message()
                   << "\n";
  }

  // Stack unwinding appears to modify the context. Copy it to preserve the
  // caller's context.
  CONTEXT ContextCopy;
  if (ep)
    memcpy(&ContextCopy, ep->ContextRecord, sizeof(ContextCopy));

  LocalPrintStackTrace(llvm::errs(), ep ? &ContextCopy : nullptr);

  return EXCEPTION_EXECUTE_HANDLER;
}

static BOOL WINAPI LLVMConsoleCtrlHandler(DWORD dwCtrlType) {
  // We are running in our very own thread, courtesy of Windows.
  EnterCriticalSection(&CriticalSection);
  // This function is only ever called when a CTRL-C or similar control signal
  // is fired. Killing a process in this way is normal, so don't trigger the
  // signal handlers.
  Cleanup(false);

  // If an interrupt function has been set, go and run one it; otherwise,
  // the process dies.
  void (*IF)() = InterruptFunction;
  InterruptFunction = 0; // Don't run it on another CTRL-C.

  if (IF) {
    // Note: if the interrupt function throws an exception, there is nothing
    // to catch it in this thread so it will kill the process.
    IF(); // Run it now.
    LeaveCriticalSection(&CriticalSection);
    return TRUE; // Don't kill the process.
  }

  // Allow normal processing to take place; i.e., the process dies.
  LeaveCriticalSection(&CriticalSection);
  return FALSE;
}

#if __MINGW32__
// We turned these warnings off for this file so that MinGW-g++ doesn't
// complain about the ll format specifiers used.  Now we are turning the
// warnings back on.  If MinGW starts to support diagnostic stacks, we can
// replace this with a pop.
#pragma GCC diagnostic warning "-Wformat"
#pragma GCC diagnostic warning "-Wformat-extra-args"
#endif

void sys::unregisterHandlers() {}
