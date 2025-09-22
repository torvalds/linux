/*===---- mm3dnow.h - 3DNow! intrinsics ------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

// 3dNow intrinsics are no longer supported.

#ifndef _MM3DNOW_H_INCLUDED
#define _MM3DNOW_H_INCLUDED

#ifndef _CLANG_DISABLE_CRT_DEPRECATION_WARNINGS
#warning "The <mm3dnow.h> header is deprecated, and 3dNow! intrinsics are unsupported. For other intrinsics, include <x86intrin.h>, instead."
#endif

#include <mmintrin.h>
#include <prfchwintrin.h>

#endif
