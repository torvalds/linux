//===- ListWarnings.h - diagtool tool for printing warning flags ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a diagtool tool that displays warning flags for
// diagnostics.
//
//===----------------------------------------------------------------------===//

#include "DiagTool.h"
#include "DiagnosticNames.h"
#include "clang/Basic/AllDiagnostics.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Format.h"

DEF_DIAGTOOL("list-warnings",
             "List warnings and their corresponding flags",
             ListWarnings)

using namespace clang;
using namespace diagtool;

namespace {
struct Entry {
  llvm::StringRef DiagName;
  llvm::StringRef Flag;

  Entry(llvm::StringRef diagN, llvm::StringRef flag)
    : DiagName(diagN), Flag(flag) {}

  bool operator<(const Entry &x) const { return DiagName < x.DiagName; }
};
}

static void printEntries(std::vector<Entry> &entries, llvm::raw_ostream &out) {
  for (const Entry &E : entries) {
    out << "  " << E.DiagName;
    if (!E.Flag.empty())
      out << " [-W" << E.Flag << "]";
    out << '\n';
  }
}

int ListWarnings::run(unsigned int argc, char **argv, llvm::raw_ostream &out) {
  std::vector<Entry> Flagged, Unflagged;
  llvm::StringMap<std::vector<unsigned> > flagHistogram;

  for (const DiagnosticRecord &DR : getBuiltinDiagnosticsByName()) {
    const unsigned diagID = DR.DiagID;

    if (DiagnosticIDs::isBuiltinNote(diagID))
      continue;

    if (!DiagnosticIDs::isBuiltinWarningOrExtension(diagID))
      continue;

    Entry entry(DR.getName(), DiagnosticIDs::getWarningOptionForDiag(diagID));

    if (entry.Flag.empty())
      Unflagged.push_back(entry);
    else {
      Flagged.push_back(entry);
      flagHistogram[entry.Flag].push_back(diagID);
    }
  }

  out << "Warnings with flags (" << Flagged.size() << "):\n";
  printEntries(Flagged, out);

  out << "Warnings without flags (" << Unflagged.size() << "):\n";
  printEntries(Unflagged, out);

  out << "\nSTATISTICS:\n\n";

  double percentFlagged =
      ((double)Flagged.size()) / (Flagged.size() + Unflagged.size()) * 100.0;

  out << "  Percentage of warnings with flags: "
      << llvm::format("%.4g", percentFlagged) << "%\n";

  out << "  Number of unique flags: "
      << flagHistogram.size() << '\n';

  double avgDiagsPerFlag = (double) Flagged.size() / flagHistogram.size();
  out << "  Average number of diagnostics per flag: "
      << llvm::format("%.4g", avgDiagsPerFlag) << '\n';

  out << "  Number in -Wpedantic (not covered by other -W flags): "
      << flagHistogram["pedantic"].size() << '\n';

  out << '\n';

  return 0;
}

