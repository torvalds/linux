//===-- UnwindPlan.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/UnwindPlan.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

bool UnwindPlan::Row::RegisterLocation::
operator==(const UnwindPlan::Row::RegisterLocation &rhs) const {
  if (m_type == rhs.m_type) {
    switch (m_type) {
    case unspecified:
    case undefined:
    case same:
      return true;

    case atCFAPlusOffset:
    case isCFAPlusOffset:
    case atAFAPlusOffset:
    case isAFAPlusOffset:
      return m_location.offset == rhs.m_location.offset;

    case inOtherRegister:
      return m_location.reg_num == rhs.m_location.reg_num;

    case atDWARFExpression:
    case isDWARFExpression:
      if (m_location.expr.length == rhs.m_location.expr.length)
        return !memcmp(m_location.expr.opcodes, rhs.m_location.expr.opcodes,
                       m_location.expr.length);
      break;
    }
  }
  return false;
}

// This function doesn't copy the dwarf expression bytes; they must remain in
// allocated memory for the lifespan of this UnwindPlan object.
void UnwindPlan::Row::RegisterLocation::SetAtDWARFExpression(
    const uint8_t *opcodes, uint32_t len) {
  m_type = atDWARFExpression;
  m_location.expr.opcodes = opcodes;
  m_location.expr.length = len;
}

// This function doesn't copy the dwarf expression bytes; they must remain in
// allocated memory for the lifespan of this UnwindPlan object.
void UnwindPlan::Row::RegisterLocation::SetIsDWARFExpression(
    const uint8_t *opcodes, uint32_t len) {
  m_type = isDWARFExpression;
  m_location.expr.opcodes = opcodes;
  m_location.expr.length = len;
}

static std::optional<std::pair<lldb::ByteOrder, uint32_t>>
GetByteOrderAndAddrSize(Thread *thread) {
  if (!thread)
    return std::nullopt;
  ProcessSP process_sp = thread->GetProcess();
  if (!process_sp)
    return std::nullopt;
  ArchSpec arch = process_sp->GetTarget().GetArchitecture();
  return std::make_pair(arch.GetByteOrder(), arch.GetAddressByteSize());
}

static void DumpDWARFExpr(Stream &s, llvm::ArrayRef<uint8_t> expr, Thread *thread) {
  if (auto order_and_width = GetByteOrderAndAddrSize(thread)) {
    llvm::DataExtractor data(expr, order_and_width->first == eByteOrderLittle,
                             order_and_width->second);
    llvm::DWARFExpression(data, order_and_width->second, llvm::dwarf::DWARF32)
        .print(s.AsRawOstream(), llvm::DIDumpOptions(), nullptr);
  } else
    s.PutCString("dwarf-expr");
}

void UnwindPlan::Row::RegisterLocation::Dump(Stream &s,
                                             const UnwindPlan *unwind_plan,
                                             const UnwindPlan::Row *row,
                                             Thread *thread,
                                             bool verbose) const {
  switch (m_type) {
  case unspecified:
    if (verbose)
      s.PutCString("=<unspec>");
    else
      s.PutCString("=!");
    break;
  case undefined:
    if (verbose)
      s.PutCString("=<undef>");
    else
      s.PutCString("=?");
    break;
  case same:
    s.PutCString("= <same>");
    break;

  case atCFAPlusOffset:
  case isCFAPlusOffset: {
    s.PutChar('=');
    if (m_type == atCFAPlusOffset)
      s.PutChar('[');
    s.Printf("CFA%+d", m_location.offset);
    if (m_type == atCFAPlusOffset)
      s.PutChar(']');
  } break;

  case atAFAPlusOffset:
  case isAFAPlusOffset: {
    s.PutChar('=');
    if (m_type == atAFAPlusOffset)
      s.PutChar('[');
    s.Printf("AFA%+d", m_location.offset);
    if (m_type == atAFAPlusOffset)
      s.PutChar(']');
  } break;

  case inOtherRegister: {
    const RegisterInfo *other_reg_info = nullptr;
    if (unwind_plan)
      other_reg_info = unwind_plan->GetRegisterInfo(thread, m_location.reg_num);
    if (other_reg_info)
      s.Printf("=%s", other_reg_info->name);
    else
      s.Printf("=reg(%u)", m_location.reg_num);
  } break;

  case atDWARFExpression:
  case isDWARFExpression: {
    s.PutChar('=');
    if (m_type == atDWARFExpression)
      s.PutChar('[');
    DumpDWARFExpr(
        s, llvm::ArrayRef(m_location.expr.opcodes, m_location.expr.length),
        thread);
    if (m_type == atDWARFExpression)
      s.PutChar(']');
  } break;
  }
}

static void DumpRegisterName(Stream &s, const UnwindPlan *unwind_plan,
                             Thread *thread, uint32_t reg_num) {
  const RegisterInfo *reg_info = unwind_plan->GetRegisterInfo(thread, reg_num);
  if (reg_info)
    s.PutCString(reg_info->name);
  else
    s.Printf("reg(%u)", reg_num);
}

bool UnwindPlan::Row::FAValue::
operator==(const UnwindPlan::Row::FAValue &rhs) const {
  if (m_type == rhs.m_type) {
    switch (m_type) {
    case unspecified:
    case isRaSearch:
      return m_value.ra_search_offset == rhs.m_value.ra_search_offset;

    case isRegisterPlusOffset:
      return m_value.reg.offset == rhs.m_value.reg.offset;

    case isRegisterDereferenced:
      return m_value.reg.reg_num == rhs.m_value.reg.reg_num;

    case isDWARFExpression:
      if (m_value.expr.length == rhs.m_value.expr.length)
        return !memcmp(m_value.expr.opcodes, rhs.m_value.expr.opcodes,
                       m_value.expr.length);
      break;
    }
  }
  return false;
}

void UnwindPlan::Row::FAValue::Dump(Stream &s, const UnwindPlan *unwind_plan,
                                     Thread *thread) const {
  switch (m_type) {
  case isRegisterPlusOffset:
    DumpRegisterName(s, unwind_plan, thread, m_value.reg.reg_num);
    s.Printf("%+3d", m_value.reg.offset);
    break;
  case isRegisterDereferenced:
    s.PutChar('[');
    DumpRegisterName(s, unwind_plan, thread, m_value.reg.reg_num);
    s.PutChar(']');
    break;
  case isDWARFExpression:
    DumpDWARFExpr(s, llvm::ArrayRef(m_value.expr.opcodes, m_value.expr.length),
                  thread);
    break;
  case unspecified:
    s.PutCString("unspecified");
    break;
  case isRaSearch:
    s.Printf("RaSearch@SP%+d", m_value.ra_search_offset);
    break;
  }
}

void UnwindPlan::Row::Clear() {
  m_cfa_value.SetUnspecified();
  m_afa_value.SetUnspecified();
  m_offset = 0;
  m_unspecified_registers_are_undefined = false;
  m_register_locations.clear();
}

void UnwindPlan::Row::Dump(Stream &s, const UnwindPlan *unwind_plan,
                           Thread *thread, addr_t base_addr) const {
  if (base_addr != LLDB_INVALID_ADDRESS)
    s.Printf("0x%16.16" PRIx64 ": CFA=", base_addr + GetOffset());
  else
    s.Printf("%4" PRId64 ": CFA=", GetOffset());

  m_cfa_value.Dump(s, unwind_plan, thread);

  if (!m_afa_value.IsUnspecified()) {
    s.Printf(" AFA=");
    m_afa_value.Dump(s, unwind_plan, thread);
  }

  s.Printf(" => ");
  for (collection::const_iterator idx = m_register_locations.begin();
       idx != m_register_locations.end(); ++idx) {
    DumpRegisterName(s, unwind_plan, thread, idx->first);
    const bool verbose = false;
    idx->second.Dump(s, unwind_plan, this, thread, verbose);
    s.PutChar(' ');
  }
}

UnwindPlan::Row::Row() : m_cfa_value(), m_afa_value(), m_register_locations() {}

bool UnwindPlan::Row::GetRegisterInfo(
    uint32_t reg_num,
    UnwindPlan::Row::RegisterLocation &register_location) const {
  collection::const_iterator pos = m_register_locations.find(reg_num);
  if (pos != m_register_locations.end()) {
    register_location = pos->second;
    return true;
  }
  if (m_unspecified_registers_are_undefined) {
    register_location.SetUndefined();
    return true;
  }
  return false;
}

void UnwindPlan::Row::RemoveRegisterInfo(uint32_t reg_num) {
  collection::const_iterator pos = m_register_locations.find(reg_num);
  if (pos != m_register_locations.end()) {
    m_register_locations.erase(pos);
  }
}

void UnwindPlan::Row::SetRegisterInfo(
    uint32_t reg_num,
    const UnwindPlan::Row::RegisterLocation register_location) {
  m_register_locations[reg_num] = register_location;
}

bool UnwindPlan::Row::SetRegisterLocationToAtCFAPlusOffset(uint32_t reg_num,
                                                           int32_t offset,
                                                           bool can_replace) {
  if (!can_replace &&
      m_register_locations.find(reg_num) != m_register_locations.end())
    return false;
  RegisterLocation reg_loc;
  reg_loc.SetAtCFAPlusOffset(offset);
  m_register_locations[reg_num] = reg_loc;
  return true;
}

bool UnwindPlan::Row::SetRegisterLocationToIsCFAPlusOffset(uint32_t reg_num,
                                                           int32_t offset,
                                                           bool can_replace) {
  if (!can_replace &&
      m_register_locations.find(reg_num) != m_register_locations.end())
    return false;
  RegisterLocation reg_loc;
  reg_loc.SetIsCFAPlusOffset(offset);
  m_register_locations[reg_num] = reg_loc;
  return true;
}

bool UnwindPlan::Row::SetRegisterLocationToUndefined(
    uint32_t reg_num, bool can_replace, bool can_replace_only_if_unspecified) {
  collection::iterator pos = m_register_locations.find(reg_num);
  collection::iterator end = m_register_locations.end();

  if (pos != end) {
    if (!can_replace)
      return false;
    if (can_replace_only_if_unspecified && !pos->second.IsUnspecified())
      return false;
  }
  RegisterLocation reg_loc;
  reg_loc.SetUndefined();
  m_register_locations[reg_num] = reg_loc;
  return true;
}

bool UnwindPlan::Row::SetRegisterLocationToUnspecified(uint32_t reg_num,
                                                       bool can_replace) {
  if (!can_replace &&
      m_register_locations.find(reg_num) != m_register_locations.end())
    return false;
  RegisterLocation reg_loc;
  reg_loc.SetUnspecified();
  m_register_locations[reg_num] = reg_loc;
  return true;
}

bool UnwindPlan::Row::SetRegisterLocationToRegister(uint32_t reg_num,
                                                    uint32_t other_reg_num,
                                                    bool can_replace) {
  if (!can_replace &&
      m_register_locations.find(reg_num) != m_register_locations.end())
    return false;
  RegisterLocation reg_loc;
  reg_loc.SetInRegister(other_reg_num);
  m_register_locations[reg_num] = reg_loc;
  return true;
}

bool UnwindPlan::Row::SetRegisterLocationToSame(uint32_t reg_num,
                                                bool must_replace) {
  if (must_replace &&
      m_register_locations.find(reg_num) == m_register_locations.end())
    return false;
  RegisterLocation reg_loc;
  reg_loc.SetSame();
  m_register_locations[reg_num] = reg_loc;
  return true;
}

bool UnwindPlan::Row::operator==(const UnwindPlan::Row &rhs) const {
  return m_offset == rhs.m_offset && m_cfa_value == rhs.m_cfa_value &&
         m_afa_value == rhs.m_afa_value &&
         m_unspecified_registers_are_undefined ==
             rhs.m_unspecified_registers_are_undefined &&
         m_register_locations == rhs.m_register_locations;
}

void UnwindPlan::AppendRow(const UnwindPlan::RowSP &row_sp) {
  if (m_row_list.empty() ||
      m_row_list.back()->GetOffset() != row_sp->GetOffset())
    m_row_list.push_back(row_sp);
  else
    m_row_list.back() = row_sp;
}

void UnwindPlan::InsertRow(const UnwindPlan::RowSP &row_sp,
                           bool replace_existing) {
  collection::iterator it = m_row_list.begin();
  while (it != m_row_list.end()) {
    RowSP row = *it;
    if (row->GetOffset() >= row_sp->GetOffset())
      break;
    it++;
  }
  if (it == m_row_list.end() || (*it)->GetOffset() != row_sp->GetOffset())
    m_row_list.insert(it, row_sp);
  else if (replace_existing)
    *it = row_sp;
}

UnwindPlan::RowSP UnwindPlan::GetRowForFunctionOffset(int offset) const {
  RowSP row;
  if (!m_row_list.empty()) {
    if (offset == -1)
      row = m_row_list.back();
    else {
      collection::const_iterator pos, end = m_row_list.end();
      for (pos = m_row_list.begin(); pos != end; ++pos) {
        if ((*pos)->GetOffset() <= static_cast<lldb::offset_t>(offset))
          row = *pos;
        else
          break;
      }
    }
  }
  return row;
}

bool UnwindPlan::IsValidRowIndex(uint32_t idx) const {
  return idx < m_row_list.size();
}

const UnwindPlan::RowSP UnwindPlan::GetRowAtIndex(uint32_t idx) const {
  if (idx < m_row_list.size())
    return m_row_list[idx];
  else {
    Log *log = GetLog(LLDBLog::Unwind);
    LLDB_LOGF(log,
              "error: UnwindPlan::GetRowAtIndex(idx = %u) invalid index "
              "(number rows is %u)",
              idx, (uint32_t)m_row_list.size());
    return UnwindPlan::RowSP();
  }
}

const UnwindPlan::RowSP UnwindPlan::GetLastRow() const {
  if (m_row_list.empty()) {
    Log *log = GetLog(LLDBLog::Unwind);
    LLDB_LOGF(log, "UnwindPlan::GetLastRow() when rows are empty");
    return UnwindPlan::RowSP();
  }
  return m_row_list.back();
}

int UnwindPlan::GetRowCount() const { return m_row_list.size(); }

void UnwindPlan::SetPlanValidAddressRange(const AddressRange &range) {
  if (range.GetBaseAddress().IsValid() && range.GetByteSize() != 0)
    m_plan_valid_address_range = range;
}

bool UnwindPlan::PlanValidAtAddress(Address addr) {
  // If this UnwindPlan has no rows, it is an invalid UnwindPlan.
  if (GetRowCount() == 0) {
    Log *log = GetLog(LLDBLog::Unwind);
    if (log) {
      StreamString s;
      if (addr.Dump(&s, nullptr, Address::DumpStyleSectionNameOffset)) {
        LLDB_LOGF(log,
                  "UnwindPlan is invalid -- no unwind rows for UnwindPlan "
                  "'%s' at address %s",
                  m_source_name.GetCString(), s.GetData());
      } else {
        LLDB_LOGF(log,
                  "UnwindPlan is invalid -- no unwind rows for UnwindPlan '%s'",
                  m_source_name.GetCString());
      }
    }
    return false;
  }

  // If the 0th Row of unwind instructions is missing, or if it doesn't provide
  // a register to use to find the Canonical Frame Address, this is not a valid
  // UnwindPlan.
  if (GetRowAtIndex(0).get() == nullptr ||
      GetRowAtIndex(0)->GetCFAValue().GetValueType() ==
          Row::FAValue::unspecified) {
    Log *log = GetLog(LLDBLog::Unwind);
    if (log) {
      StreamString s;
      if (addr.Dump(&s, nullptr, Address::DumpStyleSectionNameOffset)) {
        LLDB_LOGF(log,
                  "UnwindPlan is invalid -- no CFA register defined in row 0 "
                  "for UnwindPlan '%s' at address %s",
                  m_source_name.GetCString(), s.GetData());
      } else {
        LLDB_LOGF(log,
                  "UnwindPlan is invalid -- no CFA register defined in row 0 "
                  "for UnwindPlan '%s'",
                  m_source_name.GetCString());
      }
    }
    return false;
  }

  if (!m_plan_valid_address_range.GetBaseAddress().IsValid() ||
      m_plan_valid_address_range.GetByteSize() == 0)
    return true;

  if (!addr.IsValid())
    return true;

  if (m_plan_valid_address_range.ContainsFileAddress(addr))
    return true;

  return false;
}

void UnwindPlan::Dump(Stream &s, Thread *thread, lldb::addr_t base_addr) const {
  if (!m_source_name.IsEmpty()) {
    s.Printf("This UnwindPlan originally sourced from %s\n",
             m_source_name.GetCString());
  }
  if (m_lsda_address.IsValid() && m_personality_func_addr.IsValid()) {
    TargetSP target_sp(thread->CalculateTarget());
    addr_t lsda_load_addr = m_lsda_address.GetLoadAddress(target_sp.get());
    addr_t personality_func_load_addr =
        m_personality_func_addr.GetLoadAddress(target_sp.get());

    if (lsda_load_addr != LLDB_INVALID_ADDRESS &&
        personality_func_load_addr != LLDB_INVALID_ADDRESS) {
      s.Printf("LSDA address 0x%" PRIx64
               ", personality routine is at address 0x%" PRIx64 "\n",
               lsda_load_addr, personality_func_load_addr);
    }
  }
  s.Printf("This UnwindPlan is sourced from the compiler: ");
  switch (m_plan_is_sourced_from_compiler) {
  case eLazyBoolYes:
    s.Printf("yes.\n");
    break;
  case eLazyBoolNo:
    s.Printf("no.\n");
    break;
  case eLazyBoolCalculate:
    s.Printf("not specified.\n");
    break;
  }
  s.Printf("This UnwindPlan is valid at all instruction locations: ");
  switch (m_plan_is_valid_at_all_instruction_locations) {
  case eLazyBoolYes:
    s.Printf("yes.\n");
    break;
  case eLazyBoolNo:
    s.Printf("no.\n");
    break;
  case eLazyBoolCalculate:
    s.Printf("not specified.\n");
    break;
  }
  s.Printf("This UnwindPlan is for a trap handler function: ");
  switch (m_plan_is_for_signal_trap) {
  case eLazyBoolYes:
    s.Printf("yes.\n");
    break;
  case eLazyBoolNo:
    s.Printf("no.\n");
    break;
  case eLazyBoolCalculate:
    s.Printf("not specified.\n");
    break;
  }
  if (m_plan_valid_address_range.GetBaseAddress().IsValid() &&
      m_plan_valid_address_range.GetByteSize() > 0) {
    s.PutCString("Address range of this UnwindPlan: ");
    TargetSP target_sp(thread->CalculateTarget());
    m_plan_valid_address_range.Dump(&s, target_sp.get(),
                                    Address::DumpStyleSectionNameOffset);
    s.EOL();
  }
  collection::const_iterator pos, begin = m_row_list.begin(),
                                  end = m_row_list.end();
  for (pos = begin; pos != end; ++pos) {
    s.Printf("row[%u]: ", (uint32_t)std::distance(begin, pos));
    (*pos)->Dump(s, this, thread, base_addr);
    s.Printf("\n");
  }
}

void UnwindPlan::SetSourceName(const char *source) {
  m_source_name = ConstString(source);
}

ConstString UnwindPlan::GetSourceName() const { return m_source_name; }

const RegisterInfo *UnwindPlan::GetRegisterInfo(Thread *thread,
                                                uint32_t unwind_reg) const {
  if (thread) {
    RegisterContext *reg_ctx = thread->GetRegisterContext().get();
    if (reg_ctx) {
      uint32_t reg;
      if (m_register_kind == eRegisterKindLLDB)
        reg = unwind_reg;
      else
        reg = reg_ctx->ConvertRegisterKindToRegisterNumber(m_register_kind,
                                                           unwind_reg);
      if (reg != LLDB_INVALID_REGNUM)
        return reg_ctx->GetRegisterInfoAtIndex(reg);
    }
  }
  return nullptr;
}
