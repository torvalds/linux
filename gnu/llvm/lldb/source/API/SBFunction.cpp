//===-- SBFunction.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBFunction.h"
#include "lldb/API/SBAddressRange.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBFunction::SBFunction() { LLDB_INSTRUMENT_VA(this); }

SBFunction::SBFunction(lldb_private::Function *lldb_object_ptr)
    : m_opaque_ptr(lldb_object_ptr) {}

SBFunction::SBFunction(const lldb::SBFunction &rhs)
    : m_opaque_ptr(rhs.m_opaque_ptr) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBFunction &SBFunction::operator=(const SBFunction &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

SBFunction::~SBFunction() { m_opaque_ptr = nullptr; }

bool SBFunction::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBFunction::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr != nullptr;
}

const char *SBFunction::GetName() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetName().AsCString();

  return nullptr;
}

const char *SBFunction::GetDisplayName() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetMangled().GetDisplayDemangledName().AsCString();

  return nullptr;
}

const char *SBFunction::GetMangledName() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetMangled().GetMangledName().AsCString();
  return nullptr;
}

bool SBFunction::operator==(const SBFunction &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool SBFunction::operator!=(const SBFunction &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr != rhs.m_opaque_ptr;
}

bool SBFunction::GetDescription(SBStream &s) {
  LLDB_INSTRUMENT_VA(this, s);

  if (m_opaque_ptr) {
    s.Printf("SBFunction: id = 0x%8.8" PRIx64 ", name = %s",
             m_opaque_ptr->GetID(), m_opaque_ptr->GetName().AsCString());
    Type *func_type = m_opaque_ptr->GetType();
    if (func_type)
      s.Printf(", type = %s", func_type->GetName().AsCString());
    return true;
  }
  s.Printf("No value");
  return false;
}

SBInstructionList SBFunction::GetInstructions(SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  return GetInstructions(target, nullptr);
}

SBInstructionList SBFunction::GetInstructions(SBTarget target,
                                              const char *flavor) {
  LLDB_INSTRUMENT_VA(this, target, flavor);

  SBInstructionList sb_instructions;
  if (m_opaque_ptr) {
    TargetSP target_sp(target.GetSP());
    std::unique_lock<std::recursive_mutex> lock;
    ModuleSP module_sp(
        m_opaque_ptr->GetAddressRange().GetBaseAddress().GetModule());
    if (target_sp && module_sp) {
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
      const bool force_live_memory = true;
      sb_instructions.SetDisassembler(Disassembler::DisassembleRange(
          module_sp->GetArchitecture(), nullptr, flavor, *target_sp,
          m_opaque_ptr->GetAddressRange(), force_live_memory));
    }
  }
  return sb_instructions;
}

lldb_private::Function *SBFunction::get() { return m_opaque_ptr; }

void SBFunction::reset(lldb_private::Function *lldb_object_ptr) {
  m_opaque_ptr = lldb_object_ptr;
}

SBAddress SBFunction::GetStartAddress() {
  LLDB_INSTRUMENT_VA(this);

  SBAddress addr;
  if (m_opaque_ptr)
    addr.SetAddress(m_opaque_ptr->GetAddressRange().GetBaseAddress());
  return addr;
}

SBAddress SBFunction::GetEndAddress() {
  LLDB_INSTRUMENT_VA(this);

  SBAddress addr;
  if (m_opaque_ptr) {
    addr_t byte_size = m_opaque_ptr->GetAddressRange().GetByteSize();
    if (byte_size > 0) {
      addr.SetAddress(m_opaque_ptr->GetAddressRange().GetBaseAddress());
      addr->Slide(byte_size);
    }
  }
  return addr;
}

lldb::SBAddressRangeList SBFunction::GetRanges() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBAddressRangeList ranges;
  if (m_opaque_ptr) {
    lldb::SBAddressRange range;
    (*range.m_opaque_up) = m_opaque_ptr->GetAddressRange();
    ranges.Append(std::move(range));
  }

  return ranges;
}

const char *SBFunction::GetArgumentName(uint32_t arg_idx) {
  LLDB_INSTRUMENT_VA(this, arg_idx);

  if (!m_opaque_ptr)
    return nullptr;

  Block &block = m_opaque_ptr->GetBlock(true);
  VariableListSP variable_list_sp = block.GetBlockVariableList(true);
  if (!variable_list_sp)
    return nullptr;

  VariableList arguments;
  variable_list_sp->AppendVariablesWithScope(eValueTypeVariableArgument,
                                             arguments, true);
  lldb::VariableSP variable_sp = arguments.GetVariableAtIndex(arg_idx);
  if (!variable_sp)
    return nullptr;

  return variable_sp->GetName().GetCString();
}

uint32_t SBFunction::GetPrologueByteSize() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetPrologueByteSize();
  return 0;
}

SBType SBFunction::GetType() {
  LLDB_INSTRUMENT_VA(this);

  SBType sb_type;
  if (m_opaque_ptr) {
    Type *function_type = m_opaque_ptr->GetType();
    if (function_type)
      sb_type.ref().SetType(function_type->shared_from_this());
  }
  return sb_type;
}

SBBlock SBFunction::GetBlock() {
  LLDB_INSTRUMENT_VA(this);

  SBBlock sb_block;
  if (m_opaque_ptr)
    sb_block.SetPtr(&m_opaque_ptr->GetBlock(true));
  return sb_block;
}

lldb::LanguageType SBFunction::GetLanguage() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr) {
    if (m_opaque_ptr->GetCompileUnit())
      return m_opaque_ptr->GetCompileUnit()->GetLanguage();
  }
  return lldb::eLanguageTypeUnknown;
}

bool SBFunction::GetIsOptimized() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr) {
    if (m_opaque_ptr->GetCompileUnit())
      return m_opaque_ptr->GetCompileUnit()->GetIsOptimized();
  }
  return false;
}
