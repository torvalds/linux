//===- DWARFDebugAbbrev.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cinttypes>
#include <cstdint>

using namespace llvm;

DWARFAbbreviationDeclarationSet::DWARFAbbreviationDeclarationSet() {
  clear();
}

void DWARFAbbreviationDeclarationSet::clear() {
  Offset = 0;
  FirstAbbrCode = 0;
  Decls.clear();
}

Error DWARFAbbreviationDeclarationSet::extract(DataExtractor Data,
                                               uint64_t *OffsetPtr) {
  clear();
  const uint64_t BeginOffset = *OffsetPtr;
  Offset = BeginOffset;
  DWARFAbbreviationDeclaration AbbrDecl;
  uint32_t PrevAbbrCode = 0;
  while (true) {
    Expected<DWARFAbbreviationDeclaration::ExtractState> ES =
        AbbrDecl.extract(Data, OffsetPtr);
    if (!ES)
      return ES.takeError();

    if (*ES == DWARFAbbreviationDeclaration::ExtractState::Complete)
      break;

    if (FirstAbbrCode == 0) {
      FirstAbbrCode = AbbrDecl.getCode();
    } else if (PrevAbbrCode + 1 != AbbrDecl.getCode()) {
      // Codes are not consecutive, can't do O(1) lookups.
      FirstAbbrCode = UINT32_MAX;
    }
    PrevAbbrCode = AbbrDecl.getCode();
    Decls.push_back(std::move(AbbrDecl));
  }
  return Error::success();
}

void DWARFAbbreviationDeclarationSet::dump(raw_ostream &OS) const {
  for (const auto &Decl : Decls)
    Decl.dump(OS);
}

const DWARFAbbreviationDeclaration *
DWARFAbbreviationDeclarationSet::getAbbreviationDeclaration(
    uint32_t AbbrCode) const {
  if (FirstAbbrCode == UINT32_MAX) {
    for (const auto &Decl : Decls) {
      if (Decl.getCode() == AbbrCode)
        return &Decl;
    }
    return nullptr;
  }
  if (AbbrCode < FirstAbbrCode || AbbrCode >= FirstAbbrCode + Decls.size())
    return nullptr;
  return &Decls[AbbrCode - FirstAbbrCode];
}

std::string DWARFAbbreviationDeclarationSet::getCodeRange() const {
  // Create a sorted list of all abbrev codes.
  std::vector<uint32_t> Codes;
  Codes.reserve(Decls.size());
  for (const auto &Decl : Decls)
    Codes.push_back(Decl.getCode());

  std::string Buffer;
  raw_string_ostream Stream(Buffer);
  // Each iteration through this loop represents a single contiguous range in
  // the set of codes.
  for (auto Current = Codes.begin(), End = Codes.end(); Current != End;) {
    uint32_t RangeStart = *Current;
    // Add the current range start.
    Stream << *Current;
    uint32_t RangeEnd = RangeStart;
    // Find the end of the current range.
    while (++Current != End && *Current == RangeEnd + 1)
      ++RangeEnd;
    // If there is more than one value in the range, add the range end too.
    if (RangeStart != RangeEnd)
      Stream << "-" << RangeEnd;
    // If there is at least one more range, add a separator.
    if (Current != End)
      Stream << ", ";
  }
  return Buffer;
}

DWARFDebugAbbrev::DWARFDebugAbbrev(DataExtractor Data)
    : AbbrDeclSets(), PrevAbbrOffsetPos(AbbrDeclSets.end()), Data(Data) {}

Error DWARFDebugAbbrev::parse() const {
  if (!Data)
    return Error::success();
  uint64_t Offset = 0;
  auto I = AbbrDeclSets.begin();
  while (Data->isValidOffset(Offset)) {
    while (I != AbbrDeclSets.end() && I->first < Offset)
      ++I;
    uint64_t CUAbbrOffset = Offset;
    DWARFAbbreviationDeclarationSet AbbrDecls;
    if (Error Err = AbbrDecls.extract(*Data, &Offset)) {
      Data = std::nullopt;
      return Err;
    }
    AbbrDeclSets.insert(I, std::make_pair(CUAbbrOffset, std::move(AbbrDecls)));
  }
  Data = std::nullopt;
  return Error::success();
}

void DWARFDebugAbbrev::dump(raw_ostream &OS) const {
  if (Error Err = parse())
    // FIXME: We should propagate this error or otherwise display it.
    llvm::consumeError(std::move(Err));

  if (AbbrDeclSets.empty()) {
    OS << "< EMPTY >\n";
    return;
  }

  for (const auto &I : AbbrDeclSets) {
    OS << format("Abbrev table for offset: 0x%8.8" PRIx64 "\n", I.first);
    I.second.dump(OS);
  }
}

Expected<const DWARFAbbreviationDeclarationSet *>
DWARFDebugAbbrev::getAbbreviationDeclarationSet(uint64_t CUAbbrOffset) const {
  const auto End = AbbrDeclSets.end();
  if (PrevAbbrOffsetPos != End && PrevAbbrOffsetPos->first == CUAbbrOffset) {
    return &PrevAbbrOffsetPos->second;
  }

  const auto Pos = AbbrDeclSets.find(CUAbbrOffset);
  if (Pos != End) {
    PrevAbbrOffsetPos = Pos;
    return &Pos->second;
  }

  if (!Data || CUAbbrOffset >= Data->getData().size())
    return make_error<llvm::object::GenericBinaryError>(
        "the abbreviation offset into the .debug_abbrev section is not valid");

  uint64_t Offset = CUAbbrOffset;
  DWARFAbbreviationDeclarationSet AbbrDecls;
  if (Error Err = AbbrDecls.extract(*Data, &Offset))
    return std::move(Err);

  PrevAbbrOffsetPos =
      AbbrDeclSets.insert(std::make_pair(CUAbbrOffset, std::move(AbbrDecls)))
          .first;
  return &PrevAbbrOffsetPos->second;
}
