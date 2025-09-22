/*	$OpenBSD: rfc5322.c,v 1.4 2023/12/05 13:38:25 op Exp $	*/

/*
 * Copyright (c) 2018 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "rfc5322.h"

struct buf {
	char	*buf;
	size_t	 bufsz;
	size_t	 buflen;
	size_t	 bufmax;
};

static int buf_alloc(struct buf *, size_t);
static int buf_grow(struct buf *, size_t);
static int buf_cat(struct buf *, const char *);

struct rfc5322_parser {
	const char	*line;
	int		 state;		/* last parser state */
	int		 next;		/* parser needs data */
	int		 unfold;
	const char	*currhdr;
	struct buf	 hdr;
	struct buf	 val;
};

struct rfc5322_parser *
rfc5322_parser_new(void)
{
	struct rfc5322_parser *parser;

	parser = calloc(1, sizeof(*parser));
	if (parser == NULL)
		return NULL;

	rfc5322_clear(parser);
	parser->hdr.bufmax = 1024;
	parser->val.bufmax = 65536;

	return parser;
}

void
rfc5322_free(struct rfc5322_parser *parser)
{
	free(parser->hdr.buf);
	free(parser->val.buf);
	free(parser);
}

void
rfc5322_clear(struct rfc5322_parser *parser)
{
	parser->line = NULL;
	parser->state = RFC5322_NONE;
	parser->next = 0;
	parser->hdr.buflen = 0;
	parser->val.buflen = 0;
}

int
rfc5322_push(struct rfc5322_parser *parser, const char *line)
{
	if (parser->line) {
		errno = EALREADY;
		return -1;
	}

	parser->line = line;
	parser->next = 0;

	return 0;
}

int
rfc5322_unfold_header(struct rfc5322_parser *parser)
{
	if (parser->unfold) {
		errno = EALREADY;
		return -1;
	}

	if (parser->currhdr == NULL) {
		errno = EOPNOTSUPP;
		return -1;
	}

	if (buf_cat(&parser->val, parser->currhdr) == -1)
		return -1;

	parser->currhdr = NULL;
	parser->unfold = 1;

	return 0;
}

static int
_rfc5322_next(struct rfc5322_parser *parser, struct rfc5322_result *res)
{
	size_t len;
	const char *pos, *line;

	line = parser->line;

	switch(parser->state) {

	case RFC5322_HEADER_START:
	case RFC5322_HEADER_CONT:
		res->hdr = parser->hdr.buf;

		if (line && (line[0] == ' ' || line[0] == '\t')) {
			parser->line = NULL;
			parser->next = 1;
			if (parser->unfold) {
				if (buf_cat(&parser->val, "\n") == -1 ||
				    buf_cat(&parser->val, line) == -1)
					return -1;
			}
			res->value = line;
			return RFC5322_HEADER_CONT;
		}

		if (parser->unfold) {
			parser->val.buflen = 0;
			parser->unfold = 0;
			res->value = parser->val.buf;
		}
		return RFC5322_HEADER_END;

	case RFC5322_NONE:
	case RFC5322_HEADER_END:
		if (line && line[0] != ' ' && line[0] != '\t' &&
		    (pos = strchr(line, ':'))) {
			len = pos - line;
			if (buf_grow(&parser->hdr, len + 1) == -1)
				return -1;
			(void)memcpy(parser->hdr.buf, line, len);
			parser->hdr.buf[len] = '\0';
			parser->hdr.buflen = len + 1;
			parser->line = NULL;
			parser->next = 1;
			parser->currhdr = pos + 1;
			res->hdr = parser->hdr.buf;
			res->value = pos + 1;
			return RFC5322_HEADER_START;
		}

		return RFC5322_END_OF_HEADERS;

	case RFC5322_END_OF_HEADERS:
		if (line == NULL)
			return RFC5322_END_OF_MESSAGE;

		if (line[0] == '\0') {
			parser->line = NULL;
			parser->next = 1;
			res->value = line;
			return RFC5322_BODY_START;
		}

		errno = EINVAL;
		return -1;

	case RFC5322_BODY_START:
	case RFC5322_BODY:
		if (line == NULL)
			return RFC5322_END_OF_MESSAGE;

		parser->line = NULL;
		parser->next = 1;
		res->value = line;
		return RFC5322_BODY;

	case RFC5322_END_OF_MESSAGE:
		errno = ENOMSG;
		return -1;

	default:
		errno = EINVAL;
		return -1;
	}
}

int
rfc5322_next(struct rfc5322_parser *parser, struct rfc5322_result *res)
{
	memset(res, 0, sizeof(*res));

	if (parser->next)
		return RFC5322_NONE;

	return (parser->state = _rfc5322_next(parser, res));
}

static int
buf_alloc(struct buf *b, size_t need)
{
	char *buf;
	size_t alloc;

	if (b->buf && b->bufsz >= need)
		return 0;

	if (need >= b->bufmax) {
		errno = ERANGE;
		return -1;
	}

#define N 256
	alloc = N * (need / N) + ((need % N) ? N : 0);
#undef N
	buf = reallocarray(b->buf, alloc, 1);
	if (buf == NULL)
		return -1;

	b->buf = buf;
	b->bufsz = alloc;

	return 0;
}

static int
buf_grow(struct buf *b, size_t sz)
{
	if (SIZE_T_MAX - b->buflen <= sz) {
		errno = ERANGE;
		return -1;
	}

	return buf_alloc(b, b->buflen + sz);
}

static int
buf_cat(struct buf *b, const char *s)
{
	size_t len = strlen(s);

	if (buf_grow(b, len + 1) == -1)
		return -1;

	(void)memmove(b->buf + b->buflen, s, len + 1);
	b->buflen += len;
	return 0;
}
