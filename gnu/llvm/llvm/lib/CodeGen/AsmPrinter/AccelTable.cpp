//===- llvm/CodeGen/AsmPrinter/AccelTable.cpp - Accelerator Tables --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing accelerator tables.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/AccelTable.h"
#include "DwarfCompileUnit.h"
#include "DwarfUnit.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using namespace llvm;

void AccelTableBase::computeBucketCount() {
  SmallVector<uint32_t, 0> Uniques;
  Uniques.reserve(Entries.size());
  for (const auto &E : Entries)
    Uniques.push_back(E.second.HashValue);
  llvm::sort(Uniques);
  UniqueHashCount = llvm::unique(Uniques) - Uniques.begin();
  BucketCount = dwarf::getDebugNamesBucketCount(UniqueHashCount);
}

void AccelTableBase::finalize(AsmPrinter *Asm, StringRef Prefix) {
  // Create the individual hash data outputs.
  for (auto &E : Entries) {
    // Unique the entries.
    llvm::stable_sort(E.second.Values,
                      [](const AccelTableData *A, const AccelTableData *B) {
                        return *A < *B;
                      });
    E.second.Values.erase(llvm::unique(E.second.Values), E.second.Values.end());
  }

  // Figure out how many buckets we need, then compute the bucket contents and
  // the final ordering. The hashes and offsets can be emitted by walking these
  // data structures. We add temporary symbols to the data so they can be
  // referenced when emitting the offsets.
  computeBucketCount();

  // Compute bucket contents and final ordering.
  Buckets.resize(BucketCount);
  for (auto &E : Entries) {
    uint32_t Bucket = E.second.HashValue % BucketCount;
    Buckets[Bucket].push_back(&E.second);
    E.second.Sym = Asm->createTempSymbol(Prefix);
  }

  // Sort the contents of the buckets by hash value so that hash collisions end
  // up together. Stable sort makes testing easier and doesn't cost much more.
  for (auto &Bucket : Buckets)
    llvm::stable_sort(Bucket, [](HashData *LHS, HashData *RHS) {
      return LHS->HashValue < RHS->HashValue;
    });
}

namespace {
/// Base class for writing out Accelerator tables. It holds the common
/// functionality for the two Accelerator table types.
class AccelTableWriter {
protected:
  AsmPrinter *const Asm;          ///< Destination.
  const AccelTableBase &Contents; ///< Data to emit.

  /// Controls whether to emit duplicate hash and offset table entries for names
  /// with identical hashes. Apple tables don't emit duplicate entries, DWARF v5
  /// tables do.
  const bool SkipIdenticalHashes;

  void emitHashes() const;

  /// Emit offsets to lists of entries with identical names. The offsets are
  /// relative to the Base argument.
  void emitOffsets(const MCSymbol *Base) const;

public:
  AccelTableWriter(AsmPrinter *Asm, const AccelTableBase &Contents,
                   bool SkipIdenticalHashes)
      : Asm(Asm), Contents(Contents), SkipIdenticalHashes(SkipIdenticalHashes) {
  }
};

class AppleAccelTableWriter : public AccelTableWriter {
  using Atom = AppleAccelTableData::Atom;

  /// The fixed header of an Apple Accelerator Table.
  struct Header {
    uint32_t Magic = MagicHash;
    uint16_t Version = 1;
    uint16_t HashFunction = dwarf::DW_hash_function_djb;
    uint32_t BucketCount;
    uint32_t HashCount;
    uint32_t HeaderDataLength;

    /// 'HASH' magic value to detect endianness.
    static const uint32_t MagicHash = 0x48415348;

    Header(uint32_t BucketCount, uint32_t UniqueHashCount, uint32_t DataLength)
        : BucketCount(BucketCount), HashCount(UniqueHashCount),
          HeaderDataLength(DataLength) {}

    void emit(AsmPrinter *Asm) const;
#ifndef NDEBUG
    void print(raw_ostream &OS) const;
    void dump() const { print(dbgs()); }
#endif
  };

  /// The HeaderData describes the structure of an Apple accelerator table
  /// through a list of Atoms.
  struct HeaderData {
    /// In the case of data that is referenced via DW_FORM_ref_* the offset
    /// base is used to describe the offset for all forms in the list of atoms.
    uint32_t DieOffsetBase;

    const SmallVector<Atom, 4> Atoms;

    HeaderData(ArrayRef<Atom> AtomList, uint32_t Offset = 0)
        : DieOffsetBase(Offset), Atoms(AtomList.begin(), AtomList.end()) {}

    void emit(AsmPrinter *Asm) const;
#ifndef NDEBUG
    void print(raw_ostream &OS) const;
    void dump() const { print(dbgs()); }
#endif
  };

  Header Header;
  HeaderData HeaderData;
  const MCSymbol *SecBegin;

  void emitBuckets() const;
  void emitData() const;

public:
  AppleAccelTableWriter(AsmPrinter *Asm, const AccelTableBase &Contents,
                        ArrayRef<Atom> Atoms, const MCSymbol *SecBegin)
      : AccelTableWriter(Asm, Contents, true),
        Header(Contents.getBucketCount(), Contents.getUniqueHashCount(),
               8 + (Atoms.size() * 4)),
        HeaderData(Atoms), SecBegin(SecBegin) {}

  void emit() const;

#ifndef NDEBUG
  void print(raw_ostream &OS) const;
  void dump() const { print(dbgs()); }
#endif
};

/// Class responsible for emitting a DWARF v5 Accelerator Table. The only
/// public function is emit(), which performs the actual emission.
///
/// A callback abstracts the logic to provide a CU index for a given entry.
class Dwarf5AccelTableWriter : public AccelTableWriter {
  struct Header {
    uint16_t Version = 5;
    uint16_t Padding = 0;
    uint32_t CompUnitCount;
    uint32_t LocalTypeUnitCount = 0;
    uint32_t ForeignTypeUnitCount = 0;
    uint32_t BucketCount = 0;
    uint32_t NameCount = 0;
    uint32_t AbbrevTableSize = 0;
    uint32_t AugmentationStringSize = sizeof(AugmentationString);
    char AugmentationString[8] = {'L', 'L', 'V', 'M', '0', '7', '0', '0'};

    Header(uint32_t CompUnitCount, uint32_t LocalTypeUnitCount,
           uint32_t ForeignTypeUnitCount, uint32_t BucketCount,
           uint32_t NameCount)
        : CompUnitCount(CompUnitCount), LocalTypeUnitCount(LocalTypeUnitCount),
          ForeignTypeUnitCount(ForeignTypeUnitCount), BucketCount(BucketCount),
          NameCount(NameCount) {}

    void emit(Dwarf5AccelTableWriter &Ctx);
  };

  Header Header;
  /// FoldingSet that uniques the abbreviations.
  FoldingSet<DebugNamesAbbrev> AbbreviationsSet;
  /// Vector containing DebugNames abbreviations for iteration in order.
  SmallVector<DebugNamesAbbrev *, 5> AbbreviationsVector;
  /// The bump allocator to use when creating DIEAbbrev objects in the uniqued
  /// storage container.
  BumpPtrAllocator Alloc;
  ArrayRef<std::variant<MCSymbol *, uint64_t>> CompUnits;
  ArrayRef<std::variant<MCSymbol *, uint64_t>> TypeUnits;
  llvm::function_ref<std::optional<DWARF5AccelTable::UnitIndexAndEncoding>(
      const DWARF5AccelTableData &)>
      getIndexForEntry;
  MCSymbol *ContributionEnd = nullptr;
  MCSymbol *AbbrevStart = Asm->createTempSymbol("names_abbrev_start");
  MCSymbol *AbbrevEnd = Asm->createTempSymbol("names_abbrev_end");
  MCSymbol *EntryPool = Asm->createTempSymbol("names_entries");
  // Indicates if this module is built with Split Dwarf enabled.
  bool IsSplitDwarf = false;
  /// Stores the DIE offsets which are indexed by this table.
  DenseSet<OffsetAndUnitID> IndexedOffsets;

  void populateAbbrevsMap();

  void emitCUList() const;
  void emitTUList() const;
  void emitBuckets() const;
  void emitStringOffsets() const;
  void emitAbbrevs() const;
  void emitEntry(
      const DWARF5AccelTableData &Entry,
      const DenseMap<OffsetAndUnitID, MCSymbol *> &DIEOffsetToAccelEntryLabel,
      DenseSet<MCSymbol *> &EmittedAccelEntrySymbols);
  void emitData();

public:
  Dwarf5AccelTableWriter(
      AsmPrinter *Asm, const AccelTableBase &Contents,
      ArrayRef<std::variant<MCSymbol *, uint64_t>> CompUnits,
      ArrayRef<std::variant<MCSymbol *, uint64_t>> TypeUnits,
      llvm::function_ref<std::optional<DWARF5AccelTable::UnitIndexAndEncoding>(
          const DWARF5AccelTableData &)>
          getIndexForEntry,
      bool IsSplitDwarf);
  ~Dwarf5AccelTableWriter() {
    for (DebugNamesAbbrev *Abbrev : AbbreviationsVector)
      Abbrev->~DebugNamesAbbrev();
  }
  void emit();
};
} // namespace

void AccelTableWriter::emitHashes() const {
  uint64_t PrevHash = std::numeric_limits<uint64_t>::max();
  unsigned BucketIdx = 0;
  for (const auto &Bucket : Contents.getBuckets()) {
    for (const auto &Hash : Bucket) {
      uint32_t HashValue = Hash->HashValue;
      if (SkipIdenticalHashes && PrevHash == HashValue)
        continue;
      Asm->OutStreamer->AddComment("Hash in Bucket " + Twine(BucketIdx));
      Asm->emitInt32(HashValue);
      PrevHash = HashValue;
    }
    BucketIdx++;
  }
}

void AccelTableWriter::emitOffsets(const MCSymbol *Base) const {
  const auto &Buckets = Contents.getBuckets();
  uint64_t PrevHash = std::numeric_limits<uint64_t>::max();
  for (size_t i = 0, e = Buckets.size(); i < e; ++i) {
    for (auto *Hash : Buckets[i]) {
      uint32_t HashValue = Hash->HashValue;
      if (SkipIdenticalHashes && PrevHash == HashValue)
        continue;
      PrevHash = HashValue;
      Asm->OutStreamer->AddComment("Offset in Bucket " + Twine(i));
      Asm->emitLabelDifference(Hash->Sym, Base, Asm->getDwarfOffsetByteSize());
    }
  }
}

void AppleAccelTableWriter::Header::emit(AsmPrinter *Asm) const {
  Asm->OutStreamer->AddComment("Header Magic");
  Asm->emitInt32(Magic);
  Asm->OutStreamer->AddComment("Header Version");
  Asm->emitInt16(Version);
  Asm->OutStreamer->AddComment("Header Hash Function");
  Asm->emitInt16(HashFunction);
  Asm->OutStreamer->AddComment("Header Bucket Count");
  Asm->emitInt32(BucketCount);
  Asm->OutStreamer->AddComment("Header Hash Count");
  Asm->emitInt32(HashCount);
  Asm->OutStreamer->AddComment("Header Data Length");
  Asm->emitInt32(HeaderDataLength);
}

void AppleAccelTableWriter::HeaderData::emit(AsmPrinter *Asm) const {
  Asm->OutStreamer->AddComment("HeaderData Die Offset Base");
  Asm->emitInt32(DieOffsetBase);
  Asm->OutStreamer->AddComment("HeaderData Atom Count");
  Asm->emitInt32(Atoms.size());

  for (const Atom &A : Atoms) {
    Asm->OutStreamer->AddComment(dwarf::AtomTypeString(A.Type));
    Asm->emitInt16(A.Type);
    Asm->OutStreamer->AddComment(dwarf::FormEncodingString(A.Form));
    Asm->emitInt16(A.Form);
  }
}

void AppleAccelTableWriter::emitBuckets() const {
  const auto &Buckets = Contents.getBuckets();
  unsigned index = 0;
  for (size_t i = 0, e = Buckets.size(); i < e; ++i) {
    Asm->OutStreamer->AddComment("Bucket " + Twine(i));
    if (!Buckets[i].empty())
      Asm->emitInt32(index);
    else
      Asm->emitInt32(std::numeric_limits<uint32_t>::max());
    // Buckets point in the list of hashes, not to the data. Do not increment
    // the index multiple times in case of hash collisions.
    uint64_t PrevHash = std::numeric_limits<uint64_t>::max();
    for (auto *HD : Buckets[i]) {
      uint32_t HashValue = HD->HashValue;
      if (PrevHash != HashValue)
        ++index;
      PrevHash = HashValue;
    }
  }
}

void AppleAccelTableWriter::emitData() const {
  const auto &Buckets = Contents.getBuckets();
  for (const AccelTableBase::HashList &Bucket : Buckets) {
    uint64_t PrevHash = std::numeric_limits<uint64_t>::max();
    for (const auto &Hash : Bucket) {
      // Terminate the previous entry if there is no hash collision with the
      // current one.
      if (PrevHash != std::numeric_limits<uint64_t>::max() &&
          PrevHash != Hash->HashValue)
        Asm->emitInt32(0);
      // Remember to emit the label for our offset.
      Asm->OutStreamer->emitLabel(Hash->Sym);
      Asm->OutStreamer->AddComment(Hash->Name.getString());
      Asm->emitDwarfStringOffset(Hash->Name);
      Asm->OutStreamer->AddComment("Num DIEs");
      Asm->emitInt32(Hash->Values.size());
      for (const auto *V : Hash->getValues<const AppleAccelTableData *>())
        V->emit(Asm);
      PrevHash = Hash->HashValue;
    }
    // Emit the final end marker for the bucket.
    if (!Bucket.empty())
      Asm->emitInt32(0);
  }
}

void AppleAccelTableWriter::emit() const {
  Header.emit(Asm);
  HeaderData.emit(Asm);
  emitBuckets();
  emitHashes();
  emitOffsets(SecBegin);
  emitData();
}

DWARF5AccelTableData::DWARF5AccelTableData(const DIE &Die,
                                           const uint32_t UnitID,
                                           const bool IsTU)
    : OffsetVal(&Die), DieTag(Die.getTag()), AbbrevNumber(0), IsTU(IsTU),
      UnitID(UnitID) {}

void Dwarf5AccelTableWriter::Header::emit(Dwarf5AccelTableWriter &Ctx) {
  assert(CompUnitCount > 0 && "Index must have at least one CU.");

  AsmPrinter *Asm = Ctx.Asm;
  Ctx.ContributionEnd =
      Asm->emitDwarfUnitLength("names", "Header: unit length");
  Asm->OutStreamer->AddComment("Header: version");
  Asm->emitInt16(Version);
  Asm->OutStreamer->AddComment("Header: padding");
  Asm->emitInt16(Padding);
  Asm->OutStreamer->AddComment("Header: compilation unit count");
  Asm->emitInt32(CompUnitCount);
  Asm->OutStreamer->AddComment("Header: local type unit count");
  Asm->emitInt32(LocalTypeUnitCount);
  Asm->OutStreamer->AddComment("Header: foreign type unit count");
  Asm->emitInt32(ForeignTypeUnitCount);
  Asm->OutStreamer->AddComment("Header: bucket count");
  Asm->emitInt32(BucketCount);
  Asm->OutStreamer->AddComment("Header: name count");
  Asm->emitInt32(NameCount);
  Asm->OutStreamer->AddComment("Header: abbreviation table size");
  Asm->emitLabelDifference(Ctx.AbbrevEnd, Ctx.AbbrevStart, sizeof(uint32_t));
  Asm->OutStreamer->AddComment("Header: augmentation string size");
  assert(AugmentationStringSize % 4 == 0);
  Asm->emitInt32(AugmentationStringSize);
  Asm->OutStreamer->AddComment("Header: augmentation string");
  Asm->OutStreamer->emitBytes({AugmentationString, AugmentationStringSize});
}

std::optional<uint64_t>
DWARF5AccelTableData::getDefiningParentDieOffset(const DIE &Die) {
  if (auto *Parent = Die.getParent();
      Parent && !Parent->findAttribute(dwarf::Attribute::DW_AT_declaration))
    return Parent->getOffset();
  return {};
}

static std::optional<dwarf::Form>
getFormForIdxParent(const DenseSet<OffsetAndUnitID> &IndexedOffsets,
                    std::optional<OffsetAndUnitID> ParentOffset) {
  // No parent information
  if (!ParentOffset)
    return std::nullopt;
  // Parent is indexed by this table.
  if (IndexedOffsets.contains(*ParentOffset))
    return dwarf::Form::DW_FORM_ref4;
  // Parent is not indexed by this table.
  return dwarf::Form::DW_FORM_flag_present;
}

void DebugNamesAbbrev::Profile(FoldingSetNodeID &ID) const {
  ID.AddInteger(DieTag);
  for (const DebugNamesAbbrev::AttributeEncoding &Enc : AttrVect) {
    ID.AddInteger(Enc.Index);
    ID.AddInteger(Enc.Form);
  }
}

void Dwarf5AccelTableWriter::populateAbbrevsMap() {
  for (auto &Bucket : Contents.getBuckets()) {
    for (auto *Hash : Bucket) {
      for (auto *Value : Hash->getValues<DWARF5AccelTableData *>()) {
        std::optional<DWARF5AccelTable::UnitIndexAndEncoding> EntryRet =
            getIndexForEntry(*Value);
        std::optional<dwarf::Form> MaybeParentForm = getFormForIdxParent(
            IndexedOffsets, Value->getParentDieOffsetAndUnitID());
        DebugNamesAbbrev Abbrev(Value->getDieTag());
        if (EntryRet)
          Abbrev.addAttribute(EntryRet->Encoding);
        Abbrev.addAttribute({dwarf::DW_IDX_die_offset, dwarf::DW_FORM_ref4});
        if (MaybeParentForm)
          Abbrev.addAttribute({dwarf::DW_IDX_parent, *MaybeParentForm});
        FoldingSetNodeID ID;
        Abbrev.Profile(ID);
        void *InsertPos;
        if (DebugNamesAbbrev *Existing =
                AbbreviationsSet.FindNodeOrInsertPos(ID, InsertPos)) {
          Value->setAbbrevNumber(Existing->getNumber());
          continue;
        }
        DebugNamesAbbrev *NewAbbrev =
            new (Alloc) DebugNamesAbbrev(std::move(Abbrev));
        AbbreviationsVector.push_back(NewAbbrev);
        NewAbbrev->setNumber(AbbreviationsVector.size());
        AbbreviationsSet.InsertNode(NewAbbrev, InsertPos);
        Value->setAbbrevNumber(NewAbbrev->getNumber());
      }
    }
  }
}

void Dwarf5AccelTableWriter::emitCUList() const {
  for (const auto &CU : enumerate(CompUnits)) {
    Asm->OutStreamer->AddComment("Compilation unit " + Twine(CU.index()));
    if (std::holds_alternative<MCSymbol *>(CU.value()))
      Asm->emitDwarfSymbolReference(std::get<MCSymbol *>(CU.value()));
    else
      Asm->emitDwarfLengthOrOffset(std::get<uint64_t>(CU.value()));
  }
}

void Dwarf5AccelTableWriter::emitTUList() const {
  for (const auto &TU : enumerate(TypeUnits)) {
    Asm->OutStreamer->AddComment("Type unit " + Twine(TU.index()));
    if (std::holds_alternative<MCSymbol *>(TU.value()))
      Asm->emitDwarfSymbolReference(std::get<MCSymbol *>(TU.value()));
    else if (IsSplitDwarf)
      Asm->emitInt64(std::get<uint64_t>(TU.value()));
    else
      Asm->emitDwarfLengthOrOffset(std::get<uint64_t>(TU.value()));
  }
}

void Dwarf5AccelTableWriter::emitBuckets() const {
  uint32_t Index = 1;
  for (const auto &Bucket : enumerate(Contents.getBuckets())) {
    Asm->OutStreamer->AddComment("Bucket " + Twine(Bucket.index()));
    Asm->emitInt32(Bucket.value().empty() ? 0 : Index);
    Index += Bucket.value().size();
  }
}

void Dwarf5AccelTableWriter::emitStringOffsets() const {
  for (const auto &Bucket : enumerate(Contents.getBuckets())) {
    for (auto *Hash : Bucket.value()) {
      DwarfStringPoolEntryRef String = Hash->Name;
      Asm->OutStreamer->AddComment("String in Bucket " + Twine(Bucket.index()) +
                                   ": " + String.getString());
      Asm->emitDwarfStringOffset(String);
    }
  }
}

void Dwarf5AccelTableWriter::emitAbbrevs() const {
  Asm->OutStreamer->emitLabel(AbbrevStart);
  for (const DebugNamesAbbrev *Abbrev : AbbreviationsVector) {
    Asm->OutStreamer->AddComment("Abbrev code");
    Asm->emitULEB128(Abbrev->getNumber());
    Asm->OutStreamer->AddComment(dwarf::TagString(Abbrev->getDieTag()));
    Asm->emitULEB128(Abbrev->getDieTag());
    for (const DebugNamesAbbrev::AttributeEncoding &AttrEnc :
         Abbrev->getAttributes()) {
      Asm->emitULEB128(AttrEnc.Index, dwarf::IndexString(AttrEnc.Index).data());
      Asm->emitULEB128(AttrEnc.Form,
                       dwarf::FormEncodingString(AttrEnc.Form).data());
    }
    Asm->emitULEB128(0, "End of abbrev");
    Asm->emitULEB128(0, "End of abbrev");
  }
  Asm->emitULEB128(0, "End of abbrev list");
  Asm->OutStreamer->emitLabel(AbbrevEnd);
}

void Dwarf5AccelTableWriter::emitEntry(
    const DWARF5AccelTableData &Entry,
    const DenseMap<OffsetAndUnitID, MCSymbol *> &DIEOffsetToAccelEntryLabel,
    DenseSet<MCSymbol *> &EmittedAccelEntrySymbols) {
  unsigned AbbrevIndex = Entry.getAbbrevNumber() - 1;
  assert(AbbrevIndex < AbbreviationsVector.size() &&
         "Entry abbrev index is outside of abbreviations vector range.");
  DebugNamesAbbrev *Abbrev = AbbreviationsVector[AbbrevIndex];
  std::optional<DWARF5AccelTable::UnitIndexAndEncoding> EntryRet =
      getIndexForEntry(Entry);
  std::optional<OffsetAndUnitID> MaybeParentOffset =
      Entry.getParentDieOffsetAndUnitID();
  auto EntrySymbolIt =
      DIEOffsetToAccelEntryLabel.find(Entry.getDieOffsetAndUnitID());
  assert(EntrySymbolIt != DIEOffsetToAccelEntryLabel.end());
  MCSymbol *EntrySymbol = EntrySymbolIt->getSecond();

  // Emit the label for this Entry, so that IDX_parents may refer to it.
  // Note: a DIE may have multiple accelerator Entries; this check avoids
  // creating/emitting multiple labels for the same DIE.
  if (EmittedAccelEntrySymbols.insert(EntrySymbol).second)
    Asm->OutStreamer->emitLabel(EntrySymbol);

  Asm->emitULEB128(Entry.getAbbrevNumber(), "Abbreviation code");

  for (const DebugNamesAbbrev::AttributeEncoding &AttrEnc :
       Abbrev->getAttributes()) {
    Asm->OutStreamer->AddComment(dwarf::IndexString(AttrEnc.Index));
    switch (AttrEnc.Index) {
    case dwarf::DW_IDX_compile_unit:
    case dwarf::DW_IDX_type_unit: {
      DIEInteger ID(EntryRet->Index);
      ID.emitValue(Asm, AttrEnc.Form);
      break;
    }
    case dwarf::DW_IDX_die_offset:
      assert(AttrEnc.Form == dwarf::DW_FORM_ref4);
      Asm->emitInt32(Entry.getDieOffset());
      break;
    case dwarf::DW_IDX_parent: {
      if (AttrEnc.Form == dwarf::Form::DW_FORM_flag_present)
        break;
      auto ParentSymbolIt = DIEOffsetToAccelEntryLabel.find(*MaybeParentOffset);
      assert(ParentSymbolIt != DIEOffsetToAccelEntryLabel.end());
      Asm->emitLabelDifference(ParentSymbolIt->getSecond(), EntryPool, 4);
      break;
    }
    default:
      llvm_unreachable("Unexpected index attribute!");
    }
  }
}

void Dwarf5AccelTableWriter::emitData() {
  DenseMap<OffsetAndUnitID, MCSymbol *> DIEOffsetToAccelEntryLabel;

  for (OffsetAndUnitID Offset : IndexedOffsets)
    DIEOffsetToAccelEntryLabel.insert({Offset, Asm->createTempSymbol("")});

  Asm->OutStreamer->emitLabel(EntryPool);
  DenseSet<MCSymbol *> EmittedAccelEntrySymbols;
  for (auto &Bucket : Contents.getBuckets()) {
    for (auto *Hash : Bucket) {
      // Remember to emit the label for our offset.
      Asm->OutStreamer->emitLabel(Hash->Sym);
      for (const auto *Value : Hash->getValues<DWARF5AccelTableData *>())
        emitEntry(*Value, DIEOffsetToAccelEntryLabel, EmittedAccelEntrySymbols);
      Asm->OutStreamer->AddComment("End of list: " + Hash->Name.getString());
      Asm->emitInt8(0);
    }
  }
}

Dwarf5AccelTableWriter::Dwarf5AccelTableWriter(
    AsmPrinter *Asm, const AccelTableBase &Contents,
    ArrayRef<std::variant<MCSymbol *, uint64_t>> CompUnits,
    ArrayRef<std::variant<MCSymbol *, uint64_t>> TypeUnits,
    llvm::function_ref<std::optional<DWARF5AccelTable::UnitIndexAndEncoding>(
        const DWARF5AccelTableData &)>
        getIndexForEntry,
    bool IsSplitDwarf)
    : AccelTableWriter(Asm, Contents, false),
      Header(CompUnits.size(), IsSplitDwarf ? 0 : TypeUnits.size(),
             IsSplitDwarf ? TypeUnits.size() : 0, Contents.getBucketCount(),
             Contents.getUniqueNameCount()),
      CompUnits(CompUnits), TypeUnits(TypeUnits),
      getIndexForEntry(std::move(getIndexForEntry)),
      IsSplitDwarf(IsSplitDwarf) {

  for (auto &Bucket : Contents.getBuckets())
    for (auto *Hash : Bucket)
      for (auto *Value : Hash->getValues<DWARF5AccelTableData *>())
        IndexedOffsets.insert(Value->getDieOffsetAndUnitID());

  populateAbbrevsMap();
}

void Dwarf5AccelTableWriter::emit() {
  Header.emit(*this);
  emitCUList();
  emitTUList();
  emitBuckets();
  emitHashes();
  emitStringOffsets();
  emitOffsets(EntryPool);
  emitAbbrevs();
  emitData();
  Asm->OutStreamer->emitValueToAlignment(Align(4), 0);
  Asm->OutStreamer->emitLabel(ContributionEnd);
}

void llvm::emitAppleAccelTableImpl(AsmPrinter *Asm, AccelTableBase &Contents,
                                   StringRef Prefix, const MCSymbol *SecBegin,
                                   ArrayRef<AppleAccelTableData::Atom> Atoms) {
  Contents.finalize(Asm, Prefix);
  AppleAccelTableWriter(Asm, Contents, Atoms, SecBegin).emit();
}

void llvm::emitDWARF5AccelTable(
    AsmPrinter *Asm, DWARF5AccelTable &Contents, const DwarfDebug &DD,
    ArrayRef<std::unique_ptr<DwarfCompileUnit>> CUs) {
  TUVectorTy TUSymbols = Contents.getTypeUnitsSymbols();
  std::vector<std::variant<MCSymbol *, uint64_t>> CompUnits;
  std::vector<std::variant<MCSymbol *, uint64_t>> TypeUnits;
  SmallVector<unsigned, 1> CUIndex(CUs.size());
  DenseMap<unsigned, unsigned> TUIndex(TUSymbols.size());
  int CUCount = 0;
  int TUCount = 0;
  for (const auto &CU : enumerate(CUs)) {
    switch (CU.value()->getCUNode()->getNameTableKind()) {
    case DICompileUnit::DebugNameTableKind::Default:
    case DICompileUnit::DebugNameTableKind::Apple:
      break;
    default:
      continue;
    }
    CUIndex[CU.index()] = CUCount++;
    assert(CU.index() == CU.value()->getUniqueID());
    const DwarfCompileUnit *MainCU =
        DD.useSplitDwarf() ? CU.value()->getSkeleton() : CU.value().get();
    CompUnits.push_back(MainCU->getLabelBegin());
  }

  for (const auto &TU : TUSymbols) {
    TUIndex[TU.UniqueID] = TUCount++;
    if (DD.useSplitDwarf())
      TypeUnits.push_back(std::get<uint64_t>(TU.LabelOrSignature));
    else
      TypeUnits.push_back(std::get<MCSymbol *>(TU.LabelOrSignature));
  }

  if (CompUnits.empty())
    return;

  Asm->OutStreamer->switchSection(
      Asm->getObjFileLowering().getDwarfDebugNamesSection());

  Contents.finalize(Asm, "names");
  dwarf::Form CUIndexForm =
      DIEInteger::BestForm(/*IsSigned*/ false, CompUnits.size() - 1);
  dwarf::Form TUIndexForm =
      DIEInteger::BestForm(/*IsSigned*/ false, TypeUnits.size() - 1);
  Dwarf5AccelTableWriter(
      Asm, Contents, CompUnits, TypeUnits,
      [&](const DWARF5AccelTableData &Entry)
          -> std::optional<DWARF5AccelTable::UnitIndexAndEncoding> {
        if (Entry.isTU())
          return {{TUIndex[Entry.getUnitID()],
                   {dwarf::DW_IDX_type_unit, TUIndexForm}}};
        if (CUIndex.size() > 1)
          return {{CUIndex[Entry.getUnitID()],
                   {dwarf::DW_IDX_compile_unit, CUIndexForm}}};
        return std::nullopt;
      },
      DD.useSplitDwarf())
      .emit();
}

void DWARF5AccelTable::addTypeUnitSymbol(DwarfTypeUnit &U) {
  TUSymbolsOrHashes.push_back({U.getLabelBegin(), U.getUniqueID()});
}

void DWARF5AccelTable::addTypeUnitSignature(DwarfTypeUnit &U) {
  TUSymbolsOrHashes.push_back({U.getTypeSignature(), U.getUniqueID()});
}

void llvm::emitDWARF5AccelTable(
    AsmPrinter *Asm, DWARF5AccelTable &Contents,
    ArrayRef<std::variant<MCSymbol *, uint64_t>> CUs,
    llvm::function_ref<std::optional<DWARF5AccelTable::UnitIndexAndEncoding>(
        const DWARF5AccelTableData &)>
        getIndexForEntry) {
  std::vector<std::variant<MCSymbol *, uint64_t>> TypeUnits;
  Contents.finalize(Asm, "names");
  Dwarf5AccelTableWriter(Asm, Contents, CUs, TypeUnits, getIndexForEntry, false)
      .emit();
}

void AppleAccelTableOffsetData::emit(AsmPrinter *Asm) const {
  assert(Die.getDebugSectionOffset() <= UINT32_MAX &&
         "The section offset exceeds the limit.");
  Asm->emitInt32(Die.getDebugSectionOffset());
}

void AppleAccelTableTypeData::emit(AsmPrinter *Asm) const {
  assert(Die.getDebugSectionOffset() <= UINT32_MAX &&
         "The section offset exceeds the limit.");
  Asm->emitInt32(Die.getDebugSectionOffset());
  Asm->emitInt16(Die.getTag());
  Asm->emitInt8(0);
}

void AppleAccelTableStaticOffsetData::emit(AsmPrinter *Asm) const {
  Asm->emitInt32(Offset);
}

void AppleAccelTableStaticTypeData::emit(AsmPrinter *Asm) const {
  Asm->emitInt32(Offset);
  Asm->emitInt16(Tag);
  Asm->emitInt8(ObjCClassIsImplementation ? dwarf::DW_FLAG_type_implementation
                                          : 0);
  Asm->emitInt32(QualifiedNameHash);
}

constexpr AppleAccelTableData::Atom AppleAccelTableTypeData::Atoms[];
constexpr AppleAccelTableData::Atom AppleAccelTableOffsetData::Atoms[];
constexpr AppleAccelTableData::Atom AppleAccelTableStaticOffsetData::Atoms[];
constexpr AppleAccelTableData::Atom AppleAccelTableStaticTypeData::Atoms[];

#ifndef NDEBUG
void AppleAccelTableWriter::Header::print(raw_ostream &OS) const {
  OS << "Magic: " << format("0x%x", Magic) << "\n"
     << "Version: " << Version << "\n"
     << "Hash Function: " << HashFunction << "\n"
     << "Bucket Count: " << BucketCount << "\n"
     << "Header Data Length: " << HeaderDataLength << "\n";
}

void AppleAccelTableData::Atom::print(raw_ostream &OS) const {
  OS << "Type: " << dwarf::AtomTypeString(Type) << "\n"
     << "Form: " << dwarf::FormEncodingString(Form) << "\n";
}

void AppleAccelTableWriter::HeaderData::print(raw_ostream &OS) const {
  OS << "DIE Offset Base: " << DieOffsetBase << "\n";
  for (auto Atom : Atoms)
    Atom.print(OS);
}

void AppleAccelTableWriter::print(raw_ostream &OS) const {
  Header.print(OS);
  HeaderData.print(OS);
  Contents.print(OS);
  SecBegin->print(OS, nullptr);
}

void AccelTableBase::HashData::print(raw_ostream &OS) const {
  OS << "Name: " << Name.getString() << "\n";
  OS << "  Hash Value: " << format("0x%x", HashValue) << "\n";
  OS << "  Symbol: ";
  if (Sym)
    OS << *Sym;
  else
    OS << "<none>";
  OS << "\n";
  for (auto *Value : Values)
    Value->print(OS);
}

void AccelTableBase::print(raw_ostream &OS) const {
  // Print Content.
  OS << "Entries: \n";
  for (const auto &[Name, Data] : Entries) {
    OS << "Name: " << Name << "\n";
    for (auto *V : Data.Values)
      V->print(OS);
  }

  OS << "Buckets and Hashes: \n";
  for (const auto &Bucket : Buckets)
    for (const auto &Hash : Bucket)
      Hash->print(OS);

  OS << "Data: \n";
  for (const auto &E : Entries)
    E.second.print(OS);
}

void DWARF5AccelTableData::print(raw_ostream &OS) const {
  OS << "  Offset: " << getDieOffset() << "\n";
  OS << "  Tag: " << dwarf::TagString(getDieTag()) << "\n";
}

void AppleAccelTableOffsetData::print(raw_ostream &OS) const {
  OS << "  Offset: " << Die.getOffset() << "\n";
}

void AppleAccelTableTypeData::print(raw_ostream &OS) const {
  OS << "  Offset: " << Die.getOffset() << "\n";
  OS << "  Tag: " << dwarf::TagString(Die.getTag()) << "\n";
}

void AppleAccelTableStaticOffsetData::print(raw_ostream &OS) const {
  OS << "  Static Offset: " << Offset << "\n";
}

void AppleAccelTableStaticTypeData::print(raw_ostream &OS) const {
  OS << "  Static Offset: " << Offset << "\n";
  OS << "  QualifiedNameHash: " << format("%x\n", QualifiedNameHash) << "\n";
  OS << "  Tag: " << dwarf::TagString(Tag) << "\n";
  OS << "  ObjCClassIsImplementation: "
     << (ObjCClassIsImplementation ? "true" : "false");
  OS << "\n";
}
#endif
