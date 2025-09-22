//===-- FileCache.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLDB_HOST_FILECACHE_H
#define LLDB_HOST_FILECACHE_H

#include <cstdint>
#include <map>

#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "lldb/Host/File.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"

namespace lldb_private {
class FileCache {
private:
  FileCache() = default;

  typedef std::map<lldb::user_id_t, lldb::FileUP> FDToFileMap;

public:
  static FileCache &GetInstance();

  lldb::user_id_t OpenFile(const FileSpec &file_spec, File::OpenOptions flags,
                           uint32_t mode, Status &error);
  bool CloseFile(lldb::user_id_t fd, Status &error);

  uint64_t WriteFile(lldb::user_id_t fd, uint64_t offset, const void *src,
                     uint64_t src_len, Status &error);
  uint64_t ReadFile(lldb::user_id_t fd, uint64_t offset, void *dst,
                    uint64_t dst_len, Status &error);

private:
  static FileCache *m_instance;

  FDToFileMap m_cache;
};
}

#endif
