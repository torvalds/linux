/* $FreeBSD$ */
/*	$NetBSD: citrus_memstream.c,v 1.5 2012/03/13 21:13:31 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_region.h"
#include "citrus_memstream.h"
#include "citrus_bcs.h"

const char *
_citrus_memory_stream_getln(struct _citrus_memory_stream * __restrict ms,
    size_t * __restrict rlen)
{
	const uint8_t *h, *p;
	size_t i, ret;

	if (ms->ms_pos>=_region_size(&ms->ms_region))
		return (NULL);

	h = p = (uint8_t *)_region_offset(&ms->ms_region, ms->ms_pos);
	ret = 0;
	for (i = _region_size(&ms->ms_region) - ms->ms_pos; i > 0; i--) {
		ret++;
		if (_bcs_iseol(*p))
			break;
		p++;
	}

	ms->ms_pos += ret;
	*rlen = ret;
	return ((const char *)h);
}

#define T_COMM	'#'

const char *
_citrus_memory_stream_matchline(struct _citrus_memory_stream * __restrict ms,
    const char * __restrict key, size_t * __restrict rlen, int iscasesensitive)
{
	const char *p, *q;
	size_t keylen, len;

	keylen = strlen(key);
	for(;;) {
		p = _citrus_memory_stream_getln(ms, &len);
		if (p == NULL)
			return (NULL);

		/* ignore comment */
		q = memchr(p, T_COMM, len);
		if (q) {
			len = q - p;
		}
		/* ignore trailing white space and newline */
		_bcs_trunc_rws_len(p, &len);
		if (len == 0)
			continue; /* ignore null line */

		/* skip white spaces at the head of the line */
		p = _bcs_skip_ws_len(p, &len);
		q = _bcs_skip_nonws_len(p, &len);

		if ((size_t)(q - p) == keylen) {
			if (iscasesensitive) {
				if (memcmp(key, p, keylen) == 0)
					break; /* match */
			} else {
				if (_bcs_strncasecmp(key, p, keylen) == 0)
					break; /* match */
			}
		}
	}

	p = _bcs_skip_ws_len(q, &len);
	*rlen = len;

	return (p);
}

void *
_citrus_memory_stream_chr(struct _citrus_memory_stream *ms,
    struct _citrus_region *r, char ch)
{
	void *chr, *head;
	size_t sz;

	if (ms->ms_pos >= _region_size(&ms->ms_region))
		return (NULL);

	head = _region_offset(&ms->ms_region, ms->ms_pos);
	chr = memchr(head, ch, _memstream_remainder(ms));
	if (chr == NULL) {
		_region_init(r, head, _memstream_remainder(ms));
		ms->ms_pos = _region_size(&ms->ms_region);
		return (NULL);
	}
	sz = (char *)chr - (char *)head;

	_region_init(r, head, sz);
	ms->ms_pos += sz + 1;

	return (chr);
}

void
_citrus_memory_stream_skip_ws(struct _citrus_memory_stream *ms)
{
	int ch;

	while ((ch = _memstream_peek(ms)) != EOF) {
		if (!_bcs_isspace(ch))
			break;
		_memstream_getc(ms);
	}
}
