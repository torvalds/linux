//===-- DWARFUnit.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFUnit.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugAbbrev.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/Object/Error.h"

#include "DWARFCompileUnit.h"
#include "DWARFDebugAranges.h"
#include "DWARFDebugInfo.h"
#include "DWARFTypeUnit.h"
#include "LogChannelDWARF.h"
#include "SymbolFileDWARFDwo.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

extern int g_verbose;

DWARFUnit::DWARFUnit(SymbolFileDWARF &dwarf, lldb::user_id_t uid,
                     const llvm::DWARFUnitHeader &header,
                     const llvm::DWARFAbbreviationDeclarationSet &abbrevs,
                     DIERef::Section section, bool is_dwo)
    : UserID(uid), m_dwarf(dwarf), m_header(header), m_abbrevs(&abbrevs),
      m_cancel_scopes(false), m_section(section), m_is_dwo(is_dwo),
      m_has_parsed_non_skeleton_unit(false), m_dwo_id(header.getDWOId()) {}

DWARFUnit::~DWARFUnit() = default;

// Parses first DIE of a compile unit, excluding DWO.
void DWARFUnit::ExtractUnitDIENoDwoIfNeeded() {
  {
    llvm::sys::ScopedReader lock(m_first_die_mutex);
    if (m_first_die)
      return; // Already parsed
  }
  llvm::sys::ScopedWriter lock(m_first_die_mutex);
  if (m_first_die)
    return; // Already parsed

  ElapsedTime elapsed(m_dwarf.GetDebugInfoParseTimeRef());

  // Set the offset to that of the first DIE and calculate the start of the
  // next compilation unit header.
  lldb::offset_t offset = GetFirstDIEOffset();

  // We are in our compile unit, parse starting at the offset we were told to
  // parse
  const DWARFDataExtractor &data = GetData();
  if (offset < GetNextUnitOffset() &&
      m_first_die.Extract(data, *this, &offset)) {
    AddUnitDIE(m_first_die);
    return;
  }
}

// Parses first DIE of a compile unit including DWO.
void DWARFUnit::ExtractUnitDIEIfNeeded() {
  ExtractUnitDIENoDwoIfNeeded();

  if (m_has_parsed_non_skeleton_unit)
    return;

  m_has_parsed_non_skeleton_unit = true;
  m_dwo_error.Clear();

  if (!m_dwo_id)
    return; // No DWO file.

  std::shared_ptr<SymbolFileDWARFDwo> dwo_symbol_file =
      m_dwarf.GetDwoSymbolFileForCompileUnit(*this, m_first_die);
  if (!dwo_symbol_file)
    return;

  DWARFUnit *dwo_cu = dwo_symbol_file->GetDWOCompileUnitForHash(*m_dwo_id);

  if (!dwo_cu) {
    SetDwoError(Status::createWithFormat(
        "unable to load .dwo file from \"{0}\" due to ID ({1:x16}) mismatch "
        "for skeleton DIE at {2:x8}",
        dwo_symbol_file->GetObjectFile()->GetFileSpec().GetPath().c_str(),
        *m_dwo_id, m_first_die.GetOffset()));
    return; // Can't fetch the compile unit from the dwo file.
  }
  // If the skeleton compile unit gets its unit DIE parsed first, then this
  // will fill in the DWO file's back pointer to this skeleton compile unit.
  // If the DWO files get parsed on their own first the skeleton back link
  // can be done manually in DWARFUnit::GetSkeletonCompileUnit() which will
  // do a reverse lookup and cache the result.
  dwo_cu->SetSkeletonUnit(this);

  DWARFBaseDIE dwo_cu_die = dwo_cu->GetUnitDIEOnly();
  if (!dwo_cu_die.IsValid()) {
    // Can't fetch the compile unit DIE from the dwo file.
    SetDwoError(Status::createWithFormat(
        "unable to extract compile unit DIE from .dwo file for skeleton "
        "DIE at {0:x16}",
        m_first_die.GetOffset()));
    return;
  }

  // Here for DWO CU we want to use the address base set in the skeleton unit
  // (DW_AT_addr_base) if it is available and use the DW_AT_GNU_addr_base
  // otherwise. We do that because pre-DWARF v5 could use the DW_AT_GNU_*
  // attributes which were applicable to the DWO units. The corresponding
  // DW_AT_* attributes standardized in DWARF v5 are also applicable to the
  // main unit in contrast.
  if (m_addr_base)
    dwo_cu->SetAddrBase(*m_addr_base);
  else if (m_gnu_addr_base)
    dwo_cu->SetAddrBase(*m_gnu_addr_base);

  if (GetVersion() <= 4 && m_gnu_ranges_base)
    dwo_cu->SetRangesBase(*m_gnu_ranges_base);
  else if (dwo_symbol_file->GetDWARFContext()
               .getOrLoadRngListsData()
               .GetByteSize() > 0)
    dwo_cu->SetRangesBase(llvm::DWARFListTableHeader::getHeaderSize(DWARF32));

  if (GetVersion() >= 5 &&
      dwo_symbol_file->GetDWARFContext().getOrLoadLocListsData().GetByteSize() >
          0)
    dwo_cu->SetLoclistsBase(llvm::DWARFListTableHeader::getHeaderSize(DWARF32));

  dwo_cu->SetBaseAddress(GetBaseAddress());

  m_dwo = std::shared_ptr<DWARFUnit>(std::move(dwo_symbol_file), dwo_cu);
}

// Parses a compile unit and indexes its DIEs if it hasn't already been done.
// It will leave this compile unit extracted forever.
void DWARFUnit::ExtractDIEsIfNeeded() {
  m_cancel_scopes = true;

  {
    llvm::sys::ScopedReader lock(m_die_array_mutex);
    if (!m_die_array.empty())
      return; // Already parsed
  }
  llvm::sys::ScopedWriter lock(m_die_array_mutex);
  if (!m_die_array.empty())
    return; // Already parsed

  ExtractDIEsRWLocked();
}

// Parses a compile unit and indexes its DIEs if it hasn't already been done.
// It will clear this compile unit after returned instance gets out of scope,
// no other ScopedExtractDIEs instance is running for this compile unit
// and no ExtractDIEsIfNeeded() has been executed during this ScopedExtractDIEs
// lifetime.
DWARFUnit::ScopedExtractDIEs DWARFUnit::ExtractDIEsScoped() {
  ScopedExtractDIEs scoped(*this);

  {
    llvm::sys::ScopedReader lock(m_die_array_mutex);
    if (!m_die_array.empty())
      return scoped; // Already parsed
  }
  llvm::sys::ScopedWriter lock(m_die_array_mutex);
  if (!m_die_array.empty())
    return scoped; // Already parsed

  // Otherwise m_die_array would be already populated.
  lldbassert(!m_cancel_scopes);

  ExtractDIEsRWLocked();
  scoped.m_clear_dies = true;
  return scoped;
}

DWARFUnit::ScopedExtractDIEs::ScopedExtractDIEs(DWARFUnit &cu) : m_cu(&cu) {
  m_cu->m_die_array_scoped_mutex.lock_shared();
}

DWARFUnit::ScopedExtractDIEs::~ScopedExtractDIEs() {
  if (!m_cu)
    return;
  m_cu->m_die_array_scoped_mutex.unlock_shared();
  if (!m_clear_dies || m_cu->m_cancel_scopes)
    return;
  // Be sure no other ScopedExtractDIEs is running anymore.
  llvm::sys::ScopedWriter lock_scoped(m_cu->m_die_array_scoped_mutex);
  llvm::sys::ScopedWriter lock(m_cu->m_die_array_mutex);
  if (m_cu->m_cancel_scopes)
    return;
  m_cu->ClearDIEsRWLocked();
}

DWARFUnit::ScopedExtractDIEs::ScopedExtractDIEs(ScopedExtractDIEs &&rhs)
    : m_cu(rhs.m_cu), m_clear_dies(rhs.m_clear_dies) {
  rhs.m_cu = nullptr;
}

DWARFUnit::ScopedExtractDIEs &
DWARFUnit::ScopedExtractDIEs::operator=(DWARFUnit::ScopedExtractDIEs &&rhs) {
  m_cu = rhs.m_cu;
  rhs.m_cu = nullptr;
  m_clear_dies = rhs.m_clear_dies;
  return *this;
}

// Parses a compile unit and indexes its DIEs, m_die_array_mutex must be
// held R/W and m_die_array must be empty.
void DWARFUnit::ExtractDIEsRWLocked() {
  llvm::sys::ScopedWriter first_die_lock(m_first_die_mutex);

  ElapsedTime elapsed(m_dwarf.GetDebugInfoParseTimeRef());
  LLDB_SCOPED_TIMERF(
      "%s",
      llvm::formatv("{0:x16}: DWARFUnit::ExtractDIEsIfNeeded()", GetOffset())
          .str()
          .c_str());

  // Set the offset to that of the first DIE and calculate the start of the
  // next compilation unit header.
  lldb::offset_t offset = GetFirstDIEOffset();
  lldb::offset_t next_cu_offset = GetNextUnitOffset();

  DWARFDebugInfoEntry die;

  uint32_t depth = 0;
  // We are in our compile unit, parse starting at the offset we were told to
  // parse
  const DWARFDataExtractor &data = GetData();
  std::vector<uint32_t> die_index_stack;
  die_index_stack.reserve(32);
  die_index_stack.push_back(0);
  bool prev_die_had_children = false;
  while (offset < next_cu_offset && die.Extract(data, *this, &offset)) {
    const bool null_die = die.IsNULL();
    if (depth == 0) {
      assert(m_die_array.empty() && "Compile unit DIE already added");

      // The average bytes per DIE entry has been seen to be around 14-20 so
      // lets pre-reserve half of that since we are now stripping the NULL
      // tags.

      // Only reserve the memory if we are adding children of the main
      // compile unit DIE. The compile unit DIE is always the first entry, so
      // if our size is 1, then we are adding the first compile unit child
      // DIE and should reserve the memory.
      m_die_array.reserve(GetDebugInfoSize() / 24);
      m_die_array.push_back(die);

      if (!m_first_die)
        AddUnitDIE(m_die_array.front());

      // With -fsplit-dwarf-inlining, clang will emit non-empty skeleton compile
      // units. We are not able to access these DIE *and* the dwo file
      // simultaneously. We also don't need to do that as the dwo file will
      // contain a superset of information. So, we don't even attempt to parse
      // any remaining DIEs.
      if (m_dwo) {
        m_die_array.front().SetHasChildren(false);
        break;
      }

    } else {
      if (null_die) {
        if (prev_die_had_children) {
          // This will only happen if a DIE says is has children but all it
          // contains is a NULL tag. Since we are removing the NULL DIEs from
          // the list (saves up to 25% in C++ code), we need a way to let the
          // DIE know that it actually doesn't have children.
          if (!m_die_array.empty())
            m_die_array.back().SetHasChildren(false);
        }
      } else {
        die.SetParentIndex(m_die_array.size() - die_index_stack[depth - 1]);

        if (die_index_stack.back())
          m_die_array[die_index_stack.back()].SetSiblingIndex(
              m_die_array.size() - die_index_stack.back());

        // Only push the DIE if it isn't a NULL DIE
        m_die_array.push_back(die);
      }
    }

    if (null_die) {
      // NULL DIE.
      if (!die_index_stack.empty())
        die_index_stack.pop_back();

      if (depth > 0)
        --depth;
      prev_die_had_children = false;
    } else {
      die_index_stack.back() = m_die_array.size() - 1;
      // Normal DIE
      const bool die_has_children = die.HasChildren();
      if (die_has_children) {
        die_index_stack.push_back(0);
        ++depth;
      }
      prev_die_had_children = die_has_children;
    }

    if (depth == 0)
      break; // We are done with this compile unit!
  }

  if (!m_die_array.empty()) {
    // The last die cannot have children (if it did, it wouldn't be the last
    // one). This only makes a difference for malformed dwarf that does not have
    // a terminating null die.
    m_die_array.back().SetHasChildren(false);

    if (m_first_die) {
      // Only needed for the assertion.
      m_first_die.SetHasChildren(m_die_array.front().HasChildren());
      lldbassert(m_first_die == m_die_array.front());
    }
    m_first_die = m_die_array.front();
  }

  m_die_array.shrink_to_fit();

  if (m_dwo)
    m_dwo->ExtractDIEsIfNeeded();
}

// This is used when a split dwarf is enabled.
// A skeleton compilation unit may contain the DW_AT_str_offsets_base attribute
// that points to the first string offset of the CU contribution to the
// .debug_str_offsets. At the same time, the corresponding split debug unit also
// may use DW_FORM_strx* forms pointing to its own .debug_str_offsets.dwo and
// for that case, we should find the offset (skip the section header).
void DWARFUnit::SetDwoStrOffsetsBase() {
  lldb::offset_t baseOffset = 0;

  if (const llvm::DWARFUnitIndex::Entry *entry = m_header.getIndexEntry()) {
    if (const auto *contribution =
            entry->getContribution(llvm::DW_SECT_STR_OFFSETS))
      baseOffset = contribution->getOffset();
    else
      return;
  }

  if (GetVersion() >= 5) {
    const DWARFDataExtractor &strOffsets =
        GetSymbolFileDWARF().GetDWARFContext().getOrLoadStrOffsetsData();
    uint64_t length = strOffsets.GetU32(&baseOffset);
    if (length == 0xffffffff)
      length = strOffsets.GetU64(&baseOffset);

    // Check version.
    if (strOffsets.GetU16(&baseOffset) < 5)
      return;

    // Skip padding.
    baseOffset += 2;
  }

  SetStrOffsetsBase(baseOffset);
}

std::optional<uint64_t> DWARFUnit::GetDWOId() {
  ExtractUnitDIENoDwoIfNeeded();
  return m_dwo_id;
}

// m_die_array_mutex must be already held as read/write.
void DWARFUnit::AddUnitDIE(const DWARFDebugInfoEntry &cu_die) {
  DWARFAttributes attributes = cu_die.GetAttributes(this);

  // Extract DW_AT_addr_base first, as other attributes may need it.
  for (size_t i = 0; i < attributes.Size(); ++i) {
    if (attributes.AttributeAtIndex(i) != DW_AT_addr_base)
      continue;
    DWARFFormValue form_value;
    if (attributes.ExtractFormValueAtIndex(i, form_value)) {
      SetAddrBase(form_value.Unsigned());
      break;
    }
  }

  for (size_t i = 0; i < attributes.Size(); ++i) {
    dw_attr_t attr = attributes.AttributeAtIndex(i);
    DWARFFormValue form_value;
    if (!attributes.ExtractFormValueAtIndex(i, form_value))
      continue;
    switch (attr) {
    default:
      break;
    case DW_AT_loclists_base:
      SetLoclistsBase(form_value.Unsigned());
      break;
    case DW_AT_rnglists_base:
      SetRangesBase(form_value.Unsigned());
      break;
    case DW_AT_str_offsets_base:
      SetStrOffsetsBase(form_value.Unsigned());
      break;
    case DW_AT_low_pc:
      SetBaseAddress(form_value.Address());
      break;
    case DW_AT_entry_pc:
      // If the value was already set by DW_AT_low_pc, don't update it.
      if (m_base_addr == LLDB_INVALID_ADDRESS)
        SetBaseAddress(form_value.Address());
      break;
    case DW_AT_stmt_list:
      m_line_table_offset = form_value.Unsigned();
      break;
    case DW_AT_GNU_addr_base:
      m_gnu_addr_base = form_value.Unsigned();
      break;
    case DW_AT_GNU_ranges_base:
      m_gnu_ranges_base = form_value.Unsigned();
      break;
    case DW_AT_GNU_dwo_id:
      m_dwo_id = form_value.Unsigned();
      break;
    }
  }

  if (m_is_dwo) {
    m_has_parsed_non_skeleton_unit = true;
    SetDwoStrOffsetsBase();
    return;
  }
}

size_t DWARFUnit::GetDebugInfoSize() const {
  return GetLengthByteSize() + GetLength() - GetHeaderByteSize();
}

const llvm::DWARFAbbreviationDeclarationSet *
DWARFUnit::GetAbbreviations() const {
  return m_abbrevs;
}

dw_offset_t DWARFUnit::GetAbbrevOffset() const {
  return m_abbrevs ? m_abbrevs->getOffset() : DW_INVALID_OFFSET;
}

dw_offset_t DWARFUnit::GetLineTableOffset() {
  ExtractUnitDIENoDwoIfNeeded();
  return m_line_table_offset;
}

void DWARFUnit::SetAddrBase(dw_addr_t addr_base) { m_addr_base = addr_base; }

// Parse the rangelist table header, including the optional array of offsets
// following it (DWARF v5 and later).
template <typename ListTableType>
static llvm::Expected<ListTableType>
ParseListTableHeader(const llvm::DWARFDataExtractor &data, uint64_t offset,
                     DwarfFormat format) {
  // We are expected to be called with Offset 0 or pointing just past the table
  // header. Correct Offset in the latter case so that it points to the start
  // of the header.
  if (offset == 0) {
    // This means DW_AT_rnglists_base is missing and therefore DW_FORM_rnglistx
    // cannot be handled. Returning a default-constructed ListTableType allows
    // DW_FORM_sec_offset to be supported.
    return ListTableType();
  }

  uint64_t HeaderSize = llvm::DWARFListTableHeader::getHeaderSize(format);
  if (offset < HeaderSize)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "did not detect a valid"
                                   " list table with base = 0x%" PRIx64 "\n",
                                   offset);
  offset -= HeaderSize;
  ListTableType Table;
  if (llvm::Error E = Table.extractHeaderAndOffsets(data, &offset))
    return std::move(E);
  return Table;
}

void DWARFUnit::SetLoclistsBase(dw_addr_t loclists_base) {
  uint64_t offset = 0;
  if (const llvm::DWARFUnitIndex::Entry *entry = m_header.getIndexEntry()) {
    const auto *contribution = entry->getContribution(llvm::DW_SECT_LOCLISTS);
    if (!contribution) {
      GetSymbolFileDWARF().GetObjectFile()->GetModule()->ReportError(
          "Failed to find location list contribution for CU with DWO Id "
          "{0:x16}",
          *GetDWOId());
      return;
    }
    offset += contribution->getOffset();
  }
  m_loclists_base = loclists_base;

  uint64_t header_size = llvm::DWARFListTableHeader::getHeaderSize(DWARF32);
  if (loclists_base < header_size)
    return;

  m_loclist_table_header.emplace(".debug_loclists", "locations");
  offset += loclists_base - header_size;
  if (llvm::Error E = m_loclist_table_header->extract(
          m_dwarf.GetDWARFContext().getOrLoadLocListsData().GetAsLLVMDWARF(),
          &offset)) {
    GetSymbolFileDWARF().GetObjectFile()->GetModule()->ReportError(
        "Failed to extract location list table at offset {0:x16} (location "
        "list base: {1:x16}): {2}",
        offset, loclists_base, toString(std::move(E)).c_str());
  }
}

std::unique_ptr<llvm::DWARFLocationTable>
DWARFUnit::GetLocationTable(const DataExtractor &data) const {
  llvm::DWARFDataExtractor llvm_data(
      data.GetData(), data.GetByteOrder() == lldb::eByteOrderLittle,
      data.GetAddressByteSize());

  if (m_is_dwo || GetVersion() >= 5)
    return std::make_unique<llvm::DWARFDebugLoclists>(llvm_data, GetVersion());
  return std::make_unique<llvm::DWARFDebugLoc>(llvm_data);
}

DWARFDataExtractor DWARFUnit::GetLocationData() const {
  DWARFContext &Ctx = GetSymbolFileDWARF().GetDWARFContext();
  const DWARFDataExtractor &data =
      GetVersion() >= 5 ? Ctx.getOrLoadLocListsData() : Ctx.getOrLoadLocData();
  if (const llvm::DWARFUnitIndex::Entry *entry = m_header.getIndexEntry()) {
    if (const auto *contribution = entry->getContribution(
            GetVersion() >= 5 ? llvm::DW_SECT_LOCLISTS : llvm::DW_SECT_EXT_LOC))
      return DWARFDataExtractor(data, contribution->getOffset(),
                                contribution->getLength32());
    return DWARFDataExtractor();
  }
  return data;
}

DWARFDataExtractor DWARFUnit::GetRnglistData() const {
  DWARFContext &Ctx = GetSymbolFileDWARF().GetDWARFContext();
  const DWARFDataExtractor &data = Ctx.getOrLoadRngListsData();
  if (const llvm::DWARFUnitIndex::Entry *entry = m_header.getIndexEntry()) {
    if (const auto *contribution =
            entry->getContribution(llvm::DW_SECT_RNGLISTS))
      return DWARFDataExtractor(data, contribution->getOffset(),
                                contribution->getLength32());
    GetSymbolFileDWARF().GetObjectFile()->GetModule()->ReportError(
        "Failed to find range list contribution for CU with signature {0:x16}",
        entry->getSignature());

    return DWARFDataExtractor();
  }
  return data;
}

void DWARFUnit::SetRangesBase(dw_addr_t ranges_base) {
  lldbassert(!m_rnglist_table_done);

  m_ranges_base = ranges_base;
}

const std::optional<llvm::DWARFDebugRnglistTable> &
DWARFUnit::GetRnglistTable() {
  if (GetVersion() >= 5 && !m_rnglist_table_done) {
    m_rnglist_table_done = true;
    if (auto table_or_error =
            ParseListTableHeader<llvm::DWARFDebugRnglistTable>(
                GetRnglistData().GetAsLLVMDWARF(), m_ranges_base, DWARF32))
      m_rnglist_table = std::move(table_or_error.get());
    else
      GetSymbolFileDWARF().GetObjectFile()->GetModule()->ReportError(
          "Failed to extract range list table at offset {0:x16}: {1}",
          m_ranges_base, toString(table_or_error.takeError()).c_str());
  }
  return m_rnglist_table;
}

// This function is called only for DW_FORM_rnglistx.
llvm::Expected<uint64_t> DWARFUnit::GetRnglistOffset(uint32_t Index) {
  if (!GetRnglistTable())
    return llvm::createStringError(std::errc::invalid_argument,
                                   "missing or invalid range list table");
  if (!m_ranges_base)
    return llvm::createStringError(
        std::errc::invalid_argument,
        llvm::formatv("DW_FORM_rnglistx cannot be used without "
                      "DW_AT_rnglists_base for CU at {0:x16}",
                      GetOffset())
            .str()
            .c_str());
  if (std::optional<uint64_t> off = GetRnglistTable()->getOffsetEntry(
          GetRnglistData().GetAsLLVM(), Index))
    return *off + m_ranges_base;
  return llvm::createStringError(
      std::errc::invalid_argument,
      "invalid range list table index %u; OffsetEntryCount is %u, "
      "DW_AT_rnglists_base is %" PRIu64,
      Index, GetRnglistTable()->getOffsetEntryCount(), m_ranges_base);
}

void DWARFUnit::SetStrOffsetsBase(dw_offset_t str_offsets_base) {
  m_str_offsets_base = str_offsets_base;
}

dw_addr_t DWARFUnit::ReadAddressFromDebugAddrSection(uint32_t index) const {
  uint32_t index_size = GetAddressByteSize();
  dw_offset_t addr_base = GetAddrBase();
  dw_addr_t offset = addr_base + static_cast<dw_addr_t>(index) * index_size;
  const DWARFDataExtractor &data =
      m_dwarf.GetDWARFContext().getOrLoadAddrData();
  if (data.ValidOffsetForDataOfSize(offset, index_size))
    return data.GetMaxU64_unchecked(&offset, index_size);
  return LLDB_INVALID_ADDRESS;
}

// It may be called only with m_die_array_mutex held R/W.
void DWARFUnit::ClearDIEsRWLocked() {
  m_die_array.clear();
  m_die_array.shrink_to_fit();

  if (m_dwo && !m_dwo->m_cancel_scopes)
    m_dwo->ClearDIEsRWLocked();
}

lldb::ByteOrder DWARFUnit::GetByteOrder() const {
  return m_dwarf.GetObjectFile()->GetByteOrder();
}

void DWARFUnit::SetBaseAddress(dw_addr_t base_addr) { m_base_addr = base_addr; }

// Compare function DWARFDebugAranges::Range structures
static bool CompareDIEOffset(const DWARFDebugInfoEntry &die,
                             const dw_offset_t die_offset) {
  return die.GetOffset() < die_offset;
}

// GetDIE()
//
// Get the DIE (Debug Information Entry) with the specified offset by first
// checking if the DIE is contained within this compile unit and grabbing the
// DIE from this compile unit. Otherwise we grab the DIE from the DWARF file.
DWARFDIE
DWARFUnit::GetDIE(dw_offset_t die_offset) {
  if (die_offset == DW_INVALID_OFFSET)
    return DWARFDIE(); // Not found

  if (!ContainsDIEOffset(die_offset)) {
    GetSymbolFileDWARF().GetObjectFile()->GetModule()->ReportError(
        "GetDIE for DIE {0:x16} is outside of its CU {0:x16}", die_offset,
        GetOffset());
    return DWARFDIE(); // Not found
  }

  ExtractDIEsIfNeeded();
  DWARFDebugInfoEntry::const_iterator end = m_die_array.cend();
  DWARFDebugInfoEntry::const_iterator pos =
      lower_bound(m_die_array.cbegin(), end, die_offset, CompareDIEOffset);

  if (pos != end && die_offset == (*pos).GetOffset())
    return DWARFDIE(this, &(*pos));
  return DWARFDIE(); // Not found
}

llvm::StringRef DWARFUnit::PeekDIEName(dw_offset_t die_offset) {
  DWARFDebugInfoEntry die;
  if (!die.Extract(GetData(), *this, &die_offset))
    return llvm::StringRef();

  // Does die contain a DW_AT_Name?
  if (const char *name =
          die.GetAttributeValueAsString(this, DW_AT_name, nullptr))
    return name;

  // Does its DW_AT_specification or DW_AT_abstract_origin contain an AT_Name?
  for (auto attr : {DW_AT_specification, DW_AT_abstract_origin}) {
    DWARFFormValue form_value;
    if (!die.GetAttributeValue(this, attr, form_value))
      continue;
    auto [unit, offset] = form_value.ReferencedUnitAndOffset();
    if (unit)
      if (auto name = unit->PeekDIEName(offset); !name.empty())
        return name;
  }

  return llvm::StringRef();
}

DWARFUnit &DWARFUnit::GetNonSkeletonUnit() {
  ExtractUnitDIEIfNeeded();
  if (m_dwo)
    return *m_dwo;
  return *this;
}

uint8_t DWARFUnit::GetAddressByteSize(const DWARFUnit *cu) {
  if (cu)
    return cu->GetAddressByteSize();
  return DWARFUnit::GetDefaultAddressSize();
}

uint8_t DWARFUnit::GetDefaultAddressSize() { return 4; }

DWARFCompileUnit *DWARFUnit::GetSkeletonUnit() {
  if (m_skeleton_unit == nullptr && IsDWOUnit()) {
    SymbolFileDWARFDwo *dwo =
        llvm::dyn_cast_or_null<SymbolFileDWARFDwo>(&GetSymbolFileDWARF());
    // Do a reverse lookup if the skeleton compile unit wasn't set.
    if (dwo)
      m_skeleton_unit = dwo->GetBaseSymbolFile().GetSkeletonUnit(this);
  }
  return llvm::dyn_cast_or_null<DWARFCompileUnit>(m_skeleton_unit);
}

void DWARFUnit::SetSkeletonUnit(DWARFUnit *skeleton_unit) {
  // If someone is re-setting the skeleton compile unit backlink, make sure
  // it is setting it to a valid value when it wasn't valid, or if the
  // value in m_skeleton_unit was valid, it should be the same value.
  assert(skeleton_unit);
  assert(m_skeleton_unit == nullptr || m_skeleton_unit == skeleton_unit);
  m_skeleton_unit = skeleton_unit;
}

bool DWARFUnit::Supports_DW_AT_APPLE_objc_complete_type() {
  return GetProducer() != eProducerLLVMGCC;
}

bool DWARFUnit::DW_AT_decl_file_attributes_are_invalid() {
  // llvm-gcc makes completely invalid decl file attributes and won't ever be
  // fixed, so we need to know to ignore these.
  return GetProducer() == eProducerLLVMGCC;
}

bool DWARFUnit::Supports_unnamed_objc_bitfields() {
  if (GetProducer() == eProducerClang)
    return GetProducerVersion() >= llvm::VersionTuple(425, 0, 13);
  // Assume all other compilers didn't have incorrect ObjC bitfield info.
  return true;
}

void DWARFUnit::ParseProducerInfo() {
  m_producer = eProducerOther;
  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
  if (!die)
    return;

  llvm::StringRef producer(
      die->GetAttributeValueAsString(this, DW_AT_producer, nullptr));
  if (producer.empty())
    return;

  static const RegularExpression g_swiftlang_version_regex(
      llvm::StringRef(R"(swiftlang-([0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?))"));
  static const RegularExpression g_clang_version_regex(
      llvm::StringRef(R"(clang-([0-9]+\.[0-9]+\.[0-9]+(\.[0-9]+)?))"));
  static const RegularExpression g_llvm_gcc_regex(
      llvm::StringRef(R"(4\.[012]\.[01] )"
                      R"(\(Based on Apple Inc\. build [0-9]+\) )"
                      R"(\(LLVM build [\.0-9]+\)$)"));

  llvm::SmallVector<llvm::StringRef, 3> matches;
  if (g_swiftlang_version_regex.Execute(producer, &matches)) {
    m_producer_version.tryParse(matches[1]);
    m_producer = eProducerSwift;
  } else if (producer.contains("clang")) {
    if (g_clang_version_regex.Execute(producer, &matches))
      m_producer_version.tryParse(matches[1]);
    m_producer = eProducerClang;
  } else if (producer.contains("GNU")) {
    m_producer = eProducerGCC;
  } else if (g_llvm_gcc_regex.Execute(producer)) {
    m_producer = eProducerLLVMGCC;
  }
}

DWARFProducer DWARFUnit::GetProducer() {
  if (m_producer == eProducerInvalid)
    ParseProducerInfo();
  return m_producer;
}

llvm::VersionTuple DWARFUnit::GetProducerVersion() {
  if (m_producer_version.empty())
    ParseProducerInfo();
  return m_producer_version;
}

uint64_t DWARFUnit::GetDWARFLanguageType() {
  if (m_language_type)
    return *m_language_type;

  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
  if (!die)
    m_language_type = 0;
  else
    m_language_type = die->GetAttributeValueAsUnsigned(this, DW_AT_language, 0);
  return *m_language_type;
}

bool DWARFUnit::GetIsOptimized() {
  if (m_is_optimized == eLazyBoolCalculate) {
    const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
    if (die) {
      m_is_optimized = eLazyBoolNo;
      if (die->GetAttributeValueAsUnsigned(this, DW_AT_APPLE_optimized, 0) ==
          1) {
        m_is_optimized = eLazyBoolYes;
      }
    }
  }
  return m_is_optimized == eLazyBoolYes;
}

FileSpec::Style DWARFUnit::GetPathStyle() {
  if (!m_comp_dir)
    ComputeCompDirAndGuessPathStyle();
  return m_comp_dir->GetPathStyle();
}

const FileSpec &DWARFUnit::GetCompilationDirectory() {
  if (!m_comp_dir)
    ComputeCompDirAndGuessPathStyle();
  return *m_comp_dir;
}

const FileSpec &DWARFUnit::GetAbsolutePath() {
  if (!m_file_spec)
    ComputeAbsolutePath();
  return *m_file_spec;
}

FileSpec DWARFUnit::GetFile(size_t file_idx) {
  return m_dwarf.GetFile(*this, file_idx);
}

// DWARF2/3 suggests the form hostname:pathname for compilation directory.
// Remove the host part if present.
static llvm::StringRef
removeHostnameFromPathname(llvm::StringRef path_from_dwarf) {
  if (!path_from_dwarf.contains(':'))
    return path_from_dwarf;
  llvm::StringRef host, path;
  std::tie(host, path) = path_from_dwarf.split(':');

  if (host.contains('/'))
    return path_from_dwarf;

  // check whether we have a windows path, and so the first character is a
  // drive-letter not a hostname.
  if (host.size() == 1 && llvm::isAlpha(host[0]) &&
      (path.starts_with("\\") || path.starts_with("/")))
    return path_from_dwarf;

  return path;
}

void DWARFUnit::ComputeCompDirAndGuessPathStyle() {
  m_comp_dir = FileSpec();
  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
  if (!die)
    return;

  llvm::StringRef comp_dir = removeHostnameFromPathname(
      die->GetAttributeValueAsString(this, DW_AT_comp_dir, nullptr));
  if (!comp_dir.empty()) {
    FileSpec::Style comp_dir_style =
        FileSpec::GuessPathStyle(comp_dir).value_or(FileSpec::Style::native);
    m_comp_dir = FileSpec(comp_dir, comp_dir_style);
  } else {
    // Try to detect the style based on the DW_AT_name attribute, but just store
    // the detected style in the m_comp_dir field.
    const char *name =
        die->GetAttributeValueAsString(this, DW_AT_name, nullptr);
    m_comp_dir = FileSpec(
        "", FileSpec::GuessPathStyle(name).value_or(FileSpec::Style::native));
  }
}

void DWARFUnit::ComputeAbsolutePath() {
  m_file_spec = FileSpec();
  const DWARFDebugInfoEntry *die = GetUnitDIEPtrOnly();
  if (!die)
    return;

  m_file_spec =
      FileSpec(die->GetAttributeValueAsString(this, DW_AT_name, nullptr),
               GetPathStyle());

  if (m_file_spec->IsRelative())
    m_file_spec->MakeAbsolute(GetCompilationDirectory());
}

SymbolFileDWARFDwo *DWARFUnit::GetDwoSymbolFile(bool load_all_debug_info) {
  if (load_all_debug_info)
    ExtractUnitDIEIfNeeded();
  if (m_dwo)
    return &llvm::cast<SymbolFileDWARFDwo>(m_dwo->GetSymbolFileDWARF());
  return nullptr;
}

const DWARFDebugAranges &DWARFUnit::GetFunctionAranges() {
  if (m_func_aranges_up == nullptr) {
    m_func_aranges_up = std::make_unique<DWARFDebugAranges>();
    const DWARFDebugInfoEntry *die = DIEPtr();
    if (die)
      die->BuildFunctionAddressRangeTable(this, m_func_aranges_up.get());

    if (m_dwo) {
      const DWARFDebugInfoEntry *dwo_die = m_dwo->DIEPtr();
      if (dwo_die)
        dwo_die->BuildFunctionAddressRangeTable(m_dwo.get(),
                                                m_func_aranges_up.get());
    }

    const bool minimize = false;
    m_func_aranges_up->Sort(minimize);
  }
  return *m_func_aranges_up;
}

llvm::Expected<DWARFUnitSP>
DWARFUnit::extract(SymbolFileDWARF &dwarf, user_id_t uid,
                   const DWARFDataExtractor &debug_info,
                   DIERef::Section section, lldb::offset_t *offset_ptr) {
  assert(debug_info.ValidOffset(*offset_ptr));

  DWARFContext &context = dwarf.GetDWARFContext();

  // FIXME: Either properly map between DIERef::Section and
  // llvm::DWARFSectionKind or switch to llvm's definition entirely.
  llvm::DWARFSectionKind section_kind_llvm =
      section == DIERef::Section::DebugInfo
          ? llvm::DWARFSectionKind::DW_SECT_INFO
          : llvm::DWARFSectionKind::DW_SECT_EXT_TYPES;

  llvm::DWARFDataExtractor debug_info_llvm = debug_info.GetAsLLVMDWARF();
  llvm::DWARFUnitHeader header;
  if (llvm::Error extract_err = header.extract(
          context.GetAsLLVM(), debug_info_llvm, offset_ptr, section_kind_llvm))
    return std::move(extract_err);

  if (context.isDwo()) {
    const llvm::DWARFUnitIndex::Entry *entry = nullptr;
    const llvm::DWARFUnitIndex &index = header.isTypeUnit()
                                            ? context.GetAsLLVM().getTUIndex()
                                            : context.GetAsLLVM().getCUIndex();
    if (index) {
      if (header.isTypeUnit())
        entry = index.getFromHash(header.getTypeHash());
      else if (auto dwo_id = header.getDWOId())
        entry = index.getFromHash(*dwo_id);
    }
    if (!entry)
      entry = index.getFromOffset(header.getOffset());
    if (entry)
      if (llvm::Error err = header.applyIndexEntry(entry))
        return std::move(err);
  }

  const llvm::DWARFDebugAbbrev *abbr = dwarf.DebugAbbrev();
  if (!abbr)
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "No debug_abbrev data");

  bool abbr_offset_OK =
      dwarf.GetDWARFContext().getOrLoadAbbrevData().ValidOffset(
          header.getAbbrOffset());
  if (!abbr_offset_OK)
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "Abbreviation offset for unit is not valid");

  llvm::Expected<const llvm::DWARFAbbreviationDeclarationSet *> abbrevs_or_err =
      abbr->getAbbreviationDeclarationSet(header.getAbbrOffset());
  if (!abbrevs_or_err)
    return abbrevs_or_err.takeError();

  const llvm::DWARFAbbreviationDeclarationSet *abbrevs = *abbrevs_or_err;
  if (!abbrevs)
    return llvm::make_error<llvm::object::GenericBinaryError>(
        "No abbrev exists at the specified offset.");

  bool is_dwo = dwarf.GetDWARFContext().isDwo();
  if (header.isTypeUnit())
    return DWARFUnitSP(
        new DWARFTypeUnit(dwarf, uid, header, *abbrevs, section, is_dwo));
  return DWARFUnitSP(
      new DWARFCompileUnit(dwarf, uid, header, *abbrevs, section, is_dwo));
}

const lldb_private::DWARFDataExtractor &DWARFUnit::GetData() const {
  return m_section == DIERef::Section::DebugTypes
             ? m_dwarf.GetDWARFContext().getOrLoadDebugTypesData()
             : m_dwarf.GetDWARFContext().getOrLoadDebugInfoData();
}

uint32_t DWARFUnit::GetHeaderByteSize() const {
  switch (m_header.getUnitType()) {
  case llvm::dwarf::DW_UT_compile:
  case llvm::dwarf::DW_UT_partial:
    return GetVersion() < 5 ? 11 : 12;
  case llvm::dwarf::DW_UT_skeleton:
  case llvm::dwarf::DW_UT_split_compile:
    return 20;
  case llvm::dwarf::DW_UT_type:
  case llvm::dwarf::DW_UT_split_type:
    return GetVersion() < 5 ? 23 : 24;
  }
  llvm_unreachable("invalid UnitType.");
}

std::optional<uint64_t>
DWARFUnit::GetStringOffsetSectionItem(uint32_t index) const {
  offset_t offset = GetStrOffsetsBase() + index * 4;
  return m_dwarf.GetDWARFContext().getOrLoadStrOffsetsData().GetU32(&offset);
}

llvm::Expected<DWARFRangeList>
DWARFUnit::FindRnglistFromOffset(dw_offset_t offset) {
  if (GetVersion() <= 4) {
    const DWARFDebugRanges *debug_ranges = m_dwarf.GetDebugRanges();
    if (!debug_ranges)
      return llvm::make_error<llvm::object::GenericBinaryError>(
          "No debug_ranges section");
    return debug_ranges->FindRanges(this, offset);
  }

  if (!GetRnglistTable())
    return llvm::createStringError(std::errc::invalid_argument,
                                   "missing or invalid range list table");

  llvm::DWARFDataExtractor data = GetRnglistData().GetAsLLVMDWARF();

  // As DW_AT_rnglists_base may be missing we need to call setAddressSize.
  data.setAddressSize(m_header.getAddressByteSize());
  auto range_list_or_error = GetRnglistTable()->findList(data, offset);
  if (!range_list_or_error)
    return range_list_or_error.takeError();

  llvm::Expected<llvm::DWARFAddressRangesVector> llvm_ranges =
      range_list_or_error->getAbsoluteRanges(
          llvm::object::SectionedAddress{GetBaseAddress()},
          GetAddressByteSize(), [&](uint32_t index) {
            uint32_t index_size = GetAddressByteSize();
            dw_offset_t addr_base = GetAddrBase();
            lldb::offset_t offset =
                addr_base + static_cast<lldb::offset_t>(index) * index_size;
            return llvm::object::SectionedAddress{
                m_dwarf.GetDWARFContext().getOrLoadAddrData().GetMaxU64(
                    &offset, index_size)};
          });
  if (!llvm_ranges)
    return llvm_ranges.takeError();

  DWARFRangeList ranges;
  for (const llvm::DWARFAddressRange &llvm_range : *llvm_ranges) {
    ranges.Append(DWARFRangeList::Entry(llvm_range.LowPC,
                                        llvm_range.HighPC - llvm_range.LowPC));
  }
  ranges.Sort();
  return ranges;
}

llvm::Expected<DWARFRangeList> DWARFUnit::FindRnglistFromIndex(uint32_t index) {
  llvm::Expected<uint64_t> maybe_offset = GetRnglistOffset(index);
  if (!maybe_offset)
    return maybe_offset.takeError();
  return FindRnglistFromOffset(*maybe_offset);
}

bool DWARFUnit::HasAny(llvm::ArrayRef<dw_tag_t> tags) {
  ExtractUnitDIEIfNeeded();
  if (m_dwo)
    return m_dwo->HasAny(tags);

  for (const auto &die : m_die_array) {
    for (const auto tag : tags) {
      if (tag == die.Tag())
        return true;
    }
  }
  return false;
}
