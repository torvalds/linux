//===-- SBScriptObject.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBSCRIPTOBJECT_H
#define LLDB_API_SBSCRIPTOBJECT_H

#include "lldb/API/SBDefines.h"

namespace lldb_private {
class ScriptObject;
}

namespace lldb {

class LLDB_API SBScriptObject {
public:
  SBScriptObject(const ScriptObjectPtr ptr, lldb::ScriptLanguage lang);

  SBScriptObject(const lldb::SBScriptObject &rhs);

  ~SBScriptObject();

  const lldb::SBScriptObject &operator=(const lldb::SBScriptObject &rhs);

  explicit operator bool() const;

  bool operator!=(const SBScriptObject &rhs) const;

  bool IsValid() const;

  lldb::ScriptObjectPtr GetPointer() const;

  lldb::ScriptLanguage GetLanguage() const;

protected:
  friend class SBStructuredData;

  lldb_private::ScriptObject *get();

  lldb_private::ScriptObject &ref();

  const lldb_private::ScriptObject &ref() const;

private:
  std::unique_ptr<lldb_private::ScriptObject> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBSCRIPTOBJECT_H
