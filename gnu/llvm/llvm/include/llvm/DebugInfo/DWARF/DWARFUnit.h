//===- DWARFUnit.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFUNIT_H
#define LLVM_DEBUGINFO_DWARF_DWARFUNIT_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFAddressRange.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugInfoEntry.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFLocationExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFUnitIndex.h"
#include "llvm/Support/DataExtractor.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace llvm {

class DWARFAbbreviationDeclarationSet;
class DWARFContext;
class DWARFDebugAbbrev;
class DWARFUnit;
class DWARFDebugRangeList;
class DWARFLocationTable;
class DWARFObject;
class raw_ostream;
struct DIDumpOptions;
struct DWARFSection;
namespace dwarf_linker {
namespace parallel {
class CompileUnit;
}
} // namespace dwarf_linker

/// Base class describing the header of any kind of "unit."  Some information
/// is specific to certain unit types.  We separate this class out so we can
/// parse the header before deciding what specific kind of unit to construct.
class DWARFUnitHeader {
  // Offset within section.
  uint64_t Offset = 0;
  // Version, address size, and DWARF format.
  dwarf::FormParams FormParams;
  uint64_t Length = 0;
  uint64_t AbbrOffset = 0;

  // For DWO units only.
  const DWARFUnitIndex::Entry *IndexEntry = nullptr;

  // For type units only.
  uint64_t TypeHash = 0;
  uint64_t TypeOffset = 0;

  // For v5 split or skeleton compile units only.
  std::optional<uint64_t> DWOId;

  // Unit type as parsed, or derived from the section kind.
  uint8_t UnitType = 0;

  // Size as parsed. uint8_t for compactness.
  uint8_t Size = 0;

public:
  /// Parse a unit header from \p debug_info starting at \p offset_ptr.
  /// Note that \p SectionKind is used as a hint to guess the unit type
  /// for DWARF formats prior to DWARFv5. In DWARFv5 the unit type is
  /// explicitly defined in the header and the hint is ignored.
  Error extract(DWARFContext &Context, const DWARFDataExtractor &debug_info,
                uint64_t *offset_ptr, DWARFSectionKind SectionKind);
  // For units in DWARF Package File, remember the index entry and update
  // the abbreviation offset read by extract().
  Error applyIndexEntry(const DWARFUnitIndex::Entry *Entry);
  uint64_t getOffset() const { return Offset; }
  const dwarf::FormParams &getFormParams() const { return FormParams; }
  uint16_t getVersion() const { return FormParams.Version; }
  dwarf::DwarfFormat getFormat() const { return FormParams.Format; }
  uint8_t getAddressByteSize() const { return FormParams.AddrSize; }
  uint8_t getRefAddrByteSize() const { return FormParams.getRefAddrByteSize(); }
  uint8_t getDwarfOffsetByteSize() const {
    return FormParams.getDwarfOffsetByteSize();
  }
  uint64_t getLength() const { return Length; }
  uint64_t getAbbrOffset() const { return AbbrOffset; }
  std::optional<uint64_t> getDWOId() const { return DWOId; }
  void setDWOId(uint64_t Id) {
    assert((!DWOId || *DWOId == Id) && "setting DWOId to a different value");
    DWOId = Id;
  }
  const DWARFUnitIndex::Entry *getIndexEntry() const { return IndexEntry; }
  uint64_t getTypeHash() const { return TypeHash; }
  uint64_t getTypeOffset() const { return TypeOffset; }
  uint8_t getUnitType() const { return UnitType; }
  bool isTypeUnit() const {
    return UnitType == dwarf::DW_UT_type || UnitType == dwarf::DW_UT_split_type;
  }
  uint8_t getSize() const { return Size; }
  uint8_t getUnitLengthFieldByteSize() const {
    return dwarf::getUnitLengthFieldByteSize(FormParams.Format);
  }
  uint64_t getNextUnitOffset() const {
    return Offset + Length + getUnitLengthFieldByteSize();
  }
};

const DWARFUnitIndex &getDWARFUnitIndex(DWARFContext &Context,
                                        DWARFSectionKind Kind);

bool isCompileUnit(const std::unique_ptr<DWARFUnit> &U);

/// Describe a collection of units. Intended to hold all units either from
/// .debug_info and .debug_types, or from .debug_info.dwo and .debug_types.dwo.
class DWARFUnitVector final : public SmallVector<std::unique_ptr<DWARFUnit>, 1> {
  std::function<std::unique_ptr<DWARFUnit>(uint64_t, DWARFSectionKind,
                                           const DWARFSection *,
                                           const DWARFUnitIndex::Entry *)>
      Parser;
  int NumInfoUnits = -1;

public:
  using UnitVector = SmallVectorImpl<std::unique_ptr<DWARFUnit>>;
  using iterator = typename UnitVector::iterator;
  using iterator_range = llvm::iterator_range<typename UnitVector::iterator>;

  using compile_unit_range =
      decltype(make_filter_range(std::declval<iterator_range>(), isCompileUnit));

  DWARFUnit *getUnitForOffset(uint64_t Offset) const;
  DWARFUnit *getUnitForIndexEntry(const DWARFUnitIndex::Entry &E);

  /// Read units from a .debug_info or .debug_types section.  Calls made
  /// before finishedInfoUnits() are assumed to be for .debug_info sections,
  /// calls after finishedInfoUnits() are for .debug_types sections.  Caller
  /// must not mix calls to addUnitsForSection and addUnitsForDWOSection.
  void addUnitsForSection(DWARFContext &C, const DWARFSection &Section,
                          DWARFSectionKind SectionKind);
  /// Read units from a .debug_info.dwo or .debug_types.dwo section.  Calls
  /// made before finishedInfoUnits() are assumed to be for .debug_info.dwo
  /// sections, calls after finishedInfoUnits() are for .debug_types.dwo
  /// sections.  Caller must not mix calls to addUnitsForSection and
  /// addUnitsForDWOSection.
  void addUnitsForDWOSection(DWARFContext &C, const DWARFSection &DWOSection,
                             DWARFSectionKind SectionKind, bool Lazy = false);

  /// Add an existing DWARFUnit to this UnitVector. This is used by the DWARF
  /// verifier to process unit separately.
  DWARFUnit *addUnit(std::unique_ptr<DWARFUnit> Unit);

  /// Returns number of all units held by this instance.
  unsigned getNumUnits() const { return size(); }
  /// Returns number of units from all .debug_info[.dwo] sections.
  unsigned getNumInfoUnits() const {
    return NumInfoUnits == -1 ? size() : NumInfoUnits;
  }
  /// Returns number of units from all .debug_types[.dwo] sections.
  unsigned getNumTypesUnits() const { return size() - NumInfoUnits; }
  /// Indicate that parsing .debug_info[.dwo] is done, and remaining units
  /// will be from .debug_types[.dwo].
  void finishedInfoUnits() { NumInfoUnits = size(); }

private:
  void addUnitsImpl(DWARFContext &Context, const DWARFObject &Obj,
                    const DWARFSection &Section, const DWARFDebugAbbrev *DA,
                    const DWARFSection *RS, const DWARFSection *LocSection,
                    StringRef SS, const DWARFSection &SOS,
                    const DWARFSection *AOS, const DWARFSection &LS, bool LE,
                    bool IsDWO, bool Lazy, DWARFSectionKind SectionKind);
};

/// Represents base address of the CU.
/// Represents a unit's contribution to the string offsets table.
struct StrOffsetsContributionDescriptor {
  uint64_t Base = 0;
  /// The contribution size not including the header.
  uint64_t Size = 0;
  /// Format and version.
  dwarf::FormParams FormParams = {0, 0, dwarf::DwarfFormat::DWARF32};

  StrOffsetsContributionDescriptor(uint64_t Base, uint64_t Size,
                                   uint8_t Version, dwarf::DwarfFormat Format)
      : Base(Base), Size(Size), FormParams({Version, 0, Format}) {}
  StrOffsetsContributionDescriptor() = default;

  uint8_t getVersion() const { return FormParams.Version; }
  dwarf::DwarfFormat getFormat() const { return FormParams.Format; }
  uint8_t getDwarfOffsetByteSize() const {
    return FormParams.getDwarfOffsetByteSize();
  }
  /// Determine whether a contribution to the string offsets table is
  /// consistent with the relevant section size and that its length is
  /// a multiple of the size of one of its entries.
  Expected<StrOffsetsContributionDescriptor>
  validateContributionSize(DWARFDataExtractor &DA);
};

class DWARFUnit {
  DWARFContext &Context;
  /// Section containing this DWARFUnit.
  const DWARFSection &InfoSection;

  DWARFUnitHeader Header;
  const DWARFDebugAbbrev *Abbrev;
  const DWARFSection *RangeSection;
  uint64_t RangeSectionBase;
  uint64_t LocSectionBase;

  /// Location table of this unit.
  std::unique_ptr<DWARFLocationTable> LocTable;

  const DWARFSection &LineSection;
  StringRef StringSection;
  const DWARFSection &StringOffsetSection;
  const DWARFSection *AddrOffsetSection;
  DWARFUnit *SU;
  std::optional<uint64_t> AddrOffsetSectionBase;
  bool IsLittleEndian;
  bool IsDWO;
  const DWARFUnitVector &UnitVector;

  /// Start, length, and DWARF format of the unit's contribution to the string
  /// offsets table (DWARF v5).
  std::optional<StrOffsetsContributionDescriptor>
      StringOffsetsTableContribution;

  mutable const DWARFAbbreviationDeclarationSet *Abbrevs;
  std::optional<object::SectionedAddress> BaseAddr;
  /// The compile unit debug information entry items.
  std::vector<DWARFDebugInfoEntry> DieArray;

  /// Map from range's start address to end address and corresponding DIE.
  /// IntervalMap does not support range removal, as a result, we use the
  /// std::map::upper_bound for address range lookup.
  std::map<uint64_t, std::pair<uint64_t, DWARFDie>> AddrDieMap;

  /// Map from the location (interpreted DW_AT_location) of a DW_TAG_variable,
  /// to the end address and the corresponding DIE.
  std::map<uint64_t, std::pair<uint64_t, DWARFDie>> VariableDieMap;
  DenseSet<uint64_t> RootsParsedForVariables;

  using die_iterator_range =
      iterator_range<std::vector<DWARFDebugInfoEntry>::iterator>;

  std::shared_ptr<DWARFUnit> DWO;

protected:
  friend dwarf_linker::parallel::CompileUnit;

  /// Return the index of a \p Die entry inside the unit's DIE vector.
  ///
  /// It is illegal to call this method with a DIE that hasn't be
  /// created by this unit. In other word, it's illegal to call this
  /// method on a DIE that isn't accessible by following
  /// children/sibling links starting from this unit's getUnitDIE().
  uint32_t getDIEIndex(const DWARFDebugInfoEntry *Die) const {
    auto First = DieArray.data();
    assert(Die >= First && Die < First + DieArray.size());
    return Die - First;
  }

  /// Return DWARFDebugInfoEntry for the specified index \p Index.
  const DWARFDebugInfoEntry *getDebugInfoEntry(unsigned Index) const {
    assert(Index < DieArray.size());
    return &DieArray[Index];
  }

  const DWARFDebugInfoEntry *
  getParentEntry(const DWARFDebugInfoEntry *Die) const;
  const DWARFDebugInfoEntry *
  getSiblingEntry(const DWARFDebugInfoEntry *Die) const;
  const DWARFDebugInfoEntry *
  getPreviousSiblingEntry(const DWARFDebugInfoEntry *Die) const;
  const DWARFDebugInfoEntry *
  getFirstChildEntry(const DWARFDebugInfoEntry *Die) const;
  const DWARFDebugInfoEntry *
  getLastChildEntry(const DWARFDebugInfoEntry *Die) const;

  const DWARFUnitHeader &getHeader() const { return Header; }

  /// Find the unit's contribution to the string offsets table and determine its
  /// length and form. The given offset is expected to be derived from the unit
  /// DIE's DW_AT_str_offsets_base attribute.
  Expected<std::optional<StrOffsetsContributionDescriptor>>
  determineStringOffsetsTableContribution(DWARFDataExtractor &DA);

  /// Find the unit's contribution to the string offsets table and determine its
  /// length and form. The given offset is expected to be 0 in a dwo file or,
  /// in a dwp file, the start of the unit's contribution to the string offsets
  /// table section (as determined by the index table).
  Expected<std::optional<StrOffsetsContributionDescriptor>>
  determineStringOffsetsTableContributionDWO(DWARFDataExtractor &DA);

public:
  DWARFUnit(DWARFContext &Context, const DWARFSection &Section,
            const DWARFUnitHeader &Header, const DWARFDebugAbbrev *DA,
            const DWARFSection *RS, const DWARFSection *LocSection,
            StringRef SS, const DWARFSection &SOS, const DWARFSection *AOS,
            const DWARFSection &LS, bool LE, bool IsDWO,
            const DWARFUnitVector &UnitVector);

  virtual ~DWARFUnit();

  bool isLittleEndian() const { return IsLittleEndian; }
  bool isDWOUnit() const { return IsDWO; }
  DWARFContext& getContext() const { return Context; }
  const DWARFSection &getInfoSection() const { return InfoSection; }
  uint64_t getOffset() const { return Header.getOffset(); }
  const dwarf::FormParams &getFormParams() const {
    return Header.getFormParams();
  }
  uint16_t getVersion() const { return Header.getVersion(); }
  uint8_t getAddressByteSize() const { return Header.getAddressByteSize(); }
  uint8_t getRefAddrByteSize() const { return Header.getRefAddrByteSize(); }
  uint8_t getDwarfOffsetByteSize() const {
    return Header.getDwarfOffsetByteSize();
  }
  /// Size in bytes of the parsed unit header.
  uint32_t getHeaderSize() const { return Header.getSize(); }
  uint64_t getLength() const { return Header.getLength(); }
  dwarf::DwarfFormat getFormat() const { return Header.getFormat(); }
  uint8_t getUnitType() const { return Header.getUnitType(); }
  bool isTypeUnit() const { return Header.isTypeUnit(); }
  uint64_t getAbbrOffset() const { return Header.getAbbrOffset(); }
  uint64_t getNextUnitOffset() const { return Header.getNextUnitOffset(); }
  const DWARFSection &getLineSection() const { return LineSection; }
  StringRef getStringSection() const { return StringSection; }
  const DWARFSection &getStringOffsetSection() const {
    return StringOffsetSection;
  }

  void setSkeletonUnit(DWARFUnit *SU) { this->SU = SU; }
  // Returns itself if not using Split DWARF, or if the unit is a skeleton unit
  // - otherwise returns the split full unit's corresponding skeleton, if
  // available.
  DWARFUnit *getLinkedUnit() { return IsDWO ? SU : this; }

  void setAddrOffsetSection(const DWARFSection *AOS, uint64_t Base) {
    AddrOffsetSection = AOS;
    AddrOffsetSectionBase = Base;
  }

  std::optional<uint64_t> getAddrOffsetSectionBase() const {
    return AddrOffsetSectionBase;
  }

  /// Returns offset to the indexed address value inside .debug_addr section.
  std::optional<uint64_t> getIndexedAddressOffset(uint64_t Index) {
    if (std::optional<uint64_t> AddrOffsetSectionBase =
            getAddrOffsetSectionBase())
      return *AddrOffsetSectionBase + Index * getAddressByteSize();

    return std::nullopt;
  }

  /// Recursively update address to Die map.
  void updateAddressDieMap(DWARFDie Die);

  /// Recursively update address to variable Die map.
  void updateVariableDieMap(DWARFDie Die);

  void setRangesSection(const DWARFSection *RS, uint64_t Base) {
    RangeSection = RS;
    RangeSectionBase = Base;
  }

  uint64_t getLocSectionBase() const {
    return LocSectionBase;
  }

  std::optional<object::SectionedAddress>
  getAddrOffsetSectionItem(uint32_t Index) const;
  Expected<uint64_t> getStringOffsetSectionItem(uint32_t Index) const;

  DWARFDataExtractor getDebugInfoExtractor() const;

  DataExtractor getStringExtractor() const {
    return DataExtractor(StringSection, false, 0);
  }

  const DWARFLocationTable &getLocationTable() { return *LocTable; }

  /// Extract the range list referenced by this compile unit from the
  /// .debug_ranges section. If the extraction is unsuccessful, an error
  /// is returned. Successful extraction requires that the compile unit
  /// has already been extracted.
  Error extractRangeList(uint64_t RangeListOffset,
                         DWARFDebugRangeList &RangeList) const;
  void clear();

  const std::optional<StrOffsetsContributionDescriptor> &
  getStringOffsetsTableContribution() {
    extractDIEsIfNeeded(true /*CUDIeOnly*/);
    return StringOffsetsTableContribution;
  }

  uint8_t getDwarfStringOffsetsByteSize() const {
    assert(StringOffsetsTableContribution);
    return StringOffsetsTableContribution->getDwarfOffsetByteSize();
  }

  uint64_t getStringOffsetsBase() const {
    assert(StringOffsetsTableContribution);
    return StringOffsetsTableContribution->Base;
  }

  uint64_t getAbbreviationsOffset() const { return Header.getAbbrOffset(); }

  const DWARFAbbreviationDeclarationSet *getAbbreviations() const;

  static bool isMatchingUnitTypeAndTag(uint8_t UnitType, dwarf::Tag Tag) {
    switch (UnitType) {
    case dwarf::DW_UT_compile:
      return Tag == dwarf::DW_TAG_compile_unit;
    case dwarf::DW_UT_type:
      return Tag == dwarf::DW_TAG_type_unit;
    case dwarf::DW_UT_partial:
      return Tag == dwarf::DW_TAG_partial_unit;
    case dwarf::DW_UT_skeleton:
      return Tag == dwarf::DW_TAG_skeleton_unit;
    case dwarf::DW_UT_split_compile:
    case dwarf::DW_UT_split_type:
      return dwarf::isUnitType(Tag);
    }
    return false;
  }

  std::optional<object::SectionedAddress> getBaseAddress();

  DWARFDie getUnitDIE(bool ExtractUnitDIEOnly = true) {
    extractDIEsIfNeeded(ExtractUnitDIEOnly);
    if (DieArray.empty())
      return DWARFDie();
    return DWARFDie(this, &DieArray[0]);
  }

  DWARFDie getNonSkeletonUnitDIE(bool ExtractUnitDIEOnly = true,
                                 StringRef DWOAlternativeLocation = {}) {
    parseDWO(DWOAlternativeLocation);
    return DWO ? DWO->getUnitDIE(ExtractUnitDIEOnly)
               : getUnitDIE(ExtractUnitDIEOnly);
  }

  const char *getCompilationDir();
  std::optional<uint64_t> getDWOId() {
    extractDIEsIfNeeded(/*CUDieOnly*/ true);
    return getHeader().getDWOId();
  }
  void setDWOId(uint64_t NewID) { Header.setDWOId(NewID); }

  /// Return a vector of address ranges resulting from a (possibly encoded)
  /// range list starting at a given offset in the appropriate ranges section.
  Expected<DWARFAddressRangesVector> findRnglistFromOffset(uint64_t Offset);

  /// Return a vector of address ranges retrieved from an encoded range
  /// list whose offset is found via a table lookup given an index (DWARF v5
  /// and later).
  Expected<DWARFAddressRangesVector> findRnglistFromIndex(uint32_t Index);

  /// Return a rangelist's offset based on an index. The index designates
  /// an entry in the rangelist table's offset array and is supplied by
  /// DW_FORM_rnglistx.
  std::optional<uint64_t> getRnglistOffset(uint32_t Index);

  std::optional<uint64_t> getLoclistOffset(uint32_t Index);

  Expected<DWARFAddressRangesVector> collectAddressRanges();

  Expected<DWARFLocationExpressionsVector>
  findLoclistFromOffset(uint64_t Offset);

  /// Returns subprogram DIE with address range encompassing the provided
  /// address. The pointer is alive as long as parsed compile unit DIEs are not
  /// cleared.
  DWARFDie getSubroutineForAddress(uint64_t Address);

  /// Returns variable DIE for the address provided. The pointer is alive as
  /// long as parsed compile unit DIEs are not cleared.
  DWARFDie getVariableForAddress(uint64_t Address);

  /// getInlinedChainForAddress - fetches inlined chain for a given address.
  /// Returns empty chain if there is no subprogram containing address. The
  /// chain is valid as long as parsed compile unit DIEs are not cleared.
  void getInlinedChainForAddress(uint64_t Address,
                                 SmallVectorImpl<DWARFDie> &InlinedChain);

  /// Return the DWARFUnitVector containing this unit.
  const DWARFUnitVector &getUnitVector() const { return UnitVector; }

  /// Returns the number of DIEs in the unit. Parses the unit
  /// if necessary.
  unsigned getNumDIEs() {
    extractDIEsIfNeeded(false);
    return DieArray.size();
  }

  /// Return the index of a DIE inside the unit's DIE vector.
  ///
  /// It is illegal to call this method with a DIE that hasn't be
  /// created by this unit. In other word, it's illegal to call this
  /// method on a DIE that isn't accessible by following
  /// children/sibling links starting from this unit's getUnitDIE().
  uint32_t getDIEIndex(const DWARFDie &D) const {
    return getDIEIndex(D.getDebugInfoEntry());
  }

  /// Return the DIE object at the given index \p Index.
  DWARFDie getDIEAtIndex(unsigned Index) {
    return DWARFDie(this, getDebugInfoEntry(Index));
  }

  DWARFDie getParent(const DWARFDebugInfoEntry *Die);
  DWARFDie getSibling(const DWARFDebugInfoEntry *Die);
  DWARFDie getPreviousSibling(const DWARFDebugInfoEntry *Die);
  DWARFDie getFirstChild(const DWARFDebugInfoEntry *Die);
  DWARFDie getLastChild(const DWARFDebugInfoEntry *Die);

  /// Return the DIE object for a given offset \p Offset inside the
  /// unit's DIE vector.
  DWARFDie getDIEForOffset(uint64_t Offset) {
    if (std::optional<uint32_t> DieIdx = getDIEIndexForOffset(Offset))
      return DWARFDie(this, &DieArray[*DieIdx]);

    return DWARFDie();
  }

  /// Return the DIE index for a given offset \p Offset inside the
  /// unit's DIE vector.
  std::optional<uint32_t> getDIEIndexForOffset(uint64_t Offset) {
    extractDIEsIfNeeded(false);
    auto It =
        llvm::partition_point(DieArray, [=](const DWARFDebugInfoEntry &DIE) {
          return DIE.getOffset() < Offset;
        });
    if (It != DieArray.end() && It->getOffset() == Offset)
      return It - DieArray.begin();
    return std::nullopt;
  }

  uint32_t getLineTableOffset() const {
    if (auto IndexEntry = Header.getIndexEntry())
      if (const auto *Contrib = IndexEntry->getContribution(DW_SECT_LINE))
        return Contrib->getOffset32();
    return 0;
  }

  die_iterator_range dies() {
    extractDIEsIfNeeded(false);
    return die_iterator_range(DieArray.begin(), DieArray.end());
  }

  virtual void dump(raw_ostream &OS, DIDumpOptions DumpOpts) = 0;

  Error tryExtractDIEsIfNeeded(bool CUDieOnly);

private:
  /// Size in bytes of the .debug_info data associated with this compile unit.
  size_t getDebugInfoSize() const {
    return Header.getLength() + Header.getUnitLengthFieldByteSize() -
           getHeaderSize();
  }

  /// extractDIEsIfNeeded - Parses a compile unit and indexes its DIEs if it
  /// hasn't already been done
  void extractDIEsIfNeeded(bool CUDieOnly);

  /// extractDIEsToVector - Appends all parsed DIEs to a vector.
  void extractDIEsToVector(bool AppendCUDie, bool AppendNonCUDIEs,
                           std::vector<DWARFDebugInfoEntry> &DIEs) const;

  /// clearDIEs - Clear parsed DIEs to keep memory usage low.
  void clearDIEs(bool KeepCUDie);

  /// parseDWO - Parses .dwo file for current compile unit. Returns true if
  /// it was actually constructed.
  /// The \p AlternativeLocation specifies an alternative location to get
  /// the DWARF context for the DWO object; this is the case when it has
  /// been moved from its original location.
  bool parseDWO(StringRef AlternativeLocation = {});
};

inline bool isCompileUnit(const std::unique_ptr<DWARFUnit> &U) {
  return !U->isTypeUnit();
}

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFUNIT_H
