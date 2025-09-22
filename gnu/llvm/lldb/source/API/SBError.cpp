//===-- SBError.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBError.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Status.h"

#include <cstdarg>

using namespace lldb;
using namespace lldb_private;

SBError::SBError() { LLDB_INSTRUMENT_VA(this); }

SBError::SBError(const SBError &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBError::SBError(const char *message) {
  LLDB_INSTRUMENT_VA(this, message);

  SetErrorString(message);
}

SBError::SBError(const lldb_private::Status &status)
    : m_opaque_up(new Status(status)) {
  LLDB_INSTRUMENT_VA(this, status);
}

SBError::~SBError() = default;

const SBError &SBError::operator=(const SBError &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

const char *SBError::GetCString() const {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    return m_opaque_up->AsCString();
  return nullptr;
}

void SBError::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up)
    m_opaque_up->Clear();
}

bool SBError::Fail() const {
  LLDB_INSTRUMENT_VA(this);

  bool ret_value = false;
  if (m_opaque_up)
    ret_value = m_opaque_up->Fail();


  return ret_value;
}

bool SBError::Success() const {
  LLDB_INSTRUMENT_VA(this);

  bool ret_value = true;
  if (m_opaque_up)
    ret_value = m_opaque_up->Success();

  return ret_value;
}

uint32_t SBError::GetError() const {
  LLDB_INSTRUMENT_VA(this);

  uint32_t err = 0;
  if (m_opaque_up)
    err = m_opaque_up->GetError();


  return err;
}

ErrorType SBError::GetType() const {
  LLDB_INSTRUMENT_VA(this);

  ErrorType err_type = eErrorTypeInvalid;
  if (m_opaque_up)
    err_type = m_opaque_up->GetType();

  return err_type;
}

void SBError::SetError(uint32_t err, ErrorType type) {
  LLDB_INSTRUMENT_VA(this, err, type);

  CreateIfNeeded();
  m_opaque_up->SetError(err, type);
}

void SBError::SetError(const Status &lldb_error) {
  CreateIfNeeded();
  *m_opaque_up = lldb_error;
}

void SBError::SetErrorToErrno() {
  LLDB_INSTRUMENT_VA(this);

  CreateIfNeeded();
  m_opaque_up->SetErrorToErrno();
}

void SBError::SetErrorToGenericError() {
  LLDB_INSTRUMENT_VA(this);

  CreateIfNeeded();
  m_opaque_up->SetErrorToGenericError();
}

void SBError::SetErrorString(const char *err_str) {
  LLDB_INSTRUMENT_VA(this, err_str);

  CreateIfNeeded();
  m_opaque_up->SetErrorString(err_str);
}

int SBError::SetErrorStringWithFormat(const char *format, ...) {
  CreateIfNeeded();
  va_list args;
  va_start(args, format);
  int num_chars = m_opaque_up->SetErrorStringWithVarArg(format, args);
  va_end(args);
  return num_chars;
}

bool SBError::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBError::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up != nullptr;
}

void SBError::CreateIfNeeded() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<Status>();
}

lldb_private::Status *SBError::operator->() { return m_opaque_up.get(); }

lldb_private::Status *SBError::get() { return m_opaque_up.get(); }

lldb_private::Status &SBError::ref() {
  CreateIfNeeded();
  return *m_opaque_up;
}

const lldb_private::Status &SBError::operator*() const {
  // Be sure to call "IsValid()" before calling this function or it will crash
  return *m_opaque_up;
}

bool SBError::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  if (m_opaque_up) {
    if (m_opaque_up->Success())
      description.Printf("success");
    else {
      const char *err_string = GetCString();
      description.Printf("error: %s",
                         (err_string != nullptr ? err_string : ""));
    }
  } else
    description.Printf("error: <NULL>");

  return true;
}
