/*	$OpenBSD: frontend.h,v 1.9 2021/01/27 08:30:50 florian Exp $	*/

/*
 * Copyright (c) 2018 Florian Obser <florian@openbsd.org>
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

#define	HAVE_IPV4	1
#define	HAVE_IPV6	2

struct trust_anchor {
	TAILQ_ENTRY(trust_anchor)	 entry;
	char				*ta;
};

TAILQ_HEAD(trust_anchor_head, trust_anchor);

struct imsg_rdns_proposal {
	uint32_t		 if_index;
	int			 src;
	struct sockaddr_rtdns	 rtdns;
};

struct dns64_prefix {
	struct in6_addr	 in6;
	int		 prefixlen;
	int		 flags;
};

void		 frontend(int, int);
void		 frontend_dispatch_main(int, short, void *);
void		 frontend_dispatch_resolver(int, short, void *);
int		 frontend_imsg_compose_main(int, pid_t, void *, uint16_t);
int		 frontend_imsg_compose_resolver(int, pid_t, void *, uint16_t);
char		*ip_port(struct sockaddr *);
void		 add_new_ta(struct trust_anchor_head *, char *);
void		 free_tas(struct trust_anchor_head *);
int		 merge_tas(struct trust_anchor_head *,
		    struct trust_anchor_head *);
