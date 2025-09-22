//===- Signals.cpp - Signal Handling support --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some helpful functions for dealing with the possibility of
// Unix signals occurring while your program is running.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Signals.h"

#include "DebugOptions.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FileUtilities.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/raw_ostream.h"
#include <array>
#include <cmath>
#include <vector>

//===----------------------------------------------------------------------===//
//=== WARNING: Implementation here must contain only TRULY operating system
//===          independent code.
//===----------------------------------------------------------------------===//

using namespace llvm;

// Use explicit storage to avoid accessing cl::opt in a signal handler.
static bool DisableSymbolicationFlag = false;
static ManagedStatic<std::string> CrashDiagnosticsDirectory;
namespace {
struct CreateDisableSymbolication {
  static void *call() {
    return new cl::opt<bool, true>(
        "disable-symbolication",
        cl::desc("Disable symbolizing crash backtraces."),
        cl::location(DisableSymbolicationFlag), cl::Hidden);
  }
};
struct CreateCrashDiagnosticsDir {
  static void *call() {
    return new cl::opt<std::string, true>(
        "crash-diagnostics-dir", cl::value_desc("directory"),
        cl::desc("Directory for crash diagnostic files."),
        cl::location(*CrashDiagnosticsDirectory), cl::Hidden);
  }
};
} // namespace
void llvm::initSignalsOptions() {
  static ManagedStatic<cl::opt<bool, true>, CreateDisableSymbolication>
      DisableSymbolication;
  static ManagedStatic<cl::opt<std::string, true>, CreateCrashDiagnosticsDir>
      CrashDiagnosticsDir;
  *DisableSymbolication;
  *CrashDiagnosticsDir;
}

constexpr char DisableSymbolizationEnv[] = "LLVM_DISABLE_SYMBOLIZATION";
constexpr char LLVMSymbolizerPathEnv[] = "LLVM_SYMBOLIZER_PATH";
constexpr char EnableSymbolizerMarkupEnv[] = "LLVM_ENABLE_SYMBOLIZER_MARKUP";

// Callbacks to run in signal handler must be lock-free because a signal handler
// could be running as we add new callbacks. We don't add unbounded numbers of
// callbacks, an array is therefore sufficient.
struct CallbackAndCookie {
  sys::SignalHandlerCallback Callback;
  void *Cookie;
  enum class Status { Empty, Initializing, Initialized, Executing };
  std::atomic<Status> Flag;
};

static constexpr size_t MaxSignalHandlerCallbacks = 8;

// A global array of CallbackAndCookie may not compile with
// -Werror=global-constructors in c++20 and above
static std::array<CallbackAndCookie, MaxSignalHandlerCallbacks> &
CallBacksToRun() {
  static std::array<CallbackAndCookie, MaxSignalHandlerCallbacks> callbacks;
  return callbacks;
}

// Signal-safe.
void sys::RunSignalHandlers() {
  for (CallbackAndCookie &RunMe : CallBacksToRun()) {
    auto Expected = CallbackAndCookie::Status::Initialized;
    auto Desired = CallbackAndCookie::Status::Executing;
    if (!RunMe.Flag.compare_exchange_strong(Expected, Desired))
      continue;
    (*RunMe.Callback)(RunMe.Cookie);
    RunMe.Callback = nullptr;
    RunMe.Cookie = nullptr;
    RunMe.Flag.store(CallbackAndCookie::Status::Empty);
  }
}

// Signal-safe.
static void insertSignalHandler(sys::SignalHandlerCallback FnPtr,
                                void *Cookie) {
  for (CallbackAndCookie &SetMe : CallBacksToRun()) {
    auto Expected = CallbackAndCookie::Status::Empty;
    auto Desired = CallbackAndCookie::Status::Initializing;
    if (!SetMe.Flag.compare_exchange_strong(Expected, Desired))
      continue;
    SetMe.Callback = FnPtr;
    SetMe.Cookie = Cookie;
    SetMe.Flag.store(CallbackAndCookie::Status::Initialized);
    return;
  }
  report_fatal_error("too many signal callbacks already registered");
}

static bool findModulesAndOffsets(void **StackTrace, int Depth,
                                  const char **Modules, intptr_t *Offsets,
                                  const char *MainExecutableName,
                                  StringSaver &StrPool);

/// Format a pointer value as hexadecimal. Zero pad it out so its always the
/// same width.
static FormattedNumber format_ptr(void *PC) {
  // Each byte is two hex digits plus 2 for the 0x prefix.
  unsigned PtrWidth = 2 + 2 * sizeof(void *);
  return format_hex((uint64_t)PC, PtrWidth);
}

/// Helper that launches llvm-symbolizer and symbolizes a backtrace.
LLVM_ATTRIBUTE_USED
static bool printSymbolizedStackTrace(StringRef Argv0, void **StackTrace,
                                      int Depth, llvm::raw_ostream &OS) {
  if (DisableSymbolicationFlag || getenv(DisableSymbolizationEnv))
    return false;

  // Don't recursively invoke the llvm-symbolizer binary.
  if (Argv0.contains("llvm-symbolizer"))
    return false;

  // FIXME: Subtract necessary number from StackTrace entries to turn return addresses
  // into actual instruction addresses.
  // Use llvm-symbolizer tool to symbolize the stack traces. First look for it
  // alongside our binary, then in $PATH.
  ErrorOr<std::string> LLVMSymbolizerPathOrErr = std::error_code();
  if (const char *Path = getenv(LLVMSymbolizerPathEnv)) {
    LLVMSymbolizerPathOrErr = sys::findProgramByName(Path);
  } else if (!Argv0.empty()) {
    StringRef Parent = llvm::sys::path::parent_path(Argv0);
    if (!Parent.empty())
      LLVMSymbolizerPathOrErr = sys::findProgramByName("llvm-symbolizer", Parent);
  }
  if (!LLVMSymbolizerPathOrErr)
    LLVMSymbolizerPathOrErr = sys::findProgramByName("llvm-symbolizer");
  if (!LLVMSymbolizerPathOrErr)
    return false;
  const std::string &LLVMSymbolizerPath = *LLVMSymbolizerPathOrErr;

  // If we don't know argv0 or the address of main() at this point, try
  // to guess it anyway (it's possible on some platforms).
  std::string MainExecutableName =
      sys::fs::exists(Argv0) ? (std::string)std::string(Argv0)
                             : sys::fs::getMainExecutable(nullptr, nullptr);
  BumpPtrAllocator Allocator;
  StringSaver StrPool(Allocator);
  std::vector<const char *> Modules(Depth, nullptr);
  std::vector<intptr_t> Offsets(Depth, 0);
  if (!findModulesAndOffsets(StackTrace, Depth, Modules.data(), Offsets.data(),
                             MainExecutableName.c_str(), StrPool))
    return false;
  int InputFD;
  SmallString<32> InputFile, OutputFile;
  sys::fs::createTemporaryFile("symbolizer-input", "", InputFD, InputFile);
  sys::fs::createTemporaryFile("symbolizer-output", "", OutputFile);
  FileRemover InputRemover(InputFile.c_str());
  FileRemover OutputRemover(OutputFile.c_str());

  {
    raw_fd_ostream Input(InputFD, true);
    for (int i = 0; i < Depth; i++) {
      if (Modules[i])
        Input << Modules[i] << " " << (void*)Offsets[i] << "\n";
    }
  }

  std::optional<StringRef> Redirects[] = {InputFile.str(), OutputFile.str(),
                                          StringRef("")};
  StringRef Args[] = {"llvm-symbolizer", "--functions=linkage", "--inlining",
#ifdef _WIN32
                      // Pass --relative-address on Windows so that we don't
                      // have to add ImageBase from PE file.
                      // FIXME: Make this the default for llvm-symbolizer.
                      "--relative-address",
#endif
                      "--demangle"};
  int RunResult =
      sys::ExecuteAndWait(LLVMSymbolizerPath, Args, std::nullopt, Redirects);
  if (RunResult != 0)
    return false;

  // This report format is based on the sanitizer stack trace printer.  See
  // sanitizer_stacktrace_printer.cc in compiler-rt.
  auto OutputBuf = MemoryBuffer::getFile(OutputFile.c_str());
  if (!OutputBuf)
    return false;
  StringRef Output = OutputBuf.get()->getBuffer();
  SmallVector<StringRef, 32> Lines;
  Output.split(Lines, "\n");
  auto CurLine = Lines.begin();
  int frame_no = 0;
  for (int i = 0; i < Depth; i++) {
    auto PrintLineHeader = [&]() {
      OS << right_justify(formatv("#{0}", frame_no++).str(),
                          std::log10(Depth) + 2)
         << ' ' << format_ptr(StackTrace[i]) << ' ';
    };
    if (!Modules[i]) {
      PrintLineHeader();
      OS << '\n';
      continue;
    }
    // Read pairs of lines (function name and file/line info) until we
    // encounter empty line.
    for (;;) {
      if (CurLine == Lines.end())
        return false;
      StringRef FunctionName = *CurLine++;
      if (FunctionName.empty())
        break;
      PrintLineHeader();
      if (!FunctionName.starts_with("??"))
        OS << FunctionName << ' ';
      if (CurLine == Lines.end())
        return false;
      StringRef FileLineInfo = *CurLine++;
      if (!FileLineInfo.starts_with("??"))
        OS << FileLineInfo;
      else
        OS << "(" << Modules[i] << '+' << format_hex(Offsets[i], 0) << ")";
      OS << "\n";
    }
  }
  return true;
}

static bool printMarkupContext(raw_ostream &OS, const char *MainExecutableName);

LLVM_ATTRIBUTE_USED
static bool printMarkupStackTrace(StringRef Argv0, void **StackTrace, int Depth,
                                  raw_ostream &OS) {
  const char *Env = getenv(EnableSymbolizerMarkupEnv);
  if (!Env || !*Env)
    return false;

  std::string MainExecutableName =
      sys::fs::exists(Argv0) ? std::string(Argv0)
                             : sys::fs::getMainExecutable(nullptr, nullptr);
  if (!printMarkupContext(OS, MainExecutableName.c_str()))
    return false;
  for (int I = 0; I < Depth; I++)
    OS << format("{{{bt:%d:%#016x}}}\n", I, StackTrace[I]);
  return true;
}

// Include the platform-specific parts of this class.
#ifdef LLVM_ON_UNIX
#include "Unix/Signals.inc"
#endif
#ifdef _WIN32
#include "Windows/Signals.inc"
#endif
