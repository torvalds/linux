/* $FreeBSD$ */
/*	$NetBSD: citrus_mapper_zone.c,v 1.4 2003/07/12 15:39:21 tshiozak Exp $	*/

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
#include <sys/queue.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "citrus_region.h"
#include "citrus_memstream.h"
#include "citrus_mmap.h"
#include "citrus_hash.h"
#include "citrus_mapper.h"
#include "citrus_mapper_zone.h"

/* ---------------------------------------------------------------------- */

_CITRUS_MAPPER_DECLS(mapper_zone);
_CITRUS_MAPPER_DEF_OPS(mapper_zone);


/* ---------------------------------------------------------------------- */

struct _zone {
	uint32_t	 z_begin;
	uint32_t	 z_end;
};

struct _citrus_mapper_zone {
	struct _zone	 mz_col;
	struct _zone	 mz_row;
	int32_t		 mz_col_offset;
	int32_t		 mz_row_offset;
	int		 mz_col_bits;
};

struct _parse_state {
	enum { S_BEGIN, S_OFFSET }	ps_state;
	union {
		uint32_t	u_imm;
		int32_t		s_imm;
		struct _zone	zone;
	} u;
#define ps_u_imm	u.u_imm
#define ps_s_imm	u.s_imm
#define ps_zone		u.zone
	int ps_top;
};

int
_citrus_mapper_zone_mapper_getops(struct _citrus_mapper_ops *ops)
{

	memcpy(ops, &_citrus_mapper_zone_mapper_ops,
	       sizeof(_citrus_mapper_zone_mapper_ops));

	return (0);
}

#define BUFSIZE 20
#define T_ERR	0x100
#define T_IMM	0x101

static int
get_imm(struct _memstream *ms, struct _parse_state *ps)
{
	int c, i, sign = 0;
	char buf[BUFSIZE + 1];
	char *p;

	for (i = 0; i < BUFSIZE; i++) {
retry:
		c = _memstream_peek(ms);
		if (i == 0) {
			if (sign == 0 && (c == '+' || c == '-')) {
				sign = c;
				_memstream_getc(ms);
				goto retry;
			} else if (!_bcs_isdigit(c))
				break;
		} else if (!_bcs_isxdigit(c))
			if (!(i == 1 && c == 'x'))
				break;
		buf[i] = _memstream_getc(ms);
	}
	buf[i] = '\0';
	ps->ps_u_imm = strtoul(buf, &p, 0);
	if ((p - buf) != i)
		return (T_ERR);
	if (sign == '-')
		ps->ps_u_imm = (unsigned long)-(long)ps->ps_u_imm;
	return (T_IMM);
}

static int
get_tok(struct _memstream *ms, struct _parse_state *ps)
{
	int c;

loop:
	c = _memstream_peek(ms);
	if (c == 0x00)
		return (EOF);
	if (_bcs_isspace(c)) {
		_memstream_getc(ms);
		goto loop;
	}

	switch (ps->ps_state) {
	case S_BEGIN:
		switch (c) {
		case ':':
		case '-':
		case '/':
			_memstream_getc(ms);
			return (c);
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			return (get_imm(ms, ps));
		}
		break;
	case S_OFFSET:
		switch (c) {
		case '/':
			_memstream_getc(ms);
			return (c);
		case '+':
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			return (get_imm(ms, ps));
		}
		break;
	}
	return (T_ERR);
}

static int
parse_zone(struct _memstream *ms, struct _parse_state *ps, struct _zone *z)
{

	if (get_tok(ms, ps) != T_IMM)
		return (-1);
	z->z_begin = ps->ps_u_imm;
	if (get_tok(ms, ps) != '-')
		return (-1);
	if (get_tok(ms, ps) != T_IMM)
		return (-1);
	z->z_end = ps->ps_u_imm;

	if (z->z_begin > z->z_end)
		return (-1);

	return (0);
}

static int
check_rowcol(struct _zone *z, int32_t ofs, uint32_t maxval)
{
	uint32_t remain;

	if (maxval != 0 && z->z_end >= maxval)
		return (-1);

	if (ofs > 0) {
		if (maxval == 0)
			/* this should 0x100000000 - z->z_end */
			remain = (z->z_end == 0) ? 0xFFFFFFFF :
			    0xFFFFFFFF - z->z_end + 1;
		else
			remain = maxval - z->z_end;
		if ((uint32_t)ofs > remain)
			return (-1);
	} else if (ofs < 0) {
		if (z->z_begin < (uint32_t)-ofs)
			return (-1);
	}

	return (0);
}

static int
parse_var(struct _citrus_mapper_zone *mz, struct _memstream *ms)
{
	struct _parse_state ps;
	uint32_t colmax, rowmax;
	int isrc, ret;

	ps.ps_state = S_BEGIN;

	if (parse_zone(ms, &ps, &mz->mz_col))
		return (-1);

	ret = get_tok(ms, &ps);
	if (ret == '/') {
		/* rowzone / colzone / bits */
		isrc = 1;
		mz->mz_row = mz->mz_col;

		if (parse_zone(ms, &ps, &mz->mz_col))
			return (-1);
		if (get_tok(ms, &ps) != '/')
			return (-1);
		if (get_tok(ms, &ps) != T_IMM)
			return (-1);
		mz->mz_col_bits = ps.ps_u_imm;
		if (mz->mz_col_bits < 0 || mz->mz_col_bits > 32)
			return (-1);
		ret = get_tok(ms, &ps);
	} else {
		/* colzone */
		isrc = 0;
		mz->mz_col_bits = 32;
		mz->mz_row.z_begin = mz->mz_row.z_end = 0;
	}
	if (ret == ':') {
		/* offset */
		ps.ps_state = S_OFFSET;
		if (get_tok(ms, &ps) != T_IMM)
			return (-1);
		mz->mz_col_offset = ps.ps_s_imm;
		if (isrc) {
			/* row/col */
			mz->mz_row_offset = mz->mz_col_offset;
			if (get_tok(ms, &ps) != '/')
				return (-1);
			if (get_tok(ms, &ps) != T_IMM)
				return (-1);
			mz->mz_col_offset = ps.ps_s_imm;
		} else
			mz->mz_row_offset = 0;
		ret = get_tok(ms, &ps);
	}
	if (ret != EOF)
		return (-1);

	/* sanity check */
	colmax = (mz->mz_col_bits == 32) ? 0 : 1 << mz->mz_col_bits;
	rowmax = (mz->mz_col_bits == 0) ? 0 : 1 << (32-mz->mz_col_bits);
	if (check_rowcol(&mz->mz_col, mz->mz_col_offset, colmax))
		return (-1);
	if (check_rowcol(&mz->mz_row, mz->mz_row_offset, rowmax))
		return (-1);

	return (0);
}

static int
/*ARGSUSED*/
_citrus_mapper_zone_mapper_init(struct _citrus_mapper_area *__restrict ma __unused,
    struct _citrus_mapper * __restrict cm, const char * __restrict dir __unused,
    const void * __restrict var, size_t lenvar,
    struct _citrus_mapper_traits * __restrict mt, size_t lenmt)
{
	struct _citrus_mapper_zone *mz;
	struct _memstream ms;
	struct _region r;

	if (lenmt < sizeof(*mt))
		return (EINVAL);

	mz = malloc(sizeof(*mz));
	if (mz == NULL)
		return (errno);

	mz->mz_col.z_begin = mz->mz_col.z_end = 0;
	mz->mz_row.z_begin = mz->mz_row.z_end = 0;
	mz->mz_col_bits = 0;
	mz->mz_row_offset = 0;
	mz->mz_col_offset = 0;

	_region_init(&r, __DECONST(void *, var), lenvar);
	_memstream_bind(&ms, &r);
	if (parse_var(mz, &ms)) {
		free(mz);
		return (EINVAL);
	}
	cm->cm_closure = mz;
	mt->mt_src_max = mt->mt_dst_max = 1;	/* 1:1 converter */
	mt->mt_state_size = 0;			/* stateless */

	return (0);
}

static void
/*ARGSUSED*/
_citrus_mapper_zone_mapper_uninit(struct _citrus_mapper *cm __unused)
{

}

static int
/*ARGSUSED*/
_citrus_mapper_zone_mapper_convert(struct _citrus_mapper * __restrict cm,
    _citrus_index_t * __restrict dst, _citrus_index_t src,
    void * __restrict ps __unused)
{
	struct _citrus_mapper_zone *mz = cm->cm_closure;
	uint32_t col, row;

	if (mz->mz_col_bits == 32) {
		col = src;
		row = 0;
		if (col < mz->mz_col.z_begin || col > mz->mz_col.z_end)
			return (_CITRUS_MAPPER_CONVERT_NONIDENTICAL);
		if (mz->mz_col_offset > 0)
			col += (uint32_t)mz->mz_col_offset;
		else
			col -= (uint32_t)-mz->mz_col_offset;
		*dst = col;
	} else {
		col = src & (((uint32_t)1 << mz->mz_col_bits) - 1);
		row = src >> mz->mz_col_bits;
		if (row < mz->mz_row.z_begin || row > mz->mz_row.z_end ||
		    col < mz->mz_col.z_begin || col > mz->mz_col.z_end)
			return (_CITRUS_MAPPER_CONVERT_NONIDENTICAL);
		if (mz->mz_col_offset > 0)
			col += (uint32_t)mz->mz_col_offset;
		else
			col -= (uint32_t)-mz->mz_col_offset;
		if (mz->mz_row_offset > 0)
			row += (uint32_t)mz->mz_row_offset;
		else
			row -= (uint32_t)-mz->mz_row_offset;
		*dst = col | (row << mz->mz_col_bits);
	}
	return (_CITRUS_MAPPER_CONVERT_SUCCESS);
}

static void
/*ARGSUSED*/
_citrus_mapper_zone_mapper_init_state(void)
{

}
