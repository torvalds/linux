//===- DWARFAcceleratorTable.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/raw_ostream.h"
#include <cstddef>
#include <cstdint>
#include <utility>

using namespace llvm;

namespace {
struct Atom {
  unsigned Value;
};

static raw_ostream &operator<<(raw_ostream &OS, const Atom &A) {
  StringRef Str = dwarf::AtomTypeString(A.Value);
  if (!Str.empty())
    return OS << Str;
  return OS << "DW_ATOM_unknown_" << format("%x", A.Value);
}
} // namespace

static Atom formatAtom(unsigned Atom) { return {Atom}; }

DWARFAcceleratorTable::~DWARFAcceleratorTable() = default;

Error AppleAcceleratorTable::extract() {
  uint64_t Offset = 0;

  // Check that we can at least read the header.
  if (!AccelSection.isValidOffset(offsetof(Header, HeaderDataLength) + 4))
    return createStringError(errc::illegal_byte_sequence,
                             "Section too small: cannot read header.");

  Hdr.Magic = AccelSection.getU32(&Offset);
  Hdr.Version = AccelSection.getU16(&Offset);
  Hdr.HashFunction = AccelSection.getU16(&Offset);
  Hdr.BucketCount = AccelSection.getU32(&Offset);
  Hdr.HashCount = AccelSection.getU32(&Offset);
  Hdr.HeaderDataLength = AccelSection.getU32(&Offset);
  FormParams = {Hdr.Version, 0, dwarf::DwarfFormat::DWARF32};

  // Check that we can read all the hashes and offsets from the
  // section (see SourceLevelDebugging.rst for the structure of the index).
  if (!AccelSection.isValidOffset(getIthBucketBase(Hdr.BucketCount - 1)))
    return createStringError(
        errc::illegal_byte_sequence,
        "Section too small: cannot read buckets and hashes.");

  HdrData.DIEOffsetBase = AccelSection.getU32(&Offset);
  uint32_t NumAtoms = AccelSection.getU32(&Offset);

  HashDataEntryLength = 0;
  auto MakeUnsupportedFormError = [](dwarf::Form Form) {
    return createStringError(errc::not_supported,
                             "Unsupported form:" +
                                 dwarf::FormEncodingString(Form));
  };

  for (unsigned i = 0; i < NumAtoms; ++i) {
    uint16_t AtomType = AccelSection.getU16(&Offset);
    auto AtomForm = static_cast<dwarf::Form>(AccelSection.getU16(&Offset));
    HdrData.Atoms.push_back(std::make_pair(AtomType, AtomForm));

    std::optional<uint8_t> FormSize =
        dwarf::getFixedFormByteSize(AtomForm, FormParams);
    if (!FormSize)
      return MakeUnsupportedFormError(AtomForm);
    HashDataEntryLength += *FormSize;
  }

  IsValid = true;
  return Error::success();
}

uint32_t AppleAcceleratorTable::getNumBuckets() const {
  return Hdr.BucketCount;
}
uint32_t AppleAcceleratorTable::getNumHashes() const { return Hdr.HashCount; }
uint32_t AppleAcceleratorTable::getSizeHdr() const { return sizeof(Hdr); }
uint32_t AppleAcceleratorTable::getHeaderDataLength() const {
  return Hdr.HeaderDataLength;
}

ArrayRef<std::pair<AppleAcceleratorTable::HeaderData::AtomType,
                   AppleAcceleratorTable::HeaderData::Form>>
AppleAcceleratorTable::getAtomsDesc() {
  return HdrData.Atoms;
}

bool AppleAcceleratorTable::validateForms() {
  for (auto Atom : getAtomsDesc()) {
    DWARFFormValue FormValue(Atom.second);
    switch (Atom.first) {
    case dwarf::DW_ATOM_die_offset:
    case dwarf::DW_ATOM_die_tag:
    case dwarf::DW_ATOM_type_flags:
      if ((!FormValue.isFormClass(DWARFFormValue::FC_Constant) &&
           !FormValue.isFormClass(DWARFFormValue::FC_Flag)) ||
          FormValue.getForm() == dwarf::DW_FORM_sdata)
        return false;
      break;
    default:
      break;
    }
  }
  return true;
}

std::pair<uint64_t, dwarf::Tag>
AppleAcceleratorTable::readAtoms(uint64_t *HashDataOffset) {
  uint64_t DieOffset = dwarf::DW_INVALID_OFFSET;
  dwarf::Tag DieTag = dwarf::DW_TAG_null;

  for (auto Atom : getAtomsDesc()) {
    DWARFFormValue FormValue(Atom.second);
    FormValue.extractValue(AccelSection, HashDataOffset, FormParams);
    switch (Atom.first) {
    case dwarf::DW_ATOM_die_offset:
      DieOffset = *FormValue.getAsUnsignedConstant();
      break;
    case dwarf::DW_ATOM_die_tag:
      DieTag = (dwarf::Tag)*FormValue.getAsUnsignedConstant();
      break;
    default:
      break;
    }
  }
  return {DieOffset, DieTag};
}

void AppleAcceleratorTable::Header::dump(ScopedPrinter &W) const {
  DictScope HeaderScope(W, "Header");
  W.printHex("Magic", Magic);
  W.printHex("Version", Version);
  W.printHex("Hash function", HashFunction);
  W.printNumber("Bucket count", BucketCount);
  W.printNumber("Hashes count", HashCount);
  W.printNumber("HeaderData length", HeaderDataLength);
}

std::optional<uint64_t> AppleAcceleratorTable::HeaderData::extractOffset(
    std::optional<DWARFFormValue> Value) const {
  if (!Value)
    return std::nullopt;

  switch (Value->getForm()) {
  case dwarf::DW_FORM_ref1:
  case dwarf::DW_FORM_ref2:
  case dwarf::DW_FORM_ref4:
  case dwarf::DW_FORM_ref8:
  case dwarf::DW_FORM_ref_udata:
    return Value->getRawUValue() + DIEOffsetBase;
  default:
    return Value->getAsSectionOffset();
  }
}

bool AppleAcceleratorTable::dumpName(ScopedPrinter &W,
                                     SmallVectorImpl<DWARFFormValue> &AtomForms,
                                     uint64_t *DataOffset) const {
  uint64_t NameOffset = *DataOffset;
  if (!AccelSection.isValidOffsetForDataOfSize(*DataOffset, 4)) {
    W.printString("Incorrectly terminated list.");
    return false;
  }
  uint64_t StringOffset = AccelSection.getRelocatedValue(4, DataOffset);
  if (!StringOffset)
    return false; // End of list

  DictScope NameScope(W, ("Name@0x" + Twine::utohexstr(NameOffset)).str());
  W.startLine() << format("String: 0x%08" PRIx64, StringOffset);
  W.getOStream() << " \"" << StringSection.getCStr(&StringOffset) << "\"\n";

  unsigned NumData = AccelSection.getU32(DataOffset);
  for (unsigned Data = 0; Data < NumData; ++Data) {
    ListScope DataScope(W, ("Data " + Twine(Data)).str());
    unsigned i = 0;
    for (auto &Atom : AtomForms) {
      W.startLine() << format("Atom[%d]: ", i);
      if (Atom.extractValue(AccelSection, DataOffset, FormParams)) {
        Atom.dump(W.getOStream());
        if (std::optional<uint64_t> Val = Atom.getAsUnsignedConstant()) {
          StringRef Str = dwarf::AtomValueString(HdrData.Atoms[i].first, *Val);
          if (!Str.empty())
            W.getOStream() << " (" << Str << ")";
        }
      } else
        W.getOStream() << "Error extracting the value";
      W.getOStream() << "\n";
      i++;
    }
  }
  return true; // more entries follow
}

LLVM_DUMP_METHOD void AppleAcceleratorTable::dump(raw_ostream &OS) const {
  if (!IsValid)
    return;

  ScopedPrinter W(OS);

  Hdr.dump(W);

  W.printNumber("DIE offset base", HdrData.DIEOffsetBase);
  W.printNumber("Number of atoms", uint64_t(HdrData.Atoms.size()));
  W.printNumber("Size of each hash data entry", getHashDataEntryLength());
  SmallVector<DWARFFormValue, 3> AtomForms;
  {
    ListScope AtomsScope(W, "Atoms");
    unsigned i = 0;
    for (const auto &Atom : HdrData.Atoms) {
      DictScope AtomScope(W, ("Atom " + Twine(i++)).str());
      W.startLine() << "Type: " << formatAtom(Atom.first) << '\n';
      W.startLine() << "Form: " << formatv("{0}", Atom.second) << '\n';
      AtomForms.push_back(DWARFFormValue(Atom.second));
    }
  }

  // Now go through the actual tables and dump them.
  uint64_t Offset = sizeof(Hdr) + Hdr.HeaderDataLength;
  uint64_t HashesBase = Offset + Hdr.BucketCount * 4;
  uint64_t OffsetsBase = HashesBase + Hdr.HashCount * 4;

  for (unsigned Bucket = 0; Bucket < Hdr.BucketCount; ++Bucket) {
    unsigned Index = AccelSection.getU32(&Offset);

    ListScope BucketScope(W, ("Bucket " + Twine(Bucket)).str());
    if (Index == UINT32_MAX) {
      W.printString("EMPTY");
      continue;
    }

    for (unsigned HashIdx = Index; HashIdx < Hdr.HashCount; ++HashIdx) {
      uint64_t HashOffset = HashesBase + HashIdx*4;
      uint64_t OffsetsOffset = OffsetsBase + HashIdx*4;
      uint32_t Hash = AccelSection.getU32(&HashOffset);

      if (Hash % Hdr.BucketCount != Bucket)
        break;

      uint64_t DataOffset = AccelSection.getU32(&OffsetsOffset);
      ListScope HashScope(W, ("Hash 0x" + Twine::utohexstr(Hash)).str());
      if (!AccelSection.isValidOffset(DataOffset)) {
        W.printString("Invalid section offset");
        continue;
      }
      while (dumpName(W, AtomForms, &DataOffset))
        /*empty*/;
    }
  }
}

AppleAcceleratorTable::Entry::Entry(const AppleAcceleratorTable &Table)
    : Table(Table) {
  Values.reserve(Table.HdrData.Atoms.size());
  for (const auto &Atom : Table.HdrData.Atoms)
    Values.push_back(DWARFFormValue(Atom.second));
}

void AppleAcceleratorTable::Entry::extract(uint64_t *Offset) {
  for (auto &FormValue : Values)
    FormValue.extractValue(Table.AccelSection, Offset, Table.FormParams);
}

std::optional<DWARFFormValue>
AppleAcceleratorTable::Entry::lookup(HeaderData::AtomType AtomToFind) const {
  for (auto [Atom, FormValue] : zip_equal(Table.HdrData.Atoms, Values))
    if (Atom.first == AtomToFind)
      return FormValue;
  return std::nullopt;
}

std::optional<uint64_t>
AppleAcceleratorTable::Entry::getDIESectionOffset() const {
  return Table.HdrData.extractOffset(lookup(dwarf::DW_ATOM_die_offset));
}

std::optional<uint64_t> AppleAcceleratorTable::Entry::getCUOffset() const {
  return Table.HdrData.extractOffset(lookup(dwarf::DW_ATOM_cu_offset));
}

std::optional<dwarf::Tag> AppleAcceleratorTable::Entry::getTag() const {
  std::optional<DWARFFormValue> Tag = lookup(dwarf::DW_ATOM_die_tag);
  if (!Tag)
    return std::nullopt;
  if (std::optional<uint64_t> Value = Tag->getAsUnsignedConstant())
    return dwarf::Tag(*Value);
  return std::nullopt;
}

AppleAcceleratorTable::SameNameIterator::SameNameIterator(
    const AppleAcceleratorTable &AccelTable, uint64_t DataOffset)
    : Current(AccelTable), Offset(DataOffset) {}

void AppleAcceleratorTable::Iterator::prepareNextEntryOrEnd() {
  if (NumEntriesToCome == 0)
    prepareNextStringOrEnd();
  if (isEnd())
    return;
  uint64_t OffsetCopy = Offset;
  Current.BaseEntry.extract(&OffsetCopy);
  NumEntriesToCome--;
  Offset += getTable().getHashDataEntryLength();
}

void AppleAcceleratorTable::Iterator::prepareNextStringOrEnd() {
  std::optional<uint32_t> StrOffset = getTable().readStringOffsetAt(Offset);
  if (!StrOffset)
    return setToEnd();

  // A zero denotes the end of the collision list. Read the next string
  // again.
  if (*StrOffset == 0)
    return prepareNextStringOrEnd();
  Current.StrOffset = *StrOffset;

  std::optional<uint32_t> MaybeNumEntries = getTable().readU32FromAccel(Offset);
  if (!MaybeNumEntries || *MaybeNumEntries == 0)
    return setToEnd();
  NumEntriesToCome = *MaybeNumEntries;
}

AppleAcceleratorTable::Iterator::Iterator(const AppleAcceleratorTable &Table,
                                          bool SetEnd)
    : Current(Table), Offset(Table.getEntriesBase()), NumEntriesToCome(0) {
  if (SetEnd)
    setToEnd();
  else
    prepareNextEntryOrEnd();
}

iterator_range<AppleAcceleratorTable::SameNameIterator>
AppleAcceleratorTable::equal_range(StringRef Key) const {
  const auto EmptyRange =
      make_range(SameNameIterator(*this, 0), SameNameIterator(*this, 0));
  if (!IsValid)
    return EmptyRange;

  // Find the bucket.
  uint32_t SearchHash = djbHash(Key);
  uint32_t BucketIdx = hashToBucketIdx(SearchHash);
  std::optional<uint32_t> HashIdx = idxOfHashInBucket(SearchHash, BucketIdx);
  if (!HashIdx)
    return EmptyRange;

  std::optional<uint64_t> MaybeDataOffset = readIthOffset(*HashIdx);
  if (!MaybeDataOffset)
    return EmptyRange;

  uint64_t DataOffset = *MaybeDataOffset;
  if (DataOffset >= AccelSection.size())
    return EmptyRange;

  std::optional<uint32_t> StrOffset = readStringOffsetAt(DataOffset);
  // Valid input and still have strings in this hash.
  while (StrOffset && *StrOffset) {
    std::optional<StringRef> MaybeStr = readStringFromStrSection(*StrOffset);
    std::optional<uint32_t> NumEntries = this->readU32FromAccel(DataOffset);
    if (!MaybeStr || !NumEntries)
      return EmptyRange;
    uint64_t EndOffset = DataOffset + *NumEntries * getHashDataEntryLength();
    if (Key == *MaybeStr)
      return make_range({*this, DataOffset},
                        SameNameIterator{*this, EndOffset});
    DataOffset = EndOffset;
    StrOffset = readStringOffsetAt(DataOffset);
  }

  return EmptyRange;
}

std::optional<uint32_t>
AppleAcceleratorTable::idxOfHashInBucket(uint32_t HashToFind,
                                         uint32_t BucketIdx) const {
  std::optional<uint32_t> HashStartIdx = readIthBucket(BucketIdx);
  if (!HashStartIdx)
    return std::nullopt;

  for (uint32_t HashIdx = *HashStartIdx; HashIdx < getNumHashes(); HashIdx++) {
    std::optional<uint32_t> MaybeHash = readIthHash(HashIdx);
    if (!MaybeHash || !wouldHashBeInBucket(*MaybeHash, BucketIdx))
      break;
    if (*MaybeHash == HashToFind)
      return HashIdx;
  }
  return std::nullopt;
}

std::optional<StringRef> AppleAcceleratorTable::readStringFromStrSection(
    uint64_t StringSectionOffset) const {
  Error E = Error::success();
  StringRef Str = StringSection.getCStrRef(&StringSectionOffset, &E);
  if (E) {
    consumeError(std::move(E));
    return std::nullopt;
  }
  return Str;
}

std::optional<uint32_t>
AppleAcceleratorTable::readU32FromAccel(uint64_t &Offset,
                                        bool UseRelocation) const {
  Error E = Error::success();
  uint32_t Data = UseRelocation
                      ? AccelSection.getRelocatedValue(4, &Offset, nullptr, &E)
                      : AccelSection.getU32(&Offset, &E);
  if (E) {
    consumeError(std::move(E));
    return std::nullopt;
  }
  return Data;
}

void DWARFDebugNames::Header::dump(ScopedPrinter &W) const {
  DictScope HeaderScope(W, "Header");
  W.printHex("Length", UnitLength);
  W.printString("Format", dwarf::FormatString(Format));
  W.printNumber("Version", Version);
  W.printNumber("CU count", CompUnitCount);
  W.printNumber("Local TU count", LocalTypeUnitCount);
  W.printNumber("Foreign TU count", ForeignTypeUnitCount);
  W.printNumber("Bucket count", BucketCount);
  W.printNumber("Name count", NameCount);
  W.printHex("Abbreviations table size", AbbrevTableSize);
  W.startLine() << "Augmentation: '" << AugmentationString << "'\n";
}

Error DWARFDebugNames::Header::extract(const DWARFDataExtractor &AS,
                                             uint64_t *Offset) {
  auto HeaderError = [Offset = *Offset](Error E) {
    return createStringError(errc::illegal_byte_sequence,
                             "parsing .debug_names header at 0x%" PRIx64 ": %s",
                             Offset, toString(std::move(E)).c_str());
  };

  DataExtractor::Cursor C(*Offset);
  std::tie(UnitLength, Format) = AS.getInitialLength(C);

  Version = AS.getU16(C);
  AS.skip(C, 2); // padding
  CompUnitCount = AS.getU32(C);
  LocalTypeUnitCount = AS.getU32(C);
  ForeignTypeUnitCount = AS.getU32(C);
  BucketCount = AS.getU32(C);
  NameCount = AS.getU32(C);
  AbbrevTableSize = AS.getU32(C);
  AugmentationStringSize = alignTo(AS.getU32(C), 4);

  if (!C)
    return HeaderError(C.takeError());

  if (!AS.isValidOffsetForDataOfSize(C.tell(), AugmentationStringSize))
    return HeaderError(createStringError(errc::illegal_byte_sequence,
                                         "cannot read header augmentation"));
  AugmentationString.resize(AugmentationStringSize);
  AS.getU8(C, reinterpret_cast<uint8_t *>(AugmentationString.data()),
           AugmentationStringSize);
  *Offset = C.tell();
  return C.takeError();
}

void DWARFDebugNames::Abbrev::dump(ScopedPrinter &W) const {
  DictScope AbbrevScope(W, ("Abbreviation 0x" + Twine::utohexstr(Code)).str());
  W.startLine() << formatv("Tag: {0}\n", Tag);

  for (const auto &Attr : Attributes)
    W.startLine() << formatv("{0}: {1}\n", Attr.Index, Attr.Form);
}

static constexpr DWARFDebugNames::AttributeEncoding sentinelAttrEnc() {
  return {dwarf::Index(0), dwarf::Form(0)};
}

static bool isSentinel(const DWARFDebugNames::AttributeEncoding &AE) {
  return AE == sentinelAttrEnc();
}

static DWARFDebugNames::Abbrev sentinelAbbrev() {
  return DWARFDebugNames::Abbrev(0, dwarf::Tag(0), 0, {});
}

static bool isSentinel(const DWARFDebugNames::Abbrev &Abbr) {
  return Abbr.Code == 0;
}

DWARFDebugNames::Abbrev DWARFDebugNames::AbbrevMapInfo::getEmptyKey() {
  return sentinelAbbrev();
}

DWARFDebugNames::Abbrev DWARFDebugNames::AbbrevMapInfo::getTombstoneKey() {
  return DWARFDebugNames::Abbrev(~0, dwarf::Tag(0), 0, {});
}

Expected<DWARFDebugNames::AttributeEncoding>
DWARFDebugNames::NameIndex::extractAttributeEncoding(uint64_t *Offset) {
  if (*Offset >= Offsets.EntriesBase) {
    return createStringError(errc::illegal_byte_sequence,
                             "Incorrectly terminated abbreviation table.");
  }

  uint32_t Index = Section.AccelSection.getULEB128(Offset);
  uint32_t Form = Section.AccelSection.getULEB128(Offset);
  return AttributeEncoding(dwarf::Index(Index), dwarf::Form(Form));
}

Expected<std::vector<DWARFDebugNames::AttributeEncoding>>
DWARFDebugNames::NameIndex::extractAttributeEncodings(uint64_t *Offset) {
  std::vector<AttributeEncoding> Result;
  for (;;) {
    auto AttrEncOr = extractAttributeEncoding(Offset);
    if (!AttrEncOr)
      return AttrEncOr.takeError();
    if (isSentinel(*AttrEncOr))
      return std::move(Result);

    Result.emplace_back(*AttrEncOr);
  }
}

Expected<DWARFDebugNames::Abbrev>
DWARFDebugNames::NameIndex::extractAbbrev(uint64_t *Offset) {
  if (*Offset >= Offsets.EntriesBase) {
    return createStringError(errc::illegal_byte_sequence,
                             "Incorrectly terminated abbreviation table.");
  }
  const uint64_t AbbrevOffset = *Offset;
  uint32_t Code = Section.AccelSection.getULEB128(Offset);
  if (Code == 0)
    return sentinelAbbrev();

  uint32_t Tag = Section.AccelSection.getULEB128(Offset);
  auto AttrEncOr = extractAttributeEncodings(Offset);
  if (!AttrEncOr)
    return AttrEncOr.takeError();
  return Abbrev(Code, dwarf::Tag(Tag), AbbrevOffset, std::move(*AttrEncOr));
}

DWARFDebugNames::DWARFDebugNamesOffsets
dwarf::findDebugNamesOffsets(uint64_t EndOfHeaderOffset,
                             const DWARFDebugNames::Header &Hdr) {
  uint64_t DwarfSize = getDwarfOffsetByteSize(Hdr.Format);
  DWARFDebugNames::DWARFDebugNamesOffsets Ret;
  Ret.CUsBase = EndOfHeaderOffset;
  Ret.BucketsBase = Ret.CUsBase + Hdr.CompUnitCount * DwarfSize +
                    Hdr.LocalTypeUnitCount * DwarfSize +
                    Hdr.ForeignTypeUnitCount * 8;
  Ret.HashesBase = Ret.BucketsBase + Hdr.BucketCount * 4;
  Ret.StringOffsetsBase =
      Ret.HashesBase + (Hdr.BucketCount > 0 ? Hdr.NameCount * 4 : 0);
  Ret.EntryOffsetsBase = Ret.StringOffsetsBase + Hdr.NameCount * DwarfSize;
  Ret.EntriesBase =
      Ret.EntryOffsetsBase + Hdr.NameCount * DwarfSize + Hdr.AbbrevTableSize;
  return Ret;
}

Error DWARFDebugNames::NameIndex::extract() {
  const DWARFDataExtractor &AS = Section.AccelSection;
  uint64_t EndOfHeaderOffset = Base;
  if (Error E = Hdr.extract(AS, &EndOfHeaderOffset))
    return E;

  const unsigned SectionOffsetSize = dwarf::getDwarfOffsetByteSize(Hdr.Format);
  Offsets = dwarf::findDebugNamesOffsets(EndOfHeaderOffset, Hdr);

  uint64_t Offset =
      Offsets.EntryOffsetsBase + (Hdr.NameCount * SectionOffsetSize);

  if (!AS.isValidOffsetForDataOfSize(Offset, Hdr.AbbrevTableSize))
    return createStringError(errc::illegal_byte_sequence,
                             "Section too small: cannot read abbreviations.");

  Offsets.EntriesBase = Offset + Hdr.AbbrevTableSize;

  for (;;) {
    auto AbbrevOr = extractAbbrev(&Offset);
    if (!AbbrevOr)
      return AbbrevOr.takeError();
    if (isSentinel(*AbbrevOr))
      return Error::success();

    if (!Abbrevs.insert(std::move(*AbbrevOr)).second)
      return createStringError(errc::invalid_argument,
                               "Duplicate abbreviation code.");
  }
}

DWARFDebugNames::Entry::Entry(const NameIndex &NameIdx, const Abbrev &Abbr)
    : NameIdx(&NameIdx), Abbr(&Abbr) {
  // This merely creates form values. It is up to the caller
  // (NameIndex::getEntry) to populate them.
  Values.reserve(Abbr.Attributes.size());
  for (const auto &Attr : Abbr.Attributes)
    Values.emplace_back(Attr.Form);
}

std::optional<DWARFFormValue>
DWARFDebugNames::Entry::lookup(dwarf::Index Index) const {
  assert(Abbr->Attributes.size() == Values.size());
  for (auto Tuple : zip_first(Abbr->Attributes, Values)) {
    if (std::get<0>(Tuple).Index == Index)
      return std::get<1>(Tuple);
  }
  return std::nullopt;
}

bool DWARFDebugNames::Entry::hasParentInformation() const {
  return lookup(dwarf::DW_IDX_parent).has_value();
}

std::optional<uint64_t> DWARFDebugNames::Entry::getDIEUnitOffset() const {
  if (std::optional<DWARFFormValue> Off = lookup(dwarf::DW_IDX_die_offset))
    return Off->getAsReferenceUVal();
  return std::nullopt;
}

std::optional<uint64_t> DWARFDebugNames::Entry::getRelatedCUIndex() const {
  // Return the DW_IDX_compile_unit attribute value if it is specified.
  if (std::optional<DWARFFormValue> Off = lookup(dwarf::DW_IDX_compile_unit))
    return Off->getAsUnsignedConstant();
  // In a per-CU index, the entries without a DW_IDX_compile_unit attribute
  // implicitly refer to the single CU. 
  if (NameIdx->getCUCount() == 1)
    return 0;
  return std::nullopt;
}

std::optional<uint64_t> DWARFDebugNames::Entry::getCUIndex() const {
  // Return the DW_IDX_compile_unit attribute value but only if we don't have a
  // DW_IDX_type_unit attribute. Use Entry::getRelatedCUIndex() to get the
  // associated CU index if this behaviour is not desired.
  if (lookup(dwarf::DW_IDX_type_unit).has_value())
    return std::nullopt;
  return getRelatedCUIndex();
}

std::optional<uint64_t> DWARFDebugNames::Entry::getCUOffset() const {
  std::optional<uint64_t> Index = getCUIndex();
  if (!Index || *Index >= NameIdx->getCUCount())
    return std::nullopt;
  return NameIdx->getCUOffset(*Index);
}

std::optional<uint64_t> DWARFDebugNames::Entry::getRelatedCUOffset() const {
  std::optional<uint64_t> Index = getRelatedCUIndex();
  if (!Index || *Index >= NameIdx->getCUCount())
    return std::nullopt;
  return NameIdx->getCUOffset(*Index);
}

std::optional<uint64_t> DWARFDebugNames::Entry::getLocalTUOffset() const {
  std::optional<uint64_t> Index = getLocalTUIndex();
  if (!Index || *Index >= NameIdx->getLocalTUCount())
    return std::nullopt;
  return NameIdx->getLocalTUOffset(*Index);
}

std::optional<uint64_t>
DWARFDebugNames::Entry::getForeignTUTypeSignature() const {
  std::optional<uint64_t> Index = getLocalTUIndex();
  const uint32_t NumLocalTUs = NameIdx->getLocalTUCount();
  if (!Index || *Index < NumLocalTUs)
    return std::nullopt; // Invalid TU index or TU index is for a local TU
  // The foreign TU index is the TU index minus the number of local TUs.
  const uint64_t ForeignTUIndex = *Index - NumLocalTUs;
  if (ForeignTUIndex >= NameIdx->getForeignTUCount())
    return std::nullopt; // Invalid foreign TU index.
  return NameIdx->getForeignTUSignature(ForeignTUIndex);
}

std::optional<uint64_t> DWARFDebugNames::Entry::getLocalTUIndex() const {
  if (std::optional<DWARFFormValue> Off = lookup(dwarf::DW_IDX_type_unit))
    return Off->getAsUnsignedConstant();
  return std::nullopt;
}

Expected<std::optional<DWARFDebugNames::Entry>>
DWARFDebugNames::Entry::getParentDIEEntry() const {
  // The offset of the accelerator table entry for the parent.
  std::optional<DWARFFormValue> ParentEntryOff = lookup(dwarf::DW_IDX_parent);
  assert(ParentEntryOff.has_value() && "hasParentInformation() must be called");

  if (ParentEntryOff->getForm() == dwarf::Form::DW_FORM_flag_present)
    return std::nullopt;
  return NameIdx->getEntryAtRelativeOffset(ParentEntryOff->getRawUValue());
}

void DWARFDebugNames::Entry::dumpParentIdx(
    ScopedPrinter &W, const DWARFFormValue &FormValue) const {
  Expected<std::optional<Entry>> ParentEntry = getParentDIEEntry();
  if (!ParentEntry) {
    W.getOStream() << "<invalid offset data>";
    consumeError(ParentEntry.takeError());
    return;
  }

  if (!ParentEntry->has_value()) {
    W.getOStream() << "<parent not indexed>";
    return;
  }

  auto AbsoluteOffset = NameIdx->Offsets.EntriesBase + FormValue.getRawUValue();
  W.getOStream() << "Entry @ 0x" + Twine::utohexstr(AbsoluteOffset);
}

void DWARFDebugNames::Entry::dump(ScopedPrinter &W) const {
  W.startLine() << formatv("Abbrev: {0:x}\n", Abbr->Code);
  W.startLine() << formatv("Tag: {0}\n", Abbr->Tag);
  assert(Abbr->Attributes.size() == Values.size());
  for (auto Tuple : zip_first(Abbr->Attributes, Values)) {
    auto Index = std::get<0>(Tuple).Index;
    W.startLine() << formatv("{0}: ", Index);

    auto FormValue = std::get<1>(Tuple);
    if (Index == dwarf::Index::DW_IDX_parent)
      dumpParentIdx(W, FormValue);
    else
      FormValue.dump(W.getOStream());
    W.getOStream() << '\n';
  }
}

char DWARFDebugNames::SentinelError::ID;
std::error_code DWARFDebugNames::SentinelError::convertToErrorCode() const {
  return inconvertibleErrorCode();
}

uint64_t DWARFDebugNames::NameIndex::getCUOffset(uint32_t CU) const {
  assert(CU < Hdr.CompUnitCount);
  const unsigned SectionOffsetSize = dwarf::getDwarfOffsetByteSize(Hdr.Format);
  uint64_t Offset = Offsets.CUsBase + SectionOffsetSize * CU;
  return Section.AccelSection.getRelocatedValue(SectionOffsetSize, &Offset);
}

uint64_t DWARFDebugNames::NameIndex::getLocalTUOffset(uint32_t TU) const {
  assert(TU < Hdr.LocalTypeUnitCount);
  const unsigned SectionOffsetSize = dwarf::getDwarfOffsetByteSize(Hdr.Format);
  uint64_t Offset =
      Offsets.CUsBase + SectionOffsetSize * (Hdr.CompUnitCount + TU);
  return Section.AccelSection.getRelocatedValue(SectionOffsetSize, &Offset);
}

uint64_t DWARFDebugNames::NameIndex::getForeignTUSignature(uint32_t TU) const {
  assert(TU < Hdr.ForeignTypeUnitCount);
  const unsigned SectionOffsetSize = dwarf::getDwarfOffsetByteSize(Hdr.Format);
  uint64_t Offset =
      Offsets.CUsBase +
      SectionOffsetSize * (Hdr.CompUnitCount + Hdr.LocalTypeUnitCount) + 8 * TU;
  return Section.AccelSection.getU64(&Offset);
}

Expected<DWARFDebugNames::Entry>
DWARFDebugNames::NameIndex::getEntry(uint64_t *Offset) const {
  const DWARFDataExtractor &AS = Section.AccelSection;
  if (!AS.isValidOffset(*Offset))
    return createStringError(errc::illegal_byte_sequence,
                             "Incorrectly terminated entry list.");

  uint32_t AbbrevCode = AS.getULEB128(Offset);
  if (AbbrevCode == 0)
    return make_error<SentinelError>();

  const auto AbbrevIt = Abbrevs.find_as(AbbrevCode);
  if (AbbrevIt == Abbrevs.end())
    return createStringError(errc::invalid_argument, "Invalid abbreviation.");

  Entry E(*this, *AbbrevIt);

  dwarf::FormParams FormParams = {Hdr.Version, 0, Hdr.Format};
  for (auto &Value : E.Values) {
    if (!Value.extractValue(AS, Offset, FormParams))
      return createStringError(errc::io_error,
                               "Error extracting index attribute values.");
  }
  return std::move(E);
}

DWARFDebugNames::NameTableEntry
DWARFDebugNames::NameIndex::getNameTableEntry(uint32_t Index) const {
  assert(0 < Index && Index <= Hdr.NameCount);
  const unsigned SectionOffsetSize = dwarf::getDwarfOffsetByteSize(Hdr.Format);
  uint64_t StringOffsetOffset =
      Offsets.StringOffsetsBase + SectionOffsetSize * (Index - 1);
  uint64_t EntryOffsetOffset =
      Offsets.EntryOffsetsBase + SectionOffsetSize * (Index - 1);
  const DWARFDataExtractor &AS = Section.AccelSection;

  uint64_t StringOffset =
      AS.getRelocatedValue(SectionOffsetSize, &StringOffsetOffset);
  uint64_t EntryOffset = AS.getUnsigned(&EntryOffsetOffset, SectionOffsetSize);
  EntryOffset += Offsets.EntriesBase;
  return {Section.StringSection, Index, StringOffset, EntryOffset};
}

uint32_t
DWARFDebugNames::NameIndex::getBucketArrayEntry(uint32_t Bucket) const {
  assert(Bucket < Hdr.BucketCount);
  uint64_t BucketOffset = Offsets.BucketsBase + 4 * Bucket;
  return Section.AccelSection.getU32(&BucketOffset);
}

uint32_t DWARFDebugNames::NameIndex::getHashArrayEntry(uint32_t Index) const {
  assert(0 < Index && Index <= Hdr.NameCount);
  uint64_t HashOffset = Offsets.HashesBase + 4 * (Index - 1);
  return Section.AccelSection.getU32(&HashOffset);
}

// Returns true if we should continue scanning for entries, false if this is the
// last (sentinel) entry). In case of a parsing error we also return false, as
// it's not possible to recover this entry list (but the other lists may still
// parse OK).
bool DWARFDebugNames::NameIndex::dumpEntry(ScopedPrinter &W,
                                           uint64_t *Offset) const {
  uint64_t EntryId = *Offset;
  auto EntryOr = getEntry(Offset);
  if (!EntryOr) {
    handleAllErrors(EntryOr.takeError(), [](const SentinelError &) {},
                    [&W](const ErrorInfoBase &EI) { EI.log(W.startLine()); });
    return false;
  }

  DictScope EntryScope(W, ("Entry @ 0x" + Twine::utohexstr(EntryId)).str());
  EntryOr->dump(W);
  return true;
}

void DWARFDebugNames::NameIndex::dumpName(ScopedPrinter &W,
                                          const NameTableEntry &NTE,
                                          std::optional<uint32_t> Hash) const {
  DictScope NameScope(W, ("Name " + Twine(NTE.getIndex())).str());
  if (Hash)
    W.printHex("Hash", *Hash);

  W.startLine() << format("String: 0x%08" PRIx64, NTE.getStringOffset());
  W.getOStream() << " \"" << NTE.getString() << "\"\n";

  uint64_t EntryOffset = NTE.getEntryOffset();
  while (dumpEntry(W, &EntryOffset))
    /*empty*/;
}

void DWARFDebugNames::NameIndex::dumpCUs(ScopedPrinter &W) const {
  ListScope CUScope(W, "Compilation Unit offsets");
  for (uint32_t CU = 0; CU < Hdr.CompUnitCount; ++CU)
    W.startLine() << format("CU[%u]: 0x%08" PRIx64 "\n", CU, getCUOffset(CU));
}

void DWARFDebugNames::NameIndex::dumpLocalTUs(ScopedPrinter &W) const {
  if (Hdr.LocalTypeUnitCount == 0)
    return;

  ListScope TUScope(W, "Local Type Unit offsets");
  for (uint32_t TU = 0; TU < Hdr.LocalTypeUnitCount; ++TU)
    W.startLine() << format("LocalTU[%u]: 0x%08" PRIx64 "\n", TU,
                            getLocalTUOffset(TU));
}

void DWARFDebugNames::NameIndex::dumpForeignTUs(ScopedPrinter &W) const {
  if (Hdr.ForeignTypeUnitCount == 0)
    return;

  ListScope TUScope(W, "Foreign Type Unit signatures");
  for (uint32_t TU = 0; TU < Hdr.ForeignTypeUnitCount; ++TU) {
    W.startLine() << format("ForeignTU[%u]: 0x%016" PRIx64 "\n", TU,
                            getForeignTUSignature(TU));
  }
}

void DWARFDebugNames::NameIndex::dumpAbbreviations(ScopedPrinter &W) const {
  ListScope AbbrevsScope(W, "Abbreviations");
  std::vector<const Abbrev *> AbbrevsVect;
  for (const DWARFDebugNames::Abbrev &Abbr : Abbrevs)
    AbbrevsVect.push_back(&Abbr);
  llvm::sort(AbbrevsVect, [](const Abbrev *LHS, const Abbrev *RHS) {
    return LHS->AbbrevOffset < RHS->AbbrevOffset;
  });
  for (const DWARFDebugNames::Abbrev *Abbr : AbbrevsVect)
    Abbr->dump(W);
}

void DWARFDebugNames::NameIndex::dumpBucket(ScopedPrinter &W,
                                            uint32_t Bucket) const {
  ListScope BucketScope(W, ("Bucket " + Twine(Bucket)).str());
  uint32_t Index = getBucketArrayEntry(Bucket);
  if (Index == 0) {
    W.printString("EMPTY");
    return;
  }
  if (Index > Hdr.NameCount) {
    W.printString("Name index is invalid");
    return;
  }

  for (; Index <= Hdr.NameCount; ++Index) {
    uint32_t Hash = getHashArrayEntry(Index);
    if (Hash % Hdr.BucketCount != Bucket)
      break;

    dumpName(W, getNameTableEntry(Index), Hash);
  }
}

LLVM_DUMP_METHOD void DWARFDebugNames::NameIndex::dump(ScopedPrinter &W) const {
  DictScope UnitScope(W, ("Name Index @ 0x" + Twine::utohexstr(Base)).str());
  Hdr.dump(W);
  dumpCUs(W);
  dumpLocalTUs(W);
  dumpForeignTUs(W);
  dumpAbbreviations(W);

  if (Hdr.BucketCount > 0) {
    for (uint32_t Bucket = 0; Bucket < Hdr.BucketCount; ++Bucket)
      dumpBucket(W, Bucket);
    return;
  }

  W.startLine() << "Hash table not present\n";
  for (const NameTableEntry &NTE : *this)
    dumpName(W, NTE, std::nullopt);
}

Error DWARFDebugNames::extract() {
  uint64_t Offset = 0;
  while (AccelSection.isValidOffset(Offset)) {
    NameIndex Next(*this, Offset);
    if (Error E = Next.extract())
      return E;
    Offset = Next.getNextUnitOffset();
    NameIndices.push_back(std::move(Next));
  }
  return Error::success();
}

iterator_range<DWARFDebugNames::ValueIterator>
DWARFDebugNames::NameIndex::equal_range(StringRef Key) const {
  return make_range(ValueIterator(*this, Key), ValueIterator());
}

LLVM_DUMP_METHOD void DWARFDebugNames::dump(raw_ostream &OS) const {
  ScopedPrinter W(OS);
  for (const NameIndex &NI : NameIndices)
    NI.dump(W);
}

std::optional<uint64_t>
DWARFDebugNames::ValueIterator::findEntryOffsetInCurrentIndex() {
  const Header &Hdr = CurrentIndex->Hdr;
  if (Hdr.BucketCount == 0) {
    // No Hash Table, We need to search through all names in the Name Index.
    for (const NameTableEntry &NTE : *CurrentIndex) {
      if (NTE.sameNameAs(Key))
        return NTE.getEntryOffset();
    }
    return std::nullopt;
  }

  // The Name Index has a Hash Table, so use that to speed up the search.
  // Compute the Key Hash, if it has not been done already.
  if (!Hash)
    Hash = caseFoldingDjbHash(Key);
  uint32_t Bucket = *Hash % Hdr.BucketCount;
  uint32_t Index = CurrentIndex->getBucketArrayEntry(Bucket);
  if (Index == 0)
    return std::nullopt; // Empty bucket

  for (; Index <= Hdr.NameCount; ++Index) {
    uint32_t HashAtIndex = CurrentIndex->getHashArrayEntry(Index);
    if (HashAtIndex % Hdr.BucketCount != Bucket)
      return std::nullopt; // End of bucket
    // Only compare names if the hashes match.
    if (HashAtIndex != Hash)
      continue;

    NameTableEntry NTE = CurrentIndex->getNameTableEntry(Index);
    if (NTE.sameNameAs(Key))
      return NTE.getEntryOffset();
  }
  return std::nullopt;
}

bool DWARFDebugNames::ValueIterator::getEntryAtCurrentOffset() {
  auto EntryOr = CurrentIndex->getEntry(&DataOffset);
  if (!EntryOr) {
    consumeError(EntryOr.takeError());
    return false;
  }
  CurrentEntry = std::move(*EntryOr);
  return true;
}

bool DWARFDebugNames::ValueIterator::findInCurrentIndex() {
  std::optional<uint64_t> Offset = findEntryOffsetInCurrentIndex();
  if (!Offset)
    return false;
  DataOffset = *Offset;
  return getEntryAtCurrentOffset();
}

void DWARFDebugNames::ValueIterator::searchFromStartOfCurrentIndex() {
  for (const NameIndex *End = CurrentIndex->Section.NameIndices.end();
       CurrentIndex != End; ++CurrentIndex) {
    if (findInCurrentIndex())
      return;
  }
  setEnd();
}

void DWARFDebugNames::ValueIterator::next() {
  assert(CurrentIndex && "Incrementing an end() iterator?");

  // First try the next entry in the current Index.
  if (getEntryAtCurrentOffset())
    return;

  // If we're a local iterator or we have reached the last Index, we're done.
  if (IsLocal || CurrentIndex == &CurrentIndex->Section.NameIndices.back()) {
    setEnd();
    return;
  }

  // Otherwise, try the next index.
  ++CurrentIndex;
  searchFromStartOfCurrentIndex();
}

DWARFDebugNames::ValueIterator::ValueIterator(const DWARFDebugNames &AccelTable,
                                              StringRef Key)
    : CurrentIndex(AccelTable.NameIndices.begin()), IsLocal(false),
      Key(std::string(Key)) {
  searchFromStartOfCurrentIndex();
}

DWARFDebugNames::ValueIterator::ValueIterator(
    const DWARFDebugNames::NameIndex &NI, StringRef Key)
    : CurrentIndex(&NI), IsLocal(true), Key(std::string(Key)) {
  if (!findInCurrentIndex())
    setEnd();
}

iterator_range<DWARFDebugNames::ValueIterator>
DWARFDebugNames::equal_range(StringRef Key) const {
  if (NameIndices.empty())
    return make_range(ValueIterator(), ValueIterator());
  return make_range(ValueIterator(*this, Key), ValueIterator());
}

const DWARFDebugNames::NameIndex *
DWARFDebugNames::getCUNameIndex(uint64_t CUOffset) {
  if (CUToNameIndex.size() == 0 && NameIndices.size() > 0) {
    for (const auto &NI : *this) {
      for (uint32_t CU = 0; CU < NI.getCUCount(); ++CU)
        CUToNameIndex.try_emplace(NI.getCUOffset(CU), &NI);
    }
  }
  return CUToNameIndex.lookup(CUOffset);
}

static bool isObjCSelector(StringRef Name) {
  return Name.size() > 2 && (Name[0] == '-' || Name[0] == '+') &&
         (Name[1] == '[');
}

std::optional<ObjCSelectorNames> llvm::getObjCNamesIfSelector(StringRef Name) {
  if (!isObjCSelector(Name))
    return std::nullopt;
  // "-[Atom setMass:]"
  StringRef ClassNameStart(Name.drop_front(2));
  size_t FirstSpace = ClassNameStart.find(' ');
  if (FirstSpace == StringRef::npos)
    return std::nullopt;

  StringRef SelectorStart = ClassNameStart.drop_front(FirstSpace + 1);
  if (!SelectorStart.size())
    return std::nullopt;

  ObjCSelectorNames Ans;
  Ans.ClassName = ClassNameStart.take_front(FirstSpace);
  Ans.Selector = SelectorStart.drop_back(); // drop ']';

  // "-[Class(Category) selector :withArg ...]"
  if (Ans.ClassName.back() == ')') {
    size_t OpenParens = Ans.ClassName.find('(');
    if (OpenParens != StringRef::npos) {
      Ans.ClassNameNoCategory = Ans.ClassName.take_front(OpenParens);

      Ans.MethodNameNoCategory = Name.take_front(OpenParens + 2);
      // FIXME: The missing space here may be a bug, but dsymutil-classic also
      // does it this way.
      append_range(*Ans.MethodNameNoCategory, SelectorStart);
    }
  }
  return Ans;
}

std::optional<StringRef> llvm::StripTemplateParameters(StringRef Name) {
  // We are looking for template parameters to strip from Name. e.g.
  //
  //  operator<<B>
  //
  // We look for > at the end but if it does not contain any < then we
  // have something like operator>>. We check for the operator<=> case.
  if (!Name.ends_with(">") || Name.count("<") == 0 || Name.ends_with("<=>"))
    return {};

  // How many < until we have the start of the template parameters.
  size_t NumLeftAnglesToSkip = 1;

  // If we have operator<=> then we need to skip its < as well.
  NumLeftAnglesToSkip += Name.count("<=>");

  size_t RightAngleCount = Name.count('>');
  size_t LeftAngleCount = Name.count('<');

  // If we have more < than > we have operator< or operator<<
  // we to account for their < as well.
  if (LeftAngleCount > RightAngleCount)
    NumLeftAnglesToSkip += LeftAngleCount - RightAngleCount;

  size_t StartOfTemplate = 0;
  while (NumLeftAnglesToSkip--)
    StartOfTemplate = Name.find('<', StartOfTemplate) + 1;

  return Name.substr(0, StartOfTemplate - 1);
}
