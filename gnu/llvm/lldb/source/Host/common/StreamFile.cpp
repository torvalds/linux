//===-- StreamFile.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/StreamFile.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include <cstdio>

using namespace lldb;
using namespace lldb_private;

StreamFile::StreamFile(uint32_t flags, uint32_t addr_size, ByteOrder byte_order)
    : Stream(flags, addr_size, byte_order) {
  m_file_sp = std::make_shared<File>();
}

StreamFile::StreamFile(int fd, bool transfer_ownership) : Stream() {
  m_file_sp = std::make_shared<NativeFile>(fd, File::eOpenOptionWriteOnly,
                                           transfer_ownership);
}

StreamFile::StreamFile(FILE *fh, bool transfer_ownership) : Stream() {
  m_file_sp = std::make_shared<NativeFile>(fh, transfer_ownership);
}

StreamFile::StreamFile(const char *path, File::OpenOptions options,
                       uint32_t permissions)
    : Stream() {
  auto file = FileSystem::Instance().Open(FileSpec(path), options, permissions);
  if (file)
    m_file_sp = std::move(file.get());
  else {
    // TODO refactor this so the error gets popagated up instead of logged here.
    LLDB_LOG_ERROR(GetLog(LLDBLog::Host), file.takeError(),
                   "Cannot open {1}: {0}", path);
    m_file_sp = std::make_shared<File>();
  }
}

StreamFile::~StreamFile() = default;

void StreamFile::Flush() { m_file_sp->Flush(); }

size_t StreamFile::WriteImpl(const void *s, size_t length) {
  m_file_sp->Write(s, length);
  return length;
}
