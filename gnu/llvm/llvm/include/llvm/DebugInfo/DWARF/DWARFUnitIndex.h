//===- DWARFUnitIndex.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H
#define LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <memory>

namespace llvm {

class raw_ostream;
class DataExtractor;

/// The enum of section identifiers to be used in internal interfaces.
///
/// Pre-standard implementation of package files defined a number of section
/// identifiers with values that clash definitions in the DWARFv5 standard.
/// See https://gcc.gnu.org/wiki/DebugFissionDWP and Section 7.3.5.3 in DWARFv5.
///
/// The following identifiers are the same in the proposal and in DWARFv5:
/// - DW_SECT_INFO         = 1 (.debug_info.dwo)
/// - DW_SECT_ABBREV       = 3 (.debug_abbrev.dwo)
/// - DW_SECT_LINE         = 4 (.debug_line.dwo)
/// - DW_SECT_STR_OFFSETS  = 6 (.debug_str_offsets.dwo)
///
/// The following identifiers are defined only in DWARFv5:
/// - DW_SECT_LOCLISTS     = 5 (.debug_loclists.dwo)
/// - DW_SECT_RNGLISTS     = 8 (.debug_rnglists.dwo)
///
/// The following identifiers are defined only in the GNU proposal:
/// - DW_SECT_TYPES        = 2 (.debug_types.dwo)
/// - DW_SECT_LOC          = 5 (.debug_loc.dwo)
/// - DW_SECT_MACINFO      = 7 (.debug_macinfo.dwo)
///
/// DW_SECT_MACRO for the .debug_macro.dwo section is defined in both standards,
/// but with different values, 8 in GNU and 7 in DWARFv5.
///
/// This enum defines constants to represent the identifiers of both sets.
/// For DWARFv5 ones, the values are the same as defined in the standard.
/// For pre-standard ones that correspond to sections being deprecated in
/// DWARFv5, the values are chosen arbitrary and a tag "_EXT_" is added to
/// the names.
///
/// The enum is for internal use only. The user should not expect the values
/// to correspond to any input/output constants. Special conversion functions,
/// serializeSectionKind() and deserializeSectionKind(), should be used for
/// the translation.
enum DWARFSectionKind {
  /// Denotes a value read from an index section that does not correspond
  /// to any of the supported standards.
  DW_SECT_EXT_unknown = 0,
#define HANDLE_DW_SECT(ID, NAME) DW_SECT_##NAME = ID,
#include "llvm/BinaryFormat/Dwarf.def"
  DW_SECT_EXT_TYPES = 2,
  DW_SECT_EXT_LOC = 9,
  DW_SECT_EXT_MACINFO = 10,
};

inline const char *toString(DWARFSectionKind Kind) {
  switch (Kind) {
  case DW_SECT_EXT_unknown:
    return "Unknown DW_SECT value 0";
#define STRINGIZE(X) #X
#define HANDLE_DW_SECT(ID, NAME)                                               \
  case DW_SECT_##NAME:                                                         \
    return "DW_SECT_" STRINGIZE(NAME);
#include "llvm/BinaryFormat/Dwarf.def"
  case DW_SECT_EXT_TYPES:
    return "DW_SECT_TYPES";
  case DW_SECT_EXT_LOC:
    return "DW_SECT_LOC";
  case DW_SECT_EXT_MACINFO:
    return "DW_SECT_MACINFO";
  }
  llvm_unreachable("unknown DWARFSectionKind");
}

/// Convert the internal value for a section kind to an on-disk value.
///
/// The conversion depends on the version of the index section.
/// IndexVersion is expected to be either 2 for pre-standard GNU proposal
/// or 5 for DWARFv5 package file.
uint32_t serializeSectionKind(DWARFSectionKind Kind, unsigned IndexVersion);

/// Convert a value read from an index section to the internal representation.
///
/// The conversion depends on the index section version, which is expected
/// to be either 2 for pre-standard GNU proposal or 5 for DWARFv5 package file.
DWARFSectionKind deserializeSectionKind(uint32_t Value, unsigned IndexVersion);

class DWARFUnitIndex {
  struct Header {
    uint32_t Version;
    uint32_t NumColumns;
    uint32_t NumUnits;
    uint32_t NumBuckets = 0;

    bool parse(DataExtractor IndexData, uint64_t *OffsetPtr);
    void dump(raw_ostream &OS) const;
  };

public:
  class Entry {
  public:
    class SectionContribution {
    private:
      uint64_t Offset;
      uint64_t Length;

    public:
      SectionContribution() : Offset(0), Length(0) {}
      SectionContribution(uint64_t Offset, uint64_t Length)
          : Offset(Offset), Length(Length) {}

      void setOffset(uint64_t Value) { Offset = Value; }
      void setLength(uint64_t Value) { Length = Value; }
      uint64_t getOffset() const { return Offset; }
      uint64_t getLength() const { return Length; }
      uint32_t getOffset32() const { return (uint32_t)Offset; }
      uint32_t getLength32() const { return (uint32_t)Length; }
    };

  private:
    const DWARFUnitIndex *Index;
    uint64_t Signature;
    std::unique_ptr<SectionContribution[]> Contributions;
    friend class DWARFUnitIndex;

  public:
    const SectionContribution *getContribution(DWARFSectionKind Sec) const;
    const SectionContribution *getContribution() const;
    SectionContribution &getContribution();

    const SectionContribution *getContributions() const {
      return Contributions.get();
    }

    uint64_t getSignature() const { return Signature; }
    bool isValid() { return Index; }
  };

private:
  struct Header Header;

  DWARFSectionKind InfoColumnKind;
  int InfoColumn = -1;
  std::unique_ptr<DWARFSectionKind[]> ColumnKinds;
  // This is a parallel array of section identifiers as they read from the input
  // file. The mapping from raw values to DWARFSectionKind is not revertable in
  // case of unknown identifiers, so we keep them here.
  std::unique_ptr<uint32_t[]> RawSectionIds;
  std::unique_ptr<Entry[]> Rows;
  mutable std::vector<Entry *> OffsetLookup;

  static StringRef getColumnHeader(DWARFSectionKind DS);

  bool parseImpl(DataExtractor IndexData);

public:
  DWARFUnitIndex(DWARFSectionKind InfoColumnKind)
      : InfoColumnKind(InfoColumnKind) {}

  explicit operator bool() const { return Header.NumBuckets; }

  bool parse(DataExtractor IndexData);
  void dump(raw_ostream &OS) const;

  uint32_t getVersion() const { return Header.Version; }

  const Entry *getFromOffset(uint64_t Offset) const;
  const Entry *getFromHash(uint64_t Offset) const;

  ArrayRef<DWARFSectionKind> getColumnKinds() const {
    return ArrayRef(ColumnKinds.get(), Header.NumColumns);
  }

  ArrayRef<Entry> getRows() const {
    return ArrayRef(Rows.get(), Header.NumBuckets);
  }

  MutableArrayRef<Entry> getMutableRows() {
    return MutableArrayRef(Rows.get(), Header.NumBuckets);
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFUNITINDEX_H
