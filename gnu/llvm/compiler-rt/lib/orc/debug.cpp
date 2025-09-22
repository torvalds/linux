//===- debug.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

#include "debug.h"

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>


namespace __orc_rt {

#ifndef NDEBUG

std::atomic<const char *> DebugTypes;
char DebugTypesAll;
char DebugTypesNone;

/// Sets the DebugState and DebugTypes values -- this function may be called
/// concurrently on multiple threads, but will always assign the same values so
/// this should be safe.
const char *initializeDebug() {
  if (const char *DT = getenv("ORC_RT_DEBUG")) {
    // If ORC_RT_DEBUG=1 then log everything.
    if (strcmp(DT, "1") == 0) {
      DebugTypes.store(&DebugTypesAll, std::memory_order_relaxed);
      return &DebugTypesAll;
    }

    // If ORC_RT_DEBUG is non-empty then record the string for use in
    // debugTypeEnabled.
    if (strcmp(DT, "") != 0) {
      DebugTypes.store(DT, std::memory_order_relaxed);
      return DT;
    }
  }

  // If ORT_RT_DEBUG is undefined or defined as empty then log nothing.
  DebugTypes.store(&DebugTypesNone, std::memory_order_relaxed);
  return &DebugTypesNone;
}

bool debugTypeEnabled(const char *Type, const char *Types) {
  assert(Types && Types != &DebugTypesAll && Types != &DebugTypesNone &&
         "Invalid Types value");
  size_t TypeLen = strlen(Type);
  const char *Start = Types;
  const char *End = Start;

  do {
    if (*End == '\0' || *End == ',') {
      size_t ItemLen = End - Start;
      if (ItemLen == TypeLen && memcmp(Type, Start, TypeLen) == 0)
        return true;
      if (*End == '\0')
        return false;
      Start = End + 1;
    }
    ++End;
  } while (true);
}

void printdbg(const char *format, ...) {
  va_list Args;
  va_start(Args, format);
  vfprintf(stderr, format, Args);
  va_end(Args);
}

#endif // !NDEBUG

} // end namespace __orc_rt
