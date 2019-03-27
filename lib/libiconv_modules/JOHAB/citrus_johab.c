/* $FreeBSD$ */
/* $NetBSD: citrus_johab.c,v 1.4 2008/06/14 16:01:07 tnozaki Exp $ */

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
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
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
#include "citrus_johab.h"

/* ----------------------------------------------------------------------
 * private stuffs used by templates
 */

typedef struct {
	int	 chlen;
	char	 ch[2];
} _JOHABState;

typedef struct {
	int	 dummy;
} _JOHABEncodingInfo;

#define _CEI_TO_EI(_cei_)		(&(_cei_)->ei)
#define _CEI_TO_STATE(_cei_, _func_)	(_cei_)->states.s_##_func_

#define _FUNCNAME(m)			_citrus_JOHAB_##m
#define _ENCODING_INFO			_JOHABEncodingInfo
#define _ENCODING_STATE			_JOHABState
#define _ENCODING_MB_CUR_MAX(_ei_)		2
#define _ENCODING_IS_STATE_DEPENDENT		0
#define _STATE_NEEDS_EXPLICIT_INIT(_ps_)	0


static __inline void
/*ARGSUSED*/
_citrus_JOHAB_init_state(_JOHABEncodingInfo * __restrict ei __unused,
    _JOHABState * __restrict psenc)
{

	psenc->chlen = 0;
}

#if 0
static __inline void
/*ARGSUSED*/
_citrus_JOHAB_pack_state(_JOHABEncodingInfo * __restrict ei __unused,
    void * __restrict pspriv, const _JOHABState * __restrict psenc)
{

	memcpy(pspriv, (const void *)psenc, sizeof(*psenc));
}

static __inline void
/*ARGSUSED*/
_citrus_JOHAB_unpack_state(_JOHABEncodingInfo * __restrict ei __unused,
    _JOHABState * __restrict psenc, const void * __restrict pspriv)
{

	memcpy((void *)psenc, pspriv, sizeof(*psenc));
}
#endif

static void
/*ARGSUSED*/
_citrus_JOHAB_encoding_module_uninit(_JOHABEncodingInfo *ei __unused)
{

	/* ei may be null */
}

static int
/*ARGSUSED*/
_citrus_JOHAB_encoding_module_init(_JOHABEncodingInfo * __restrict ei __unused,
    const void * __restrict var __unused, size_t lenvar __unused)
{

	/* ei may be null */
	return (0);
}

static __inline bool
ishangul(int l, int t)
{

	return ((l >= 0x84 && l <= 0xD3) &&
	    ((t >= 0x41 && t <= 0x7E) || (t >= 0x81 && t <= 0xFE)));
}

static __inline bool
isuda(int l, int t)
{

	return ((l == 0xD8) &&
	    ((t >= 0x31 && t <= 0x7E) || (t >= 0x91 && t <= 0xFE)));
}

static __inline bool
ishanja(int l, int t)
{

	return (((l >= 0xD9 && l <= 0xDE) || (l >= 0xE0 && l <= 0xF9)) &&
	    ((t >= 0x31 && t <= 0x7E) || (t >= 0x91 && t <= 0xFE)));
}

static int
/*ARGSUSED*/
_citrus_JOHAB_mbrtowc_priv(_JOHABEncodingInfo * __restrict ei,
    wchar_t * __restrict pwc, char ** __restrict s, size_t n,
    _JOHABState * __restrict psenc, size_t * __restrict nresult)
{
	char *s0;
	int l, t;

	if (*s == NULL) {
		_citrus_JOHAB_init_state(ei, psenc);
		*nresult = _ENCODING_IS_STATE_DEPENDENT;
		return (0);
	}
	s0 = *s;

	switch (psenc->chlen) {
	case 0:
		if (n-- < 1)
			goto restart;
		l = *s0++ & 0xFF;
		if (l <= 0x7F) {
			if (pwc != NULL)
				*pwc = (wchar_t)l;
			*nresult = (l == 0) ? 0 : 1;
			*s = s0;
			return (0);
		}
		psenc->ch[psenc->chlen++] = l;
		break;
	case 1:
		l = psenc->ch[0] & 0xFF;
		break;
	default:
		return (EINVAL);
	}
	if (n-- < 1) {
restart:
		*nresult = (size_t)-2;
		*s = s0;
		return (0);
	}
	t = *s0++ & 0xFF;
	if (!ishangul(l, t) && !isuda(l, t) && !ishanja(l, t)) {
		*nresult = (size_t)-1;
		return (EILSEQ);
	}
	if (pwc != NULL)
		*pwc = (wchar_t)(l << 8 | t);
	*nresult = s0 - *s;
	*s = s0;
	psenc->chlen = 0;

	return (0);
}

static int
/*ARGSUSED*/
_citrus_JOHAB_wcrtomb_priv(_JOHABEncodingInfo * __restrict ei __unused,
    char * __restrict s, size_t n, wchar_t wc,
    _JOHABState * __restrict psenc, size_t * __restrict nresult)
{
	int l, t;

	if (psenc->chlen != 0)
		return (EINVAL);

	/* XXX assume wchar_t as int */
	if ((uint32_t)wc <= 0x7F) {
		if (n < 1)
			goto e2big;
		*s = wc & 0xFF;
		*nresult = 1;
	} else if ((uint32_t)wc <= 0xFFFF) {
		if (n < 2) {
e2big:
			*nresult = (size_t)-1;
			return (E2BIG);
		}
		l = (wc >> 8) & 0xFF;
		t = wc & 0xFF;
		if (!ishangul(l, t) && !isuda(l, t) && !ishanja(l, t))
			goto ilseq;
		*s++ = l;
		*s = t;
		*nresult = 2;
	} else {
ilseq:
		*nresult = (size_t)-1;
		return (EILSEQ);
	}
	return (0);

}

static __inline int
/*ARGSUSED*/
_citrus_JOHAB_stdenc_wctocs(_JOHABEncodingInfo * __restrict ei __unused,
    _csid_t * __restrict csid, _index_t * __restrict idx, wchar_t wc)
{
	int m, l, linear, t;

	/* XXX assume wchar_t as int */
	if ((uint32_t)wc <= 0x7F) {
		*idx = (_index_t)wc;
		*csid = 0;
	} else if ((uint32_t)wc <= 0xFFFF) {
		l = (wc >> 8) & 0xFF;
		t = wc & 0xFF;
		if (ishangul(l, t) || isuda(l, t)) {
			*idx = (_index_t)wc;
			*csid = 1;
		} else {
			if (l >= 0xD9 && l <= 0xDE) {
				linear = l - 0xD9;
				m = 0x21;
			} else if (l >= 0xE0 && l <= 0xF9) {
				linear = l - 0xE0;
				m = 0x4A;
			} else
				return (EILSEQ);
			linear *= 188;
			if (t >= 0x31 && t <= 0x7E)
				linear += t - 0x31;
			else if (t >= 0x91 && t <= 0xFE)
				linear += t - 0x43;
			else
				return (EILSEQ);
			l = (linear / 94) + m;
			t = (linear % 94) + 0x21;
			*idx = (_index_t)((l << 8) | t);
			*csid = 2;
		}
	} else
		return (EILSEQ);
	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_JOHAB_stdenc_cstowc(_JOHABEncodingInfo * __restrict ei __unused,
    wchar_t * __restrict wc, _csid_t csid, _index_t idx)
{
	int m, n, l, linear, t;

	switch (csid) {
	case 0:
	case 1:
		*wc = (wchar_t)idx;
		break;
	case 2:
		if (idx >= 0x2121 && idx <= 0x2C71) {
			m = 0xD9;
			n = 0x21;
		} else if (idx >= 0x4A21 && idx <= 0x7D7E) {
			m = 0xE0;
			n = 0x4A;
		} else
			return (EILSEQ);
		l = ((idx >> 8) & 0xFF) - n;
		t = (idx & 0xFF) - 0x21;
		linear = (l * 94) + t;
		l = (linear / 188) + m;
		t = linear % 188;
		t += (t <= 0x4D) ? 0x31 : 0x43;
		break;
	default:
		return (EILSEQ);
	}
	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_JOHAB_stdenc_get_state_desc_generic(_JOHABEncodingInfo * __restrict ei __unused,
    _JOHABState * __restrict psenc, int * __restrict rstate)
{

	*rstate = (psenc->chlen == 0) ? _STDENC_SDGEN_INITIAL :
	    _STDENC_SDGEN_INCOMPLETE_CHAR;
	return (0);
}

/* ----------------------------------------------------------------------
 * public interface for stdenc
 */

_CITRUS_STDENC_DECLS(JOHAB);
_CITRUS_STDENC_DEF_OPS(JOHAB);

#include "citrus_stdenc_template.h"
