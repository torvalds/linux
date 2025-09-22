//===-- SBSymbolContext.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBSYMBOLCONTEXT_H
#define LLDB_API_SBSYMBOLCONTEXT_H

#include "lldb/API/SBBlock.h"
#include "lldb/API/SBCompileUnit.h"
#include "lldb/API/SBDefines.h"
#include "lldb/API/SBFunction.h"
#include "lldb/API/SBLineEntry.h"
#include "lldb/API/SBModule.h"
#include "lldb/API/SBSymbol.h"

namespace lldb_private {
namespace python {
class SWIGBridge;
}
} // namespace lldb_private

namespace lldb {

class LLDB_API SBSymbolContext {
public:
  SBSymbolContext();

  SBSymbolContext(const lldb::SBSymbolContext &rhs);

  ~SBSymbolContext();

  explicit operator bool() const;

  bool IsValid() const;

  const lldb::SBSymbolContext &operator=(const lldb::SBSymbolContext &rhs);

  lldb::SBModule GetModule();
  lldb::SBCompileUnit GetCompileUnit();
  lldb::SBFunction GetFunction();
  lldb::SBBlock GetBlock();
  lldb::SBLineEntry GetLineEntry();
  lldb::SBSymbol GetSymbol();

  void SetModule(lldb::SBModule module);
  void SetCompileUnit(lldb::SBCompileUnit compile_unit);
  void SetFunction(lldb::SBFunction function);
  void SetBlock(lldb::SBBlock block);
  void SetLineEntry(lldb::SBLineEntry line_entry);
  void SetSymbol(lldb::SBSymbol symbol);

  SBSymbolContext GetParentOfInlinedScope(const SBAddress &curr_frame_pc,
                                          SBAddress &parent_frame_addr) const;

  bool GetDescription(lldb::SBStream &description);

protected:
  friend class SBAddress;
  friend class SBFrame;
  friend class SBModule;
  friend class SBThread;
  friend class SBTarget;
  friend class SBSymbolContextList;

  friend class lldb_private::python::SWIGBridge;

  SBSymbolContext(const lldb_private::SymbolContext &sc_ptr);

  lldb_private::SymbolContext *operator->() const;

  lldb_private::SymbolContext &operator*();

  lldb_private::SymbolContext &ref();

  const lldb_private::SymbolContext &operator*() const;

  lldb_private::SymbolContext *get() const;

private:
  std::unique_ptr<lldb_private::SymbolContext> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBSYMBOLCONTEXT_H
