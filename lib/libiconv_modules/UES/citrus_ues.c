/* $FreeBSD$ */
/* $NetBSD: citrus_ues.c,v 1.3 2012/02/12 13:51:29 wiz Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2006 Citrus Project,
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
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"
#include "citrus_ues.h"

typedef struct {
	size_t	 mb_cur_max;
	int	 mode;
#define MODE_C99	1
} _UESEncodingInfo;

typedef struct {
	int	 chlen;
	char	 ch[12];
} _UESState;

#define _CEI_TO_EI(_cei_)               (&(_cei_)->ei)
#define _CEI_TO_STATE(_cei_, _func_)    (_cei_)->states.s_##_func_

#define _FUNCNAME(m)			_citrus_UES_##m
#define _ENCODING_INFO			_UESEncodingInfo
#define _ENCODING_STATE			_UESState
#define _ENCODING_MB_CUR_MAX(_ei_)	(_ei_)->mb_cur_max
#define _ENCODING_IS_STATE_DEPENDENT		0
#define _STATE_NEEDS_EXPLICIT_INIT(_ps_)	0

static __inline void
/*ARGSUSED*/
_citrus_UES_init_state(_UESEncodingInfo * __restrict ei __unused,
    _UESState * __restrict psenc)
{

	psenc->chlen = 0;
}

#if 0
static __inline void
/*ARGSUSED*/
_citrus_UES_pack_state(_UESEncodingInfo * __restrict ei __unused,
    void *__restrict pspriv, const _UESState * __restrict psenc)
{

	memcpy(pspriv, (const void *)psenc, sizeof(*psenc));
}

static __inline void
/*ARGSUSED*/
_citrus_UES_unpack_state(_UESEncodingInfo * __restrict ei __unused,
    _UESState * __restrict psenc, const void * __restrict pspriv)
{

	memcpy((void *)psenc, pspriv, sizeof(*psenc));
}
#endif

static __inline int
to_int(int ch)
{

	if (ch >= '0' && ch <= '9')
		return (ch - '0');
	else if (ch >= 'A' && ch <= 'F')
		return ((ch - 'A') + 10);
	else if (ch >= 'a' && ch <= 'f')
		return ((ch - 'a') + 10);
	return (-1);
}

#define ESCAPE		'\\'
#define UCS2_ESC	'u'
#define UCS4_ESC	'U'

#define UCS2_BIT	16
#define UCS4_BIT	32
#define BMP_MAX		UINT32_C(0xFFFF)
#define UCS2_MAX	UINT32_C(0x10FFFF)
#define UCS4_MAX	UINT32_C(0x7FFFFFFF)

static const char *xdig = "0123456789abcdef";

static __inline int
to_str(char *s, wchar_t wc, int bit)
{
	char *p;

	p = s;
	*p++ = ESCAPE;
	switch (bit) {
	case UCS2_BIT:
		*p++ = UCS2_ESC;
		break;
	case UCS4_BIT:
		*p++ = UCS4_ESC;
		break;
	default:
		abort();
	}
	do {
		*p++ = xdig[(wc >> (bit -= 4)) & 0xF];
	} while (bit > 0);
	return (p - s);
}

static __inline bool
is_hi_surrogate(wchar_t wc)
{

	return (wc >= 0xD800 && wc <= 0xDBFF);
}

static __inline bool
is_lo_surrogate(wchar_t wc)
{

	return (wc >= 0xDC00 && wc <= 0xDFFF);
}

static __inline wchar_t
surrogate_to_ucs(wchar_t hi, wchar_t lo)
{

	hi -= 0xD800;
	lo -= 0xDC00;
	return ((hi << 10 | lo) + 0x10000);
}

static __inline void
ucs_to_surrogate(wchar_t wc, wchar_t * __restrict hi, wchar_t * __restrict lo)
{

	wc -= 0x10000;
	*hi = (wc >> 10) + 0xD800;
	*lo = (wc & 0x3FF) + 0xDC00;
}

static __inline bool
is_basic(wchar_t wc)
{

	return ((uint32_t)wc <= 0x9F && wc != 0x24 && wc != 0x40 &&
	    wc != 0x60);
}

static int
_citrus_UES_mbrtowc_priv(_UESEncodingInfo * __restrict ei,
    wchar_t * __restrict pwc, char ** __restrict s, size_t n,
    _UESState * __restrict psenc, size_t * __restrict nresult)
{
	char *s0;
	int ch, head, num, tail;
	wchar_t hi, wc;

	if (*s == NULL) {
		_citrus_UES_init_state(ei, psenc);
		*nresult = 0;
		return (0);
	}
	s0 = *s;

	hi = (wchar_t)0;
	tail = 0;

surrogate:
	wc = (wchar_t)0;
	head = tail;
	if (psenc->chlen == head) {
		if (n-- < 1)
			goto restart;
		psenc->ch[psenc->chlen++] = *s0++;
	}
	ch = (unsigned char)psenc->ch[head++];
	if (ch == ESCAPE) {
		if (psenc->chlen == head) {
			if (n-- < 1)
				goto restart;
			psenc->ch[psenc->chlen++] = *s0++;
		}
		switch (psenc->ch[head]) {
		case UCS2_ESC:
			tail += 6;
			break;
		case UCS4_ESC:
			if (ei->mode & MODE_C99) {
				tail = 10;
				break;
			}
		/*FALLTHROUGH*/
		default:
			tail = 0;
		}
		++head;
	}
	for (; head < tail; ++head) {
		if (psenc->chlen == head) {
			if (n-- < 1) {
restart:
				*s = s0;
				*nresult = (size_t)-2;
				return (0);
			}
			psenc->ch[psenc->chlen++] = *s0++;
		}
		num = to_int((int)(unsigned char)psenc->ch[head]);
		if (num < 0) {
			tail = 0;
			break;
		}
		wc = (wc << 4) | num;
	}
	head = 0;
	switch (tail) {
	case 0:
		break;
	case 6:
		if (hi != (wchar_t)0)
			break;
		if ((ei->mode & MODE_C99) == 0) {
			if (is_hi_surrogate(wc) != 0) {
				hi = wc;
				goto surrogate;
			}
			if ((uint32_t)wc <= 0x7F /* XXX */ ||
			    is_lo_surrogate(wc) != 0)
				break;
			goto done;
		}
	/*FALLTHROUGH*/
	case 10:
		if (is_basic(wc) == 0 && (uint32_t)wc <= UCS4_MAX &&
		    is_hi_surrogate(wc) == 0 && is_lo_surrogate(wc) == 0)
			goto done;
		*nresult = (size_t)-1;
		return (EILSEQ);
	case 12:
		if (is_lo_surrogate(wc) == 0)
			break;
		wc = surrogate_to_ucs(hi, wc);
		goto done;
	}
	ch = (unsigned char)psenc->ch[0];
	head = psenc->chlen;
	if (--head > 0)
		memmove(&psenc->ch[0], &psenc->ch[1], head);
	wc = (wchar_t)ch;
done:
	psenc->chlen = head;
	if (pwc != NULL)
		*pwc = wc;
	*nresult = (size_t)((wc == 0) ? 0 : (s0 - *s));
	*s = s0;

	return (0);
}

static int
_citrus_UES_wcrtomb_priv(_UESEncodingInfo * __restrict ei,
    char * __restrict s, size_t n, wchar_t wc,
    _UESState * __restrict psenc, size_t * __restrict nresult)
{
	wchar_t hi, lo;

	if (psenc->chlen != 0)
		return (EINVAL);

	if ((ei->mode & MODE_C99) ? is_basic(wc) : (uint32_t)wc <= 0x7F) {
		if (n-- < 1)
			goto e2big;
		psenc->ch[psenc->chlen++] = (char)wc;
	} else if ((uint32_t)wc <= BMP_MAX) {
		if (n < 6)
			goto e2big;
		psenc->chlen = to_str(&psenc->ch[0], wc, UCS2_BIT);
	} else if ((ei->mode & MODE_C99) == 0 && (uint32_t)wc <= UCS2_MAX) {
		if (n < 12)
			goto e2big;
		ucs_to_surrogate(wc, &hi, &lo);
		psenc->chlen += to_str(&psenc->ch[0], hi, UCS2_BIT);
		psenc->chlen += to_str(&psenc->ch[6], lo, UCS2_BIT);
	} else if ((ei->mode & MODE_C99) && (uint32_t)wc <= UCS4_MAX) {
		if (n < 10)
			goto e2big;
		psenc->chlen = to_str(&psenc->ch[0], wc, UCS4_BIT);
	} else {
		*nresult = (size_t)-1;
		return (EILSEQ);
	}
	memcpy(s, psenc->ch, psenc->chlen);
	*nresult = psenc->chlen;
	psenc->chlen = 0;

	return (0);

e2big:
	*nresult = (size_t)-1;
	return (E2BIG);
}

/*ARGSUSED*/
static int
_citrus_UES_stdenc_wctocs(_UESEncodingInfo * __restrict ei __unused,
    _csid_t * __restrict csid, _index_t * __restrict idx, wchar_t wc)
{

	*csid = 0;
	*idx = (_index_t)wc;

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_UES_stdenc_cstowc(_UESEncodingInfo * __restrict ei __unused,
    wchar_t * __restrict wc, _csid_t csid, _index_t idx)
{

	if (csid != 0)
		return (EILSEQ);
	*wc = (wchar_t)idx;

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_UES_stdenc_get_state_desc_generic(_UESEncodingInfo * __restrict ei __unused,
    _UESState * __restrict psenc, int * __restrict rstate)
{

	*rstate = (psenc->chlen == 0) ? _STDENC_SDGEN_INITIAL :
	    _STDENC_SDGEN_INCOMPLETE_CHAR;
	return (0);
}

static void
/*ARGSUSED*/
_citrus_UES_encoding_module_uninit(_UESEncodingInfo *ei __unused)
{

	/* ei seems to be unused */
}

static int
/*ARGSUSED*/
_citrus_UES_encoding_module_init(_UESEncodingInfo * __restrict ei,
    const void * __restrict var, size_t lenvar)
{
	const char *p;

	p = var;
	memset((void *)ei, 0, sizeof(*ei));
	while (lenvar > 0) {
		switch (_bcs_toupper(*p)) {
		case 'C':
			MATCH(C99, ei->mode |= MODE_C99);
			break;
		}
		++p;
		--lenvar;
	}
	ei->mb_cur_max = (ei->mode & MODE_C99) ? 10 : 12;

	return (0);
}

/* ----------------------------------------------------------------------
 * public interface for stdenc
 */

_CITRUS_STDENC_DECLS(UES);
_CITRUS_STDENC_DEF_OPS(UES);

#include "citrus_stdenc_template.h"
