//===- DWARFListTable.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFLISTTABLE_H
#define LLVM_DEBUGINFO_DWARF_DWARFLISTTABLE_H

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <map>
#include <vector>

namespace llvm {

/// A base class for DWARF list entries, such as range or location list
/// entries.
struct DWARFListEntryBase {
  /// The offset at which the entry is located in the section.
  uint64_t Offset;
  /// The DWARF encoding (DW_RLE_* or DW_LLE_*).
  uint8_t EntryKind;
  /// The index of the section this entry belongs to.
  uint64_t SectionIndex;
};

/// A base class for lists of entries that are extracted from a particular
/// section, such as range lists or location lists.
template <typename ListEntryType> class DWARFListType {
  using EntryType = ListEntryType;
  using ListEntries = std::vector<EntryType>;

protected:
  ListEntries Entries;

public:
  const ListEntries &getEntries() const { return Entries; }
  bool empty() const { return Entries.empty(); }
  void clear() { Entries.clear(); }
  Error extract(DWARFDataExtractor Data, uint64_t HeaderOffset,
                uint64_t *OffsetPtr, StringRef SectionName,
                StringRef ListStringName);
};

/// A class representing the header of a list table such as the range list
/// table in the .debug_rnglists section.
class DWARFListTableHeader {
  struct Header {
    /// The total length of the entries for this table, not including the length
    /// field itself.
    uint64_t Length = 0;
    /// The DWARF version number.
    uint16_t Version;
    /// The size in bytes of an address on the target architecture. For
    /// segmented addressing, this is the size of the offset portion of the
    /// address.
    uint8_t AddrSize;
    /// The size in bytes of a segment selector on the target architecture.
    /// If the target system uses a flat address space, this value is 0.
    uint8_t SegSize;
    /// The number of offsets that follow the header before the range lists.
    uint32_t OffsetEntryCount;
  };

  Header HeaderData;
  /// The table's format, either DWARF32 or DWARF64.
  dwarf::DwarfFormat Format;
  /// The offset at which the header (and hence the table) is located within
  /// its section.
  uint64_t HeaderOffset;
  /// The name of the section the list is located in.
  StringRef SectionName;
  /// A characterization of the list for dumping purposes, e.g. "range" or
  /// "location".
  StringRef ListTypeString;

public:
  DWARFListTableHeader(StringRef SectionName, StringRef ListTypeString)
      : SectionName(SectionName), ListTypeString(ListTypeString) {}

  void clear() {
    HeaderData = {};
  }
  uint64_t getHeaderOffset() const { return HeaderOffset; }
  uint8_t getAddrSize() const { return HeaderData.AddrSize; }
  uint64_t getLength() const { return HeaderData.Length; }
  uint16_t getVersion() const { return HeaderData.Version; }
  uint32_t getOffsetEntryCount() const { return HeaderData.OffsetEntryCount; }
  StringRef getSectionName() const { return SectionName; }
  StringRef getListTypeString() const { return ListTypeString; }
  dwarf::DwarfFormat getFormat() const { return Format; }

  /// Return the size of the table header including the length but not including
  /// the offsets.
  static uint8_t getHeaderSize(dwarf::DwarfFormat Format) {
    switch (Format) {
    case dwarf::DwarfFormat::DWARF32:
      return 12;
    case dwarf::DwarfFormat::DWARF64:
      return 20;
    }
    llvm_unreachable("Invalid DWARF format (expected DWARF32 or DWARF64");
  }

  void dump(DataExtractor Data, raw_ostream &OS,
            DIDumpOptions DumpOpts = {}) const;
  std::optional<uint64_t> getOffsetEntry(DataExtractor Data,
                                         uint32_t Index) const {
    if (Index >= HeaderData.OffsetEntryCount)
      return std::nullopt;

    return getOffsetEntry(Data, getHeaderOffset() + getHeaderSize(Format), Format, Index);
  }

  static std::optional<uint64_t> getOffsetEntry(DataExtractor Data,
                                                uint64_t OffsetTableOffset,
                                                dwarf::DwarfFormat Format,
                                                uint32_t Index) {
    uint8_t OffsetByteSize = Format == dwarf::DWARF64 ? 8 : 4;
    uint64_t Offset = OffsetTableOffset + OffsetByteSize * Index;
    auto R = Data.getUnsigned(&Offset, OffsetByteSize);
    return R;
  }

  /// Extract the table header and the array of offsets.
  Error extract(DWARFDataExtractor Data, uint64_t *OffsetPtr);

  /// Returns the length of the table, including the length field, or 0 if the
  /// length has not been determined (e.g. because the table has not yet been
  /// parsed, or there was a problem in parsing).
  uint64_t length() const;
};

/// A class representing a table of lists as specified in the DWARF v5
/// standard for location lists and range lists. The table consists of a header
/// followed by an array of offsets into a DWARF section, followed by zero or
/// more list entries. The list entries are kept in a map where the keys are
/// the lists' section offsets.
template <typename DWARFListType> class DWARFListTableBase {
  DWARFListTableHeader Header;
  /// A mapping between file offsets and lists. It is used to find a particular
  /// list based on an offset (obtained from DW_AT_ranges, for example).
  std::map<uint64_t, DWARFListType> ListMap;
  /// This string is displayed as a heading before the list is dumped
  /// (e.g. "ranges:").
  StringRef HeaderString;

protected:
  DWARFListTableBase(StringRef SectionName, StringRef HeaderString,
                     StringRef ListTypeString)
      : Header(SectionName, ListTypeString), HeaderString(HeaderString) {}

public:
  void clear() {
    Header.clear();
    ListMap.clear();
  }
  /// Extract the table header and the array of offsets.
  Error extractHeaderAndOffsets(DWARFDataExtractor Data, uint64_t *OffsetPtr) {
    return Header.extract(Data, OffsetPtr);
  }
  /// Extract an entire table, including all list entries.
  Error extract(DWARFDataExtractor Data, uint64_t *OffsetPtr);
  /// Look up a list based on a given offset. Extract it and enter it into the
  /// list map if necessary.
  Expected<DWARFListType> findList(DWARFDataExtractor Data,
                                   uint64_t Offset) const;

  uint64_t getHeaderOffset() const { return Header.getHeaderOffset(); }
  uint8_t getAddrSize() const { return Header.getAddrSize(); }
  uint32_t getOffsetEntryCount() const { return Header.getOffsetEntryCount(); }
  dwarf::DwarfFormat getFormat() const { return Header.getFormat(); }

  void
  dump(DWARFDataExtractor Data, raw_ostream &OS,
       llvm::function_ref<std::optional<object::SectionedAddress>(uint32_t)>
           LookupPooledAddress,
       DIDumpOptions DumpOpts = {}) const;

  /// Return the contents of the offset entry designated by a given index.
  std::optional<uint64_t> getOffsetEntry(DataExtractor Data,
                                         uint32_t Index) const {
    return Header.getOffsetEntry(Data, Index);
  }
  /// Return the size of the table header including the length but not including
  /// the offsets. This is dependent on the table format, which is unambiguously
  /// derived from parsing the table.
  uint8_t getHeaderSize() const {
    return DWARFListTableHeader::getHeaderSize(getFormat());
  }

  uint64_t length() { return Header.length(); }
};

template <typename DWARFListType>
Error DWARFListTableBase<DWARFListType>::extract(DWARFDataExtractor Data,
                                                 uint64_t *OffsetPtr) {
  clear();
  if (Error E = extractHeaderAndOffsets(Data, OffsetPtr))
    return E;

  Data.setAddressSize(Header.getAddrSize());
  Data = DWARFDataExtractor(Data, getHeaderOffset() + Header.length());
  while (Data.isValidOffset(*OffsetPtr)) {
    DWARFListType CurrentList;
    uint64_t Off = *OffsetPtr;
    if (Error E = CurrentList.extract(Data, getHeaderOffset(), OffsetPtr,
                                      Header.getSectionName(),
                                      Header.getListTypeString()))
      return E;
    ListMap[Off] = CurrentList;
  }

  assert(*OffsetPtr == Data.size() &&
         "mismatch between expected length of table and length "
         "of extracted data");
  return Error::success();
}

template <typename ListEntryType>
Error DWARFListType<ListEntryType>::extract(DWARFDataExtractor Data,
                                            uint64_t HeaderOffset,
                                            uint64_t *OffsetPtr,
                                            StringRef SectionName,
                                            StringRef ListTypeString) {
  if (*OffsetPtr < HeaderOffset || *OffsetPtr >= Data.size())
    return createStringError(errc::invalid_argument,
                       "invalid %s list offset 0x%" PRIx64,
                       ListTypeString.data(), *OffsetPtr);
  Entries.clear();
  while (Data.isValidOffset(*OffsetPtr)) {
    ListEntryType Entry;
    if (Error E = Entry.extract(Data, OffsetPtr))
      return E;
    Entries.push_back(Entry);
    if (Entry.isSentinel())
      return Error::success();
  }
  return createStringError(errc::illegal_byte_sequence,
                     "no end of list marker detected at end of %s table "
                     "starting at offset 0x%" PRIx64,
                     SectionName.data(), HeaderOffset);
}

template <typename DWARFListType>
void DWARFListTableBase<DWARFListType>::dump(
    DWARFDataExtractor Data, raw_ostream &OS,
    llvm::function_ref<std::optional<object::SectionedAddress>(uint32_t)>
        LookupPooledAddress,
    DIDumpOptions DumpOpts) const {
  Header.dump(Data, OS, DumpOpts);
  OS << HeaderString << "\n";

  // Determine the length of the longest encoding string we have in the table,
  // so we can align the output properly. We only need this in verbose mode.
  size_t MaxEncodingStringLength = 0;
  if (DumpOpts.Verbose) {
    for (const auto &List : ListMap)
      for (const auto &Entry : List.second.getEntries())
        MaxEncodingStringLength =
            std::max(MaxEncodingStringLength,
                     dwarf::RangeListEncodingString(Entry.EntryKind).size());
  }

  uint64_t CurrentBase = 0;
  for (const auto &List : ListMap)
    for (const auto &Entry : List.second.getEntries())
      Entry.dump(OS, getAddrSize(), MaxEncodingStringLength, CurrentBase,
                 DumpOpts, LookupPooledAddress);
}

template <typename DWARFListType>
Expected<DWARFListType>
DWARFListTableBase<DWARFListType>::findList(DWARFDataExtractor Data,
                                            uint64_t Offset) const {
  // Extract the list from the section and enter it into the list map.
  DWARFListType List;
  if (Header.length())
    Data = DWARFDataExtractor(Data, getHeaderOffset() + Header.length());
  if (Error E =
          List.extract(Data, Header.length() ? getHeaderOffset() : 0, &Offset,
                       Header.getSectionName(), Header.getListTypeString()))
    return std::move(E);
  return List;
}

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFLISTTABLE_H
