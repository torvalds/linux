/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2014-2018 Giuseppe Lettieri
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
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

/* $FreeBSD$ */

#if defined(__FreeBSD__)
#include <sys/cdefs.h> /* prerequisite */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>	/* defines used in kernel.h */
#include <sys/kernel.h>	/* types used in module initialization */
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/socket.h> /* sockaddrs */
#include <net/if.h>
#include <net/if_var.h>
#include <machine/bus.h>	/* bus_dmamap_* */
#include <sys/refcount.h>


#elif defined(linux)

#include "bsd_glue.h"

#elif defined(__APPLE__)

#warning OSX support is only partial
#include "osx_glue.h"

#elif defined(_WIN32)
#include "win_glue.h"

#else

#error	Unsupported platform

#endif /* unsupported */

/*
 * common headers
 */

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_mem2.h>

#ifdef WITH_PIPES

#define NM_PIPE_MAXSLOTS	4096
#define NM_PIPE_MAXRINGS	256

static int netmap_default_pipes = 0; /* ignored, kept for compatibility */
SYSBEGIN(vars_pipes);
SYSCTL_DECL(_dev_netmap);
SYSCTL_INT(_dev_netmap, OID_AUTO, default_pipes, CTLFLAG_RW,
		&netmap_default_pipes, 0, "For compatibility only");
SYSEND;

/* allocate the pipe array in the parent adapter */
static int
nm_pipe_alloc(struct netmap_adapter *na, u_int npipes)
{
	size_t old_len, len;
	struct netmap_pipe_adapter **npa;

	if (npipes <= na->na_max_pipes)
		/* we already have more entries that requested */
		return 0;

	if (npipes < na->na_next_pipe || npipes > NM_MAXPIPES)
		return EINVAL;

	old_len = sizeof(struct netmap_pipe_adapter *)*na->na_max_pipes;
	len = sizeof(struct netmap_pipe_adapter *) * npipes;
	npa = nm_os_realloc(na->na_pipes, len, old_len);
	if (npa == NULL)
		return ENOMEM;

	na->na_pipes = npa;
	na->na_max_pipes = npipes;

	return 0;
}

/* deallocate the parent array in the parent adapter */
void
netmap_pipe_dealloc(struct netmap_adapter *na)
{
	if (na->na_pipes) {
		if (na->na_next_pipe > 0) {
			nm_prerr("freeing not empty pipe array for %s (%d dangling pipes)!",
			    na->name, na->na_next_pipe);
		}
		nm_os_free(na->na_pipes);
		na->na_pipes = NULL;
		na->na_max_pipes = 0;
		na->na_next_pipe = 0;
	}
}

/* find a pipe endpoint with the given id among the parent's pipes */
static struct netmap_pipe_adapter *
netmap_pipe_find(struct netmap_adapter *parent, const char *pipe_id)
{
	int i;
	struct netmap_pipe_adapter *na;

	for (i = 0; i < parent->na_next_pipe; i++) {
		const char *na_pipe_id;
		na = parent->na_pipes[i];
		na_pipe_id = strrchr(na->up.name,
			na->role == NM_PIPE_ROLE_MASTER ? '{' : '}');
		KASSERT(na_pipe_id != NULL, ("Invalid pipe name"));
		++na_pipe_id;
		if (!strcmp(na_pipe_id, pipe_id)) {
			return na;
		}
	}
	return NULL;
}

/* add a new pipe endpoint to the parent array */
static int
netmap_pipe_add(struct netmap_adapter *parent, struct netmap_pipe_adapter *na)
{
	if (parent->na_next_pipe >= parent->na_max_pipes) {
		u_int npipes = parent->na_max_pipes ?  2*parent->na_max_pipes : 2;
		int error = nm_pipe_alloc(parent, npipes);
		if (error)
			return error;
	}

	parent->na_pipes[parent->na_next_pipe] = na;
	na->parent_slot = parent->na_next_pipe;
	parent->na_next_pipe++;
	return 0;
}

/* remove the given pipe endpoint from the parent array */
static void
netmap_pipe_remove(struct netmap_adapter *parent, struct netmap_pipe_adapter *na)
{
	u_int n;
	n = --parent->na_next_pipe;
	if (n != na->parent_slot) {
		struct netmap_pipe_adapter **p =
			&parent->na_pipes[na->parent_slot];
		*p = parent->na_pipes[n];
		(*p)->parent_slot = na->parent_slot;
	}
	parent->na_pipes[n] = NULL;
}

int
netmap_pipe_txsync(struct netmap_kring *txkring, int flags)
{
	struct netmap_kring *rxkring = txkring->pipe;
	u_int k, lim = txkring->nkr_num_slots - 1, nk;
	int m; /* slots to transfer */
	int complete; /* did we see a complete packet ? */
	struct netmap_ring *txring = txkring->ring, *rxring = rxkring->ring;

	nm_prdis("%p: %s %x -> %s", txkring, txkring->name, flags, rxkring->name);
	nm_prdis(20, "TX before: hwcur %d hwtail %d cur %d head %d tail %d",
		txkring->nr_hwcur, txkring->nr_hwtail,
		txkring->rcur, txkring->rhead, txkring->rtail);

	/* update the hwtail */
	txkring->nr_hwtail = txkring->pipe_tail;

	m = txkring->rhead - txkring->nr_hwcur; /* new slots */
	if (m < 0)
		m += txkring->nkr_num_slots;

	if (m == 0) {
		/* nothing to send */
		return 0;
	}

	for (k = txkring->nr_hwcur, nk = lim + 1, complete = 0; m;
			m--, k = nm_next(k, lim), nk = (complete ? k : nk)) {
		struct netmap_slot *rs = &rxring->slot[k];
		struct netmap_slot *ts = &txring->slot[k];

		*rs = *ts;
		if (ts->flags & NS_BUF_CHANGED) {
			ts->flags &= ~NS_BUF_CHANGED;
		}
		complete = !(ts->flags & NS_MOREFRAG);
	}

	txkring->nr_hwcur = k;

	nm_prdis(20, "TX after : hwcur %d hwtail %d cur %d head %d tail %d k %d",
		txkring->nr_hwcur, txkring->nr_hwtail,
		txkring->rcur, txkring->rhead, txkring->rtail, k);

	if (likely(nk <= lim)) {
		mb(); /* make sure the slots are updated before publishing them */
		rxkring->pipe_tail = nk; /* only publish complete packets */
		rxkring->nm_notify(rxkring, 0);
	}

	return 0;
}

int
netmap_pipe_rxsync(struct netmap_kring *rxkring, int flags)
{
	struct netmap_kring *txkring = rxkring->pipe;
	u_int k, lim = rxkring->nkr_num_slots - 1;
	int m; /* slots to release */
	struct netmap_ring *txring = txkring->ring, *rxring = rxkring->ring;

	nm_prdis("%p: %s %x -> %s", txkring, txkring->name, flags, rxkring->name);
	nm_prdis(20, "RX before: hwcur %d hwtail %d cur %d head %d tail %d",
		rxkring->nr_hwcur, rxkring->nr_hwtail,
		rxkring->rcur, rxkring->rhead, rxkring->rtail);

	/* update the hwtail */
	rxkring->nr_hwtail = rxkring->pipe_tail;

	m = rxkring->rhead - rxkring->nr_hwcur; /* released slots */
	if (m < 0)
		m += rxkring->nkr_num_slots;

	if (m == 0) {
		/* nothing to release */
		return 0;
	}

	for (k = rxkring->nr_hwcur; m; m--, k = nm_next(k, lim)) {
		struct netmap_slot *rs = &rxring->slot[k];
		struct netmap_slot *ts = &txring->slot[k];

		if (rs->flags & NS_BUF_CHANGED) {
			/* copy the slot and report the buffer change */
			*ts = *rs;
			rs->flags &= ~NS_BUF_CHANGED;
		}
	}

	mb(); /* make sure the slots are updated before publishing them */
	txkring->pipe_tail = nm_prev(k, lim);
	rxkring->nr_hwcur = k;

	nm_prdis(20, "RX after : hwcur %d hwtail %d cur %d head %d tail %d k %d",
		rxkring->nr_hwcur, rxkring->nr_hwtail,
		rxkring->rcur, rxkring->rhead, rxkring->rtail, k);

	txkring->nm_notify(txkring, 0);

	return 0;
}

/* Pipe endpoints are created and destroyed together, so that endopoints do not
 * have to check for the existence of their peer at each ?xsync.
 *
 * To play well with the existing netmap infrastructure (refcounts etc.), we
 * adopt the following strategy:
 *
 * 1) The first endpoint that is created also creates the other endpoint and
 * grabs a reference to it.
 *
 *    state A)  user1 --> endpoint1 --> endpoint2
 *
 * 2) If, starting from state A, endpoint2 is then registered, endpoint1 gives
 * its reference to the user:
 *
 *    state B)  user1 --> endpoint1     endpoint2 <--- user2
 *
 * 3) Assume that, starting from state B endpoint2 is closed. In the unregister
 * callback endpoint2 notes that endpoint1 is still active and adds a reference
 * from endpoint1 to itself. When user2 then releases her own reference,
 * endpoint2 is not destroyed and we are back to state A. A symmetrical state
 * would be reached if endpoint1 were released instead.
 *
 * 4) If, starting from state A, endpoint1 is closed, the destructor notes that
 * it owns a reference to endpoint2 and releases it.
 *
 * Something similar goes on for the creation and destruction of the krings.
 */


int netmap_pipe_krings_create_both(struct netmap_adapter *na,
				  struct netmap_adapter *ona)
{
	enum txrx t;
	int error;
	int i;

	/* case 1) below */
	nm_prdis("%p: case 1, create both ends", na);
	error = netmap_krings_create(na, 0);
	if (error)
		return error;

	/* create the krings of the other end */
	error = netmap_krings_create(ona, 0);
	if (error)
		goto del_krings1;

	/* cross link the krings and initialize the pipe_tails */
	for_rx_tx(t) {
		enum txrx r = nm_txrx_swap(t); /* swap NR_TX <-> NR_RX */
		for (i = 0; i < nma_get_nrings(na, t); i++) {
			struct netmap_kring *k1 = NMR(na, t)[i],
					    *k2 = NMR(ona, r)[i];
			k1->pipe = k2;
			k2->pipe = k1;
			/* mark all peer-adapter rings as fake */
			k2->nr_kflags |= NKR_FAKERING;
			/* init tails */
			k1->pipe_tail = k1->nr_hwtail;
			k2->pipe_tail = k2->nr_hwtail;
		}
	}

	return 0;

del_krings1:
	netmap_krings_delete(na);
	return error;
}

/* netmap_pipe_krings_create.
 *
 * There are two cases:
 *
 * 1) state is
 *
 *        usr1 --> e1 --> e2
 *
 *    and we are e1. We have to create both sets
 *    of krings.
 *
 * 2) state is
 *
 *        usr1 --> e1 --> e2
 *
 *    and we are e2. e1 is certainly registered and our
 *    krings already exist. Nothing to do.
 */
static int
netmap_pipe_krings_create(struct netmap_adapter *na)
{
	struct netmap_pipe_adapter *pna =
		(struct netmap_pipe_adapter *)na;
	struct netmap_adapter *ona = &pna->peer->up;

	if (pna->peer_ref)
		return netmap_pipe_krings_create_both(na, ona);

	return 0;
}

int
netmap_pipe_reg_both(struct netmap_adapter *na, struct netmap_adapter *ona)
{
	int i, error = 0;
	enum txrx t;

	for_rx_tx(t) {
		for (i = 0; i < nma_get_nrings(na, t); i++) {
			struct netmap_kring *kring = NMR(na, t)[i];

			if (nm_kring_pending_on(kring)) {
				/* mark the peer ring as needed */
				kring->pipe->nr_kflags |= NKR_NEEDRING;
			}
		}
	}

	/* create all missing needed rings on the other end.
	 * Either our end, or the other, has been marked as
	 * fake, so the allocation will not be done twice.
	 */
	error = netmap_mem_rings_create(ona);
	if (error)
		return error;

	/* In case of no error we put our rings in netmap mode */
	for_rx_tx(t) {
		for (i = 0; i < nma_get_nrings(na, t); i++) {
			struct netmap_kring *kring = NMR(na, t)[i];
			if (nm_kring_pending_on(kring)) {
				struct netmap_kring *sring, *dring;

				kring->nr_mode = NKR_NETMAP_ON;
				if ((kring->nr_kflags & NKR_FAKERING) &&
				    (kring->pipe->nr_kflags & NKR_FAKERING)) {
					/* this is a re-open of a pipe
					 * end-point kept alive by the other end.
					 * We need to leave everything as it is
					 */
					continue;
				}

				/* copy the buffers from the non-fake ring */
				if (kring->nr_kflags & NKR_FAKERING) {
					sring = kring->pipe;
					dring = kring;
				} else {
					sring = kring;
					dring = kring->pipe;
				}
				memcpy(dring->ring->slot,
				       sring->ring->slot,
				       sizeof(struct netmap_slot) *
						sring->nkr_num_slots);
				/* mark both rings as fake and needed,
				 * so that buffers will not be
				 * deleted by the standard machinery
				 * (we will delete them by ourselves in
				 * netmap_pipe_krings_delete)
				 */
				sring->nr_kflags |=
					(NKR_FAKERING | NKR_NEEDRING);
				dring->nr_kflags |=
					(NKR_FAKERING | NKR_NEEDRING);
				kring->nr_mode = NKR_NETMAP_ON;
			}
		}
	}

	return 0;
}

/* netmap_pipe_reg.
 *
 * There are two cases on registration (onoff==1)
 *
 * 1.a) state is
 *
 *        usr1 --> e1 --> e2
 *
 *      and we are e1. Create the needed rings of the
 *      other end.
 *
 * 1.b) state is
 *
 *        usr1 --> e1 --> e2 <-- usr2
 *
 *      and we are e2. Drop the ref e1 is holding.
 *
 *  There are two additional cases on unregister (onoff==0)
 *
 *  2.a) state is
 *
 *         usr1 --> e1 --> e2
 *
 *       and we are e1. Nothing special to do, e2 will
 *       be cleaned up by the destructor of e1.
 *
 *  2.b) state is
 *
 *         usr1 --> e1     e2 <-- usr2
 *
 *       and we are either e1 or e2. Add a ref from the
 *       other end.
 */
static int
netmap_pipe_reg(struct netmap_adapter *na, int onoff)
{
	struct netmap_pipe_adapter *pna =
		(struct netmap_pipe_adapter *)na;
	struct netmap_adapter *ona = &pna->peer->up;
	int error = 0;

	nm_prdis("%p: onoff %d", na, onoff);
	if (onoff) {
		error = netmap_pipe_reg_both(na, ona);
		if (error) {
			return error;
		}
		if (na->active_fds == 0)
			na->na_flags |= NAF_NETMAP_ON;
	} else {
		if (na->active_fds == 0)
			na->na_flags &= ~NAF_NETMAP_ON;
		netmap_krings_mode_commit(na, onoff);
	}

	if (na->active_fds) {
		nm_prdis("active_fds %d", na->active_fds);
		return 0;
	}

	if (pna->peer_ref) {
		nm_prdis("%p: case 1.a or 2.a, nothing to do", na);
		return 0;
	}
	if (onoff) {
		nm_prdis("%p: case 1.b, drop peer", na);
		pna->peer->peer_ref = 0;
		netmap_adapter_put(na);
	} else {
		nm_prdis("%p: case 2.b, grab peer", na);
		netmap_adapter_get(na);
		pna->peer->peer_ref = 1;
	}
	return error;
}

void
netmap_pipe_krings_delete_both(struct netmap_adapter *na,
			       struct netmap_adapter *ona)
{
	struct netmap_adapter *sna;
	enum txrx t;
	int i;

	/* case 1) below */
	nm_prdis("%p: case 1, deleting everything", na);
	/* To avoid double-frees we zero-out all the buffers in the kernel part
	 * of each ring. The reason is this: If the user is behaving correctly,
	 * all buffers are found in exactly one slot in the userspace part of
	 * some ring.  If the user is not behaving correctly, we cannot release
	 * buffers cleanly anyway. In the latter case, the allocator will
	 * return to a clean state only when all its users will close.
	 */
	sna = na;
cleanup:
	for_rx_tx(t) {
		for (i = 0; i < nma_get_nrings(sna, t); i++) {
			struct netmap_kring *kring = NMR(sna, t)[i];
			struct netmap_ring *ring = kring->ring;
			uint32_t j, lim = kring->nkr_num_slots - 1;

			nm_prdis("%s ring %p hwtail %u hwcur %u",
				kring->name, ring, kring->nr_hwtail, kring->nr_hwcur);

			if (ring == NULL)
				continue;

			if (kring->tx == NR_RX)
				ring->slot[kring->pipe_tail].buf_idx = 0;

			for (j = nm_next(kring->pipe_tail, lim);
			     j != kring->nr_hwcur;
			     j = nm_next(j, lim))
			{
				nm_prdis("%s[%d] %u", kring->name, j, ring->slot[j].buf_idx);
				ring->slot[j].buf_idx = 0;
			}
			kring->nr_kflags &= ~(NKR_FAKERING | NKR_NEEDRING);
		}

	}
	if (sna != ona && ona->tx_rings) {
		sna = ona;
		goto cleanup;
	}

	netmap_mem_rings_delete(na);
	netmap_krings_delete(na); /* also zeroes tx_rings etc. */

	if (ona->tx_rings == NULL) {
		/* already deleted, we must be on an
		 * cleanup-after-error path */
		return;
	}
	netmap_mem_rings_delete(ona);
	netmap_krings_delete(ona);
}

/* netmap_pipe_krings_delete.
 *
 * There are two cases:
 *
 * 1) state is
 *
 *                usr1 --> e1 --> e2
 *
 *    and we are e1 (e2 is not registered, so krings_delete cannot be
 *    called on it);
 *
 * 2) state is
 *
 *                usr1 --> e1     e2 <-- usr2
 *
 *    and we are either e1 or e2.
 *
 * In the former case we have to also delete the krings of e2;
 * in the latter case we do nothing.
 */
static void
netmap_pipe_krings_delete(struct netmap_adapter *na)
{
	struct netmap_pipe_adapter *pna =
		(struct netmap_pipe_adapter *)na;
	struct netmap_adapter *ona; /* na of the other end */

	if (!pna->peer_ref) {
		nm_prdis("%p: case 2, kept alive by peer",  na);
		return;
	}
	ona = &pna->peer->up;
	netmap_pipe_krings_delete_both(na, ona);
}


static void
netmap_pipe_dtor(struct netmap_adapter *na)
{
	struct netmap_pipe_adapter *pna =
		(struct netmap_pipe_adapter *)na;
	nm_prdis("%p %p", na, pna->parent_ifp);
	if (pna->peer_ref) {
		nm_prdis("%p: clean up peer", na);
		pna->peer_ref = 0;
		netmap_adapter_put(&pna->peer->up);
	}
	if (pna->role == NM_PIPE_ROLE_MASTER)
		netmap_pipe_remove(pna->parent, pna);
	if (pna->parent_ifp)
		if_rele(pna->parent_ifp);
	netmap_adapter_put(pna->parent);
	pna->parent = NULL;
}

int
netmap_get_pipe_na(struct nmreq_header *hdr, struct netmap_adapter **na,
		struct netmap_mem_d *nmd, int create)
{
	struct nmreq_register *req = (struct nmreq_register *)(uintptr_t)hdr->nr_body;
	struct netmap_adapter *pna; /* parent adapter */
	struct netmap_pipe_adapter *mna, *sna, *reqna;
	struct ifnet *ifp = NULL;
	const char *pipe_id = NULL;
	int role = 0;
	int error, retries = 0;
	char *cbra;

	/* Try to parse the pipe syntax 'xx{yy' or 'xx}yy'. */
	cbra = strrchr(hdr->nr_name, '{');
	if (cbra != NULL) {
		role = NM_PIPE_ROLE_MASTER;
	} else {
		cbra = strrchr(hdr->nr_name, '}');
		if (cbra != NULL) {
			role = NM_PIPE_ROLE_SLAVE;
		} else {
			nm_prdis("not a pipe");
			return 0;
		}
	}
	pipe_id = cbra + 1;
	if (*pipe_id == '\0' || cbra == hdr->nr_name) {
		/* Bracket is the last character, so pipe name is missing;
		 * or bracket is the first character, so base port name
		 * is missing. */
		return EINVAL;
	}

	if (req->nr_mode != NR_REG_ALL_NIC && req->nr_mode != NR_REG_ONE_NIC) {
		/* We only accept modes involving hardware rings. */
		return EINVAL;
	}

	/* first, try to find the parent adapter */
	for (;;) {
		char nr_name_orig[NETMAP_REQ_IFNAMSIZ];
		int create_error;

		/* Temporarily remove the pipe suffix. */
		strlcpy(nr_name_orig, hdr->nr_name, sizeof(nr_name_orig));
		*cbra = '\0';
		error = netmap_get_na(hdr, &pna, &ifp, nmd, create);
		/* Restore the pipe suffix. */
		strlcpy(hdr->nr_name, nr_name_orig, sizeof(hdr->nr_name));
		if (!error)
			break;
		if (error != ENXIO || retries++) {
			nm_prdis("parent lookup failed: %d", error);
			return error;
		}
		nm_prdis("try to create a persistent vale port");
		/* create a persistent vale port and try again */
		*cbra = '\0';
		NMG_UNLOCK();
		create_error = netmap_vi_create(hdr, 1 /* autodelete */);
		NMG_LOCK();
		strlcpy(hdr->nr_name, nr_name_orig, sizeof(hdr->nr_name));
		if (create_error && create_error != EEXIST) {
			if (create_error != EOPNOTSUPP) {
				nm_prerr("failed to create a persistent vale port: %d",
				    create_error);
			}
			return error;
		}
	}

	if (NETMAP_OWNED_BY_KERN(pna)) {
		nm_prdis("parent busy");
		error = EBUSY;
		goto put_out;
	}

	/* next, lookup the pipe id in the parent list */
	reqna = NULL;
	mna = netmap_pipe_find(pna, pipe_id);
	if (mna) {
		if (mna->role == role) {
			nm_prdis("found %s directly at %d", pipe_id, mna->parent_slot);
			reqna = mna;
		} else {
			nm_prdis("found %s indirectly at %d", pipe_id, mna->parent_slot);
			reqna = mna->peer;
		}
		/* the pipe we have found already holds a ref to the parent,
		 * so we need to drop the one we got from netmap_get_na()
		 */
		netmap_unget_na(pna, ifp);
		goto found;
	}
	nm_prdis("pipe %s not found, create %d", pipe_id, create);
	if (!create) {
		error = ENODEV;
		goto put_out;
	}
	/* we create both master and slave.
	 * The endpoint we were asked for holds a reference to
	 * the other one.
	 */
	mna = nm_os_malloc(sizeof(*mna));
	if (mna == NULL) {
		error = ENOMEM;
		goto put_out;
	}
	snprintf(mna->up.name, sizeof(mna->up.name), "%s{%s", pna->name, pipe_id);

	mna->role = NM_PIPE_ROLE_MASTER;
	mna->parent = pna;
	mna->parent_ifp = ifp;

	mna->up.nm_txsync = netmap_pipe_txsync;
	mna->up.nm_rxsync = netmap_pipe_rxsync;
	mna->up.nm_register = netmap_pipe_reg;
	mna->up.nm_dtor = netmap_pipe_dtor;
	mna->up.nm_krings_create = netmap_pipe_krings_create;
	mna->up.nm_krings_delete = netmap_pipe_krings_delete;
	mna->up.nm_mem = netmap_mem_get(pna->nm_mem);
	mna->up.na_flags |= NAF_MEM_OWNER;
	mna->up.na_lut = pna->na_lut;

	mna->up.num_tx_rings = req->nr_tx_rings;
	nm_bound_var(&mna->up.num_tx_rings, 1,
			1, NM_PIPE_MAXRINGS, NULL);
	mna->up.num_rx_rings = req->nr_rx_rings;
	nm_bound_var(&mna->up.num_rx_rings, 1,
			1, NM_PIPE_MAXRINGS, NULL);
	mna->up.num_tx_desc = req->nr_tx_slots;
	nm_bound_var(&mna->up.num_tx_desc, pna->num_tx_desc,
			1, NM_PIPE_MAXSLOTS, NULL);
	mna->up.num_rx_desc = req->nr_rx_slots;
	nm_bound_var(&mna->up.num_rx_desc, pna->num_rx_desc,
			1, NM_PIPE_MAXSLOTS, NULL);
	error = netmap_attach_common(&mna->up);
	if (error)
		goto free_mna;
	/* register the master with the parent */
	error = netmap_pipe_add(pna, mna);
	if (error)
		goto free_mna;

	/* create the slave */
	sna = nm_os_malloc(sizeof(*mna));
	if (sna == NULL) {
		error = ENOMEM;
		goto unregister_mna;
	}
	/* most fields are the same, copy from master and then fix */
	*sna = *mna;
	sna->up.nm_mem = netmap_mem_get(mna->up.nm_mem);
	/* swap the number of tx/rx rings and slots */
	sna->up.num_tx_rings = mna->up.num_rx_rings;
	sna->up.num_tx_desc  = mna->up.num_rx_desc;
	sna->up.num_rx_rings = mna->up.num_tx_rings;
	sna->up.num_rx_desc  = mna->up.num_tx_desc;
	snprintf(sna->up.name, sizeof(sna->up.name), "%s}%s", pna->name, pipe_id);
	sna->role = NM_PIPE_ROLE_SLAVE;
	error = netmap_attach_common(&sna->up);
	if (error)
		goto free_sna;

	/* join the two endpoints */
	mna->peer = sna;
	sna->peer = mna;

	/* we already have a reference to the parent, but we
	 * need another one for the other endpoint we created
	 */
	netmap_adapter_get(pna);
	/* likewise for the ifp, if any */
	if (ifp)
		if_ref(ifp);

	if (role == NM_PIPE_ROLE_MASTER) {
		reqna = mna;
		mna->peer_ref = 1;
		netmap_adapter_get(&sna->up);
	} else {
		reqna = sna;
		sna->peer_ref = 1;
		netmap_adapter_get(&mna->up);
	}
	nm_prdis("created master %p and slave %p", mna, sna);
found:

	nm_prdis("pipe %s %s at %p", pipe_id,
		(reqna->role == NM_PIPE_ROLE_MASTER ? "master" : "slave"), reqna);
	*na = &reqna->up;
	netmap_adapter_get(*na);

	/* keep the reference to the parent.
	 * It will be released by the req destructor
	 */

	return 0;

free_sna:
	nm_os_free(sna);
unregister_mna:
	netmap_pipe_remove(pna, mna);
free_mna:
	nm_os_free(mna);
put_out:
	netmap_unget_na(pna, ifp);
	return error;
}


#endif /* WITH_PIPES */
