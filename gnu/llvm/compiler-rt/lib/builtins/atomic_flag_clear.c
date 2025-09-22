//===-- atomic_flag_clear.c -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements atomic_flag_clear from C11's stdatomic.h.
//
//===----------------------------------------------------------------------===//

#ifndef __has_include
#define __has_include(inc) 0
#endif

#if __has_include(<stdatomic.h>)

#include <stdatomic.h>
#undef atomic_flag_clear
void atomic_flag_clear(volatile atomic_flag *object) {
  __c11_atomic_store(&(object)->_Value, 0, __ATOMIC_SEQ_CST);
}

#endif
