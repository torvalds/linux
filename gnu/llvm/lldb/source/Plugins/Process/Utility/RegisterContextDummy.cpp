//===-- RegisterContextDummy.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Value.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/FuncUnwinders.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-private.h"

#include "RegisterContextDummy.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextDummy::RegisterContextDummy(Thread &thread,
                                           uint32_t concrete_frame_idx,
                                           uint32_t address_byte_size)
    : RegisterContext(thread, concrete_frame_idx) {
  m_reg_set0.name = "General Purpose Registers";
  m_reg_set0.short_name = "GPR";
  m_reg_set0.num_registers = 1;
  m_reg_set0.registers = new uint32_t(0);

  m_pc_reg_info.name = "pc";
  m_pc_reg_info.alt_name = "pc";
  m_pc_reg_info.byte_offset = 0;
  m_pc_reg_info.byte_size = address_byte_size;
  m_pc_reg_info.encoding = eEncodingUint;
  m_pc_reg_info.format = eFormatPointer;
  m_pc_reg_info.invalidate_regs = nullptr;
  m_pc_reg_info.value_regs = nullptr;
  m_pc_reg_info.kinds[eRegisterKindEHFrame] = LLDB_INVALID_REGNUM;
  m_pc_reg_info.kinds[eRegisterKindDWARF] = LLDB_INVALID_REGNUM;
  m_pc_reg_info.kinds[eRegisterKindGeneric] = LLDB_REGNUM_GENERIC_PC;
  m_pc_reg_info.kinds[eRegisterKindProcessPlugin] = LLDB_INVALID_REGNUM;
  m_pc_reg_info.kinds[eRegisterKindLLDB] = LLDB_INVALID_REGNUM;
}

RegisterContextDummy::~RegisterContextDummy() {
  delete m_reg_set0.registers;
  delete m_pc_reg_info.invalidate_regs;
  delete m_pc_reg_info.value_regs;
}

void RegisterContextDummy::InvalidateAllRegisters() {}

size_t RegisterContextDummy::GetRegisterCount() { return 1; }

const lldb_private::RegisterInfo *
RegisterContextDummy::GetRegisterInfoAtIndex(size_t reg) {
  if (reg)
    return nullptr;
  return &m_pc_reg_info;
}

size_t RegisterContextDummy::GetRegisterSetCount() { return 1; }

const lldb_private::RegisterSet *
RegisterContextDummy::GetRegisterSet(size_t reg_set) {
  if (reg_set)
    return nullptr;
  return &m_reg_set0;
}

bool RegisterContextDummy::ReadRegister(
    const lldb_private::RegisterInfo *reg_info,
    lldb_private::RegisterValue &value) {
  if (!reg_info)
    return false;
  uint32_t reg_number = reg_info->kinds[eRegisterKindGeneric];
  if (reg_number == LLDB_REGNUM_GENERIC_PC) {
    value.SetUInt(LLDB_INVALID_ADDRESS, reg_info->byte_size);
    return true;
  }
  return false;
}

bool RegisterContextDummy::WriteRegister(
    const lldb_private::RegisterInfo *reg_info,
    const lldb_private::RegisterValue &value) {
  return false;
}

bool RegisterContextDummy::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  return false;
}

bool RegisterContextDummy::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  return false;
}

uint32_t RegisterContextDummy::ConvertRegisterKindToRegisterNumber(
    lldb::RegisterKind kind, uint32_t num) {
  if (kind == eRegisterKindGeneric && num == LLDB_REGNUM_GENERIC_PC)
    return 0;
  return LLDB_INVALID_REGNUM;
}
