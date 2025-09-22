//===- TextDiagnosticBuffer.cpp - Buffer Text Diagnostics -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a concrete diagnostic client, which buffers the diagnostic messages.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/ErrorHandling.h"

using namespace clang;

/// HandleDiagnostic - Store the errors, warnings, and notes that are
/// reported.
void TextDiagnosticBuffer::HandleDiagnostic(DiagnosticsEngine::Level Level,
                                            const Diagnostic &Info) {
  // Default implementation (Warnings/errors count).
  DiagnosticConsumer::HandleDiagnostic(Level, Info);

  SmallString<100> Buf;
  Info.FormatDiagnostic(Buf);
  switch (Level) {
  default: llvm_unreachable(
                         "Diagnostic not handled during diagnostic buffering!");
  case DiagnosticsEngine::Note:
    All.emplace_back(Level, Notes.size());
    Notes.emplace_back(Info.getLocation(), std::string(Buf));
    break;
  case DiagnosticsEngine::Warning:
    All.emplace_back(Level, Warnings.size());
    Warnings.emplace_back(Info.getLocation(), std::string(Buf));
    break;
  case DiagnosticsEngine::Remark:
    All.emplace_back(Level, Remarks.size());
    Remarks.emplace_back(Info.getLocation(), std::string(Buf));
    break;
  case DiagnosticsEngine::Error:
  case DiagnosticsEngine::Fatal:
    All.emplace_back(Level, Errors.size());
    Errors.emplace_back(Info.getLocation(), std::string(Buf));
    break;
  }
}

void TextDiagnosticBuffer::FlushDiagnostics(DiagnosticsEngine &Diags) const {
  for (const auto &I : All) {
    auto Diag = Diags.Report(Diags.getCustomDiagID(I.first, "%0"));
    switch (I.first) {
    default: llvm_unreachable(
                           "Diagnostic not handled during diagnostic flushing!");
    case DiagnosticsEngine::Note:
      Diag << Notes[I.second].second;
      break;
    case DiagnosticsEngine::Warning:
      Diag << Warnings[I.second].second;
      break;
    case DiagnosticsEngine::Remark:
      Diag << Remarks[I.second].second;
      break;
    case DiagnosticsEngine::Error:
    case DiagnosticsEngine::Fatal:
      Diag << Errors[I.second].second;
      break;
    }
  }
}
