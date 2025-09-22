//===-- SBFileSpec.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/API/SBFileSpec.h"
#include "Utils.h"
#include "lldb/API/SBStream.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/PosixApi.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Instrumentation.h"
#include "lldb/Utility/Stream.h"

#include "llvm/ADT/SmallString.h"

#include <cinttypes>
#include <climits>

using namespace lldb;
using namespace lldb_private;

SBFileSpec::SBFileSpec() : m_opaque_up(new lldb_private::FileSpec()) {
  LLDB_INSTRUMENT_VA(this);
}

SBFileSpec::SBFileSpec(const SBFileSpec &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  m_opaque_up = clone(rhs.m_opaque_up);
}

SBFileSpec::SBFileSpec(const lldb_private::FileSpec &fspec)
    : m_opaque_up(new lldb_private::FileSpec(fspec)) {}

// Deprecated!!!
SBFileSpec::SBFileSpec(const char *path) : m_opaque_up(new FileSpec(path)) {
  LLDB_INSTRUMENT_VA(this, path);

  FileSystem::Instance().Resolve(*m_opaque_up);
}

SBFileSpec::SBFileSpec(const char *path, bool resolve)
    : m_opaque_up(new FileSpec(path)) {
  LLDB_INSTRUMENT_VA(this, path, resolve);

  if (resolve)
    FileSystem::Instance().Resolve(*m_opaque_up);
}

SBFileSpec::~SBFileSpec() = default;

const SBFileSpec &SBFileSpec::operator=(const SBFileSpec &rhs) {
  LLDB_INSTRUMENT_VA(this, rhs);

  if (this != &rhs)
    m_opaque_up = clone(rhs.m_opaque_up);
  return *this;
}

bool SBFileSpec::operator==(const SBFileSpec &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return ref() == rhs.ref();
}

bool SBFileSpec::operator!=(const SBFileSpec &rhs) const {
  LLDB_INSTRUMENT_VA(this, rhs);

  return !(*this == rhs);
}

bool SBFileSpec::IsValid() const {
  LLDB_INSTRUMENT_VA(this);
  return this->operator bool();
}
SBFileSpec::operator bool() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->operator bool();
}

bool SBFileSpec::Exists() const {
  LLDB_INSTRUMENT_VA(this);

  return FileSystem::Instance().Exists(*m_opaque_up);
}

bool SBFileSpec::ResolveExecutableLocation() {
  LLDB_INSTRUMENT_VA(this);

  return FileSystem::Instance().ResolveExecutableLocation(*m_opaque_up);
}

int SBFileSpec::ResolvePath(const char *src_path, char *dst_path,
                            size_t dst_len) {
  LLDB_INSTRUMENT_VA(src_path, dst_path, dst_len);

  llvm::SmallString<64> result(src_path);
  FileSystem::Instance().Resolve(result);
  ::snprintf(dst_path, dst_len, "%s", result.c_str());
  return std::min(dst_len - 1, result.size());
}

const char *SBFileSpec::GetFilename() const {
  LLDB_INSTRUMENT_VA(this);

  return m_opaque_up->GetFilename().AsCString();
}

const char *SBFileSpec::GetDirectory() const {
  LLDB_INSTRUMENT_VA(this);

  FileSpec directory{*m_opaque_up};
  directory.ClearFilename();
  return directory.GetPathAsConstString().GetCString();
}

void SBFileSpec::SetFilename(const char *filename) {
  LLDB_INSTRUMENT_VA(this, filename);

  if (filename && filename[0])
    m_opaque_up->SetFilename(filename);
  else
    m_opaque_up->ClearFilename();
}

void SBFileSpec::SetDirectory(const char *directory) {
  LLDB_INSTRUMENT_VA(this, directory);

  if (directory && directory[0])
    m_opaque_up->SetDirectory(directory);
  else
    m_opaque_up->ClearDirectory();
}

uint32_t SBFileSpec::GetPath(char *dst_path, size_t dst_len) const {
  LLDB_INSTRUMENT_VA(this, dst_path, dst_len);

  uint32_t result = m_opaque_up->GetPath(dst_path, dst_len);

  if (result == 0 && dst_path && dst_len > 0)
    *dst_path = '\0';
  return result;
}

const lldb_private::FileSpec *SBFileSpec::operator->() const {
  return m_opaque_up.get();
}

const lldb_private::FileSpec *SBFileSpec::get() const {
  return m_opaque_up.get();
}

const lldb_private::FileSpec &SBFileSpec::operator*() const {
  return *m_opaque_up;
}

const lldb_private::FileSpec &SBFileSpec::ref() const { return *m_opaque_up; }

void SBFileSpec::SetFileSpec(const lldb_private::FileSpec &fs) {
  *m_opaque_up = fs;
}

bool SBFileSpec::GetDescription(SBStream &description) const {
  LLDB_INSTRUMENT_VA(this, description);

  Stream &strm = description.ref();
  char path[PATH_MAX];
  if (m_opaque_up->GetPath(path, sizeof(path)))
    strm.PutCString(path);
  return true;
}

void SBFileSpec::AppendPathComponent(const char *fn) {
  LLDB_INSTRUMENT_VA(this, fn);

  m_opaque_up->AppendPathComponent(fn);
}
