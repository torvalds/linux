/*===---- x86intrin.h - Implementation of X86 intrinsics on PowerPC --------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef NO_WARN_X86_INTRINSICS
/* This header is distributed to simplify porting x86_64 code that
   makes explicit use of Intel intrinsics to powerpc64le.
   It is the user's responsibility to determine if the results are
   acceptable and make additional changes as necessary.
   Note that much code that uses Intel intrinsics can be rewritten in
   standard C or GNU C extensions, which are more portable and better
   optimized across multiple targets.  */
#error "Please read comment above.  Use -DNO_WARN_X86_INTRINSICS to disable this error."
#endif

#ifndef X86INTRIN_H_
#define X86INTRIN_H_

#ifdef __ALTIVEC__
#include <immintrin.h>
#endif /* __ALTIVEC__ */

#endif /* X86INTRIN_H_ */
