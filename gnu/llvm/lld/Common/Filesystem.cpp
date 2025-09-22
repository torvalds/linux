//===- Filesystem.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a few utility functions to handle files.
//
//===----------------------------------------------------------------------===//

#include "lld/Common/Filesystem.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeProfiler.h"
#if LLVM_ON_UNIX
#include <unistd.h>
#endif
#include <thread>

using namespace llvm;
using namespace lld;

// Removes a given file asynchronously. This is a performance hack,
// so remove this when operating systems are improved.
//
// On Linux (and probably on other Unix-like systems), unlink(2) is a
// noticeably slow system call. As of 2016, unlink takes 250
// milliseconds to remove a 1 GB file on ext4 filesystem on my machine.
//
// To create a new result file, we first remove existing file. So, if
// you repeatedly link a 1 GB program in a regular compile-link-debug
// cycle, every cycle wastes 250 milliseconds only to remove a file.
// Since LLD can link a 1 GB binary in about 5 seconds, that waste
// actually counts.
//
// This function spawns a background thread to remove the file.
// The calling thread returns almost immediately.
void lld::unlinkAsync(StringRef path) {
  if (!sys::fs::exists(path) || !sys::fs::is_regular_file(path))
    return;

// Removing a file is async on windows.
#if defined(_WIN32)
  // On Windows co-operative programs can be expected to open LLD's
  // output in FILE_SHARE_DELETE mode. This allows us to delete the
  // file (by moving it to a temporary filename and then deleting
  // it) so that we can link another output file that overwrites
  // the existing file, even if the current file is in use.
  //
  // This is done on a best effort basis - we do not error if the
  // operation fails. The consequence is merely that the user
  // experiences an inconvenient work-flow.
  //
  // The code here allows LLD to work on all versions of Windows.
  // However, at Windows 10 1903 it seems that the behavior of
  // Windows has changed, so that we could simply delete the output
  // file. This code should be simplified once support for older
  // versions of Windows is dropped.
  //
  // Warning: It seems that the WINVER and _WIN32_WINNT preprocessor
  // defines affect the behavior of the Windows versions of the calls
  // we are using here. If this code stops working this is worth
  // bearing in mind.
  SmallString<128> tmpName;
  if (!sys::fs::createUniqueFile(path + "%%%%%%%%.tmp", tmpName)) {
    if (!sys::fs::rename(path, tmpName))
      path = tmpName;
    else
      sys::fs::remove(tmpName);
  }
  sys::fs::remove(path);
#else
  if (parallel::strategy.ThreadsRequested == 1)
    return;

  // We cannot just remove path from a different thread because we are now going
  // to create path as a new file.
  // Instead we open the file and unlink it on this thread. The unlink is fast
  // since the open fd guarantees that it is not removing the last reference.
  int fd;
  std::error_code ec = sys::fs::openFileForRead(path, fd);
  sys::fs::remove(path);

  if (ec)
    return;

  // close and therefore remove TempPath in background.
  std::mutex m;
  std::condition_variable cv;
  bool started = false;
  std::thread([&, fd] {
    {
      std::lock_guard<std::mutex> l(m);
      started = true;
      cv.notify_all();
    }
    ::close(fd);
  }).detach();

  // GLIBC 2.26 and earlier have race condition that crashes an entire process
  // if the main thread calls exit(2) while other thread is starting up.
  std::unique_lock<std::mutex> l(m);
  cv.wait(l, [&] { return started; });
#endif
}

// Simulate file creation to see if Path is writable.
//
// Determining whether a file is writable or not is amazingly hard,
// and after all the only reliable way of doing that is to actually
// create a file. But we don't want to do that in this function
// because LLD shouldn't update any file if it will end in a failure.
// We also don't want to reimplement heuristics to determine if a
// file is writable. So we'll let FileOutputBuffer do the work.
//
// FileOutputBuffer doesn't touch a destination file until commit()
// is called. We use that class without calling commit() to predict
// if the given file is writable.
std::error_code lld::tryCreateFile(StringRef path) {
  llvm::TimeTraceScope timeScope("Try create output file");
  if (path.empty())
    return std::error_code();
  if (path == "-")
    return std::error_code();
  return errorToErrorCode(FileOutputBuffer::create(path, 1).takeError());
}

// Creates an empty file to and returns a raw_fd_ostream to write to it.
std::unique_ptr<raw_fd_ostream> lld::openFile(StringRef file) {
  std::error_code ec;
  auto ret =
      std::make_unique<raw_fd_ostream>(file, ec, sys::fs::OpenFlags::OF_None);
  if (ec) {
    error("cannot open " + file + ": " + ec.message());
    return nullptr;
  }
  return ret;
}

// The merged bitcode after LTO is large. Try opening a file stream that
// supports reading, seeking and writing. Such a file allows BitcodeWriter to
// flush buffered data to reduce memory consumption. If this fails, open a file
// stream that supports only write.
std::unique_ptr<raw_fd_ostream> lld::openLTOOutputFile(StringRef file) {
  std::error_code ec;
  std::unique_ptr<raw_fd_ostream> fs =
      std::make_unique<raw_fd_stream>(file, ec);
  if (!ec)
    return fs;
  return openFile(file);
}
