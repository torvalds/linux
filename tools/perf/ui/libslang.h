/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_UI_SLANG_H_
#define _PERF_UI_SLANG_H_ 1
/*
 * slang versions <= 2.0.6 have a "#if HAVE_LONG_LONG" that breaks
 * the build if it isn't defined. Use the equivalent one that glibc
 * has on features.h.
 */
#include <features.h>
#ifndef HAVE_LONG_LONG
#define HAVE_LONG_LONG __GLIBC_HAVE_LONG_LONG
#endif

/* Enable future slang's corrected function prototypes. */
#define ENABLE_SLFUTURE_CONST 1
#define ENABLE_SLFUTURE_VOID 1

#include <slang.h>

#define SL_KEY_UNTAB 0x1000

#endif /* _PERF_UI_SLANG_H_ */
