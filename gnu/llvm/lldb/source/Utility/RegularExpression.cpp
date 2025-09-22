//===-- RegularExpression.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/RegularExpression.h"

#include <string>

using namespace lldb_private;

RegularExpression::RegularExpression(llvm::StringRef str,
                                     llvm::Regex::RegexFlags flags)
    : m_regex_text(std::string(str)),
      // m_regex does not reference str anymore after it is constructed.
      m_regex(llvm::Regex(str, flags)) {}

RegularExpression::RegularExpression(const RegularExpression &rhs)
    : RegularExpression(rhs.GetText()) {}

bool RegularExpression::Execute(
    llvm::StringRef str,
    llvm::SmallVectorImpl<llvm::StringRef> *matches) const {
  if (!IsValid())
    return false;
  return m_regex.match(str, matches);
}

bool RegularExpression::IsValid() const { return m_regex.isValid(); }

llvm::StringRef RegularExpression::GetText() const { return m_regex_text; }

llvm::Error RegularExpression::GetError() const {
  std::string error;
  if (!m_regex.isValid(error))
    return llvm::make_error<llvm::StringError>(error,
                                               llvm::inconvertibleErrorCode());
  return llvm::Error::success();
}
