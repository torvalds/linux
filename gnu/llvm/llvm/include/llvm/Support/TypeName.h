//===- TypeName.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_TYPENAME_H
#define LLVM_SUPPORT_TYPENAME_H

#include "llvm/ADT/StringRef.h"

namespace llvm {

/// We provide a function which tries to compute the (demangled) name of a type
/// statically.
///
/// This routine may fail on some platforms or for particularly unusual types.
/// Do not use it for anything other than logging and debugging aids. It isn't
/// portable or dependendable in any real sense.
///
/// The returned StringRef will point into a static storage duration string.
/// However, it may not be null terminated and may be some strangely aligned
/// inner substring of a larger string.
template <typename DesiredTypeName>
inline StringRef getTypeName() {
#if defined(__clang__) || defined(__GNUC__)
  StringRef Name = __PRETTY_FUNCTION__;

  StringRef Key = "DesiredTypeName = ";
  Name = Name.substr(Name.find(Key));
  assert(!Name.empty() && "Unable to find the template parameter!");
  Name = Name.drop_front(Key.size());

  assert(Name.ends_with("]") && "Name doesn't end in the substitution key!");
  return Name.drop_back(1);
#elif defined(_MSC_VER)
  StringRef Name = __FUNCSIG__;

  StringRef Key = "getTypeName<";
  Name = Name.substr(Name.find(Key));
  assert(!Name.empty() && "Unable to find the function name!");
  Name = Name.drop_front(Key.size());

  for (StringRef Prefix : {"class ", "struct ", "union ", "enum "})
    if (Name.starts_with(Prefix)) {
      Name = Name.drop_front(Prefix.size());
      break;
    }

  auto AnglePos = Name.rfind('>');
  assert(AnglePos != StringRef::npos && "Unable to find the closing '>'!");
  return Name.substr(0, AnglePos);
#else
  // No known technique for statically extracting a type name on this compiler.
  // We return a string that is unlikely to look like any type in LLVM.
  return "UNKNOWN_TYPE";
#endif
}

} // namespace llvm

#endif
