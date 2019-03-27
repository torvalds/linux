/* $FreeBSD$ */
/* $NetBSD: citrus_memstream.h,v 1.3 2005/05/14 17:55:42 tshiozak Exp $ */

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
 *
 */

#ifndef _CITRUS_MEMSTREAM_H_
#define _CITRUS_MEMSTREAM_H_

struct _citrus_memory_stream {
	struct _citrus_region	ms_region;
	size_t			ms_pos;
};

__BEGIN_DECLS
const char	*_citrus_memory_stream_getln(
		    struct _citrus_memory_stream * __restrict,
		    size_t * __restrict);
const char	*_citrus_memory_stream_matchline(
		    struct _citrus_memory_stream * __restrict,
		    const char * __restrict, size_t * __restrict, int);
void		*_citrus_memory_stream_chr(struct _citrus_memory_stream *,
		    struct _citrus_region *, char);
void		_citrus_memory_stream_skip_ws(struct _citrus_memory_stream *);
__END_DECLS

static __inline int
_citrus_memory_stream_iseof(struct _citrus_memory_stream *ms)
{

	return (ms->ms_pos >= _citrus_region_size(&ms->ms_region));
}

static __inline void
_citrus_memory_stream_bind(struct _citrus_memory_stream * __restrict ms,
    const struct _citrus_region * __restrict r)
{

	ms->ms_region = *r;
	ms->ms_pos = 0;
}

static __inline void
_citrus_memory_stream_bind_ptr(struct _citrus_memory_stream * __restrict ms,
    void *ptr, size_t sz)
{
	struct _citrus_region r;

	_citrus_region_init(&r, ptr, sz);
	_citrus_memory_stream_bind(ms, &r);
}

static __inline void
_citrus_memory_stream_rewind(struct _citrus_memory_stream *ms)
{

	ms->ms_pos = 0;
}

static __inline size_t
_citrus_memory_stream_tell(struct _citrus_memory_stream *ms)
{

	return (ms->ms_pos);
}

static __inline size_t
_citrus_memory_stream_remainder(struct _citrus_memory_stream *ms)
{
	size_t sz;

	sz = _citrus_region_size(&ms->ms_region);
	if (ms->ms_pos>sz)
		return (0);
	return (sz-ms->ms_pos);
}

static __inline int
_citrus_memory_stream_seek(struct _citrus_memory_stream *ms, size_t pos, int w)
{
	size_t sz;

	sz = _citrus_region_size(&ms->ms_region);

	switch (w) {
	case SEEK_SET:
		if (pos >= sz)
			return (-1);
		ms->ms_pos = pos;
		break;
	case SEEK_CUR:
		pos += (ssize_t)ms->ms_pos;
		if (pos >= sz)
			return (-1);
		ms->ms_pos = pos;
		break;
	case SEEK_END:
		if (sz < pos)
			return (-1);
		ms->ms_pos = sz - pos;
		break;
	}
	return (0);
}

static __inline int
_citrus_memory_stream_getc(struct _citrus_memory_stream *ms)
{

	if (_citrus_memory_stream_iseof(ms))
		return (EOF);
	return (_citrus_region_peek8(&ms->ms_region, ms->ms_pos++));
}

static __inline void
_citrus_memory_stream_ungetc(struct _citrus_memory_stream *ms, int ch)
{

	if (ch != EOF && ms->ms_pos > 0)
		ms->ms_pos--;
}

static __inline int
_citrus_memory_stream_peek(struct _citrus_memory_stream *ms)
{

	if (_citrus_memory_stream_iseof(ms))
		return (EOF);
	return (_citrus_region_peek8(&ms->ms_region, ms->ms_pos));
}

static __inline void *
_citrus_memory_stream_getregion(struct _citrus_memory_stream *ms,
    struct _citrus_region *r, size_t sz)
{
	void *ret;

	if (ms->ms_pos + sz > _citrus_region_size(&ms->ms_region))
		return (NULL);

	ret = _citrus_region_offset(&ms->ms_region, ms->ms_pos);
	ms->ms_pos += sz;
	if (r)
		_citrus_region_init(r, ret, sz);

	return (ret);
}

static __inline int
_citrus_memory_stream_get8(struct _citrus_memory_stream *ms, uint8_t *rval)
{

	if (ms->ms_pos + 1 > _citrus_region_size(&ms->ms_region))
		return (-1);

	*rval = _citrus_region_peek8(&ms->ms_region, ms->ms_pos);
	ms->ms_pos += 2;

	return (0);
}

static __inline int
_citrus_memory_stream_get16(struct _citrus_memory_stream *ms, uint16_t *rval)
{

	if (ms->ms_pos + 2 > _citrus_region_size(&ms->ms_region))
		return (-1);

	*rval = _citrus_region_peek16(&ms->ms_region, ms->ms_pos);
	ms->ms_pos += 2;

	return (0);
}

static __inline int
_citrus_memory_stream_get32(struct _citrus_memory_stream *ms, uint32_t *rval)
{

	if (ms->ms_pos + 4 > _citrus_region_size(&ms->ms_region))
		return (-1);

	*rval = _citrus_region_peek32(&ms->ms_region, ms->ms_pos);
	ms->ms_pos += 4;

	return (0);
}

static __inline int
_citrus_memory_stream_getln_region(struct _citrus_memory_stream *ms,
    struct _citrus_region *r)
{
	const char *ptr;
	size_t sz;

	ptr = _citrus_memory_stream_getln(ms, &sz);
	if (ptr)
		_citrus_region_init(r, __DECONST(void *, ptr), sz);

	return (ptr == NULL);
}

#endif
