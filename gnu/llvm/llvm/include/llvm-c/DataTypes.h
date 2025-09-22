/*===-- include/llvm-c/DataTypes.h - Define fixed size types ------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file contains definitions to figure out the size of _HOST_ data types.*|
|* This file is important because different host OS's define different macros,*|
|* which makes portability tough.  This file exports the following            *|
|* definitions:                                                               *|
|*                                                                            *|
|*   [u]int(32|64)_t : typedefs for signed and unsigned 32/64 bit system types*|
|*   [U]INT(8|16|32|64)_(MIN|MAX) : Constants for the min and max values.     *|
|*                                                                            *|
|* No library is required when using these functions.                         *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*/

/* Please leave this file C-compatible. */

#ifndef LLVM_C_DATATYPES_H
#define LLVM_C_DATATYPES_H

#include <inttypes.h>
#include <stdint.h>

#ifndef _MSC_VER

#if !defined(UINT32_MAX)
# error "The standard header <cstdint> is not C++11 compliant. Must #define "\
        "__STDC_LIMIT_MACROS before #including llvm-c/DataTypes.h"
#endif

#if !defined(UINT32_C)
# error "The standard header <cstdint> is not C++11 compliant. Must #define "\
        "__STDC_CONSTANT_MACROS before #including llvm-c/DataTypes.h"
#endif

/* Note that <inttypes.h> includes <stdint.h>, if this is a C99 system. */
#include <sys/types.h>

#ifdef _AIX
// GCC is strict about defining large constants: they must have LL modifier.
#undef INT64_MAX
#undef INT64_MIN
#endif

#else /* _MSC_VER */
#ifdef __cplusplus
#include <cstddef>
#include <cstdlib>
#else
#include <stddef.h>
#include <stdlib.h>
#endif
#include <sys/types.h>

#if defined(_WIN64)
typedef signed __int64 ssize_t;
#else
typedef signed int ssize_t;
#endif /* _WIN64 */

#endif /* _MSC_VER */

/* Set defaults for constants which we cannot find. */
#if !defined(INT64_MAX)
# define INT64_MAX 9223372036854775807LL
#endif
#if !defined(INT64_MIN)
# define INT64_MIN ((-INT64_MAX)-1)
#endif
#if !defined(UINT64_MAX)
# define UINT64_MAX 0xffffffffffffffffULL
#endif

#endif /* LLVM_C_DATATYPES_H */
