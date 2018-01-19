/* SPDX-License-Identifier: GPL-2.0 */
#ifndef INT_TYPEDEFS_H
#define INT_TYPEDEFS_H

#include <inttypes.h>

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;
typedef uint64_t u64;

typedef int8_t __s8;
typedef uint8_t __u8;
typedef int16_t __s16;
typedef uint16_t __u16;
typedef int32_t __s32;
typedef uint32_t __u32;
typedef int64_t __s64;
typedef uint64_t __u64;

#define S8_C(x) INT8_C(x)
#define U8_C(x) UINT8_C(x)
#define S16_C(x) INT16_C(x)
#define U16_C(x) UINT16_C(x)
#define S32_C(x) INT32_C(x)
#define U32_C(x) UINT32_C(x)
#define S64_C(x) INT64_C(x)
#define U64_C(x) UINT64_C(x)

#endif
