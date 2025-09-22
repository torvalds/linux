//===-- ubsan_handlers_cxx.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Entry points to the runtime library for Clang's undefined behavior sanitizer,
// for C++-specific checks. This code is not linked into C binaries.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_HANDLERS_CXX_H
#define UBSAN_HANDLERS_CXX_H

#include "ubsan_value.h"

namespace __ubsan {

struct DynamicTypeCacheMissData {
  SourceLocation Loc;
  const TypeDescriptor &Type;
  void *TypeInfo;
  unsigned char TypeCheckKind;
};

/// \brief Handle a runtime type check failure, caused by an incorrect vptr.
/// When this handler is called, all we know is that the type was not in the
/// cache; this does not necessarily imply the existence of a bug.
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __ubsan_handle_dynamic_type_cache_miss(
  DynamicTypeCacheMissData *Data, ValueHandle Pointer, ValueHandle Hash);
extern "C" SANITIZER_INTERFACE_ATTRIBUTE
void __ubsan_handle_dynamic_type_cache_miss_abort(
  DynamicTypeCacheMissData *Data, ValueHandle Pointer, ValueHandle Hash);
}

#endif // UBSAN_HANDLERS_CXX_H
