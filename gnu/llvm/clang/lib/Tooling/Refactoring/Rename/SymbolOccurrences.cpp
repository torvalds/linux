//===--- SymbolOccurrences.cpp - Clang refactoring library ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Rename/SymbolOccurrences.h"
#include "clang/Tooling/Refactoring/Rename/SymbolName.h"
#include "llvm/ADT/STLExtras.h"

using namespace clang;
using namespace tooling;

SymbolOccurrence::SymbolOccurrence(const SymbolName &Name, OccurrenceKind Kind,
                                   ArrayRef<SourceLocation> Locations)
    : Kind(Kind) {
  ArrayRef<std::string> NamePieces = Name.getNamePieces();
  assert(Locations.size() == NamePieces.size() &&
         "mismatching number of locations and lengths");
  assert(!Locations.empty() && "no locations");
  if (Locations.size() == 1) {
    new (&SingleRange) SourceRange(
        Locations[0], Locations[0].getLocWithOffset(NamePieces[0].size()));
    return;
  }
  MultipleRanges = std::make_unique<SourceRange[]>(Locations.size());
  NumRanges = Locations.size();
  for (const auto &Loc : llvm::enumerate(Locations)) {
    MultipleRanges[Loc.index()] = SourceRange(
        Loc.value(),
        Loc.value().getLocWithOffset(NamePieces[Loc.index()].size()));
  }
}
