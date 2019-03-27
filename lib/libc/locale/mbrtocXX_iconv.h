/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ed Schouten <ed@FreeBSD.org>
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

#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <langinfo.h>
#include <limits.h>
#include <string.h>
#include <uchar.h>

#include "../iconv/citrus_hash.h"
#include "../iconv/citrus_module.h"
#include "../iconv/citrus_iconv.h"
#include "mblocal.h"

typedef struct {
	bool			initialized;
	struct _citrus_iconv	iconv;
	char			srcbuf[MB_LEN_MAX];
	size_t			srcbuf_len;
	union {
		charXX_t	widechar[DSTBUF_LEN];
		char		bytes[sizeof(charXX_t) * DSTBUF_LEN];
	} dstbuf;
	size_t			dstbuf_len;
} _ConversionState;
_Static_assert(sizeof(_ConversionState) <= sizeof(mbstate_t),
    "Size of _ConversionState must not exceed mbstate_t's size.");

size_t
mbrtocXX_l(charXX_t * __restrict pc, const char * __restrict s, size_t n,
    mbstate_t * __restrict ps, locale_t locale)
{
	_ConversionState *cs;
	struct _citrus_iconv *handle;
	size_t i, retval;
	charXX_t retchar;

	FIX_LOCALE(locale);
	if (ps == NULL)
		ps = &(XLOCALE_CTYPE(locale)->mbrtocXX);
	cs = (_ConversionState *)ps;
	handle = &cs->iconv;

	/* Reinitialize mbstate_t. */
	if (s == NULL || !cs->initialized) {
		if (_citrus_iconv_open(&handle,
		    nl_langinfo_l(CODESET, locale), UTF_XX_INTERNAL) != 0) {
			cs->initialized = false;
			errno = EINVAL;
			return (-1);
		}
		handle->cv_shared->ci_discard_ilseq = true;
		handle->cv_shared->ci_hooks = NULL;
		cs->srcbuf_len = cs->dstbuf_len = 0;
		cs->initialized = true;
		if (s == NULL)
			return (0);
	}

	/* See if we still have characters left from the previous invocation. */
	if (cs->dstbuf_len > 0) {
		retval = (size_t)-3;
		goto return_char;
	}

	/* Fill up the read buffer as far as possible. */
	if (n > sizeof(cs->srcbuf) - cs->srcbuf_len)
		n = sizeof(cs->srcbuf) - cs->srcbuf_len;
	memcpy(cs->srcbuf + cs->srcbuf_len, s, n);

	/* Convert as few characters to the dst buffer as possible. */
	for (i = 0; ; i++) {
		char *src, *dst;
		size_t srcleft, dstleft, invlen;
		int err;

		src = cs->srcbuf;
		srcleft = cs->srcbuf_len + n;
		dst = cs->dstbuf.bytes;
		dstleft = i * sizeof(charXX_t);
		assert(srcleft <= sizeof(cs->srcbuf) &&
		    dstleft <= sizeof(cs->dstbuf.bytes));
		err = _citrus_iconv_convert(handle, &src, &srcleft,
		    &dst, &dstleft, 0, &invlen);
		cs->dstbuf_len = (dst - cs->dstbuf.bytes) / sizeof(charXX_t);

		/* Got new character(s). Return the first. */
		if (cs->dstbuf_len > 0) {
			assert(src - cs->srcbuf > cs->srcbuf_len);
			retval = src - cs->srcbuf - cs->srcbuf_len;
			cs->srcbuf_len = 0;
			goto return_char;
		}

		/* Increase dst buffer size, to obtain the surrogate pair. */
		if (err == E2BIG)
			continue;

		/* Illegal sequence. */
		if (invlen > 0) {
			cs->srcbuf_len = 0;
			errno = EILSEQ;
			return ((size_t)-1);
		}

		/* Save unprocessed remainder for the next invocation. */
		memmove(cs->srcbuf, src, srcleft);
		cs->srcbuf_len = srcleft;
		return ((size_t)-2);
	}

return_char:
	retchar = cs->dstbuf.widechar[0];
	memmove(&cs->dstbuf.widechar[0], &cs->dstbuf.widechar[1],
	    --cs->dstbuf_len * sizeof(charXX_t));
	if (pc != NULL)
		*pc = retchar;
	if (retchar == 0)
		return (0);
	return (retval);
}

size_t
mbrtocXX(charXX_t * __restrict pc, const char * __restrict s, size_t n,
    mbstate_t * __restrict ps)
{

	return (mbrtocXX_l(pc, s, n, ps, __get_locale()));
}
