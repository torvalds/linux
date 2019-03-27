/* $FreeBSD$ */
/* $NetBSD: citrus_hz.c,v 1.2 2008/06/14 16:01:07 tnozaki Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2004, 2006 Citrus Project,
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

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <sys/types.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "citrus_namespace.h"
#include "citrus_types.h"
#include "citrus_bcs.h"
#include "citrus_module.h"
#include "citrus_stdenc.h"

#include "citrus_hz.h"
#include "citrus_prop.h"

/*
 * wchar_t mapping:
 *
 * CTRL/ASCII	00000000 00000000 00000000 gxxxxxxx
 * GB2312	00000000 00000000 0xxxxxxx gxxxxxxx
 * 94/96*n (~M)	0mmmmmmm 0xxxxxxx 0xxxxxxx gxxxxxxx
 */

#define ESCAPE_CHAR	'~'

typedef enum {
	CTRL = 0, ASCII = 1, GB2312 = 2, CS94 = 3, CS96 = 4
} charset_t;

typedef struct {
	int	 start;
	int	 end;
	int	 width;
} range_t;

static const range_t ranges[] = {
#define RANGE(start, end) { start, end, (end - start) + 1 }
/* CTRL   */ RANGE(0x00, 0x1F),
/* ASCII  */ RANGE(0x20, 0x7F),
/* GB2312 */ RANGE(0x21, 0x7E),
/* CS94   */ RANGE(0x21, 0x7E),
/* CS96   */ RANGE(0x20, 0x7F),
#undef RANGE
};

typedef struct escape_t escape_t;
typedef struct {
	charset_t	 charset;
	escape_t	*escape;
	ssize_t		 length;
#define ROWCOL_MAX	3
} graphic_t;

typedef TAILQ_HEAD(escape_list, escape_t) escape_list;
struct escape_t {
	TAILQ_ENTRY(escape_t)	 entry;
	escape_list		*set;
	graphic_t		*left;
	graphic_t		*right;
	int			 ch;
};

#define GL(escape)	((escape)->left)
#define GR(escape)	((escape)->right)
#define SET(escape)	((escape)->set)
#define ESC(escape)	((escape)->ch)
#define INIT(escape)	(TAILQ_FIRST(SET(escape)))

static __inline escape_t *
find_escape(escape_list *set, int ch)
{
	escape_t *escape;

	TAILQ_FOREACH(escape, set, entry) {
		if (ESC(escape) == ch)
			break;
	}

	return (escape);
}

typedef struct {
	escape_list	 e0;
	escape_list	 e1;
	graphic_t	*ascii;
	graphic_t	*gb2312;
} _HZEncodingInfo;

#define E0SET(ei)	(&(ei)->e0)
#define E1SET(ei)	(&(ei)->e1)
#define INIT0(ei)	(TAILQ_FIRST(E0SET(ei)))
#define INIT1(ei)	(TAILQ_FIRST(E1SET(ei)))

typedef struct {
	escape_t	*inuse;
	int		 chlen;
	char		 ch[ROWCOL_MAX];
} _HZState;

#define _CEI_TO_EI(_cei_)		(&(_cei_)->ei)
#define _CEI_TO_STATE(_cei_, _func_)	(_cei_)->states.s_##_func_

#define _FUNCNAME(m)			_citrus_HZ_##m
#define _ENCODING_INFO			_HZEncodingInfo
#define _ENCODING_STATE			_HZState
#define _ENCODING_MB_CUR_MAX(_ei_)	MB_LEN_MAX
#define _ENCODING_IS_STATE_DEPENDENT		1
#define _STATE_NEEDS_EXPLICIT_INIT(_ps_)	((_ps_)->inuse == NULL)

static __inline void
_citrus_HZ_init_state(_HZEncodingInfo * __restrict ei,
    _HZState * __restrict psenc)
{

	psenc->chlen = 0;
	psenc->inuse = INIT0(ei);
}

#if 0
static __inline void
/*ARGSUSED*/
_citrus_HZ_pack_state(_HZEncodingInfo * __restrict ei __unused,
    void *__restrict pspriv, const _HZState * __restrict psenc)
{

	memcpy(pspriv, (const void *)psenc, sizeof(*psenc));
}

static __inline void
/*ARGSUSED*/
_citrus_HZ_unpack_state(_HZEncodingInfo * __restrict ei __unused,
    _HZState * __restrict psenc, const void * __restrict pspriv)
{

	memcpy((void *)psenc, pspriv, sizeof(*psenc));
}
#endif

static int
_citrus_HZ_mbrtowc_priv(_HZEncodingInfo * __restrict ei,
    wchar_t * __restrict pwc, char ** __restrict s, size_t n,
    _HZState * __restrict psenc, size_t * __restrict nresult)
{
	escape_t *candidate, *init;
	graphic_t *graphic;
	const range_t *range;
	char *s0;
	wchar_t wc;
	int bit, ch, head, len, tail;

	if (*s == NULL) {
		_citrus_HZ_init_state(ei, psenc);
		*nresult = 1;
		return (0);
	}
	s0 = *s;
	if (psenc->chlen < 0 || psenc->inuse == NULL)
		return (EINVAL);

	wc = (wchar_t)0;
	bit = head = tail = 0;
	graphic = NULL;
	for (len = 0; len <= MB_LEN_MAX;) {
		if (psenc->chlen == tail) {
			if (n-- < 1) {
				*s = s0;
				*nresult = (size_t)-2;
				return (0);
			}
			psenc->ch[psenc->chlen++] = *s0++;
			++len;
		}
		ch = (unsigned char)psenc->ch[tail++];
		if (tail == 1) {
			if ((ch & ~0x80) <= 0x1F) {
				if (psenc->inuse != INIT0(ei))
					break;
				wc = (wchar_t)ch;
				goto done;
			}
			if (ch & 0x80) {
				graphic = GR(psenc->inuse);
				bit = 0x80;
				ch &= ~0x80;
			} else {
				graphic = GL(psenc->inuse);
				if (ch == ESCAPE_CHAR)
					continue;
				bit = 0x0;
			}
			if (graphic == NULL)
				break;
		} else if (tail == 2 && psenc->ch[0] == ESCAPE_CHAR) {
			if (tail < psenc->chlen)
				return (EINVAL);
			if (ch == ESCAPE_CHAR) {
				++head;
			} else if (ch == '\n') {
				if (psenc->inuse != INIT0(ei))
					break;
				tail = psenc->chlen = 0;
				continue;
			} else {
				candidate = NULL;
				init = INIT0(ei);
				if (psenc->inuse == init) {
					init = INIT1(ei);
				} else if (INIT(psenc->inuse) == init) {
					if (ESC(init) != ch)
						break;
					candidate = init;
				}
				if (candidate == NULL) {
					candidate = find_escape(
					    SET(psenc->inuse), ch);
					if (candidate == NULL) {
						if (init == NULL ||
						    ESC(init) != ch)
							break;
						candidate = init;
					}
				}
				psenc->inuse = candidate;
				tail = psenc->chlen = 0;
				continue;
			}
		} else if (ch & 0x80) {
			if (graphic != GR(psenc->inuse))
				break;
			ch &= ~0x80;
		} else {
			if (graphic != GL(psenc->inuse))
				break;
		}
		range = &ranges[(size_t)graphic->charset];
		if (range->start > ch || range->end < ch)
			break;
		wc <<= 8;
		wc |= ch;
		if (graphic->length == (tail - head)) {
			if (graphic->charset > GB2312)
				bit |= ESC(psenc->inuse) << 24;
			wc |= bit;
			goto done;
		}
	}
	*nresult = (size_t)-1;
	return (EILSEQ);
done:
	if (tail < psenc->chlen)
		return (EINVAL);
	*s = s0;
	if (pwc != NULL)
		*pwc = wc;
	psenc->chlen = 0;
	*nresult = (wc == 0) ? 0 : len;

	return (0);
}

static int
_citrus_HZ_wcrtomb_priv(_HZEncodingInfo * __restrict ei,
    char * __restrict s, size_t n, wchar_t wc,
    _HZState * __restrict psenc, size_t * __restrict nresult)
{
	escape_t *candidate, *init;
	graphic_t *graphic;
	const range_t *range;
	size_t len;
	int bit, ch;

	if (psenc->chlen != 0 || psenc->inuse == NULL)
		return (EINVAL);
	if (wc & 0x80) {
		bit = 0x80;
		wc &= ~0x80;
	} else {
		bit = 0x0;
	}
	if ((uint32_t)wc <= 0x1F) {
		candidate = INIT0(ei);
		graphic = (bit == 0) ? candidate->left : candidate->right;
		if (graphic == NULL)
			goto ilseq;
		range = &ranges[(size_t)CTRL];
		len = 1;
	} else if ((uint32_t)wc <= 0x7F) {
		graphic = ei->ascii;
		if (graphic == NULL)
			goto ilseq;
		candidate = graphic->escape;
		range = &ranges[(size_t)graphic->charset];
		len = graphic->length;
	} else if ((uint32_t)wc <= 0x7F7F) {
		graphic = ei->gb2312;
		if (graphic == NULL)
			goto ilseq;
		candidate = graphic->escape;
		range = &ranges[(size_t)graphic->charset];
		len = graphic->length;
	} else {
		ch = (wc >> 24) & 0xFF;
		candidate = find_escape(E0SET(ei), ch);
		if (candidate == NULL) {
			candidate = find_escape(E1SET(ei), ch);
			if (candidate == NULL)
				goto ilseq;
		}
		wc &= ~0xFF000000;
		graphic = (bit == 0) ? candidate->left : candidate->right;
		if (graphic == NULL)
			goto ilseq;
		range = &ranges[(size_t)graphic->charset];
		len = graphic->length;
	}
	if (psenc->inuse != candidate) {
		init = INIT0(ei);
		if (SET(psenc->inuse) == SET(candidate)) {
			if (INIT(psenc->inuse) != init ||
			    psenc->inuse == init || candidate == init)
				init = NULL;
		} else if (candidate == (init = INIT(candidate))) {
			init = NULL;
		}
		if (init != NULL) {
			if (n < 2)
				return (E2BIG);
			n -= 2;
			psenc->ch[psenc->chlen++] = ESCAPE_CHAR;
			psenc->ch[psenc->chlen++] = ESC(init);
		}
		if (n < 2)
			return (E2BIG);
		n -= 2;
		psenc->ch[psenc->chlen++] = ESCAPE_CHAR;
		psenc->ch[psenc->chlen++] = ESC(candidate);
		psenc->inuse = candidate;
	}
	if (n < len)
		return (E2BIG);
	while (len-- > 0) {
		ch = (wc >> (len * 8)) & 0xFF;
		if (range->start > ch || range->end < ch)
			goto ilseq;
		psenc->ch[psenc->chlen++] = ch | bit;
	}
	memcpy(s, psenc->ch, psenc->chlen);
	*nresult = psenc->chlen;
	psenc->chlen = 0;

	return (0);

ilseq:
	*nresult = (size_t)-1;
	return (EILSEQ);
}

static __inline int
_citrus_HZ_put_state_reset(_HZEncodingInfo * __restrict ei,
    char * __restrict s, size_t n, _HZState * __restrict psenc,
    size_t * __restrict nresult)
{
	escape_t *candidate;

	if (psenc->chlen != 0 || psenc->inuse == NULL)
		return (EINVAL);
	candidate = INIT0(ei);
	if (psenc->inuse != candidate) {
		if (n < 2)
			return (E2BIG);
		n -= 2;
		psenc->ch[psenc->chlen++] = ESCAPE_CHAR;
		psenc->ch[psenc->chlen++] = ESC(candidate);
	}
	if (n < 1)
		return (E2BIG);
	if (psenc->chlen > 0)
		memcpy(s, psenc->ch, psenc->chlen);
	*nresult = psenc->chlen;
	_citrus_HZ_init_state(ei, psenc);

	return (0);
}

static __inline int
_citrus_HZ_stdenc_get_state_desc_generic(_HZEncodingInfo * __restrict ei,
    _HZState * __restrict psenc, int * __restrict rstate)
{

	if (psenc->chlen < 0 || psenc->inuse == NULL)
		return (EINVAL);
	*rstate = (psenc->chlen == 0)
	    ? ((psenc->inuse == INIT0(ei))
	        ? _STDENC_SDGEN_INITIAL
	        : _STDENC_SDGEN_STABLE)
	    : ((psenc->ch[0] == ESCAPE_CHAR)
	        ? _STDENC_SDGEN_INCOMPLETE_SHIFT
	        : _STDENC_SDGEN_INCOMPLETE_CHAR);

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_HZ_stdenc_wctocs(_HZEncodingInfo * __restrict ei __unused,
    _csid_t * __restrict csid, _index_t * __restrict idx, wchar_t wc)
{
	int bit;

	if (wc & 0x80) {
		bit = 0x80;
		wc &= ~0x80;
	} else
		bit = 0x0;
	if ((uint32_t)wc <= 0x7F) {
		*csid = (_csid_t)bit;
		*idx = (_index_t)wc;
	} else if ((uint32_t)wc <= 0x7F7F) {
		*csid = (_csid_t)(bit | 0x8000);
		*idx = (_index_t)wc;
	} else {
		*csid = (_index_t)(wc & ~0x00FFFF7F);
		*idx = (_csid_t)(wc & 0x00FFFF7F);
	}

	return (0);
}

static __inline int
/*ARGSUSED*/
_citrus_HZ_stdenc_cstowc(_HZEncodingInfo * __restrict ei __unused,
    wchar_t * __restrict wc, _csid_t csid, _index_t idx)
{

	*wc = (wchar_t)idx;
	switch (csid) {
	case 0x80:
	case 0x8080:
		*wc |= (wchar_t)0x80;
		/*FALLTHROUGH*/
	case 0x0:
	case 0x8000:
		break;
	default:
		*wc |= (wchar_t)csid;
	}

	return (0);
}

static void
_citrus_HZ_encoding_module_uninit(_HZEncodingInfo *ei)
{
	escape_t *escape;

	while ((escape = TAILQ_FIRST(E0SET(ei))) != NULL) {
		TAILQ_REMOVE(E0SET(ei), escape, entry);
		free(GL(escape));
		free(GR(escape));
		free(escape);
	}
	while ((escape = TAILQ_FIRST(E1SET(ei))) != NULL) {
		TAILQ_REMOVE(E1SET(ei), escape, entry);
		free(GL(escape));
		free(GR(escape));
		free(escape);
	}
}

static int
_citrus_HZ_parse_char(void *context, const char *name __unused, const char *s)
{
	escape_t *escape;
	void **p;

	p = (void **)context;
	escape = (escape_t *)p[0];
	if (escape->ch != '\0')
		return (EINVAL);
	escape->ch = *s++;
	if (escape->ch == ESCAPE_CHAR || *s != '\0')
		return (EINVAL);

	return (0);
}

static int
_citrus_HZ_parse_graphic(void *context, const char *name, const char *s)
{
	_HZEncodingInfo *ei;
	escape_t *escape;
	graphic_t *graphic;
	void **p;

	p = (void **)context;
	escape = (escape_t *)p[0];
	ei = (_HZEncodingInfo *)p[1];
	graphic = calloc(1, sizeof(*graphic));
	if (graphic == NULL)
		return (ENOMEM);
	if (strcmp("GL", name) == 0) {
		if (GL(escape) != NULL)
			goto release;
		GL(escape) = graphic;
	} else if (strcmp("GR", name) == 0) {
		if (GR(escape) != NULL)
			goto release;
		GR(escape) = graphic;
	} else {
release:
		free(graphic);
		return (EINVAL);
	}
	graphic->escape = escape;
	if (_bcs_strncasecmp("ASCII", s, 5) == 0) {
		if (s[5] != '\0')
			return (EINVAL);
		graphic->charset = ASCII;
		graphic->length = 1;
		ei->ascii = graphic;
		return (0);
	} else if (_bcs_strncasecmp("GB2312", s, 6) == 0) {
		if (s[6] != '\0')
			return (EINVAL);
		graphic->charset = GB2312;
		graphic->length = 2;
		ei->gb2312 = graphic;
		return (0);
	} else if (strncmp("94*", s, 3) == 0)
		graphic->charset = CS94;
	else if (strncmp("96*", s, 3) == 0)
		graphic->charset = CS96;
	else
		return (EINVAL);
	s += 3;
	switch(*s) {
	case '1': case '2': case '3':
		graphic->length = (size_t)(*s - '0');
		if (*++s == '\0')
			break;
	/*FALLTHROUGH*/
	default:
		return (EINVAL);
	}
	return (0);
}

static const _citrus_prop_hint_t escape_hints[] = {
_CITRUS_PROP_HINT_STR("CH", &_citrus_HZ_parse_char),
_CITRUS_PROP_HINT_STR("GL", &_citrus_HZ_parse_graphic),
_CITRUS_PROP_HINT_STR("GR", &_citrus_HZ_parse_graphic),
_CITRUS_PROP_HINT_END
};

static int
_citrus_HZ_parse_escape(void *context, const char *name, const char *s)
{
	_HZEncodingInfo *ei;
	escape_t *escape;
	void *p[2];

	ei = (_HZEncodingInfo *)context;
	escape = calloc(1, sizeof(*escape));
	if (escape == NULL)
		return (EINVAL);
	if (strcmp("0", name) == 0) {
		escape->set = E0SET(ei);
		TAILQ_INSERT_TAIL(E0SET(ei), escape, entry);
	} else if (strcmp("1", name) == 0) {
		escape->set = E1SET(ei);
		TAILQ_INSERT_TAIL(E1SET(ei), escape, entry);
	} else {
		free(escape);
		return (EINVAL);
	}
	p[0] = (void *)escape;
	p[1] = (void *)ei;
	return (_citrus_prop_parse_variable(
	    escape_hints, (void *)&p[0], s, strlen(s)));
}

static const _citrus_prop_hint_t root_hints[] = {
_CITRUS_PROP_HINT_STR("0", &_citrus_HZ_parse_escape),
_CITRUS_PROP_HINT_STR("1", &_citrus_HZ_parse_escape),
_CITRUS_PROP_HINT_END
};

static int
_citrus_HZ_encoding_module_init(_HZEncodingInfo * __restrict ei,
    const void * __restrict var, size_t lenvar)
{
	int errnum;

	memset(ei, 0, sizeof(*ei));
	TAILQ_INIT(E0SET(ei));
	TAILQ_INIT(E1SET(ei));
	errnum = _citrus_prop_parse_variable(
	    root_hints, (void *)ei, var, lenvar);
	if (errnum != 0)
		_citrus_HZ_encoding_module_uninit(ei);
	return (errnum);
}

/* ----------------------------------------------------------------------
 * public interface for stdenc
 */

_CITRUS_STDENC_DECLS(HZ);
_CITRUS_STDENC_DEF_OPS(HZ);

#include "citrus_stdenc_template.h"
