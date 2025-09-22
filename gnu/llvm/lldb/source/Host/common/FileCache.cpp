//===-- FileCache.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/FileCache.h"

#include "lldb/Host/File.h"
#include "lldb/Host/FileSystem.h"

using namespace lldb;
using namespace lldb_private;

FileCache *FileCache::m_instance = nullptr;

FileCache &FileCache::GetInstance() {
  if (m_instance == nullptr)
    m_instance = new FileCache();

  return *m_instance;
}

lldb::user_id_t FileCache::OpenFile(const FileSpec &file_spec,
                                    File::OpenOptions flags, uint32_t mode,
                                    Status &error) {
  if (!file_spec) {
    error.SetErrorString("empty path");
    return UINT64_MAX;
  }
  auto file = FileSystem::Instance().Open(file_spec, flags, mode);
  if (!file) {
    error = file.takeError();
    return UINT64_MAX;
  }
  lldb::user_id_t fd = file.get()->GetDescriptor();
  m_cache[fd] = std::move(file.get());
  return fd;
}

bool FileCache::CloseFile(lldb::user_id_t fd, Status &error) {
  if (fd == UINT64_MAX) {
    error.SetErrorString("invalid file descriptor");
    return false;
  }
  FDToFileMap::iterator pos = m_cache.find(fd);
  if (pos == m_cache.end()) {
    error.SetErrorStringWithFormat("invalid host file descriptor %" PRIu64, fd);
    return false;
  }
  FileUP &file_up = pos->second;
  if (!file_up) {
    error.SetErrorString("invalid host backing file");
    return false;
  }
  error = file_up->Close();
  m_cache.erase(pos);
  return error.Success();
}

uint64_t FileCache::WriteFile(lldb::user_id_t fd, uint64_t offset,
                              const void *src, uint64_t src_len,
                              Status &error) {
  if (fd == UINT64_MAX) {
    error.SetErrorString("invalid file descriptor");
    return UINT64_MAX;
  }
  FDToFileMap::iterator pos = m_cache.find(fd);
  if (pos == m_cache.end()) {
    error.SetErrorStringWithFormat("invalid host file descriptor %" PRIu64, fd);
    return false;
  }
  FileUP &file_up = pos->second;
  if (!file_up) {
    error.SetErrorString("invalid host backing file");
    return UINT64_MAX;
  }
  if (static_cast<uint64_t>(file_up->SeekFromStart(offset, &error)) != offset ||
      error.Fail())
    return UINT64_MAX;
  size_t bytes_written = src_len;
  error = file_up->Write(src, bytes_written);
  if (error.Fail())
    return UINT64_MAX;
  return bytes_written;
}

uint64_t FileCache::ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                             uint64_t dst_len, Status &error) {
  if (fd == UINT64_MAX) {
    error.SetErrorString("invalid file descriptor");
    return UINT64_MAX;
  }
  FDToFileMap::iterator pos = m_cache.find(fd);
  if (pos == m_cache.end()) {
    error.SetErrorStringWithFormat("invalid host file descriptor %" PRIu64, fd);
    return false;
  }
  FileUP &file_up = pos->second;
  if (!file_up) {
    error.SetErrorString("invalid host backing file");
    return UINT64_MAX;
  }
  if (static_cast<uint64_t>(file_up->SeekFromStart(offset, &error)) != offset ||
      error.Fail())
    return UINT64_MAX;
  size_t bytes_read = dst_len;
  error = file_up->Read(dst, bytes_read);
  if (error.Fail())
    return UINT64_MAX;
  return bytes_read;
}
