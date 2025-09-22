//===-- ZipFileResolver.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/common/ZipFileResolver.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/ZipFile.h"

using namespace lldb_private;
using namespace llvm::support;

bool ZipFileResolver::ResolveSharedLibraryPath(const FileSpec &file_spec,
                                               FileKind &file_kind,
                                               std::string &file_path,
                                               lldb::offset_t &so_file_offset,
                                               lldb::offset_t &so_file_size) {
  // When bionic loads .so file from APK or zip file, this file_spec will be
  // "zip_path!/so_path". Otherwise it is just a normal file path.
  static constexpr llvm::StringLiteral k_zip_separator("!/");
  std::string path(file_spec.GetPath());
  size_t pos = path.find(k_zip_separator);

#if defined(_WIN32)
  // When the file_spec is resolved as a Windows path, the zip .so path will be
  // "zip_path!\so_path". Support both patterns on Windows.
  static constexpr llvm::StringLiteral k_zip_separator_win("!\\");
  if (pos == std::string::npos)
    pos = path.find(k_zip_separator_win);
#endif

  if (pos == std::string::npos) {
    // This file_spec does not contain the zip separator.
    // Treat this file_spec as a normal file.
    // so_file_offset and so_file_size should be 0.
    file_kind = FileKind::eFileKindNormal;
    file_path = path;
    so_file_offset = 0;
    so_file_size = 0;
    return true;
  }

  // This file_spec is a zip .so path. Extract the zip path and the .so path.
  std::string zip_path(path.substr(0, pos));
  std::string so_path(path.substr(pos + k_zip_separator.size()));

#if defined(_WIN32)
  // Replace the .so path to use POSIX file separator for file searching inside
  // the zip file.
  std::replace(so_path.begin(), so_path.end(), '\\', '/');
#endif

  // Try to find the .so file from the zip file.
  FileSpec zip_file_spec(zip_path);
  uint64_t zip_file_size = FileSystem::Instance().GetByteSize(zip_file_spec);
  lldb::DataBufferSP zip_data =
      FileSystem::Instance().CreateDataBuffer(zip_file_spec, zip_file_size);
  if (ZipFile::Find(zip_data, so_path, so_file_offset, so_file_size)) {
    // Found the .so file from the zip file and got the file offset and size.
    // Return the zip path. so_file_offset and so_file_size are already set.
    file_kind = FileKind::eFileKindZip;
    file_path = zip_path;
    return true;
  }

  return false;
}
