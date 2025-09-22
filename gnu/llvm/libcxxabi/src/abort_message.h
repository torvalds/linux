//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __ABORT_MESSAGE_H_
#define __ABORT_MESSAGE_H_

#include "cxxabi.h"

extern "C" _LIBCXXABI_HIDDEN _LIBCXXABI_NORETURN void
abort_message(const char *format, ...) __attribute__((format(printf, 1, 2)));

#ifndef _LIBCXXABI_ASSERT
#  define _LIBCXXABI_ASSERT(expr, msg)                                                                                 \
    do {                                                                                                               \
      if (!(expr)) {                                                                                                   \
        char const* __msg = (msg);                                                                                     \
        ::abort_message("%s:%d: %s", __FILE__, __LINE__, __msg);                                                       \
      }                                                                                                                \
    } while (false)

#endif

#endif // __ABORT_MESSAGE_H_
