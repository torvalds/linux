/*-
 * Copyright (c) 2017 Juniper Networks.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "libc_private.h"

/* ISO/IEC 9899:2011 K.3.7.4.1 */
errno_t
memset_s(void *s, rsize_t smax, int c, rsize_t n)
{
	errno_t ret;
	rsize_t lim;
	unsigned char v;
	volatile unsigned char *dst;

	ret = EINVAL;
	lim = n < smax ? n : smax;
	v = (unsigned char)c;
	dst = (unsigned char *)s;
	if (s == NULL) {
		__throw_constraint_handler_s("memset_s : s is NULL", ret);
	} else if (smax > RSIZE_MAX) {
		__throw_constraint_handler_s("memset_s : smax > RSIZE_MAX",
		    ret);
	} else if (n > RSIZE_MAX) {
		__throw_constraint_handler_s("memset_s : n > RSIZE_MAX", ret);
	} else {
		while (lim > 0)
			dst[--lim] = v;
		if (n > smax) {
			__throw_constraint_handler_s("memset_s : n > smax",
			    ret);
		} else {
			ret = 0;
		}
	}
	return (ret);
}
