/*===---- s390intrin.h - SystemZ intrinsics --------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __S390INTRIN_H
#define __S390INTRIN_H

#ifndef __s390__
#error "<s390intrin.h> is for s390 only"
#endif

#ifdef __HTM__
#include <htmintrin.h>
#endif

#ifdef __VEC__
#include <vecintrin.h>
#endif

#endif /* __S390INTRIN_H*/
