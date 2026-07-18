/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * Assert for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_ASSERT_H
#define _NOLIBC_ASSERT_H

#include "errno.h"
#include "stdio.h"
#include "stdlib.h"

#endif /* _NOLIBC_ASSERT_H */

/* NDEBUG needs to be evaluated on *each* inclusion */
#ifdef assert
#undef assert
#endif

#ifndef NDEBUG
#define assert(expr)									\
({											\
	if (!(expr)) {									\
		fprintf(stderr, "%s: %s:%d: %s: Assertion `%s' failed.\n",		\
			program_invocation_short_name, __FILE__, __LINE__, __func__,	\
			#expr);								\
		abort();								\
	}										\
})
#else
#define assert(expr) ((void)0)
#endif
