//===-- DebugMacros.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/DebugMacros.h"

#include "lldb/Symbol/CompileUnit.h"

using namespace lldb_private;

DebugMacroEntry::DebugMacroEntry(EntryType type, uint32_t line,
                                 uint32_t debug_line_file_idx, const char *str)
    : m_type(type), m_line(line), m_debug_line_file_idx(debug_line_file_idx),
      m_str(str) {}

DebugMacroEntry::DebugMacroEntry(EntryType type,
                                 const DebugMacrosSP &debug_macros_sp)
    : m_type(type), m_line(0), m_debug_line_file_idx(0),
      m_debug_macros_sp(debug_macros_sp) {}

const FileSpec &DebugMacroEntry::GetFileSpec(CompileUnit *comp_unit) const {
  return comp_unit->GetSupportFiles().GetFileSpecAtIndex(m_debug_line_file_idx);
}

DebugMacroEntry DebugMacroEntry::CreateDefineEntry(uint32_t line,
                                                   const char *str) {
  return DebugMacroEntry(DebugMacroEntry::DEFINE, line, 0, str);
}

DebugMacroEntry DebugMacroEntry::CreateUndefEntry(uint32_t line,
                                                  const char *str) {
  return DebugMacroEntry(DebugMacroEntry::UNDEF, line, 0, str);
}

DebugMacroEntry
DebugMacroEntry::CreateStartFileEntry(uint32_t line,
                                      uint32_t debug_line_file_idx) {
  return DebugMacroEntry(DebugMacroEntry::START_FILE, line, debug_line_file_idx,
                         nullptr);
}

DebugMacroEntry DebugMacroEntry::CreateEndFileEntry() {
  return DebugMacroEntry(DebugMacroEntry::END_FILE, 0, 0, nullptr);
}

DebugMacroEntry
DebugMacroEntry::CreateIndirectEntry(const DebugMacrosSP &debug_macros_sp) {
  return DebugMacroEntry(DebugMacroEntry::INDIRECT, debug_macros_sp);
}
