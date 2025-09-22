//===-- DWARFUnit.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFUNIT_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFUNIT_H

#include "DWARFDIE.h"
#include "DWARFDebugInfoEntry.h"
#include "lldb/Utility/XcodeSDK.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRnglists.h"
#include "llvm/Support/RWMutex.h"
#include <atomic>
#include <optional>

namespace lldb_private::plugin {
namespace dwarf {
class DWARFUnit;
class DWARFCompileUnit;
class NameToDIE;
class SymbolFileDWARF;
class SymbolFileDWARFDwo;

typedef std::shared_ptr<DWARFUnit> DWARFUnitSP;

enum DWARFProducer {
  eProducerInvalid = 0,
  eProducerClang,
  eProducerGCC,
  eProducerLLVMGCC,
  eProducerSwift,
  eProducerOther
};

class DWARFUnit : public UserID {
  using die_iterator_range =
      llvm::iterator_range<DWARFDebugInfoEntry::collection::iterator>;

public:
  static llvm::Expected<DWARFUnitSP>
  extract(SymbolFileDWARF &dwarf2Data, lldb::user_id_t uid,
          const DWARFDataExtractor &debug_info, DIERef::Section section,
          lldb::offset_t *offset_ptr);
  virtual ~DWARFUnit();

  bool IsDWOUnit() { return m_is_dwo; }
  /// Get the DWO ID from the DWARFUnitHeader for DWARF5, or from the unit DIE's
  /// DW_AT_dwo_id or DW_AT_GNU_dwo_id for DWARF4 and earlier.
  std::optional<uint64_t> GetDWOId();
  /// Get the DWO ID from the DWARFUnitHeader only. DWARF5 skeleton units have
  /// the DWO ID in the compile unit header and we sometimes only want to access
  /// this cheap value without causing the more expensive attribute fetches that
  /// GetDWOId() uses.
  std::optional<uint64_t> GetHeaderDWOId() { return m_header.getDWOId(); }
  void ExtractUnitDIEIfNeeded();
  void ExtractUnitDIENoDwoIfNeeded();
  void ExtractDIEsIfNeeded();

  class ScopedExtractDIEs {
    DWARFUnit *m_cu;

  public:
    bool m_clear_dies = false;
    ScopedExtractDIEs(DWARFUnit &cu);
    ~ScopedExtractDIEs();
    ScopedExtractDIEs(const ScopedExtractDIEs &) = delete;
    const ScopedExtractDIEs &operator=(const ScopedExtractDIEs &) = delete;
    ScopedExtractDIEs(ScopedExtractDIEs &&rhs);
    ScopedExtractDIEs &operator=(ScopedExtractDIEs &&rhs);
  };
  ScopedExtractDIEs ExtractDIEsScoped();

  bool Verify(Stream *s) const;
  virtual void Dump(Stream *s) const = 0;
  /// Get the data that contains the DIE information for this unit.
  ///
  /// This will return the correct bytes that contain the data for
  /// this DWARFUnit. It could be .debug_info or .debug_types
  /// depending on where the data for this unit originates.
  ///
  /// \return
  ///   The correct data for the DIE information in this unit.
  const DWARFDataExtractor &GetData() const;

  /// Get the size in bytes of the unit header.
  ///
  /// \return
  ///     Byte size of the unit header
  uint32_t GetHeaderByteSize() const;

  // Offset of the initial length field.
  dw_offset_t GetOffset() const { return m_header.getOffset(); }
  /// Get the size in bytes of the length field in the header.
  ///
  /// In DWARF32 this is just 4 bytes
  ///
  /// \return
  ///     Byte size of the compile unit header length field
  size_t GetLengthByteSize() const { return 4; }

  bool ContainsDIEOffset(dw_offset_t die_offset) const {
    return die_offset >= GetFirstDIEOffset() &&
           die_offset < GetNextUnitOffset();
  }
  dw_offset_t GetFirstDIEOffset() const {
    return GetOffset() + GetHeaderByteSize();
  }
  dw_offset_t GetNextUnitOffset() const { return m_header.getNextUnitOffset(); }
  // Size of the CU data (without initial length and without header).
  size_t GetDebugInfoSize() const;
  // Size of the CU data incl. header but without initial length.
  dw_offset_t GetLength() const { return m_header.getLength(); }
  uint16_t GetVersion() const { return m_header.getVersion(); }
  const llvm::DWARFAbbreviationDeclarationSet *GetAbbreviations() const;
  dw_offset_t GetAbbrevOffset() const;
  uint8_t GetAddressByteSize() const { return m_header.getAddressByteSize(); }
  dw_addr_t GetAddrBase() const { return m_addr_base.value_or(0); }
  dw_addr_t GetBaseAddress() const { return m_base_addr; }
  dw_offset_t GetLineTableOffset();
  dw_addr_t GetRangesBase() const { return m_ranges_base; }
  dw_addr_t GetStrOffsetsBase() const { return m_str_offsets_base; }
  void SetAddrBase(dw_addr_t addr_base);
  void SetLoclistsBase(dw_addr_t loclists_base);
  void SetRangesBase(dw_addr_t ranges_base);
  void SetStrOffsetsBase(dw_offset_t str_offsets_base);
  virtual void BuildAddressRangeTable(DWARFDebugAranges *debug_aranges) = 0;

  dw_addr_t ReadAddressFromDebugAddrSection(uint32_t index) const;

  lldb::ByteOrder GetByteOrder() const;

  const DWARFDebugAranges &GetFunctionAranges();

  void SetBaseAddress(dw_addr_t base_addr);

  DWARFBaseDIE GetUnitDIEOnly() { return {this, GetUnitDIEPtrOnly()}; }

  DWARFDIE DIE() { return DWARFDIE(this, DIEPtr()); }

  DWARFDIE GetDIE(dw_offset_t die_offset);

  /// Returns the AT_Name of the DIE at `die_offset`, if it exists, without
  /// parsing the entire compile unit. An empty is string is returned upon
  /// error or if the attribute is not present.
  llvm::StringRef PeekDIEName(dw_offset_t die_offset);

  DWARFUnit &GetNonSkeletonUnit();

  static uint8_t GetAddressByteSize(const DWARFUnit *cu);

  static uint8_t GetDefaultAddressSize();

  lldb_private::CompileUnit *GetLLDBCompUnit() const { return m_lldb_cu; }

  void SetLLDBCompUnit(lldb_private::CompileUnit *cu) { m_lldb_cu = cu; }

  /// Get the skeleton compile unit for a DWO file.
  ///
  /// We need to keep track of the skeleton compile unit for a DWO file so
  /// we can access it. Sometimes this value is cached when the skeleton
  /// compile unit is first parsed, but if a .dwp file parses all of the
  /// DWARFUnits in the file, the skeleton compile unit might not have been
  /// parsed yet, to there might not be a backlink. This accessor handles
  /// both cases correctly and avoids crashes.
  DWARFCompileUnit *GetSkeletonUnit();

  void SetSkeletonUnit(DWARFUnit *skeleton_unit);

  bool Supports_DW_AT_APPLE_objc_complete_type();

  bool DW_AT_decl_file_attributes_are_invalid();

  bool Supports_unnamed_objc_bitfields();

  SymbolFileDWARF &GetSymbolFileDWARF() const { return m_dwarf; }

  DWARFProducer GetProducer();

  llvm::VersionTuple GetProducerVersion();

  uint64_t GetDWARFLanguageType();

  bool GetIsOptimized();

  const FileSpec &GetCompilationDirectory();
  const FileSpec &GetAbsolutePath();
  FileSpec GetFile(size_t file_idx);
  FileSpec::Style GetPathStyle();

  SymbolFileDWARFDwo *GetDwoSymbolFile(bool load_all_debug_info = true);

  die_iterator_range dies() {
    ExtractDIEsIfNeeded();
    return die_iterator_range(m_die_array.begin(), m_die_array.end());
  }

  DIERef::Section GetDebugSection() const { return m_section; }

  uint8_t GetUnitType() const { return m_header.getUnitType(); }
  bool IsTypeUnit() const { return m_header.isTypeUnit(); }
  /// Note that this check only works for DWARF5+.
  bool IsSkeletonUnit() const {
    return GetUnitType() == llvm::dwarf::DW_UT_skeleton;
  }

  std::optional<uint64_t> GetStringOffsetSectionItem(uint32_t index) const;

  /// Return a list of address ranges resulting from a (possibly encoded)
  /// range list starting at a given offset in the appropriate ranges section.
  llvm::Expected<DWARFRangeList> FindRnglistFromOffset(dw_offset_t offset);

  /// Return a list of address ranges retrieved from an encoded range
  /// list whose offset is found via a table lookup given an index (DWARF v5
  /// and later).
  llvm::Expected<DWARFRangeList> FindRnglistFromIndex(uint32_t index);

  /// Return a rangelist's offset based on an index. The index designates
  /// an entry in the rangelist table's offset array and is supplied by
  /// DW_FORM_rnglistx.
  llvm::Expected<uint64_t> GetRnglistOffset(uint32_t Index);

  std::optional<uint64_t> GetLoclistOffset(uint32_t Index) {
    if (!m_loclist_table_header)
      return std::nullopt;

    std::optional<uint64_t> Offset = m_loclist_table_header->getOffsetEntry(
        m_dwarf.GetDWARFContext().getOrLoadLocListsData().GetAsLLVM(), Index);
    if (!Offset)
      return std::nullopt;
    return *Offset + m_loclists_base;
  }

  /// Return the location table for parsing the given location list data. The
  /// format is chosen according to the unit type. Never returns null.
  std::unique_ptr<llvm::DWARFLocationTable>
  GetLocationTable(const DataExtractor &data) const;

  DWARFDataExtractor GetLocationData() const;

  /// Returns true if any DIEs in the unit match any DW_TAG values in \a tags.
  ///
  /// \param[in] tags
  ///   An array of dw_tag_t values to check all abbrevitions for.
  ///
  /// \returns
  ///   True if any DIEs match any tag in \a tags, false otherwise.
  bool HasAny(llvm::ArrayRef<dw_tag_t> tags);

  /// Get the fission .dwo file specific error for this compile unit.
  ///
  /// The skeleton compile unit only can have a DWO error. Any other type
  /// of DWARFUnit will not have a valid DWO error.
  ///
  /// \returns
  ///   A valid DWO error if there is a problem with anything in the
  ///   locating or parsing inforamtion in the .dwo file
  const Status &GetDwoError() const { return m_dwo_error; }

  /// Set the fission .dwo file specific error for this compile unit.
  ///
  /// This helps tracks issues that arise when trying to locate or parse a
  /// .dwo file. Things like a missing .dwo file, DWO ID mismatch, and other
  /// .dwo errors can be stored in each compile unit so the issues can be
  /// communicated to the user.
  void SetDwoError(const Status &error) { m_dwo_error = error; }

protected:
  DWARFUnit(SymbolFileDWARF &dwarf, lldb::user_id_t uid,
            const llvm::DWARFUnitHeader &header,
            const llvm::DWARFAbbreviationDeclarationSet &abbrevs,
            DIERef::Section section, bool is_dwo);

  llvm::Error ExtractHeader(SymbolFileDWARF &dwarf,
                            const DWARFDataExtractor &data,
                            lldb::offset_t *offset_ptr);

  // Get the DWARF unit DWARF debug information entry. Parse the single DIE
  // if needed.
  const DWARFDebugInfoEntry *GetUnitDIEPtrOnly() {
    ExtractUnitDIENoDwoIfNeeded();
    // m_first_die_mutex is not required as m_first_die is never cleared.
    if (!m_first_die)
      return nullptr;
    return &m_first_die;
  }

  // Get all DWARF debug informration entries. Parse all DIEs if needed.
  const DWARFDebugInfoEntry *DIEPtr() {
    ExtractDIEsIfNeeded();
    if (m_die_array.empty())
      return nullptr;
    return &m_die_array[0];
  }

  const std::optional<llvm::DWARFDebugRnglistTable> &GetRnglistTable();

  DWARFDataExtractor GetRnglistData() const;

  SymbolFileDWARF &m_dwarf;
  std::shared_ptr<DWARFUnit> m_dwo;
  llvm::DWARFUnitHeader m_header;
  const llvm::DWARFAbbreviationDeclarationSet *m_abbrevs = nullptr;
  lldb_private::CompileUnit *m_lldb_cu = nullptr;
  // If this is a DWO file, we have a backlink to our skeleton compile unit.
  DWARFUnit *m_skeleton_unit = nullptr;
  // The compile unit debug information entry item
  DWARFDebugInfoEntry::collection m_die_array;
  mutable llvm::sys::RWMutex m_die_array_mutex;
  // It is used for tracking of ScopedExtractDIEs instances.
  mutable llvm::sys::RWMutex m_die_array_scoped_mutex;
  // ScopedExtractDIEs instances should not call ClearDIEsRWLocked()
  // as someone called ExtractDIEsIfNeeded().
  std::atomic<bool> m_cancel_scopes;
  // GetUnitDIEPtrOnly() needs to return pointer to the first DIE.
  // But the first element of m_die_array after ExtractUnitDIEIfNeeded()
  // would possibly move in memory after later ExtractDIEsIfNeeded().
  DWARFDebugInfoEntry m_first_die;
  llvm::sys::RWMutex m_first_die_mutex;
  // A table similar to the .debug_aranges table, but this one points to the
  // exact DW_TAG_subprogram DIEs
  std::unique_ptr<DWARFDebugAranges> m_func_aranges_up;
  dw_addr_t m_base_addr = 0;
  DWARFProducer m_producer = eProducerInvalid;
  llvm::VersionTuple m_producer_version;
  std::optional<uint64_t> m_language_type;
  LazyBool m_is_optimized = eLazyBoolCalculate;
  std::optional<FileSpec> m_comp_dir;
  std::optional<FileSpec> m_file_spec;
  std::optional<dw_addr_t> m_addr_base; ///< Value of DW_AT_addr_base.
  dw_addr_t m_loclists_base = 0;        ///< Value of DW_AT_loclists_base.
  dw_addr_t m_ranges_base = 0;          ///< Value of DW_AT_rnglists_base.
  std::optional<uint64_t> m_gnu_addr_base;
  std::optional<uint64_t> m_gnu_ranges_base;

  /// Value of DW_AT_stmt_list.
  dw_offset_t m_line_table_offset = DW_INVALID_OFFSET;

  dw_offset_t m_str_offsets_base = 0; // Value of DW_AT_str_offsets_base.

  std::optional<llvm::DWARFDebugRnglistTable> m_rnglist_table;
  bool m_rnglist_table_done = false;
  std::optional<llvm::DWARFListTableHeader> m_loclist_table_header;

  const DIERef::Section m_section;
  bool m_is_dwo;
  bool m_has_parsed_non_skeleton_unit;
  /// Value of DW_AT_GNU_dwo_id (v4) or dwo_id from CU header (v5).
  std::optional<uint64_t> m_dwo_id;
  /// If we get an error when trying to load a .dwo file, save that error here.
  /// Errors include .dwo/.dwp file not found, or the .dwp/.dwp file was found
  /// but DWO ID doesn't match, etc.
  Status m_dwo_error;

private:
  void ParseProducerInfo();
  void ExtractDIEsRWLocked();
  void ClearDIEsRWLocked();

  void AddUnitDIE(const DWARFDebugInfoEntry &cu_die);
  void SetDwoStrOffsetsBase();

  void ComputeCompDirAndGuessPathStyle();
  void ComputeAbsolutePath();

  DWARFUnit(const DWARFUnit &) = delete;
  const DWARFUnit &operator=(const DWARFUnit &) = delete;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFUNIT_H
