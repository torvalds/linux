/*===---- builtins.h - z/Architecture Builtin Functions --------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __ZOS_WRAPPERS_BUILTINS_H
#define __ZOS_WRAPPERS_BUILTINS_H
#if defined(__MVS__)
#include_next <builtins.h>
#if defined(__VEC__)
#include <vecintrin.h>
#endif
#endif /* defined(__MVS__) */
#endif /* __ZOS_WRAPPERS_BUILTINS_H */
