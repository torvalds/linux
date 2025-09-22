//===- DWARFAcceleratorTable.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFACCELERATORTABLE_H
#define LLVM_DEBUGINFO_DWARF_DWARFACCELERATORTABLE_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include <cstdint>
#include <utility>

namespace llvm {

class raw_ostream;
class ScopedPrinter;

/// The accelerator tables are designed to allow efficient random access
/// (using a symbol name as a key) into debug info by providing an index of the
/// debug info DIEs. This class implements the common functionality of Apple and
/// DWARF 5 accelerator tables.
/// TODO: Generalize the rest of the AppleAcceleratorTable interface and move it
/// to this class.
class DWARFAcceleratorTable {
protected:
  DWARFDataExtractor AccelSection;
  DataExtractor StringSection;

public:
  /// An abstract class representing a single entry in the accelerator tables.
  class Entry {
  protected:
    SmallVector<DWARFFormValue, 3> Values;

    Entry() = default;

    // Make these protected so only (final) subclasses can be copied around.
    Entry(const Entry &) = default;
    Entry(Entry &&) = default;
    Entry &operator=(const Entry &) = default;
    Entry &operator=(Entry &&) = default;
    ~Entry() = default;


  public:
    /// Returns the Offset of the Compilation Unit associated with this
    /// Accelerator Entry or std::nullopt if the Compilation Unit offset is not
    /// recorded in this Accelerator Entry.
    virtual std::optional<uint64_t> getCUOffset() const = 0;

    /// Returns the Offset of the Type Unit associated with this
    /// Accelerator Entry or std::nullopt if the Type Unit offset is not
    /// recorded in this Accelerator Entry.
    virtual std::optional<uint64_t> getLocalTUOffset() const {
      // Default return for accelerator tables that don't support type units.
      return std::nullopt;
    }

    /// Returns the type signature of the Type Unit associated with this
    /// Accelerator Entry or std::nullopt if the Type Unit offset is not
    /// recorded in this Accelerator Entry.
    virtual std::optional<uint64_t> getForeignTUTypeSignature() const {
      // Default return for accelerator tables that don't support type units.
      return std::nullopt;
    }

    /// Returns the Tag of the Debug Info Entry associated with this
    /// Accelerator Entry or std::nullopt if the Tag is not recorded in this
    /// Accelerator Entry.
    virtual std::optional<dwarf::Tag> getTag() const = 0;

    /// Returns the raw values of fields in the Accelerator Entry. In general,
    /// these can only be interpreted with the help of the metadata in the
    /// owning Accelerator Table.
    ArrayRef<DWARFFormValue> getValues() const { return Values; }
  };

  DWARFAcceleratorTable(const DWARFDataExtractor &AccelSection,
                        DataExtractor StringSection)
      : AccelSection(AccelSection), StringSection(StringSection) {}
  virtual ~DWARFAcceleratorTable();

  virtual Error extract() = 0;
  virtual void dump(raw_ostream &OS) const = 0;

  DWARFAcceleratorTable(const DWARFAcceleratorTable &) = delete;
  void operator=(const DWARFAcceleratorTable &) = delete;
};

/// This implements the Apple accelerator table format, a precursor of the
/// DWARF 5 accelerator table format.
class AppleAcceleratorTable : public DWARFAcceleratorTable {
  struct Header {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HashFunction;
    uint32_t BucketCount;
    uint32_t HashCount;
    uint32_t HeaderDataLength;

    void dump(ScopedPrinter &W) const;
  };

  struct HeaderData {
    using AtomType = uint16_t;
    using Form = dwarf::Form;

    uint64_t DIEOffsetBase;
    SmallVector<std::pair<AtomType, Form>, 3> Atoms;

    std::optional<uint64_t>
    extractOffset(std::optional<DWARFFormValue> Value) const;
  };

  Header Hdr;
  HeaderData HdrData;
  dwarf::FormParams FormParams;
  uint32_t HashDataEntryLength;
  bool IsValid = false;

  /// Returns true if we should continue scanning for entries or false if we've
  /// reached the last (sentinel) entry of encountered a parsing error.
  bool dumpName(ScopedPrinter &W, SmallVectorImpl<DWARFFormValue> &AtomForms,
                uint64_t *DataOffset) const;

  /// Reads an uint32_t from the accelerator table at Offset, which is
  /// incremented by the number of bytes read.
  std::optional<uint32_t> readU32FromAccel(uint64_t &Offset,
                                           bool UseRelocation = false) const;

  /// Reads a StringRef from the string table at Offset.
  std::optional<StringRef>
  readStringFromStrSection(uint64_t StringSectionOffset) const;

  /// Return the offset into the section where the Buckets begin.
  uint64_t getBucketBase() const { return sizeof(Hdr) + Hdr.HeaderDataLength; }

  /// Return the offset into the section where the I-th bucket is.
  uint64_t getIthBucketBase(uint32_t I) const {
    return getBucketBase() + I * 4;
  }

  /// Return the offset into the section where the hash list begins.
  uint64_t getHashBase() const { return getBucketBase() + getNumBuckets() * 4; }

  /// Return the offset into the section where the I-th hash is.
  uint64_t getIthHashBase(uint32_t I) const { return getHashBase() + I * 4; }

  /// Return the offset into the section where the offset list begins.
  uint64_t getOffsetBase() const { return getHashBase() + getNumHashes() * 4; }

  /// Return the offset into the section where the table entries begin.
  uint64_t getEntriesBase() const {
    return getOffsetBase() + getNumHashes() * 4;
  }

  /// Return the offset into the section where the I-th offset is.
  uint64_t getIthOffsetBase(uint32_t I) const {
    return getOffsetBase() + I * 4;
  }

  /// Returns the index of the bucket where a hypothetical Hash would be.
  uint32_t hashToBucketIdx(uint32_t Hash) const {
    return Hash % getNumBuckets();
  }

  /// Returns true iff a hypothetical Hash would be assigned to the BucketIdx-th
  /// bucket.
  bool wouldHashBeInBucket(uint32_t Hash, uint32_t BucketIdx) const {
    return hashToBucketIdx(Hash) == BucketIdx;
  }

  /// Reads the contents of the I-th bucket, that is, the index in the hash list
  /// where the hashes corresponding to this bucket begin.
  std::optional<uint32_t> readIthBucket(uint32_t I) const {
    uint64_t Offset = getIthBucketBase(I);
    return readU32FromAccel(Offset);
  }

  /// Reads the I-th hash in the hash list.
  std::optional<uint32_t> readIthHash(uint32_t I) const {
    uint64_t Offset = getIthHashBase(I);
    return readU32FromAccel(Offset);
  }

  /// Reads the I-th offset in the offset list.
  std::optional<uint32_t> readIthOffset(uint32_t I) const {
    uint64_t Offset = getIthOffsetBase(I);
    return readU32FromAccel(Offset);
  }

  /// Reads a string offset from the accelerator table at Offset, which is
  /// incremented by the number of bytes read.
  std::optional<uint32_t> readStringOffsetAt(uint64_t &Offset) const {
    return readU32FromAccel(Offset, /*UseRelocation*/ true);
  }

  /// Scans through all Hashes in the BucketIdx-th bucket, attempting to find
  /// HashToFind. If it is found, its index in the list of hashes is returned.
  std::optional<uint32_t> idxOfHashInBucket(uint32_t HashToFind,
                                            uint32_t BucketIdx) const;

public:
  /// Apple-specific implementation of an Accelerator Entry.
  class Entry final : public DWARFAcceleratorTable::Entry {
    const AppleAcceleratorTable &Table;

    Entry(const AppleAcceleratorTable &Table);
    void extract(uint64_t *Offset);

  public:
    std::optional<uint64_t> getCUOffset() const override;

    /// Returns the Section Offset of the Debug Info Entry associated with this
    /// Accelerator Entry or std::nullopt if the DIE offset is not recorded in
    /// this Accelerator Entry. The returned offset is relative to the start of
    /// the Section containing the DIE.
    std::optional<uint64_t> getDIESectionOffset() const;

    std::optional<dwarf::Tag> getTag() const override;

    /// Returns the value of the Atom in this Accelerator Entry, if the Entry
    /// contains such Atom.
    std::optional<DWARFFormValue> lookup(HeaderData::AtomType Atom) const;

    friend class AppleAcceleratorTable;
    friend class ValueIterator;
  };

  /// An iterator for Entries all having the same string as key.
  class SameNameIterator
      : public iterator_facade_base<SameNameIterator, std::forward_iterator_tag,
                                    Entry> {
    Entry Current;
    uint64_t Offset = 0;

  public:
    /// Construct a new iterator for the entries at \p DataOffset.
    SameNameIterator(const AppleAcceleratorTable &AccelTable,
                     uint64_t DataOffset);

    const Entry &operator*() {
      uint64_t OffsetCopy = Offset;
      Current.extract(&OffsetCopy);
      return Current;
    }
    SameNameIterator &operator++() {
      Offset += Current.Table.getHashDataEntryLength();
      return *this;
    }
    friend bool operator==(const SameNameIterator &A,
                           const SameNameIterator &B) {
      return A.Offset == B.Offset;
    }
  };

  struct EntryWithName {
    EntryWithName(const AppleAcceleratorTable &Table)
        : BaseEntry(Table), StrOffset(0) {}

    std::optional<StringRef> readName() const {
      return BaseEntry.Table.readStringFromStrSection(StrOffset);
    }

    Entry BaseEntry;
    uint32_t StrOffset;
  };

  /// An iterator for all entries in the table.
  class Iterator
      : public iterator_facade_base<Iterator, std::forward_iterator_tag,
                                    EntryWithName> {
    constexpr static auto EndMarker = std::numeric_limits<uint64_t>::max();

    EntryWithName Current;
    uint64_t Offset = EndMarker;
    uint32_t NumEntriesToCome = 0;

    void setToEnd() { Offset = EndMarker; }
    bool isEnd() const { return Offset == EndMarker; }
    const AppleAcceleratorTable &getTable() const {
      return Current.BaseEntry.Table;
    }

    /// Reads the next Entry in the table, populating `Current`.
    /// If not possible (e.g. end of the section), becomes the end iterator.
    void prepareNextEntryOrEnd();

    /// Reads the next string pointer and the entry count for that string,
    /// populating `NumEntriesToCome`.
    /// If not possible (e.g. end of the section), becomes the end iterator.
    /// Assumes `Offset` points to a string reference.
    void prepareNextStringOrEnd();

  public:
    Iterator(const AppleAcceleratorTable &Table, bool SetEnd = false);

    Iterator &operator++() {
      prepareNextEntryOrEnd();
      return *this;
    }
    bool operator==(const Iterator &It) const { return Offset == It.Offset; }
    const EntryWithName &operator*() const {
      assert(!isEnd() && "dereferencing end iterator");
      return Current;
    }
  };

  AppleAcceleratorTable(const DWARFDataExtractor &AccelSection,
                        DataExtractor StringSection)
      : DWARFAcceleratorTable(AccelSection, StringSection) {}

  Error extract() override;
  uint32_t getNumBuckets() const;
  uint32_t getNumHashes() const;
  uint32_t getSizeHdr() const;
  uint32_t getHeaderDataLength() const;

  /// Returns the size of one HashData entry.
  uint32_t getHashDataEntryLength() const { return HashDataEntryLength; }

  /// Return the Atom description, which can be used to interpret the raw values
  /// of the Accelerator Entries in this table.
  ArrayRef<std::pair<HeaderData::AtomType, HeaderData::Form>> getAtomsDesc();

  /// Returns true iff `AtomTy` is one of the atoms available in Entries of this
  /// table.
  bool containsAtomType(HeaderData::AtomType AtomTy) const {
    return is_contained(make_first_range(HdrData.Atoms), AtomTy);
  }

  bool validateForms();

  /// Return information related to the DWARF DIE we're looking for when
  /// performing a lookup by name.
  ///
  /// \param HashDataOffset an offset into the hash data table
  /// \returns <DieOffset, DieTag>
  /// DieOffset is the offset into the .debug_info section for the DIE
  /// related to the input hash data offset.
  /// DieTag is the tag of the DIE
  std::pair<uint64_t, dwarf::Tag> readAtoms(uint64_t *HashDataOffset);
  void dump(raw_ostream &OS) const override;

  /// Look up all entries in the accelerator table matching \c Key.
  iterator_range<SameNameIterator> equal_range(StringRef Key) const;

  /// Lookup all entries in the accelerator table.
  auto entries() const {
    return make_range(Iterator(*this), Iterator(*this, /*SetEnd*/ true));
  }
};

/// .debug_names section consists of one or more units. Each unit starts with a
/// header, which is followed by a list of compilation units, local and foreign
/// type units.
///
/// These may be followed by an (optional) hash lookup table, which consists of
/// an array of buckets and hashes similar to the apple tables above. The only
/// difference is that the hashes array is 1-based, and consequently an empty
/// bucket is denoted by 0 and not UINT32_MAX.
///
/// Next is the name table, which consists of an array of names and array of
/// entry offsets. This is different from the apple tables, which store names
/// next to the actual entries.
///
/// The structure of the entries is described by an abbreviations table, which
/// comes after the name table. Unlike the apple tables, which have a uniform
/// entry structure described in the header, each .debug_names entry may have
/// different index attributes (DW_IDX_???) attached to it.
///
/// The last segment consists of a list of entries, which is a 0-terminated list
/// referenced by the name table and interpreted with the help of the
/// abbreviation table.
class DWARFDebugNames : public DWARFAcceleratorTable {
public:
  class NameIndex;
  class NameIterator;
  class ValueIterator;

  /// DWARF v5 Name Index header.
  struct Header {
    uint64_t UnitLength;
    dwarf::DwarfFormat Format;
    uint16_t Version;
    uint32_t CompUnitCount;
    uint32_t LocalTypeUnitCount;
    uint32_t ForeignTypeUnitCount;
    uint32_t BucketCount;
    uint32_t NameCount;
    uint32_t AbbrevTableSize;
    uint32_t AugmentationStringSize;
    SmallString<8> AugmentationString;

    Error extract(const DWARFDataExtractor &AS, uint64_t *Offset);
    void dump(ScopedPrinter &W) const;
  };

  /// Index attribute and its encoding.
  struct AttributeEncoding {
    dwarf::Index Index;
    dwarf::Form Form;

    constexpr AttributeEncoding(dwarf::Index Index, dwarf::Form Form)
        : Index(Index), Form(Form) {}

    friend bool operator==(const AttributeEncoding &LHS,
                           const AttributeEncoding &RHS) {
      return LHS.Index == RHS.Index && LHS.Form == RHS.Form;
    }
  };

  /// Abbreviation describing the encoding of Name Index entries.
  struct Abbrev {
    uint64_t AbbrevOffset; /// < Abbreviation offset in the .debug_names section
    uint32_t Code;         ///< Abbreviation code
    dwarf::Tag Tag; ///< Dwarf Tag of the described entity.
    std::vector<AttributeEncoding> Attributes; ///< List of index attributes.

    Abbrev(uint32_t Code, dwarf::Tag Tag, uint64_t AbbrevOffset,
           std::vector<AttributeEncoding> Attributes)
        : AbbrevOffset(AbbrevOffset), Code(Code), Tag(Tag),
          Attributes(std::move(Attributes)) {}

    void dump(ScopedPrinter &W) const;
  };

  /// DWARF v5-specific implementation of an Accelerator Entry.
  class Entry final : public DWARFAcceleratorTable::Entry {
    const NameIndex *NameIdx;
    const Abbrev *Abbr;

    Entry(const NameIndex &NameIdx, const Abbrev &Abbr);

  public:
    const NameIndex *getNameIndex() const { return NameIdx; }
    std::optional<uint64_t> getCUOffset() const override;
    std::optional<uint64_t> getLocalTUOffset() const override;
    std::optional<uint64_t> getForeignTUTypeSignature() const override;
    std::optional<dwarf::Tag> getTag() const override { return tag(); }

    // Special function that will return the related CU offset needed type 
    // units. This gets used to find the .dwo file that originated the entries
    // for a given type unit.
    std::optional<uint64_t> getRelatedCUOffset() const;

    /// Returns the Index into the Compilation Unit list of the owning Name
    /// Index or std::nullopt if this Accelerator Entry does not have an
    /// associated Compilation Unit. It is up to the user to verify that the
    /// returned Index is valid in the owning NameIndex (or use getCUOffset(),
    /// which will handle that check itself). Note that entries in NameIndexes
    /// which index just a single Compilation Unit are implicitly associated
    /// with that unit, so this function will return 0 even without an explicit
    /// DW_IDX_compile_unit attribute, unless there is a DW_IDX_type_unit
    /// attribute.
    std::optional<uint64_t> getCUIndex() const;

    /// Similar functionality to getCUIndex() but without the DW_IDX_type_unit
    /// restriction. This allows us to get the associated a compilation unit
    /// index for an entry that is a type unit.
    std::optional<uint64_t> getRelatedCUIndex() const;

    /// Returns the Index into the Local Type Unit list of the owning Name
    /// Index or std::nullopt if this Accelerator Entry does not have an
    /// associated Type Unit. It is up to the user to verify that the
    /// returned Index is valid in the owning NameIndex (or use
    /// getLocalTUOffset(), which will handle that check itself).
    std::optional<uint64_t> getLocalTUIndex() const;

    /// .debug_names-specific getter, which always succeeds (DWARF v5 index
    /// entries always have a tag).
    dwarf::Tag tag() const { return Abbr->Tag; }

    /// Returns the Offset of the DIE within the containing CU or TU.
    std::optional<uint64_t> getDIEUnitOffset() const;

    /// Returns true if this Entry has information about its parent DIE (i.e. if
    /// it has an IDX_parent attribute)
    bool hasParentInformation() const;

    /// Returns the Entry corresponding to the parent of the DIE represented by
    /// `this` Entry. If the parent is not in the table, nullopt is returned.
    /// Precondition: hasParentInformation() == true.
    /// An error is returned for ill-formed tables.
    Expected<std::optional<DWARFDebugNames::Entry>> getParentDIEEntry() const;

    /// Return the Abbreviation that can be used to interpret the raw values of
    /// this Accelerator Entry.
    const Abbrev &getAbbrev() const { return *Abbr; }

    /// Returns the value of the Index Attribute in this Accelerator Entry, if
    /// the Entry contains such Attribute.
    std::optional<DWARFFormValue> lookup(dwarf::Index Index) const;

    void dump(ScopedPrinter &W) const;
    void dumpParentIdx(ScopedPrinter &W, const DWARFFormValue &FormValue) const;

    friend class NameIndex;
    friend class ValueIterator;
  };

  /// Error returned by NameIndex::getEntry to report it has reached the end of
  /// the entry list.
  class SentinelError : public ErrorInfo<SentinelError> {
  public:
    static char ID;

    void log(raw_ostream &OS) const override { OS << "Sentinel"; }
    std::error_code convertToErrorCode() const override;
  };

private:
  /// DenseMapInfo for struct Abbrev.
  struct AbbrevMapInfo {
    static Abbrev getEmptyKey();
    static Abbrev getTombstoneKey();
    static unsigned getHashValue(uint32_t Code) {
      return DenseMapInfo<uint32_t>::getHashValue(Code);
    }
    static unsigned getHashValue(const Abbrev &Abbr) {
      return getHashValue(Abbr.Code);
    }
    static bool isEqual(uint32_t LHS, const Abbrev &RHS) {
      return LHS == RHS.Code;
    }
    static bool isEqual(const Abbrev &LHS, const Abbrev &RHS) {
      return LHS.Code == RHS.Code;
    }
  };

public:
  /// A single entry in the Name Table (DWARF v5 sect. 6.1.1.4.6) of the Name
  /// Index.
  class NameTableEntry {
    DataExtractor StrData;

    uint32_t Index;
    uint64_t StringOffset;
    uint64_t EntryOffset;

  public:
    NameTableEntry(const DataExtractor &StrData, uint32_t Index,
                   uint64_t StringOffset, uint64_t EntryOffset)
        : StrData(StrData), Index(Index), StringOffset(StringOffset),
          EntryOffset(EntryOffset) {}

    /// Return the index of this name in the parent Name Index.
    uint32_t getIndex() const { return Index; }

    /// Returns the offset of the name of the described entities.
    uint64_t getStringOffset() const { return StringOffset; }

    /// Return the string referenced by this name table entry or nullptr if the
    /// string offset is not valid.
    const char *getString() const {
      uint64_t Off = StringOffset;
      return StrData.getCStr(&Off);
    }

    /// Compares the name of this entry against Target, returning true if they
    /// are equal. This is more efficient in hot code paths that do not need the
    /// length of the name.
    bool sameNameAs(StringRef Target) const {
      // Note: this is not the name, but the rest of debug_str starting from
      // name. This handles corrupt data (non-null terminated) without
      // overrunning the buffer.
      StringRef Data = StrData.getData().substr(StringOffset);
      size_t TargetSize = Target.size();
      return Data.size() > TargetSize && !Data[TargetSize] &&
             strncmp(Data.data(), Target.data(), TargetSize) == 0;
    }

    /// Returns the offset of the first Entry in the list.
    uint64_t getEntryOffset() const { return EntryOffset; }
  };

  /// Offsets for the start of various important tables from the start of the
  /// section.
  struct DWARFDebugNamesOffsets {
    uint64_t CUsBase;
    uint64_t BucketsBase;
    uint64_t HashesBase;
    uint64_t StringOffsetsBase;
    uint64_t EntryOffsetsBase;
    uint64_t EntriesBase;
  };

  /// Represents a single accelerator table within the DWARF v5 .debug_names
  /// section.
  class NameIndex {
    DenseSet<Abbrev, AbbrevMapInfo> Abbrevs;
    struct Header Hdr;
    const DWARFDebugNames &Section;

    // Base of the whole unit and of various important tables, as offsets from
    // the start of the section.
    uint64_t Base;
    DWARFDebugNamesOffsets Offsets;

    void dumpCUs(ScopedPrinter &W) const;
    void dumpLocalTUs(ScopedPrinter &W) const;
    void dumpForeignTUs(ScopedPrinter &W) const;
    void dumpAbbreviations(ScopedPrinter &W) const;
    bool dumpEntry(ScopedPrinter &W, uint64_t *Offset) const;
    void dumpName(ScopedPrinter &W, const NameTableEntry &NTE,
                  std::optional<uint32_t> Hash) const;
    void dumpBucket(ScopedPrinter &W, uint32_t Bucket) const;

    Expected<AttributeEncoding> extractAttributeEncoding(uint64_t *Offset);

    Expected<std::vector<AttributeEncoding>>
    extractAttributeEncodings(uint64_t *Offset);

    Expected<Abbrev> extractAbbrev(uint64_t *Offset);

  public:
    NameIndex(const DWARFDebugNames &Section, uint64_t Base)
        : Section(Section), Base(Base) {}

    /// Returns Hdr field
    Header getHeader() const { return Hdr; }

    /// Returns Offsets field
    DWARFDebugNamesOffsets getOffsets() const { return Offsets; }

    /// Reads offset of compilation unit CU. CU is 0-based.
    uint64_t getCUOffset(uint32_t CU) const;
    uint32_t getCUCount() const { return Hdr.CompUnitCount; }

    /// Reads offset of local type unit TU, TU is 0-based.
    uint64_t getLocalTUOffset(uint32_t TU) const;
    uint32_t getLocalTUCount() const { return Hdr.LocalTypeUnitCount; }

    /// Reads signature of foreign type unit TU. TU is 0-based.
    uint64_t getForeignTUSignature(uint32_t TU) const;
    uint32_t getForeignTUCount() const { return Hdr.ForeignTypeUnitCount; }

    /// Reads an entry in the Bucket Array for the given Bucket. The returned
    /// value is a (1-based) index into the Names, StringOffsets and
    /// EntryOffsets arrays. The input Bucket index is 0-based.
    uint32_t getBucketArrayEntry(uint32_t Bucket) const;
    uint32_t getBucketCount() const { return Hdr.BucketCount; }

    /// Reads an entry in the Hash Array for the given Index. The input Index
    /// is 1-based.
    uint32_t getHashArrayEntry(uint32_t Index) const;

    /// Reads an entry in the Name Table for the given Index. The Name Table
    /// consists of two arrays -- String Offsets and Entry Offsets. The returned
    /// offsets are relative to the starts of respective sections. Input Index
    /// is 1-based.
    NameTableEntry getNameTableEntry(uint32_t Index) const;

    uint32_t getNameCount() const { return Hdr.NameCount; }

    const DenseSet<Abbrev, AbbrevMapInfo> &getAbbrevs() const {
      return Abbrevs;
    }

    Expected<Entry> getEntry(uint64_t *Offset) const;

    /// Returns the Entry at the relative `Offset` from the start of the Entry
    /// pool.
    Expected<Entry> getEntryAtRelativeOffset(uint64_t Offset) const {
      auto OffsetFromSection = Offset + this->Offsets.EntriesBase;
      return getEntry(&OffsetFromSection);
    }

    /// Look up all entries in this Name Index matching \c Key.
    iterator_range<ValueIterator> equal_range(StringRef Key) const;

    NameIterator begin() const { return NameIterator(this, 1); }
    NameIterator end() const { return NameIterator(this, getNameCount() + 1); }

    Error extract();
    uint64_t getUnitOffset() const { return Base; }
    uint64_t getNextUnitOffset() const {
      return Base + dwarf::getUnitLengthFieldByteSize(Hdr.Format) +
             Hdr.UnitLength;
    }
    void dump(ScopedPrinter &W) const;

    friend class DWARFDebugNames;
  };

  class ValueIterator {
  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = Entry;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

  private:
    /// The Name Index we are currently iterating through. The implementation
    /// relies on the fact that this can also be used as an iterator into the
    /// "NameIndices" vector in the Accelerator section.
    const NameIndex *CurrentIndex = nullptr;

    /// Whether this is a local iterator (searches in CurrentIndex only) or not
    /// (searches all name indices).
    bool IsLocal;

    std::optional<Entry> CurrentEntry;
    uint64_t DataOffset = 0; ///< Offset into the section.
    std::string Key;         ///< The Key we are searching for.
    std::optional<uint32_t> Hash; ///< Hash of Key, if it has been computed.

    bool getEntryAtCurrentOffset();
    std::optional<uint64_t> findEntryOffsetInCurrentIndex();
    bool findInCurrentIndex();
    void searchFromStartOfCurrentIndex();
    void next();

    /// Set the iterator to the "end" state.
    void setEnd() { *this = ValueIterator(); }

  public:
    /// Create a "begin" iterator for looping over all entries in the
    /// accelerator table matching Key. The iterator will run through all Name
    /// Indexes in the section in sequence.
    ValueIterator(const DWARFDebugNames &AccelTable, StringRef Key);

    /// Create a "begin" iterator for looping over all entries in a specific
    /// Name Index. Other indices in the section will not be visited.
    ValueIterator(const NameIndex &NI, StringRef Key);

    /// End marker.
    ValueIterator() = default;

    const Entry &operator*() const { return *CurrentEntry; }
    ValueIterator &operator++() {
      next();
      return *this;
    }
    ValueIterator operator++(int) {
      ValueIterator I = *this;
      next();
      return I;
    }

    friend bool operator==(const ValueIterator &A, const ValueIterator &B) {
      return A.CurrentIndex == B.CurrentIndex && A.DataOffset == B.DataOffset;
    }
    friend bool operator!=(const ValueIterator &A, const ValueIterator &B) {
      return !(A == B);
    }
  };

  class NameIterator {

    /// The Name Index we are iterating through.
    const NameIndex *CurrentIndex;

    /// The current name in the Name Index.
    uint32_t CurrentName;

    void next() {
      assert(CurrentName <= CurrentIndex->getNameCount());
      ++CurrentName;
    }

  public:
    using iterator_category = std::input_iterator_tag;
    using value_type = NameTableEntry;
    using difference_type = uint32_t;
    using pointer = NameTableEntry *;
    using reference = NameTableEntry; // We return entries by value.

    /// Creates an iterator whose initial position is name CurrentName in
    /// CurrentIndex.
    NameIterator(const NameIndex *CurrentIndex, uint32_t CurrentName)
        : CurrentIndex(CurrentIndex), CurrentName(CurrentName) {}

    NameTableEntry operator*() const {
      return CurrentIndex->getNameTableEntry(CurrentName);
    }
    NameIterator &operator++() {
      next();
      return *this;
    }
    NameIterator operator++(int) {
      NameIterator I = *this;
      next();
      return I;
    }

    friend bool operator==(const NameIterator &A, const NameIterator &B) {
      return A.CurrentIndex == B.CurrentIndex && A.CurrentName == B.CurrentName;
    }
    friend bool operator!=(const NameIterator &A, const NameIterator &B) {
      return !(A == B);
    }
  };

private:
  SmallVector<NameIndex, 0> NameIndices;
  DenseMap<uint64_t, const NameIndex *> CUToNameIndex;

public:
  DWARFDebugNames(const DWARFDataExtractor &AccelSection,
                  DataExtractor StringSection)
      : DWARFAcceleratorTable(AccelSection, StringSection) {}

  Error extract() override;
  void dump(raw_ostream &OS) const override;

  /// Look up all entries in the accelerator table matching \c Key.
  iterator_range<ValueIterator> equal_range(StringRef Key) const;

  using const_iterator = SmallVector<NameIndex, 0>::const_iterator;
  const_iterator begin() const { return NameIndices.begin(); }
  const_iterator end() const { return NameIndices.end(); }

  /// Return the Name Index covering the compile unit at CUOffset, or nullptr if
  /// there is no Name Index covering that unit.
  const NameIndex *getCUNameIndex(uint64_t CUOffset);
};

/// Calculates the starting offsets for various sections within the
/// .debug_names section.
namespace dwarf {
DWARFDebugNames::DWARFDebugNamesOffsets
findDebugNamesOffsets(uint64_t EndOfHeaderOffset,
                      const DWARFDebugNames::Header &Hdr);
}

/// If `Name` is the name of a templated function that includes template
/// parameters, returns a substring of `Name` containing no template
/// parameters.
/// E.g.: StripTemplateParameters("foo<int>") = "foo".
std::optional<StringRef> StripTemplateParameters(StringRef Name);

struct ObjCSelectorNames {
  /// For "-[A(Category) method:]", this would be "method:"
  StringRef Selector;
  /// For "-[A(Category) method:]", this would be "A(category)"
  StringRef ClassName;
  /// For "-[A(Category) method:]", this would be "A"
  std::optional<StringRef> ClassNameNoCategory;
  /// For "-[A(Category) method:]", this would be "A method:"
  std::optional<std::string> MethodNameNoCategory;
};

/// If `Name` is the AT_name of a DIE which refers to an Objective-C selector,
/// returns an instance of ObjCSelectorNames. The Selector and ClassName fields
/// are guaranteed to be non-empty in the result.
std::optional<ObjCSelectorNames> getObjCNamesIfSelector(StringRef Name);

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFACCELERATORTABLE_H
