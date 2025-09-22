//===-- TempFile.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/FileSystem.h"
#include <TempFile.h>

using namespace lldb_fuzzer;
using namespace llvm;

TempFile::~TempFile() {
  if (!m_path.empty())
    sys::fs::remove(m_path.str(), true);
}

std::unique_ptr<TempFile> TempFile::Create(uint8_t *data, size_t size) {
  int fd;
  std::unique_ptr<TempFile> temp_file = std::make_unique<TempFile>();
  std::error_code ec = sys::fs::createTemporaryFile("lldb-fuzzer", "input", fd,
                                                    temp_file->m_path);
  if (ec)
    return nullptr;

  raw_fd_ostream os(fd, true);
  os.write(reinterpret_cast<const char *>(data), size);
  os.close();

  return temp_file;
}
