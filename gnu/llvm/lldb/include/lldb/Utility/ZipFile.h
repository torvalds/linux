//===-- ZipFile.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ZIPFILE_H
#define LLDB_UTILITY_ZIPFILE_H

#include "lldb/lldb-private.h"

namespace lldb_private {

/// In Android API level 23 and above, bionic dynamic linker is able to load
/// .so file directly from APK or .zip file. This is a utility class to find
/// .so file offset and size from zip file.
/// https://android.googlesource.com/platform/bionic/+/master/
/// android-changes-for-ndk-developers.md#
/// opening-shared-libraries-directly-from-an-apk
class ZipFile {
public:
  static bool Find(lldb::DataBufferSP zip_data, const llvm::StringRef file_path,
                   lldb::offset_t &file_offset, lldb::offset_t &file_size);
};

} // end of namespace lldb_private

#endif // LLDB_UTILITY_ZIPFILE_H
