//===-- FifoFiles.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FifoFiles.h"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <chrono>
#include <fstream>
#include <future>
#include <optional>
#include <thread>

#include "llvm/Support/FileSystem.h"

#include "lldb/lldb-defines.h"

using namespace llvm;

namespace lldb_dap {

FifoFile::FifoFile(StringRef path) : m_path(path) {}

FifoFile::~FifoFile() {
#if !defined(_WIN32)
  unlink(m_path.c_str());
#endif
}

Expected<std::shared_ptr<FifoFile>> CreateFifoFile(StringRef path) {
#if defined(_WIN32)
  return createStringError(inconvertibleErrorCode(), "Unimplemented");
#else
  if (int err = mkfifo(path.data(), 0600))
    return createStringError(std::error_code(err, std::generic_category()),
                             "Couldn't create fifo file: %s", path.data());
  return std::make_shared<FifoFile>(path);
#endif
}

FifoFileIO::FifoFileIO(StringRef fifo_file, StringRef other_endpoint_name)
    : m_fifo_file(fifo_file), m_other_endpoint_name(other_endpoint_name) {}

Expected<json::Value> FifoFileIO::ReadJSON(std::chrono::milliseconds timeout) {
  // We use a pointer for this future, because otherwise its normal destructor
  // would wait for the getline to end, rendering the timeout useless.
  std::optional<std::string> line;
  std::future<void> *future =
      new std::future<void>(std::async(std::launch::async, [&]() {
        std::ifstream reader(m_fifo_file, std::ifstream::in);
        std::string buffer;
        std::getline(reader, buffer);
        if (!buffer.empty())
          line = buffer;
      }));
  if (future->wait_for(timeout) == std::future_status::timeout || !line)
    // Indeed this is a leak, but it's intentional. "future" obj destructor
    //  will block on waiting for the worker thread to join. And the worker
    //  thread might be stuck in blocking I/O. Intentionally leaking the  obj
    //  as a hack to avoid blocking main thread, and adding annotation to
    //  supress static code inspection warnings

    // coverity[leaked_storage]
    return createStringError(inconvertibleErrorCode(),
                             "Timed out trying to get messages from the " +
                                 m_other_endpoint_name);
  delete future;
  return json::parse(*line);
}

Error FifoFileIO::SendJSON(const json::Value &json,
                           std::chrono::milliseconds timeout) {
  bool done = false;
  std::future<void> *future =
      new std::future<void>(std::async(std::launch::async, [&]() {
        std::ofstream writer(m_fifo_file, std::ofstream::out);
        writer << JSONToString(json) << std::endl;
        done = true;
      }));
  if (future->wait_for(timeout) == std::future_status::timeout || !done) {
    // Indeed this is a leak, but it's intentional. "future" obj destructor will
    // block on waiting for the worker thread to join. And the worker thread
    // might be stuck in blocking I/O. Intentionally leaking the  obj as a hack
    // to avoid blocking main thread, and adding annotation to supress static
    // code inspection warnings"

    // coverity[leaked_storage]
    return createStringError(inconvertibleErrorCode(),
                             "Timed out trying to send messages to the " +
                                 m_other_endpoint_name);
  }
  delete future;
  return Error::success();
}

} // namespace lldb_dap
