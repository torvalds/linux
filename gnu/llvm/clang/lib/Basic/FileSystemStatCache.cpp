//===- FileSystemStatCache.cpp - Caching for 'stat' calls -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the FileSystemStatCache interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/FileSystemStatCache.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <utility>

using namespace clang;

void FileSystemStatCache::anchor() {}

/// FileSystemStatCache::get - Get the 'stat' information for the specified
/// path, using the cache to accelerate it if possible.  This returns true if
/// the path does not exist or false if it exists.
///
/// If isFile is true, then this lookup should only return success for files
/// (not directories).  If it is false this lookup should only return
/// success for directories (not files).  On a successful file lookup, the
/// implementation can optionally fill in FileDescriptor with a valid
/// descriptor and the client guarantees that it will close it.
std::error_code
FileSystemStatCache::get(StringRef Path, llvm::vfs::Status &Status,
                         bool isFile, std::unique_ptr<llvm::vfs::File> *F,
                         FileSystemStatCache *Cache,
                         llvm::vfs::FileSystem &FS) {
  bool isForDir = !isFile;
  std::error_code RetCode;

  // If we have a cache, use it to resolve the stat query.
  if (Cache)
    RetCode = Cache->getStat(Path, Status, isFile, F, FS);
  else if (isForDir || !F) {
    // If this is a directory or a file descriptor is not needed and we have
    // no cache, just go to the file system.
    llvm::ErrorOr<llvm::vfs::Status> StatusOrErr = FS.status(Path);
    if (!StatusOrErr) {
      RetCode = StatusOrErr.getError();
    } else {
      Status = *StatusOrErr;
    }
  } else {
    // Otherwise, we have to go to the filesystem.  We can always just use
    // 'stat' here, but (for files) the client is asking whether the file exists
    // because it wants to turn around and *open* it.  It is more efficient to
    // do "open+fstat" on success than it is to do "stat+open".
    //
    // Because of this, check to see if the file exists with 'open'.  If the
    // open succeeds, use fstat to get the stat info.
    auto OwnedFile = FS.openFileForRead(Path);

    if (!OwnedFile) {
      // If the open fails, our "stat" fails.
      RetCode = OwnedFile.getError();
    } else {
      // Otherwise, the open succeeded.  Do an fstat to get the information
      // about the file.  We'll end up returning the open file descriptor to the
      // client to do what they please with it.
      llvm::ErrorOr<llvm::vfs::Status> StatusOrErr = (*OwnedFile)->status();
      if (StatusOrErr) {
        Status = *StatusOrErr;
        *F = std::move(*OwnedFile);
      } else {
        // fstat rarely fails.  If it does, claim the initial open didn't
        // succeed.
        *F = nullptr;
        RetCode = StatusOrErr.getError();
      }
    }
  }

  // If the path doesn't exist, return failure.
  if (RetCode)
    return RetCode;

  // If the path exists, make sure that its "directoryness" matches the clients
  // demands.
  if (Status.isDirectory() != isForDir) {
    // If not, close the file if opened.
    if (F)
      *F = nullptr;
    return std::make_error_code(
        Status.isDirectory() ?
            std::errc::is_a_directory : std::errc::not_a_directory);
  }

  return std::error_code();
}

std::error_code
MemorizeStatCalls::getStat(StringRef Path, llvm::vfs::Status &Status,
                           bool isFile,
                           std::unique_ptr<llvm::vfs::File> *F,
                           llvm::vfs::FileSystem &FS) {
  auto err = get(Path, Status, isFile, F, nullptr, FS);
  if (err) {
    // Do not cache failed stats, it is easy to construct common inconsistent
    // situations if we do, and they are not important for PCH performance
    // (which currently only needs the stats to construct the initial
    // FileManager entries).
    return err;
  }

  // Cache file 'stat' results and directories with absolutely paths.
  if (!Status.isDirectory() || llvm::sys::path::is_absolute(Path))
    StatCalls[Path] = Status;

  return std::error_code();
}
