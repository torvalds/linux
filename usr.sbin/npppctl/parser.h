/*	$OpenBSD: parser.h,v 1.3 2014/03/22 04:30:31 yasuoka Exp $	*/

/* This file is derived from OpenBSD:src/usr.sbin/ikectl/parser.h 1.9 */
/*
 * Copyright (c) 2007, 2008 Reyk Floeter <reyk@vantronix.net>
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

#ifndef _NPPPCTL_PARSER_H
#define _NPPPCTL_PARSER_H

enum actions {
	NONE,
	SESSION_BRIEF,
	SESSION_PKTS,
	SESSION_ALL,
	CLEAR_SESSION,
	MONITOR_SESSION
};

enum protocol {
	PROTO_UNSPEC = 0,
	PPTP,
	L2TP,
	PPPOE,
	SSTP
};

struct parse_result {
	enum actions		 action;
	u_int			 ppp_id;
	int			 has_ppp_id;
	struct sockaddr_storage	 address;
	const char		*interface;
	enum protocol		 protocol;
	const char		*realm;
	const char		*username;
};

struct parse_result	*parse(int, char *[]);
enum protocol            parse_protocol(const char *);

#endif /* _NPPPCTL_PARSER_H */
