//===-- SBFile.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBFile.h"
#include "lldb/API/SBError.h"
#include "lldb/Host/File.h"
#include "lldb/Utility/Instrumentation.h"

using namespace lldb;
using namespace lldb_private;

SBFile::~SBFile() = default;

SBFile::SBFile(FileSP file_sp) : m_opaque_sp(file_sp) {
  // We have no way to capture the incoming FileSP as the class isn't
  // instrumented, so pretend that it's always null.
  LLDB_INSTRUMENT_VA(this, file_sp);
}

SBFile::SBFile(const SBFile &rhs) : m_opaque_sp(rhs.m_opaque_sp) {
  LLDB_INSTRUMENT_VA(this, rhs);
}

SBFile &SBFile ::operator=(const SBFile &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_sp = rhs.m_opaque_sp;
  return *this;
}

SBFile::SBFile() { LLDB_INSTRUMENT_VA(this); }

SBFile::SBFile(FILE *file, bool transfer_ownership) {
  LLDB_INSTRUMENT_VA(this, file, transfer_ownership);

  m_opaque_sp = std::make_shared<NativeFile>(file, transfer_ownership);
}

SBFile::SBFile(int fd, const char *mode, bool transfer_owndership) {
  LLDB_INSTRUMENT_VA(this, fd, mode, transfer_owndership);

  auto options = File::GetOptionsFromMode(mode);
  if (!options) {
    llvm::consumeError(options.takeError());
    return;
  }
  m_opaque_sp =
      std::make_shared<NativeFile>(fd, options.get(), transfer_owndership);
}

SBError SBFile::Read(uint8_t *buf, size_t num_bytes, size_t *bytes_read) {
  LLDB_INSTRUMENT_VA(this, buf, num_bytes, bytes_read);

  SBError error;
  if (!m_opaque_sp) {
    error.SetErrorString("invalid SBFile");
    *bytes_read = 0;
  } else {
    Status status = m_opaque_sp->Read(buf, num_bytes);
    error.SetError(status);
    *bytes_read = num_bytes;
  }
  return error;
}

SBError SBFile::Write(const uint8_t *buf, size_t num_bytes,
                      size_t *bytes_written) {
  LLDB_INSTRUMENT_VA(this, buf, num_bytes, bytes_written);

  SBError error;
  if (!m_opaque_sp) {
    error.SetErrorString("invalid SBFile");
    *bytes_written = 0;
  } else {
    Status status = m_opaque_sp->Write(buf, num_bytes);
    error.SetError(status);
    *bytes_written = num_bytes;
  }
  return error;
}

SBError SBFile::Flush() {
  LLDB_INSTRUMENT_VA(this);

  SBError error;
  if (!m_opaque_sp) {
    error.SetErrorString("invalid SBFile");
  } else {
    Status status = m_opaque_sp->Flush();
    error.SetError(status);
  }
  return error;
}

bool SBFile::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return m_opaque_sp && m_opaque_sp->IsValid();
}

SBError SBFile::Close() {
  LLDB_INSTRUMENT_VA(this);
  SBError error;
  if (m_opaque_sp) {
    Status status = m_opaque_sp->Close();
    error.SetError(status);
  }
  return error;
}

SBFile::operator bool() const {
  LLDB_INSTRUMENT_VA(this);
  return IsValid();
}

bool SBFile::operator!() const {
  LLDB_INSTRUMENT_VA(this);
  return !IsValid();
}

FileSP SBFile::GetFile() const {
  LLDB_INSTRUMENT_VA(this);
  return m_opaque_sp;
}
