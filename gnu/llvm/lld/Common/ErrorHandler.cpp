//===- ErrorHandler.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lld/Common/ErrorHandler.h"

#include "llvm/Support/Parallel.h"

#include "lld/Common/CommonLinkerContext.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <regex>

using namespace llvm;
using namespace lld;

static StringRef getSeparator(const Twine &msg) {
  if (StringRef(msg.str()).contains('\n'))
    return "\n";
  return "";
}

ErrorHandler::~ErrorHandler() {
  if (cleanupCallback)
    cleanupCallback();
}

void ErrorHandler::initialize(llvm::raw_ostream &stdoutOS,
                              llvm::raw_ostream &stderrOS, bool exitEarly,
                              bool disableOutput) {
  this->stdoutOS = &stdoutOS;
  this->stderrOS = &stderrOS;
  stderrOS.enable_colors(stderrOS.has_colors());
  this->exitEarly = exitEarly;
  this->disableOutput = disableOutput;
}

void ErrorHandler::flushStreams() {
  std::lock_guard<std::mutex> lock(mu);
  outs().flush();
  errs().flush();
}

ErrorHandler &lld::errorHandler() { return context().e; }

void lld::error(const Twine &msg) { errorHandler().error(msg); }
void lld::error(const Twine &msg, ErrorTag tag, ArrayRef<StringRef> args) {
  errorHandler().error(msg, tag, args);
}
void lld::fatal(const Twine &msg) { errorHandler().fatal(msg); }
void lld::log(const Twine &msg) { errorHandler().log(msg); }
void lld::message(const Twine &msg, llvm::raw_ostream &s) {
  errorHandler().message(msg, s);
}
void lld::warn(const Twine &msg) { errorHandler().warn(msg); }
uint64_t lld::errorCount() { return errorHandler().errorCount; }

raw_ostream &lld::outs() {
  ErrorHandler &e = errorHandler();
  return e.outs();
}

raw_ostream &lld::errs() {
  ErrorHandler &e = errorHandler();
  return e.errs();
}

raw_ostream &ErrorHandler::outs() {
  if (disableOutput)
    return llvm::nulls();
  return stdoutOS ? *stdoutOS : llvm::outs();
}

raw_ostream &ErrorHandler::errs() {
  if (disableOutput)
    return llvm::nulls();
  return stderrOS ? *stderrOS : llvm::errs();
}

void lld::exitLld(int val) {
  if (hasContext()) {
    ErrorHandler &e = errorHandler();
    // Delete any temporary file, while keeping the memory mapping open.
    if (e.outputBuffer)
      e.outputBuffer->discard();
  }

  // Re-throw a possible signal or exception once/if it was caught by
  // safeLldMain().
  CrashRecoveryContext::throwIfCrash(val);

  // Dealloc/destroy ManagedStatic variables before calling _exit().
  // In an LTO build, allows us to get the output of -time-passes.
  // Ensures that the thread pool for the parallel algorithms is stopped to
  // avoid intermittent crashes on Windows when exiting.
  if (!CrashRecoveryContext::GetCurrent())
    llvm_shutdown();

  if (hasContext())
    lld::errorHandler().flushStreams();

  // When running inside safeLldMain(), restore the control flow back to the
  // CrashRecoveryContext. Otherwise simply use _exit(), meanning no cleanup,
  // since we want to avoid further crashes on shutdown.
  llvm::sys::Process::Exit(val, /*NoCleanup=*/true);
}

void lld::diagnosticHandler(const DiagnosticInfo &di) {
  SmallString<128> s;
  raw_svector_ostream os(s);
  DiagnosticPrinterRawOStream dp(os);

  // For an inline asm diagnostic, prepend the module name to get something like
  // "$module <inline asm>:1:5: ".
  if (auto *dism = dyn_cast<DiagnosticInfoSrcMgr>(&di))
    if (dism->isInlineAsmDiag())
      os << dism->getModuleName() << ' ';

  di.print(dp);
  switch (di.getSeverity()) {
  case DS_Error:
    error(s);
    break;
  case DS_Warning:
    warn(s);
    break;
  case DS_Remark:
  case DS_Note:
    message(s);
    break;
  }
}

void lld::checkError(Error e) {
  handleAllErrors(std::move(e),
                  [&](ErrorInfoBase &eib) { error(eib.message()); });
}

// This is for --vs-diagnostics.
//
// Normally, lld's error message starts with argv[0]. Therefore, it usually
// looks like this:
//
//   ld.lld: error: ...
//
// This error message style is unfortunately unfriendly to Visual Studio
// IDE. VS interprets the first word of the first line as an error location
// and make it clickable, thus "ld.lld" in the above message would become a
// clickable text. When you click it, VS opens "ld.lld" executable file with
// a binary editor.
//
// As a workaround, we print out an error location instead of "ld.lld" if
// lld is running in VS diagnostics mode. As a result, error message will
// look like this:
//
//   src/foo.c(35): error: ...
//
// This function returns an error location string. An error location is
// extracted from an error message using regexps.
std::string ErrorHandler::getLocation(const Twine &msg) {
  if (!vsDiagnostics)
    return std::string(logName);

  static std::regex regexes[] = {
      std::regex(
          R"(^undefined (?:\S+ )?symbol:.*\n)"
          R"(>>> referenced by .+\((\S+):(\d+)\))"),
      std::regex(
          R"(^undefined (?:\S+ )?symbol:.*\n>>> referenced by (\S+):(\d+))"),
      std::regex(R"(^undefined symbol:.*\n>>> referenced by (.*):)"),
      std::regex(
          R"(^duplicate symbol: .*\n>>> defined in (\S+)\n>>> defined in.*)"),
      std::regex(
          R"(^duplicate symbol: .*\n>>> defined at .+\((\S+):(\d+)\))"),
      std::regex(R"(^duplicate symbol: .*\n>>> defined at (\S+):(\d+))"),
      std::regex(
          R"(.*\n>>> defined in .*\n>>> referenced by .+\((\S+):(\d+)\))"),
      std::regex(R"(.*\n>>> defined in .*\n>>> referenced by (\S+):(\d+))"),
      std::regex(R"((\S+):(\d+): unclosed quote)"),
  };

  std::string str = msg.str();
  for (std::regex &re : regexes) {
    std::smatch m;
    if (!std::regex_search(str, m, re))
      continue;

    assert(m.size() == 2 || m.size() == 3);
    if (m.size() == 2)
      return m.str(1);
    return m.str(1) + "(" + m.str(2) + ")";
  }

  return std::string(logName);
}

void ErrorHandler::reportDiagnostic(StringRef location, Colors c,
                                    StringRef diagKind, const Twine &msg) {
  SmallString<256> buf;
  raw_svector_ostream os(buf);
  os << sep << location << ": ";
  if (!diagKind.empty()) {
    if (lld::errs().colors_enabled()) {
      os.enable_colors(true);
      os << c << diagKind << ": " << Colors::RESET;
    } else {
      os << diagKind << ": ";
    }
  }
  os << msg << '\n';
  lld::errs() << buf;
}

void ErrorHandler::log(const Twine &msg) {
  if (!verbose || disableOutput)
    return;
  std::lock_guard<std::mutex> lock(mu);
  reportDiagnostic(logName, Colors::RESET, "", msg);
}

void ErrorHandler::message(const Twine &msg, llvm::raw_ostream &s) {
  if (disableOutput)
    return;
  std::lock_guard<std::mutex> lock(mu);
  s << msg << "\n";
  s.flush();
}

void ErrorHandler::warn(const Twine &msg) {
  if (fatalWarnings) {
    error(msg);
    return;
  }

  if (suppressWarnings)
    return;

  std::lock_guard<std::mutex> lock(mu);
  reportDiagnostic(getLocation(msg), Colors::MAGENTA, "warning", msg);
  sep = getSeparator(msg);
}

void ErrorHandler::error(const Twine &msg) {
  // If Visual Studio-style error message mode is enabled,
  // this particular error is printed out as two errors.
  if (vsDiagnostics) {
    static std::regex re(R"(^(duplicate symbol: .*))"
                         R"((\n>>> defined at \S+:\d+.*\n>>>.*))"
                         R"((\n>>> defined at \S+:\d+.*\n>>>.*))");
    std::string str = msg.str();
    std::smatch m;

    if (std::regex_match(str, m, re)) {
      error(m.str(1) + m.str(2));
      error(m.str(1) + m.str(3));
      return;
    }
  }

  bool exit = false;
  {
    std::lock_guard<std::mutex> lock(mu);

    if (errorLimit == 0 || errorCount < errorLimit) {
      reportDiagnostic(getLocation(msg), Colors::RED, "error", msg);
    } else if (errorCount == errorLimit) {
      reportDiagnostic(logName, Colors::RED, "error", errorLimitExceededMsg);
      exit = exitEarly;
    }

    sep = getSeparator(msg);
    ++errorCount;
  }

  if (exit)
    exitLld(1);
}

void ErrorHandler::error(const Twine &msg, ErrorTag tag,
                         ArrayRef<StringRef> args) {
  if (errorHandlingScript.empty()) {
    error(msg);
    return;
  }
  SmallVector<StringRef, 4> scriptArgs;
  scriptArgs.push_back(errorHandlingScript);
  switch (tag) {
  case ErrorTag::LibNotFound:
    scriptArgs.push_back("missing-lib");
    break;
  case ErrorTag::SymbolNotFound:
    scriptArgs.push_back("undefined-symbol");
    break;
  }
  scriptArgs.insert(scriptArgs.end(), args.begin(), args.end());
  int res = llvm::sys::ExecuteAndWait(errorHandlingScript, scriptArgs);
  if (res == 0) {
    return error(msg);
  } else {
    // Temporarily disable error limit to make sure the two calls to error(...)
    // only count as one.
    uint64_t currentErrorLimit = errorLimit;
    errorLimit = 0;
    error(msg);
    errorLimit = currentErrorLimit;
    --errorCount;

    switch (res) {
    case -1:
      error("error handling script '" + errorHandlingScript +
            "' failed to execute");
      break;
    case -2:
      error("error handling script '" + errorHandlingScript +
            "' crashed or timeout");
      break;
    default:
      error("error handling script '" + errorHandlingScript +
            "' exited with code " + Twine(res));
    }
  }
}

void ErrorHandler::fatal(const Twine &msg) {
  error(msg);
  exitLld(1);
}
