//===-- StringSaver.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/StringSaver.h"

#include "llvm/ADT/SmallString.h"

using namespace llvm;

StringRef StringSaver::save(StringRef S) {
  char *P = Alloc.Allocate<char>(S.size() + 1);
  if (!S.empty())
    memcpy(P, S.data(), S.size());
  P[S.size()] = '\0';
  return StringRef(P, S.size());
}

StringRef StringSaver::save(const Twine &S) {
  SmallString<128> Storage;
  return save(S.toStringRef(Storage));
}

StringRef UniqueStringSaver::save(StringRef S) {
  auto R = Unique.insert(S);
  if (R.second)                 // cache miss, need to actually save the string
    *R.first = Strings.save(S); // safe replacement with equal value
  return *R.first;
}

StringRef UniqueStringSaver::save(const Twine &S) {
  SmallString<128> Storage;
  return save(S.toStringRef(Storage));
}
