/*	$OpenBSD: util.h,v 1.5 2023/07/07 20:38:17 bluhm Exp $	*/

/*
 * Copyright (c) 2015 Martin Pieuchot
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

void	route_insert(unsigned int, sa_family_t, char *);
void	route_delete(unsigned int, sa_family_t, char *);
void	route_lookup(unsigned int, sa_family_t, char *);

int	do_from_file(unsigned int, sa_family_t, char *,
	    void (*f)(unsigned int, sa_family_t, char *));

int	rtentry_dump(struct rtentry *, void *, unsigned int);
int	rtentry_delete(struct rtentry *, void *, unsigned int);
void	rt_maskedcopy(struct sockaddr *, struct sockaddr *, struct sockaddr *);
int	maskcmp(sa_family_t, struct sockaddr *, struct sockaddr *);
int	inet_net_ptosa(sa_family_t, const char *, struct sockaddr *,
	     struct sockaddr *);
char	*inet_net_satop(sa_family_t, struct sockaddr *, int, char *, size_t);

#define satosin(sa)	((struct sockaddr_in *)(sa))
#define satosin6(sa)	((struct sockaddr_in6 *)(sa))
