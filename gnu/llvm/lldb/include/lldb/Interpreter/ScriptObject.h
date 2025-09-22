//===-- ScriptObject.h ------------------------------------ -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_SCRIPTOBJECT_H
#define LLDB_INTERPRETER_SCRIPTOBJECT_H

#include "lldb/lldb-types.h"

namespace lldb_private {
class ScriptObject {
public:
  ScriptObject(lldb::ScriptObjectPtr ptr, lldb::ScriptLanguage lang)
      : m_ptr(ptr), m_language(lang) {}

  operator bool() const { return m_ptr != nullptr; }

  const void *GetPointer() const { return m_ptr; }

  lldb::ScriptLanguage GetLanguage() const { return m_language; }

private:
  const void *m_ptr;
  lldb::ScriptLanguage m_language;
};
} // namespace lldb_private

#endif // LLDB_INTERPRETER_SCRIPTOBJECT_H
