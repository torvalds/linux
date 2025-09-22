//===-- DWARFFormValue.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <optional>

#include "lldb/Core/Module.h"
#include "lldb/Core/dwarf.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/Stream.h"

#include "DWARFDebugInfo.h"
#include "DWARFFormValue.h"
#include "DWARFUnit.h"

using namespace lldb_private;
using namespace lldb_private::dwarf;
using namespace lldb_private::plugin::dwarf;

void DWARFFormValue::Clear() {
  m_unit = nullptr;
  m_form = dw_form_t(0);
  m_value = ValueTypeTag();
}

bool DWARFFormValue::ExtractValue(const DWARFDataExtractor &data,
                                  lldb::offset_t *offset_ptr) {
  if (m_form == DW_FORM_implicit_const)
    return true;

  bool indirect = false;
  bool is_block = false;
  m_value.data = nullptr;
  uint8_t ref_addr_size;
  // Read the value for the form into value and follow and DW_FORM_indirect
  // instances we run into
  do {
    indirect = false;
    switch (m_form) {
    case DW_FORM_addr:
      assert(m_unit);
      m_value.value.uval =
          data.GetMaxU64(offset_ptr, DWARFUnit::GetAddressByteSize(m_unit));
      break;
    case DW_FORM_block1:
      m_value.value.uval = data.GetU8(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_block2:
      m_value.value.uval = data.GetU16(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_block4:
      m_value.value.uval = data.GetU32(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_data16:
      m_value.value.uval = 16;
      is_block = true;
      break;
    case DW_FORM_exprloc:
    case DW_FORM_block:
      m_value.value.uval = data.GetULEB128(offset_ptr);
      is_block = true;
      break;
    case DW_FORM_string:
      m_value.value.cstr = data.GetCStr(offset_ptr);
      break;
    case DW_FORM_sdata:
      m_value.value.sval = data.GetSLEB128(offset_ptr);
      break;
    case DW_FORM_strp:
    case DW_FORM_line_strp:
    case DW_FORM_sec_offset:
      m_value.value.uval = data.GetMaxU64(offset_ptr, 4);
      break;
    case DW_FORM_addrx1:
    case DW_FORM_strx1:
    case DW_FORM_ref1:
    case DW_FORM_data1:
    case DW_FORM_flag:
      m_value.value.uval = data.GetU8(offset_ptr);
      break;
    case DW_FORM_addrx2:
    case DW_FORM_strx2:
    case DW_FORM_ref2:
    case DW_FORM_data2:
      m_value.value.uval = data.GetU16(offset_ptr);
      break;
    case DW_FORM_addrx3:
    case DW_FORM_strx3:
      m_value.value.uval = data.GetMaxU64(offset_ptr, 3);
      break;
    case DW_FORM_addrx4:
    case DW_FORM_strx4:
    case DW_FORM_ref4:
    case DW_FORM_data4:
      m_value.value.uval = data.GetU32(offset_ptr);
      break;
    case DW_FORM_data8:
    case DW_FORM_ref8:
    case DW_FORM_ref_sig8:
      m_value.value.uval = data.GetU64(offset_ptr);
      break;
    case DW_FORM_addrx:
    case DW_FORM_loclistx:
    case DW_FORM_rnglistx:
    case DW_FORM_strx:
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
    case DW_FORM_GNU_str_index:
    case DW_FORM_GNU_addr_index:
      m_value.value.uval = data.GetULEB128(offset_ptr);
      break;
    case DW_FORM_ref_addr:
      assert(m_unit);
      if (m_unit->GetVersion() <= 2)
        ref_addr_size = m_unit->GetAddressByteSize();
      else
        ref_addr_size = 4;
      m_value.value.uval = data.GetMaxU64(offset_ptr, ref_addr_size);
      break;
    case DW_FORM_indirect:
      m_form = static_cast<dw_form_t>(data.GetULEB128(offset_ptr));
      indirect = true;
      break;
    case DW_FORM_flag_present:
      m_value.value.uval = 1;
      break;
    default:
      return false;
    }
  } while (indirect);

  if (is_block) {
    m_value.data = data.PeekData(*offset_ptr, m_value.value.uval);
    if (m_value.data != nullptr) {
      *offset_ptr += m_value.value.uval;
    }
  }

  return true;
}

struct FormSize {
  uint8_t valid:1, size:7;
};
static FormSize g_form_sizes[] = {
    {0, 0}, // 0x00 unused
    {0, 0}, // 0x01 DW_FORM_addr
    {0, 0}, // 0x02 unused
    {0, 0}, // 0x03 DW_FORM_block2
    {0, 0}, // 0x04 DW_FORM_block4
    {1, 2}, // 0x05 DW_FORM_data2
    {1, 4}, // 0x06 DW_FORM_data4
    {1, 8}, // 0x07 DW_FORM_data8
    {0, 0}, // 0x08 DW_FORM_string
    {0, 0}, // 0x09 DW_FORM_block
    {0, 0}, // 0x0a DW_FORM_block1
    {1, 1}, // 0x0b DW_FORM_data1
    {1, 1}, // 0x0c DW_FORM_flag
    {0, 0}, // 0x0d DW_FORM_sdata
    {1, 4}, // 0x0e DW_FORM_strp
    {0, 0}, // 0x0f DW_FORM_udata
    {0, 0}, // 0x10 DW_FORM_ref_addr (addr size for DWARF2 and earlier, 4 bytes
            // for DWARF32, 8 bytes for DWARF32 in DWARF 3 and later
    {1, 1},  // 0x11 DW_FORM_ref1
    {1, 2},  // 0x12 DW_FORM_ref2
    {1, 4},  // 0x13 DW_FORM_ref4
    {1, 8},  // 0x14 DW_FORM_ref8
    {0, 0},  // 0x15 DW_FORM_ref_udata
    {0, 0},  // 0x16 DW_FORM_indirect
    {1, 4},  // 0x17 DW_FORM_sec_offset
    {0, 0},  // 0x18 DW_FORM_exprloc
    {1, 0},  // 0x19 DW_FORM_flag_present
    {0, 0},  // 0x1a DW_FORM_strx (ULEB128)
    {0, 0},  // 0x1b DW_FORM_addrx (ULEB128)
    {1, 4},  // 0x1c DW_FORM_ref_sup4
    {0, 0},  // 0x1d DW_FORM_strp_sup (4 bytes for DWARF32, 8 bytes for DWARF64)
    {1, 16}, // 0x1e DW_FORM_data16
    {1, 4},  // 0x1f DW_FORM_line_strp
    {1, 8},  // 0x20 DW_FORM_ref_sig8
};

std::optional<uint8_t> DWARFFormValue::GetFixedSize(dw_form_t form,
                                                    const DWARFUnit *u) {
  if (form <= DW_FORM_ref_sig8 && g_form_sizes[form].valid)
    return static_cast<uint8_t>(g_form_sizes[form].size);
  if (form == DW_FORM_addr && u)
    return u->GetAddressByteSize();
  return std::nullopt;
}

std::optional<uint8_t> DWARFFormValue::GetFixedSize() const {
  return GetFixedSize(m_form, m_unit);
}

bool DWARFFormValue::SkipValue(const DWARFDataExtractor &debug_info_data,
                               lldb::offset_t *offset_ptr) const {
  return DWARFFormValue::SkipValue(m_form, debug_info_data, offset_ptr, m_unit);
}

bool DWARFFormValue::SkipValue(dw_form_t form,
                               const DWARFDataExtractor &debug_info_data,
                               lldb::offset_t *offset_ptr,
                               const DWARFUnit *unit) {
  uint8_t ref_addr_size;
  switch (form) {
  // Blocks if inlined data that have a length field and the data bytes inlined
  // in the .debug_info
  case DW_FORM_exprloc:
  case DW_FORM_block: {
    uint64_t size = debug_info_data.GetULEB128(offset_ptr);
    *offset_ptr += size;
  }
    return true;
  case DW_FORM_block1: {
    uint8_t size = debug_info_data.GetU8(offset_ptr);
    *offset_ptr += size;
  }
    return true;
  case DW_FORM_block2: {
    uint16_t size = debug_info_data.GetU16(offset_ptr);
    *offset_ptr += size;
  }
    return true;
  case DW_FORM_block4: {
    uint32_t size = debug_info_data.GetU32(offset_ptr);
    *offset_ptr += size;
  }
    return true;

  // Inlined NULL terminated C-strings
  case DW_FORM_string:
    debug_info_data.GetCStr(offset_ptr);
    return true;

  // Compile unit address sized values
  case DW_FORM_addr:
    *offset_ptr += DWARFUnit::GetAddressByteSize(unit);
    return true;

  case DW_FORM_ref_addr:
    ref_addr_size = 4;
    assert(unit); // Unit must be valid for DW_FORM_ref_addr objects or we will
                  // get this wrong
    if (unit->GetVersion() <= 2)
      ref_addr_size = unit->GetAddressByteSize();
    else
      ref_addr_size = 4;
    *offset_ptr += ref_addr_size;
    return true;

  // 0 bytes values (implied from DW_FORM)
  case DW_FORM_flag_present:
  case DW_FORM_implicit_const:
    return true;

    // 1 byte values
    case DW_FORM_addrx1:
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_ref1:
    case DW_FORM_strx1:
      *offset_ptr += 1;
      return true;

    // 2 byte values
    case DW_FORM_addrx2:
    case DW_FORM_data2:
    case DW_FORM_ref2:
    case DW_FORM_strx2:
      *offset_ptr += 2;
      return true;

    // 3 byte values
    case DW_FORM_addrx3:
    case DW_FORM_strx3:
      *offset_ptr += 3;
      return true;

    // 32 bit for DWARF 32, 64 for DWARF 64
    case DW_FORM_sec_offset:
    case DW_FORM_strp:
    case DW_FORM_line_strp:
      *offset_ptr += 4;
      return true;

    // 4 byte values
    case DW_FORM_addrx4:
    case DW_FORM_data4:
    case DW_FORM_ref4:
    case DW_FORM_strx4:
      *offset_ptr += 4;
      return true;

    // 8 byte values
    case DW_FORM_data8:
    case DW_FORM_ref8:
    case DW_FORM_ref_sig8:
      *offset_ptr += 8;
      return true;

    // signed or unsigned LEB 128 values
    case DW_FORM_addrx:
    case DW_FORM_loclistx:
    case DW_FORM_rnglistx:
    case DW_FORM_sdata:
    case DW_FORM_udata:
    case DW_FORM_ref_udata:
    case DW_FORM_GNU_addr_index:
    case DW_FORM_GNU_str_index:
    case DW_FORM_strx:
      debug_info_data.Skip_LEB128(offset_ptr);
      return true;

  case DW_FORM_indirect: {
      auto indirect_form =
          static_cast<dw_form_t>(debug_info_data.GetULEB128(offset_ptr));
      return DWARFFormValue::SkipValue(indirect_form, debug_info_data,
                                       offset_ptr, unit);
  }

  default:
    break;
  }
  return false;
}

void DWARFFormValue::Dump(Stream &s) const {
  uint64_t uvalue = Unsigned();
  bool unit_relative_offset = false;

  switch (m_form) {
  case DW_FORM_addr:
    DumpAddress(s.AsRawOstream(), uvalue, sizeof(uint64_t));
    break;
  case DW_FORM_flag:
  case DW_FORM_data1:
    s.PutHex8(uvalue);
    break;
  case DW_FORM_data2:
    s.PutHex16(uvalue);
    break;
  case DW_FORM_sec_offset:
  case DW_FORM_data4:
    s.PutHex32(uvalue);
    break;
  case DW_FORM_ref_sig8:
  case DW_FORM_data8:
    s.PutHex64(uvalue);
    break;
  case DW_FORM_string:
    s.QuotedCString(AsCString());
    break;
  case DW_FORM_exprloc:
  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_block2:
  case DW_FORM_block4:
    if (uvalue > 0) {
      switch (m_form) {
      case DW_FORM_exprloc:
      case DW_FORM_block:
        s.Printf("<0x%" PRIx64 "> ", uvalue);
        break;
      case DW_FORM_block1:
        s.Printf("<0x%2.2x> ", (uint8_t)uvalue);
        break;
      case DW_FORM_block2:
        s.Printf("<0x%4.4x> ", (uint16_t)uvalue);
        break;
      case DW_FORM_block4:
        s.Printf("<0x%8.8x> ", (uint32_t)uvalue);
        break;
      default:
        break;
      }

      const uint8_t *data_ptr = m_value.data;
      if (data_ptr) {
        const uint8_t *end_data_ptr =
            data_ptr + uvalue; // uvalue contains size of block
        while (data_ptr < end_data_ptr) {
          s.Printf("%2.2x ", *data_ptr);
          ++data_ptr;
        }
      } else
        s.PutCString("NULL");
    }
    break;

  case DW_FORM_sdata:
    s.PutSLEB128(uvalue);
    break;
  case DW_FORM_udata:
    s.PutULEB128(uvalue);
    break;
  case DW_FORM_strp:
  case DW_FORM_line_strp: {
    const char *dbg_str = AsCString();
    if (dbg_str) {
      s.QuotedCString(dbg_str);
    } else {
      s.PutHex32(uvalue);
    }
  } break;

  case DW_FORM_ref_addr: {
    assert(m_unit); // Unit must be valid for DW_FORM_ref_addr objects or we
                    // will get this wrong
    if (m_unit->GetVersion() <= 2)
      DumpAddress(s.AsRawOstream(), uvalue, sizeof(uint64_t) * 2);
    else
      DumpAddress(s.AsRawOstream(), uvalue,
                  4 * 2); // 4 for DWARF32, 8 for DWARF64, but we don't
                          // support DWARF64 yet
    break;
  }
  case DW_FORM_ref1:
    unit_relative_offset = true;
    break;
  case DW_FORM_ref2:
    unit_relative_offset = true;
    break;
  case DW_FORM_ref4:
    unit_relative_offset = true;
    break;
  case DW_FORM_ref8:
    unit_relative_offset = true;
    break;
  case DW_FORM_ref_udata:
    unit_relative_offset = true;
    break;

  // All DW_FORM_indirect attributes should be resolved prior to calling this
  // function
  case DW_FORM_indirect:
    s.PutCString("DW_FORM_indirect");
    break;
  case DW_FORM_flag_present:
    break;
  default:
    s.Printf("DW_FORM(0x%4.4x)", m_form);
    break;
  }

  if (unit_relative_offset) {
    assert(m_unit); // Unit must be valid for DW_FORM_ref forms that are compile
                    // unit relative or we will get this wrong
    s.Printf("{0x%8.8" PRIx64 "}", uvalue + m_unit->GetOffset());
  }
}

const char *DWARFFormValue::AsCString() const {
  DWARFContext &context = m_unit->GetSymbolFileDWARF().GetDWARFContext();

  if (m_form == DW_FORM_string)
    return m_value.value.cstr;
  if (m_form == DW_FORM_strp)
    return context.getOrLoadStrData().PeekCStr(m_value.value.uval);

  if (m_form == DW_FORM_GNU_str_index || m_form == DW_FORM_strx ||
      m_form == DW_FORM_strx1 || m_form == DW_FORM_strx2 ||
      m_form == DW_FORM_strx3 || m_form == DW_FORM_strx4) {

    std::optional<uint64_t> offset =
        m_unit->GetStringOffsetSectionItem(m_value.value.uval);
    if (!offset)
      return nullptr;
    return context.getOrLoadStrData().PeekCStr(*offset);
  }

  if (m_form == DW_FORM_line_strp)
    return context.getOrLoadLineStrData().PeekCStr(m_value.value.uval);

  return nullptr;
}

dw_addr_t DWARFFormValue::Address() const {
  SymbolFileDWARF &symbol_file = m_unit->GetSymbolFileDWARF();

  if (m_form == DW_FORM_addr)
    return Unsigned();

  assert(m_unit);
  assert(m_form == DW_FORM_GNU_addr_index || m_form == DW_FORM_addrx ||
         m_form == DW_FORM_addrx1 || m_form == DW_FORM_addrx2 ||
         m_form == DW_FORM_addrx3 || m_form == DW_FORM_addrx4);

  uint32_t index_size = m_unit->GetAddressByteSize();
  dw_offset_t addr_base = m_unit->GetAddrBase();
  lldb::offset_t offset = addr_base + m_value.value.uval * index_size;
  return symbol_file.GetDWARFContext().getOrLoadAddrData().GetMaxU64(
      &offset, index_size);
}

std::pair<DWARFUnit *, uint64_t>
DWARFFormValue::ReferencedUnitAndOffset() const {
  uint64_t value = m_value.value.uval;
  switch (m_form) {
  case DW_FORM_ref1:
  case DW_FORM_ref2:
  case DW_FORM_ref4:
  case DW_FORM_ref8:
  case DW_FORM_ref_udata:
    assert(m_unit); // Unit must be valid for DW_FORM_ref forms that are compile
                    // unit relative or we will get this wrong
    value += m_unit->GetOffset();
    if (!m_unit->ContainsDIEOffset(value)) {
      m_unit->GetSymbolFileDWARF().GetObjectFile()->GetModule()->ReportError(
          "DW_FORM_ref* DIE reference {0:x16} is outside of its CU", value);
      return {nullptr, 0};
    }
    return {const_cast<DWARFUnit *>(m_unit), value};

  case DW_FORM_ref_addr: {
    DWARFUnit *ref_cu =
        m_unit->GetSymbolFileDWARF().DebugInfo().GetUnitContainingDIEOffset(
            DIERef::Section::DebugInfo, value);
    if (!ref_cu) {
      m_unit->GetSymbolFileDWARF().GetObjectFile()->GetModule()->ReportError(
          "DW_FORM_ref_addr DIE reference {0:x16} has no matching CU", value);
      return {nullptr, 0};
    }
    return {ref_cu, value};
  }

  case DW_FORM_ref_sig8: {
    DWARFTypeUnit *tu =
        m_unit->GetSymbolFileDWARF().DebugInfo().GetTypeUnitForHash(value);
    if (!tu)
      return {nullptr, 0};
    return {tu, tu->GetTypeOffset()};
  }

  default:
    return {nullptr, 0};
  }
}

DWARFDIE DWARFFormValue::Reference() const {
  auto [unit, offset] = ReferencedUnitAndOffset();
  return unit ? unit->GetDIE(offset) : DWARFDIE();
}

uint64_t DWARFFormValue::Reference(dw_offset_t base_offset) const {
  uint64_t value = m_value.value.uval;
  switch (m_form) {
  case DW_FORM_ref1:
  case DW_FORM_ref2:
  case DW_FORM_ref4:
  case DW_FORM_ref8:
  case DW_FORM_ref_udata:
    return value + base_offset;

  case DW_FORM_ref_addr:
  case DW_FORM_ref_sig8:
  case DW_FORM_GNU_ref_alt:
    return value;

  default:
    return DW_INVALID_OFFSET;
  }
}

const uint8_t *DWARFFormValue::BlockData() const { return m_value.data; }

bool DWARFFormValue::IsBlockForm(const dw_form_t form) {
  switch (form) {
  case DW_FORM_exprloc:
  case DW_FORM_block:
  case DW_FORM_block1:
  case DW_FORM_block2:
  case DW_FORM_block4:
    return true;
  default:
    return false;
  }
  llvm_unreachable("All cases handled above!");
}

bool DWARFFormValue::IsDataForm(const dw_form_t form) {
  switch (form) {
  case DW_FORM_sdata:
  case DW_FORM_udata:
  case DW_FORM_data1:
  case DW_FORM_data2:
  case DW_FORM_data4:
  case DW_FORM_data8:
    return true;
  default:
    return false;
  }
  llvm_unreachable("All cases handled above!");
}

bool DWARFFormValue::FormIsSupported(dw_form_t form) {
  switch (form) {
    case DW_FORM_addr:
    case DW_FORM_addrx:
    case DW_FORM_loclistx:
    case DW_FORM_rnglistx:
    case DW_FORM_block2:
    case DW_FORM_block4:
    case DW_FORM_data2:
    case DW_FORM_data4:
    case DW_FORM_data8:
    case DW_FORM_string:
    case DW_FORM_block:
    case DW_FORM_block1:
    case DW_FORM_data1:
    case DW_FORM_flag:
    case DW_FORM_sdata:
    case DW_FORM_strp:
    case DW_FORM_line_strp:
    case DW_FORM_strx:
    case DW_FORM_strx1:
    case DW_FORM_strx2:
    case DW_FORM_strx3:
    case DW_FORM_strx4:
    case DW_FORM_udata:
    case DW_FORM_ref_addr:
    case DW_FORM_ref1:
    case DW_FORM_ref2:
    case DW_FORM_ref4:
    case DW_FORM_ref8:
    case DW_FORM_ref_udata:
    case DW_FORM_indirect:
    case DW_FORM_sec_offset:
    case DW_FORM_exprloc:
    case DW_FORM_flag_present:
    case DW_FORM_ref_sig8:
    case DW_FORM_GNU_str_index:
    case DW_FORM_GNU_addr_index:
    case DW_FORM_implicit_const:
      return true;
    default:
      break;
  }
  return false;
}
