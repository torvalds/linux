/*	$OpenBSD: rfc5322.h,v 1.1 2018/08/23 10:07:06 eric Exp $	*/

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

struct rfc5322_result {
	const char	*hdr;
	const char	*value;
};

#define	RFC5322_ERR		-1
#define	RFC5322_NONE		0
#define	RFC5322_HEADER_START	1
#define	RFC5322_HEADER_CONT	2
#define	RFC5322_HEADER_END	3
#define	RFC5322_END_OF_HEADERS	4
#define	RFC5322_BODY_START	5
#define	RFC5322_BODY		6
#define	RFC5322_END_OF_MESSAGE	7

struct rfc5322_parser;

struct rfc5322_parser *rfc5322_parser_new(void);
void rfc5322_free(struct rfc5322_parser *);
void rfc5322_clear(struct rfc5322_parser *);
int rfc5322_push(struct rfc5322_parser *, const char *);
int rfc5322_next(struct rfc5322_parser *, struct rfc5322_result *);
int rfc5322_unfold_header(struct rfc5322_parser *);
