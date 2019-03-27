/* $FreeBSD$ */
/* $NetBSD: citrus_dechanyu.c,v 1.4 2011/11/19 18:20:13 tnozaki Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2007 Citrus Project,
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
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"
#include "citrus_dechanyu.h"

/* ----------------------------------------------------------------------
 * private stuffs used by templates
 */

typedef struct {
	size_t	 chlen;
	char	 ch[4];
} _DECHanyuState;

typedef struct {
	int	 dummy;
} _DECHanyuEncodingInfo;

#define _CEI_TO_EI(_cei_)		(&(_cei_)->ei)
#define _CEI_TO_STATE(_cei_, _func_)	(_cei_)->states.__CONCAT(s_,_func_)

#define _FUNCNAME(m)			__CONCAT(_citrus_DECHanyu_,m)
#define _ENCODING_INFO			_DECHanyuEncodingInfo
#define _ENCODING_STATE			_DECHanyuState
#define _ENCODING_MB_CUR_MAX(_ei_)		4
#define _ENCODING_IS_STATE_DEPENDENT		0
#define _STATE_NEEDS_EXPLICIT_INIT(_ps_)	0

static __inline void
/*ARGSUSED*/
_citrus_DECHanyu_init_state(_DECHanyuEncodingInfo * __restrict ei __unused,
    _DECHanyuState * __restrict psenc)
{

	psenc->chlen = 0;
}

#if 0
static __inline void
/*ARGSUSED*/
_citrus_DECHanyu_pack_state(_DECHanyuEncodingInfo * __restrict ei __unused,
    void * __restrict pspriv, const _DECHanyuState * __restrict psenc)
{

	memcpy(pspriv, (const void *)psenc, sizeof(*psenc));
}

static __inline void
/*ARGSUSED*/
_citrus_DECHanyu_unpack_state(_DECHanyuEncodingInfo * __restrict ei __unused,
    _DECHanyuState * __restrict psenc,
    const void * __restrict pspriv)
{

	memcpy((void *)psenc, pspriv, sizeof(*psenc));
}
#endif

static void
/*ARGSUSED*/
_citrus_DECHanyu_encoding_module_uninit(_DECHanyuEncodingInfo *ei __unused)
{

	/* ei may be null */
}

static int
/*ARGSUSED*/
_citrus_DECHanyu_encoding_module_init(_DECHanyuEncodingInfo * __restrict ei __unused,
    const void * __restrict var __unused, size_t lenvar __unused)
{

	/* ei may be null */
	return (0);
}

static __inline bool
is_singlebyte(int c)
{

	return (c <= 0x7F);
}

static __inline bool
is_leadbyte(int c)
{

	return (c >= 0xA1 && c <= 0xFE);
}

static __inline bool
is_trailbyte(int c)
{

	c &= ~0x80;
	return (c >= 0x21 && c <= 0x7E);
}

static __inline bool
is_hanyu1(int c)
{

	return (c == 0xC2);
}

static __inline bool
is_hanyu2(int c)
{

	return (c == 0xCB);
}

#define HANYUBIT	0xC2CB0000

static __inline bool
is_94charset(int c)
{

	return (c >= 0x21 && c <= 0x7E);
}

static int
/*ARGSUSED*/
_citrus_DECHanyu_mbrtowc_priv(_DECHanyuEncodingInfo * __restrict ei,
    wchar_t * __restrict pwc, char ** __restrict s, size_t n,
    _DECHanyuState * __restrict psenc, size_t * __restrict nresult)
{
	char *s0;
	wchar_t wc;
	int ch;

	if (*s == NULL) {
		_citrus_DECHanyu_init_state(ei, psenc);
		*nresult = _ENCODING_IS_STATE_DEPENDENT;
		return (0);
	}
	s0 = *s;

	wc = (wchar_t)0;
	switch (psenc->chlen) {
	case 0:
		if (n-- < 1)
			goto restart;
		ch = *s0++ & 0xFF;
		if (is_singlebyte(ch)) {
			if (pwc != NULL)
				*pwc = (wchar_t)ch;
			*nresult = (size_t)((ch == 0) ? 0 : 1);
			*s = s0;
			return (0);
		}
		if (!is_leadbyte(ch))
			goto ilseq;
		psenc->ch[psenc->chlen++] = ch;
		break;
	case 1:
		ch = psenc->ch[0] & 0xFF;
		if (!is_leadbyte(ch))
			return (EINVAL);
		break;
	case 2: case 3:
		ch = psenc->ch[0] & 0xFF;
		if (is_hanyu1(ch)) {
			ch = psenc->ch[1] & 0xFF;
			if (is_hanyu2(ch)) {
				wc |= (wchar_t)HANYUBIT;
				break;
			}
		}
	/*FALLTHROUGH*/
	default:
		return (EINVAL);
	}

	switch (psenc->chlen) {
	case 1:
		if (is_hanyu1(ch)) {
			if (n-- < 1)
				goto restart;
			ch = *s0++ & 0xFF;
			if (!is_hanyu2(ch))
				goto ilseq;
			psenc->ch[psenc->chlen++] = ch;
			wc |= (wchar_t)HANYUBIT;
			if (n-- < 1)
				goto restart;
			ch = *s0++ & 0xFF;
			if (!is_leadbyte(ch))
				goto ilseq;
			psenc->ch[psenc->chlen++] = ch;
		}
		break;
	case 2:
		if (n-- < 1)
			goto restart;
		ch = *s0++ & 0xFF;
		if (!is_leadbyte(ch))
			goto ilseq;
		psenc->ch[psenc->chlen++] = ch;
		break;
	case 3:
		ch = psenc->ch[2] & 0xFF;
		if (!is_leadbyte(ch))
			return (EINVAL);
	}
	if (n-- < 1)
		goto restart;
	wc |= (wchar_t)(ch << 8);
	ch = *s0++ & 0xFF;
	if (!is_trailbyte(ch))
		goto ilseq;
	wc |= (wchar_t)ch;
	if (pwc != NULL)
		*pwc = wc;
	*nresult = (size_t)(s0 - *s);
	*s = s0;
	psenc->chlen = 0;

	return (0);

restart:
	*nresult = (size_t)-2;
	*s = s0;
	return (0);

ilseq:
	*nresult = (size_t)-1;
	return (EILSEQ);
}

static int
/*ARGSUSED*/
_citrus_DECHanyu_wcrtomb_priv(_DECHanyuEncodingInfo * __restrict ei __unused,
    char * __restrict s, size_t n, wchar_t wc,
    _DECHanyuState * __restrict psenc, size_t * __restrict nresult)
{
	int ch;

	if (psenc->chlen != 0)
		return (EINVAL);

	/* XXX: assume wchar_t as int */
	if ((uint32_t)wc <= 0x7F) {
		ch = wc & 0xFF;
	} else {
		if ((uint32_t)wc > 0xFFFF) {
			if ((wc & ~0xFFFF) != (wchar_t)HANYUBIT)
				goto ilseq;
			psenc->ch[psenc->chlen++] = (wc >> 24) & 0xFF;
			psenc->ch[psenc->chlen++] = (wc >> 16) & 0xFF;
			wc &= 0xFFFF;
		}
		ch = (wc >> 8) & 0xFF;
		if (!is_leadbyte(ch))
			goto ilseq;
		psenc->ch[psenc->chlen++] = ch;
		ch = wc & 0xFF;
		if (!is_trailbyte(ch))
			goto ilseq;
	}
	psenc->ch[psenc->chlen++] = ch;
	if (n < psenc->chlen) {
		*nresult = (size_t)-1;
		return (E2BIG);
	}
	memcpy(s, psenc->ch, psenc->chlen);
	*nresult = psenc->chlen;
	psenc->chlen = 0;

	return (0);

ilseq:
	*nresult = (size_t)-1;
	return (EILSEQ);
}

static __inline int
/*ARGSUSED*/
_citrus_DECHanyu_stdenc_wctocs(_DECHanyuEncodingInfo * __restrict ei __unused,
    _csid_t * __restrict csid, _index_t * __restrict idx, wchar_t wc)
{
	wchar_t mask;
	int plane;

	plane = 0;
	mask = 0x7F;
	/* XXX: assume wchar_t as int */
	if ((uint32_t)wc > 0x7F) {
		if ((uint32_t)wc > 0xFFFF) {
			if ((wc & ~0xFFFF) != (wchar_t)HANYUBIT)
				return (EILSEQ);
			plane += 2;
		}
		if (!is_leadbyte((wc >> 8) & 0xFF) ||
		    !is_trailbyte(wc & 0xFF))
			return (EILSEQ);
		plane += (wc & 0x80) ? 1 : 2;
		mask |= 0x7F00;
	}
	*csid = plane;
	*idx = (_index_t)(wc & mask);

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_DECHanyu_stdenc_cstowc(_DECHanyuEncodingInfo * __restrict ei __unused,
    wchar_t * __restrict wc, _csid_t csid, _index_t idx)
{

	if (csid == 0) {
		if (idx > 0x7F)
			return (EILSEQ);
	} else if (csid <= 4) {
		if (!is_94charset(idx >> 8))
			return (EILSEQ);
		if (!is_94charset(idx & 0xFF))
			return (EILSEQ);
		if (csid % 2)
			idx |= 0x80;
		idx |= 0x8000;
		if (csid > 2)
			idx |= HANYUBIT;
	} else
		return (EILSEQ);
	*wc = (wchar_t)idx;
	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_DECHanyu_stdenc_get_state_desc_generic(
    _DECHanyuEncodingInfo * __restrict ei __unused,
    _DECHanyuState * __restrict psenc, int * __restrict rstate)
{

	*rstate = (psenc->chlen == 0)
	    ? _STDENC_SDGEN_INITIAL
	    : _STDENC_SDGEN_INCOMPLETE_CHAR;
	return (0);
}

/* ----------------------------------------------------------------------
 * public interface for stdenc
 */

_CITRUS_STDENC_DECLS(DECHanyu);
_CITRUS_STDENC_DEF_OPS(DECHanyu);

#include "citrus_stdenc_template.h"
