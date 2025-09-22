/*	$OpenBSD: rtable.h,v 1.36 2025/07/15 09:55:49 dlg Exp $ */

/*
 * Copyright (c) 2014-2016 Martin Pieuchot
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

#ifndef	_NET_RTABLE_H_
#define	_NET_RTABLE_H_

#include <sys/rwlock.h>

struct art;

/*
 *  Locks used to protect struct members in this file:
 *	I	immutable after creation
 *	N	net lock
 */

struct rtable {
	struct rwlock		 r_lock;
	struct art		*r_art;		/* [I] */
	unsigned int		 r_off;		/* [I] Offset of key in bytes */

	struct sockaddr		*r_source;	/* [N] use optional src addr */
};

/*
 * Newer routing table implementation based on ART (Allotment Routing
 * Table).
 */

#define	rt_key(rt)	((rt)->rt_dest)
#define	rt_plen(rt)	((rt)->rt_plen)
#define	RT_ROOT(rt)	(0)

int		 rtable_satoplen(sa_family_t, const struct sockaddr *);

void		 rtable_init(void);
int		 rtable_exists(unsigned int);
int		 rtable_empty(unsigned int);
int		 rtable_add(unsigned int);
unsigned int	 rtable_l2(unsigned int);
unsigned int	 rtable_loindex(unsigned int);
void		 rtable_l2set(unsigned int, unsigned int, unsigned int);

int		 rtable_setsource(unsigned int, int, struct sockaddr *);
struct sockaddr *rtable_getsource(unsigned int, int);
void		 rtable_clearsource(unsigned int, struct sockaddr *);
struct rtentry	*rtable_lookup(unsigned int, const struct sockaddr *,
		     const struct sockaddr *, const struct sockaddr *, uint8_t);
struct rtentry	*rtable_match(unsigned int, const struct sockaddr *,
		     uint32_t *);
struct rtentry	*rtable_iterate(struct rtentry *);
int		 rtable_insert(unsigned int, struct sockaddr *,
		     const struct sockaddr *, const struct sockaddr *, uint8_t,
		     struct rtentry *);
int		 rtable_delete(unsigned int, const struct sockaddr *,
		     const struct sockaddr *, struct rtentry *);
int		 rtable_walk(unsigned int, sa_family_t, struct rtentry **,
		     int (*)(struct rtentry *, void *, unsigned int), void *);
int		 rtable_read(unsigned int, sa_family_t,
		     int (*)(const struct rtentry *, void *, unsigned int),
		     void *);

int		 rtable_mpath_capable(unsigned int, sa_family_t);
int		 rtable_mpath_reprio(unsigned int, struct sockaddr *, int,
		     uint8_t, struct rtentry *);

#endif /* _NET_RTABLE_H_ */
