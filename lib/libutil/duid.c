/* $OpenBSD: duid.c,v 1.2 2012/07/09 14:26:40 nicm Exp $ */

/*
 * Copyright (c) 2010 Joel Sing <jsing@openbsd.org>
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

#include <string.h>

#include "util.h"

int
isduid(const char *duid, int dflags)
{
	char c;
	int i;

	/* Basic format check. */
	if (!((strlen(duid) == 16 && (dflags & OPENDEV_PART)) ||
	    (strlen(duid) == 18 && duid[16] == '.')))
		return 0;

	/* Check UID. */
	for (i = 0; i < 16; i++) {
		c = duid[i];
		if ((c < '0' || c > '9') && (c < 'a' || c > 'f'))
			return 0;
	}

	return 1;
}
