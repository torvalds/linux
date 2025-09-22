//===-- StringList.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_STRINGLIST_H
#define LLDB_UTILITY_STRINGLIST_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

#include <cstddef>
#include <string>
#include <vector>

namespace lldb_private {
class Log;
class Stream;
}

namespace lldb_private {

class StringList {
  typedef std::vector<std::string> collection;

public:
  StringList();

  explicit StringList(const char *str);

  StringList(const char **strv, int strc);

  virtual ~StringList();

  void AppendString(const std::string &s);

  void AppendString(std::string &&s);

  void AppendString(const char *str);

  void AppendString(const char *str, size_t str_len);

  void AppendString(llvm::StringRef str);

  void AppendString(const llvm::Twine &str);

  void AppendList(const char **strv, int strc);

  void AppendList(StringList strings);

  size_t GetSize() const;

  void SetSize(size_t n) { m_strings.resize(n); }

  size_t GetMaxStringLength() const;

  typedef collection::iterator iterator;
  typedef collection::const_iterator const_iterator;

  iterator begin() { return m_strings.begin(); }
  iterator end() { return m_strings.end(); }
  const_iterator begin() const { return m_strings.begin(); }
  const_iterator end() const { return m_strings.end(); }

  std::string &operator[](size_t idx) {
    // No bounds checking, verify "idx" is good prior to calling this function
    return m_strings[idx];
  }

  const std::string &operator[](size_t idx) const {
    // No bounds checking, verify "idx" is good prior to calling this function
    return m_strings[idx];
  }

  void PopBack() { m_strings.pop_back(); }
  const char *GetStringAtIndex(size_t idx) const;

  void Join(const char *separator, Stream &strm);

  void Clear();

  std::string LongestCommonPrefix();

  void InsertStringAtIndex(size_t idx, const std::string &str);

  void InsertStringAtIndex(size_t idx, std::string &&str);

  void InsertStringAtIndex(size_t id, const char *str);

  void DeleteStringAtIndex(size_t id);

  void RemoveBlankLines();

  size_t SplitIntoLines(const std::string &lines);

  size_t SplitIntoLines(const char *lines, size_t len);

  std::string CopyList(const char *item_preamble = nullptr,
                       const char *items_sep = "\n") const;

  StringList &operator<<(const char *str);

  StringList &operator<<(const std::string &s);

  StringList &operator<<(const StringList &strings);

  // Copy assignment for a vector of strings
  StringList &operator=(const std::vector<std::string> &rhs);

  // Dump the StringList to the given lldb_private::Log, `log`, one item per
  // line. If given, `name` will be used to identify the start and end of the
  // list in the output.
  virtual void LogDump(Log *log, const char *name = nullptr);

  // Static helper to convert an iterable of strings to a StringList, and then
  // dump it with the semantics of the `LogDump` method.
  template <typename T>
  static void LogDump(Log *log, T s_iterable, const char *name = nullptr) {
    if (!log)
      return;
    // Make a copy of the iterable as a StringList
    StringList l{};
    for (const auto &s : s_iterable)
      l << s;

    l.LogDump(log, name);
  }

private:
  collection m_strings;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_STRINGLIST_H
