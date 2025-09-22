//===-- SBDeclaration.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBDeclaration.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/Core/Declaration.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Stream.h"

#include <climits>

using namespace lldb;
using namespace lldb_private;

SBDeclaration::SBDeclaration() { LLDB_INSTRUMENT_VA(this); }

SBDeclaration::SBDeclaration(const SBDeclaration &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBDeclaration::SBDeclaration(const lldb_private::Declaration *lldb_object_ptr) {
  if (lldb_object_ptr)
    m_opaque_up = std::make_unique<Declaration>(*lldb_object_ptr);
}

const SBDeclaration &SBDeclaration::operator=(const SBDeclaration &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

void SBDeclaration::SetDeclaration(
    const lldb_private::Declaration &lldb_object_ref) {
  ref() = lldb_object_ref;
}

SBDeclaration::~SBDeclaration() = default;

bool SBDeclaration::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBDeclaration::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up.get() && m_opaque_up->IsValid();
}

SBFileSpec SBDeclaration::GetFileSpec() const {
  LLDB_INSTRUMENT_VA(this);

  SBFileSpec sb_file_spec;
  if (m_opaque_up.get() && m_opaque_up->GetFile())
    sb_file_spec.SetFileSpec(m_opaque_up->GetFile());

  return sb_file_spec;
}

uint32_t SBDeclaration::GetLine() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t line = 0;
  if (m_opaque_up)
    line = m_opaque_up->GetLine();


  return line;
}

uint32_t SBDeclaration::GetColumn() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->GetColumn();
  return 0;
}

void SBDeclaration::SetFileSpec(lldb::SBFileSpec filespec) {
  LLDB_INSTRUMENT_VA(this, filespec);

  if (filespec.IsValid())
    ref().SetFile(filespec.ref());
  else
    ref().SetFile(FileSpec());
}
void SBDeclaration::SetLine(uint32_t line) {
  LLDB_INSTRUMENT_VA(this, line);

  ref().SetLine(line);
}

void SBDeclaration::SetColumn(uint32_t column) {
  LLDB_INSTRUMENT_VA(this, column);

  ref().SetColumn(column);
}

bool SBDeclaration::operator==(const SBDeclaration &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  lldb_private::Declaration *lhs_ptr = m_opaque_up.get();
  lldb_private::Declaration *rhs_ptr = rhs.m_opaque_up.get();

  if (lhs_ptr && rhs_ptr)
    return lldb_private::Declaration::Compare(*lhs_ptr, *rhs_ptr) == 0;

  return lhs_ptr == rhs_ptr;
}

bool SBDeclaration::operator!=(const SBDeclaration &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  lldb_private::Declaration *lhs_ptr = m_opaque_up.get();
  lldb_private::Declaration *rhs_ptr = rhs.m_opaque_up.get();

  if (lhs_ptr && rhs_ptr)
    return lldb_private::Declaration::Compare(*lhs_ptr, *rhs_ptr) != 0;

  return lhs_ptr != rhs_ptr;
}

const lldb_private::Declaration *SBDeclaration::operator->() const {
  return m_opaque_up.get();
}

lldb_private::Declaration &SBDeclaration::ref() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<lldb_private::Declaration>();
  return *m_opaque_up;
}

const lldb_private::Declaration &SBDeclaration::ref() const {
  return *m_opaque_up;
}

bool SBDeclaration::GetDescription(SBStream &description) {
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

lldb_private::Declaration *SBDeclaration::get() { return m_opaque_up.get(); }
