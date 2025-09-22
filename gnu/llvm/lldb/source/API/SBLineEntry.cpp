//===-- SBLineEntry.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBLineEntry.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Symbol/LineEntry.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/StreamString.h"

#include <climits>

using namespace lldb;
using namespace lldb_private;

SBLineEntry::SBLineEntry() { LLDB_INSTRUMENT_VA(this); }

SBLineEntry::SBLineEntry(const SBLineEntry &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBLineEntry::SBLineEntry(const lldb_private::LineEntry *lldb_object_ptr) {
  if (lldb_object_ptr)
    m_opaque_up = std::make_unique<LineEntry>(*lldb_object_ptr);
}

const SBLineEntry &SBLineEntry::operator=(const SBLineEntry &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

void SBLineEntry::SetLineEntry(const lldb_private::LineEntry &lldb_object_ref) {
  m_opaque_up = std::make_unique<LineEntry>(lldb_object_ref);
}

SBLineEntry::~SBLineEntry() = default;

SBAddress SBLineEntry::GetStartAddress() const {
  LLDB_INSTRUMENT_VA(this);

  SBAddress sb_address;
  if (m_opaque_up)
    sb_address.SetAddress(m_opaque_up->range.GetBaseAddress());

  return sb_address;
}

SBAddress SBLineEntry::GetEndAddress() const {
  LLDB_INSTRUMENT_VA(this);

  SBAddress sb_address;
  if (m_opaque_up) {
    sb_address.SetAddress(m_opaque_up->range.GetBaseAddress());
    sb_address.OffsetAddress(m_opaque_up->range.GetByteSize());
  }
  return sb_address;
}

SBAddress SBLineEntry::GetSameLineContiguousAddressRangeEnd(
    bool include_inlined_functions) const {
  LLDB_INSTRUMENT_VA(this);

  SBAddress sb_address;
  if (m_opaque_up) {
    AddressRange line_range = m_opaque_up->GetSameLineContiguousAddressRange(
        include_inlined_functions);

    sb_address.SetAddress(line_range.GetBaseAddress());
    sb_address.OffsetAddress(line_range.GetByteSize());
  }
  return sb_address;
}

bool SBLineEntry::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBLineEntry::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up.get() && m_opaque_up->IsValid();
}

SBFileSpec SBLineEntry::GetFileSpec() const {
  LLDB_INSTRUMENT_VA(this);

  SBFileSpec sb_file_spec;
  if (m_opaque_up.get() && m_opaque_up->GetFile())
    sb_file_spec.SetFileSpec(m_opaque_up->GetFile());

  return sb_file_spec;
}

uint32_t SBLineEntry::GetLine() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t line = 0;
  if (m_opaque_up)
    line = m_opaque_up->line;

  return line;
}

uint32_t SBLineEntry::GetColumn() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->column;
  return 0;
}

void SBLineEntry::SetFileSpec(lldb::SBFileSpec filespec) {
  LLDB_INSTRUMENT_VA(this, filespec);

  if (filespec.IsValid())
    ref().file_sp = std::make_shared<SupportFile>(filespec.ref());
  else
    ref().file_sp = std::make_shared<SupportFile>();
}
void SBLineEntry::SetLine(uint32_t line) {
  LLDB_INSTRUMENT_VA(this, line);

  ref().line = line;
}

void SBLineEntry::SetColumn(uint32_t column) {
  LLDB_INSTRUMENT_VA(this, column);

  ref().line = column;
}

bool SBLineEntry::operator==(const SBLineEntry &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  lldb_private::LineEntry *lhs_ptr = m_opaque_up.get();
  lldb_private::LineEntry *rhs_ptr = rhs.m_opaque_up.get();

  if (lhs_ptr && rhs_ptr)
    return lldb_private::LineEntry::Compare(*lhs_ptr, *rhs_ptr) == 0;

  return lhs_ptr == rhs_ptr;
}

bool SBLineEntry::operator!=(const SBLineEntry &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  lldb_private::LineEntry *lhs_ptr = m_opaque_up.get();
  lldb_private::LineEntry *rhs_ptr = rhs.m_opaque_up.get();

  if (lhs_ptr && rhs_ptr)
    return lldb_private::LineEntry::Compare(*lhs_ptr, *rhs_ptr) != 0;

  return lhs_ptr != rhs_ptr;
}

const lldb_private::LineEntry *SBLineEntry::operator->() const {
  return m_opaque_up.get();
}

lldb_private::LineEntry &SBLineEntry::ref() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<lldb_private::LineEntry>();
  return *m_opaque_up;
}

const lldb_private::LineEntry &SBLineEntry::ref() const { return *m_opaque_up; }

bool SBLineEntry::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  if (m_opaque_up) {
    char file_path[PATH_MAX * 2];
    m_opaque_up->GetFile().GetPath(file_path, sizeof(file_path));
    strm.Printf("%s:%u", file_path, GetLine());
    if (GetColumn() > 0)
      strm.Printf(":%u", GetColumn());
  } else
    strm.PutCString("No value");

  return true;
}

lldb_private::LineEntry *SBLineEntry::get() { return m_opaque_up.get(); }
