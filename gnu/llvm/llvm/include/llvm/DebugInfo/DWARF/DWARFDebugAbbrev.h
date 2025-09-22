//===- DWARFDebugAbbrev.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFDEBUGABBREV_H
#define LLVM_DEBUGINFO_DWARF_DWARFDEBUGABBREV_H

#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/Support/DataExtractor.h"
#include <cstdint>
#include <map>
#include <vector>

namespace llvm {

class raw_ostream;

class DWARFAbbreviationDeclarationSet {
  uint64_t Offset;
  /// Code of the first abbreviation, if all abbreviations in the set have
  /// consecutive codes. UINT32_MAX otherwise.
  uint32_t FirstAbbrCode;
  std::vector<DWARFAbbreviationDeclaration> Decls;

  using const_iterator =
      std::vector<DWARFAbbreviationDeclaration>::const_iterator;

public:
  DWARFAbbreviationDeclarationSet();

  uint64_t getOffset() const { return Offset; }
  void dump(raw_ostream &OS) const;
  Error extract(DataExtractor Data, uint64_t *OffsetPtr);

  const DWARFAbbreviationDeclaration *
  getAbbreviationDeclaration(uint32_t AbbrCode) const;

  const_iterator begin() const {
    return Decls.begin();
  }

  const_iterator end() const {
    return Decls.end();
  }

  std::string getCodeRange() const;

  uint32_t getFirstAbbrCode() const { return FirstAbbrCode; }

private:
  void clear();
};

class DWARFDebugAbbrev {
  using DWARFAbbreviationDeclarationSetMap =
      std::map<uint64_t, DWARFAbbreviationDeclarationSet>;

  mutable DWARFAbbreviationDeclarationSetMap AbbrDeclSets;
  mutable DWARFAbbreviationDeclarationSetMap::const_iterator PrevAbbrOffsetPos;
  mutable std::optional<DataExtractor> Data;

public:
  DWARFDebugAbbrev(DataExtractor Data);

  Expected<const DWARFAbbreviationDeclarationSet *>
  getAbbreviationDeclarationSet(uint64_t CUAbbrOffset) const;

  void dump(raw_ostream &OS) const;
  Error parse() const;

  DWARFAbbreviationDeclarationSetMap::const_iterator begin() const {
    assert(!Data && "Must call parse before iterating over DWARFDebugAbbrev");
    return AbbrDeclSets.begin();
  }

  DWARFAbbreviationDeclarationSetMap::const_iterator end() const {
    return AbbrDeclSets.end();
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFDEBUGABBREV_H
