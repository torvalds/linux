/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Kyle Martin <mkm@ieee.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <ulimit.h>

long
ulimit(int cmd, ...)
{
	struct rlimit limit;
	va_list ap;
	rlim_t arg;

	if (cmd == UL_GETFSIZE) {
		if (getrlimit(RLIMIT_FSIZE, &limit) == -1)
			return (-1);
		limit.rlim_cur /= 512;
		if (limit.rlim_cur > LONG_MAX)
			return (LONG_MAX);
		return ((long)limit.rlim_cur);
	} else if (cmd == UL_SETFSIZE) {
		va_start(ap, cmd);
		arg = va_arg(ap, long);
		va_end(ap);
		if (arg < 0)
			arg = LONG_MAX;
		if (arg > RLIM_INFINITY / 512)
			arg = RLIM_INFINITY / 512;
		limit.rlim_max = limit.rlim_cur = arg * 512;

		/* The setrlimit() function sets errno to EPERM if needed. */
		if (setrlimit(RLIMIT_FSIZE, &limit) == -1)
			return (-1);
		return ((long)arg);
	} else {
		errno = EINVAL;
		return (-1);
	}
}
