/* $FreeBSD$ */
/*	$NetBSD: citrus_utf1632.c,v 1.9 2008/06/14 16:01:08 tnozaki Exp $	*/

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
#include <sys/endian.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"
#include "citrus_bcs.h"

#include "citrus_utf1632.h"


/* ----------------------------------------------------------------------
 * private stuffs used by templates
 */

typedef struct {
	int		 chlen;
	int		 current_endian;
	uint8_t		 ch[4];
} _UTF1632State;

#define _ENDIAN_UNKNOWN		0
#define _ENDIAN_BIG		1
#define _ENDIAN_LITTLE		2
#if BYTE_ORDER == BIG_ENDIAN
#define _ENDIAN_INTERNAL	_ENDIAN_BIG
#define _ENDIAN_SWAPPED		_ENDIAN_LITTLE
#else
#define _ENDIAN_INTERNAL	_ENDIAN_LITTLE
#define _ENDIAN_SWAPPED	_ENDIAN_BIG
#endif
#define _MODE_UTF32		0x00000001U
#define _MODE_FORCE_ENDIAN	0x00000002U

typedef struct {
	int		 preffered_endian;
	unsigned int	 cur_max;
	uint32_t	 mode;
} _UTF1632EncodingInfo;

#define _FUNCNAME(m)			_citrus_UTF1632_##m
#define _ENCODING_INFO			_UTF1632EncodingInfo
#define _ENCODING_STATE			_UTF1632State
#define _ENCODING_MB_CUR_MAX(_ei_)	((_ei_)->cur_max)
#define _ENCODING_IS_STATE_DEPENDENT	0
#define _STATE_NEEDS_EXPLICIT_INIT(_ps_)	0


static __inline void
/*ARGSUSED*/
_citrus_UTF1632_init_state(_UTF1632EncodingInfo *ei __unused,
    _UTF1632State *s)
{

	memset(s, 0, sizeof(*s));
}

static int
_citrus_UTF1632_mbrtowc_priv(_UTF1632EncodingInfo *ei, wchar_t *pwc,
    char **s, size_t n, _UTF1632State *psenc, size_t *nresult)
{
	char *s0;
	size_t result;
	wchar_t wc = L'\0';
	int chlenbak, endian, needlen;

	s0 = *s;

	if (s0 == NULL) {
		_citrus_UTF1632_init_state(ei, psenc);
		*nresult = 0; /* state independent */
		return (0);
	}

	result = 0;
	chlenbak = psenc->chlen;

refetch:
	needlen = ((ei->mode & _MODE_UTF32) != 0 || chlenbak >= 2) ? 4 : 2;

	while (chlenbak < needlen) {
		if (n == 0)
			goto restart;
		psenc->ch[chlenbak++] = *s0++;
		n--;
		result++;
	}

	/* judge endian marker */
	if ((ei->mode & _MODE_UTF32) == 0) {
		/* UTF16 */
		if (psenc->ch[0] == 0xFE && psenc->ch[1] == 0xFF) {
			psenc->current_endian = _ENDIAN_BIG;
			chlenbak = 0;
			goto refetch;
		} else if (psenc->ch[0] == 0xFF && psenc->ch[1] == 0xFE) {
			psenc->current_endian = _ENDIAN_LITTLE;
			chlenbak = 0;
			goto refetch;
		}
	} else {
		/* UTF32 */
		if (psenc->ch[0] == 0x00 && psenc->ch[1] == 0x00 &&
		    psenc->ch[2] == 0xFE && psenc->ch[3] == 0xFF) {
			psenc->current_endian = _ENDIAN_BIG;
			chlenbak = 0;
			goto refetch;
		} else if (psenc->ch[0] == 0xFF && psenc->ch[1] == 0xFE &&
			   psenc->ch[2] == 0x00 && psenc->ch[3] == 0x00) {
			psenc->current_endian = _ENDIAN_LITTLE;
			chlenbak = 0;
			goto refetch;
		}
	}
	endian = ((ei->mode & _MODE_FORCE_ENDIAN) != 0 ||
	    psenc->current_endian == _ENDIAN_UNKNOWN) ? ei->preffered_endian :
	    psenc->current_endian;

	/* get wc */
	if ((ei->mode & _MODE_UTF32) == 0) {
		/* UTF16 */
		if (needlen == 2) {
			switch (endian) {
			case _ENDIAN_LITTLE:
				wc = (psenc->ch[0] |
				    ((wchar_t)psenc->ch[1] << 8));
				break;
			case _ENDIAN_BIG:
				wc = (psenc->ch[1] |
				    ((wchar_t)psenc->ch[0] << 8));
				break;
			default:
				goto ilseq;
			}
			if (wc >= 0xD800 && wc <= 0xDBFF) {
				/* surrogate high */
				needlen = 4;
				goto refetch;
			}
		} else {
			/* surrogate low */
			wc -= 0xD800; /* wc : surrogate high (see above) */
			wc <<= 10;
			switch (endian) {
			case _ENDIAN_LITTLE:
				if (psenc->ch[3] < 0xDC || psenc->ch[3] > 0xDF)
					goto ilseq;
				wc |= psenc->ch[2];
				wc |= (wchar_t)(psenc->ch[3] & 3) << 8;
				break;
			case _ENDIAN_BIG:
				if (psenc->ch[2]<0xDC || psenc->ch[2]>0xDF)
					goto ilseq;
				wc |= psenc->ch[3];
				wc |= (wchar_t)(psenc->ch[2] & 3) << 8;
				break;
			default:
				goto ilseq;
			}
			wc += 0x10000;
		}
	} else {
		/* UTF32 */
		switch (endian) {
		case _ENDIAN_LITTLE:
			wc = (psenc->ch[0] |
			    ((wchar_t)psenc->ch[1] << 8) |
			    ((wchar_t)psenc->ch[2] << 16) |
			    ((wchar_t)psenc->ch[3] << 24));
			break;
		case _ENDIAN_BIG:
			wc = (psenc->ch[3] |
			    ((wchar_t)psenc->ch[2] << 8) |
			    ((wchar_t)psenc->ch[1] << 16) |
			    ((wchar_t)psenc->ch[0] << 24));
			break;
		default:
			goto ilseq;
		}
		if (wc >= 0xD800 && wc <= 0xDFFF)
			goto ilseq;
	}


	*pwc = wc;
	psenc->chlen = 0;
	*nresult = result;
	*s = s0;

	return (0);

ilseq:
	*nresult = (size_t)-1;
	psenc->chlen = 0;
	return (EILSEQ);

restart:
	*nresult = (size_t)-2;
	psenc->chlen = chlenbak;
	*s = s0;
	return (0);
}

static int
_citrus_UTF1632_wcrtomb_priv(_UTF1632EncodingInfo *ei, char *s, size_t n,
    wchar_t wc, _UTF1632State *psenc, size_t *nresult)
{
	wchar_t wc2;
	static const char _bom[4] = {
	    0x00, 0x00, 0xFE, 0xFF,
	};
	const char *bom = &_bom[0];
	size_t cnt;

	cnt = (size_t)0;
	if (psenc->current_endian == _ENDIAN_UNKNOWN) {
		if ((ei->mode & _MODE_FORCE_ENDIAN) == 0) {
			if (ei->mode & _MODE_UTF32)
				cnt = 4;
			else {
				cnt = 2;
				bom += 2;
			}
			if (n < cnt)
				goto e2big;
			memcpy(s, bom, cnt);
			s += cnt, n -= cnt;
		}
		psenc->current_endian = ei->preffered_endian;
	}

	wc2 = 0;
	if ((ei->mode & _MODE_UTF32)==0) {
		/* UTF16 */
		if (wc > 0xFFFF) {
			/* surrogate */
			if (wc > 0x10FFFF)
				goto ilseq;
			if (n < 4)
				goto e2big;
			cnt += 4;
			wc -= 0x10000;
			wc2 = (wc & 0x3FF) | 0xDC00;
			wc = (wc>>10) | 0xD800;
		} else {
			if (n < 2)
				goto e2big;
			cnt += 2;
		}

surrogate:
		switch (psenc->current_endian) {
		case _ENDIAN_BIG:
			s[1] = wc;
			s[0] = (wc >>= 8);
			break;
		case _ENDIAN_LITTLE:
			s[0] = wc;
			s[1] = (wc >>= 8);
			break;
		}
		if (wc2 != 0) {
			wc = wc2;
			wc2 = 0;
			s += 2;
			goto surrogate;
		}
	} else {
		/* UTF32 */
		if (wc >= 0xD800 && wc <= 0xDFFF)
			goto ilseq;
		if (n < 4)
			goto e2big;
		cnt += 4;
		switch (psenc->current_endian) {
		case _ENDIAN_BIG:
			s[3] = wc;
			s[2] = (wc >>= 8);
			s[1] = (wc >>= 8);
			s[0] = (wc >>= 8);
			break;
		case _ENDIAN_LITTLE:
			s[0] = wc;
			s[1] = (wc >>= 8);
			s[2] = (wc >>= 8);
			s[3] = (wc >>= 8);
			break;
		}
	}
	*nresult = cnt;

	return (0);

ilseq:
	*nresult = (size_t)-1;
	return (EILSEQ);
e2big:
	*nresult = (size_t)-1;
	return (E2BIG);
}

static void
parse_variable(_UTF1632EncodingInfo * __restrict ei,
    const void * __restrict var, size_t lenvar)
{
	const char *p;

	p = var;
	while (lenvar > 0) {
		switch (*p) {
		case 'B':
		case 'b':
			MATCH(big, ei->preffered_endian = _ENDIAN_BIG);
			break;
		case 'L':
		case 'l':
			MATCH(little, ei->preffered_endian = _ENDIAN_LITTLE);
			break;
		case 'i':
		case 'I':
			MATCH(internal, ei->preffered_endian = _ENDIAN_INTERNAL);
			break;
		case 's':
		case 'S':
			MATCH(swapped, ei->preffered_endian = _ENDIAN_SWAPPED);
			break;
		case 'F':
		case 'f':
			MATCH(force, ei->mode |= _MODE_FORCE_ENDIAN);
			break;
		case 'U':
		case 'u':
			MATCH(utf32, ei->mode |= _MODE_UTF32);
			break;
		}
		p++;
		lenvar--;
	}
}

static int
/*ARGSUSED*/
_citrus_UTF1632_encoding_module_init(_UTF1632EncodingInfo * __restrict ei,
    const void * __restrict var, size_t lenvar)
{

	memset((void *)ei, 0, sizeof(*ei));

	parse_variable(ei, var, lenvar);

	ei->cur_max = ((ei->mode&_MODE_UTF32) == 0) ? 6 : 8;
	/* 6: endian + surrogate */
	/* 8: endian + normal */

	if (ei->preffered_endian == _ENDIAN_UNKNOWN) {
		ei->preffered_endian = _ENDIAN_BIG;
	}

	return (0);
}

static void
/*ARGSUSED*/
_citrus_UTF1632_encoding_module_uninit(_UTF1632EncodingInfo *ei __unused)
{

}

static __inline int
/*ARGSUSED*/
_citrus_UTF1632_stdenc_wctocs(_UTF1632EncodingInfo * __restrict ei __unused,
     _csid_t * __restrict csid, _index_t * __restrict idx, _wc_t wc)
{

	*csid = 0;
	*idx = (_index_t)wc;

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_UTF1632_stdenc_cstowc(_UTF1632EncodingInfo * __restrict ei __unused,
    _wc_t * __restrict wc, _csid_t csid, _index_t idx)
{

	if (csid != 0)
		return (EILSEQ);

	*wc = (_wc_t)idx;

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_UTF1632_stdenc_get_state_desc_generic(_UTF1632EncodingInfo * __restrict ei __unused,
    _UTF1632State * __restrict psenc, int * __restrict rstate)
{

	*rstate = (psenc->chlen == 0) ? _STDENC_SDGEN_INITIAL :
	    _STDENC_SDGEN_INCOMPLETE_CHAR;
	return (0);
}

/* ----------------------------------------------------------------------
 * public interface for stdenc
 */

_CITRUS_STDENC_DECLS(UTF1632);
_CITRUS_STDENC_DEF_OPS(UTF1632);

#include "citrus_stdenc_template.h"
