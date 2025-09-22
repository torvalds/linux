//===-- StringList.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StringList.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/StreamString.h"
#include "llvm/ADT/ArrayRef.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

using namespace lldb_private;

StringList::StringList() : m_strings() {}

StringList::StringList(const char *str) : m_strings() {
  if (str)
    m_strings.push_back(str);
}

StringList::StringList(const char **strv, int strc) : m_strings() {
  for (int i = 0; i < strc; ++i) {
    if (strv[i])
      m_strings.push_back(strv[i]);
  }
}

StringList::~StringList() = default;

void StringList::AppendString(const char *str) {
  if (str)
    m_strings.push_back(str);
}

void StringList::AppendString(const std::string &s) { m_strings.push_back(s); }

void StringList::AppendString(std::string &&s) {
  m_strings.push_back(std::move(s));
}

void StringList::AppendString(const char *str, size_t str_len) {
  if (str)
    m_strings.push_back(std::string(str, str_len));
}

void StringList::AppendString(llvm::StringRef str) {
  m_strings.push_back(str.str());
}

void StringList::AppendString(const llvm::Twine &str) {
  m_strings.push_back(str.str());
}

void StringList::AppendList(const char **strv, int strc) {
  for (int i = 0; i < strc; ++i) {
    if (strv[i])
      m_strings.push_back(strv[i]);
  }
}

void StringList::AppendList(StringList strings) {
  m_strings.reserve(m_strings.size() + strings.GetSize());
  m_strings.insert(m_strings.end(), strings.begin(), strings.end());
}

size_t StringList::GetSize() const { return m_strings.size(); }

size_t StringList::GetMaxStringLength() const {
  size_t max_length = 0;
  for (const auto &s : m_strings) {
    const size_t len = s.size();
    if (max_length < len)
      max_length = len;
  }
  return max_length;
}

const char *StringList::GetStringAtIndex(size_t idx) const {
  if (idx < m_strings.size())
    return m_strings[idx].c_str();
  return nullptr;
}

void StringList::Join(const char *separator, Stream &strm) {
  size_t size = GetSize();

  if (size == 0)
    return;

  for (uint32_t i = 0; i < size; ++i) {
    if (i > 0)
      strm.PutCString(separator);
    strm.PutCString(GetStringAtIndex(i));
  }
}

void StringList::Clear() { m_strings.clear(); }

std::string StringList::LongestCommonPrefix() {
  if (m_strings.empty())
    return {};

  auto args = llvm::ArrayRef(m_strings);
  llvm::StringRef prefix = args.front();
  for (auto arg : args.drop_front()) {
    size_t count = 0;
    for (count = 0; count < std::min(prefix.size(), arg.size()); ++count) {
      if (prefix[count] != arg[count])
        break;
    }
    prefix = prefix.take_front(count);
  }
  return prefix.str();
}

void StringList::InsertStringAtIndex(size_t idx, const char *str) {
  if (str) {
    if (idx < m_strings.size())
      m_strings.insert(m_strings.begin() + idx, str);
    else
      m_strings.push_back(str);
  }
}

void StringList::InsertStringAtIndex(size_t idx, const std::string &str) {
  if (idx < m_strings.size())
    m_strings.insert(m_strings.begin() + idx, str);
  else
    m_strings.push_back(str);
}

void StringList::InsertStringAtIndex(size_t idx, std::string &&str) {
  if (idx < m_strings.size())
    m_strings.insert(m_strings.begin() + idx, std::move(str));
  else
    m_strings.push_back(std::move(str));
}

void StringList::DeleteStringAtIndex(size_t idx) {
  if (idx < m_strings.size())
    m_strings.erase(m_strings.begin() + idx);
}

size_t StringList::SplitIntoLines(const std::string &lines) {
  return SplitIntoLines(lines.c_str(), lines.size());
}

size_t StringList::SplitIntoLines(const char *lines, size_t len) {
  const size_t orig_size = m_strings.size();

  if (len == 0)
    return 0;

  const char *k_newline_chars = "\r\n";
  const char *p = lines;
  const char *end = lines + len;
  while (p < end) {
    size_t count = strcspn(p, k_newline_chars);
    if (count == 0) {
      if (p[count] == '\r' || p[count] == '\n')
        m_strings.push_back(std::string());
      else
        break;
    } else {
      if (p + count > end)
        count = end - p;
      m_strings.push_back(std::string(p, count));
    }
    if (p[count] == '\r' && p[count + 1] == '\n')
      count++; // Skip an extra newline char for the DOS newline
    count++;   // Skip the newline character
    p += count;
  }
  return m_strings.size() - orig_size;
}

void StringList::RemoveBlankLines() {
  if (GetSize() == 0)
    return;

  size_t idx = 0;
  while (idx < m_strings.size()) {
    if (m_strings[idx].empty())
      DeleteStringAtIndex(idx);
    else
      idx++;
  }
}

std::string StringList::CopyList(const char *item_preamble,
                                 const char *items_sep) const {
  StreamString strm;
  for (size_t i = 0; i < GetSize(); i++) {
    if (i && items_sep && items_sep[0])
      strm << items_sep;
    if (item_preamble)
      strm << item_preamble;
    strm << GetStringAtIndex(i);
  }
  return std::string(strm.GetString());
}

StringList &StringList::operator<<(const char *str) {
  AppendString(str);
  return *this;
}

StringList &StringList::operator<<(const std::string &str) {
  AppendString(str);
  return *this;
}

StringList &StringList::operator<<(const StringList &strings) {
  AppendList(strings);
  return *this;
}

StringList &StringList::operator=(const std::vector<std::string> &rhs) {
  m_strings.assign(rhs.begin(), rhs.end());

  return *this;
}

void StringList::LogDump(Log *log, const char *name) {
  if (!log)
    return;

  StreamString strm;
  if (name)
    strm.Printf("Begin %s:\n", name);
  for (const auto &s : m_strings) {
    strm.Indent();
    strm.Printf("%s\n", s.c_str());
  }
  if (name)
    strm.Printf("End %s.\n", name);

  LLDB_LOGV(log, "{0}", strm.GetData());
}
