/* $FreeBSD$ */
/*	$NetBSD: citrus_mapper_std.c,v 1.11 2018/06/11 18:03:38 kamil Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003, 2006 Citrus Project,
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_region.h"
#include "citrus_mmap.h"
#include "citrus_module.h"
#include "citrus_hash.h"
#include "citrus_mapper.h"
#include "citrus_db.h"
#include "citrus_db_hash.h"

#include "citrus_mapper_std.h"
#include "citrus_mapper_std_file.h"

/* ---------------------------------------------------------------------- */

_CITRUS_MAPPER_DECLS(mapper_std);
_CITRUS_MAPPER_DEF_OPS(mapper_std);


/* ---------------------------------------------------------------------- */

int
_citrus_mapper_std_mapper_getops(struct _citrus_mapper_ops *ops)
{

	memcpy(ops, &_citrus_mapper_std_mapper_ops,
	    sizeof(_citrus_mapper_std_mapper_ops));

	return (0);
}

/* ---------------------------------------------------------------------- */

static int
/*ARGSUSED*/
rowcol_convert(struct _citrus_mapper_std * __restrict ms,
    _index_t * __restrict dst, _index_t src, void * __restrict ps __unused)
{
	struct _citrus_mapper_std_linear_zone *lz;
	struct _citrus_mapper_std_rowcol *rc;
	_index_t idx = 0, n;
	size_t i;
	uint32_t conv;

	/* ps may be unused */
	rc = &ms->ms_rowcol;

	for (i = rc->rc_src_rowcol_len * rc->rc_src_rowcol_bits,
	    lz = &rc->rc_src_rowcol[0]; i > 0; ++lz) {
		i -= rc->rc_src_rowcol_bits;
		n = (src >> i) & rc->rc_src_rowcol_mask;
		if (n < lz->begin || n > lz->end) {
			switch (rc->rc_oob_mode) {
			case _CITRUS_MAPPER_STD_OOB_NONIDENTICAL:
				*dst = rc->rc_dst_invalid;
				return (_MAPPER_CONVERT_NONIDENTICAL);
			case _CITRUS_MAPPER_STD_OOB_ILSEQ:
				return (_MAPPER_CONVERT_ILSEQ);
			default:
				return (_MAPPER_CONVERT_FATAL);
			}
		}
		idx = idx * lz->width + n - lz->begin;
	}
	switch (rc->rc_dst_unit_bits) {
	case 8:
		conv = _region_peek8(&rc->rc_table, idx);
		break;
	case 16:
		conv = be16toh(_region_peek16(&rc->rc_table, idx*2));
		break;
	case 32:
		conv = be32toh(_region_peek32(&rc->rc_table, idx*4));
		break;
	default:
		return (_MAPPER_CONVERT_FATAL);
	}

	if (conv == rc->rc_dst_invalid) {
		*dst = rc->rc_dst_invalid;
		return (_MAPPER_CONVERT_NONIDENTICAL);
	}
	if (conv == rc->rc_dst_ilseq)
		return (_MAPPER_CONVERT_ILSEQ);

	*dst = conv;

	return (_MAPPER_CONVERT_SUCCESS);
}

static __inline int
set_linear_zone(struct _citrus_mapper_std_linear_zone *lz,
    uint32_t begin, uint32_t end)
{

	if (begin > end)
		return (EFTYPE);

	lz->begin = begin;
	lz->end = end;
	lz->width= end - begin + 1;

	return (0);
}

static __inline int
rowcol_parse_variable_compat(struct _citrus_mapper_std_rowcol *rc,
    struct _region *r)
{
	const struct _citrus_mapper_std_rowcol_info_compat_x *rcx;
	struct _citrus_mapper_std_linear_zone *lz;
	uint32_t m, n;
	int ret;

	rcx = _region_head(r);

	rc->rc_dst_invalid = be32toh(rcx->rcx_dst_invalid);
	rc->rc_dst_unit_bits = be32toh(rcx->rcx_dst_unit_bits);
	m = be32toh(rcx->rcx_src_col_bits);
	n = 1U << (m - 1);
	n |= n - 1;
	rc->rc_src_rowcol_bits = m;
	rc->rc_src_rowcol_mask = n;

	rc->rc_src_rowcol = malloc(2 *
	    sizeof(*rc->rc_src_rowcol));
	if (rc->rc_src_rowcol == NULL)
		return (ENOMEM);
	lz = rc->rc_src_rowcol;
	rc->rc_src_rowcol_len = 1;
	m = be32toh(rcx->rcx_src_row_begin);
	n = be32toh(rcx->rcx_src_row_end);
	if (m + n > 0) {
		ret = set_linear_zone(lz, m, n);
		if (ret != 0) {
			free(rc->rc_src_rowcol);
			rc->rc_src_rowcol = NULL;
			return (ret);
		}
		++rc->rc_src_rowcol_len, ++lz;
	}
	m = be32toh(rcx->rcx_src_col_begin);
	n = be32toh(rcx->rcx_src_col_end);

	return (set_linear_zone(lz, m, n));
}

static __inline int
rowcol_parse_variable(struct _citrus_mapper_std_rowcol *rc,
    struct _region *r)
{
	const struct _citrus_mapper_std_rowcol_info_x *rcx;
	struct _citrus_mapper_std_linear_zone *lz;
	size_t i;
	uint32_t m, n;
	int ret;

	rcx = _region_head(r);

	rc->rc_dst_invalid = be32toh(rcx->rcx_dst_invalid);
	rc->rc_dst_unit_bits = be32toh(rcx->rcx_dst_unit_bits);

	m = be32toh(rcx->rcx_src_rowcol_bits);
	n = 1 << (m - 1);
	n |= n - 1;
	rc->rc_src_rowcol_bits = m;
	rc->rc_src_rowcol_mask = n;

	rc->rc_src_rowcol_len = be32toh(rcx->rcx_src_rowcol_len);
	if (rc->rc_src_rowcol_len > _CITRUS_MAPPER_STD_ROWCOL_MAX)
		return (EFTYPE);
	rc->rc_src_rowcol = malloc(rc->rc_src_rowcol_len *
	    sizeof(*rc->rc_src_rowcol));
	if (rc->rc_src_rowcol == NULL)
		return (ENOMEM);
	for (i = 0, lz = rc->rc_src_rowcol;
	    i < rc->rc_src_rowcol_len; ++i, ++lz) {
		m = be32toh(rcx->rcx_src_rowcol[i].begin),
		n = be32toh(rcx->rcx_src_rowcol[i].end);
		ret = set_linear_zone(lz, m, n);
		if (ret != 0) {
			free(rc->rc_src_rowcol);
			rc->rc_src_rowcol = NULL;
			return (ret);
		}
	}
	return (0);
}

static void
rowcol_uninit(struct _citrus_mapper_std *ms)
{
	struct _citrus_mapper_std_rowcol *rc;

	rc = &ms->ms_rowcol;
	free(rc->rc_src_rowcol);
}

static int
rowcol_init(struct _citrus_mapper_std *ms)
{
	struct _citrus_mapper_std_linear_zone *lz;
	struct _citrus_mapper_std_rowcol *rc;
	const struct _citrus_mapper_std_rowcol_ext_ilseq_info_x *eix;
	struct _region r;
	uint64_t table_size;
	size_t i;
	int ret;

	ms->ms_convert = &rowcol_convert;
	ms->ms_uninit = &rowcol_uninit;
	rc = &ms->ms_rowcol;

	/* get table region */
	ret = _db_lookup_by_s(ms->ms_db, _CITRUS_MAPPER_STD_SYM_TABLE,
	    &rc->rc_table, NULL);
	if (ret) {
		if (ret == ENOENT)
			ret = EFTYPE;
		return (ret);
	}

	/* get table information */
	ret = _db_lookup_by_s(ms->ms_db, _CITRUS_MAPPER_STD_SYM_INFO, &r, NULL);
	if (ret) {
		if (ret == ENOENT)
			ret = EFTYPE;
		return (ret);
	}
	switch (_region_size(&r)) {
	case _CITRUS_MAPPER_STD_ROWCOL_INFO_COMPAT_SIZE:
		ret = rowcol_parse_variable_compat(rc, &r);
		break;
	case _CITRUS_MAPPER_STD_ROWCOL_INFO_SIZE:
		ret = rowcol_parse_variable(rc, &r);
		break;
	default:
		return (EFTYPE);
	}
	if (ret != 0)
		return (ret);
	/* sanity check */
	switch (rc->rc_src_rowcol_bits) {
	case 8: case 16: case 32:
		if (rc->rc_src_rowcol_len <= 32 / rc->rc_src_rowcol_bits)
			break;
	/*FALLTHROUGH*/
	default:
		return (EFTYPE);
	}

	/* ilseq extension */
	rc->rc_oob_mode = _CITRUS_MAPPER_STD_OOB_NONIDENTICAL;
	rc->rc_dst_ilseq = rc->rc_dst_invalid;
	ret = _db_lookup_by_s(ms->ms_db,
	    _CITRUS_MAPPER_STD_SYM_ROWCOL_EXT_ILSEQ, &r, NULL);
	if (ret && ret != ENOENT)
		return (ret);
	if (_region_size(&r) < sizeof(*eix))
		return (EFTYPE);
	if (ret == 0) {
		eix = _region_head(&r);
		rc->rc_oob_mode = be32toh(eix->eix_oob_mode);
		rc->rc_dst_ilseq = be32toh(eix->eix_dst_ilseq);
	}

	/* calcurate expected table size */
	i = rc->rc_src_rowcol_len;
	lz = &rc->rc_src_rowcol[--i];
	table_size = lz->width;
	while (i > 0) {
		lz = &rc->rc_src_rowcol[--i];
		table_size *= lz->width;
	}
	table_size *= rc->rc_dst_unit_bits/8;

	if (table_size > UINT32_MAX ||
	    _region_size(&rc->rc_table) < table_size)
		return (EFTYPE);

	return (0);
}

typedef int (*initfunc_t)(struct _citrus_mapper_std *);
static const struct {
	initfunc_t			 t_init;
	const char			*t_name;
} types[] = {
	{ &rowcol_init, _CITRUS_MAPPER_STD_TYPE_ROWCOL },
};
#define NUM_OF_TYPES ((int)(sizeof(types)/sizeof(types[0])))

static int
/*ARGSUSED*/
_citrus_mapper_std_mapper_init(struct _citrus_mapper_area *__restrict ma __unused,
    struct _citrus_mapper * __restrict cm, const char * __restrict curdir,
    const void * __restrict var, size_t lenvar,
    struct _citrus_mapper_traits * __restrict mt, size_t lenmt)
{
	struct _citrus_mapper_std *ms;
	char path[PATH_MAX];
	const char *type;
	int id, ret;

	/* set traits */
	if (lenmt < sizeof(*mt)) {
		ret = EINVAL;
		goto err0;
	}
	mt->mt_src_max = mt->mt_dst_max = 1;	/* 1:1 converter */
	mt->mt_state_size = 0;			/* stateless */

	/* alloc mapper std structure */
	ms = malloc(sizeof(*ms));
	if (ms == NULL) {
		ret = errno;
		goto err0;
	}

	/* open mapper file */
	snprintf(path, sizeof(path), "%s/%.*s", curdir, (int)lenvar,
	    (const char *)var);
	ret = _map_file(&ms->ms_file, path);
	if (ret)
		goto err1;

	ret = _db_open(&ms->ms_db, &ms->ms_file, _CITRUS_MAPPER_STD_MAGIC,
	    &_db_hash_std, NULL);
	if (ret)
		goto err2;

	/* get mapper type */
	ret = _db_lookupstr_by_s(ms->ms_db, _CITRUS_MAPPER_STD_SYM_TYPE,
	    &type, NULL);
	if (ret) {
		if (ret == ENOENT)
			ret = EFTYPE;
		goto err3;
	}
	for (id = 0; id < NUM_OF_TYPES; id++)
		if (_bcs_strcasecmp(type, types[id].t_name) == 0)
			break;

	if (id == NUM_OF_TYPES)
		goto err3;

	/* init the per-type structure */
	ret = (*types[id].t_init)(ms);
	if (ret)
		goto err3;

	cm->cm_closure = ms;

	return (0);

err3:
	_db_close(ms->ms_db);
err2:
	_unmap_file(&ms->ms_file);
err1:
	free(ms);
err0:
	return (ret);
}

static void
/*ARGSUSED*/
_citrus_mapper_std_mapper_uninit(struct _citrus_mapper *cm)
{
	struct _citrus_mapper_std *ms;

	ms = cm->cm_closure;
	if (ms->ms_uninit)
		(*ms->ms_uninit)(ms);
	_db_close(ms->ms_db);
	_unmap_file(&ms->ms_file);
	free(ms);
}

static void
/*ARGSUSED*/
_citrus_mapper_std_mapper_init_state(void)
{

}

static int
/*ARGSUSED*/
_citrus_mapper_std_mapper_convert(struct _citrus_mapper * __restrict cm,
    _index_t * __restrict dst, _index_t src, void * __restrict ps)
{
	struct _citrus_mapper_std *ms;

	ms = cm->cm_closure;
	return ((*ms->ms_convert)(ms, dst, src, ps));
}
