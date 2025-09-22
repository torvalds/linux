//===- SymbolRemappingReader.cpp - Read symbol remapping file -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions needed for reading and applying symbol
// remapping files.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/SymbolRemappingReader.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace llvm;

char SymbolRemappingParseError::ID;

/// Load a set of name remappings from a text file.
///
/// See the documentation at the top of the file for an explanation of
/// the expected format.
Error SymbolRemappingReader::read(MemoryBuffer &B) {
  line_iterator LineIt(B, /*SkipBlanks=*/true, '#');

  auto ReportError = [&](Twine Msg) {
    return llvm::make_error<SymbolRemappingParseError>(
        B.getBufferIdentifier(), LineIt.line_number(), Msg);
  };

  for (; !LineIt.is_at_eof(); ++LineIt) {
    StringRef Line = *LineIt;
    Line = Line.ltrim(' ');
    // line_iterator only detects comments starting in column 1.
    if (Line.starts_with("#") || Line.empty())
      continue;

    SmallVector<StringRef, 4> Parts;
    Line.split(Parts, ' ', /*MaxSplits*/-1, /*KeepEmpty*/false);

    if (Parts.size() != 3)
      return ReportError("Expected 'kind mangled_name mangled_name', "
                         "found '" + Line + "'");

    using FK = ItaniumManglingCanonicalizer::FragmentKind;
    std::optional<FK> FragmentKind = StringSwitch<std::optional<FK>>(Parts[0])
                                         .Case("name", FK::Name)
                                         .Case("type", FK::Type)
                                         .Case("encoding", FK::Encoding)
                                         .Default(std::nullopt);
    if (!FragmentKind)
      return ReportError("Invalid kind, expected 'name', 'type', or 'encoding',"
                         " found '" + Parts[0] + "'");

    using EE = ItaniumManglingCanonicalizer::EquivalenceError;
    switch (Canonicalizer.addEquivalence(*FragmentKind, Parts[1], Parts[2])) {
    case EE::Success:
      break;

    case EE::ManglingAlreadyUsed:
      return ReportError("Manglings '" + Parts[1] + "' and '" + Parts[2] + "' "
                         "have both been used in prior remappings. Move this "
                         "remapping earlier in the file.");

    case EE::InvalidFirstMangling:
      return ReportError("Could not demangle '" + Parts[1] + "' "
                         "as a <" + Parts[0] + ">; invalid mangling?");

    case EE::InvalidSecondMangling:
      return ReportError("Could not demangle '" + Parts[2] + "' "
                         "as a <" + Parts[0] + ">; invalid mangling?");
    }
  }

  return Error::success();
}
