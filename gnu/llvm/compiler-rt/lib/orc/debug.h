//===- debug.h - Debugging output utilities ---------------------*- C++ -*-===//
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

#ifndef ORC_RT_DEBUG_H
#define ORC_RT_DEBUG_H

#include <atomic>

#ifndef NDEBUG

namespace __orc_rt {

extern std::atomic<const char *> DebugTypes;
extern char DebugTypesAll;
extern char DebugTypesNone;

const char *initializeDebug();
bool debugTypeEnabled(const char *Type, const char *Types);
void printdbg(const char *format, ...);

} // namespace __orc_rt

#define ORC_RT_DEBUG_WITH_TYPE(TYPE, X)                                        \
  do {                                                                         \
    const char *Types =                                                        \
        ::__orc_rt::DebugTypes.load(std::memory_order_relaxed);                \
    if (!Types)                                                                \
      Types = initializeDebug();                                               \
    if (Types == &DebugTypesNone)                                              \
      break;                                                                   \
    if (Types == &DebugTypesAll ||                                             \
        ::__orc_rt::debugTypeEnabled(TYPE, Types)) {                           \
      X;                                                                       \
    }                                                                          \
  } while (false)

#else

#define ORC_RT_DEBUG_WITH_TYPE(TYPE, X)                                        \
  do {                                                                         \
  } while (false)

#endif // !NDEBUG

#define ORC_RT_DEBUG(X) ORC_RT_DEBUG_WITH_TYPE(DEBUG_TYPE, X)

#endif // ORC_RT_DEBUG_H
