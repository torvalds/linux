//===-- ModuleCache.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_MODULECACHE_H
#define LLDB_TARGET_MODULECACHE_H

#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "lldb/Host/File.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Status.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace lldb_private {

class Module;
class UUID;

/// \class ModuleCache ModuleCache.h "lldb/Target/ModuleCache.h"
/// A module cache class.
///
/// Caches locally modules that are downloaded from remote targets. Each
/// cached module maintains 2 views:
///  - UUID view:
///  /${CACHE_ROOT}/${PLATFORM_NAME}/.cache/${UUID}/${MODULE_FILENAME}
///  - Sysroot view:
///  /${CACHE_ROOT}/${PLATFORM_NAME}/${HOSTNAME}/${MODULE_FULL_FILEPATH}
///
/// UUID views stores a real module file, whereas Sysroot view holds a symbolic
/// link to UUID-view file.
///
/// Example:
/// UUID view   :
/// /tmp/lldb/remote-
/// linux/.cache/30C94DC6-6A1F-E951-80C3-D68D2B89E576-D5AE213C/libc.so.6
/// Sysroot view: /tmp/lldb/remote-linux/ubuntu/lib/x86_64-linux-gnu/libc.so.6

class ModuleCache {
public:
  using ModuleDownloader =
      std::function<Status(const ModuleSpec &, const FileSpec &)>;
  using SymfileDownloader =
      std::function<Status(const lldb::ModuleSP &, const FileSpec &)>;

  Status GetAndPut(const FileSpec &root_dir_spec, const char *hostname,
                   const ModuleSpec &module_spec,
                   const ModuleDownloader &module_downloader,
                   const SymfileDownloader &symfile_downloader,
                   lldb::ModuleSP &cached_module_sp, bool *did_create_ptr);

private:
  Status Put(const FileSpec &root_dir_spec, const char *hostname,
             const ModuleSpec &module_spec, const FileSpec &tmp_file,
             const FileSpec &target_file);

  Status Get(const FileSpec &root_dir_spec, const char *hostname,
             const ModuleSpec &module_spec, lldb::ModuleSP &cached_module_sp,
             bool *did_create_ptr);

  std::unordered_map<std::string, lldb::ModuleWP> m_loaded_modules;
};

} // namespace lldb_private

#endif // LLDB_TARGET_MODULECACHE_H
