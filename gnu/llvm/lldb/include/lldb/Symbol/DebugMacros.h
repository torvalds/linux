//===-- DebugMacros.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SYMBOL_DEBUGMACROS_H
#define LLDB_SYMBOL_DEBUGMACROS_H

#include <memory>
#include <vector>

#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CompileUnit;
class DebugMacros;
typedef std::shared_ptr<DebugMacros> DebugMacrosSP;

class DebugMacroEntry {
public:
  enum EntryType : uint8_t {
      INVALID, DEFINE, UNDEF, START_FILE, END_FILE, INDIRECT
  };

  static DebugMacroEntry CreateDefineEntry(uint32_t line, const char *str);

  static DebugMacroEntry CreateUndefEntry(uint32_t line, const char *str);

  static DebugMacroEntry CreateStartFileEntry(uint32_t line,
                                              uint32_t debug_line_file_idx);

  static DebugMacroEntry CreateEndFileEntry();

  static DebugMacroEntry
  CreateIndirectEntry(const DebugMacrosSP &debug_macros_sp);

  DebugMacroEntry() : m_type(INVALID), m_line(0), m_debug_line_file_idx(0) {}

  ~DebugMacroEntry() = default;

  EntryType GetType() const { return static_cast<EntryType>(m_type); }

  uint64_t GetLineNumber() const { return m_line; }

  ConstString GetMacroString() const { return m_str; }

  const FileSpec &GetFileSpec(CompileUnit *comp_unit) const;

  DebugMacros *GetIndirectDebugMacros() const {
    return m_debug_macros_sp.get();
  }

private:
  DebugMacroEntry(EntryType type, uint32_t line, uint32_t debug_line_file_idx,
                  const char *str);

  DebugMacroEntry(EntryType type, const DebugMacrosSP &debug_macros_sp);

  uint32_t m_type : 3;
  uint32_t m_line : 29;
  uint32_t m_debug_line_file_idx;
  ConstString m_str;
  DebugMacrosSP m_debug_macros_sp;
};

class DebugMacros {
public:
  DebugMacros() = default;

  ~DebugMacros() = default;

  void AddMacroEntry(const DebugMacroEntry &entry) {
    m_macro_entries.push_back(entry);
  }

  size_t GetNumMacroEntries() const { return m_macro_entries.size(); }

  DebugMacroEntry GetMacroEntryAtIndex(const size_t index) const {
    if (index < m_macro_entries.size())
      return m_macro_entries[index];
    else
      return DebugMacroEntry();
  }

private:
  DebugMacros(const DebugMacros &) = delete;
  const DebugMacros &operator=(const DebugMacros &) = delete;

  std::vector<DebugMacroEntry> m_macro_entries;
};

} // namespace lldb_private

#endif // LLDB_SYMBOL_DEBUGMACROS_H
