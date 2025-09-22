//===-- SBAddress.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBAddress.h"
#include "Utils.h"
#include "lldb/API/SBProcess.h"
#include "lldb/API/SBSection.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBAddress::SBAddress() : m_opaque_up(new Address()) {
  LLDB_INSTRUMENT_VA(this);
}

SBAddress::SBAddress(const Address &address)
    : m_opaque_up(std::make_unique<Address>(address)) {}

SBAddress::SBAddress(const SBAddress &rhs) : m_opaque_up(new Address()) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBAddress::SBAddress(lldb::SBSection section, lldb::addr_t offset)
    : m_opaque_up(new Address(section.GetSP(), offset)) {
  LLDB_INSTRUMENT_VA(this, section, offset);
}

// Create an address by resolving a load address using the supplied target
SBAddress::SBAddress(lldb::addr_t load_addr, lldb::SBTarget &target)
    : m_opaque_up(new Address()) {
  LLDB_INSTRUMENT_VA(this, load_addr, target);

  SetLoadAddress(load_addr, target);
}

SBAddress::~SBAddress() = default;

const SBAddress &SBAddress::operator=(const SBAddress &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

bool lldb::operator==(const SBAddress &lhs, const SBAddress &rhs) {
  if (lhs.IsValid() && rhs.IsValid())
    return lhs.ref() == rhs.ref();
  return false;
}

bool SBAddress::operator!=(const SBAddress &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return !(*this == rhs);
}

bool SBAddress::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBAddress::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up != nullptr && m_opaque_up->IsValid();
}

void SBAddress::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_opaque_up = std::make_unique<Address>();
}

void SBAddress::SetAddress(lldb::SBSection section, lldb::addr_t offset) {
  LLDB_INSTRUMENT_VA(this, section, offset);

  Address &addr = ref();
  addr.SetSection(section.GetSP());
  addr.SetOffset(offset);
}

void SBAddress::SetAddress(const Address &address) { ref() = address; }

lldb::addr_t SBAddress::GetFileAddress() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up->IsValid())
    return m_opaque_up->GetFileAddress();
  else
    return LLDB_INVALID_ADDRESS;
}

lldb::addr_t SBAddress::GetLoadAddress(const SBTarget &target) const {
  LLDB_INSTRUMENT_VA(this, target);

  lldb::addr_t addr = LLDB_INVALID_ADDRESS;
  TargetSP target_sp(target.GetSP());
  if (target_sp) {
    if (m_opaque_up->IsValid()) {
      std::lock_guard<std::recursive_mutex> guard(target_sp->GetAPIMutex());
      addr = m_opaque_up->GetLoadAddress(target_sp.get());
    }
  }

  return addr;
}

void SBAddress::SetLoadAddress(lldb::addr_t load_addr, lldb::SBTarget &target) {
  LLDB_INSTRUMENT_VA(this, load_addr, target);

  // Create the address object if we don't already have one
  ref();
  if (target.IsValid())
    *this = target.ResolveLoadAddress(load_addr);
  else
    m_opaque_up->Clear();

  // Check if we weren't were able to resolve a section offset address. If we
  // weren't it is ok, the load address might be a location on the stack or
  // heap, so we should just have an address with no section and a valid offset
  if (!m_opaque_up->IsValid())
    m_opaque_up->SetOffset(load_addr);
}

bool SBAddress::OffsetAddress(addr_t offset) {
  LLDB_INSTRUMENT_VA(this, offset);

  if (m_opaque_up->IsValid()) {
    addr_t addr_offset = m_opaque_up->GetOffset();
    if (addr_offset != LLDB_INVALID_ADDRESS) {
      m_opaque_up->SetOffset(addr_offset + offset);
      return true;
    }
  }
  return false;
}

lldb::SBSection SBAddress::GetSection() {
  LLDB_INSTRUMENT_VA(this);

  lldb::SBSection sb_section;
  if (m_opaque_up->IsValid())
    sb_section.SetSP(m_opaque_up->GetSection());
  return sb_section;
}

lldb::addr_t SBAddress::GetOffset() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up->IsValid())
    return m_opaque_up->GetOffset();
  return 0;
}

Address *SBAddress::operator->() { return m_opaque_up.get(); }

const Address *SBAddress::operator->() const { return m_opaque_up.get(); }

Address &SBAddress::ref() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<Address>();
  return *m_opaque_up;
}

const Address &SBAddress::ref() const {
  // This object should already have checked with "IsValid()" prior to calling
  // this function. In case you didn't we will assert and die to let you know.
  assert(m_opaque_up.get());
  return *m_opaque_up;
}

Address *SBAddress::get() { return m_opaque_up.get(); }

bool SBAddress::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  // Call "ref()" on the stream to make sure it creates a backing stream in
  // case there isn't one already...
  Stream &strm = description.ref();
  if (m_opaque_up->IsValid()) {
    m_opaque_up->Dump(&strm, nullptr, Address::DumpStyleResolvedDescription,
                      Address::DumpStyleModuleWithFileAddress, 4);
  } else
    strm.PutCString("No value");

  return true;
}

SBModule SBAddress::GetModule() {
  LLDB_INSTRUMENT_VA(this);

  SBModule sb_module;
  if (m_opaque_up->IsValid())
    sb_module.SetSP(m_opaque_up->GetModule());
  return sb_module;
}

SBSymbolContext SBAddress::GetSymbolContext(uint32_t resolve_scope) {
  LLDB_INSTRUMENT_VA(this, resolve_scope);

  SBSymbolContext sb_sc;
  SymbolContextItem scope = static_cast<SymbolContextItem>(resolve_scope);
  if (m_opaque_up->IsValid())
    m_opaque_up->CalculateSymbolContext(&sb_sc.ref(), scope);
  return sb_sc;
}

SBCompileUnit SBAddress::GetCompileUnit() {
  LLDB_INSTRUMENT_VA(this);

  SBCompileUnit sb_comp_unit;
  if (m_opaque_up->IsValid())
    sb_comp_unit.reset(m_opaque_up->CalculateSymbolContextCompileUnit());
  return sb_comp_unit;
}

SBFunction SBAddress::GetFunction() {
  LLDB_INSTRUMENT_VA(this);

  SBFunction sb_function;
  if (m_opaque_up->IsValid())
    sb_function.reset(m_opaque_up->CalculateSymbolContextFunction());
  return sb_function;
}

SBBlock SBAddress::GetBlock() {
  LLDB_INSTRUMENT_VA(this);

  SBBlock sb_block;
  if (m_opaque_up->IsValid())
    sb_block.SetPtr(m_opaque_up->CalculateSymbolContextBlock());
  return sb_block;
}

SBSymbol SBAddress::GetSymbol() {
  LLDB_INSTRUMENT_VA(this);

  SBSymbol sb_symbol;
  if (m_opaque_up->IsValid())
    sb_symbol.reset(m_opaque_up->CalculateSymbolContextSymbol());
  return sb_symbol;
}

SBLineEntry SBAddress::GetLineEntry() {
  LLDB_INSTRUMENT_VA(this);

  SBLineEntry sb_line_entry;
  if (m_opaque_up->IsValid()) {
    LineEntry line_entry;
    if (m_opaque_up->CalculateSymbolContextLineEntry(line_entry))
      sb_line_entry.SetLineEntry(line_entry);
  }
  return sb_line_entry;
}
