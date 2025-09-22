//===-- StringLexer.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_STRINGLEXER_H
#define LLDB_UTILITY_STRINGLEXER_H

#include <initializer_list>
#include <string>
#include <utility>

namespace lldb_private {

class StringLexer {
public:
  typedef std::string::size_type Position;
  typedef std::string::size_type Size;

  typedef std::string::value_type Character;

  StringLexer(std::string s);

  // These APIs are not bounds-checked.  Use HasAtLeast() if you're not sure.
  Character Peek();

  bool NextIf(Character c);

  std::pair<bool, Character> NextIf(std::initializer_list<Character> cs);

  bool AdvanceIf(const std::string &token);

  Character Next();

  bool HasAtLeast(Size s);

  std::string GetUnlexed();

  // This will assert if there are less than s characters preceding the cursor.
  void PutBack(Size s);

  StringLexer &operator=(const StringLexer &rhs);

private:
  std::string m_data;
  Position m_position;

  void Consume();
};

} // namespace lldb_private

#endif // LLDB_UTILITY_STRINGLEXER_H
