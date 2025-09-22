//===-- SBStructuredData.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBStructuredData.h"

#include "lldb/API/SBDebugger.h"
#include "lldb/API/SBScriptObject.h"
#include "lldb/API/SBStream.h"
#include "lldb/API/SBStringList.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/StructuredDataPlugin.h"
#include "lldb/Utility/Event.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StringList.h"
#include "lldb/Utility/StructuredData.h"

using namespace lldb;
using namespace lldb_private;

#pragma mark--
#pragma mark SBStructuredData

SBStructuredData::SBStructuredData() : m_impl_up(new StructuredDataImpl()) {
  LLDB_INSTRUMENT_VA(this);
}

SBStructuredData::SBStructuredData(const lldb::SBStructuredData &rhs)
    : m_impl_up(new StructuredDataImpl(*rhs.m_impl_up)) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBStructuredData::SBStructuredData(const lldb::SBScriptObject obj,
                                   const lldb::SBDebugger &debugger) {
  LLDB_INSTRUMENT_VA(this, obj, debugger);

  if (!obj.IsValid())
    return;

  ScriptInterpreter *interpreter =
      debugger.m_opaque_sp->GetScriptInterpreter(true, obj.GetLanguage());

  if (!interpreter)
    return;

  StructuredDataImplUP impl_up = std::make_unique<StructuredDataImpl>(
      interpreter->CreateStructuredDataFromScriptObject(obj.ref()));
  if (impl_up && impl_up->IsValid())
    m_impl_up.reset(impl_up.release());
}

SBStructuredData::SBStructuredData(const lldb::EventSP &event_sp)
    : m_impl_up(new StructuredDataImpl(event_sp)) {
  LLDB_INSTRUMENT_VA(this, event_sp);
}

SBStructuredData::SBStructuredData(const lldb_private::StructuredDataImpl &impl)
    : m_impl_up(new StructuredDataImpl(impl)) {
  LLDB_INSTRUMENT_VA(this, impl);
}

SBStructuredData::~SBStructuredData() = default;

SBStructuredData &SBStructuredData::
operator=(const lldb::SBStructuredData &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  *m_impl_up = *rhs.m_impl_up;
  return *this;
}

lldb::SBError SBStructuredData::SetFromJSON(lldb::SBStream &stream) {
  LLDB_INSTRUMENT_VA(this, stream);

  lldb::SBError error;

  StructuredData::ObjectSP json_obj =
      StructuredData::ParseJSON(stream.GetData());
  m_impl_up->SetObjectSP(json_obj);

  if (!json_obj || json_obj->GetType() != eStructuredDataTypeDictionary)
    error.SetErrorString("Invalid Syntax");
  return error;
}

lldb::SBError SBStructuredData::SetFromJSON(const char *json) {
  LLDB_INSTRUMENT_VA(this, json);
  lldb::SBStream s;
  s.Print(json);
  return SetFromJSON(s);
}

bool SBStructuredData::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}

SBStructuredData::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_impl_up->IsValid();
}

void SBStructuredData::Clear() {
  LLDB_INSTRUMENT_VA(this);

  m_impl_up->Clear();
}

SBError SBStructuredData::GetAsJSON(lldb::SBStream &stream) const {
  LLDB_INSTRUMENT_VA(this, stream);

  SBError error;
  error.SetError(m_impl_up->GetAsJSON(stream.ref()));
  return error;
}

lldb::SBError SBStructuredData::GetDescription(lldb::SBStream &stream) const {
  LLDB_INSTRUMENT_VA(this, stream);

  Status error = m_impl_up->GetDescription(stream.ref());
  SBError sb_error;
  sb_error.SetError(error);
  return sb_error;
}

StructuredDataType SBStructuredData::GetType() const {
  LLDB_INSTRUMENT_VA(this);

  return m_impl_up->GetType();
}

size_t SBStructuredData::GetSize() const {
  LLDB_INSTRUMENT_VA(this);

  return m_impl_up->GetSize();
}

bool SBStructuredData::GetKeys(lldb::SBStringList &keys) const {
  LLDB_INSTRUMENT_VA(this, keys);

  if (GetType() != eStructuredDataTypeDictionary)
    return false;

  StructuredData::ObjectSP obj_sp = m_impl_up->GetObjectSP();
  if (!obj_sp)
    return false;

  StructuredData::Dictionary *dict = obj_sp->GetAsDictionary();
  // We claimed we were a dictionary, so this can't be null.
  assert(dict);
  // The return kind of GetKeys is an Array:
  StructuredData::ObjectSP array_sp = dict->GetKeys();
  StructuredData::Array *key_arr = array_sp->GetAsArray();
  assert(key_arr);

  key_arr->ForEach([&keys](StructuredData::Object *object) -> bool {
    llvm::StringRef key = object->GetStringValue("");
    keys->AppendString(key);
    return true;
  });
  return true;
}

lldb::SBStructuredData SBStructuredData::GetValueForKey(const char *key) const {
  LLDB_INSTRUMENT_VA(this, key);

  SBStructuredData result;
  result.m_impl_up->SetObjectSP(m_impl_up->GetValueForKey(key));
  return result;
}

lldb::SBStructuredData SBStructuredData::GetItemAtIndex(size_t idx) const {
  LLDB_INSTRUMENT_VA(this, idx);

  SBStructuredData result;
  result.m_impl_up->SetObjectSP(m_impl_up->GetItemAtIndex(idx));
  return result;
}

uint64_t SBStructuredData::GetIntegerValue(uint64_t fail_value) const {
  LLDB_INSTRUMENT_VA(this, fail_value);

  return GetUnsignedIntegerValue(fail_value);
}

uint64_t SBStructuredData::GetUnsignedIntegerValue(uint64_t fail_value) const {
  LLDB_INSTRUMENT_VA(this, fail_value);

  return m_impl_up->GetIntegerValue(fail_value);
}

int64_t SBStructuredData::GetSignedIntegerValue(int64_t fail_value) const {
  LLDB_INSTRUMENT_VA(this, fail_value);

  return m_impl_up->GetIntegerValue(fail_value);
}

double SBStructuredData::GetFloatValue(double fail_value) const {
  LLDB_INSTRUMENT_VA(this, fail_value);

  return m_impl_up->GetFloatValue(fail_value);
}

bool SBStructuredData::GetBooleanValue(bool fail_value) const {
  LLDB_INSTRUMENT_VA(this, fail_value);

  return m_impl_up->GetBooleanValue(fail_value);
}

size_t SBStructuredData::GetStringValue(char *dst, size_t dst_len) const {
  LLDB_INSTRUMENT_VA(this, dst, dst_len);

  return m_impl_up->GetStringValue(dst, dst_len);
}

lldb::SBScriptObject SBStructuredData::GetGenericValue() const {
  LLDB_INSTRUMENT_VA(this);

  return {m_impl_up->GetGenericValue(), eScriptLanguageDefault};
}
