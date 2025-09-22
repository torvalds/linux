//===-- SBFormat.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBFormat.h"
#include "Utils.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/lldb-types.h"
#include <lldb/API/SBError.h>
#include <lldb/Utility/Status.h>

using namespace lldb;
using namespace lldb_private;

SBFormat::SBFormat() : m_opaque_sp() {}

SBFormat::SBFormat(const SBFormat &rhs) {
  m_opaque_sp = clone(rhs.m_opaque_sp);
}

SBFormat::~SBFormat() = default;

SBFormat &SBFormat::operator=(const SBFormat &rhs) {
  if (this != &rhs)
    m_opaque_sp = clone(rhs.m_opaque_sp);
  return *this;
}

SBFormat::operator bool() const { return (bool)m_opaque_sp; }

SBFormat::SBFormat(const char *format, lldb::SBError &error) {
  FormatEntrySP format_entry_sp = std::make_shared<FormatEntity::Entry>();
  Status status = FormatEntity::Parse(format, *format_entry_sp);

  error.SetError(status);
  if (error.Success())
    m_opaque_sp = format_entry_sp;
}

lldb::FormatEntrySP SBFormat::GetFormatEntrySP() const { return m_opaque_sp; }
