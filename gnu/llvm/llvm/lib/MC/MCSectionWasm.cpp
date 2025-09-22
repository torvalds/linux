//===- lib/MC/MCSectionWasm.cpp - Wasm Code Section Representation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Decides whether a '.section' directive
// should be printed before the section name.
bool MCSectionWasm::shouldOmitSectionDirective(StringRef Name,
                                               const MCAsmInfo &MAI) const {
  return MAI.shouldOmitSectionDirective(Name);
}

static void printName(raw_ostream &OS, StringRef Name) {
  if (Name.find_first_not_of("0123456789_."
                             "abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ") == Name.npos) {
    OS << Name;
    return;
  }
  OS << '"';
  for (const char *B = Name.begin(), *E = Name.end(); B < E; ++B) {
    if (*B == '"') // Unquoted "
      OS << "\\\"";
    else if (*B != '\\') // Neither " or backslash
      OS << *B;
    else if (B + 1 == E) // Trailing backslash
      OS << "\\\\";
    else {
      OS << B[0] << B[1]; // Quoted character
      ++B;
    }
  }
  OS << '"';
}

void MCSectionWasm::printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                                         raw_ostream &OS,
                                         uint32_t Subsection) const {

  if (shouldOmitSectionDirective(getName(), MAI)) {
    OS << '\t' << getName();
    if (Subsection)
      OS << '\t' << Subsection;
    OS << '\n';
    return;
  }

  OS << "\t.section\t";
  printName(OS, getName());
  OS << ",\"";

  if (IsPassive)
    OS << 'p';
  if (Group)
    OS << 'G';
  if (SegmentFlags & wasm::WASM_SEG_FLAG_STRINGS)
    OS << 'S';
  if (SegmentFlags & wasm::WASM_SEG_FLAG_TLS)
    OS << 'T';
  if (SegmentFlags & wasm::WASM_SEG_FLAG_RETAIN)
    OS << 'R';

  OS << '"';

  OS << ',';

  // If comment string is '@', e.g. as on ARM - use '%' instead
  if (MAI.getCommentString()[0] == '@')
    OS << '%';
  else
    OS << '@';

  // TODO: Print section type.

  if (Group) {
    OS << ",";
    printName(OS, Group->getName());
    OS << ",comdat";
  }

  if (isUnique())
    OS << ",unique," << UniqueID;

  OS << '\n';

  if (Subsection)
    OS << "\t.subsection\t" << Subsection << '\n';
}

bool MCSectionWasm::useCodeAlign() const { return false; }
