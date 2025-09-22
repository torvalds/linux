//===-- DWARFExpressionList.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/DWARFExpressionList.h"
#include "Plugins/SymbolFile/DWARF/DWARFUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLoc.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"

using namespace lldb;
using namespace lldb_private;

bool DWARFExpressionList::IsAlwaysValidSingleExpr() const {
  return GetAlwaysValidExpr() != nullptr;
}

const DWARFExpression * DWARFExpressionList::GetAlwaysValidExpr() const {
  if (m_exprs.GetSize() != 1)
    return nullptr;
  const auto *expr = m_exprs.GetEntryAtIndex(0);
  if (expr->base == 0 && expr->size == LLDB_INVALID_ADDRESS)
    return &expr->data;
  return nullptr;
}

bool DWARFExpressionList::AddExpression(addr_t base, addr_t end,
                                        DWARFExpression expr) {
  if (IsAlwaysValidSingleExpr() || base >= end)
    return false;
  m_exprs.Append({base, end - base, expr});
  return true;
}

bool DWARFExpressionList::GetExpressionData(DataExtractor &data,
                                            lldb::addr_t func_load_addr,
                                            lldb::addr_t file_addr) const {
  if (const DWARFExpression *expr =
          GetExpressionAtAddress(func_load_addr, file_addr))
    return expr->GetExpressionData(data);
  return false;
}

bool DWARFExpressionList::ContainsAddress(lldb::addr_t func_load_addr,
                                          lldb::addr_t addr) const {
  if (IsAlwaysValidSingleExpr())
    return true;
  return GetExpressionAtAddress(func_load_addr, addr) != nullptr;
}

const DWARFExpression *
DWARFExpressionList::GetExpressionAtAddress(lldb::addr_t func_load_addr,
                                            lldb::addr_t load_addr) const {
  if (const DWARFExpression *expr = GetAlwaysValidExpr())
    return expr;
  if (func_load_addr == LLDB_INVALID_ADDRESS)
    func_load_addr = m_func_file_addr;
  addr_t addr = load_addr - func_load_addr + m_func_file_addr;
  uint32_t index = m_exprs.FindEntryIndexThatContains(addr);
  if (index == UINT32_MAX)
    return nullptr;
  return &m_exprs.GetEntryAtIndex(index)->data;
}

DWARFExpression *
DWARFExpressionList::GetMutableExpressionAtAddress(lldb::addr_t func_load_addr,
                                                   lldb::addr_t load_addr) {
  if (IsAlwaysValidSingleExpr())
    return &m_exprs.GetMutableEntryAtIndex(0)->data;
  if (func_load_addr == LLDB_INVALID_ADDRESS)
    func_load_addr = m_func_file_addr;
  addr_t addr = load_addr - func_load_addr + m_func_file_addr;
  uint32_t index = m_exprs.FindEntryIndexThatContains(addr);
  if (index == UINT32_MAX)
    return nullptr;
  return &m_exprs.GetMutableEntryAtIndex(index)->data;
}

bool DWARFExpressionList::ContainsThreadLocalStorage() const {
  // We are assuming for now that any thread local variable will not have a
  // location list. This has been true for all thread local variables we have
  // seen so far produced by any compiler.
  if (!IsAlwaysValidSingleExpr())
    return false;

  const DWARFExpression &expr = m_exprs.GetEntryRef(0).data;
  return expr.ContainsThreadLocalStorage(m_dwarf_cu);
}

bool DWARFExpressionList::LinkThreadLocalStorage(
    lldb::ModuleSP new_module_sp,
    std::function<lldb::addr_t(lldb::addr_t file_addr)> const
        &link_address_callback) {
  // We are assuming for now that any thread local variable will not have a
  // location list. This has been true for all thread local variables we have
  // seen so far produced by any compiler.
  if (!IsAlwaysValidSingleExpr())
    return false;

  DWARFExpression &expr = m_exprs.GetEntryRef(0).data;
  // If we linked the TLS address correctly, update the module so that when the
  // expression is evaluated it can resolve the file address to a load address
  // and read the TLS data
  if (expr.LinkThreadLocalStorage(m_dwarf_cu, link_address_callback))
    m_module_wp = new_module_sp;
  return true;
}

bool DWARFExpressionList::MatchesOperand(
    StackFrame &frame, const Instruction::Operand &operand) const {
  RegisterContextSP reg_ctx_sp = frame.GetRegisterContext();
  if (!reg_ctx_sp) {
    return false;
  }
  const DWARFExpression *expr = nullptr;
  if (IsAlwaysValidSingleExpr())
    expr = &m_exprs.GetEntryAtIndex(0)->data;
  else {
    SymbolContext sc = frame.GetSymbolContext(eSymbolContextFunction);
    if (!sc.function)
      return false;

    addr_t load_function_start =
        sc.function->GetAddressRange().GetBaseAddress().GetFileAddress();
    if (load_function_start == LLDB_INVALID_ADDRESS)
      return false;

    addr_t pc = frame.GetFrameCodeAddressForSymbolication().GetFileAddress();
    expr = GetExpressionAtAddress(LLDB_INVALID_ADDRESS, pc);
  }
  if (!expr)
    return false;
  return expr->MatchesOperand(frame, operand);
}

bool DWARFExpressionList::DumpLocations(Stream *s, lldb::DescriptionLevel level,
                                        lldb::addr_t func_load_addr,
                                        lldb::addr_t file_addr,
                                        ABI *abi) const {
  llvm::raw_ostream &os = s->AsRawOstream();
  llvm::ListSeparator separator;
  if (const DWARFExpression *expr = GetAlwaysValidExpr()) {
    expr->DumpLocation(s, level, abi);
    return true;
  }
  for (const Entry &entry : *this) {
    addr_t load_base = entry.GetRangeBase() + func_load_addr - m_func_file_addr;
    addr_t load_end = entry.GetRangeEnd() + func_load_addr - m_func_file_addr;
    if (file_addr != LLDB_INVALID_ADDRESS &&
        (file_addr < load_base || file_addr >= load_end))
      continue;
    const auto &expr = entry.data;
    DataExtractor data;
    expr.GetExpressionData(data);
    uint32_t addr_size = data.GetAddressByteSize();

    os << separator;
    os << "[";
    os << llvm::format_hex(load_base, 2 + 2 * addr_size);
    os << ", ";
    os << llvm::format_hex(load_end, 2 + 2 * addr_size);
    os << ") -> ";
    expr.DumpLocation(s, level, abi);
    if (file_addr != LLDB_INVALID_ADDRESS)
      break;
  }
  return true;
}

void DWARFExpressionList::GetDescription(Stream *s,
                                         lldb::DescriptionLevel level,
                                         ABI *abi) const {
  llvm::raw_ostream &os = s->AsRawOstream();
  if (IsAlwaysValidSingleExpr()) {
    m_exprs.Back()->data.DumpLocation(s, level, abi);
    return;
  }
  os << llvm::format("0x%8.8" PRIx64 ": ", 0);
  for (const Entry &entry : *this) {
    const auto &expr = entry.data;
    DataExtractor data;
    expr.GetExpressionData(data);
    uint32_t addr_size = data.GetAddressByteSize();
    os << "\n";
    os.indent(s->GetIndentLevel() + 2);
    os << "[";
    llvm::DWARFFormValue::dumpAddress(os, addr_size, entry.GetRangeBase());
    os << ", ";
    llvm::DWARFFormValue::dumpAddress(os, addr_size, entry.GetRangeEnd());
    os << "): ";
    expr.DumpLocation(s, level, abi);
  }
}

llvm::Expected<Value> DWARFExpressionList::Evaluate(
    ExecutionContext *exe_ctx, RegisterContext *reg_ctx,
    lldb::addr_t func_load_addr, const Value *initial_value_ptr,
    const Value *object_address_ptr) const {
  ModuleSP module_sp = m_module_wp.lock();
  DataExtractor data;
  RegisterKind reg_kind;
  DWARFExpression expr;
  if (IsAlwaysValidSingleExpr()) {
    expr = m_exprs.Back()->data;
  } else {
    Address pc;
    StackFrame *frame = nullptr;
    if (!reg_ctx || !reg_ctx->GetPCForSymbolication(pc)) {
      if (exe_ctx)
        frame = exe_ctx->GetFramePtr();
      if (!frame)
        return llvm::createStringError("no frame");
      RegisterContextSP reg_ctx_sp = frame->GetRegisterContext();
      if (!reg_ctx_sp)
        return llvm::createStringError("no register context");
      reg_ctx_sp->GetPCForSymbolication(pc);
    }

    if (!pc.IsValid()) {
      return llvm::createStringError("Invalid PC in frame.");
    }
    addr_t pc_load_addr = pc.GetLoadAddress(exe_ctx->GetTargetPtr());
    const DWARFExpression *entry =
        GetExpressionAtAddress(func_load_addr, pc_load_addr);
    if (!entry)
      return llvm::createStringError("variable not available");
    expr = *entry;
  }
  expr.GetExpressionData(data);
  reg_kind = expr.GetRegisterKind();
  return DWARFExpression::Evaluate(exe_ctx, reg_ctx, module_sp, data,
                                   m_dwarf_cu, reg_kind, initial_value_ptr,
                                   object_address_ptr);
}
