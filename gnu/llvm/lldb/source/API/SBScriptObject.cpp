//===-- SBScriptObject.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBScriptObject.h"

#include "Utils.h"

#include "lldb/Interpreter/ScriptObject.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBScriptObject::SBScriptObject(const ScriptObjectPtr ptr,
                               lldb::ScriptLanguage lang)
    : m_opaque_up(std::make_unique<lldb_private::ScriptObject>(ptr, lang)) {
  LLDB_INSTRUMENT_VA(this, ptr, lang);
}

SBScriptObject::SBScriptObject(const SBScriptObject &rhs)
    : m_opaque_up(new ScriptObject(nullptr, eScriptLanguageNone)) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}
SBScriptObject::~SBScriptObject() = default;

const SBScriptObject &SBScriptObject::operator=(const SBScriptObject &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

bool SBScriptObject::operator!=(const SBScriptObject &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return !(m_opaque_up == rhs.m_opaque_up);
}

bool SBScriptObject::IsValid() const {
  LLDB_INSTRUMENT_VA(this);

  return this->operator bool();
}

SBScriptObject::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up != nullptr && m_opaque_up->operator bool();
}

lldb::ScriptObjectPtr SBScriptObject::GetPointer() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up ? const_cast<void *>(m_opaque_up->GetPointer()) : nullptr;
}

lldb::ScriptLanguage SBScriptObject::GetLanguage() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up ? m_opaque_up->GetLanguage() : eScriptLanguageNone;
}

ScriptObject &SBScriptObject::ref() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<ScriptObject>(nullptr, eScriptLanguageNone);
  return *m_opaque_up;
}

const ScriptObject &SBScriptObject::ref() const {
  // This object should already have checked with "IsValid()" prior to calling
  // this function. In case you didn't we will assert and die to let you know.
  assert(m_opaque_up.get());
  return *m_opaque_up;
}

ScriptObject *SBScriptObject::get() { return m_opaque_up.get(); }
