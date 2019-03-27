/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Chelsio Communications, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_inet.h"
#include "opt_inet6.h"

#ifdef TCP_OFFLOAD
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/fnv_hash.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/socket.h>
#include <sys/sbuf.h>
#include <sys/taskqueue.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/if_vlan_var.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/toecore.h>

#include "common/common.h"
#include "common/t4_msg.h"
#include "tom/t4_tom_l2t.h"
#include "tom/t4_tom.h"

#define VLAN_NONE	0xfff

static inline void
l2t_hold(struct l2t_data *d, struct l2t_entry *e)
{

	if (atomic_fetchadd_int(&e->refcnt, 1) == 0)  /* 0 -> 1 transition */
		atomic_subtract_int(&d->nfree, 1);
}

static inline u_int
l2_hash(struct l2t_data *d, const struct sockaddr *sa, int ifindex)
{
	u_int hash, half = d->l2t_size / 2, start = 0;
	const void *key;
	size_t len;

	KASSERT(sa->sa_family == AF_INET || sa->sa_family == AF_INET6,
	    ("%s: sa %p has unexpected sa_family %d", __func__, sa,
	    sa->sa_family));

	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *sin = (const void *)sa;

		key = &sin->sin_addr;
		len = sizeof(sin->sin_addr);
	} else {
		const struct sockaddr_in6 *sin6 = (const void *)sa;

		key = &sin6->sin6_addr;
		len = sizeof(sin6->sin6_addr);
		start = half;
	}

	hash = fnv_32_buf(key, len, FNV1_32_INIT);
	hash = fnv_32_buf(&ifindex, sizeof(ifindex), hash);
	hash %= half;

	return (hash + start);
}

static inline int
l2_cmp(const struct sockaddr *sa, struct l2t_entry *e)
{

	KASSERT(sa->sa_family == AF_INET || sa->sa_family == AF_INET6,
	    ("%s: sa %p has unexpected sa_family %d", __func__, sa,
	    sa->sa_family));

	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *sin = (const void *)sa;

		return (e->addr[0] != sin->sin_addr.s_addr);
	} else {
		const struct sockaddr_in6 *sin6 = (const void *)sa;

		return (memcmp(&e->addr[0], &sin6->sin6_addr, sizeof(e->addr)));
	}
}

static inline void
l2_store(const struct sockaddr *sa, struct l2t_entry *e)
{

	KASSERT(sa->sa_family == AF_INET || sa->sa_family == AF_INET6,
	    ("%s: sa %p has unexpected sa_family %d", __func__, sa,
	    sa->sa_family));

	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *sin = (const void *)sa;

		e->addr[0] = sin->sin_addr.s_addr;
		e->ipv6 = 0;
	} else {
		const struct sockaddr_in6 *sin6 = (const void *)sa;

		memcpy(&e->addr[0], &sin6->sin6_addr, sizeof(e->addr));
		e->ipv6 = 1;
	}
}

/*
 * Add a WR to an L2T entry's queue of work requests awaiting resolution.
 * Must be called with the entry's lock held.
 */
static inline void
arpq_enqueue(struct l2t_entry *e, struct wrqe *wr)
{
	mtx_assert(&e->lock, MA_OWNED);

	STAILQ_INSERT_TAIL(&e->wr_list, wr, link);
}

static inline void
send_pending(struct adapter *sc, struct l2t_entry *e)
{
	struct wrqe *wr;

	mtx_assert(&e->lock, MA_OWNED);

	while ((wr = STAILQ_FIRST(&e->wr_list)) != NULL) {
		STAILQ_REMOVE_HEAD(&e->wr_list, link);
		t4_wrq_tx(sc, wr);
	}
}

static void
resolution_failed(struct adapter *sc, struct l2t_entry *e)
{
	struct tom_data *td = sc->tom_softc;

	mtx_assert(&e->lock, MA_OWNED);

	mtx_lock(&td->unsent_wr_lock);
	STAILQ_CONCAT(&td->unsent_wr_list, &e->wr_list);
	mtx_unlock(&td->unsent_wr_lock);

	taskqueue_enqueue(taskqueue_thread, &td->reclaim_wr_resources);
}

static void
update_entry(struct adapter *sc, struct l2t_entry *e, uint8_t *lladdr,
    uint16_t vtag)
{

	mtx_assert(&e->lock, MA_OWNED);

	/*
	 * The entry may be in active use (e->refcount > 0) or not.  We update
	 * it even when it's not as this simplifies the case where we decide to
	 * reuse the entry later.
	 */

	if (lladdr == NULL &&
	    (e->state == L2T_STATE_RESOLVING || e->state == L2T_STATE_FAILED)) {
		/*
		 * Never got a valid L2 address for this one.  Just mark it as
		 * failed instead of removing it from the hash (for which we'd
		 * need to wlock the table).
		 */
		e->state = L2T_STATE_FAILED;
		resolution_failed(sc, e);
		return;

	} else if (lladdr == NULL) {

		/* Valid or already-stale entry was deleted (or expired) */

		KASSERT(e->state == L2T_STATE_VALID ||
		    e->state == L2T_STATE_STALE,
		    ("%s: lladdr NULL, state %d", __func__, e->state));

		e->state = L2T_STATE_STALE;

	} else {

		if (e->state == L2T_STATE_RESOLVING ||
		    e->state == L2T_STATE_FAILED ||
		    memcmp(e->dmac, lladdr, ETHER_ADDR_LEN)) {

			/* unresolved -> resolved; or dmac changed */

			memcpy(e->dmac, lladdr, ETHER_ADDR_LEN);
			e->vlan = vtag;
			t4_write_l2e(e, 1);
		}
		e->state = L2T_STATE_VALID;
	}
}

static int
resolve_entry(struct adapter *sc, struct l2t_entry *e)
{
	struct tom_data *td = sc->tom_softc;
	struct toedev *tod = &td->tod;
	struct sockaddr_in sin = {0};
	struct sockaddr_in6 sin6 = {0};
	struct sockaddr *sa;
	uint8_t dmac[ETHER_HDR_LEN];
	uint16_t vtag;
	int rc;

	if (e->ipv6 == 0) {
		sin.sin_family = AF_INET;
		sin.sin_len = sizeof(struct sockaddr_in);
		sin.sin_addr.s_addr = e->addr[0];
		sa = (void *)&sin;
	} else {
		sin6.sin6_family = AF_INET6;
		sin6.sin6_len = sizeof(struct sockaddr_in6);
		memcpy(&sin6.sin6_addr, &e->addr[0], sizeof(e->addr));
		sa = (void *)&sin6;
	}

	vtag = EVL_MAKETAG(VLAN_NONE, 0, 0);
	rc = toe_l2_resolve(tod, e->ifp, sa, dmac, &vtag);
	if (rc == EWOULDBLOCK)
		return (rc);

	mtx_lock(&e->lock);
	update_entry(sc, e, rc == 0 ? dmac : NULL, vtag);
	mtx_unlock(&e->lock);

	return (rc);
}

int
t4_l2t_send_slow(struct adapter *sc, struct wrqe *wr, struct l2t_entry *e)
{

again:
	switch (e->state) {
	case L2T_STATE_STALE:     /* entry is stale, kick off revalidation */

		if (resolve_entry(sc, e) != EWOULDBLOCK)
			goto again;	/* entry updated, re-examine state */

		/* Fall through */

	case L2T_STATE_VALID:     /* fast-path, send the packet on */

		t4_wrq_tx(sc, wr);
		return (0);

	case L2T_STATE_RESOLVING:
	case L2T_STATE_SYNC_WRITE:

		mtx_lock(&e->lock);
		if (e->state != L2T_STATE_SYNC_WRITE &&
		    e->state != L2T_STATE_RESOLVING) {
			/* state changed by the time we got here */
			mtx_unlock(&e->lock);
			goto again;
		}
		arpq_enqueue(e, wr);
		mtx_unlock(&e->lock);

		if (resolve_entry(sc, e) == EWOULDBLOCK)
			break;

		mtx_lock(&e->lock);
		if (e->state == L2T_STATE_VALID && !STAILQ_EMPTY(&e->wr_list))
			send_pending(sc, e);
		if (e->state == L2T_STATE_FAILED)
			resolution_failed(sc, e);
		mtx_unlock(&e->lock);
		break;

	case L2T_STATE_FAILED:
		return (EHOSTUNREACH);
	}

	return (0);
}

int
do_l2t_write_rpl2(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_l2t_write_rpl *rpl = (const void *)(rss + 1);
	unsigned int tid = GET_TID(rpl);
	unsigned int idx = tid % L2T_SIZE;

	if (__predict_false(rpl->status != CPL_ERR_NONE)) {
		log(LOG_ERR,
		    "Unexpected L2T_WRITE_RPL (%u) for entry at hw_idx %u\n",
		    rpl->status, idx);
		return (EINVAL);
	}

	if (tid & F_SYNC_WR) {
		struct l2t_entry *e = &sc->l2t->l2tab[idx - sc->vres.l2t.start];

		mtx_lock(&e->lock);
		if (e->state != L2T_STATE_SWITCHING) {
			send_pending(sc, e);
			e->state = L2T_STATE_VALID;
		}
		mtx_unlock(&e->lock);
	}

	return (0);
}

/*
 * The TOE wants an L2 table entry that it can use to reach the next hop over
 * the specified port.  Produce such an entry - create one if needed.
 *
 * Note that the ifnet could be a pseudo-device like if_vlan, if_lagg, etc. on
 * top of the real cxgbe interface.
 */
struct l2t_entry *
t4_l2t_get(struct port_info *pi, struct ifnet *ifp, struct sockaddr *sa)
{
	struct l2t_entry *e;
	struct adapter *sc = pi->adapter;
	struct l2t_data *d = sc->l2t;
	u_int hash, smt_idx = pi->port_id;
	uint16_t vid, pcp, vtag;

	KASSERT(sa->sa_family == AF_INET || sa->sa_family == AF_INET6,
	    ("%s: sa %p has unexpected sa_family %d", __func__, sa,
	    sa->sa_family));

	vid = VLAN_NONE;
	pcp = 0;
	if (ifp->if_type == IFT_L2VLAN) {
		VLAN_TAG(ifp, &vid);
		VLAN_PCP(ifp, &pcp);
	} else if (ifp->if_pcp != IFNET_PCP_NONE) {
		vid = 0;
		pcp = ifp->if_pcp;
	}
	vtag = EVL_MAKETAG(vid, pcp, 0);

	hash = l2_hash(d, sa, ifp->if_index);
	rw_wlock(&d->lock);
	for (e = d->l2tab[hash].first; e; e = e->next) {
		if (l2_cmp(sa, e) == 0 && e->ifp == ifp && e->vlan == vtag &&
		    e->smt_idx == smt_idx) {
			l2t_hold(d, e);
			goto done;
		}
	}

	/* Need to allocate a new entry */
	e = t4_alloc_l2e(d);
	if (e) {
		mtx_lock(&e->lock);          /* avoid race with t4_l2t_free */
		e->next = d->l2tab[hash].first;
		d->l2tab[hash].first = e;

		e->state = L2T_STATE_RESOLVING;
		l2_store(sa, e);
		e->ifp = ifp;
		e->smt_idx = smt_idx;
		e->hash = hash;
		e->lport = pi->lport;
		e->wrq = &sc->sge.ctrlq[pi->port_id];
		e->iqid = sc->sge.ofld_rxq[pi->vi[0].first_ofld_rxq].iq.abs_id;
		atomic_store_rel_int(&e->refcnt, 1);
		e->vlan = vtag;
		mtx_unlock(&e->lock);
	}
done:
	rw_wunlock(&d->lock);
	return e;
}

/*
 * Called when the host's ARP layer makes a change to some entry that is loaded
 * into the HW L2 table.
 */
void
t4_l2_update(struct toedev *tod, struct ifnet *ifp, struct sockaddr *sa,
    uint8_t *lladdr, uint16_t vtag)
{
	struct adapter *sc = tod->tod_softc;
	struct l2t_entry *e;
	struct l2t_data *d = sc->l2t;
	u_int hash;

	KASSERT(d != NULL, ("%s: no L2 table", __func__));

	hash = l2_hash(d, sa, ifp->if_index);
	rw_rlock(&d->lock);
	for (e = d->l2tab[hash].first; e; e = e->next) {
		if (l2_cmp(sa, e) == 0 && e->ifp == ifp) {
			mtx_lock(&e->lock);
			if (atomic_load_acq_int(&e->refcnt))
				goto found;
			e->state = L2T_STATE_STALE;
			mtx_unlock(&e->lock);
			break;
		}
	}
	rw_runlock(&d->lock);

	/*
	 * This is of no interest to us.  We've never had an offloaded
	 * connection to this destination, and we aren't attempting one right
	 * now.
	 */
	return;

found:
	rw_runlock(&d->lock);

	KASSERT(e->state != L2T_STATE_UNUSED,
	    ("%s: unused entry in the hash.", __func__));

	update_entry(sc, e, lladdr, vtag);
	mtx_unlock(&e->lock);
}
#endif
