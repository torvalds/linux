//===-- CommandHistory.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_INTERPRETER_COMMANDHISTORY_H
#define LLDB_INTERPRETER_COMMANDHISTORY_H

#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "lldb/Utility/Stream.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CommandHistory {
public:
  CommandHistory() = default;

  ~CommandHistory() = default;

  size_t GetSize() const;

  bool IsEmpty() const;

  std::optional<llvm::StringRef> FindString(llvm::StringRef input_str) const;

  llvm::StringRef GetStringAtIndex(size_t idx) const;

  llvm::StringRef operator[](size_t idx) const;

  llvm::StringRef GetRecentmostString() const;

  void AppendString(llvm::StringRef str, bool reject_if_dupe = true);

  void Clear();

  void Dump(Stream &stream, size_t start_idx = 0,
            size_t stop_idx = SIZE_MAX) const;

  static const char g_repeat_char = '!';

private:
  CommandHistory(const CommandHistory &) = delete;
  const CommandHistory &operator=(const CommandHistory &) = delete;

  typedef std::vector<std::string> History;
  mutable std::recursive_mutex m_mutex;
  History m_history;
};

} // namespace lldb_private

#endif // LLDB_INTERPRETER_COMMANDHISTORY_H
