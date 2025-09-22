//===-- SBSymbolContextList.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBSYMBOLCONTEXTLIST_H
#define LLDB_API_SBSYMBOLCONTEXTLIST_H

#include "lldb/API/SBDefines.h"
#include "lldb/API/SBSymbolContext.h"

namespace lldb {

class LLDB_API SBSymbolContextList {
public:
  SBSymbolContextList();

  SBSymbolContextList(const lldb::SBSymbolContextList &rhs);

  ~SBSymbolContextList();

  const lldb::SBSymbolContextList &
  operator=(const lldb::SBSymbolContextList &rhs);

  explicit operator bool() const;

  bool IsValid() const;

  uint32_t GetSize() const;

  lldb::SBSymbolContext GetContextAtIndex(uint32_t idx);

  bool GetDescription(lldb::SBStream &description);

  void Append(lldb::SBSymbolContext &sc);

  void Append(lldb::SBSymbolContextList &sc_list);

  void Clear();

protected:
  friend class SBModule;
  friend class SBTarget;
  friend class SBCompileUnit;

  lldb_private::SymbolContextList *operator->() const;

  lldb_private::SymbolContextList &operator*() const;

private:
  std::unique_ptr<lldb_private::SymbolContextList> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBSYMBOLCONTEXTLIST_H
