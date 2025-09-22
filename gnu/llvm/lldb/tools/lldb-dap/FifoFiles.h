//===-- FifoFiles.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_FIFOFILES_H
#define LLDB_TOOLS_LLDB_DAP_FIFOFILES_H

#include "llvm/Config/llvm-config.h" // for LLVM_ON_UNIX
#include "llvm/Support/Error.h"

#include "JSONUtils.h"

#include <chrono>

namespace lldb_dap {

/// Struct that controls the life of a fifo file in the filesystem.
///
/// The file is destroyed when the destructor is invoked.
struct FifoFile {
  FifoFile(llvm::StringRef path);

  ~FifoFile();

  std::string m_path;
};

/// Create a fifo file in the filesystem.
///
/// \param[in] path
///     The path for the fifo file.
///
/// \return
///     A \a std::shared_ptr<FifoFile> if the file could be created, or an
///     \a llvm::Error in case of failures.
llvm::Expected<std::shared_ptr<FifoFile>> CreateFifoFile(llvm::StringRef path);

class FifoFileIO {
public:
  /// \param[in] fifo_file
  ///     The path to an input fifo file that exists in the file system.
  ///
  /// \param[in] other_endpoint_name
  ///     A human readable name for the other endpoint that will communicate
  ///     using this file. This is used for error messages.
  FifoFileIO(llvm::StringRef fifo_file, llvm::StringRef other_endpoint_name);

  /// Read the next JSON object from the underlying input fifo file.
  ///
  /// The JSON object is expected to be a single line delimited with \a
  /// std::endl.
  ///
  /// \return
  ///     An \a llvm::Error object indicating the success or failure of this
  ///     operation. Failures arise if the timeout is hit, the next line of text
  ///     from the fifo file is not a valid JSON object, or is it impossible to
  ///     read from the file.
  llvm::Expected<llvm::json::Value> ReadJSON(std::chrono::milliseconds timeout);

  /// Serialize a JSON object and write it to the underlying output fifo file.
  ///
  /// \param[in] json
  ///     The JSON object to send. It will be printed as a single line delimited
  ///     with \a std::endl.
  ///
  /// \param[in] timeout
  ///     A timeout for how long we should until for the data to be consumed.
  ///
  /// \return
  ///     An \a llvm::Error object indicating whether the data was consumed by
  ///     a reader or not.
  llvm::Error SendJSON(
      const llvm::json::Value &json,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(20000));

private:
  std::string m_fifo_file;
  std::string m_other_endpoint_name;
};

} // namespace lldb_dap

#endif // LLDB_TOOLS_LLDB_DAP_FIFOFILES_H
