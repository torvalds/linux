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
#include <uchar.h>

#include "../iconv/citrus_hash.h"
#include "../iconv/citrus_module.h"
#include "../iconv/citrus_iconv.h"
#include "mblocal.h"

typedef struct {
	bool			initialized;
	struct _citrus_iconv	iconv;
	union {
		charXX_t	widechar[SRCBUF_LEN];
		char		bytes[sizeof(charXX_t) * SRCBUF_LEN];
	} srcbuf;
	size_t			srcbuf_len;
} _ConversionState;
_Static_assert(sizeof(_ConversionState) <= sizeof(mbstate_t),
    "Size of _ConversionState must not exceed mbstate_t's size.");

size_t
cXXrtomb_l(char * __restrict s, charXX_t c, mbstate_t * __restrict ps,
    locale_t locale)
{
	_ConversionState *cs;
	struct _citrus_iconv *handle;
	char *src, *dst;
	size_t srcleft, dstleft, invlen;
	int err;

	FIX_LOCALE(locale);
	if (ps == NULL)
		ps = &(XLOCALE_CTYPE(locale)->cXXrtomb);
	cs = (_ConversionState *)ps;
	handle = &cs->iconv;

	/* Reinitialize mbstate_t. */
	if (s == NULL || !cs->initialized) {
		if (_citrus_iconv_open(&handle, UTF_XX_INTERNAL,
		    nl_langinfo_l(CODESET, locale)) != 0) {
			cs->initialized = false;
			errno = EINVAL;
			return (-1);
		}
		handle->cv_shared->ci_discard_ilseq = true;
		handle->cv_shared->ci_hooks = NULL;
		cs->srcbuf_len = 0;
		cs->initialized = true;
		if (s == NULL)
			return (1);
	}

	assert(cs->srcbuf_len < sizeof(cs->srcbuf.widechar) / sizeof(charXX_t));
	cs->srcbuf.widechar[cs->srcbuf_len++] = c;

	/* Perform conversion. */
	src = cs->srcbuf.bytes;
	srcleft = cs->srcbuf_len * sizeof(charXX_t);
	dst = s;
	dstleft = MB_CUR_MAX_L(locale);
	err = _citrus_iconv_convert(handle, &src, &srcleft, &dst, &dstleft,
	    0, &invlen);

	/* Character is part of a surrogate pair. We need more input. */
	if (err == EINVAL)
		return (0);
	cs->srcbuf_len = 0;
	
	/* Illegal sequence. */
	if (dst == s) {
		errno = EILSEQ;
		return ((size_t)-1);
	}
	return (dst - s);
}

size_t
cXXrtomb(char * __restrict s, charXX_t c, mbstate_t * __restrict ps)
{

	return (cXXrtomb_l(s, c, ps, __get_locale()));
}
