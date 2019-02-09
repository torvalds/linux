// SPDX-License-Identifier: LGPL-2.1
#undef _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include "str_error.h"

/*
 * Wrapper to allow for building in non-GNU systems such as Alpine Linux's musl
 * libc, while checking strerror_r() return to avoid having to check this in
 * all places calling it.
 */
char *str_error(int err, char *dst, int len)
{
	int ret = strerror_r(err, dst, len);
	if (ret)
		snprintf(dst, len, "ERROR: strerror_r(%d)=%d", err, ret);
	return dst;
}
