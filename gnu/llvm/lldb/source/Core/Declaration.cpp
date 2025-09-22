//===-- Declaration.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Declaration.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;

void Declaration::Dump(Stream *s, bool show_fullpaths) const {
  if (m_file) {
    *s << ", decl = ";
    if (show_fullpaths)
      *s << m_file;
    else
      *s << m_file.GetFilename();
    if (m_line > 0)
      s->Printf(":%u", m_line);
    if (m_column != LLDB_INVALID_COLUMN_NUMBER)
      s->Printf(":%u", m_column);
  } else {
    if (m_line > 0) {
      s->Printf(", line = %u", m_line);
      if (m_column != LLDB_INVALID_COLUMN_NUMBER)
        s->Printf(":%u", m_column);
    } else if (m_column != LLDB_INVALID_COLUMN_NUMBER)
      s->Printf(", column = %u", m_column);
  }
}

bool Declaration::DumpStopContext(Stream *s, bool show_fullpaths) const {
  if (m_file) {
    if (show_fullpaths)
      *s << m_file;
    else
      m_file.GetFilename().Dump(s);

    if (m_line > 0)
      s->Printf(":%u", m_line);
    if (m_column != LLDB_INVALID_COLUMN_NUMBER)
      s->Printf(":%u", m_column);
    return true;
  } else if (m_line > 0) {
    s->Printf(" line %u", m_line);
    if (m_column != LLDB_INVALID_COLUMN_NUMBER)
      s->Printf(":%u", m_column);
    return true;
  }
  return false;
}

size_t Declaration::MemorySize() const { return sizeof(Declaration); }

int Declaration::Compare(const Declaration &a, const Declaration &b) {
  int result = FileSpec::Compare(a.m_file, b.m_file, true);
  if (result)
    return result;
  if (a.m_line < b.m_line)
    return -1;
  else if (a.m_line > b.m_line)
    return 1;
  if (a.m_column < b.m_column)
    return -1;
  else if (a.m_column > b.m_column)
    return 1;
  return 0;
}

bool Declaration::FileAndLineEqual(const Declaration &declaration) const {
  int file_compare = FileSpec::Compare(this->m_file, declaration.m_file, true);
  return file_compare == 0 && this->m_line == declaration.m_line;
}

bool lldb_private::operator==(const Declaration &lhs, const Declaration &rhs) {
  if (lhs.GetColumn() != rhs.GetColumn())
    return false;

  return lhs.GetLine() == rhs.GetLine() && lhs.GetFile() == rhs.GetFile();
}
