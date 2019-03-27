/*-
 * Copyright (c) 2012 Martin Matuska <mm@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _OPENSOLARIS_SYS_ASSFAIL_H_
#define	_OPENSOLARIS_SYS_ASSFAIL_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdio.h>
#include <stdlib.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
int assfail(const char *, const char *, int);
void assfail3(const char *, uintmax_t, const char *, uintmax_t, const char *,
    int);
#else	/* !defined(_KERNEL) */

#ifndef HAVE_ASSFAIL
extern int aok;

__inline int __assfail(const char *expr, const char *file, int line);

__inline int
__assfail(const char *expr, const char *file, int line)
{

	(void)fprintf(stderr, "Assertion failed: (%s), file %s, line %d.\n",
	    expr, file, line);
	if (!aok)
		abort();
	return (0);
}
#define assfail __assfail
#endif

#ifndef HAVE_ASSFAIL3
extern int aok;

static __inline void
__assfail3(const char *expr, uintmax_t lv, const char *op, uintmax_t rv,
    const char *file, int line) {

	(void)fprintf(stderr,
	    "Assertion failed: %s (0x%jx %s 0x%jx), file %s, line %d.\n",
	    expr, lv, op, rv, file, line);
	if (!aok)
		abort();
}
#define assfail3 __assfail3
#endif

#endif	/* !defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _OPENSOLARIS_SYS_ASSFAIL_H_ */
