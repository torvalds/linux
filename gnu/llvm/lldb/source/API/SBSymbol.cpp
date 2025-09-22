//===-- SBSymbol.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBSymbol.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBSymbol::SBSymbol() { LLDB_INSTRUMENT_VA(this); }

SBSymbol::SBSymbol(lldb_private::Symbol *lldb_object_ptr)
    : m_opaque_ptr(lldb_object_ptr) {}

SBSymbol::SBSymbol(const lldb::SBSymbol &rhs) : m_opaque_ptr(rhs.m_opaque_ptr) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

const SBSymbol &SBSymbol::operator=(const SBSymbol &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_ptr = rhs.m_opaque_ptr;
  return *this;
}

SBSymbol::~SBSymbol() { m_opaque_ptr = nullptr; }

void SBSymbol::SetSymbol(lldb_private::Symbol *lldb_object_ptr) {
  m_opaque_ptr = lldb_object_ptr;
}

bool SBSymbol::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBSymbol::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_ptr != nullptr;
}

const char *SBSymbol::GetName() const {
  LLDB_INSTRUMENT_VA(this);

  const char *name = nullptr;
  if (m_opaque_ptr)
    name = m_opaque_ptr->GetName().AsCString();

  return name;
}

const char *SBSymbol::GetDisplayName() const {
  LLDB_INSTRUMENT_VA(this);

  const char *name = nullptr;
  if (m_opaque_ptr)
    name = m_opaque_ptr->GetMangled().GetDisplayDemangledName().AsCString();

  return name;
}

const char *SBSymbol::GetMangledName() const {
  LLDB_INSTRUMENT_VA(this);

  const char *name = nullptr;
  if (m_opaque_ptr)
    name = m_opaque_ptr->GetMangled().GetMangledName().AsCString();
  return name;
}

bool SBSymbol::operator==(const SBSymbol &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr == rhs.m_opaque_ptr;
}

bool SBSymbol::operator!=(const SBSymbol &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return m_opaque_ptr != rhs.m_opaque_ptr;
}

bool SBSymbol::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  if (m_opaque_ptr) {
    m_opaque_ptr->GetDescription(&strm, lldb::eDescriptionLevelFull, nullptr);
  } else
    strm.PutCString("No value");

  return true;
}

SBInstructionList SBSymbol::GetInstructions(SBTarget target) {
  LLDB_INSTRUMENT_VA(this, target);

  return GetInstructions(target, nullptr);
}

SBInstructionList SBSymbol::GetInstructions(SBTarget target,
                                            const char *flavor_string) {
  LLDB_INSTRUMENT_VA(this, target, flavor_string);

  SBInstructionList sb_instructions;
  if (m_opaque_ptr) {
    TargetSP target_sp(target.GetSP());
    std::unique_lock<std::recursive_mutex> lock;
    if (target_sp && m_opaque_ptr->ValueIsAddress()) {
      lock = std::unique_lock<std::recursive_mutex>(target_sp->GetAPIMutex());
      const Address &symbol_addr = m_opaque_ptr->GetAddressRef();
      ModuleSP module_sp = symbol_addr.GetModule();
      if (module_sp) {
        AddressRange symbol_range(symbol_addr, m_opaque_ptr->GetByteSize());
        const bool force_live_memory = true;
        sb_instructions.SetDisassembler(Disassembler::DisassembleRange(
            module_sp->GetArchitecture(), nullptr, flavor_string, *target_sp,
            symbol_range, force_live_memory));
      }
    }
  }
  return sb_instructions;
}

lldb_private::Symbol *SBSymbol::get() { return m_opaque_ptr; }

void SBSymbol::reset(lldb_private::Symbol *symbol) { m_opaque_ptr = symbol; }

SBAddress SBSymbol::GetStartAddress() {
  LLDB_INSTRUMENT_VA(this);

  SBAddress addr;
  if (m_opaque_ptr && m_opaque_ptr->ValueIsAddress()) {
    addr.SetAddress(m_opaque_ptr->GetAddressRef());
  }
  return addr;
}

SBAddress SBSymbol::GetEndAddress() {
  LLDB_INSTRUMENT_VA(this);

  SBAddress addr;
  if (m_opaque_ptr && m_opaque_ptr->ValueIsAddress()) {
    lldb::addr_t range_size = m_opaque_ptr->GetByteSize();
    if (range_size > 0) {
      addr.SetAddress(m_opaque_ptr->GetAddressRef());
      addr->Slide(m_opaque_ptr->GetByteSize());
    }
  }
  return addr;
}

uint64_t SBSymbol::GetValue() {
  LLDB_INSTRUMENT_VA(this);
  if (m_opaque_ptr)
    return m_opaque_ptr->GetRawValue();
  return 0;
}

uint64_t SBSymbol::GetSize() {
  LLDB_INSTRUMENT_VA(this);
  if (m_opaque_ptr && m_opaque_ptr->GetByteSizeIsValid())
    return m_opaque_ptr->GetByteSize();
  return 0;
}

uint32_t SBSymbol::GetPrologueByteSize() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetPrologueByteSize();
  return 0;
}

SymbolType SBSymbol::GetType() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->GetType();
  return eSymbolTypeInvalid;
}

bool SBSymbol::IsExternal() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->IsExternal();
  return false;
}

bool SBSymbol::IsSynthetic() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_ptr)
    return m_opaque_ptr->IsSynthetic();
  return false;
}
