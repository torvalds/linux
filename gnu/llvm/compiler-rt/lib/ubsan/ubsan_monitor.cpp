//===-- ubsan_monitor.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hooks which allow a monitor process to inspect UBSan's diagnostics.
//
//===----------------------------------------------------------------------===//

#include "ubsan_monitor.h"

using namespace __ubsan;

UndefinedBehaviorReport::UndefinedBehaviorReport(const char *IssueKind,
                                                 Location &Loc,
                                                 InternalScopedString &Msg)
    : IssueKind(IssueKind), Loc(Loc) {
  // We have the common sanitizer reporting lock, so it's safe to register a
  // new UB report.
  RegisterUndefinedBehaviorReport(this);

  // Make a copy of the diagnostic.
  if (Msg.length())
    Buffer.Append(Msg.data());

  // Let the monitor know that a report is available.
  __ubsan_on_report();
}

static UndefinedBehaviorReport *CurrentUBR;

void __ubsan::RegisterUndefinedBehaviorReport(UndefinedBehaviorReport *UBR) {
  CurrentUBR = UBR;
}

SANITIZER_WEAK_DEFAULT_IMPL
void __ubsan::__ubsan_on_report(void) {}

void __ubsan::__ubsan_get_current_report_data(const char **OutIssueKind,
                                              const char **OutMessage,
                                              const char **OutFilename,
                                              unsigned *OutLine,
                                              unsigned *OutCol,
                                              char **OutMemoryAddr) {
  if (!OutIssueKind || !OutMessage || !OutFilename || !OutLine || !OutCol ||
      !OutMemoryAddr)
    UNREACHABLE("Invalid arguments passed to __ubsan_get_current_report_data");

  InternalScopedString &Buf = CurrentUBR->Buffer;

  // Ensure that the first character of the diagnostic text can't start with a
  // lowercase letter.
  char FirstChar = *Buf.data();
  if (FirstChar >= 'a' && FirstChar <= 'z')
    *Buf.data() += 'A' - 'a';

  *OutIssueKind = CurrentUBR->IssueKind;
  *OutMessage = Buf.data();
  if (!CurrentUBR->Loc.isSourceLocation()) {
    *OutFilename = "<unknown>";
    *OutLine = *OutCol = 0;
  } else {
    SourceLocation SL = CurrentUBR->Loc.getSourceLocation();
    *OutFilename = SL.getFilename();
    *OutLine = SL.getLine();
    *OutCol = SL.getColumn();
  }

  if (CurrentUBR->Loc.isMemoryLocation())
    *OutMemoryAddr = (char *)CurrentUBR->Loc.getMemoryLocation();
  else
    *OutMemoryAddr = nullptr;
}
