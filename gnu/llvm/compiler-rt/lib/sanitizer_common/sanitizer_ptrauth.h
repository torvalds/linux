//===-- sanitizer_ptrauth.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_PTRAUTH_H
#define SANITIZER_PTRAUTH_H

#if __has_feature(ptrauth_intrinsics)
#  include <ptrauth.h>
#elif defined(__ARM_FEATURE_PAC_DEFAULT) && !defined(__APPLE__)
// On the stack the link register is protected with Pointer
// Authentication Code when compiled with -mbranch-protection.
// Let's stripping the PAC unconditionally because xpaclri is in
// the NOP space so will do nothing when it is not enabled or not available.
#  define ptrauth_strip(__value, __key) \
    ({                                  \
      __typeof(__value) ret;            \
      asm volatile(                     \
          "mov x30, %1\n\t"             \
          "hint #7\n\t"                 \
          "mov %0, x30\n\t"             \
          "mov x30, xzr\n\t"            \
          : "=r"(ret)                   \
          : "r"(__value)                \
          : "x30");                     \
      ret;                              \
    })
#  define ptrauth_auth_data(__value, __old_key, __old_data) __value
#  define ptrauth_string_discriminator(__string) ((int)0)
#else
// Copied from <ptrauth.h>
#  define ptrauth_strip(__value, __key) __value
#  define ptrauth_auth_data(__value, __old_key, __old_data) __value
#  define ptrauth_string_discriminator(__string) ((int)0)
#endif

#define STRIP_PAC_PC(pc) ((uptr)ptrauth_strip(pc, 0))

#endif // SANITIZER_PTRAUTH_H
