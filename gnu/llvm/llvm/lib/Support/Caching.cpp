//===-Caching.cpp - LLVM Local File Cache ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the localCache function, which simplifies creating,
// adding to, and querying a local file system cache. localCache takes care of
// periodically pruning older files from the cache using a CachePruningPolicy.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Caching.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

using namespace llvm;

Expected<FileCache> llvm::localCache(const Twine &CacheNameRef,
                                     const Twine &TempFilePrefixRef,
                                     const Twine &CacheDirectoryPathRef,
                                     AddBufferFn AddBuffer) {

  // Create local copies which are safely captured-by-copy in lambdas
  SmallString<64> CacheName, TempFilePrefix, CacheDirectoryPath;
  CacheNameRef.toVector(CacheName);
  TempFilePrefixRef.toVector(TempFilePrefix);
  CacheDirectoryPathRef.toVector(CacheDirectoryPath);

  return [=](unsigned Task, StringRef Key,
             const Twine &ModuleName) -> Expected<AddStreamFn> {
    // This choice of file name allows the cache to be pruned (see pruneCache()
    // in include/llvm/Support/CachePruning.h).
    SmallString<64> EntryPath;
    sys::path::append(EntryPath, CacheDirectoryPath, "llvmcache-" + Key);
    // First, see if we have a cache hit.
    SmallString<64> ResultPath;
    Expected<sys::fs::file_t> FDOrErr = sys::fs::openNativeFileForRead(
        Twine(EntryPath), sys::fs::OF_UpdateAtime, &ResultPath);
    std::error_code EC;
    if (FDOrErr) {
      ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
          MemoryBuffer::getOpenFile(*FDOrErr, EntryPath,
                                    /*FileSize=*/-1,
                                    /*RequiresNullTerminator=*/false);
      sys::fs::closeFile(*FDOrErr);
      if (MBOrErr) {
        AddBuffer(Task, ModuleName, std::move(*MBOrErr));
        return AddStreamFn();
      }
      EC = MBOrErr.getError();
    } else {
      EC = errorToErrorCode(FDOrErr.takeError());
    }

    // On Windows we can fail to open a cache file with a permission denied
    // error. This generally means that another process has requested to delete
    // the file while it is still open, but it could also mean that another
    // process has opened the file without the sharing permissions we need.
    // Since the file is probably being deleted we handle it in the same way as
    // if the file did not exist at all.
    if (EC != errc::no_such_file_or_directory && EC != errc::permission_denied)
      return createStringError(EC, Twine("Failed to open cache file ") +
                                       EntryPath + ": " + EC.message() + "\n");

    // This file stream is responsible for commiting the resulting file to the
    // cache and calling AddBuffer to add it to the link.
    struct CacheStream : CachedFileStream {
      AddBufferFn AddBuffer;
      sys::fs::TempFile TempFile;
      std::string ModuleName;
      unsigned Task;

      CacheStream(std::unique_ptr<raw_pwrite_stream> OS, AddBufferFn AddBuffer,
                  sys::fs::TempFile TempFile, std::string EntryPath,
                  std::string ModuleName, unsigned Task)
          : CachedFileStream(std::move(OS), std::move(EntryPath)),
            AddBuffer(std::move(AddBuffer)), TempFile(std::move(TempFile)),
            ModuleName(ModuleName), Task(Task) {}

      ~CacheStream() {
        // TODO: Manually commit rather than using non-trivial destructor,
        // allowing to replace report_fatal_errors with a return Error.

        // Make sure the stream is closed before committing it.
        OS.reset();

        // Open the file first to avoid racing with a cache pruner.
        ErrorOr<std::unique_ptr<MemoryBuffer>> MBOrErr =
            MemoryBuffer::getOpenFile(
                sys::fs::convertFDToNativeFile(TempFile.FD), ObjectPathName,
                /*FileSize=*/-1, /*RequiresNullTerminator=*/false);
        if (!MBOrErr)
          report_fatal_error(Twine("Failed to open new cache file ") +
                             TempFile.TmpName + ": " +
                             MBOrErr.getError().message() + "\n");

        // On POSIX systems, this will atomically replace the destination if
        // it already exists. We try to emulate this on Windows, but this may
        // fail with a permission denied error (for example, if the destination
        // is currently opened by another process that does not give us the
        // sharing permissions we need). Since the existing file should be
        // semantically equivalent to the one we are trying to write, we give
        // AddBuffer a copy of the bytes we wrote in that case. We do this
        // instead of just using the existing file, because the pruner might
        // delete the file before we get a chance to use it.
        Error E = TempFile.keep(ObjectPathName);
        E = handleErrors(std::move(E), [&](const ECError &E) -> Error {
          std::error_code EC = E.convertToErrorCode();
          if (EC != errc::permission_denied)
            return errorCodeToError(EC);

          auto MBCopy = MemoryBuffer::getMemBufferCopy((*MBOrErr)->getBuffer(),
                                                       ObjectPathName);
          MBOrErr = std::move(MBCopy);

          // FIXME: should we consume the discard error?
          consumeError(TempFile.discard());

          return Error::success();
        });

        if (E)
          report_fatal_error(Twine("Failed to rename temporary file ") +
                             TempFile.TmpName + " to " + ObjectPathName + ": " +
                             toString(std::move(E)) + "\n");

        AddBuffer(Task, ModuleName, std::move(*MBOrErr));
      }
    };

    return [=](size_t Task, const Twine &ModuleName)
               -> Expected<std::unique_ptr<CachedFileStream>> {
      // Create the cache directory if not already done. Doing this lazily
      // ensures the filesystem isn't mutated until the cache is.
      if (std::error_code EC = sys::fs::create_directories(
              CacheDirectoryPath, /*IgnoreExisting=*/true))
        return createStringError(EC, Twine("can't create cache directory ") +
                                         CacheDirectoryPath + ": " +
                                         EC.message());

      // Write to a temporary to avoid race condition
      SmallString<64> TempFilenameModel;
      sys::path::append(TempFilenameModel, CacheDirectoryPath,
                        TempFilePrefix + "-%%%%%%.tmp.o");
      Expected<sys::fs::TempFile> Temp = sys::fs::TempFile::create(
          TempFilenameModel, sys::fs::owner_read | sys::fs::owner_write);
      if (!Temp)
        return createStringError(errc::io_error,
                                 toString(Temp.takeError()) + ": " + CacheName +
                                     ": Can't get a temporary file");

      // This CacheStream will move the temporary file into the cache when done.
      return std::make_unique<CacheStream>(
          std::make_unique<raw_fd_ostream>(Temp->FD, /* ShouldClose */ false),
          AddBuffer, std::move(*Temp), std::string(EntryPath), ModuleName.str(),
          Task);
    };
  };
}
