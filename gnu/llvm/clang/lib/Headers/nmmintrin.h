/*===---- nmmintrin.h - SSE4 intrinsics ------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __NMMINTRIN_H
#define __NMMINTRIN_H

#if !defined(__i386__) && !defined(__x86_64__)
#error "This header is only meant to be used on x86 and x64 architecture"
#endif

/* To match expectations of gcc we put the sse4.2 definitions into smmintrin.h,
   just include it now then.  */
#include <smmintrin.h>
#endif /* __NMMINTRIN_H */
