/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Ed Schouten <ed@FreeBSD.org>
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

#include <sys/libkern.h>
#include <sys/limits.h>
#include <sys/param.h>

/*
 * memcchr(): find first character in buffer not matching `c'.
 *
 * This function performs the complement of memchr().  To provide decent
 * performance, this function compares data from the buffer one word at
 * a time.
 *
 * This code is inspired by libc's strlen(), written by Xin Li.
 */

#if LONG_BIT != 32 && LONG_BIT != 64
#error Unsupported word size
#endif

#define	LONGPTR_MASK (sizeof(long) - 1)

#define	TESTBYTE				\
	do {					\
		if (*p != (unsigned char)c)	\
			goto done;		\
		p++;				\
	} while (0)

void *
memcchr(const void *begin, int c, size_t n)
{
	const unsigned long *lp;
	const unsigned char *p, *end;
	unsigned long word;

	/* Four or eight repetitions of `c'. */
	word = (unsigned char)c;
	word |= word << 8;
	word |= word << 16;
#if LONG_BIT >= 64
	word |= word << 32;
#endif

	/* Don't perform memory I/O when passing a zero-length buffer. */
	if (n == 0)
		return (NULL);

	/*
	 * First determine whether there is a character unequal to `c'
	 * in the first word.  As this word may contain bytes before
	 * `begin', we may execute this loop spuriously.
	 */
	lp = (const unsigned long *)((uintptr_t)begin & ~LONGPTR_MASK);
	end = (const unsigned char *)begin + n;
	if (*lp++ != word)
		for (p = begin; p < (const unsigned char *)lp;)
			TESTBYTE;

	/* Now compare the data one word at a time. */
	for (; (const unsigned char *)lp < end; lp++) {
		if (*lp != word) {
			p = (const unsigned char *)lp;
			TESTBYTE;
			TESTBYTE;
			TESTBYTE;
#if LONG_BIT >= 64
			TESTBYTE;
			TESTBYTE;
			TESTBYTE;
			TESTBYTE;
#endif
			goto done;
		}
	}

	return (NULL);

done:
	/*
	 * If the end of the buffer is not word aligned, the previous
	 * loops may obtain an address that's beyond the end of the
	 * buffer.
	 */
	if (p < end)
		return (__DECONST(void *, p));
	return (NULL);
}
