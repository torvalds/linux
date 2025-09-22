/*	$OpenBSD: asprintf.c,v 1.27 2025/08/08 15:58:53 yasuoka Exp $	*/

/*
 * Copyright (c) 1997 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include "local.h"

#define	INITIAL_SIZE	128

int
asprintf(char **str, const char *fmt, ...)
{
	int ret;
	va_list ap;
	FILE f = FILEINIT(__SWR | __SSTR | __SALC);
	const int pgsz = getpagesize();

	f._bf._base = f._p = malloc(INITIAL_SIZE);
	if (f._bf._base == NULL)
		goto err;
	f._bf._size = f._w = INITIAL_SIZE - 1;	/* leave room for the NUL */
	va_start(ap, fmt);
	ret = __vfprintf(&f, fmt, ap);
	va_end(ap);
	if (ret == -1)
		goto err;
	*f._p = '\0';
	if (ret + 1 > INITIAL_SIZE && ret + 1 < pgsz / 2) {
		/* midsize allocations can try to conserve memory */
		unsigned char *_base = recallocarray(f._bf._base,
		    f._bf._size + 1, ret + 1, 1);

		if (_base == NULL)
			goto err;
		*str = (char *)_base;
	} else
		*str = (char *)f._bf._base;
	return (ret);

err:
	free(f._bf._base);
	f._bf._base = NULL;
	*str = NULL;
	errno = ENOMEM;
	return (-1);
}
DEF_WEAK(asprintf);
