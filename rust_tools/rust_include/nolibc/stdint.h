/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Standard definitions and types for NOLIBC
 * Copyright (C) 2023 Vincent Dagonneau <v@vda.io>
 */

#ifndef _NOLIBC_STDINT_H
#define _NOLIBC_STDINT_H

typedef unsigned char       uint8_t;
typedef   signed char        int8_t;
typedef unsigned short     uint16_t;
typedef   signed short      int16_t;
typedef unsigned int       uint32_t;
typedef   signed int        int32_t;
typedef unsigned long long uint64_t;
typedef   signed long long  int64_t;
typedef __SIZE_TYPE__        size_t;
typedef   signed long       ssize_t;
typedef unsigned long     uintptr_t;
typedef   signed long      intptr_t;
typedef   signed long     ptrdiff_t;

typedef   int8_t       int_least8_t;
typedef  uint8_t      uint_least8_t;
typedef  int16_t      int_least16_t;
typedef uint16_t     uint_least16_t;
typedef  int32_t      int_least32_t;
typedef uint32_t     uint_least32_t;
typedef  int64_t      int_least64_t;
typedef uint64_t     uint_least64_t;

typedef   int8_t        int_fast8_t;
typedef  uint8_t       uint_fast8_t;
typedef  ssize_t       int_fast16_t;
typedef   size_t      uint_fast16_t;
typedef  ssize_t       int_fast32_t;
typedef   size_t      uint_fast32_t;
typedef  int64_t       int_fast64_t;
typedef uint64_t      uint_fast64_t;

typedef __INTMAX_TYPE__    intmax_t;
typedef __UINTMAX_TYPE__  uintmax_t;

/* limits of integral types */

#define        INT8_MIN  (-128)
#define       INT16_MIN  (-32767-1)
#define       INT32_MIN  (-2147483647-1)
#define       INT64_MIN  (-9223372036854775807LL-1)

#define        INT8_MAX  (127)
#define       INT16_MAX  (32767)
#define       INT32_MAX  (2147483647)
#define       INT64_MAX  (9223372036854775807LL)

#define       UINT8_MAX  (255)
#define      UINT16_MAX  (65535)
#define      UINT32_MAX  (4294967295U)
#define      UINT64_MAX  (18446744073709551615ULL)

#define  INT_LEAST8_MIN  INT8_MIN
#define INT_LEAST16_MIN  INT16_MIN
#define INT_LEAST32_MIN  INT32_MIN
#define INT_LEAST64_MIN  INT64_MIN

#define  INT_LEAST8_MAX  INT8_MAX
#define INT_LEAST16_MAX  INT16_MAX
#define INT_LEAST32_MAX  INT32_MAX
#define INT_LEAST64_MAX  INT64_MAX

#define  UINT_LEAST8_MAX UINT8_MAX
#define UINT_LEAST16_MAX UINT16_MAX
#define UINT_LEAST32_MAX UINT32_MAX
#define UINT_LEAST64_MAX UINT64_MAX

#define SIZE_MAX         ((size_t)(__LONG_MAX__) * 2 + 1)
#define INTPTR_MIN       (-__LONG_MAX__ - 1)
#define INTPTR_MAX       __LONG_MAX__
#define PTRDIFF_MIN      INTPTR_MIN
#define PTRDIFF_MAX      INTPTR_MAX
#define UINTPTR_MAX      SIZE_MAX

#define  INT_FAST8_MIN   INT8_MIN
#define INT_FAST16_MIN   INTPTR_MIN
#define INT_FAST32_MIN   INTPTR_MIN
#define INT_FAST64_MIN   INT64_MIN

#define  INT_FAST8_MAX   INT8_MAX
#define INT_FAST16_MAX   INTPTR_MAX
#define INT_FAST32_MAX   INTPTR_MAX
#define INT_FAST64_MAX   INT64_MAX

#define  UINT_FAST8_MAX  UINT8_MAX
#define UINT_FAST16_MAX  SIZE_MAX
#define UINT_FAST32_MAX  SIZE_MAX
#define UINT_FAST64_MAX  UINT64_MAX

#define INTMAX_MIN       INT64_MIN
#define INTMAX_MAX       INT64_MAX
#define UINTMAX_MAX      UINT64_MAX

#ifndef INT_MIN
#define INT_MIN          (-__INT_MAX__ - 1)
#endif
#ifndef INT_MAX
#define INT_MAX          __INT_MAX__
#endif

#ifndef LONG_MIN
#define LONG_MIN         (-__LONG_MAX__ - 1)
#endif
#ifndef LONG_MAX
#define LONG_MAX         __LONG_MAX__
#endif

#ifndef ULONG_MAX
#define ULONG_MAX         ((unsigned long)(__LONG_MAX__) * 2 + 1)
#endif

#ifndef LLONG_MIN
#define LLONG_MIN         (-__LONG_LONG_MAX__ - 1)
#endif
#ifndef LLONG_MAX
#define LLONG_MAX         __LONG_LONG_MAX__
#endif

#ifndef ULLONG_MAX
#define ULLONG_MAX         ((unsigned long long)(__LONG_LONG_MAX__) * 2 + 1)
#endif

#endif /* _NOLIBC_STDINT_H */
