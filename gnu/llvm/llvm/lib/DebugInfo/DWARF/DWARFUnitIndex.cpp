//===- DWARFUnitIndex.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <cinttypes>
#include <cstdint>

using namespace llvm;

namespace {

enum class DWARFSectionKindV2 {
  DW_SECT_INFO = 1,
  DW_SECT_TYPES = 2,
  DW_SECT_ABBREV = 3,
  DW_SECT_LINE = 4,
  DW_SECT_LOC = 5,
  DW_SECT_STR_OFFSETS = 6,
  DW_SECT_MACINFO = 7,
  DW_SECT_MACRO = 8,
};

} // namespace

// Return true if the section identifier is defined in the DWARFv5 standard.
constexpr bool isKnownV5SectionID(uint32_t ID) {
  return ID >= DW_SECT_INFO && ID <= DW_SECT_RNGLISTS &&
         ID != DW_SECT_EXT_TYPES;
}

uint32_t llvm::serializeSectionKind(DWARFSectionKind Kind,
                                    unsigned IndexVersion) {
  if (IndexVersion == 5) {
    assert(isKnownV5SectionID(Kind));
    return static_cast<uint32_t>(Kind);
  }
  assert(IndexVersion == 2);
  switch (Kind) {
#define CASE(S,T) \
  case DW_SECT_##S: \
    return static_cast<uint32_t>(DWARFSectionKindV2::DW_SECT_##T)
  CASE(INFO, INFO);
  CASE(EXT_TYPES, TYPES);
  CASE(ABBREV, ABBREV);
  CASE(LINE, LINE);
  CASE(EXT_LOC, LOC);
  CASE(STR_OFFSETS, STR_OFFSETS);
  CASE(EXT_MACINFO, MACINFO);
  CASE(MACRO, MACRO);
#undef CASE
  default:
    // All other section kinds have no corresponding values in v2 indexes.
    llvm_unreachable("Invalid DWARFSectionKind");
  }
}

DWARFSectionKind llvm::deserializeSectionKind(uint32_t Value,
                                              unsigned IndexVersion) {
  if (IndexVersion == 5)
    return isKnownV5SectionID(Value)
               ? static_cast<DWARFSectionKind>(Value)
               : DW_SECT_EXT_unknown;
  assert(IndexVersion == 2);
  switch (static_cast<DWARFSectionKindV2>(Value)) {
#define CASE(S,T) \
  case DWARFSectionKindV2::DW_SECT_##S: \
    return DW_SECT_##T
  CASE(INFO, INFO);
  CASE(TYPES, EXT_TYPES);
  CASE(ABBREV, ABBREV);
  CASE(LINE, LINE);
  CASE(LOC, EXT_LOC);
  CASE(STR_OFFSETS, STR_OFFSETS);
  CASE(MACINFO, EXT_MACINFO);
  CASE(MACRO, MACRO);
#undef CASE
  }
  return DW_SECT_EXT_unknown;
}

bool DWARFUnitIndex::Header::parse(DataExtractor IndexData,
                                   uint64_t *OffsetPtr) {
  const uint64_t BeginOffset = *OffsetPtr;
  if (!IndexData.isValidOffsetForDataOfSize(*OffsetPtr, 16))
    return false;
  // GCC Debug Fission defines the version as an unsigned 32-bit field
  // with value of 2, https://gcc.gnu.org/wiki/DebugFissionDWP.
  // DWARFv5 defines the same space as an uhalf version field with value of 5
  // and a 2 bytes long padding, see Section 7.3.5.3.
  Version = IndexData.getU32(OffsetPtr);
  if (Version != 2) {
    *OffsetPtr = BeginOffset;
    Version = IndexData.getU16(OffsetPtr);
    if (Version != 5)
      return false;
    *OffsetPtr += 2; // Skip padding.
  }
  NumColumns = IndexData.getU32(OffsetPtr);
  NumUnits = IndexData.getU32(OffsetPtr);
  NumBuckets = IndexData.getU32(OffsetPtr);
  return true;
}

void DWARFUnitIndex::Header::dump(raw_ostream &OS) const {
  OS << format("version = %u, units = %u, slots = %u\n\n", Version, NumUnits, NumBuckets);
}

bool DWARFUnitIndex::parse(DataExtractor IndexData) {
  bool b = parseImpl(IndexData);
  if (!b) {
    // Make sure we don't try to dump anything
    Header.NumBuckets = 0;
    // Release any partially initialized data.
    ColumnKinds.reset();
    Rows.reset();
  }
  return b;
}

bool DWARFUnitIndex::parseImpl(DataExtractor IndexData) {
  uint64_t Offset = 0;
  if (!Header.parse(IndexData, &Offset))
    return false;

  // Fix InfoColumnKind: in DWARFv5, type units are in .debug_info.dwo.
  if (Header.Version == 5)
    InfoColumnKind = DW_SECT_INFO;

  if (!IndexData.isValidOffsetForDataOfSize(
          Offset, Header.NumBuckets * (8 + 4) +
                      (2 * Header.NumUnits + 1) * 4 * Header.NumColumns))
    return false;

  Rows = std::make_unique<Entry[]>(Header.NumBuckets);
  auto Contribs =
      std::make_unique<Entry::SectionContribution *[]>(Header.NumUnits);
  ColumnKinds = std::make_unique<DWARFSectionKind[]>(Header.NumColumns);
  RawSectionIds = std::make_unique<uint32_t[]>(Header.NumColumns);

  // Read Hash Table of Signatures
  for (unsigned i = 0; i != Header.NumBuckets; ++i)
    Rows[i].Signature = IndexData.getU64(&Offset);

  // Read Parallel Table of Indexes
  for (unsigned i = 0; i != Header.NumBuckets; ++i) {
    auto Index = IndexData.getU32(&Offset);
    if (!Index)
      continue;
    Rows[i].Index = this;
    Rows[i].Contributions =
        std::make_unique<Entry::SectionContribution[]>(Header.NumColumns);
    Contribs[Index - 1] = Rows[i].Contributions.get();
  }

  // Read the Column Headers
  for (unsigned i = 0; i != Header.NumColumns; ++i) {
    RawSectionIds[i] = IndexData.getU32(&Offset);
    ColumnKinds[i] = deserializeSectionKind(RawSectionIds[i], Header.Version);
    if (ColumnKinds[i] == InfoColumnKind) {
      if (InfoColumn != -1)
        return false;
      InfoColumn = i;
    }
  }

  if (InfoColumn == -1)
    return false;

  // Read Table of Section Offsets
  for (unsigned i = 0; i != Header.NumUnits; ++i) {
    auto *Contrib = Contribs[i];
    for (unsigned i = 0; i != Header.NumColumns; ++i)
      Contrib[i].setOffset(IndexData.getU32(&Offset));
  }

  // Read Table of Section Sizes
  for (unsigned i = 0; i != Header.NumUnits; ++i) {
    auto *Contrib = Contribs[i];
    for (unsigned i = 0; i != Header.NumColumns; ++i)
      Contrib[i].setLength(IndexData.getU32(&Offset));
  }

  return true;
}

StringRef DWARFUnitIndex::getColumnHeader(DWARFSectionKind DS) {
  switch (DS) {
#define HANDLE_DW_SECT(ID, NAME)                                               \
  case DW_SECT_##NAME:                                                         \
    return #NAME;
#include "llvm/BinaryFormat/Dwarf.def"
  case DW_SECT_EXT_TYPES:
    return "TYPES";
  case DW_SECT_EXT_LOC:
    return "LOC";
  case DW_SECT_EXT_MACINFO:
    return "MACINFO";
  case DW_SECT_EXT_unknown:
    return StringRef();
  }
  llvm_unreachable("Unknown DWARFSectionKind");
}

void DWARFUnitIndex::dump(raw_ostream &OS) const {
  if (!*this)
    return;

  Header.dump(OS);
  OS << "Index Signature         ";
  for (unsigned i = 0; i != Header.NumColumns; ++i) {
    DWARFSectionKind Kind = ColumnKinds[i];
    StringRef Name = getColumnHeader(Kind);
    if (!Name.empty())
      OS << ' '
         << left_justify(Name,
                         Kind == DWARFSectionKind::DW_SECT_INFO ? 40 : 24);
    else
      OS << format(" Unknown: %-15" PRIu32, RawSectionIds[i]);
  }
  OS << "\n----- ------------------";
  for (unsigned i = 0; i != Header.NumColumns; ++i) {
    DWARFSectionKind Kind = ColumnKinds[i];
    if (Kind == DWARFSectionKind::DW_SECT_INFO ||
        Kind == DWARFSectionKind::DW_SECT_EXT_TYPES)
      OS << " ----------------------------------------";
    else
      OS << " ------------------------";
  }
  OS << '\n';
  for (unsigned i = 0; i != Header.NumBuckets; ++i) {
    auto &Row = Rows[i];
    if (auto *Contribs = Row.Contributions.get()) {
      OS << format("%5u 0x%016" PRIx64 " ", i + 1, Row.Signature);
      for (unsigned i = 0; i != Header.NumColumns; ++i) {
        auto &Contrib = Contribs[i];
        DWARFSectionKind Kind = ColumnKinds[i];
        if (Kind == DWARFSectionKind::DW_SECT_INFO ||
            Kind == DWARFSectionKind::DW_SECT_EXT_TYPES)
          OS << format("[0x%016" PRIx64 ", 0x%016" PRIx64 ") ",
                       Contrib.getOffset(),
                       Contrib.getOffset() + Contrib.getLength());
        else
          OS << format("[0x%08" PRIx32 ", 0x%08" PRIx32 ") ",
                       Contrib.getOffset32(),
                       Contrib.getOffset32() + Contrib.getLength32());
      }
      OS << '\n';
    }
  }
}

const DWARFUnitIndex::Entry::SectionContribution *
DWARFUnitIndex::Entry::getContribution(DWARFSectionKind Sec) const {
  uint32_t i = 0;
  for (; i != Index->Header.NumColumns; ++i)
    if (Index->ColumnKinds[i] == Sec)
      return &Contributions[i];
  return nullptr;
}

DWARFUnitIndex::Entry::SectionContribution &
DWARFUnitIndex::Entry::getContribution() {
  return Contributions[Index->InfoColumn];
}

const DWARFUnitIndex::Entry::SectionContribution *
DWARFUnitIndex::Entry::getContribution() const {
  return &Contributions[Index->InfoColumn];
}

const DWARFUnitIndex::Entry *
DWARFUnitIndex::getFromOffset(uint64_t Offset) const {
  if (OffsetLookup.empty()) {
    for (uint32_t i = 0; i != Header.NumBuckets; ++i)
      if (Rows[i].Contributions)
        OffsetLookup.push_back(&Rows[i]);
    llvm::sort(OffsetLookup, [&](Entry *E1, Entry *E2) {
      return E1->Contributions[InfoColumn].getOffset() <
             E2->Contributions[InfoColumn].getOffset();
    });
  }
  auto I = partition_point(OffsetLookup, [&](Entry *E2) {
    return E2->Contributions[InfoColumn].getOffset() <= Offset;
  });
  if (I == OffsetLookup.begin())
    return nullptr;
  --I;
  const auto *E = *I;
  const auto &InfoContrib = E->Contributions[InfoColumn];
  if ((InfoContrib.getOffset() + InfoContrib.getLength()) <= Offset)
    return nullptr;
  return E;
}

const DWARFUnitIndex::Entry *DWARFUnitIndex::getFromHash(uint64_t S) const {
  uint64_t Mask = Header.NumBuckets - 1;

  auto H = S & Mask;
  auto HP = ((S >> 32) & Mask) | 1;
  // The spec says "while 0 is a valid hash value, the row index in a used slot
  // will always be non-zero". Loop until we find a match or an empty slot.
  while (Rows[H].getSignature() != S && Rows[H].Index != nullptr)
    H = (H + HP) & Mask;

  // If the slot is empty, we don't care whether the signature matches (it could
  // be zero and still match the zeros in the empty slot).
  if (Rows[H].Index == nullptr)
    return nullptr;

  return &Rows[H];
}
