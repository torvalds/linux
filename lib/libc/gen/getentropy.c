/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Conrad Meyer <cem@FreeBSD.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/random.h>
#include <sys/sysctl.h>

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>

#include "libc_private.h"

/* First __FreeBSD_version bump after introduction of getrandom(2) (r331279) */
#define GETRANDOM_FIRST 1200061

extern int __sysctl(int *, u_int, void *, size_t *, void *, size_t);

static inline void
_getentropy_fail(void)
{
	raise(SIGKILL);
}

static size_t
arnd_sysctl(u_char *buf, size_t size)
{
	int mib[2];
	size_t len, done;

	mib[0] = CTL_KERN;
	mib[1] = KERN_ARND;
	done = 0;

	do {
		len = size;
		if (__sysctl(mib, 2, buf, &len, NULL, 0) == -1)
			return (done);
		done += len;
		buf += len;
		size -= len;
	} while (size > 0);

	return (done);
}

/*
 * If a newer libc is accidentally installed on an older kernel, provide high
 * quality random data anyway.  The sysctl interface is not as fast and does
 * not block by itself, but is provided by even very old kernels.
 */
static int
getentropy_fallback(void *buf, size_t buflen)
{
	/*
	 * oldp (buf) == NULL has a special meaning for sysctl that results in
	 * no EFAULT.  For compatibility with the kernel getrandom(2), detect
	 * this case and return the appropriate error.
	 */
	if (buf == NULL && buflen > 0) {
		errno = EFAULT;
		return (-1);
	}
	if (arnd_sysctl(buf, buflen) != buflen) {
		if (errno == EFAULT)
			return (-1);
		/*
		 * This cannot happen.  arnd_sysctl() spins until the random
		 * device is seeded and then repeatedly reads until the full
		 * request is satisfied.  The only way for this to return a zero
		 * byte or short read is if sysctl(2) on the kern.arandom MIB
		 * fails.  In this case, excepting the user-provided-a-bogus-
		 * buffer EFAULT, give up (like for arc4random(3)'s arc4_stir).
		 */
		_getentropy_fail();
	}
	return (0);
}

int
getentropy(void *buf, size_t buflen)
{
	ssize_t rd;
	bool have_getrandom;

	if (buflen > 256) {
		errno = EIO;
		return (-1);
	}

	have_getrandom = (__getosreldate() >= GETRANDOM_FIRST);

	while (buflen > 0) {
		if (have_getrandom) {
			rd = getrandom(buf, buflen, 0);
			if (rd == -1) {
				switch (errno) {
				case ECAPMODE:
					/*
					 * Kernel >= r331280 and < r337999
					 * will return ECAPMODE when the
					 * caller is already in capability
					 * mode, fallback to traditional
					 * method in this case.
					 */
					have_getrandom = false;
					continue;
				case EINTR:
					continue;
				case EFAULT:
					return (-1);
				default:
					_getentropy_fail();
				}
			}
		} else {
			return (getentropy_fallback(buf, buflen));
		}

		/* This cannot happen. */
		if (rd == 0)
			_getentropy_fail();

		buf = (char *)buf + rd;
		buflen -= rd;
	}

	return (0);
}
