/* $FreeBSD$ */
/*	$NetBSD: citrus_iconv_std.c,v 1.16 2012/02/12 13:51:29 wiz Exp $	*/

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
#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_module.h"
#include "citrus_region.h"
#include "citrus_mmap.h"
#include "citrus_hash.h"
#include "citrus_iconv.h"
#include "citrus_stdenc.h"
#include "citrus_mapper.h"
#include "citrus_csmapper.h"
#include "citrus_memstream.h"
#include "citrus_iconv_std.h"
#include "citrus_esdb.h"

/* ---------------------------------------------------------------------- */

_CITRUS_ICONV_DECLS(iconv_std);
_CITRUS_ICONV_DEF_OPS(iconv_std);


/* ---------------------------------------------------------------------- */

int
_citrus_iconv_std_iconv_getops(struct _citrus_iconv_ops *ops)
{

	memcpy(ops, &_citrus_iconv_std_iconv_ops,
	    sizeof(_citrus_iconv_std_iconv_ops));

	return (0);
}

/* ---------------------------------------------------------------------- */

/*
 * convenience routines for stdenc.
 */
static __inline void
save_encoding_state(struct _citrus_iconv_std_encoding *se)
{

	if (se->se_ps)
		memcpy(se->se_pssaved, se->se_ps,
		    _stdenc_get_state_size(se->se_handle));
}

static __inline void
restore_encoding_state(struct _citrus_iconv_std_encoding *se)
{

	if (se->se_ps)
		memcpy(se->se_ps, se->se_pssaved,
		    _stdenc_get_state_size(se->se_handle));
}

static __inline void
init_encoding_state(struct _citrus_iconv_std_encoding *se)
{

	if (se->se_ps)
		_stdenc_init_state(se->se_handle, se->se_ps);
}

static __inline int
mbtocsx(struct _citrus_iconv_std_encoding *se,
    _csid_t *csid, _index_t *idx, char **s, size_t n, size_t *nresult,
    struct iconv_hooks *hooks)
{

	return (_stdenc_mbtocs(se->se_handle, csid, idx, s, n, se->se_ps,
			      nresult, hooks));
}

static __inline int
cstombx(struct _citrus_iconv_std_encoding *se,
    char *s, size_t n, _csid_t csid, _index_t idx, size_t *nresult,
    struct iconv_hooks *hooks)
{

	return (_stdenc_cstomb(se->se_handle, s, n, csid, idx, se->se_ps,
			      nresult, hooks));
}

static __inline int
wctombx(struct _citrus_iconv_std_encoding *se,
    char *s, size_t n, _wc_t wc, size_t *nresult,
    struct iconv_hooks *hooks)
{

	return (_stdenc_wctomb(se->se_handle, s, n, wc, se->se_ps, nresult,
			     hooks));
}

static __inline int
put_state_resetx(struct _citrus_iconv_std_encoding *se, char *s, size_t n,
    size_t *nresult)
{

	return (_stdenc_put_state_reset(se->se_handle, s, n, se->se_ps, nresult));
}

static __inline int
get_state_desc_gen(struct _citrus_iconv_std_encoding *se, int *rstate)
{
	struct _stdenc_state_desc ssd;
	int ret;

	ret = _stdenc_get_state_desc(se->se_handle, se->se_ps,
	    _STDENC_SDID_GENERIC, &ssd);
	if (!ret)
		*rstate = ssd.u.generic.state;

	return (ret);
}

/*
 * init encoding context
 */
static int
init_encoding(struct _citrus_iconv_std_encoding *se, struct _stdenc *cs,
    void *ps1, void *ps2)
{
	int ret = -1;

	se->se_handle = cs;
	se->se_ps = ps1;
	se->se_pssaved = ps2;

	if (se->se_ps)
		ret = _stdenc_init_state(cs, se->se_ps);
	if (!ret && se->se_pssaved)
		ret = _stdenc_init_state(cs, se->se_pssaved);

	return (ret);
}

static int
open_csmapper(struct _csmapper **rcm, const char *src, const char *dst,
    unsigned long *rnorm)
{
	struct _csmapper *cm;
	int ret;

	ret = _csmapper_open(&cm, src, dst, 0, rnorm);
	if (ret)
		return (ret);
	if (_csmapper_get_src_max(cm) != 1 || _csmapper_get_dst_max(cm) != 1 ||
	    _csmapper_get_state_size(cm) != 0) {
		_csmapper_close(cm);
		return (EINVAL);
	}

	*rcm = cm;

	return (0);
}

static void
close_dsts(struct _citrus_iconv_std_dst_list *dl)
{
	struct _citrus_iconv_std_dst *sd;

	while ((sd = TAILQ_FIRST(dl)) != NULL) {
		TAILQ_REMOVE(dl, sd, sd_entry);
		_csmapper_close(sd->sd_mapper);
		free(sd);
	}
}

static int
open_dsts(struct _citrus_iconv_std_dst_list *dl,
    const struct _esdb_charset *ec, const struct _esdb *dbdst)
{
	struct _citrus_iconv_std_dst *sd, *sdtmp;
	unsigned long norm;
	int i, ret;

	sd = malloc(sizeof(*sd));
	if (sd == NULL)
		return (errno);

	for (i = 0; i < dbdst->db_num_charsets; i++) {
		ret = open_csmapper(&sd->sd_mapper, ec->ec_csname,
		    dbdst->db_charsets[i].ec_csname, &norm);
		if (ret == 0) {
			sd->sd_csid = dbdst->db_charsets[i].ec_csid;
			sd->sd_norm = norm;
			/* insert this mapper by sorted order. */
			TAILQ_FOREACH(sdtmp, dl, sd_entry) {
				if (sdtmp->sd_norm > norm) {
					TAILQ_INSERT_BEFORE(sdtmp, sd,
					    sd_entry);
					sd = NULL;
					break;
				}
			}
			if (sd)
				TAILQ_INSERT_TAIL(dl, sd, sd_entry);
			sd = malloc(sizeof(*sd));
			if (sd == NULL) {
				ret = errno;
				close_dsts(dl);
				return (ret);
			}
		} else if (ret != ENOENT) {
			close_dsts(dl);
			free(sd);
			return (ret);
		}
	}
	free(sd);
	return (0);
}

static void
close_srcs(struct _citrus_iconv_std_src_list *sl)
{
	struct _citrus_iconv_std_src *ss;

	while ((ss = TAILQ_FIRST(sl)) != NULL) {
		TAILQ_REMOVE(sl, ss, ss_entry);
		close_dsts(&ss->ss_dsts);
		free(ss);
	}
}

static int
open_srcs(struct _citrus_iconv_std_src_list *sl,
    const struct _esdb *dbsrc, const struct _esdb *dbdst)
{
	struct _citrus_iconv_std_src *ss;
	int count = 0, i, ret;

	ss = malloc(sizeof(*ss));
	if (ss == NULL)
		return (errno);

	TAILQ_INIT(&ss->ss_dsts);

	for (i = 0; i < dbsrc->db_num_charsets; i++) {
		ret = open_dsts(&ss->ss_dsts, &dbsrc->db_charsets[i], dbdst);
		if (ret)
			goto err;
		if (!TAILQ_EMPTY(&ss->ss_dsts)) {
			ss->ss_csid = dbsrc->db_charsets[i].ec_csid;
			TAILQ_INSERT_TAIL(sl, ss, ss_entry);
			ss = malloc(sizeof(*ss));
			if (ss == NULL) {
				ret = errno;
				goto err;
			}
			count++;
			TAILQ_INIT(&ss->ss_dsts);
		}
	}
	free(ss);

	return (count ? 0 : ENOENT);

err:
	free(ss);
	close_srcs(sl);
	return (ret);
}

/* do convert a character */
#define E_NO_CORRESPONDING_CHAR ENOENT /* XXX */
static int
/*ARGSUSED*/
do_conv(const struct _citrus_iconv_std_shared *is,
	_csid_t *csid, _index_t *idx)
{
	struct _citrus_iconv_std_dst *sd;
	struct _citrus_iconv_std_src *ss;
	_index_t tmpidx;
	int ret;

	TAILQ_FOREACH(ss, &is->is_srcs, ss_entry) {
		if (ss->ss_csid == *csid) {
			TAILQ_FOREACH(sd, &ss->ss_dsts, sd_entry) {
				ret = _csmapper_convert(sd->sd_mapper,
				    &tmpidx, *idx, NULL);
				switch (ret) {
				case _MAPPER_CONVERT_SUCCESS:
					*csid = sd->sd_csid;
					*idx = tmpidx;
					return (0);
				case _MAPPER_CONVERT_NONIDENTICAL:
					break;
				case _MAPPER_CONVERT_SRC_MORE:
					/*FALLTHROUGH*/
				case _MAPPER_CONVERT_DST_MORE:
					/*FALLTHROUGH*/
				case _MAPPER_CONVERT_ILSEQ:
					return (EILSEQ);
				case _MAPPER_CONVERT_FATAL:
					return (EINVAL);
				}
			}
			break;
		}
	}

	return (E_NO_CORRESPONDING_CHAR);
}
/* ---------------------------------------------------------------------- */

static int
/*ARGSUSED*/
_citrus_iconv_std_iconv_init_shared(struct _citrus_iconv_shared *ci,
    const char * __restrict src, const char * __restrict dst)
{
	struct _citrus_esdb esdbdst, esdbsrc;
	struct _citrus_iconv_std_shared *is;
	int ret;

	is = malloc(sizeof(*is));
	if (is == NULL) {
		ret = errno;
		goto err0;
	}
	ret = _citrus_esdb_open(&esdbsrc, src);
	if (ret)
		goto err1;
	ret = _citrus_esdb_open(&esdbdst, dst);
	if (ret)
		goto err2;
	ret = _stdenc_open(&is->is_src_encoding, esdbsrc.db_encname,
	    esdbsrc.db_variable, esdbsrc.db_len_variable);
	if (ret)
		goto err3;
	ret = _stdenc_open(&is->is_dst_encoding, esdbdst.db_encname,
	    esdbdst.db_variable, esdbdst.db_len_variable);
	if (ret)
		goto err4;
	is->is_use_invalid = esdbdst.db_use_invalid;
	is->is_invalid = esdbdst.db_invalid;

	TAILQ_INIT(&is->is_srcs);
	ret = open_srcs(&is->is_srcs, &esdbsrc, &esdbdst);
	if (ret)
		goto err5;

	_esdb_close(&esdbsrc);
	_esdb_close(&esdbdst);
	ci->ci_closure = is;

	return (0);

err5:
	_stdenc_close(is->is_dst_encoding);
err4:
	_stdenc_close(is->is_src_encoding);
err3:
	_esdb_close(&esdbdst);
err2:
	_esdb_close(&esdbsrc);
err1:
	free(is);
err0:
	return (ret);
}

static void
_citrus_iconv_std_iconv_uninit_shared(struct _citrus_iconv_shared *ci)
{
	struct _citrus_iconv_std_shared *is = ci->ci_closure;

	if (is == NULL)
		return;

	_stdenc_close(is->is_src_encoding);
	_stdenc_close(is->is_dst_encoding);
	close_srcs(&is->is_srcs);
	free(is);
}

static int
_citrus_iconv_std_iconv_init_context(struct _citrus_iconv *cv)
{
	const struct _citrus_iconv_std_shared *is = cv->cv_shared->ci_closure;
	struct _citrus_iconv_std_context *sc;
	char *ptr;
	size_t sz, szpsdst, szpssrc;

	szpssrc = _stdenc_get_state_size(is->is_src_encoding);
	szpsdst = _stdenc_get_state_size(is->is_dst_encoding);

	sz = (szpssrc + szpsdst)*2 + sizeof(struct _citrus_iconv_std_context);
	sc = malloc(sz);
	if (sc == NULL)
		return (errno);

	ptr = (char *)&sc[1];
	if (szpssrc > 0)
		init_encoding(&sc->sc_src_encoding, is->is_src_encoding,
		    ptr, ptr+szpssrc);
	else
		init_encoding(&sc->sc_src_encoding, is->is_src_encoding,
		    NULL, NULL);
	ptr += szpssrc*2;
	if (szpsdst > 0)
		init_encoding(&sc->sc_dst_encoding, is->is_dst_encoding,
		    ptr, ptr+szpsdst);
	else
		init_encoding(&sc->sc_dst_encoding, is->is_dst_encoding,
		    NULL, NULL);

	cv->cv_closure = (void *)sc;

	return (0);
}

static void
_citrus_iconv_std_iconv_uninit_context(struct _citrus_iconv *cv)
{

	free(cv->cv_closure);
}

static int
_citrus_iconv_std_iconv_convert(struct _citrus_iconv * __restrict cv,
    char * __restrict * __restrict in, size_t * __restrict inbytes,
    char * __restrict * __restrict out, size_t * __restrict outbytes,
    uint32_t flags, size_t * __restrict invalids)
{
	const struct _citrus_iconv_std_shared *is = cv->cv_shared->ci_closure;
	struct _citrus_iconv_std_context *sc = cv->cv_closure;
	_csid_t csid;
	_index_t idx;
	char *tmpin;
	size_t inval, szrin, szrout;
	int ret, state = 0;

	inval = 0;
	if (in == NULL || *in == NULL) {
		/* special cases */
		if (out != NULL && *out != NULL) {
			/* init output state and store the shift sequence */
			save_encoding_state(&sc->sc_src_encoding);
			save_encoding_state(&sc->sc_dst_encoding);
			szrout = 0;

			ret = put_state_resetx(&sc->sc_dst_encoding,
			    *out, *outbytes, &szrout);
			if (ret)
				goto err;

			if (szrout == (size_t)-2) {
				/* too small to store the character */
				ret = EINVAL;
				goto err;
			}
			*out += szrout;
			*outbytes -= szrout;
		} else
			/* otherwise, discard the shift sequence */
			init_encoding_state(&sc->sc_dst_encoding);
		init_encoding_state(&sc->sc_src_encoding);
		*invalids = 0;
		return (0);
	}

	/* normal case */
	for (;;) {
		if (*inbytes == 0) {
			ret = get_state_desc_gen(&sc->sc_src_encoding, &state);
			if (state == _STDENC_SDGEN_INITIAL ||
			    state == _STDENC_SDGEN_STABLE)
				break;
		}

		/* save the encoding states for the error recovery */
		save_encoding_state(&sc->sc_src_encoding);
		save_encoding_state(&sc->sc_dst_encoding);

		/* mb -> csid/index */
		tmpin = *in;
		szrin = szrout = 0;
		ret = mbtocsx(&sc->sc_src_encoding, &csid, &idx, &tmpin,
		    *inbytes, &szrin, cv->cv_shared->ci_hooks);
		if (ret)
			goto err;

		if (szrin == (size_t)-2) {
			/* incompleted character */
			ret = get_state_desc_gen(&sc->sc_src_encoding, &state);
			if (ret) {
				ret = EINVAL;
				goto err;
			}
			switch (state) {
			case _STDENC_SDGEN_INITIAL:
			case _STDENC_SDGEN_STABLE:
				/* fetch shift sequences only. */
				goto next;
			}
			ret = EINVAL;
			goto err;
		}
		/* convert the character */
		ret = do_conv(is, &csid, &idx);
		if (ret) {
			if (ret == E_NO_CORRESPONDING_CHAR) {
				/*
				 * GNU iconv returns EILSEQ when no
				 * corresponding character in the output.
				 * Some software depends on this behavior
				 * though this is against POSIX specification.
				 */
				if (cv->cv_shared->ci_ilseq_invalid != 0) {
					ret = EILSEQ;
					goto err;
				}
				inval++;
				szrout = 0;
				if ((((flags & _CITRUS_ICONV_F_HIDE_INVALID) == 0) &&
				    !cv->cv_shared->ci_discard_ilseq) &&
				    is->is_use_invalid) {
					ret = wctombx(&sc->sc_dst_encoding,
					    *out, *outbytes, is->is_invalid,
					    &szrout, cv->cv_shared->ci_hooks);
					if (ret)
						goto err;
				}
				goto next;
			} else
				goto err;
		}
		/* csid/index -> mb */
		ret = cstombx(&sc->sc_dst_encoding,
		    *out, *outbytes, csid, idx, &szrout,
		    cv->cv_shared->ci_hooks);
		if (ret)
			goto err;
next:
		*inbytes -= tmpin-*in; /* szrin is insufficient on \0. */
		*in = tmpin;
		*outbytes -= szrout;
		*out += szrout;
	}
	*invalids = inval;

	return (0);

err:
	restore_encoding_state(&sc->sc_src_encoding);
	restore_encoding_state(&sc->sc_dst_encoding);
	*invalids = inval;

	return (ret);
}
