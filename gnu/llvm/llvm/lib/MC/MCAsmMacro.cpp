//===- MCAsmMacro.h - Assembly Macros ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmMacro.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void MCAsmMacroParameter::dump(raw_ostream &OS) const {
  OS << "\"" << Name << "\"";
  if (Required)
    OS << ":req";
  if (Vararg)
    OS << ":vararg";
  if (!Value.empty()) {
    OS << " = ";
    bool first = true;
    for (const AsmToken &T : Value) {
      if (!first)
        OS << ", ";
      first = false;
      OS << T.getString();
    }
  }
  OS << "\n";
}

void MCAsmMacro::dump(raw_ostream &OS) const {
  OS << "Macro " << Name << ":\n";
  OS << "  Parameters:\n";
  for (const MCAsmMacroParameter &P : Parameters) {
    OS << "    ";
    P.dump();
  }
  if (!Locals.empty()) {
    OS << "  Locals:\n";
    for (StringRef L : Locals)
      OS << "    " << L << '\n';
  }
  OS << "  (BEGIN BODY)" << Body << "(END BODY)\n";
}
#endif
