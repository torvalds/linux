//===-- DiagnosticManager.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/DiagnosticManager.h"

#include "llvm/Support/ErrorHandling.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb_private;

void DiagnosticManager::Dump(Log *log) {
  if (!log)
    return;

  std::string str = GetString();

  // GetString() puts a separator after each diagnostic. We want to remove the
  // last '\n' because log->PutCString will add one for us.

  if (str.size() && str.back() == '\n') {
    str.pop_back();
  }

  log->PutCString(str.c_str());
}

static const char *StringForSeverity(lldb::Severity severity) {
  switch (severity) {
  // this should be exhaustive
  case lldb::eSeverityError:
    return "error: ";
  case lldb::eSeverityWarning:
    return "warning: ";
  case lldb::eSeverityInfo:
    return "";
  }
  llvm_unreachable("switch needs another case for lldb::Severity enum");
}

std::string DiagnosticManager::GetString(char separator) {
  std::string ret;
  llvm::raw_string_ostream stream(ret);

  for (const auto &diagnostic : Diagnostics()) {
    llvm::StringRef severity = StringForSeverity(diagnostic->GetSeverity());
    stream << severity;

    llvm::StringRef message = diagnostic->GetMessage();
    std::string searchable_message = message.lower();
    auto severity_pos = message.find(severity);
    stream << message.take_front(severity_pos);

    if (severity_pos != llvm::StringRef::npos)
      stream << message.drop_front(severity_pos + severity.size());
    stream << separator;
  }

  return ret;
}

size_t DiagnosticManager::Printf(lldb::Severity severity, const char *format,
                                 ...) {
  StreamString ss;

  va_list args;
  va_start(args, format);
  size_t result = ss.PrintfVarArg(format, args);
  va_end(args);

  AddDiagnostic(ss.GetString(), severity, eDiagnosticOriginLLDB);

  return result;
}

void DiagnosticManager::PutString(lldb::Severity severity,
                                  llvm::StringRef str) {
  if (str.empty())
    return;
  AddDiagnostic(str, severity, eDiagnosticOriginLLDB);
}
