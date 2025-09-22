//===-- StringLexer.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/StringLexer.h"

#include <algorithm>
#include <cassert>
#include <utility>

using namespace lldb_private;

StringLexer::StringLexer(std::string s) : m_data(std::move(s)), m_position(0) {}

StringLexer::Character StringLexer::Peek() { return m_data[m_position]; }

bool StringLexer::NextIf(Character c) {
  auto val = Peek();
  if (val == c) {
    Next();
    return true;
  }
  return false;
}

std::pair<bool, StringLexer::Character>
StringLexer::NextIf(std::initializer_list<Character> cs) {
  auto val = Peek();
  for (auto c : cs) {
    if (val == c) {
      Next();
      return {true, c};
    }
  }
  return {false, 0};
}

bool StringLexer::AdvanceIf(const std::string &token) {
  auto pos = m_position;
  bool matches = true;
  for (auto c : token) {
    if (!NextIf(c)) {
      matches = false;
      break;
    }
  }
  if (!matches) {
    m_position = pos;
    return false;
  }
  return true;
}

StringLexer::Character StringLexer::Next() {
  auto val = Peek();
  Consume();
  return val;
}

bool StringLexer::HasAtLeast(Size s) {
  return (m_data.size() - m_position) >= s;
}

void StringLexer::PutBack(Size s) {
  assert(m_position >= s);
  m_position -= s;
}

std::string StringLexer::GetUnlexed() {
  return std::string(m_data, m_position);
}

void StringLexer::Consume() { m_position++; }

StringLexer &StringLexer::operator=(const StringLexer &rhs) {
  if (this != &rhs) {
    m_data = rhs.m_data;
    m_position = rhs.m_position;
  }
  return *this;
}
