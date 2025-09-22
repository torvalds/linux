/*	$OpenBSD: rtable.c,v 1.95 2025/07/16 13:48:38 jsg Exp $ */

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

#ifndef _KERNEL
#include "kern_compat.h"
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/domain.h>
#include <sys/srp.h>
#include <sys/smr.h>
#endif

#include <net/rtable.h>
#include <net/route.h>
#include <net/art.h>

/*
 * Structures used by rtable_get() to retrieve the corresponding
 * routing table for a given pair of ``af'' and ``rtableid''.
 *
 * Note that once allocated routing table heads are never freed.
 * This way we do not need to reference count them.
 *
 *	afmap		    rtmap/dommp
 *   -----------          ---------     -----
 *   |   0     |--------> | 0 | 0 | ... | 0 |	Array mapping rtableid (=index)
 *   -----------          ---------     -----   to rdomain/loopback (=value).
 *   | AF_INET |.
 *   ----------- `.       .---------.     .---------.
 *       ...	   `----> | rtable0 | ... | rtableN |	Array of pointers for
 *   -----------          '---------'     '---------'	IPv4 routing tables
 *   | AF_MPLS |					indexed by ``rtableid''.
 *   -----------
 */
struct srp	  *afmap;
uint8_t		   af2idx[AF_MAX+1];	/* To only allocate supported AF */
uint8_t		   af2idx_max;

/* Array of routing table pointers. */
struct rtmap {
	unsigned int	   limit;
	void		 **tbl;
};

/*
 * Array of rtableid -> rdomain mapping.
 *
 * Only used for the first index as described above.
 */
struct dommp {
	unsigned int	   limit;
	/*
	 * Array to get the routing domain and loopback interface related to
	 * a routing table. Format:
	 *
	 * 8 unused bits | 16 bits for loopback index | 8 bits for rdomain
	 */
	unsigned int	  *value;
};

unsigned int	   rtmap_limit = 0;

void		   rtmap_init(void);
void		   rtmap_grow(unsigned int, sa_family_t);
void		   rtmap_dtor(void *, void *);

struct srp_gc	   rtmap_gc = SRP_GC_INITIALIZER(rtmap_dtor, NULL);

void		   rtable_init_backend(void);
struct rtable	  *rtable_alloc(unsigned int, unsigned int, unsigned int);
struct rtable	  *rtable_get(unsigned int, sa_family_t);

void
rtmap_init(void)
{
	const struct domain	*dp;
	int			 i;

	/* Start with a single table for every domain that requires it. */
	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_rtoffset == 0)
			continue;

		rtmap_grow(1, dp->dom_family);
	}

	/* Initialize the rtableid->rdomain mapping table. */
	rtmap_grow(1, 0);

	rtmap_limit = 1;
}

/*
 * Grow the size of the array of routing table for AF ``af'' to ``nlimit''.
 */
void
rtmap_grow(unsigned int nlimit, sa_family_t af)
{
	struct rtmap	*map, *nmap;
	int		 i;

	KERNEL_ASSERT_LOCKED();

	KASSERT(nlimit > rtmap_limit);

	nmap = malloc(sizeof(*nmap), M_RTABLE, M_WAITOK);
	nmap->limit = nlimit;
	nmap->tbl = mallocarray(nlimit, sizeof(*nmap[0].tbl), M_RTABLE,
	    M_WAITOK|M_ZERO);

	map = srp_get_locked(&afmap[af2idx[af]]);
	if (map != NULL) {
		KASSERT(map->limit == rtmap_limit);

		for (i = 0; i < map->limit; i++)
			nmap->tbl[i] = map->tbl[i];
	}

	srp_update_locked(&rtmap_gc, &afmap[af2idx[af]], nmap);
}

void
rtmap_dtor(void *null, void *xmap)
{
	struct rtmap	*map = xmap;

	/*
	 * doesn't need to be serialized since this is the last reference
	 * to this map. there's nothing to race against.
	 */
	free(map->tbl, M_RTABLE, map->limit * sizeof(*map[0].tbl));
	free(map, M_RTABLE, sizeof(*map));
}

void
rtable_init(void)
{
	const struct domain	*dp;
	int			 i;

	KASSERT(sizeof(struct rtmap) == sizeof(struct dommp));

	/* We use index 0 for the rtable/rdomain map. */
	af2idx_max = 1;
	memset(af2idx, 0, sizeof(af2idx));

	/*
	 * Compute the maximum supported key length in case the routing
	 * table backend needs it.
	 */
	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_rtoffset == 0)
			continue;

		af2idx[dp->dom_family] = af2idx_max++;
	}
	rtable_init_backend();

	/*
	 * Allocate AF-to-id table now that we now how many AFs this
	 * kernel supports.
	 */
	afmap = mallocarray(af2idx_max + 1, sizeof(*afmap), M_RTABLE,
	    M_WAITOK|M_ZERO);

	rtmap_init();

	if (rtable_add(0) != 0)
		panic("unable to create default routing table");

	rt_timer_init();
}

int
rtable_add(unsigned int id)
{
	const struct domain	*dp;
	struct rtable		*tbl;
	struct rtmap		*map;
	struct dommp		*dmm;
	sa_family_t		 af;
	unsigned int		 off, alen;
	int			 i, error = 0;

	if (id > RT_TABLEID_MAX)
		return (EINVAL);

	KERNEL_LOCK();

	if (rtable_exists(id))
		goto out;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_rtoffset == 0)
			continue;

		af = dp->dom_family;
		off = dp->dom_rtoffset;
		alen = dp->dom_maxplen;

		if (id >= rtmap_limit)
			rtmap_grow(id + 1, af);

		tbl = rtable_alloc(id, alen, off);
		if (tbl == NULL) {
			error = ENOMEM;
			goto out;
		}

		map = srp_get_locked(&afmap[af2idx[af]]);
		map->tbl[id] = tbl;
	}

	/* Reflect possible growth. */
	if (id >= rtmap_limit) {
		rtmap_grow(id + 1, 0);
		rtmap_limit = id + 1;
	}

	/* Use main rtable/rdomain by default. */
	dmm = srp_get_locked(&afmap[0]);
	dmm->value[id] = 0;
out:
	KERNEL_UNLOCK();

	return (error);
}

struct rtable *
rtable_get(unsigned int rtableid, sa_family_t af)
{
	struct rtmap	*map;
	struct rtable	*tbl = NULL;
	struct srp_ref	 sr;

	if (af >= nitems(af2idx) || af2idx[af] == 0)
		return (NULL);

	map = srp_enter(&sr, &afmap[af2idx[af]]);
	if (rtableid < map->limit)
		tbl = map->tbl[rtableid];
	srp_leave(&sr);

	return (tbl);
}

int
rtable_exists(unsigned int rtableid)
{
	const struct domain	*dp;
	void			*tbl;
	int			 i;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_rtoffset == 0)
			continue;

		tbl = rtable_get(rtableid, dp->dom_family);
		if (tbl != NULL)
			return (1);
	}

	return (0);
}

int
rtable_empty(unsigned int rtableid)
{
	const struct domain	*dp;
	int			 i;
	struct rtable		*tbl;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_rtoffset == 0)
			continue;

		tbl = rtable_get(rtableid, dp->dom_family);
		if (tbl == NULL)
			continue;
		if (!art_is_empty(tbl->r_art))
			return (0);
	}

	return (1);
}

unsigned int
rtable_l2(unsigned int rtableid)
{
	struct dommp	*dmm;
	unsigned int	 rdomain = 0;
	struct srp_ref	 sr;

	dmm = srp_enter(&sr, &afmap[0]);
	if (rtableid < dmm->limit)
		rdomain = (dmm->value[rtableid] & RT_TABLEID_MASK);
	srp_leave(&sr);

	return (rdomain);
}

unsigned int
rtable_loindex(unsigned int rtableid)
{
	struct dommp	*dmm;
	unsigned int	 loifidx = 0;
	struct srp_ref	 sr;

	dmm = srp_enter(&sr, &afmap[0]);
	if (rtableid < dmm->limit)
		loifidx = (dmm->value[rtableid] >> RT_TABLEID_BITS);
	srp_leave(&sr);

	return (loifidx);
}

void
rtable_l2set(unsigned int rtableid, unsigned int rdomain, unsigned int loifidx)
{
	struct dommp	*dmm;
	unsigned int	 value;

	KERNEL_ASSERT_LOCKED();

	if (!rtable_exists(rtableid) || !rtable_exists(rdomain))
		return;

	value = (rdomain & RT_TABLEID_MASK) | (loifidx << RT_TABLEID_BITS);

	dmm = srp_get_locked(&afmap[0]);
	dmm->value[rtableid] = value;
}


static inline const uint8_t *satoaddr(struct rtable *,
    const struct sockaddr *);

void	rtable_mpath_insert(struct art_node *, struct rtentry *);

void
rtable_init_backend(void)
{
	art_boot();
}

struct rtable *
rtable_alloc(unsigned int rtableid, unsigned int alen, unsigned int off)
{
	struct rtable *tbl;

	tbl = malloc(sizeof(*tbl), M_RTABLE, M_NOWAIT|M_ZERO);
	if (tbl == NULL)
		return (NULL);

	tbl->r_art = art_alloc(alen);
	if (tbl->r_art == NULL) {
		free(tbl, M_RTABLE, sizeof(*tbl));
		return (NULL);
	}

	rw_init(&tbl->r_lock, "rtable");
	tbl->r_off = off;
	tbl->r_source = NULL;

	return (tbl);
}

int
rtable_setsource(unsigned int rtableid, int af, struct sockaddr *src)
{
	struct rtable		*tbl;

	NET_ASSERT_LOCKED_EXCLUSIVE();

	tbl = rtable_get(rtableid, af);
	if (tbl == NULL)
		return (EAFNOSUPPORT);

	tbl->r_source = src;

	return (0);
}

struct sockaddr *
rtable_getsource(unsigned int rtableid, int af)
{
	struct rtable		*tbl;

	NET_ASSERT_LOCKED();

	tbl = rtable_get(rtableid, af);
	if (tbl == NULL)
		return (NULL);

	return (tbl->r_source);
}

void
rtable_clearsource(unsigned int rtableid, struct sockaddr *src)
{
	struct sockaddr	*addr;

	addr = rtable_getsource(rtableid, src->sa_family);
	if (addr && (addr->sa_len == src->sa_len)) {
		if (memcmp(src, addr, addr->sa_len) == 0) {
			rtable_setsource(rtableid, src->sa_family, NULL);
		}
	}
}

struct rtentry *
rtable_lookup(unsigned int rtableid, const struct sockaddr *dst,
    const struct sockaddr *mask, const struct sockaddr *gateway, uint8_t prio)
{
	struct rtable			*tbl;
	struct art_node			*an;
	struct rtentry			*rt = NULL;
	const uint8_t			*addr;
	int				 plen;

	tbl = rtable_get(rtableid, dst->sa_family);
	if (tbl == NULL)
		return (NULL);

	addr = satoaddr(tbl, dst);

	smr_read_enter();
	if (mask == NULL) {
		/* No need for a perfect match. */
		an = art_match(tbl->r_art, addr);
	} else {
		plen = rtable_satoplen(dst->sa_family, mask);
		if (plen == -1)
			goto out;

		an = art_lookup(tbl->r_art, addr, plen);
	}
	if (an == NULL)
		goto out;

	for (rt = SMR_PTR_GET(&an->an_value); rt != NULL;
	    rt = SMR_PTR_GET(&rt->rt_next)) {
		if (prio != RTP_ANY &&
		    (rt->rt_priority & RTP_MASK) != (prio & RTP_MASK))
			continue;

		if (gateway == NULL)
			break;

		if (rt->rt_gateway->sa_len == gateway->sa_len &&
		    memcmp(rt->rt_gateway, gateway, gateway->sa_len) == 0)
			break;
	}
	if (rt != NULL)
		rtref(rt);

out:
	smr_read_leave();

	return (rt);
}

struct rtentry *
rtable_match(unsigned int rtableid, const struct sockaddr *dst, uint32_t *src)
{
	struct rtable			*tbl;
	struct art_node			*an;
	struct rtentry			*rt = NULL;
	const uint8_t			*addr;
	int				 hash;
	uint8_t				 prio;

	tbl = rtable_get(rtableid, dst->sa_family);
	if (tbl == NULL)
		return (NULL);

	addr = satoaddr(tbl, dst);

	smr_read_enter();
	an = art_match(tbl->r_art, addr);
	if (an == NULL)
		goto out;

	rt = SMR_PTR_GET(&an->an_value);
	KASSERT(rt != NULL);
	prio = rt->rt_priority;

	/* Gateway selection by Hash-Threshold (RFC 2992) */
	if ((hash = rt_hash(rt, dst, src)) != -1) {
		struct rtentry		*mrt;
		int			 threshold, npaths = 1;

		KASSERT(hash <= 0xffff);

		/* Only count nexthops with the same priority. */
		mrt = rt;
		while ((mrt = SMR_PTR_GET(&mrt->rt_next)) != NULL) {
			if (mrt->rt_priority == prio)
				npaths++;
		}

		threshold = (0xffff / npaths) + 1;

		/*
		 * we have no protection against concurrent modification of the
		 * route list attached to the node, so we won't necessarily
		 * have the same number of routes.  for most modifications,
		 * we'll pick a route that we wouldn't have if we only saw the
		 * list before or after the change.
		 */
		mrt = rt;
		while (hash > threshold) {
			if (mrt->rt_priority == prio) {
				rt = mrt;
				hash -= threshold;
			}
			mrt = SMR_PTR_GET(&mrt->rt_next);
			if (mrt == NULL)
				break;
		}
	}
	rtref(rt);
out:
	smr_read_leave();
	return (rt);
}

int
rtable_insert(unsigned int rtableid, struct sockaddr *dst,
    const struct sockaddr *mask, const struct sockaddr *gateway, uint8_t prio,
    struct rtentry *rt)
{
	struct rtable			*tbl;
	struct art_node			*an, *prev;
	const uint8_t			*addr;
	int				 plen;
	unsigned int			 rt_flags;
	int				 error = 0;

	tbl = rtable_get(rtableid, dst->sa_family);
	if (tbl == NULL)
		return (EAFNOSUPPORT);

	addr = satoaddr(tbl, dst);
	plen = rtable_satoplen(dst->sa_family, mask);
	if (plen == -1)
		return (EINVAL);

	an = art_get(addr, plen);
	if (an == NULL)
		return (ENOMEM);

	/* prepare for immediate operation if insert succeeds */
	rt_flags = rt->rt_flags;
	rt->rt_flags &= ~RTF_MPATH;
	rt->rt_dest = dst;
	rt->rt_plen = plen;
	rt->rt_next = NULL;

	rtref(rt); /* take a ref for the table */
	an->an_value = rt;

	rw_enter_write(&tbl->r_lock);
	prev = art_insert(tbl->r_art, an);
	if (prev == NULL) {
		error = ENOMEM;
		goto put;
	}

	if (prev != an) {
		struct rtentry *mrt;
		int mpathok = ISSET(rt_flags, RTF_MPATH);
		int mpath = 0;

		/*
		 * An ART node with the same destination/netmask already
		 * exists.
		 */
		art_put(an);
		an = prev;

		/* Do not permit exactly the same dst/mask/gw pair. */
		for (mrt = SMR_PTR_GET_LOCKED(&an->an_value);
		     mrt != NULL;
		     mrt = SMR_PTR_GET_LOCKED(&mrt->rt_next)) {
			if (prio != RTP_ANY &&
			    (mrt->rt_priority & RTP_MASK) != (prio & RTP_MASK))
				continue;

			if (!mpathok ||
			    (mrt->rt_gateway->sa_len == gateway->sa_len &&
			    memcmp(mrt->rt_gateway, gateway,
			    gateway->sa_len) == 0)) {
				error = EEXIST;
				goto leave;
			}
			mpath = RTF_MPATH;
		}

		/* The new route can be added to the list. */
		if (mpath) {
			SET(rt->rt_flags, RTF_MPATH);

			for (mrt = SMR_PTR_GET_LOCKED(&an->an_value);
			     mrt != NULL;
			     mrt = SMR_PTR_GET_LOCKED(&mrt->rt_next)) {
				if ((mrt->rt_priority & RTP_MASK) !=
				    (prio & RTP_MASK))
					continue;

				SET(mrt->rt_flags, RTF_MPATH);
			}
		}

		/* Put newly inserted entry at the right place. */
		rtable_mpath_insert(an, rt);
	}
	rw_exit_write(&tbl->r_lock);
	return (error);

put:
	art_put(an);
leave:
	rw_exit_write(&tbl->r_lock);
	rtfree(rt);
	return (error);
}

int
rtable_delete(unsigned int rtableid, const struct sockaddr *dst,
    const struct sockaddr *mask, struct rtentry *rt)
{
	struct rtable			*tbl;
	struct art_node			*an;
	const uint8_t			*addr;
	int				 plen;
	struct rtentry			*mrt;

	tbl = rtable_get(rtableid, dst->sa_family);
	if (tbl == NULL)
		return (EAFNOSUPPORT);

	addr = satoaddr(tbl, dst);
	plen = rtable_satoplen(dst->sa_family, mask);
	if (plen == -1)
		return (EINVAL);

	rw_enter_write(&tbl->r_lock);
	smr_read_enter();
	an = art_lookup(tbl->r_art, addr, plen);
	smr_read_leave();
	if (an == NULL) {
		rw_exit_write(&tbl->r_lock);
		return (ESRCH);
	}

	/* If this is the only route in the list then we can delete the node */
	if (SMR_PTR_GET_LOCKED(&an->an_value) == rt &&
	    SMR_PTR_GET_LOCKED(&rt->rt_next) == NULL) {
		struct art_node *oan;
		oan = art_delete(tbl->r_art, addr, plen);
		if (oan != an)
			panic("art %p changed shape during delete", tbl->r_art);
		art_put(an);
		/*
		 * XXX an and the rt ref could still be alive on other cpus.
		 * this currently works because of the NET_LOCK/KERNEL_LOCK
		 * but should be fixed if we want to do route lookups outside
		 * these locks. - dlg@
		 */
	} else {
		struct rtentry **prt;
		struct rtentry *nrt;
		unsigned int found = 0;
		unsigned int npaths = 0;

		/*
		 * If other multipath route entries are still attached to
		 * this ART node we only have to unlink it.
		 */
 		prt = (struct rtentry **)&an->an_value;
		while ((mrt = SMR_PTR_GET_LOCKED(prt)) != NULL) {
			if (mrt == rt) {
				found = 1;
				SMR_PTR_SET_LOCKED(prt,
				    SMR_PTR_GET_LOCKED(&mrt->rt_next));
			} else if ((mrt->rt_priority & RTP_MASK) ==
			    (rt->rt_priority & RTP_MASK)) {
				npaths++;
				nrt = mrt;
			}
			prt = &mrt->rt_next;
		}
		if (!found)
			panic("removing non-existent route");
		if (npaths == 1)
			CLR(nrt->rt_flags, RTF_MPATH);
	}
	KASSERT(refcnt_read(&rt->rt_refcnt) >= 1);
	rw_exit_write(&tbl->r_lock);
	rtfree(rt);

	return (0);
}

int
rtable_walk(unsigned int rtableid, sa_family_t af, struct rtentry **prt,
    int (*func)(struct rtentry *, void *, unsigned int), void *arg)
{
	struct rtable			*tbl;
	struct art_iter			 ai;
	struct art_node			*an;
	int				 error = 0;

	tbl = rtable_get(rtableid, af);
	if (tbl == NULL)
		return (EAFNOSUPPORT);

	rw_enter_write(&tbl->r_lock);
	ART_FOREACH(an, tbl->r_art, &ai) {
		/*
		 * ART nodes have a list of rtentries.
		 *
		 * art_iter holds references to the topology
		 * so it won't change, but not the an_node or rtentries.
		 */
		struct rtentry *rt = SMR_PTR_GET_LOCKED(&an->an_value);
		rtref(rt);

		rw_exit_write(&tbl->r_lock);
		do {
			struct rtentry *nrt;

			smr_read_enter();
			/* Get ready for the next entry. */
			nrt = SMR_PTR_GET(&rt->rt_next);
			if (nrt != NULL)
				rtref(nrt);
			smr_read_leave();

			error = func(rt, arg, rtableid);
			if (error != 0) {
				if (prt != NULL)
					*prt = rt;
				else
					rtfree(rt);

				if (nrt != NULL)
					rtfree(nrt);

				rw_enter_write(&tbl->r_lock);
				art_iter_close(&ai);
				rw_exit_write(&tbl->r_lock);
				return (error);
			}

			rtfree(rt);
			rt = nrt;
		} while (rt != NULL);
		rw_enter_write(&tbl->r_lock);
	}
	rw_exit_write(&tbl->r_lock);

	return (error);
}

int
rtable_read(unsigned int rtableid, sa_family_t af,
    int (*func)(const struct rtentry *, void *, unsigned int), void *arg)
{
	struct rtable			*tbl;
	struct art_iter			 ai;
	struct art_node			*an;
	int				 error = 0;

	tbl = rtable_get(rtableid, af);
	if (tbl == NULL)
		return (EAFNOSUPPORT);

	rw_enter_write(&tbl->r_lock);
	ART_FOREACH(an, tbl->r_art, &ai) {
		struct rtentry *rt;
		for (rt = SMR_PTR_GET_LOCKED(&an->an_value); rt != NULL;
		    rt = SMR_PTR_GET_LOCKED(&rt->rt_next)) {
			error = func(rt, arg, rtableid);
			if (error != 0) {
				art_iter_close(&ai);
				goto leave;
			}
		}
	}
leave:
	rw_exit_write(&tbl->r_lock);

	return (error);
}

struct rtentry *
rtable_iterate(struct rtentry *rt0)
{
	struct rtentry *rt = NULL;

	smr_read_enter();
	rt = SMR_PTR_GET(&rt0->rt_next);
	if (rt != NULL)
		rtref(rt);
	smr_read_leave();
	rtfree(rt0);
	return (rt);
}

int
rtable_mpath_capable(unsigned int rtableid, sa_family_t af)
{
	return (1);
}

int
rtable_mpath_reprio(unsigned int rtableid, struct sockaddr *dst,
    int plen, uint8_t prio, struct rtentry *rt)
{
	struct rtable			*tbl;
	struct art_node			*an;
	const uint8_t			*addr;
	int				 error = 0;

	tbl = rtable_get(rtableid, dst->sa_family);
	if (tbl == NULL)
		return (EAFNOSUPPORT);

	addr = satoaddr(tbl, dst);

	rw_enter_write(&tbl->r_lock);
	smr_read_enter();
	an = art_lookup(tbl->r_art, addr, plen);
	smr_read_leave();
	if (an == NULL) {
		error = ESRCH;
	} else if (SMR_PTR_GET_LOCKED(&an->an_value) == rt &&
	    SMR_PTR_GET_LOCKED(&rt->rt_next) == NULL) {
		/*
		 * If there's only one entry on the list do not go
		 * through an insert/remove cycle.  This is done to
		 * guarantee that ``an->an_rtlist''  is never empty
		 * when a node is in the tree.
		 */
		rt->rt_priority = prio;
	} else {
		struct rtentry **prt;
		struct rtentry *mrt;

 		prt = (struct rtentry **)&an->an_value;
		while ((mrt = SMR_PTR_GET_LOCKED(prt)) != NULL) {
			if (mrt == rt)
				break;
			prt = &mrt->rt_next;
		}
		KASSERT(mrt != NULL);

		SMR_PTR_SET_LOCKED(prt, SMR_PTR_GET_LOCKED(&rt->rt_next));
		rt->rt_priority = prio;
		rtable_mpath_insert(an, rt);
		error = EAGAIN;
	}
	rw_exit_write(&tbl->r_lock);

	return (error);
}

void
rtable_mpath_insert(struct art_node *an, struct rtentry *rt)
{
	struct rtentry			*mrt, **prt;
	uint8_t				 prio = rt->rt_priority;

	/* Iterate until we find the route to be placed after ``rt''. */

	prt = (struct rtentry **)&an->an_value;
	while ((mrt = SMR_PTR_GET_LOCKED(prt)) != NULL) {
		if (mrt->rt_priority > prio)
			break;

		prt = &mrt->rt_next;
	}

	SMR_PTR_SET_LOCKED(&rt->rt_next, mrt);
	SMR_PTR_SET_LOCKED(prt, rt);
}

/*
 * Return a pointer to the address (key).  This is an heritage from the
 * BSD radix tree needed to skip the non-address fields from the flavor
 * of "struct sockaddr" used by this routing table.
 */
static inline const uint8_t *
satoaddr(struct rtable *tbl, const struct sockaddr *sa)
{
	return (((const uint8_t *)sa) + tbl->r_off);
}

/*
 * Return the prefix length of a mask.
 */
int
rtable_satoplen(sa_family_t af, const struct sockaddr *mask)
{
	const struct domain	*dp;
	uint8_t			*ap, *ep;
	int			 mlen, plen = 0;
	int			 i;

	for (i = 0; (dp = domains[i]) != NULL; i++) {
		if (dp->dom_rtoffset == 0)
			continue;

		if (af == dp->dom_family)
			break;
	}
	if (dp == NULL)
		return (-1);

	/* Host route */
	if (mask == NULL)
		return (dp->dom_maxplen);

	mlen = mask->sa_len;

	/* Default route */
	if (mlen == 0)
		return (0);

	ap = (uint8_t *)((uint8_t *)mask) + dp->dom_rtoffset;
	ep = (uint8_t *)((uint8_t *)mask) + mlen;
	if (ap > ep)
		return (-1);

	/* Trim trailing zeroes. */
	while (ap < ep && ep[-1] == 0)
		ep--;

	if (ap == ep)
		return (0);

	/* "Beauty" adapted from sbin/route/show.c ... */
	while (ap < ep) {
		switch (*ap++) {
		case 0xff:
			plen += 8;
			break;
		case 0xfe:
			plen += 7;
			goto out;
		case 0xfc:
			plen += 6;
			goto out;
		case 0xf8:
			plen += 5;
			goto out;
		case 0xf0:
			plen += 4;
			goto out;
		case 0xe0:
			plen += 3;
			goto out;
		case 0xc0:
			plen += 2;
			goto out;
		case 0x80:
			plen += 1;
			goto out;
		default:
			/* Non contiguous mask. */
			return (-1);
		}
	}

out:
	if (plen > dp->dom_maxplen || ap != ep)
		return -1;

	return (plen);
}
