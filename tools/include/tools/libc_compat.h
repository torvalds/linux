// SPDX-License-Identifier: (LGPL-2.0+ OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#ifndef __TOOLS_LIBC_COMPAT_H
#define __TOOLS_LIBC_COMPAT_H

#include <stdlib.h>
#include <linux/overflow.h>

#ifdef COMPAT_NEED_REALLOCARRAY
static inline void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
	size_t bytes;

	if (unlikely(check_mul_overflow(nmemb, size, &bytes)))
		return NULL;
	return realloc(ptr, bytes);
}
#endif
#endif
