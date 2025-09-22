/*	$OpenBSD: debug.c,v 1.5 2018/09/26 08:33:22 miko Exp $	*/
/*
 * Copyright (c) 2011 Alexandre Ratchov <alex@caoua.org>
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
#include <unistd.h>

#include "debug.h"

#ifdef DEBUG
/*
 * debug level, -1 means uninitialized
 */
int _sndio_debug = -1;

void
_sndio_debug_init(void)
{
	char *dbg;

	if (_sndio_debug < 0) {
		dbg = issetugid() ? NULL : getenv("SNDIO_DEBUG");
		if (!dbg || sscanf(dbg, "%u", &_sndio_debug) != 1)
			_sndio_debug = 0;
	}
}
#endif

const char *
_sndio_parsetype(const char *str, char *type)
{
	while (*type) {
		if (*type != *str)
			return NULL;
		type++;
		str++;
	}
	if (*str >= 'a' && *str <= 'z')
		return NULL;
	return str;
}

const char *
_sndio_parsenum(const char *str, unsigned int *num, unsigned int max)
{
	const char *p = str;
	unsigned int dig, maxq, maxr, val;

	val = 0;
	maxq = max / 10;
	maxr = max % 10;
	for (;;) {
		dig = *p - '0';
		if (dig >= 10)
			break;
		if (val > maxq || (val == maxq && dig > maxr)) {
			DPRINTF("%s: number too large\n", str);
			return NULL;
		}
		val = val * 10 + dig;
		p++;
	}
	if (p == str) {
		DPRINTF("%s: number expected\n", str);
		return NULL;
	}
	*num = val;
	return p;
}
