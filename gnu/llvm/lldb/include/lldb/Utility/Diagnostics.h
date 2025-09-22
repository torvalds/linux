//===-- Diagnostics.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_DIAGNOSTICS_H
#define LLDB_UTILITY_DIAGNOSTICS_H

#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/Log.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Error.h"

#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace lldb_private {

/// Diagnostics are a collection of files to help investigate bugs and
/// troubleshoot issues. Any part of the debugger can register itself with the
/// help of a callback to emit one or more files into the diagnostic directory.
class Diagnostics {
public:
  Diagnostics();
  ~Diagnostics();

  /// Gather diagnostics in the given directory.
  llvm::Error Create(const FileSpec &dir);

  /// Gather diagnostics and print a message to the given output stream.
  /// @{
  bool Dump(llvm::raw_ostream &stream);
  bool Dump(llvm::raw_ostream &stream, const FileSpec &dir);
  /// @}

  void Report(llvm::StringRef message);

  using Callback = std::function<llvm::Error(const FileSpec &)>;
  using CallbackID = uint64_t;

  CallbackID AddCallback(Callback callback);
  void RemoveCallback(CallbackID id);

  static Diagnostics &Instance();

  static bool Enabled();
  static void Initialize();
  static void Terminate();

  /// Create a unique diagnostic directory.
  static llvm::Expected<FileSpec> CreateUniqueDirectory();

private:
  static std::optional<Diagnostics> &InstanceImpl();

  llvm::Error DumpDiangosticsLog(const FileSpec &dir) const;

  RotatingLogHandler m_log_handler;

  struct CallbackEntry {
    CallbackEntry(CallbackID id, Callback callback)
        : id(id), callback(std::move(callback)) {}
    CallbackID id;
    Callback callback;
  };

  /// Monotonically increasing callback identifier. Unique per Diagnostic
  /// instance.
  CallbackID m_callback_id;

  /// List of callback entries.
  llvm::SmallVector<CallbackEntry, 4> m_callbacks;

  /// Mutex to protect callback list and callback identifier.
  std::mutex m_callbacks_mutex;
};

} // namespace lldb_private

#endif
