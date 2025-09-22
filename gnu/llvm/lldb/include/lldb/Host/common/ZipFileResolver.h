//===-- ZipFileResolver.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_ZIPFILERESOLVER_H
#define LLDB_HOST_ZIPFILERESOLVER_H

#include "lldb/lldb-private.h"

namespace lldb_private {

/// In Android API level 23 and above, bionic dynamic linker is able to load
/// .so file directly from APK or .zip file. This is a utility class to resolve
/// the file spec in order to get the zip path and the .so file offset and size
/// if the file spec contains "zip_path!/so_path".
/// https://android.googlesource.com/platform/bionic/+/master/
/// android-changes-for-ndk-developers.md#
/// opening-shared-libraries-directly-from-an-apk
class ZipFileResolver {
public:
  enum FileKind {
    eFileKindInvalid = 0,
    eFileKindNormal,
    eFileKindZip,
  };

  static bool ResolveSharedLibraryPath(const FileSpec &file_spec,
                                       FileKind &file_kind,
                                       std::string &file_path,
                                       lldb::offset_t &so_file_offset,
                                       lldb::offset_t &so_file_size);
};

} // end of namespace lldb_private

#endif // LLDB_HOST_ZIPFILERESOLVER_H
