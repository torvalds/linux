/* $FreeBSD$ */
/*	$NetBSD: citrus_mskanji.c,v 1.13 2008/06/14 16:01:08 tnozaki Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2002 Citrus Project,
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

/*
 *    ja_JP.SJIS locale table for BSD4.4/rune
 *    version 1.0
 *    (C) Sin'ichiro MIYATANI / Phase One, Inc
 *    May 12, 1995
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Phase One, Inc.
 * 4. The name of Phase One, Inc. may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"
#include "citrus_mskanji.h"


/* ----------------------------------------------------------------------
 * private stuffs used by templates
 */

typedef struct _MSKanjiState {
	int	 chlen;
	char	 ch[2];
} _MSKanjiState;

typedef struct {
	int	 mode;
#define MODE_JIS2004	1
} _MSKanjiEncodingInfo;

#define _CEI_TO_EI(_cei_)		(&(_cei_)->ei)
#define _CEI_TO_STATE(_cei_, _func_)	(_cei_)->states.s_##_func_

#define _FUNCNAME(m)			_citrus_MSKanji_##m
#define _ENCODING_INFO			_MSKanjiEncodingInfo
#define _ENCODING_STATE			_MSKanjiState
#define _ENCODING_MB_CUR_MAX(_ei_)	2
#define _ENCODING_IS_STATE_DEPENDENT	0
#define _STATE_NEEDS_EXPLICIT_INIT(_ps_)	0


static bool
_mskanji1(int c)
{

	return ((c >= 0x81 && c <= 0x9f) || (c >= 0xe0 && c <= 0xfc));
}

static bool
_mskanji2(int c)
{

	return ((c >= 0x40 && c <= 0x7e) || (c >= 0x80 && c <= 0xfc));
}

static __inline void
/*ARGSUSED*/
_citrus_MSKanji_init_state(_MSKanjiEncodingInfo * __restrict ei __unused,
    _MSKanjiState * __restrict s)
{

	s->chlen = 0;
}

#if 0
static __inline void
/*ARGSUSED*/
_citrus_MSKanji_pack_state(_MSKanjiEncodingInfo * __restrict ei __unused,
    void * __restrict pspriv, const _MSKanjiState * __restrict s)
{

	memcpy(pspriv, (const void *)s, sizeof(*s));
}

static __inline void
/*ARGSUSED*/
_citrus_MSKanji_unpack_state(_MSKanjiEncodingInfo * __restrict ei __unused,
    _MSKanjiState * __restrict s, const void * __restrict pspriv)
{

	memcpy((void *)s, pspriv, sizeof(*s));
}
#endif

static int
/*ARGSUSED*/
_citrus_MSKanji_mbrtowc_priv(_MSKanjiEncodingInfo * __restrict ei,
    wchar_t * __restrict pwc, char ** __restrict s, size_t n,
    _MSKanjiState * __restrict psenc, size_t * __restrict nresult)
{
	char *s0;
	wchar_t wchar;
	int chlenbak, len;

	s0 = *s;

	if (s0 == NULL) {
		_citrus_MSKanji_init_state(ei, psenc);
		*nresult = 0; /* state independent */
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
		goto encoding_error;
	}

	len = _mskanji1(psenc->ch[0] & 0xff) ? 2 : 1;
	while (psenc->chlen < len) {
		if (n < 1)
			goto restart;
		psenc->ch[psenc->chlen] = *s0++;
		psenc->chlen++;
		n--;
	}

	*s = s0;

	switch (len) {
	case 1:
		wchar = psenc->ch[0] & 0xff;
		break;
	case 2:
		if (!_mskanji2(psenc->ch[1] & 0xff))
			goto encoding_error;
		wchar = ((psenc->ch[0] & 0xff) << 8) | (psenc->ch[1] & 0xff);
		break;
	default:
		/* illegal state */
		goto encoding_error;
	}

	psenc->chlen = 0;

	if (pwc)
		*pwc = wchar;
	*nresult = wchar ? len - chlenbak : 0;
	return (0);

encoding_error:
	psenc->chlen = 0;
	*nresult = (size_t)-1;
	return (EILSEQ);

restart:
	*nresult = (size_t)-2;
	*s = s0;
	return (0);
}


static int
_citrus_MSKanji_wcrtomb_priv(_MSKanjiEncodingInfo * __restrict ei __unused,
    char * __restrict s, size_t n, wchar_t wc,
    _MSKanjiState * __restrict psenc __unused, size_t * __restrict nresult)
{
	int ret;

	/* check invalid sequence */
	if (wc & ~0xffff) {
		ret = EILSEQ;
		goto err;
	}

	if (wc & 0xff00) {
		if (n < 2) {
			ret = E2BIG;
			goto err;
		}

		s[0] = (wc >> 8) & 0xff;
		s[1] = wc & 0xff;
		if (!_mskanji1(s[0] & 0xff) || !_mskanji2(s[1] & 0xff)) {
			ret = EILSEQ;
			goto err;
		}

		*nresult = 2;
		return (0);
	} else {
		if (n < 1) {
			ret = E2BIG;
			goto err;
		}

		s[0] = wc & 0xff;
		if (_mskanji1(s[0] & 0xff)) {
			ret = EILSEQ;
			goto err;
		}

		*nresult = 1;
		return (0);
	}

err:
	*nresult = (size_t)-1;
	return (ret);
}


static __inline int
/*ARGSUSED*/
_citrus_MSKanji_stdenc_wctocs(_MSKanjiEncodingInfo * __restrict ei,
    _csid_t * __restrict csid, _index_t * __restrict idx, wchar_t wc)
{
	_index_t col, row;
	int offset;

	if ((_wc_t)wc < 0x80) {
		/* ISO-646 */
		*csid = 0;
		*idx = (_index_t)wc;
	} else if ((_wc_t)wc < 0x100) {
		/* KANA */
		*csid = 1;
		*idx = (_index_t)wc & 0x7F;
	} else {
		/* Kanji (containing Gaiji zone) */
		/*
		 * 94^2 zone (contains a part of Gaiji (0xED40 - 0xEEFC)):
		 * 0x8140 - 0x817E -> 0x2121 - 0x215F
		 * 0x8180 - 0x819E -> 0x2160 - 0x217E
		 * 0x819F - 0x81FC -> 0x2221 - 0x227E
		 *
		 * 0x8240 - 0x827E -> 0x2321 - 0x235F
		 *  ...
		 * 0x9F9F - 0x9FFc -> 0x5E21 - 0x5E7E
		 *
		 * 0xE040 - 0xE07E -> 0x5F21 - 0x5F5F
		 *  ...
		 * 0xEF9F - 0xEFFC -> 0x7E21 - 0x7E7E
		 *
		 * extended Gaiji zone:
		 * 0xF040 - 0xFCFC
		 *
		 * JIS X0213-plane2:
		 * 0xF040 - 0xF09E -> 0x2121 - 0x217E
		 * 0xF140 - 0xF19E -> 0x2321 - 0x237E
		 * ...
		 * 0xF240 - 0xF29E -> 0x2521 - 0x257E
		 *
		 * 0xF09F - 0xF0FC -> 0x2821 - 0x287E
		 * 0xF29F - 0xF2FC -> 0x2C21 - 0x2C7E
		 * ...
		 * 0xF44F - 0xF49E -> 0x2F21 - 0x2F7E
		 *
		 * 0xF49F - 0xF4FC -> 0x6E21 - 0x6E7E
		 * ...
		 * 0xFC9F - 0xFCFC -> 0x7E21 - 0x7E7E
		 */
		row = ((_wc_t)wc >> 8) & 0xFF;
		col = (_wc_t)wc & 0xFF;
		if (!_mskanji1(row) || !_mskanji2(col))
			return (EILSEQ);
		if ((ei->mode & MODE_JIS2004) == 0 || row < 0xF0) {
			*csid = 2;
			offset = 0x81;
		} else {
			*csid = 3;
			if ((_wc_t)wc <= 0xF49E) {
				offset = (_wc_t)wc >= 0xF29F ||
				    ((_wc_t)wc >= 0xF09F &&
				    (_wc_t)wc <= 0xF0FC) ? 0xED : 0xF0;
			} else
				offset = 0xCE;
		}
		row -= offset;
		if (row >= 0x5F)
			row -= 0x40;
		row = row * 2 + 0x21;
		col -= 0x1F;
		if (col >= 0x61)
			col -= 1;
		if (col > 0x7E) {
			row += 1;
			col -= 0x5E;
		}
		*idx = ((_index_t)row << 8) | col;
	}

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_MSKanji_stdenc_cstowc(_MSKanjiEncodingInfo * __restrict ei,
    wchar_t * __restrict wc, _csid_t csid, _index_t idx)
{
	uint32_t col, row;
	int offset;

	switch (csid) {
	case 0:
		/* ISO-646 */
		if (idx >= 0x80)
			return (EILSEQ);
		*wc = (wchar_t)idx;
		break;
	case 1:
		/* kana */
		if (idx >= 0x80)
			return (EILSEQ);
		*wc = (wchar_t)idx + 0x80;
		break;
	case 3:
		if ((ei->mode & MODE_JIS2004) == 0)
			return (EILSEQ);
	/*FALLTHROUGH*/
	case 2:
		/* kanji */
		row = (idx >> 8);
		if (row < 0x21)
			return (EILSEQ);
		if (csid == 3) {
			if (row <= 0x2F)
				offset = (row == 0x22 || row >= 0x26) ?
				    0xED : 0xF0;
			else if (row >= 0x4D && row <= 0x7E)
				offset = 0xCE;
			else
				return (EILSEQ);
		} else {
			if (row > 0x97)
				return (EILSEQ);
			offset = (row < 0x5F) ? 0x81 : 0xC1;
		}
		col = idx & 0xFF;
		if (col < 0x21 || col > 0x7E)
			return (EILSEQ);
		row -= 0x21; col -= 0x21;
		if ((row & 1) == 0) {
			col += 0x40;
			if (col >= 0x7F)
				col += 1;
		} else
			col += 0x9F;
		row = row / 2 + offset;
		*wc = ((wchar_t)row << 8) | col;
		break;
	default:
		return (EILSEQ);
	}

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_MSKanji_stdenc_get_state_desc_generic(_MSKanjiEncodingInfo * __restrict ei __unused,
    _MSKanjiState * __restrict psenc, int * __restrict rstate)
{

	*rstate = (psenc->chlen == 0) ? _STDENC_SDGEN_INITIAL :
	    _STDENC_SDGEN_INCOMPLETE_CHAR;
	return (0);
}

static int
/*ARGSUSED*/
_citrus_MSKanji_encoding_module_init(_MSKanjiEncodingInfo *  __restrict ei,
    const void * __restrict var, size_t lenvar)
{
	const char *p;

	p = var;
	memset((void *)ei, 0, sizeof(*ei));
	while (lenvar > 0) {
		switch (_bcs_toupper(*p)) {
		case 'J':
			MATCH(JIS2004, ei->mode |= MODE_JIS2004);
			break;
		}
		++p;
		--lenvar;
	}

	return (0);
}

static void
_citrus_MSKanji_encoding_module_uninit(_MSKanjiEncodingInfo *ei __unused)
{

}

/* ----------------------------------------------------------------------
 * public interface for stdenc
 */

_CITRUS_STDENC_DECLS(MSKanji);
_CITRUS_STDENC_DEF_OPS(MSKanji);

#include "citrus_stdenc_template.h"
