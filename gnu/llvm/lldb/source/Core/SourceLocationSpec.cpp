//===-- SourceLocationSpec.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/SourceLocationSpec.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

SourceLocationSpec::SourceLocationSpec(FileSpec file_spec, uint32_t line,
                                       std::optional<uint16_t> column,
                                       bool check_inlines, bool exact_match)
    : m_declaration(file_spec, line,
                    column.value_or(LLDB_INVALID_COLUMN_NUMBER)),
      m_check_inlines(check_inlines), m_exact_match(exact_match) {}

SourceLocationSpec::operator bool() const { return m_declaration.IsValid(); }

bool SourceLocationSpec::operator!() const { return !operator bool(); }

bool SourceLocationSpec::operator==(const SourceLocationSpec &rhs) const {
  return m_declaration == rhs.m_declaration &&
         m_check_inlines == rhs.GetCheckInlines() &&
         m_exact_match == rhs.GetExactMatch();
}

bool SourceLocationSpec::operator!=(const SourceLocationSpec &rhs) const {
  return !(*this == rhs);
}

bool SourceLocationSpec::operator<(const SourceLocationSpec &rhs) const {
  return SourceLocationSpec::Compare(*this, rhs) < 0;
}

Stream &lldb_private::operator<<(Stream &s, const SourceLocationSpec &loc) {
  loc.Dump(s);
  return s;
}

int SourceLocationSpec::Compare(const SourceLocationSpec &lhs,
                                const SourceLocationSpec &rhs) {
  return Declaration::Compare(lhs.m_declaration, rhs.m_declaration);
}

bool SourceLocationSpec::Equal(const SourceLocationSpec &lhs,
                               const SourceLocationSpec &rhs, bool full) {
  return full ? lhs == rhs
              : (lhs.GetFileSpec() == rhs.GetFileSpec() &&
                 lhs.GetLine() == rhs.GetLine());
}

void SourceLocationSpec::Dump(Stream &s) const {
  s << "check inlines = " << llvm::toStringRef(m_check_inlines);
  s << ", exact match = " << llvm::toStringRef(m_exact_match);
  m_declaration.Dump(&s, true);
}

std::string SourceLocationSpec::GetString() const {
  StreamString ss;
  Dump(ss);
  return ss.GetString().str();
}

std::optional<uint32_t> SourceLocationSpec::GetLine() const {
  uint32_t line = m_declaration.GetLine();
  if (line == 0 || line == LLDB_INVALID_LINE_NUMBER)
    return std::nullopt;
  return line;
}

std::optional<uint16_t> SourceLocationSpec::GetColumn() const {
  uint16_t column = m_declaration.GetColumn();
  if (column == LLDB_INVALID_COLUMN_NUMBER)
    return std::nullopt;
  return column;
}
