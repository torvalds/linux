//===-- InitLLVM.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/InitLLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/AutoConvert.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SwapByteOrder.h"

#ifdef _WIN32
#include "llvm/Support/Windows/WindowsSupport.h"
#endif

#ifdef __MVS__
#include <unistd.h>

void CleanupStdHandles(void *Cookie) {
  llvm::raw_ostream *Outs = &llvm::outs(), *Errs = &llvm::errs();
  Outs->flush();
  Errs->flush();
  llvm::restoreStdHandleAutoConversion(STDIN_FILENO);
  llvm::restoreStdHandleAutoConversion(STDOUT_FILENO);
  llvm::restoreStdHandleAutoConversion(STDERR_FILENO);
}
#endif

using namespace llvm;
using namespace llvm::sys;

InitLLVM::InitLLVM(int &Argc, const char **&Argv,
                   bool InstallPipeSignalExitHandler) {
#ifndef NDEBUG
  static std::atomic<bool> Initialized{false};
  assert(!Initialized && "InitLLVM was already initialized!");
  Initialized = true;
#endif
#ifdef __MVS__
  // Bring stdin/stdout/stderr into a known state.
  sys::AddSignalHandler(CleanupStdHandles, nullptr);
#endif
  if (InstallPipeSignalExitHandler)
    // The pipe signal handler must be installed before any other handlers are
    // registered. This is because the Unix \ref RegisterHandlers function does
    // not perform a sigaction() for SIGPIPE unless a one-shot handler is
    // present, to allow long-lived processes (like lldb) to fully opt-out of
    // llvm's SIGPIPE handling and ignore the signal safely.
    sys::SetOneShotPipeSignalFunction(sys::DefaultOneShotPipeSignalHandler);
  // Initialize the stack printer after installing the one-shot pipe signal
  // handler, so we can perform a sigaction() for SIGPIPE on Unix if requested.
  StackPrinter.emplace(Argc, Argv);
  sys::PrintStackTraceOnErrorSignal(Argv[0]);
  install_out_of_memory_new_handler();

#ifdef __MVS__

  // We use UTF-8 as the internal character encoding. On z/OS, all external
  // output is encoded in EBCDIC. In order to be able to read all
  // error messages, we turn conversion to EBCDIC on for stderr fd.
  std::string Banner = std::string(Argv[0]) + ": ";
  ExitOnError ExitOnErr(Banner);

  // If turning on conversion for stderr fails then the error message
  // may be garbled. There is no solution to this problem.
  ExitOnErr(errorCodeToError(llvm::enableAutoConversion(STDERR_FILENO)));
  ExitOnErr(errorCodeToError(llvm::enableAutoConversion(STDOUT_FILENO)));
#endif

#ifdef _WIN32
  // We use UTF-8 as the internal character encoding. On Windows,
  // arguments passed to main() may not be encoded in UTF-8. In order
  // to reliably detect encoding of command line arguments, we use an
  // Windows API to obtain arguments, convert them to UTF-8, and then
  // write them back to the Argv vector.
  //
  // There's probably other way to do the same thing (e.g. using
  // wmain() instead of main()), but this way seems less intrusive
  // than that.
  std::string Banner = std::string(Argv[0]) + ": ";
  ExitOnError ExitOnErr(Banner);

  ExitOnErr(errorCodeToError(windows::GetCommandLineArguments(Args, Alloc)));

  // GetCommandLineArguments doesn't terminate the vector with a
  // nullptr.  Do it to make it compatible with the real argv.
  Args.push_back(nullptr);

  Argc = Args.size() - 1;
  Argv = Args.data();
#endif
}

InitLLVM::~InitLLVM() {
#ifdef __MVS__
  CleanupStdHandles(nullptr);
#endif
  llvm_shutdown();
}
