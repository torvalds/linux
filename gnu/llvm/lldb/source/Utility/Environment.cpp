//===-- Environment.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Environment.h"

using namespace lldb_private;

char *Environment::Envp::make_entry(llvm::StringRef Key,
                                    llvm::StringRef Value) {
  const size_t size = Key.size() + 1 /*=*/ + Value.size() + 1 /*\0*/;
  char *Result = static_cast<char *>(
      Allocator.Allocate(sizeof(char) * size, alignof(char)));
  char *Next = Result;

  Next = std::copy(Key.begin(), Key.end(), Next);
  *Next++ = '=';
  Next = std::copy(Value.begin(), Value.end(), Next);
  *Next++ = '\0';

  return Result;
}

Environment::Envp::Envp(const Environment &Env) {
  Data = static_cast<char **>(
      Allocator.Allocate(sizeof(char *) * (Env.size() + 1), alignof(char *)));
  char **Next = Data;
  for (const auto &KV : Env)
    *Next++ = make_entry(KV.first(), KV.second);
  *Next++ = nullptr;
}

Environment::Environment(const char *const *Env) {
  if (!Env)
    return;
  while (*Env)
    insert(*Env++);
}

void Environment::insert(iterator first, iterator last) {
  while (first != last) {
    try_emplace(first->first(), first->second);
    ++first;
  }
}
