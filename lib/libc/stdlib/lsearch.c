/*	$OpenBSD: lsearch.c,v 1.7 2021/12/08 22:06:28 cheloha Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Roger L. Snyder.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <sys/types.h>
#include <string.h>
#include <search.h>

typedef int (*cmp_fn_t)(const void *, const void *);

void *
lsearch(const void *key, void *base, size_t *nelp, size_t width,
    	cmp_fn_t compar)
{
	void *element = lfind(key, base, nelp, width, compar);

	/*
	 * Use memmove(3) to ensure the key is copied cleanly into the
	 * array, even if the key overlaps with the end of the array.
	 */
	if (element == NULL) {
		element = memmove((char *)base + *nelp * width, key, width);
		*nelp += 1;
	}
	return element;
}

void *
lfind(const void *key, const void *base, size_t *nelp, size_t width,
	cmp_fn_t compar)
{
	const char *element, *end;

	end = (const char *)base + *nelp * width;
	for (element = base; element < end; element += width)
		if (!compar(key, element))		/* key found */
			return((void *)element);
	return NULL;
}
DEF_WEAK(lfind);
