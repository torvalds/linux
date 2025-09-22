//===- CLog.h - Logging Interface -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_LIBCLANG_CLOG_H
#define LLVM_CLANG_TOOLS_LIBCLANG_CLOG_H

#include "clang-c/Index.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

namespace llvm {
class format_object_base;
}

namespace clang {
namespace cxindex {

class Logger;
typedef IntrusiveRefCntPtr<Logger> LogRef;

/// Collects logging output and writes it to stderr when it's destructed.
/// Common use case:
/// \code
///   if (LogRef Log = Logger::make(__func__)) {
///     *Log << "stuff";
///   }
/// \endcode
class Logger : public RefCountedBase<Logger> {
  std::string Name;
  bool Trace;
  SmallString<64> Msg;
  llvm::raw_svector_ostream LogOS;
public:
  static const char *getEnvVar() {
    static const char *sCachedVar = ::getenv("LIBCLANG_LOGGING");
    return sCachedVar;
  }
  static bool isLoggingEnabled() { return getEnvVar() != nullptr; }
  static bool isStackTracingEnabled() {
    if (const char *EnvOpt = Logger::getEnvVar())
      return llvm::StringRef(EnvOpt) == "2";
    return false;
  }
  static LogRef make(llvm::StringRef name,
                     bool trace = isStackTracingEnabled()) {
    if (isLoggingEnabled())
      return new Logger(name, trace);
    return nullptr;
  }

  explicit Logger(llvm::StringRef name, bool trace)
      : Name(std::string(name)), Trace(trace), LogOS(Msg) {}
  ~Logger();

  Logger &operator<<(CXTranslationUnit);
  Logger &operator<<(FileEntryRef FE);
  Logger &operator<<(CXCursor cursor);
  Logger &operator<<(CXSourceLocation);
  Logger &operator<<(CXSourceRange);
  Logger &operator<<(CXString);
  Logger &operator<<(llvm::StringRef Str) { LogOS << Str; return *this; }
  Logger &operator<<(const char *Str) {
    if (Str)
      LogOS << Str;
    return *this;
  }
  Logger &operator<<(unsigned long N) { LogOS << N; return *this; }
  Logger &operator<<(long N) { LogOS << N ; return *this; }
  Logger &operator<<(unsigned int N) { LogOS << N; return *this; }
  Logger &operator<<(int N) { LogOS << N; return *this; }
  Logger &operator<<(char C) { LogOS << C; return *this; }
  Logger &operator<<(unsigned char C) { LogOS << C; return *this; }
  Logger &operator<<(signed char C) { LogOS << C; return *this; }
  Logger &operator<<(const llvm::format_object_base &Fmt);
};

}
}

/// Macros to automate common uses of Logger. Like this:
/// \code
///   LOG_FUNC_SECTION {
///     *Log << "blah";
///   }
/// \endcode
#define LOG_SECTION(NAME) \
    if (clang::cxindex::LogRef Log = clang::cxindex::Logger::make(NAME))
#define LOG_FUNC_SECTION LOG_SECTION(__func__)

#endif
