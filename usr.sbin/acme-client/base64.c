/*	$Id: base64.c,v 1.9 2017/01/24 13:32:55 jsing Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <netinet/in.h>
#include <resolv.h>

#include <stdlib.h>

#include "extern.h"

/*
 * Compute the maximum buffer required for a base64 encoded string of
 * length "len".
 */
size_t
base64len(size_t len)
{

	return (len + 2) / 3 * 4 + 1;
}

/*
 * Pass a stream of bytes to be base64 encoded, then converted into
 * base64url format.
 * Returns NULL on allocation failure (not logged).
 */
char *
base64buf_url(const char *data, size_t len)
{
	size_t	 i, sz;
	char	*buf;

	sz = base64len(len);
	if ((buf = malloc(sz)) == NULL)
		return NULL;

	b64_ntop(data, len, buf, sz);

	for (i = 0; i < sz; i++)
		switch (buf[i]) {
		case '+':
			buf[i] = '-';
			break;
		case '/':
			buf[i] = '_';
			break;
		case '=':
			buf[i] = '\0';
			break;
		}

	return buf;
}
