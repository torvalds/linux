//===-- NameMatches.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "lldb/Utility/NameMatches.h"
#include "lldb/Utility/RegularExpression.h"

#include "llvm/ADT/StringRef.h"

using namespace lldb_private;

bool lldb_private::NameMatches(llvm::StringRef name, NameMatch match_type,
                               llvm::StringRef match) {
  switch (match_type) {
  case NameMatch::Ignore:
    return true;
  case NameMatch::Equals:
    return name == match;
  case NameMatch::Contains:
    return name.contains(match);
  case NameMatch::StartsWith:
    return name.starts_with(match);
  case NameMatch::EndsWith:
    return name.ends_with(match);
  case NameMatch::RegularExpression: {
    RegularExpression regex(match);
    return regex.Execute(name);
  }
  }
  return false;
}
