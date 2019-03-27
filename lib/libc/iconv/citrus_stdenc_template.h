/* $FreeBSD$ */
/* $NetBSD: citrus_stdenc_template.h,v 1.4 2008/02/09 14:56:20 junyoung Exp $ */

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

#include <iconv.h>

/*
 * CAUTION: THIS IS NOT STANDALONE FILE
 *
 * function templates of iconv standard encoding handler for each encodings.
 *
 */

/*
 * macros
 */

#undef _TO_EI
#undef _CE_TO_EI
#undef _TO_STATE
#define _TO_EI(_cl_)	((_ENCODING_INFO*)(_cl_))
#define _CE_TO_EI(_ce_)	(_TO_EI((_ce_)->ce_closure))
#define _TO_STATE(_ps_)	((_ENCODING_STATE*)(_ps_))

/* ----------------------------------------------------------------------
 * templates for public functions
 */

int
_FUNCNAME(stdenc_getops)(struct _citrus_stdenc_ops *ops,
    size_t lenops __unused)
{

	memcpy(ops, &_FUNCNAME(stdenc_ops), sizeof(_FUNCNAME(stdenc_ops)));

	return (0);
}

static int
_FUNCNAME(stdenc_init)(struct _citrus_stdenc * __restrict ce,
    const void * __restrict var, size_t lenvar,
    struct _citrus_stdenc_traits * __restrict et)
{
	_ENCODING_INFO *ei;
	int ret;

	ei = NULL;
	if (sizeof(_ENCODING_INFO) > 0) {
		ei = calloc(1, sizeof(_ENCODING_INFO));
		if (ei == NULL)
			return (errno);
	}

	ret = _FUNCNAME(encoding_module_init)(ei, var, lenvar);
	if (ret) {
		free((void *)ei);
		return (ret);
	}

	ce->ce_closure = ei;
	et->et_state_size = sizeof(_ENCODING_STATE);
	et->et_mb_cur_max = _ENCODING_MB_CUR_MAX(_CE_TO_EI(ce));

	return (0);
}

static void
_FUNCNAME(stdenc_uninit)(struct _citrus_stdenc * __restrict ce)
{

	if (ce) {
		_FUNCNAME(encoding_module_uninit)(_CE_TO_EI(ce));
		free(ce->ce_closure);
	}
}

static int
_FUNCNAME(stdenc_init_state)(struct _citrus_stdenc * __restrict ce,
    void * __restrict ps)
{

	_FUNCNAME(init_state)(_CE_TO_EI(ce), _TO_STATE(ps));

	return (0);
}

static int
_FUNCNAME(stdenc_mbtocs)(struct _citrus_stdenc * __restrict ce,
    _citrus_csid_t * __restrict csid, _citrus_index_t * __restrict idx,
    char ** __restrict s, size_t n, void * __restrict ps,
    size_t * __restrict nresult, struct iconv_hooks *hooks)
{
	wchar_t wc;
	int ret;

	ret = _FUNCNAME(mbrtowc_priv)(_CE_TO_EI(ce), &wc, s, n,
	    _TO_STATE(ps), nresult);

	if ((ret == 0) && *nresult != (size_t)-2)
		ret = _FUNCNAME(stdenc_wctocs)(_CE_TO_EI(ce), csid, idx, wc);

	if ((ret == 0) && (hooks != NULL) && (hooks->uc_hook != NULL))
		hooks->uc_hook((unsigned int)*idx, hooks->data);
	return (ret);
}

static int
_FUNCNAME(stdenc_cstomb)(struct _citrus_stdenc * __restrict ce,
    char * __restrict s, size_t n, _citrus_csid_t csid, _citrus_index_t idx,
    void * __restrict ps, size_t * __restrict nresult,
    struct iconv_hooks *hooks __unused)
{
	wchar_t wc;
	int ret;

	wc = ret = 0;

	if (csid != _CITRUS_CSID_INVALID)
		ret = _FUNCNAME(stdenc_cstowc)(_CE_TO_EI(ce), &wc, csid, idx);

	if (ret == 0)
		ret = _FUNCNAME(wcrtomb_priv)(_CE_TO_EI(ce), s, n, wc,
		    _TO_STATE(ps), nresult);
	return (ret);
}

static int
_FUNCNAME(stdenc_mbtowc)(struct _citrus_stdenc * __restrict ce,
    _citrus_wc_t * __restrict wc, char ** __restrict s, size_t n,
    void * __restrict ps, size_t * __restrict nresult,
    struct iconv_hooks *hooks)
{
	int ret;

	ret = _FUNCNAME(mbrtowc_priv)(_CE_TO_EI(ce), wc, s, n,
	    _TO_STATE(ps), nresult);
	if ((ret == 0) && (hooks != NULL) && (hooks->wc_hook != NULL))
		hooks->wc_hook(*wc, hooks->data);
	return (ret);
}

static int
_FUNCNAME(stdenc_wctomb)(struct _citrus_stdenc * __restrict ce,
    char * __restrict s, size_t n, _citrus_wc_t wc, void * __restrict ps,
    size_t * __restrict nresult, struct iconv_hooks *hooks __unused)
{
	int ret;

	ret = _FUNCNAME(wcrtomb_priv)(_CE_TO_EI(ce), s, n, wc, _TO_STATE(ps),
	    nresult);
	return (ret);
}

static int
_FUNCNAME(stdenc_put_state_reset)(struct _citrus_stdenc * __restrict ce __unused,
    char * __restrict s __unused, size_t n __unused,
    void * __restrict ps __unused, size_t * __restrict nresult)
{

#if _ENCODING_IS_STATE_DEPENDENT
	return ((_FUNCNAME(put_state_reset)(_CE_TO_EI(ce), s, n, _TO_STATE(ps),
	    nresult)));
#else
	*nresult = 0;
	return (0);
#endif
}

static int
_FUNCNAME(stdenc_get_state_desc)(struct _citrus_stdenc * __restrict ce,
    void * __restrict ps, int id,
    struct _citrus_stdenc_state_desc * __restrict d)
{
	int ret;

	switch (id) {
	case _STDENC_SDID_GENERIC:
		ret = _FUNCNAME(stdenc_get_state_desc_generic)(
		    _CE_TO_EI(ce), _TO_STATE(ps), &d->u.generic.state);
		break;
	default:
		ret = EOPNOTSUPP;
	}

	return (ret);
}
