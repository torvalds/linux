/*	$OpenBSD: engine.h,v 1.7 2024/04/21 17:33:05 florian Exp $	*/

/*
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
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

struct imsg_configure_address {
	uint32_t		 if_index;
	struct sockaddr_in6	 addr;
	struct sockaddr_in6	 gw;
	struct in6_addr		 mask;
	uint32_t		 vltime;
	uint32_t		 pltime;
	uint32_t		 mtu;
	int			 temporary;
};

struct imsg_configure_dfr {
	uint32_t		 if_index;
	int			 rdomain;
	struct sockaddr_in6	 addr;
	uint32_t		 router_lifetime;
};

void		 engine(int, int);
int		 engine_imsg_compose_frontend(int, pid_t, void *, uint16_t);
