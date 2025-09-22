//===-- FileSystem.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Host/windows/windows.h"

#include <share.h>
#include <shellapi.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "lldb/Host/FileSystem.h"
#include "lldb/Host/windows/AutoHandle.h"
#include "lldb/Host/windows/PosixApi.h"

#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FileSystem.h"

using namespace lldb_private;

const char *FileSystem::DEV_NULL = "nul";

const char *FileSystem::PATH_CONVERSION_ERROR =
    "Error converting path between UTF-8 and native encoding";

Status FileSystem::Symlink(const FileSpec &src, const FileSpec &dst) {
  Status error;
  std::wstring wsrc, wdst;
  if (!llvm::ConvertUTF8toWide(src.GetPath(), wsrc) ||
      !llvm::ConvertUTF8toWide(dst.GetPath(), wdst))
    error.SetErrorString(PATH_CONVERSION_ERROR);
  if (error.Fail())
    return error;
  DWORD attrib = ::GetFileAttributesW(wdst.c_str());
  if (attrib == INVALID_FILE_ATTRIBUTES) {
    error.SetError(::GetLastError(), lldb::eErrorTypeWin32);
    return error;
  }
  bool is_directory = !!(attrib & FILE_ATTRIBUTE_DIRECTORY);
  DWORD flag = is_directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  BOOL result = ::CreateSymbolicLinkW(wsrc.c_str(), wdst.c_str(), flag);
  if (!result)
    error.SetError(::GetLastError(), lldb::eErrorTypeWin32);
  return error;
}

Status FileSystem::Readlink(const FileSpec &src, FileSpec &dst) {
  Status error;
  std::wstring wsrc;
  if (!llvm::ConvertUTF8toWide(src.GetPath(), wsrc)) {
    error.SetErrorString(PATH_CONVERSION_ERROR);
    return error;
  }

  HANDLE h = ::CreateFileW(wsrc.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    error.SetError(::GetLastError(), lldb::eErrorTypeWin32);
    return error;
  }

  std::vector<wchar_t> buf(PATH_MAX + 1);
  // Subtract 1 from the path length since this function does not add a null
  // terminator.
  DWORD result = ::GetFinalPathNameByHandleW(
      h, buf.data(), buf.size() - 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  std::string path;
  if (result == 0)
    error.SetError(::GetLastError(), lldb::eErrorTypeWin32);
  else if (!llvm::convertWideToUTF8(buf.data(), path))
    error.SetErrorString(PATH_CONVERSION_ERROR);
  else
    dst.SetFile(path, FileSpec::Style::native);

  ::CloseHandle(h);
  return error;
}

Status FileSystem::ResolveSymbolicLink(const FileSpec &src, FileSpec &dst) {
  return Status("ResolveSymbolicLink() isn't implemented on Windows");
}

FILE *FileSystem::Fopen(const char *path, const char *mode) {
  std::wstring wpath, wmode;
  if (!llvm::ConvertUTF8toWide(path, wpath))
    return nullptr;
  if (!llvm::ConvertUTF8toWide(mode, wmode))
    return nullptr;
  FILE *file;
  if (_wfopen_s(&file, wpath.c_str(), wmode.c_str()) != 0)
    return nullptr;
  return file;
}

int FileSystem::Open(const char *path, int flags, int mode) {
  std::wstring wpath;
  if (!llvm::ConvertUTF8toWide(path, wpath))
    return -1;
  // All other bits are rejected by _wsopen_s
  mode = mode & (_S_IREAD | _S_IWRITE);
  int result;
  ::_wsopen_s(&result, wpath.c_str(), flags, _SH_DENYNO, mode);
  return result;
}
