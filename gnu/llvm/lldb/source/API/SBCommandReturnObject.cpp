//===-- SBCommandReturnObject.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBCommandReturnObject.h"
#include "Utils.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBFile.h"
#include "lldb/API/SBStream.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Status.h"

using namespace lldb;
using namespace lldb_private;

class lldb_private::SBCommandReturnObjectImpl {
public:
  SBCommandReturnObjectImpl() : m_ptr(new CommandReturnObject(false)) {}
  SBCommandReturnObjectImpl(CommandReturnObject &ref)
      : m_ptr(&ref), m_owned(false) {}
  SBCommandReturnObjectImpl(const SBCommandReturnObjectImpl &rhs)
      : m_ptr(new CommandReturnObject(*rhs.m_ptr)), m_owned(rhs.m_owned) {}
  SBCommandReturnObjectImpl &operator=(const SBCommandReturnObjectImpl &rhs) {
    SBCommandReturnObjectImpl copy(rhs);
    std::swap(*this, copy);
    return *this;
  }
  // rvalue ctor+assignment are not used by SBCommandReturnObject.
  ~SBCommandReturnObjectImpl() {
    if (m_owned)
      delete m_ptr;
  }

  CommandReturnObject &operator*() const { return *m_ptr; }

private:
  CommandReturnObject *m_ptr;
  bool m_owned = true;
};

SBCommandReturnObject::SBCommandReturnObject()
    : m_opaque_up(new SBCommandReturnObjectImpl()) {
  LLDB_INSTRUMENT_VA(this);
}

SBCommandReturnObject::SBCommandReturnObject(CommandReturnObject &ref)
    : m_opaque_up(new SBCommandReturnObjectImpl(ref)) {
  LLDB_INSTRUMENT_VA(this, ref);
}

SBCommandReturnObject::SBCommandReturnObject(const SBCommandReturnObject &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBCommandReturnObject &SBCommandReturnObject::
operator=(const SBCommandReturnObject &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

SBCommandReturnObject::~SBCommandReturnObject() = default;

bool SBCommandReturnObject::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBCommandReturnObject::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  // This method is not useful but it needs to stay to keep SB API stable.
  return true;
}

const char *SBCommandReturnObject::GetOutput() {
  LLDB_INSTRUMENT_VA(this);

  ConstString output(ref().GetOutputData());
  return output.AsCString(/*value_if_empty*/ "");
}

const char *SBCommandReturnObject::GetError() {
  LLDB_INSTRUMENT_VA(this);

  ConstString output(ref().GetErrorData());
  return output.AsCString(/*value_if_empty*/ "");
}

size_t SBCommandReturnObject::GetOutputSize() {
  LLDB_INSTRUMENT_VA(this);

  return ref().GetOutputData().size();
}

size_t SBCommandReturnObject::GetErrorSize() {
  LLDB_INSTRUMENT_VA(this);

  return ref().GetErrorData().size();
}

size_t SBCommandReturnObject::PutOutput(FILE *fh) {
  LLDB_INSTRUMENT_VA(this, fh);
  if (fh) {
    size_t num_bytes = GetOutputSize();
    if (num_bytes)
      return ::fprintf(fh, "%s", GetOutput());
  }
  return 0;
}

size_t SBCommandReturnObject::PutOutput(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);
  if (!file_sp)
    return 0;
  return file_sp->Printf("%s", GetOutput());
}

size_t SBCommandReturnObject::PutOutput(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file);
  if (!file.m_opaque_sp)
    return 0;
  return file.m_opaque_sp->Printf("%s", GetOutput());
}

size_t SBCommandReturnObject::PutError(FILE *fh) {
  LLDB_INSTRUMENT_VA(this, fh);
  if (fh) {
    size_t num_bytes = GetErrorSize();
    if (num_bytes)
      return ::fprintf(fh, "%s", GetError());
  }
  return 0;
}

size_t SBCommandReturnObject::PutError(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);
  if (!file_sp)
    return 0;
  return file_sp->Printf("%s", GetError());
}

size_t SBCommandReturnObject::PutError(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file);
  if (!file.m_opaque_sp)
    return 0;
  return file.m_opaque_sp->Printf("%s", GetError());
}

void SBCommandReturnObject::Clear() {
  LLDB_INSTRUMENT_VA(this);

  ref().Clear();
}

lldb::ReturnStatus SBCommandReturnObject::GetStatus() {
  LLDB_INSTRUMENT_VA(this);

  return ref().GetStatus();
}

void SBCommandReturnObject::SetStatus(lldb::ReturnStatus status) {
  LLDB_INSTRUMENT_VA(this, status);

  ref().SetStatus(status);
}

bool SBCommandReturnObject::Succeeded() {
  LLDB_INSTRUMENT_VA(this);

  return ref().Succeeded();
}

bool SBCommandReturnObject::HasResult() {
  LLDB_INSTRUMENT_VA(this);

  return ref().HasResult();
}

void SBCommandReturnObject::AppendMessage(const char *message) {
  LLDB_INSTRUMENT_VA(this, message);

  ref().AppendMessage(message);
}

void SBCommandReturnObject::AppendWarning(const char *message) {
  LLDB_INSTRUMENT_VA(this, message);

  ref().AppendWarning(message);
}

CommandReturnObject *SBCommandReturnObject::operator->() const {
  return &**m_opaque_up;
}

CommandReturnObject *SBCommandReturnObject::get() const {
  return &**m_opaque_up;
}

CommandReturnObject &SBCommandReturnObject::operator*() const {
  return **m_opaque_up;
}

CommandReturnObject &SBCommandReturnObject::ref() const {
  return **m_opaque_up;
}

bool SBCommandReturnObject::GetDescription(SBStream &description) {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();

  description.Printf("Error:  ");
  lldb::ReturnStatus status = ref().GetStatus();
  if (status == lldb::eReturnStatusStarted)
    strm.PutCString("Started");
  else if (status == lldb::eReturnStatusInvalid)
    strm.PutCString("Invalid");
  else if (ref().Succeeded())
    strm.PutCString("Success");
  else
    strm.PutCString("Fail");

  if (GetOutputSize() > 0)
    strm.Printf("\nOutput Message:\n%s", GetOutput());

  if (GetErrorSize() > 0)
    strm.Printf("\nError Message:\n%s", GetError());

  return true;
}

void SBCommandReturnObject::SetImmediateOutputFile(FILE *fh) {
  LLDB_INSTRUMENT_VA(this, fh);

  SetImmediateOutputFile(fh, false);
}

void SBCommandReturnObject::SetImmediateErrorFile(FILE *fh) {
  LLDB_INSTRUMENT_VA(this, fh);

  SetImmediateErrorFile(fh, false);
}

void SBCommandReturnObject::SetImmediateOutputFile(FILE *fh,
                                                   bool transfer_ownership) {
  LLDB_INSTRUMENT_VA(this, fh, transfer_ownership);
  FileSP file = std::make_shared<NativeFile>(fh, transfer_ownership);
  ref().SetImmediateOutputFile(file);
}

void SBCommandReturnObject::SetImmediateErrorFile(FILE *fh,
                                                  bool transfer_ownership) {
  LLDB_INSTRUMENT_VA(this, fh, transfer_ownership);
  FileSP file = std::make_shared<NativeFile>(fh, transfer_ownership);
  ref().SetImmediateErrorFile(file);
}

void SBCommandReturnObject::SetImmediateOutputFile(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file);
  ref().SetImmediateOutputFile(file.m_opaque_sp);
}

void SBCommandReturnObject::SetImmediateErrorFile(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file);
  ref().SetImmediateErrorFile(file.m_opaque_sp);
}

void SBCommandReturnObject::SetImmediateOutputFile(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);
  SetImmediateOutputFile(SBFile(file_sp));
}

void SBCommandReturnObject::SetImmediateErrorFile(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);
  SetImmediateErrorFile(SBFile(file_sp));
}

void SBCommandReturnObject::PutCString(const char *string, int len) {
  LLDB_INSTRUMENT_VA(this, string, len);

  if (len == 0 || string == nullptr || *string == 0) {
    return;
  } else if (len > 0) {
    std::string buffer(string, len);
    ref().AppendMessage(buffer.c_str());
  } else
    ref().AppendMessage(string);
}

const char *SBCommandReturnObject::GetOutput(bool only_if_no_immediate) {
  LLDB_INSTRUMENT_VA(this, only_if_no_immediate);

  if (!only_if_no_immediate ||
      ref().GetImmediateOutputStream().get() == nullptr)
    return GetOutput();
  return nullptr;
}

const char *SBCommandReturnObject::GetError(bool only_if_no_immediate) {
  LLDB_INSTRUMENT_VA(this, only_if_no_immediate);

  if (!only_if_no_immediate || ref().GetImmediateErrorStream().get() == nullptr)
    return GetError();
  return nullptr;
}

size_t SBCommandReturnObject::Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  size_t result = ref().GetOutputStream().PrintfVarArg(format, args);
  va_end(args);
  return result;
}

void SBCommandReturnObject::SetError(lldb::SBError &error,
                                     const char *fallback_error_cstr) {
  LLDB_INSTRUMENT_VA(this, error, fallback_error_cstr);

  if (error.IsValid())
    ref().SetError(error.ref(), fallback_error_cstr);
  else if (fallback_error_cstr)
    ref().SetError(Status(), fallback_error_cstr);
}

void SBCommandReturnObject::SetError(const char *error_cstr) {
  LLDB_INSTRUMENT_VA(this, error_cstr);

  if (error_cstr)
    ref().AppendError(error_cstr);
}
