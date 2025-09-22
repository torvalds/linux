//===- FindDiagnosticID.cpp - diagtool tool for finding diagnostic id -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DiagTool.h"
#include "DiagnosticNames.h"
#include "clang/Basic/AllDiagnostics.h"
#include "llvm/Support/CommandLine.h"
#include <optional>

DEF_DIAGTOOL("find-diagnostic-id", "Print the id of the given diagnostic",
             FindDiagnosticID)

using namespace clang;
using namespace diagtool;

static StringRef getNameFromID(StringRef Name) {
  int DiagID;
  if(!Name.getAsInteger(0, DiagID)) {
    const DiagnosticRecord &Diag = getDiagnosticForID(DiagID);
    return Diag.getName();
  }
  return StringRef();
}

static std::optional<DiagnosticRecord>
findDiagnostic(ArrayRef<DiagnosticRecord> Diagnostics, StringRef Name) {
  for (const auto &Diag : Diagnostics) {
    StringRef DiagName = Diag.getName();
    if (DiagName == Name)
      return Diag;
  }
  return std::nullopt;
}

int FindDiagnosticID::run(unsigned int argc, char **argv,
                          llvm::raw_ostream &OS) {
  static llvm::cl::OptionCategory FindDiagnosticIDOptions(
      "diagtool find-diagnostic-id options");

  static llvm::cl::opt<std::string> DiagnosticName(
      llvm::cl::Positional, llvm::cl::desc("<diagnostic-name>"),
      llvm::cl::Required, llvm::cl::cat(FindDiagnosticIDOptions));

  std::vector<const char *> Args;
  Args.push_back("diagtool find-diagnostic-id");
  for (const char *A : llvm::ArrayRef(argv, argc))
    Args.push_back(A);

  llvm::cl::HideUnrelatedOptions(FindDiagnosticIDOptions);
  llvm::cl::ParseCommandLineOptions((int)Args.size(), Args.data(),
                                    "Diagnostic ID mapping utility");

  ArrayRef<DiagnosticRecord> AllDiagnostics = getBuiltinDiagnosticsByName();
  std::optional<DiagnosticRecord> Diag =
      findDiagnostic(AllDiagnostics, DiagnosticName);
  if (!Diag) {
    // Name to id failed, so try id to name.
    auto Name = getNameFromID(DiagnosticName);
    if (!Name.empty()) {
      OS << Name << '\n';
      return 0;
    }

    llvm::errs() << "error: invalid diagnostic '" << DiagnosticName << "'\n";
    return 1;
  }
  OS << Diag->DiagID << "\n";
  return 0;
}
