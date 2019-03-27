/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Luigi Rizzo, Alessandro Cerri. All rights reserved.
 * Copyright (c) 2004-2008 Qing Li. All rights reserved.
 * Copyright (c) 2008 Kip Macy. All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>

#ifdef DDB
#include <ddb/ddb.h>
#endif

#include <vm/uma.h>

#include <netinet/in.h>
#include <net/if_llatbl.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_var.h>
#include <net/route.h>
#include <net/vnet.h>
#include <netinet/if_ether.h>
#include <netinet6/in6_var.h>
#include <netinet6/nd6.h>

MALLOC_DEFINE(M_LLTABLE, "lltable", "link level address tables");

VNET_DEFINE_STATIC(SLIST_HEAD(, lltable), lltables) =
    SLIST_HEAD_INITIALIZER(lltables);
#define	V_lltables	VNET(lltables)

static struct rwlock lltable_list_lock;
RW_SYSINIT(lltable_list_lock, &lltable_list_lock, "lltable_list_lock");
#define	LLTABLE_LIST_RLOCK()		rw_rlock(&lltable_list_lock)
#define	LLTABLE_LIST_RUNLOCK()		rw_runlock(&lltable_list_lock)
#define	LLTABLE_LIST_WLOCK()		rw_wlock(&lltable_list_lock)
#define	LLTABLE_LIST_WUNLOCK()		rw_wunlock(&lltable_list_lock)
#define	LLTABLE_LIST_LOCK_ASSERT()	rw_assert(&lltable_list_lock, RA_LOCKED)

static void lltable_unlink(struct lltable *llt);
static void llentries_unlink(struct lltable *llt, struct llentries *head);

static void htable_unlink_entry(struct llentry *lle);
static void htable_link_entry(struct lltable *llt, struct llentry *lle);
static int htable_foreach_lle(struct lltable *llt, llt_foreach_cb_t *f,
    void *farg);

/*
 * Dump lle state for a specific address family.
 */
static int
lltable_dump_af(struct lltable *llt, struct sysctl_req *wr)
{
	struct epoch_tracker et;
	int error;

	LLTABLE_LIST_LOCK_ASSERT();

	if (llt->llt_ifp->if_flags & IFF_LOOPBACK)
		return (0);
	error = 0;

	NET_EPOCH_ENTER(et);
	error = lltable_foreach_lle(llt,
	    (llt_foreach_cb_t *)llt->llt_dump_entry, wr);
	NET_EPOCH_EXIT(et);

	return (error);
}

/*
 * Dump arp state for a specific address family.
 */
int
lltable_sysctl_dumparp(int af, struct sysctl_req *wr)
{
	struct lltable *llt;
	int error = 0;

	LLTABLE_LIST_RLOCK();
	SLIST_FOREACH(llt, &V_lltables, llt_link) {
		if (llt->llt_af == af) {
			error = lltable_dump_af(llt, wr);
			if (error != 0)
				goto done;
		}
	}
done:
	LLTABLE_LIST_RUNLOCK();
	return (error);
}

/*
 * Common function helpers for chained hash table.
 */

/*
 * Runs specified callback for each entry in @llt.
 * Caller does the locking.
 *
 */
static int
htable_foreach_lle(struct lltable *llt, llt_foreach_cb_t *f, void *farg)
{
	struct llentry *lle, *next;
	int i, error;

	error = 0;

	for (i = 0; i < llt->llt_hsize; i++) {
		CK_LIST_FOREACH_SAFE(lle, &llt->lle_head[i], lle_next, next) {
			error = f(llt, lle, farg);
			if (error != 0)
				break;
		}
	}

	return (error);
}

static void
htable_link_entry(struct lltable *llt, struct llentry *lle)
{
	struct llentries *lleh;
	uint32_t hashidx;

	if ((lle->la_flags & LLE_LINKED) != 0)
		return;

	IF_AFDATA_WLOCK_ASSERT(llt->llt_ifp);

	hashidx = llt->llt_hash(lle, llt->llt_hsize);
	lleh = &llt->lle_head[hashidx];

	lle->lle_tbl  = llt;
	lle->lle_head = lleh;
	lle->la_flags |= LLE_LINKED;
	CK_LIST_INSERT_HEAD(lleh, lle, lle_next);
}

static void
htable_unlink_entry(struct llentry *lle)
{

	if ((lle->la_flags & LLE_LINKED) != 0) {
		IF_AFDATA_WLOCK_ASSERT(lle->lle_tbl->llt_ifp);
		CK_LIST_REMOVE(lle, lle_next);
		lle->la_flags &= ~(LLE_VALID | LLE_LINKED);
#if 0
		lle->lle_tbl = NULL;
		lle->lle_head = NULL;
#endif
	}
}

struct prefix_match_data {
	const struct sockaddr *addr;
	const struct sockaddr *mask;
	struct llentries dchain;
	u_int flags;
};

static int
htable_prefix_free_cb(struct lltable *llt, struct llentry *lle, void *farg)
{
	struct prefix_match_data *pmd;

	pmd = (struct prefix_match_data *)farg;

	if (llt->llt_match_prefix(pmd->addr, pmd->mask, pmd->flags, lle)) {
		LLE_WLOCK(lle);
		CK_LIST_INSERT_HEAD(&pmd->dchain, lle, lle_chain);
	}

	return (0);
}

static void
htable_prefix_free(struct lltable *llt, const struct sockaddr *addr,
    const struct sockaddr *mask, u_int flags)
{
	struct llentry *lle, *next;
	struct prefix_match_data pmd;

	bzero(&pmd, sizeof(pmd));
	pmd.addr = addr;
	pmd.mask = mask;
	pmd.flags = flags;
	CK_LIST_INIT(&pmd.dchain);

	IF_AFDATA_WLOCK(llt->llt_ifp);
	/* Push matching lles to chain */
	lltable_foreach_lle(llt, htable_prefix_free_cb, &pmd);

	llentries_unlink(llt, &pmd.dchain);
	IF_AFDATA_WUNLOCK(llt->llt_ifp);

	CK_LIST_FOREACH_SAFE(lle, &pmd.dchain, lle_chain, next)
		lltable_free_entry(llt, lle);
}

static void
htable_free_tbl(struct lltable *llt)
{

	free(llt->lle_head, M_LLTABLE);
	free(llt, M_LLTABLE);
}

static void
llentries_unlink(struct lltable *llt, struct llentries *head)
{
	struct llentry *lle, *next;

	CK_LIST_FOREACH_SAFE(lle, head, lle_chain, next)
		llt->llt_unlink_entry(lle);
}

/*
 * Helper function used to drop all mbufs in hold queue.
 *
 * Returns the number of held packets, if any, that were dropped.
 */
size_t
lltable_drop_entry_queue(struct llentry *lle)
{
	size_t pkts_dropped;
	struct mbuf *next;

	LLE_WLOCK_ASSERT(lle);

	pkts_dropped = 0;
	while ((lle->la_numheld > 0) && (lle->la_hold != NULL)) {
		next = lle->la_hold->m_nextpkt;
		m_freem(lle->la_hold);
		lle->la_hold = next;
		lle->la_numheld--;
		pkts_dropped++;
	}

	KASSERT(lle->la_numheld == 0,
		("%s: la_numheld %d > 0, pkts_droped %zd", __func__,
		 lle->la_numheld, pkts_dropped));

	return (pkts_dropped);
}

void
lltable_set_entry_addr(struct ifnet *ifp, struct llentry *lle,
    const char *linkhdr, size_t linkhdrsize, int lladdr_off)
{

	memcpy(lle->r_linkdata, linkhdr, linkhdrsize);
	lle->r_hdrlen = linkhdrsize;
	lle->ll_addr = &lle->r_linkdata[lladdr_off];
	lle->la_flags |= LLE_VALID;
	lle->r_flags |= RLLE_VALID;
}

/*
 * Tries to update @lle link-level address.
 * Since update requires AFDATA WLOCK, function
 * drops @lle lock, acquires AFDATA lock and then acquires
 * @lle lock to maintain lock order.
 *
 * Returns 1 on success.
 */
int
lltable_try_set_entry_addr(struct ifnet *ifp, struct llentry *lle,
    const char *linkhdr, size_t linkhdrsize, int lladdr_off)
{

	/* Perform real LLE update */
	/* use afdata WLOCK to update fields */
	LLE_WLOCK_ASSERT(lle);
	LLE_ADDREF(lle);
	LLE_WUNLOCK(lle);
	IF_AFDATA_WLOCK(ifp);
	LLE_WLOCK(lle);

	/*
	 * Since we droppped LLE lock, other thread might have deleted
	 * this lle. Check and return
	 */
	if ((lle->la_flags & LLE_DELETED) != 0) {
		IF_AFDATA_WUNLOCK(ifp);
		LLE_FREE_LOCKED(lle);
		return (0);
	}

	/* Update data */
	lltable_set_entry_addr(ifp, lle, linkhdr, linkhdrsize, lladdr_off);

	IF_AFDATA_WUNLOCK(ifp);

	LLE_REMREF(lle);

	return (1);
}

 /*
 * Helper function used to pre-compute full/partial link-layer
 * header data suitable for feeding into if_output().
 */
int
lltable_calc_llheader(struct ifnet *ifp, int family, char *lladdr,
    char *buf, size_t *bufsize, int *lladdr_off)
{
	struct if_encap_req ereq;
	int error;

	bzero(buf, *bufsize);
	bzero(&ereq, sizeof(ereq));
	ereq.buf = buf;
	ereq.bufsize = *bufsize;
	ereq.rtype = IFENCAP_LL;
	ereq.family = family;
	ereq.lladdr = lladdr;
	ereq.lladdr_len = ifp->if_addrlen;
	error = ifp->if_requestencap(ifp, &ereq);
	if (error == 0) {
		*bufsize = ereq.bufsize;
		*lladdr_off = ereq.lladdr_off;
	}

	return (error);
}

/*
 * Update link-layer header for given @lle after
 * interface lladdr was changed.
 */
static int
llentry_update_ifaddr(struct lltable *llt, struct llentry *lle, void *farg)
{
	struct ifnet *ifp;
	u_char linkhdr[LLE_MAX_LINKHDR];
	size_t linkhdrsize;
	u_char *lladdr;
	int lladdr_off;

	ifp = (struct ifnet *)farg;

	lladdr = lle->ll_addr;

	LLE_WLOCK(lle);
	if ((lle->la_flags & LLE_VALID) == 0) {
		LLE_WUNLOCK(lle);
		return (0);
	}

	if ((lle->la_flags & LLE_IFADDR) != 0)
		lladdr = IF_LLADDR(ifp);

	linkhdrsize = sizeof(linkhdr);
	lltable_calc_llheader(ifp, llt->llt_af, lladdr, linkhdr, &linkhdrsize,
	    &lladdr_off);
	memcpy(lle->r_linkdata, linkhdr, linkhdrsize);
	LLE_WUNLOCK(lle);

	return (0);
}

/*
 * Update all calculated headers for given @llt
 */
void
lltable_update_ifaddr(struct lltable *llt)
{

	if (llt->llt_ifp->if_flags & IFF_LOOPBACK)
		return;

	IF_AFDATA_WLOCK(llt->llt_ifp);
	lltable_foreach_lle(llt, llentry_update_ifaddr, llt->llt_ifp);
	IF_AFDATA_WUNLOCK(llt->llt_ifp);
}

/*
 *
 * Performs generic cleanup routines and frees lle.
 *
 * Called for non-linked entries, with callouts and
 * other AF-specific cleanups performed.
 *
 * @lle must be passed WLOCK'ed
 *
 * Returns the number of held packets, if any, that were dropped.
 */
size_t
llentry_free(struct llentry *lle)
{
	size_t pkts_dropped;

	LLE_WLOCK_ASSERT(lle);

	KASSERT((lle->la_flags & LLE_LINKED) == 0, ("freeing linked lle"));

	pkts_dropped = lltable_drop_entry_queue(lle);

	/* cancel timer */
	if (callout_stop(&lle->lle_timer) > 0)
		LLE_REMREF(lle);
	LLE_FREE_LOCKED(lle);

	return (pkts_dropped);
}

/*
 * (al)locate an llentry for address dst (equivalent to rtalloc for new-arp).
 *
 * If found the llentry * is returned referenced and unlocked.
 */
struct llentry *
llentry_alloc(struct ifnet *ifp, struct lltable *lt,
    struct sockaddr_storage *dst)
{
	struct epoch_tracker et;
	struct llentry *la, *la_tmp;

	NET_EPOCH_ENTER(et);
	la = lla_lookup(lt, LLE_EXCLUSIVE, (struct sockaddr *)dst);
	NET_EPOCH_EXIT(et);

	if (la != NULL) {
		LLE_ADDREF(la);
		LLE_WUNLOCK(la);
		return (la);
	}

	if ((ifp->if_flags & (IFF_NOARP | IFF_STATICARP)) == 0) {
		la = lltable_alloc_entry(lt, 0, (struct sockaddr *)dst);
		if (la == NULL)
			return (NULL);
		IF_AFDATA_WLOCK(ifp);
		LLE_WLOCK(la);
		/* Prefer any existing LLE over newly-created one */
		la_tmp = lla_lookup(lt, LLE_EXCLUSIVE, (struct sockaddr *)dst);
		if (la_tmp == NULL)
			lltable_link_entry(lt, la);
		IF_AFDATA_WUNLOCK(ifp);
		if (la_tmp != NULL) {
			lltable_free_entry(lt, la);
			la = la_tmp;
		}
		LLE_ADDREF(la);
		LLE_WUNLOCK(la);
	}

	return (la);
}

/*
 * Free all entries from given table and free itself.
 */

static int
lltable_free_cb(struct lltable *llt, struct llentry *lle, void *farg)
{
	struct llentries *dchain;

	dchain = (struct llentries *)farg;

	LLE_WLOCK(lle);
	CK_LIST_INSERT_HEAD(dchain, lle, lle_chain);

	return (0);
}

/*
 * Free all entries from given table and free itself.
 */
void
lltable_free(struct lltable *llt)
{
	struct llentry *lle, *next;
	struct llentries dchain;

	KASSERT(llt != NULL, ("%s: llt is NULL", __func__));

	lltable_unlink(llt);

	CK_LIST_INIT(&dchain);
	IF_AFDATA_WLOCK(llt->llt_ifp);
	/* Push all lles to @dchain */
	lltable_foreach_lle(llt, lltable_free_cb, &dchain);
	llentries_unlink(llt, &dchain);
	IF_AFDATA_WUNLOCK(llt->llt_ifp);

	CK_LIST_FOREACH_SAFE(lle, &dchain, lle_chain, next) {
		llentry_free(lle);
	}

	llt->llt_free_tbl(llt);
}

#if 0
void
lltable_drain(int af)
{
	struct lltable	*llt;
	struct llentry	*lle;
	int i;

	LLTABLE_LIST_RLOCK();
	SLIST_FOREACH(llt, &V_lltables, llt_link) {
		if (llt->llt_af != af)
			continue;

		for (i=0; i < llt->llt_hsize; i++) {
			CK_LIST_FOREACH(lle, &llt->lle_head[i], lle_next) {
				LLE_WLOCK(lle);
				if (lle->la_hold) {
					m_freem(lle->la_hold);
					lle->la_hold = NULL;
				}
				LLE_WUNLOCK(lle);
			}
		}
	}
	LLTABLE_LIST_RUNLOCK();
}
#endif

/*
 * Deletes an address from given lltable.
 * Used for userland interaction to remove
 * individual entries. Skips entries added by OS.
 */
int
lltable_delete_addr(struct lltable *llt, u_int flags,
    const struct sockaddr *l3addr)
{
	struct llentry *lle;
	struct ifnet *ifp;

	ifp = llt->llt_ifp;
	IF_AFDATA_WLOCK(ifp);
	lle = lla_lookup(llt, LLE_EXCLUSIVE, l3addr);

	if (lle == NULL) {
		IF_AFDATA_WUNLOCK(ifp);
		return (ENOENT);
	}
	if ((lle->la_flags & LLE_IFADDR) != 0 && (flags & LLE_IFADDR) == 0) {
		IF_AFDATA_WUNLOCK(ifp);
		LLE_WUNLOCK(lle);
		return (EPERM);
	}

	lltable_unlink_entry(llt, lle);
	IF_AFDATA_WUNLOCK(ifp);

	llt->llt_delete_entry(llt, lle);

	return (0);
}

void
lltable_prefix_free(int af, struct sockaddr *addr, struct sockaddr *mask,
    u_int flags)
{
	struct lltable *llt;

	LLTABLE_LIST_RLOCK();
	SLIST_FOREACH(llt, &V_lltables, llt_link) {
		if (llt->llt_af != af)
			continue;

		llt->llt_prefix_free(llt, addr, mask, flags);
	}
	LLTABLE_LIST_RUNLOCK();
}

struct lltable *
lltable_allocate_htbl(uint32_t hsize)
{
	struct lltable *llt;
	int i;

	llt = malloc(sizeof(struct lltable), M_LLTABLE, M_WAITOK | M_ZERO);
	llt->llt_hsize = hsize;
	llt->lle_head = malloc(sizeof(struct llentries) * hsize,
	    M_LLTABLE, M_WAITOK | M_ZERO);

	for (i = 0; i < llt->llt_hsize; i++)
		CK_LIST_INIT(&llt->lle_head[i]);

	/* Set some default callbacks */
	llt->llt_link_entry = htable_link_entry;
	llt->llt_unlink_entry = htable_unlink_entry;
	llt->llt_prefix_free = htable_prefix_free;
	llt->llt_foreach_entry = htable_foreach_lle;
	llt->llt_free_tbl = htable_free_tbl;

	return (llt);
}

/*
 * Links lltable to global llt list.
 */
void
lltable_link(struct lltable *llt)
{

	LLTABLE_LIST_WLOCK();
	SLIST_INSERT_HEAD(&V_lltables, llt, llt_link);
	LLTABLE_LIST_WUNLOCK();
}

static void
lltable_unlink(struct lltable *llt)
{

	LLTABLE_LIST_WLOCK();
	SLIST_REMOVE(&V_lltables, llt, lltable, llt_link);
	LLTABLE_LIST_WUNLOCK();

}

/*
 * External methods used by lltable consumers
 */

int
lltable_foreach_lle(struct lltable *llt, llt_foreach_cb_t *f, void *farg)
{

	return (llt->llt_foreach_entry(llt, f, farg));
}

struct llentry *
lltable_alloc_entry(struct lltable *llt, u_int flags,
    const struct sockaddr *l3addr)
{

	return (llt->llt_alloc_entry(llt, flags, l3addr));
}

void
lltable_free_entry(struct lltable *llt, struct llentry *lle)
{

	llt->llt_free_entry(llt, lle);
}

void
lltable_link_entry(struct lltable *llt, struct llentry *lle)
{

	llt->llt_link_entry(llt, lle);
}

void
lltable_unlink_entry(struct lltable *llt, struct llentry *lle)
{

	llt->llt_unlink_entry(lle);
}

void
lltable_fill_sa_entry(const struct llentry *lle, struct sockaddr *sa)
{
	struct lltable *llt;

	llt = lle->lle_tbl;
	llt->llt_fill_sa_entry(lle, sa);
}

struct ifnet *
lltable_get_ifp(const struct lltable *llt)
{

	return (llt->llt_ifp);
}

int
lltable_get_af(const struct lltable *llt)
{

	return (llt->llt_af);
}

/*
 * Called in route_output when rtm_flags contains RTF_LLDATA.
 */
int
lla_rt_output(struct rt_msghdr *rtm, struct rt_addrinfo *info)
{
	struct sockaddr_dl *dl =
	    (struct sockaddr_dl *)info->rti_info[RTAX_GATEWAY];
	struct sockaddr *dst = (struct sockaddr *)info->rti_info[RTAX_DST];
	struct ifnet *ifp;
	struct lltable *llt;
	struct llentry *lle, *lle_tmp;
	uint8_t linkhdr[LLE_MAX_LINKHDR];
	size_t linkhdrsize;
	int lladdr_off;
	u_int laflags = 0;
	int error;

	KASSERT(dl != NULL && dl->sdl_family == AF_LINK,
	    ("%s: invalid dl\n", __func__));

	ifp = ifnet_byindex(dl->sdl_index);
	if (ifp == NULL) {
		log(LOG_INFO, "%s: invalid ifp (sdl_index %d)\n",
		    __func__, dl->sdl_index);
		return EINVAL;
	}

	/* XXX linked list may be too expensive */
	LLTABLE_LIST_RLOCK();
	SLIST_FOREACH(llt, &V_lltables, llt_link) {
		if (llt->llt_af == dst->sa_family &&
		    llt->llt_ifp == ifp)
			break;
	}
	LLTABLE_LIST_RUNLOCK();
	KASSERT(llt != NULL, ("Yep, ugly hacks are bad\n"));

	error = 0;

	switch (rtm->rtm_type) {
	case RTM_ADD:
		/* Add static LLE */
		laflags = 0;
		if (rtm->rtm_rmx.rmx_expire == 0)
			laflags = LLE_STATIC;
		lle = lltable_alloc_entry(llt, laflags, dst);
		if (lle == NULL)
			return (ENOMEM);

		linkhdrsize = sizeof(linkhdr);
		if (lltable_calc_llheader(ifp, dst->sa_family, LLADDR(dl),
		    linkhdr, &linkhdrsize, &lladdr_off) != 0)
			return (EINVAL);
		lltable_set_entry_addr(ifp, lle, linkhdr, linkhdrsize,
		    lladdr_off);
		if ((rtm->rtm_flags & RTF_ANNOUNCE))
			lle->la_flags |= LLE_PUB;
		lle->la_expire = rtm->rtm_rmx.rmx_expire;

		laflags = lle->la_flags;

		/* Try to link new entry */
		lle_tmp = NULL;
		IF_AFDATA_WLOCK(ifp);
		LLE_WLOCK(lle);
		lle_tmp = lla_lookup(llt, LLE_EXCLUSIVE, dst);
		if (lle_tmp != NULL) {
			/* Check if we are trying to replace immutable entry */
			if ((lle_tmp->la_flags & LLE_IFADDR) != 0) {
				IF_AFDATA_WUNLOCK(ifp);
				LLE_WUNLOCK(lle_tmp);
				lltable_free_entry(llt, lle);
				return (EPERM);
			}
			/* Unlink existing entry from table */
			lltable_unlink_entry(llt, lle_tmp);
		}
		lltable_link_entry(llt, lle);
		IF_AFDATA_WUNLOCK(ifp);

		if (lle_tmp != NULL) {
			EVENTHANDLER_INVOKE(lle_event, lle_tmp,LLENTRY_EXPIRED);
			lltable_free_entry(llt, lle_tmp);
		}

		/*
		 * By invoking LLE handler here we might get
		 * two events on static LLE entry insertion
		 * in routing socket. However, since we might have
		 * other subscribers we need to generate this event.
		 */
		EVENTHANDLER_INVOKE(lle_event, lle, LLENTRY_RESOLVED);
		LLE_WUNLOCK(lle);
#ifdef INET
		/* gratuitous ARP */
		if ((laflags & LLE_PUB) && dst->sa_family == AF_INET)
			arprequest(ifp,
			    &((struct sockaddr_in *)dst)->sin_addr,
			    &((struct sockaddr_in *)dst)->sin_addr,
			    (u_char *)LLADDR(dl));
#endif

		break;

	case RTM_DELETE:
		return (lltable_delete_addr(llt, 0, dst));

	default:
		error = EINVAL;
	}

	return (error);
}

#ifdef DDB
struct llentry_sa {
	struct llentry		base;
	struct sockaddr		l3_addr;
};

static void
llatbl_lle_show(struct llentry_sa *la)
{
	struct llentry *lle;
	uint8_t octet[6];

	lle = &la->base;
	db_printf("lle=%p\n", lle);
	db_printf(" lle_next=%p\n", lle->lle_next.cle_next);
	db_printf(" lle_lock=%p\n", &lle->lle_lock);
	db_printf(" lle_tbl=%p\n", lle->lle_tbl);
	db_printf(" lle_head=%p\n", lle->lle_head);
	db_printf(" la_hold=%p\n", lle->la_hold);
	db_printf(" la_numheld=%d\n", lle->la_numheld);
	db_printf(" la_expire=%ju\n", (uintmax_t)lle->la_expire);
	db_printf(" la_flags=0x%04x\n", lle->la_flags);
	db_printf(" la_asked=%u\n", lle->la_asked);
	db_printf(" la_preempt=%u\n", lle->la_preempt);
	db_printf(" ln_state=%d\n", lle->ln_state);
	db_printf(" ln_router=%u\n", lle->ln_router);
	db_printf(" ln_ntick=%ju\n", (uintmax_t)lle->ln_ntick);
	db_printf(" lle_refcnt=%d\n", lle->lle_refcnt);
	bcopy(lle->ll_addr, octet, sizeof(octet));
	db_printf(" ll_addr=%02x:%02x:%02x:%02x:%02x:%02x\n",
	    octet[0], octet[1], octet[2], octet[3], octet[4], octet[5]);
	db_printf(" lle_timer=%p\n", &lle->lle_timer);

	switch (la->l3_addr.sa_family) {
#ifdef INET
	case AF_INET:
	{
		struct sockaddr_in *sin;
		char l3s[INET_ADDRSTRLEN];

		sin = (struct sockaddr_in *)&la->l3_addr;
		inet_ntoa_r(sin->sin_addr, l3s);
		db_printf(" l3_addr=%s\n", l3s);
		break;
	}
#endif
#ifdef INET6
	case AF_INET6:
	{
		struct sockaddr_in6 *sin6;
		char l3s[INET6_ADDRSTRLEN];

		sin6 = (struct sockaddr_in6 *)&la->l3_addr;
		ip6_sprintf(l3s, &sin6->sin6_addr);
		db_printf(" l3_addr=%s\n", l3s);
		break;
	}
#endif
	default:
		db_printf(" l3_addr=N/A (af=%d)\n", la->l3_addr.sa_family);
		break;
	}
}

DB_SHOW_COMMAND(llentry, db_show_llentry)
{

	if (!have_addr) {
		db_printf("usage: show llentry <struct llentry *>\n");
		return;
	}

	llatbl_lle_show((struct llentry_sa *)addr);
}

static void
llatbl_llt_show(struct lltable *llt)
{
	int i;
	struct llentry *lle;

	db_printf("llt=%p llt_af=%d llt_ifp=%p\n",
	    llt, llt->llt_af, llt->llt_ifp);

	for (i = 0; i < llt->llt_hsize; i++) {
		CK_LIST_FOREACH(lle, &llt->lle_head[i], lle_next) {

			llatbl_lle_show((struct llentry_sa *)lle);
			if (db_pager_quit)
				return;
		}
	}
}

DB_SHOW_COMMAND(lltable, db_show_lltable)
{

	if (!have_addr) {
		db_printf("usage: show lltable <struct lltable *>\n");
		return;
	}

	llatbl_llt_show((struct lltable *)addr);
}

DB_SHOW_ALL_COMMAND(lltables, db_show_all_lltables)
{
	VNET_ITERATOR_DECL(vnet_iter);
	struct lltable *llt;

	VNET_FOREACH(vnet_iter) {
		CURVNET_SET_QUIET(vnet_iter);
#ifdef VIMAGE
		db_printf("vnet=%p\n", curvnet);
#endif
		SLIST_FOREACH(llt, &V_lltables, llt_link) {
			db_printf("llt=%p llt_af=%d llt_ifp=%p(%s)\n",
			    llt, llt->llt_af, llt->llt_ifp,
			    (llt->llt_ifp != NULL) ?
				llt->llt_ifp->if_xname : "?");
			if (have_addr && addr != 0) /* verbose */
				llatbl_llt_show(llt);
			if (db_pager_quit) {
				CURVNET_RESTORE();
				return;
			}
		}
		CURVNET_RESTORE();
	}
}
#endif
