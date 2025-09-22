//===-- StreamString.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

StreamString::StreamString(bool colors) : Stream(0, 4, eByteOrderBig, colors) {}

StreamString::StreamString(uint32_t flags, uint32_t addr_size,
                           ByteOrder byte_order)
    : Stream(flags, addr_size, byte_order), m_packet() {}

StreamString::~StreamString() = default;

void StreamString::Flush() {
  // Nothing to do when flushing a buffer based stream...
}

size_t StreamString::WriteImpl(const void *s, size_t length) {
  m_packet.append(static_cast<const char *>(s), length);
  return length;
}

void StreamString::Clear() {
  m_packet.clear();
  m_bytes_written = 0;
}

bool StreamString::Empty() const { return GetSize() == 0; }

size_t StreamString::GetSize() const { return m_packet.size(); }

size_t StreamString::GetSizeOfLastLine() const {
  const size_t length = m_packet.size();
  size_t last_line_begin_pos = m_packet.find_last_of("\r\n");
  if (last_line_begin_pos == std::string::npos) {
    return length;
  } else {
    ++last_line_begin_pos;
    return length - last_line_begin_pos;
  }
}

llvm::StringRef StreamString::GetString() const { return m_packet; }

void StreamString::FillLastLineToColumn(uint32_t column, char fill_char) {
  const size_t length = m_packet.size();
  size_t last_line_begin_pos = m_packet.find_last_of("\r\n");
  if (last_line_begin_pos == std::string::npos) {
    last_line_begin_pos = 0;
  } else {
    ++last_line_begin_pos;
  }

  const size_t line_columns = length - last_line_begin_pos;
  if (column > line_columns) {
    m_packet.append(column - line_columns, fill_char);
  }
}
