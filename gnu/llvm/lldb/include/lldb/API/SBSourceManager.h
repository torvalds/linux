//===-- SBSourceManager.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBSOURCEMANAGER_H
#define LLDB_API_SBSOURCEMANAGER_H

#include "lldb/API/SBDefines.h"

#include <cstdio>

namespace lldb {

class LLDB_API SBSourceManager {
public:
  SBSourceManager(const SBDebugger &debugger);
  SBSourceManager(const SBTarget &target);
  SBSourceManager(const SBSourceManager &rhs);

  ~SBSourceManager();

  const lldb::SBSourceManager &operator=(const lldb::SBSourceManager &rhs);

  size_t DisplaySourceLinesWithLineNumbers(
      const lldb::SBFileSpec &file, uint32_t line, uint32_t context_before,
      uint32_t context_after, const char *current_line_cstr, lldb::SBStream &s);

  size_t DisplaySourceLinesWithLineNumbersAndColumn(
      const lldb::SBFileSpec &file, uint32_t line, uint32_t column,
      uint32_t context_before, uint32_t context_after,
      const char *current_line_cstr, lldb::SBStream &s);

protected:
  friend class SBCommandInterpreter;
  friend class SBDebugger;

private:
  std::unique_ptr<lldb_private::SourceManagerImpl> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBSOURCEMANAGER_H
