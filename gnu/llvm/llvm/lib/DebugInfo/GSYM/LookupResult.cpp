//===- LookupResult.cpp -------------------------------------------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/GSYM/LookupResult.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <ciso646>

using namespace llvm;
using namespace gsym;

std::string LookupResult::getSourceFile(uint32_t Index) const {
  std::string Fullpath;
  if (Index < Locations.size()) {
    if (!Locations[Index].Dir.empty()) {
      if (Locations[Index].Base.empty()) {
        Fullpath = std::string(Locations[Index].Dir);
      } else {
        llvm::SmallString<64> Storage;
        llvm::sys::path::append(Storage, Locations[Index].Dir,
                                Locations[Index].Base);
        Fullpath.assign(Storage.begin(), Storage.end());
      }
    } else if (!Locations[Index].Base.empty())
      Fullpath = std::string(Locations[Index].Base);
  }
  return Fullpath;
}

raw_ostream &llvm::gsym::operator<<(raw_ostream &OS, const SourceLocation &SL) {
  OS << SL.Name;
  if (SL.Offset > 0)
    OS << " + " << SL.Offset;
  if (SL.Dir.size() || SL.Base.size()) {
    OS << " @ ";
    if (!SL.Dir.empty()) {
      OS << SL.Dir;
      if (SL.Dir.contains('\\') && !SL.Dir.contains('/'))
        OS << '\\';
      else
        OS << '/';
    }
    if (SL.Base.empty())
      OS << "<invalid-file>";
    else
      OS << SL.Base;
    OS << ':' << SL.Line;
  }
  return OS;
}

raw_ostream &llvm::gsym::operator<<(raw_ostream &OS, const LookupResult &LR) {
  OS << HEX64(LR.LookupAddr) << ": ";
  auto NumLocations = LR.Locations.size();
  for (size_t I = 0; I < NumLocations; ++I) {
    if (I > 0) {
      OS << '\n';
      OS.indent(20);
    }
    const bool IsInlined = I + 1 != NumLocations;
    OS << LR.Locations[I];
    if (IsInlined)
      OS << " [inlined]";
  }
  OS << '\n';
  return OS;
}
