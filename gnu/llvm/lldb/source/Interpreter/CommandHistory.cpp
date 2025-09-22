//===-- CommandHistory.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cinttypes>
#include <optional>

#include "lldb/Interpreter/CommandHistory.h"

using namespace lldb;
using namespace lldb_private;

size_t CommandHistory::GetSize() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  return m_history.size();
}

bool CommandHistory::IsEmpty() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  return m_history.empty();
}

std::optional<llvm::StringRef>
CommandHistory::FindString(llvm::StringRef input_str) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (input_str.size() < 2)
    return std::nullopt;

  if (input_str[0] != g_repeat_char)
    return std::nullopt;

  if (input_str[1] == g_repeat_char) {
    if (m_history.empty())
      return std::nullopt;
    return llvm::StringRef(m_history.back());
  }

  input_str = input_str.drop_front();

  size_t idx = 0;
  if (input_str.front() == '-') {
    if (input_str.drop_front(1).getAsInteger(0, idx))
      return std::nullopt;
    if (idx >= m_history.size())
      return std::nullopt;
    idx = m_history.size() - idx;
  } else {
    if (input_str.getAsInteger(0, idx))
      return std::nullopt;
    if (idx >= m_history.size())
      return std::nullopt;
  }

  return llvm::StringRef(m_history[idx]);
}

llvm::StringRef CommandHistory::GetStringAtIndex(size_t idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (idx < m_history.size())
    return m_history[idx];
  return "";
}

llvm::StringRef CommandHistory::operator[](size_t idx) const {
  return GetStringAtIndex(idx);
}

llvm::StringRef CommandHistory::GetRecentmostString() const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_history.empty())
    return "";
  return m_history.back();
}

void CommandHistory::AppendString(llvm::StringRef str, bool reject_if_dupe) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (reject_if_dupe) {
    if (!m_history.empty()) {
      if (str == m_history.back())
        return;
    }
  }
  m_history.push_back(std::string(str));
}

void CommandHistory::Clear() {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  m_history.clear();
}

void CommandHistory::Dump(Stream &stream, size_t start_idx,
                          size_t stop_idx) const {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  stop_idx = std::min(stop_idx + 1, m_history.size());
  for (size_t counter = start_idx; counter < stop_idx; counter++) {
    const std::string hist_item = m_history[counter];
    if (!hist_item.empty()) {
      stream.Indent();
      stream.Printf("%4" PRIu64 ": %s\n", (uint64_t)counter, hist_item.c_str());
    }
  }
}
