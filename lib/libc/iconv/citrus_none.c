/* $FreeBSD$ */
/* $NetBSD: citrus_none.c,v 1.18 2008/06/14 16:01:07 tnozaki Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2002 Citrus Project,
 * Copyright (c) 2010 Gabor Kovesdan <gabor@FreeBSD.org>,
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
#include <iconv.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_module.h"
#include "citrus_none.h"
#include "citrus_stdenc.h"

_CITRUS_STDENC_DECLS(NONE);
_CITRUS_STDENC_DEF_OPS(NONE);
struct _citrus_stdenc_traits _citrus_NONE_stdenc_traits = {
	0,	/* et_state_size */
	1,	/* mb_cur_max */
};

static int
_citrus_NONE_stdenc_init(struct _citrus_stdenc * __restrict ce,
    const void *var __unused, size_t lenvar __unused,
    struct _citrus_stdenc_traits * __restrict et)
{

	et->et_state_size = 0;
	et->et_mb_cur_max = 1;

	ce->ce_closure = NULL;

	return (0);
}

static void
_citrus_NONE_stdenc_uninit(struct _citrus_stdenc *ce __unused)
{

}

static int
_citrus_NONE_stdenc_init_state(struct _citrus_stdenc * __restrict ce __unused,
    void * __restrict ps __unused)
{

	return (0);
}

static int
_citrus_NONE_stdenc_mbtocs(struct _citrus_stdenc * __restrict ce __unused,
    _csid_t *csid, _index_t *idx, char **s, size_t n,
    void *ps __unused, size_t *nresult, struct iconv_hooks *hooks)
{

	if (n < 1) {
		*nresult = (size_t)-2;
		return (0);
	}

	*csid = 0;
	*idx = (_index_t)(unsigned char)*(*s)++;
	*nresult = *idx == 0 ? 0 : 1;

	if ((hooks != NULL) && (hooks->uc_hook != NULL))
		hooks->uc_hook((unsigned int)*idx, hooks->data);

	return (0);
}

static int
_citrus_NONE_stdenc_cstomb(struct _citrus_stdenc * __restrict ce __unused,
    char *s, size_t n, _csid_t csid, _index_t idx, void *ps __unused,
    size_t *nresult, struct iconv_hooks *hooks __unused)
{

	if (csid == _CITRUS_CSID_INVALID) {
		*nresult = 0;
		return (0);
	}
	if (csid != 0)
		return (EILSEQ);

	if ((idx & 0x000000FF) == idx) {
		if (n < 1) {
			*nresult = (size_t)-1;
			return (E2BIG);
		}
		*s = (char)idx;
		*nresult = 1;
	} else if ((idx & 0x0000FFFF) == idx) {
		if (n < 2) {
			*nresult = (size_t)-1;
			return (E2BIG);
		}
		s[0] = (char)idx;
		/* XXX: might be endian dependent */
		s[1] = (char)(idx >> 8);
		*nresult = 2;
	} else if ((idx & 0x00FFFFFF) == idx) {
		if (n < 3) {
			*nresult = (size_t)-1;
			return (E2BIG);
		}
		s[0] = (char)idx;
		/* XXX: might be endian dependent */
		s[1] = (char)(idx >> 8);
		s[2] = (char)(idx >> 16);
		*nresult = 3;
	} else {
		if (n < 3) {
			*nresult = (size_t)-1;
			return (E2BIG);
		}
		s[0] = (char)idx;
		/* XXX: might be endian dependent */
		s[1] = (char)(idx >> 8);
		s[2] = (char)(idx >> 16);
		s[3] = (char)(idx >> 24);
		*nresult = 4;
	}
		
	return (0);
}

static int
_citrus_NONE_stdenc_mbtowc(struct _citrus_stdenc * __restrict ce __unused,
    _wc_t * __restrict pwc, char ** __restrict s, size_t n,
    void * __restrict pspriv __unused, size_t * __restrict nresult,
    struct iconv_hooks *hooks)
{

	if (*s == NULL) {
		*nresult = 0;
		return (0);
	}
	if (n == 0) {
		*nresult = (size_t)-2;
		return (0);
	}

	if (pwc != NULL)
		*pwc = (_wc_t)(unsigned char) **s;

	*nresult = **s == '\0' ? 0 : 1;

	if ((hooks != NULL) && (hooks->wc_hook != NULL))
		hooks->wc_hook(*pwc, hooks->data);

	return (0);
}

static int
_citrus_NONE_stdenc_wctomb(struct _citrus_stdenc * __restrict ce __unused,
    char * __restrict s, size_t n, _wc_t wc,
    void * __restrict pspriv __unused, size_t * __restrict nresult,
    struct iconv_hooks *hooks __unused)
{

	if ((wc & ~0xFFU) != 0) {
		*nresult = (size_t)-1;
		return (EILSEQ);
	}
	if (n == 0) {
		*nresult = (size_t)-1;
		return (E2BIG);
	}

	*nresult = 1;
	if (s != NULL && n > 0)
		*s = (char)wc;

	return (0);
}

static int
_citrus_NONE_stdenc_put_state_reset(struct _citrus_stdenc * __restrict ce __unused,
    char * __restrict s __unused, size_t n __unused,
    void * __restrict pspriv __unused, size_t * __restrict nresult)
{

	*nresult = 0;

	return (0);
}

static int
_citrus_NONE_stdenc_get_state_desc(struct _stdenc * __restrict ce __unused,
    void * __restrict ps __unused, int id,
    struct _stdenc_state_desc * __restrict d)
{
	int ret = 0;

	switch (id) {
	case _STDENC_SDID_GENERIC:
		d->u.generic.state = _STDENC_SDGEN_INITIAL;
		break;
	default:
		ret = EOPNOTSUPP;
	}

	return (ret);
}
