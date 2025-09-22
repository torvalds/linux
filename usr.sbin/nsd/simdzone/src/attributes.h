/*
 * attributes.h -- internal compiler attribute abstractions
 *
 * Copyright (c) 2023-2024, NLnet Labs. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "zone/attributes.h"

#ifndef ATTRIBUTES_H
#define ATTRIBUTES_H

#define nonnull(params) zone_nonnull(params)
#define nonnull_all zone_nonnull_all

#if _MSC_VER
# define really_inline __forceinline
# define never_inline __declspec(noinline)
# define warn_unused_result
# define no_sanitize_undefined

# define likely(params) (params)
# define unlikely(params) (params)

#else // _MSC_VER
#if defined __has_builtin
# define has_builtin(params) __has_builtin(params)
#else
# define has_builtin(params) (0)
#endif

# if (zone_has_attribute(always_inline) || zone_has_gnuc(3, 1)) && ! defined __NO_INLINE__
    // Compilation using GCC 4.2.1 without optimizations fails.
    //   sorry, unimplemented: inlining failed in call to ...
    // GCC 4.1.2 and GCC 4.30 compile forward declared functions annotated
    // with __attribute__((always_inline)) without problems. Test if
    // __NO_INLINE__ is defined and define macro accordingly.
#   define really_inline inline __attribute__((always_inline))
# else
#   define really_inline inline
# endif

# if zone_has_attribute(noinline) || zone_has_gnuc(2, 96)
#   define never_inline __attribute__((noinline))
# else
#   define never_inline
# endif

# if zone_has_attribute(warn_unused_result)
#   define warn_unused_result __attribute__((warn_unused_result))
# else
#   define warn_unused_result
# endif

# if zone_has_attribute(no_sanitize)
    // GCC 8.1 added the no_sanitize function attribute.
#   define no_sanitize_undefined __attribute__((no_sanitize("undefined")))
# elif zone_has_attribute(no_sanitize_undefined)
    // GCC 4.9.0 added the UndefinedBehaviorSanitizer (ubsan) and the
    // no_sanitize_undefined function attribute.
#   define no_sanitize_undefined
# else
#   define no_sanitize_undefined
# endif

# if has_builtin(__builtin_expect)
#   define likely(params) __builtin_expect(!!(params), 1)
#   define unlikely(params) __builtin_expect(!!(params), 0)
# else
#   define likely(params) (params)
#   define unlikely(params) (params)
# endif
#endif

#endif // ATTRIBUTES_H
