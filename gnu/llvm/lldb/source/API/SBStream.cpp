//===-- SBStream.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBStream.h"

#include "lldb/API/SBFile.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

SBStream::SBStream() : m_opaque_up(new StreamString()) {
  LLDB_INSTRUMENT_VA(this);
}

SBStream::SBStream(SBStream &&rhs)
    : m_opaque_up(std::move(rhs.m_opaque_up)), m_is_file(rhs.m_is_file) {}

SBStream::~SBStream() = default;

bool SBStream::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBStream::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return (m_opaque_up != nullptr);
}

// If this stream is not redirected to a file, it will maintain a local cache
// for the stream data which can be accessed using this accessor.
const char *SBStream::GetData() {
  LLDB_INSTRUMENT_VA(this);

  if (m_is_file || m_opaque_up == nullptr)
    return nullptr;

  return ConstString(static_cast<StreamString *>(m_opaque_up.get())->GetData())
      .GetCString();
}

// If this stream is not redirected to a file, it will maintain a local cache
// for the stream output whose length can be accessed using this accessor.
size_t SBStream::GetSize() {
  LLDB_INSTRUMENT_VA(this);

  if (m_is_file || m_opaque_up == nullptr)
    return 0;

  return static_cast<StreamString *>(m_opaque_up.get())->GetSize();
}

void SBStream::Print(const char *str) {
  LLDB_INSTRUMENT_VA(this, str);

  Printf("%s", str);
}

void SBStream::Printf(const char *format, ...) {
  if (!format)
    return;
  va_list args;
  va_start(args, format);
  ref().PrintfVarArg(format, args);
  va_end(args);
}

void SBStream::RedirectToFile(const char *path, bool append) {
  LLDB_INSTRUMENT_VA(this, path, append);

  if (path == nullptr)
    return;

  std::string local_data;
  if (m_opaque_up) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (!m_is_file)
      local_data = std::string(
          static_cast<StreamString *>(m_opaque_up.get())->GetString());
  }
  auto open_options = File::eOpenOptionWriteOnly | File::eOpenOptionCanCreate;
  if (append)
    open_options |= File::eOpenOptionAppend;
  else
    open_options |= File::eOpenOptionTruncate;

  llvm::Expected<FileUP> file =
      FileSystem::Instance().Open(FileSpec(path), open_options);
  if (!file) {
    LLDB_LOG_ERROR(GetLog(LLDBLog::API), file.takeError(),
                   "Cannot open {1}: {0}", path);
    return;
  }

  m_opaque_up = std::make_unique<StreamFile>(std::move(file.get()));
  m_is_file = true;

  // If we had any data locally in our StreamString, then pass that along to
  // the to new file we are redirecting to.
  if (!local_data.empty())
    m_opaque_up->Write(&local_data[0], local_data.size());
}

void SBStream::RedirectToFileHandle(FILE *fh, bool transfer_fh_ownership) {
  LLDB_INSTRUMENT_VA(this, fh, transfer_fh_ownership);
  FileSP file = std::make_unique<NativeFile>(fh, transfer_fh_ownership);
  return RedirectToFile(file);
}

void SBStream::RedirectToFile(SBFile file) {
  LLDB_INSTRUMENT_VA(this, file)
  RedirectToFile(file.GetFile());
}

void SBStream::RedirectToFile(FileSP file_sp) {
  LLDB_INSTRUMENT_VA(this, file_sp);

  if (!file_sp || !file_sp->IsValid())
    return;

  std::string local_data;
  if (m_opaque_up) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (!m_is_file)
      local_data = std::string(
          static_cast<StreamString *>(m_opaque_up.get())->GetString());
  }

  m_opaque_up = std::make_unique<StreamFile>(file_sp);
  m_is_file = true;

  // If we had any data locally in our StreamString, then pass that along to
  // the to new file we are redirecting to.
  if (!local_data.empty())
    m_opaque_up->Write(&local_data[0], local_data.size());
}

void SBStream::RedirectToFileDescriptor(int fd, bool transfer_fh_ownership) {
  LLDB_INSTRUMENT_VA(this, fd, transfer_fh_ownership);

  std::string local_data;
  if (m_opaque_up) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (!m_is_file)
      local_data = std::string(
          static_cast<StreamString *>(m_opaque_up.get())->GetString());
  }

  m_opaque_up = std::make_unique<StreamFile>(fd, transfer_fh_ownership);
  m_is_file = true;

  // If we had any data locally in our StreamString, then pass that along to
  // the to new file we are redirecting to.
  if (!local_data.empty())
    m_opaque_up->Write(&local_data[0], local_data.size());
}

lldb_private::Stream *SBStream::operator->() { return m_opaque_up.get(); }

lldb_private::Stream *SBStream::get() { return m_opaque_up.get(); }

lldb_private::Stream &SBStream::ref() {
  if (m_opaque_up == nullptr)
    m_opaque_up = std::make_unique<StreamString>();
  return *m_opaque_up;
}

void SBStream::Clear() {
  LLDB_INSTRUMENT_VA(this);

  if (m_opaque_up) {
    // See if we have any locally backed data. If so, copy it so we can then
    // redirect it to the file so we don't lose the data
    if (m_is_file)
      m_opaque_up.reset();
    else
      static_cast<StreamString *>(m_opaque_up.get())->Clear();
  }
}
