//===- DWARFTypePrinter.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFTYPEPRINTER_H
#define LLVM_DEBUGINFO_DWARF_DWARFTYPEPRINTER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"

#include <string>

namespace llvm {

class raw_ostream;

// FIXME: We should have pretty printers per language. Currently we print
// everything as if it was C++ and fall back to the TAG type name.
struct DWARFTypePrinter {
  raw_ostream &OS;
  bool Word = true;
  bool EndedWithTemplate = false;

  DWARFTypePrinter(raw_ostream &OS) : OS(OS) {}

  /// Dump the name encoded in the type tag.
  void appendTypeTagName(dwarf::Tag T);

  void appendArrayType(const DWARFDie &D);

  DWARFDie skipQualifiers(DWARFDie D);

  bool needsParens(DWARFDie D);

  void appendPointerLikeTypeBefore(DWARFDie D, DWARFDie Inner, StringRef Ptr);

  DWARFDie appendUnqualifiedNameBefore(DWARFDie D,
                                       std::string *OriginalFullName = nullptr);

  void appendUnqualifiedNameAfter(DWARFDie D, DWARFDie Inner,
                                  bool SkipFirstParamIfArtificial = false);
  void appendQualifiedName(DWARFDie D);
  DWARFDie appendQualifiedNameBefore(DWARFDie D);
  bool appendTemplateParameters(DWARFDie D, bool *FirstParameter = nullptr);
  void decomposeConstVolatile(DWARFDie &N, DWARFDie &T, DWARFDie &C,
                              DWARFDie &V);
  void appendConstVolatileQualifierAfter(DWARFDie N);
  void appendConstVolatileQualifierBefore(DWARFDie N);

  /// Recursively append the DIE type name when applicable.
  void appendUnqualifiedName(DWARFDie D,
                             std::string *OriginalFullName = nullptr);

  void appendSubroutineNameAfter(DWARFDie D, DWARFDie Inner,
                                 bool SkipFirstParamIfArtificial, bool Const,
                                 bool Volatile);
  void appendScopes(DWARFDie D);
};

} // namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFTYPEPRINTER_H
