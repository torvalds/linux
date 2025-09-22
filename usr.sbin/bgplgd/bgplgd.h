/*	$OpenBSD: bgplgd.h,v 1.5 2024/12/03 10:38:06 claudio Exp $ */
/*
 * Copyright (c) 2020 Claudio Jeker <claudio@openbsd.org>
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

#define QS_NEIGHBOR		1
#define QS_GROUP		2
#define QS_AS			3
#define QS_PREFIX		4
#define QS_COMMUNITY		5
#define QS_EXTCOMMUNITY		6
#define QS_LARGECOMMUNITY	7
#define QS_AF			8
#define QS_RIB			9
#define QS_OVS			10
#define QS_BEST			11
#define QS_ALL			12
#define QS_SHORTER		13
#define QS_ERROR		14
#define QS_AVS			15
#define QS_INVALID		16
#define QS_LEAKED		17
#define QS_FILTERED		18
#define QS_MAX			19

/* too add: empty-as, peer-as, source-as, transit-as */

#define QS_MASK_NEIGHBOR	((1 << QS_NEIGHBOR) | (1 << QS_GROUP))
#define QS_MASK_ADJRIB						\
	((1 << QS_NEIGHBOR) | (1 << QS_GROUP) |	(1 << QS_AS) |	\
	(1 << QS_PREFIX) | (1 << QS_COMMUNITY) |		\
	(1 << QS_EXTCOMMUNITY) | (1 << QS_LARGECOMMUNITY) |	\
	(1 << QS_AF) | (1 << QS_OVS) | (1 << QS_BEST) |		\
	(1 << QS_ALL) | (1 << QS_SHORTER) | (1 << QS_ERROR) |	\
	(1 << QS_AVS) | (1 << QS_INVALID) | (1 << QS_LEAKED) |	\
	(1 << QS_FILTERED))

#define QS_MASK_RIB	(QS_MASK_ADJRIB | (1 << QS_RIB))

struct cmd;
struct lg_ctx {
	const struct cmd	*command;
	unsigned int		qs_mask;
	unsigned int		qs_set;
	union {
		char	*string;
		int	one;
	}			qs_args[QS_MAX];
};

extern char	*bgpctlpath;
extern char	*bgpctlsock;

/* qs.c - query string handling */
int	parse_querystring(const char *, struct lg_ctx *);
size_t	qs_argv(char **, size_t, size_t, struct lg_ctx *, int);

/* main entry points for slowcgi */
int	prep_request(struct lg_ctx *, const char *, const char *, const char *);
void	bgpctl_call(struct lg_ctx *);
