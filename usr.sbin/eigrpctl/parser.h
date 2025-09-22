/*	$OpenBSD: parser.h,v 1.3 2016/01/15 12:57:49 renato Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
 * Copyright (c) 2004 Esben Norby <norby@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#ifndef _PARSER_H_
#define _PARSER_H_

#include <sys/types.h>
#include <net/if.h>
#include <netinet/in.h>

enum actions {
	NONE,
	FIB,
	FIB_COUPLE,
	FIB_DECOUPLE,
	LOG_VERBOSE,
	LOG_BRIEF,
	SHOW,
	SHOW_IFACE,
	SHOW_IFACE_DTAIL,
	SHOW_NBR,
	SHOW_TOPOLOGY,
	SHOW_FIB,
	SHOW_FIB_IFACE,
	SHOW_STATS,
	CLEAR_NBR,
	RELOAD
};

struct parse_result {
	int			family;
	uint16_t		as;
	union eigrpd_addr	addr;
	uint8_t			prefixlen;
	char			ifname[IF_NAMESIZE];
	int			flags;
	enum actions		action;
};

struct parse_result	*parse(int, char *[]);
int			 parse_asnum(const char *, uint16_t *);
int			 parse_addr(const char *, int *,
    union eigrpd_addr *);
int			 parse_prefix(const char *, int *,
    union eigrpd_addr *, uint8_t *);

#endif	/* _PARSER_H_ */
