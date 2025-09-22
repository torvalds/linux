//===-- FileLineResolver.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_FILELINERESOLVER_H
#define LLDB_CORE_FILELINERESOLVER_H

#include "lldb/Core/SearchFilter.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/lldb-defines.h"

#include <cstdint>

namespace lldb_private {
class Address;
class Stream;

/// \class FileLineResolver FileLineResolver.h "lldb/Core/FileLineResolver.h"
/// This class finds address for source file and line.  Optionally, it will
/// look for inlined instances of the file and line specification.

class FileLineResolver : public Searcher {
public:
  FileLineResolver()
      : m_file_spec(),
        // Set this to zero for all lines in a file
        m_sc_list() {}

  FileLineResolver(const FileSpec &resolver, uint32_t line_no,
                   bool check_inlines);

  ~FileLineResolver() override;

  Searcher::CallbackReturn SearchCallback(SearchFilter &filter,
                                          SymbolContext &context,
                                          Address *addr) override;

  lldb::SearchDepth GetDepth() override;

  void GetDescription(Stream *s) override;

  const SymbolContextList &GetFileLineMatches() { return m_sc_list; }

  void Clear();

  void Reset(const FileSpec &file_spec, uint32_t line, bool check_inlines);

protected:
  FileSpec m_file_spec;   // This is the file spec we are looking for.
  uint32_t m_line_number =
      UINT32_MAX; // This is the line number that we are looking for.
  SymbolContextList m_sc_list;
  bool m_inlines = true; // This determines whether the resolver looks for
                         // inlined functions or not.

private:
  FileLineResolver(const FileLineResolver &) = delete;
  const FileLineResolver &operator=(const FileLineResolver &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_FILELINERESOLVER_H
