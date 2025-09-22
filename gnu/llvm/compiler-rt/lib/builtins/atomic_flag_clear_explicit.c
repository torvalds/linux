//===-- atomic_flag_clear_explicit.c --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements atomic_flag_clear_explicit from C11's stdatomic.h.
//
//===----------------------------------------------------------------------===//

#ifndef __has_include
#define __has_include(inc) 0
#endif

#if __has_include(<stdatomic.h>)

#include <stdatomic.h>
#undef atomic_flag_clear_explicit
void atomic_flag_clear_explicit(volatile atomic_flag *object,
                                memory_order order) {
  __c11_atomic_store(&(object)->_Value, 0, order);
}

#endif
