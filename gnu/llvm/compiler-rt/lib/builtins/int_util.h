//===-- int_util.h - internal utility functions ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is not part of the interface of this library.
//
// This file defines non-inline utilities which are available for use in the
// library. The function definitions themselves are all contained in int_util.c
// which will always be compiled into any compiler-rt library.
//
//===----------------------------------------------------------------------===//

#ifndef INT_UTIL_H
#define INT_UTIL_H

/// \brief Trigger a program abort (or panic for kernel code).
#define compilerrt_abort() __compilerrt_abort_impl(__FILE__, __LINE__, __func__)

NORETURN void __compilerrt_abort_impl(const char *file, int line,
                                      const char *function);

#define COMPILE_TIME_ASSERT(expr) COMPILE_TIME_ASSERT1(expr, __COUNTER__)
#define COMPILE_TIME_ASSERT1(expr, cnt) COMPILE_TIME_ASSERT2(expr, cnt)
#define COMPILE_TIME_ASSERT2(expr, cnt)                                        \
  typedef char ct_assert_##cnt[(expr) ? 1 : -1] UNUSED

// Force unrolling the code specified to be repeated N times.
#define REPEAT_0_TIMES(code_to_repeat) /* do nothing */
#define REPEAT_1_TIMES(code_to_repeat) code_to_repeat
#define REPEAT_2_TIMES(code_to_repeat)                                         \
  REPEAT_1_TIMES(code_to_repeat)                                               \
  code_to_repeat
#define REPEAT_3_TIMES(code_to_repeat)                                         \
  REPEAT_2_TIMES(code_to_repeat)                                               \
  code_to_repeat
#define REPEAT_4_TIMES(code_to_repeat)                                         \
  REPEAT_3_TIMES(code_to_repeat)                                               \
  code_to_repeat

#define REPEAT_N_TIMES_(N, code_to_repeat) REPEAT_##N##_TIMES(code_to_repeat)
#define REPEAT_N_TIMES(N, code_to_repeat) REPEAT_N_TIMES_(N, code_to_repeat)

#endif // INT_UTIL_H
