/*	$OpenBSD: sdl.h,v 1.9 2017/10/18 17:31:01 millert Exp $ */

/*
 * Copyright (c) 2003-2007 Bob Beck.  All rights reserved.
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

#ifndef _SDL_H_
#define _SDL_H_

#include <sys/types.h>
#include <sys/socket.h>

/* spamd netblock (black) list entry (ipv4) */
struct sdentry_v4 {
	struct in_addr sda;
	struct in_addr sdm;
};

struct sdentries_v4 {
	struct sdentry_v4 *addrs;
	u_int naddrs;
};

struct sdaddr_v6 {
	union {
		struct in6_addr		addr;
		u_int32_t		addr32[4];
	} _sda;		    /* 128-bit address */
#define addr32  _sda.addr32
};

/* spamd netblock (black) list entry (ipv6) */
struct sdentry_v6 {
	struct sdaddr_v6 sda;
	struct sdaddr_v6 sdm;
};

struct sdentries_v6 {
	struct sdentry_v6 *addrs;
	u_int naddrs;
};

/* spamd source list */
struct sdlist {
	char *tag;	/* sdlist source name */
	char *string;	/* Format (451) string with no smtp code or \r\n */
	struct sdentries_v4 v4;
	struct sdentries_v6 v6;
};

int	sdl_add(char *, char *, char **, u_int, char **, u_int);
void	sdl_del(char *);
int	sdl_check(struct sdlist *, int, void *);
struct sdlist **sdl_lookup(struct sdlist *, int, void *);

#endif	/* _SDL_H_ */
