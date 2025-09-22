//===-- SourceBreakpoint.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_SOURCEBREAKPOINT_H
#define LLDB_TOOLS_LLDB_DAP_SOURCEBREAKPOINT_H

#include "Breakpoint.h"
#include "llvm/ADT/StringRef.h"

namespace lldb_dap {

struct SourceBreakpoint : public Breakpoint {
  // logMessage part can be either a raw text or an expression.
  struct LogMessagePart {
    LogMessagePart(llvm::StringRef text, bool is_expr)
        : text(text), is_expr(is_expr) {}
    std::string text;
    bool is_expr;
  };
  // If this attribute exists and is non-empty, the backend must not 'break'
  // (stop) but log the message instead. Expressions within {} are
  // interpolated.
  std::string logMessage;
  std::vector<LogMessagePart> logMessageParts;

  uint32_t line;   ///< The source line of the breakpoint or logpoint
  uint32_t column; ///< An optional source column of the breakpoint

  SourceBreakpoint() : Breakpoint(), line(0), column(0) {}
  SourceBreakpoint(const llvm::json::Object &obj);

  // Set this breakpoint in LLDB as a new breakpoint
  void SetBreakpoint(const llvm::StringRef source_path);
  void UpdateBreakpoint(const SourceBreakpoint &request_bp);

  void SetLogMessage();
  // Format \param text and return formatted text in \param formatted.
  // \return any formatting failures.
  lldb::SBError FormatLogText(llvm::StringRef text, std::string &formatted);
  lldb::SBError AppendLogMessagePart(llvm::StringRef part, bool is_expr);
  void NotifyLogMessageError(llvm::StringRef error);

  static bool BreakpointHitCallback(void *baton, lldb::SBProcess &process,
                                    lldb::SBThread &thread,
                                    lldb::SBBreakpointLocation &location);
};

inline bool operator<(const SourceBreakpoint &lhs,
                      const SourceBreakpoint &rhs) {
  if (lhs.line == rhs.line)
    return lhs.column < rhs.column;
  return lhs.line < rhs.line;
}

} // namespace lldb_dap

#endif
