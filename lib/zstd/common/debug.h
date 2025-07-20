/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/* ******************************************************************
 * debug
 * Part of FSE library
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */


/*
 * The purpose of this header is to enable debug functions.
 * They regroup assert(), DEBUGLOG() and RAWLOG() for run-time,
 * and DEBUG_STATIC_ASSERT() for compile-time.
 *
 * By default, DEBUGLEVEL==0, which means run-time debug is disabled.
 *
 * Level 1 enables assert() only.
 * Starting level 2, traces can be generated and pushed to stderr.
 * The higher the level, the more verbose the traces.
 *
 * It's possible to dynamically adjust level using variable g_debug_level,
 * which is only declared if DEBUGLEVEL>=2,
 * and is a global variable, not multi-thread protected (use with care)
 */

#ifndef DEBUG_H_12987983217
#define DEBUG_H_12987983217


/* static assert is triggered at compile time, leaving no runtime artefact.
 * static assert only works with compile-time constants.
 * Also, this variant can only be used inside a function. */
#define DEBUG_STATIC_ASSERT(c) (void)sizeof(char[(c) ? 1 : -1])


/* DEBUGLEVEL is expected to be defined externally,
 * typically through compiler command line.
 * Value must be a number. */
#ifndef DEBUGLEVEL
#  define DEBUGLEVEL 0
#endif


/* recommended values for DEBUGLEVEL :
 * 0 : release mode, no debug, all run-time checks disabled
 * 1 : enables assert() only, no display
 * 2 : reserved, for currently active debug path
 * 3 : events once per object lifetime (CCtx, CDict, etc.)
 * 4 : events once per frame
 * 5 : events once per block
 * 6 : events once per sequence (verbose)
 * 7+: events at every position (*very* verbose)
 *
 * It's generally inconvenient to output traces > 5.
 * In which case, it's possible to selectively trigger high verbosity levels
 * by modifying g_debug_level.
 */

#if (DEBUGLEVEL>=1)
#  define ZSTD_DEPS_NEED_ASSERT
#  include "zstd_deps.h"
#else
#  ifndef assert   /* assert may be already defined, due to prior #include <assert.h> */
#    define assert(condition) ((void)0)   /* disable assert (default) */
#  endif
#endif

#if (DEBUGLEVEL>=2)
#  define ZSTD_DEPS_NEED_IO
#  include "zstd_deps.h"
extern int g_debuglevel; /* the variable is only declared,
                            it actually lives in debug.c,
                            and is shared by the whole process.
                            It's not thread-safe.
                            It's useful when enabling very verbose levels
                            on selective conditions (such as position in src) */

#  define RAWLOG(l, ...)                   \
    do {                                   \
        if (l<=g_debuglevel) {             \
            ZSTD_DEBUG_PRINT(__VA_ARGS__); \
        }                                  \
    } while (0)

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define LINE_AS_STRING TOSTRING(__LINE__)

#  define DEBUGLOG(l, ...)                               \
    do {                                                 \
        if (l<=g_debuglevel) {                           \
            ZSTD_DEBUG_PRINT(__FILE__ ":" LINE_AS_STRING ": " __VA_ARGS__); \
            ZSTD_DEBUG_PRINT(" \n");                     \
        }                                                \
    } while (0)
#else
#  define RAWLOG(l, ...)   do { } while (0)    /* disabled */
#  define DEBUGLOG(l, ...) do { } while (0)    /* disabled */
#endif

#endif /* DEBUG_H_12987983217 */
