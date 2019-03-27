/* $FreeBSD$ */
/*	$NetBSD: citrus_big5.c,v 1.13 2011/05/23 14:53:46 joerg Exp $	*/

/*-
 * Copyright (c)2002, 2006 Citrus Project,
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

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Paul Borman at Krystal Technologies.
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

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "citrus_namespace.h"
#include "citrus_prop.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"
#include "citrus_big5.h"

/* ----------------------------------------------------------------------
 * private stuffs used by templates
 */

typedef struct {
	int	 chlen;
	char	 ch[2];
} _BIG5State;

typedef struct _BIG5Exclude {
	TAILQ_ENTRY(_BIG5Exclude)	 entry;
	wint_t				 start;
	wint_t				 end;
} _BIG5Exclude;

typedef TAILQ_HEAD(_BIG5ExcludeList, _BIG5Exclude) _BIG5ExcludeList;

typedef struct {
	_BIG5ExcludeList	 excludes;
	int			 cell[0x100];
} _BIG5EncodingInfo;

#define _CEI_TO_EI(_cei_)		(&(_cei_)->ei)
#define _CEI_TO_STATE(_cei_, _func_)	(_cei_)->states.s_##_func_

#define _FUNCNAME(m)			_citrus_BIG5_##m
#define _ENCODING_INFO			_BIG5EncodingInfo
#define _ENCODING_STATE			_BIG5State
#define _ENCODING_MB_CUR_MAX(_ei_)	2
#define _ENCODING_IS_STATE_DEPENDENT	0
#define _STATE_NEEDS_EXPLICIT_INIT(_ps_)	0


static __inline void
/*ARGSUSED*/
_citrus_BIG5_init_state(_BIG5EncodingInfo * __restrict ei __unused,
    _BIG5State * __restrict s)
{

	memset(s, 0, sizeof(*s));
}

#if 0
static __inline void
/*ARGSUSED*/
_citrus_BIG5_pack_state(_BIG5EncodingInfo * __restrict ei __unused,
    void * __restrict pspriv,
    const _BIG5State * __restrict s)
{

	memcpy(pspriv, (const void *)s, sizeof(*s));
}

static __inline void
/*ARGSUSED*/
_citrus_BIG5_unpack_state(_BIG5EncodingInfo * __restrict ei __unused,
    _BIG5State * __restrict s,
    const void * __restrict pspriv)
{

	memcpy((void *)s, pspriv, sizeof(*s));
}
#endif

static __inline int
_citrus_BIG5_check(_BIG5EncodingInfo *ei, unsigned int c)
{

	return ((ei->cell[c & 0xFF] & 0x1) ? 2 : 1);
}

static __inline int
_citrus_BIG5_check2(_BIG5EncodingInfo *ei, unsigned int c)
{

	return ((ei->cell[c & 0xFF] & 0x2) ? 1 : 0);
}

static __inline int
_citrus_BIG5_check_excludes(_BIG5EncodingInfo *ei, wint_t c)
{
	_BIG5Exclude *exclude;

	TAILQ_FOREACH(exclude, &ei->excludes, entry) {
		if (c >= exclude->start && c <= exclude->end)
			return (EILSEQ);
	}
	return (0);
}

static int
_citrus_BIG5_fill_rowcol(void * __restrict ctx, const char * __restrict s,
    uint64_t start, uint64_t end)
{
	_BIG5EncodingInfo *ei;
	uint64_t n;
	int i;

	if (start > 0xFF || end > 0xFF)
		return (EINVAL);
	ei = (_BIG5EncodingInfo *)ctx;
	i = strcmp("row", s) ? 1 : 0;
	i = 1 << i;
	for (n = start; n <= end; ++n)
		ei->cell[n & 0xFF] |= i;
	return (0);
}

static int
/*ARGSUSED*/
_citrus_BIG5_fill_excludes(void * __restrict ctx,
    const char * __restrict s __unused, uint64_t start, uint64_t end)
{
	_BIG5EncodingInfo *ei;
	_BIG5Exclude *exclude;

	if (start > 0xFFFF || end > 0xFFFF)
		return (EINVAL);
	ei = (_BIG5EncodingInfo *)ctx;
	exclude = TAILQ_LAST(&ei->excludes, _BIG5ExcludeList);
	if (exclude != NULL && (wint_t)start <= exclude->end)
		return (EINVAL);
	exclude = (void *)malloc(sizeof(*exclude));
	if (exclude == NULL)
		return (ENOMEM);
	exclude->start = (wint_t)start;
	exclude->end = (wint_t)end;
	TAILQ_INSERT_TAIL(&ei->excludes, exclude, entry);

	return (0);
}

static const _citrus_prop_hint_t root_hints[] = {
    _CITRUS_PROP_HINT_NUM("row", &_citrus_BIG5_fill_rowcol),
    _CITRUS_PROP_HINT_NUM("col", &_citrus_BIG5_fill_rowcol),
    _CITRUS_PROP_HINT_NUM("excludes", &_citrus_BIG5_fill_excludes),
    _CITRUS_PROP_HINT_END
};

static void
/*ARGSUSED*/
_citrus_BIG5_encoding_module_uninit(_BIG5EncodingInfo *ei)
{
	_BIG5Exclude *exclude;

	while ((exclude = TAILQ_FIRST(&ei->excludes)) != NULL) {
		TAILQ_REMOVE(&ei->excludes, exclude, entry);
		free(exclude);
	}
}

static int
/*ARGSUSED*/
_citrus_BIG5_encoding_module_init(_BIG5EncodingInfo * __restrict ei,
    const void * __restrict var, size_t lenvar)
{
	const char *s;
	int err;

	memset((void *)ei, 0, sizeof(*ei));
	TAILQ_INIT(&ei->excludes);

	if (lenvar > 0 && var != NULL) {
		s = _bcs_skip_ws_len((const char *)var, &lenvar);
		if (lenvar > 0 && *s != '\0') {
			err = _citrus_prop_parse_variable(
			    root_hints, (void *)ei, s, lenvar);
			if (err == 0)
				return (0);

			_citrus_BIG5_encoding_module_uninit(ei);
			memset((void *)ei, 0, sizeof(*ei));
			TAILQ_INIT(&ei->excludes);
		}
	}

	/* fallback Big5-1984, for backward compatibility. */
	_citrus_BIG5_fill_rowcol(ei, "row", 0xA1, 0xFE);
	_citrus_BIG5_fill_rowcol(ei, "col", 0x40, 0x7E);
	_citrus_BIG5_fill_rowcol(ei, "col", 0xA1, 0xFE);

	return (0);
}

static int
/*ARGSUSED*/
_citrus_BIG5_mbrtowc_priv(_BIG5EncodingInfo * __restrict ei,
    wchar_t * __restrict pwc,
    char ** __restrict s, size_t n,
    _BIG5State * __restrict psenc,
    size_t * __restrict nresult)
{
	wchar_t wchar;
	char *s0;
	int c, chlenbak;

	s0 = *s;

	if (s0 == NULL) {
		_citrus_BIG5_init_state(ei, psenc);
		*nresult = 0;
		return (0);
	}

	chlenbak = psenc->chlen;

	/* make sure we have the first byte in the buffer */
	switch (psenc->chlen) {
	case 0:
		if (n < 1)
			goto restart;
		psenc->ch[0] = *s0++;
		psenc->chlen = 1;
		n--;
		break;
	case 1:
		break;
	default:
		/* illegal state */
		goto ilseq;
	}

	c = _citrus_BIG5_check(ei, psenc->ch[0] & 0xff);
	if (c == 0)
		goto ilseq;
	while (psenc->chlen < c) {
		if (n < 1) {
			goto restart;
		}
		psenc->ch[psenc->chlen] = *s0++;
		psenc->chlen++;
		n--;
	}

	switch (c) {
	case 1:
		wchar = psenc->ch[0] & 0xff;
		break;
	case 2:
		if (!_citrus_BIG5_check2(ei, psenc->ch[1] & 0xff))
			goto ilseq;
		wchar = ((psenc->ch[0] & 0xff) << 8) | (psenc->ch[1] & 0xff);
		break;
	default:
		/* illegal state */
		goto ilseq;
	}

	if (_citrus_BIG5_check_excludes(ei, (wint_t)wchar) != 0)
		goto ilseq;

	*s = s0;
	psenc->chlen = 0;
	if (pwc)
		*pwc = wchar;
	*nresult = wchar ? c - chlenbak : 0;

	return (0);

ilseq:
	psenc->chlen = 0;
	*nresult = (size_t)-1;
	return (EILSEQ);

restart:
	*s = s0;
	*nresult = (size_t)-2;
	return (0);
}

static int
/*ARGSUSED*/
_citrus_BIG5_wcrtomb_priv(_BIG5EncodingInfo * __restrict ei,
    char * __restrict s,
    size_t n, wchar_t wc, _BIG5State * __restrict psenc __unused,
    size_t * __restrict nresult)
{
	size_t l;
	int ret;

	/* check invalid sequence */
	if (wc & ~0xffff ||
	    _citrus_BIG5_check_excludes(ei, (wint_t)wc) != 0) {
		ret = EILSEQ;
		goto err;
	}

	if (wc & 0x8000) {
		if (_citrus_BIG5_check(ei, (wc >> 8) & 0xff) != 2 ||
		    !_citrus_BIG5_check2(ei, wc & 0xff)) {
			ret = EILSEQ;
			goto err;
		}
		l = 2;
	} else {
		if (wc & ~0xff || !_citrus_BIG5_check(ei, wc & 0xff)) {
			ret = EILSEQ;
			goto err;
		}
		l = 1;
	}

	if (n < l) {
		/* bound check failure */
		ret = E2BIG;
		goto err;
	}

	if (l == 2) {
		s[0] = (wc >> 8) & 0xff;
		s[1] = wc & 0xff;
	} else
		s[0] = wc & 0xff;

	*nresult = l;

	return (0);

err:
	*nresult = (size_t)-1;
	return (ret);
}

static __inline int
/*ARGSUSED*/
_citrus_BIG5_stdenc_wctocs(_BIG5EncodingInfo * __restrict ei __unused,
    _csid_t * __restrict csid,
    _index_t * __restrict idx, wchar_t wc)
{

	*csid = (wc < 0x100) ? 0 : 1;
	*idx = (_index_t)wc;

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_BIG5_stdenc_cstowc(_BIG5EncodingInfo * __restrict ei __unused,
    wchar_t * __restrict wc,
    _csid_t csid, _index_t idx)
{

	switch (csid) {
	case 0:
	case 1:
		*wc = (wchar_t)idx;
		break;
	default:
		return (EILSEQ);
	}

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_BIG5_stdenc_get_state_desc_generic(_BIG5EncodingInfo * __restrict ei __unused,
    _BIG5State * __restrict psenc,
    int * __restrict rstate)
{

	*rstate = (psenc->chlen == 0) ? _STDENC_SDGEN_INITIAL :
	    _STDENC_SDGEN_INCOMPLETE_CHAR;
	return (0);
}

/* ----------------------------------------------------------------------
 * public interface for stdenc
 */

_CITRUS_STDENC_DECLS(BIG5);
_CITRUS_STDENC_DEF_OPS(BIG5);

#include "citrus_stdenc_template.h"
