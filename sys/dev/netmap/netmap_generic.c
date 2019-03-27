/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2013-2016 Vincenzo Maffione
 * Copyright (C) 2013-2016 Luigi Rizzo
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

/*
 * This module implements netmap support on top of standard,
 * unmodified device drivers.
 *
 * A NIOCREGIF request is handled here if the device does not
 * have native support. TX and RX rings are emulated as follows:
 *
 * NIOCREGIF
 *	We preallocate a block of TX mbufs (roughly as many as
 *	tx descriptors; the number is not critical) to speed up
 *	operation during transmissions. The refcount on most of
 *	these buffers is artificially bumped up so we can recycle
 *	them more easily. Also, the destructor is intercepted
 *	so we use it as an interrupt notification to wake up
 *	processes blocked on a poll().
 *
 *	For each receive ring we allocate one "struct mbq"
 *	(an mbuf tailq plus a spinlock). We intercept packets
 *	(through if_input)
 *	on the receive path and put them in the mbq from which
 *	netmap receive routines can grab them.
 *
 * TX:
 *	in the generic_txsync() routine, netmap buffers are copied
 *	(or linked, in a future) to the preallocated mbufs
 *	and pushed to the transmit queue. Some of these mbufs
 *	(those with NS_REPORT, or otherwise every half ring)
 *	have the refcount=1, others have refcount=2.
 *	When the destructor is invoked, we take that as
 *	a notification that all mbufs up to that one in
 *	the specific ring have been completed, and generate
 *	the equivalent of a transmit interrupt.
 *
 * RX:
 *
 */

#ifdef __FreeBSD__

#include <sys/cdefs.h> /* prerequisite */
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/lock.h>   /* PROT_EXEC */
#include <sys/rwlock.h>
#include <sys/socket.h> /* sockaddrs */
#include <sys/selinfo.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>
#include <machine/bus.h>        /* bus_dmamap_* in netmap_kern.h */

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_mem2.h>

#define MBUF_RXQ(m)	((m)->m_pkthdr.flowid)
#define smp_mb()

#elif defined _WIN32

#include "win_glue.h"

#define MBUF_TXQ(m) 	0//((m)->m_pkthdr.flowid)
#define MBUF_RXQ(m)	    0//((m)->m_pkthdr.flowid)
#define smp_mb()		//XXX: to be correctly defined

#else /* linux */

#include "bsd_glue.h"

#include <linux/ethtool.h>      /* struct ethtool_ops, get_ringparam */
#include <linux/hrtimer.h>

static inline struct mbuf *
nm_os_get_mbuf(struct ifnet *ifp, int len)
{
	return alloc_skb(ifp->needed_headroom + len +
			 ifp->needed_tailroom, GFP_ATOMIC);
}

#endif /* linux */


/* Common headers. */
#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_mem2.h>


#define for_each_kring_n(_i, _k, _karr, _n) \
	for ((_k)=*(_karr), (_i) = 0; (_i) < (_n); (_i)++, (_k) = (_karr)[(_i)])

#define for_each_tx_kring(_i, _k, _na) \
		for_each_kring_n(_i, _k, (_na)->tx_rings, (_na)->num_tx_rings)
#define for_each_tx_kring_h(_i, _k, _na) \
		for_each_kring_n(_i, _k, (_na)->tx_rings, (_na)->num_tx_rings + 1)

#define for_each_rx_kring(_i, _k, _na) \
		for_each_kring_n(_i, _k, (_na)->rx_rings, (_na)->num_rx_rings)
#define for_each_rx_kring_h(_i, _k, _na) \
		for_each_kring_n(_i, _k, (_na)->rx_rings, (_na)->num_rx_rings + 1)


/* ======================== PERFORMANCE STATISTICS =========================== */

#ifdef RATE_GENERIC
#define IFRATE(x) x
struct rate_stats {
	unsigned long txpkt;
	unsigned long txsync;
	unsigned long txirq;
	unsigned long txrepl;
	unsigned long txdrop;
	unsigned long rxpkt;
	unsigned long rxirq;
	unsigned long rxsync;
};

struct rate_context {
	unsigned refcount;
	struct timer_list timer;
	struct rate_stats new;
	struct rate_stats old;
};

#define RATE_PRINTK(_NAME_) \
	printk( #_NAME_ " = %lu Hz\n", (cur._NAME_ - ctx->old._NAME_)/RATE_PERIOD);
#define RATE_PERIOD  2
static void rate_callback(unsigned long arg)
{
	struct rate_context * ctx = (struct rate_context *)arg;
	struct rate_stats cur = ctx->new;
	int r;

	RATE_PRINTK(txpkt);
	RATE_PRINTK(txsync);
	RATE_PRINTK(txirq);
	RATE_PRINTK(txrepl);
	RATE_PRINTK(txdrop);
	RATE_PRINTK(rxpkt);
	RATE_PRINTK(rxsync);
	RATE_PRINTK(rxirq);
	printk("\n");

	ctx->old = cur;
	r = mod_timer(&ctx->timer, jiffies +
			msecs_to_jiffies(RATE_PERIOD * 1000));
	if (unlikely(r))
		nm_prerr("mod_timer() failed");
}

static struct rate_context rate_ctx;

void generic_rate(int txp, int txs, int txi, int rxp, int rxs, int rxi)
{
	if (txp) rate_ctx.new.txpkt++;
	if (txs) rate_ctx.new.txsync++;
	if (txi) rate_ctx.new.txirq++;
	if (rxp) rate_ctx.new.rxpkt++;
	if (rxs) rate_ctx.new.rxsync++;
	if (rxi) rate_ctx.new.rxirq++;
}

#else /* !RATE */
#define IFRATE(x)
#endif /* !RATE */


/* ========== GENERIC (EMULATED) NETMAP ADAPTER SUPPORT ============= */

/*
 * Wrapper used by the generic adapter layer to notify
 * the poller threads. Differently from netmap_rx_irq(), we check
 * only NAF_NETMAP_ON instead of NAF_NATIVE_ON to enable the irq.
 */
void
netmap_generic_irq(struct netmap_adapter *na, u_int q, u_int *work_done)
{
	if (unlikely(!nm_netmap_on(na)))
		return;

	netmap_common_irq(na, q, work_done);
#ifdef RATE_GENERIC
	if (work_done)
		rate_ctx.new.rxirq++;
	else
		rate_ctx.new.txirq++;
#endif  /* RATE_GENERIC */
}

static int
generic_netmap_unregister(struct netmap_adapter *na)
{
	struct netmap_generic_adapter *gna = (struct netmap_generic_adapter *)na;
	struct netmap_kring *kring = NULL;
	int i, r;

	if (na->active_fds == 0) {
		na->na_flags &= ~NAF_NETMAP_ON;

		/* Stop intercepting packets on the RX path. */
		nm_os_catch_rx(gna, 0);

		/* Release packet steering control. */
		nm_os_catch_tx(gna, 0);
	}

	netmap_krings_mode_commit(na, /*onoff=*/0);

	for_each_rx_kring(r, kring, na) {
		/* Free the mbufs still pending in the RX queues,
		 * that did not end up into the corresponding netmap
		 * RX rings. */
		mbq_safe_purge(&kring->rx_queue);
		nm_os_mitigation_cleanup(&gna->mit[r]);
	}

	/* Decrement reference counter for the mbufs in the
	 * TX pools. These mbufs can be still pending in drivers,
	 * (e.g. this happens with virtio-net driver, which
	 * does lazy reclaiming of transmitted mbufs). */
	for_each_tx_kring(r, kring, na) {
		/* We must remove the destructor on the TX event,
		 * because the destructor invokes netmap code, and
		 * the netmap module may disappear before the
		 * TX event is consumed. */
		mtx_lock_spin(&kring->tx_event_lock);
		if (kring->tx_event) {
			SET_MBUF_DESTRUCTOR(kring->tx_event, NULL);
		}
		kring->tx_event = NULL;
		mtx_unlock_spin(&kring->tx_event_lock);
	}

	if (na->active_fds == 0) {
		nm_os_free(gna->mit);

		for_each_rx_kring(r, kring, na) {
			mbq_safe_fini(&kring->rx_queue);
		}

		for_each_tx_kring(r, kring, na) {
			mtx_destroy(&kring->tx_event_lock);
			if (kring->tx_pool == NULL) {
				continue;
			}

			for (i=0; i<na->num_tx_desc; i++) {
				if (kring->tx_pool[i]) {
					m_freem(kring->tx_pool[i]);
				}
			}
			nm_os_free(kring->tx_pool);
			kring->tx_pool = NULL;
		}

#ifdef RATE_GENERIC
		if (--rate_ctx.refcount == 0) {
			nm_prinf("del_timer()");
			del_timer(&rate_ctx.timer);
		}
#endif
		nm_prinf("Emulated adapter for %s deactivated", na->name);
	}

	return 0;
}

/* Enable/disable netmap mode for a generic network interface. */
static int
generic_netmap_register(struct netmap_adapter *na, int enable)
{
	struct netmap_generic_adapter *gna = (struct netmap_generic_adapter *)na;
	struct netmap_kring *kring = NULL;
	int error;
	int i, r;

	if (!na) {
		return EINVAL;
	}

	if (!enable) {
		/* This is actually an unregif. */
		return generic_netmap_unregister(na);
	}

	if (na->active_fds == 0) {
		nm_prinf("Emulated adapter for %s activated", na->name);
		/* Do all memory allocations when (na->active_fds == 0), to
		 * simplify error management. */

		/* Allocate memory for mitigation support on all the rx queues. */
		gna->mit = nm_os_malloc(na->num_rx_rings * sizeof(struct nm_generic_mit));
		if (!gna->mit) {
			nm_prerr("mitigation allocation failed");
			error = ENOMEM;
			goto out;
		}

		for_each_rx_kring(r, kring, na) {
			/* Init mitigation support. */
			nm_os_mitigation_init(&gna->mit[r], r, na);

			/* Initialize the rx queue, as generic_rx_handler() can
			 * be called as soon as nm_os_catch_rx() returns.
			 */
			mbq_safe_init(&kring->rx_queue);
		}

		/*
		 * Prepare mbuf pools (parallel to the tx rings), for packet
		 * transmission. Don't preallocate the mbufs here, it's simpler
		 * to leave this task to txsync.
		 */
		for_each_tx_kring(r, kring, na) {
			kring->tx_pool = NULL;
		}
		for_each_tx_kring(r, kring, na) {
			kring->tx_pool =
				nm_os_malloc(na->num_tx_desc * sizeof(struct mbuf *));
			if (!kring->tx_pool) {
				nm_prerr("tx_pool allocation failed");
				error = ENOMEM;
				goto free_tx_pools;
			}
			mtx_init(&kring->tx_event_lock, "tx_event_lock",
				 NULL, MTX_SPIN);
		}
	}

	netmap_krings_mode_commit(na, /*onoff=*/1);

	for_each_tx_kring(r, kring, na) {
		/* Initialize tx_pool and tx_event. */
		for (i=0; i<na->num_tx_desc; i++) {
			kring->tx_pool[i] = NULL;
		}

		kring->tx_event = NULL;
	}

	if (na->active_fds == 0) {
		/* Prepare to intercept incoming traffic. */
		error = nm_os_catch_rx(gna, 1);
		if (error) {
			nm_prerr("nm_os_catch_rx(1) failed (%d)", error);
			goto free_tx_pools;
		}

		/* Let netmap control the packet steering. */
		error = nm_os_catch_tx(gna, 1);
		if (error) {
			nm_prerr("nm_os_catch_tx(1) failed (%d)", error);
			goto catch_rx;
		}

		na->na_flags |= NAF_NETMAP_ON;

#ifdef RATE_GENERIC
		if (rate_ctx.refcount == 0) {
			nm_prinf("setup_timer()");
			memset(&rate_ctx, 0, sizeof(rate_ctx));
			setup_timer(&rate_ctx.timer, &rate_callback, (unsigned long)&rate_ctx);
			if (mod_timer(&rate_ctx.timer, jiffies + msecs_to_jiffies(1500))) {
				nm_prerr("Error: mod_timer()");
			}
		}
		rate_ctx.refcount++;
#endif /* RATE */
	}

	return 0;

	/* Here (na->active_fds == 0) holds. */
catch_rx:
	nm_os_catch_rx(gna, 0);
free_tx_pools:
	for_each_tx_kring(r, kring, na) {
		mtx_destroy(&kring->tx_event_lock);
		if (kring->tx_pool == NULL) {
			continue;
		}
		nm_os_free(kring->tx_pool);
		kring->tx_pool = NULL;
	}
	for_each_rx_kring(r, kring, na) {
		mbq_safe_fini(&kring->rx_queue);
	}
	nm_os_free(gna->mit);
out:

	return error;
}

/*
 * Callback invoked when the device driver frees an mbuf used
 * by netmap to transmit a packet. This usually happens when
 * the NIC notifies the driver that transmission is completed.
 */
static void
generic_mbuf_destructor(struct mbuf *m)
{
	struct netmap_adapter *na = NA(GEN_TX_MBUF_IFP(m));
	struct netmap_kring *kring;
	unsigned int r = MBUF_TXQ(m);
	unsigned int r_orig = r;

	if (unlikely(!nm_netmap_on(na) || r >= na->num_tx_rings)) {
		nm_prerr("Error: no netmap adapter on device %p",
		  GEN_TX_MBUF_IFP(m));
		return;
	}

	/*
	 * First, clear the event mbuf.
	 * In principle, the event 'm' should match the one stored
	 * on ring 'r'. However we check it explicitely to stay
	 * safe against lower layers (qdisc, driver, etc.) changing
	 * MBUF_TXQ(m) under our feet. If the match is not found
	 * on 'r', we try to see if it belongs to some other ring.
	 */
	for (;;) {
		bool match = false;

		kring = na->tx_rings[r];
		mtx_lock_spin(&kring->tx_event_lock);
		if (kring->tx_event == m) {
			kring->tx_event = NULL;
			match = true;
		}
		mtx_unlock_spin(&kring->tx_event_lock);

		if (match) {
			if (r != r_orig) {
				nm_prlim(1, "event %p migrated: ring %u --> %u",
				      m, r_orig, r);
			}
			break;
		}

		if (++r == na->num_tx_rings) r = 0;

		if (r == r_orig) {
			nm_prlim(1, "Cannot match event %p", m);
			return;
		}
	}

	/* Second, wake up clients. They will reclaim the event through
	 * txsync. */
	netmap_generic_irq(na, r, NULL);
#ifdef __FreeBSD__
#if __FreeBSD_version <= 1200050
	void_mbuf_dtor(m, NULL, NULL);
#else  /* __FreeBSD_version >= 1200051 */
	void_mbuf_dtor(m);
#endif /* __FreeBSD_version >= 1200051 */
#endif
}

/* Record completed transmissions and update hwtail.
 *
 * The oldest tx buffer not yet completed is at nr_hwtail + 1,
 * nr_hwcur is the first unsent buffer.
 */
static u_int
generic_netmap_tx_clean(struct netmap_kring *kring, int txqdisc)
{
	u_int const lim = kring->nkr_num_slots - 1;
	u_int nm_i = nm_next(kring->nr_hwtail, lim);
	u_int hwcur = kring->nr_hwcur;
	u_int n = 0;
	struct mbuf **tx_pool = kring->tx_pool;

	nm_prdis("hwcur = %d, hwtail = %d", kring->nr_hwcur, kring->nr_hwtail);

	while (nm_i != hwcur) { /* buffers not completed */
		struct mbuf *m = tx_pool[nm_i];

		if (txqdisc) {
			if (m == NULL) {
				/* Nothing to do, this is going
				 * to be replenished. */
				nm_prlim(3, "Is this happening?");

			} else if (MBUF_QUEUED(m)) {
				break; /* Not dequeued yet. */

			} else if (MBUF_REFCNT(m) != 1) {
				/* This mbuf has been dequeued but is still busy
				 * (refcount is 2).
				 * Leave it to the driver and replenish. */
				m_freem(m);
				tx_pool[nm_i] = NULL;
			}

		} else {
			if (unlikely(m == NULL)) {
				int event_consumed;

				/* This slot was used to place an event. */
				mtx_lock_spin(&kring->tx_event_lock);
				event_consumed = (kring->tx_event == NULL);
				mtx_unlock_spin(&kring->tx_event_lock);
				if (!event_consumed) {
					/* The event has not been consumed yet,
					 * still busy in the driver. */
					break;
				}
				/* The event has been consumed, we can go
				 * ahead. */

			} else if (MBUF_REFCNT(m) != 1) {
				/* This mbuf is still busy: its refcnt is 2. */
				break;
			}
		}

		n++;
		nm_i = nm_next(nm_i, lim);
	}
	kring->nr_hwtail = nm_prev(nm_i, lim);
	nm_prdis("tx completed [%d] -> hwtail %d", n, kring->nr_hwtail);

	return n;
}

/* Compute a slot index in the middle between inf and sup. */
static inline u_int
ring_middle(u_int inf, u_int sup, u_int lim)
{
	u_int n = lim + 1;
	u_int e;

	if (sup >= inf) {
		e = (sup + inf) / 2;
	} else { /* wrap around */
		e = (sup + n + inf) / 2;
		if (e >= n) {
			e -= n;
		}
	}

	if (unlikely(e >= n)) {
		nm_prerr("This cannot happen");
		e = 0;
	}

	return e;
}

static void
generic_set_tx_event(struct netmap_kring *kring, u_int hwcur)
{
	u_int lim = kring->nkr_num_slots - 1;
	struct mbuf *m;
	u_int e;
	u_int ntc = nm_next(kring->nr_hwtail, lim); /* next to clean */

	if (ntc == hwcur) {
		return; /* all buffers are free */
	}

	/*
	 * We have pending packets in the driver between hwtail+1
	 * and hwcur, and we have to chose one of these slot to
	 * generate a notification.
	 * There is a race but this is only called within txsync which
	 * does a double check.
	 */
#if 0
	/* Choose a slot in the middle, so that we don't risk ending
	 * up in a situation where the client continuously wake up,
	 * fills one or a few TX slots and go to sleep again. */
	e = ring_middle(ntc, hwcur, lim);
#else
	/* Choose the first pending slot, to be safe against driver
	 * reordering mbuf transmissions. */
	e = ntc;
#endif

	m = kring->tx_pool[e];
	if (m == NULL) {
		/* An event is already in place. */
		return;
	}

	mtx_lock_spin(&kring->tx_event_lock);
	if (kring->tx_event) {
		/* An event is already in place. */
		mtx_unlock_spin(&kring->tx_event_lock);
		return;
	}

	SET_MBUF_DESTRUCTOR(m, generic_mbuf_destructor);
	kring->tx_event = m;
	mtx_unlock_spin(&kring->tx_event_lock);

	kring->tx_pool[e] = NULL;

	nm_prdis("Request Event at %d mbuf %p refcnt %d", e, m, m ? MBUF_REFCNT(m) : -2 );

	/* Decrement the refcount. This will free it if we lose the race
	 * with the driver. */
	m_freem(m);
	smp_mb();
}


/*
 * generic_netmap_txsync() transforms netmap buffers into mbufs
 * and passes them to the standard device driver
 * (ndo_start_xmit() or ifp->if_transmit() ).
 * On linux this is not done directly, but using dev_queue_xmit(),
 * since it implements the TX flow control (and takes some locks).
 */
static int
generic_netmap_txsync(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_generic_adapter *gna = (struct netmap_generic_adapter *)na;
	struct ifnet *ifp = na->ifp;
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap ring */ // j
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	u_int ring_nr = kring->ring_id;

	IFRATE(rate_ctx.new.txsync++);

	rmb();

	/*
	 * First part: process new packets to send.
	 */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {	/* we have new packets to send */
		struct nm_os_gen_arg a;
		u_int event = -1;

		if (gna->txqdisc && nm_kr_txempty(kring)) {
			/* In txqdisc mode, we ask for a delayed notification,
			 * but only when cur == hwtail, which means that the
			 * client is going to block. */
			event = ring_middle(nm_i, head, lim);
			nm_prdis("Place txqdisc event (hwcur=%u,event=%u,"
			      "head=%u,hwtail=%u)", nm_i, event, head,
			      kring->nr_hwtail);
		}

		a.ifp = ifp;
		a.ring_nr = ring_nr;
		a.head = a.tail = NULL;

		while (nm_i != head) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			u_int len = slot->len;
			void *addr = NMB(na, slot);
			/* device-specific */
			struct mbuf *m;
			int tx_ret;

			NM_CHECK_ADDR_LEN(na, addr, len);

			/* Tale a mbuf from the tx pool (replenishing the pool
			 * entry if necessary) and copy in the user packet. */
			m = kring->tx_pool[nm_i];
			if (unlikely(m == NULL)) {
				kring->tx_pool[nm_i] = m =
					nm_os_get_mbuf(ifp, NETMAP_BUF_SIZE(na));
				if (m == NULL) {
					nm_prlim(2, "Failed to replenish mbuf");
					/* Here we could schedule a timer which
					 * retries to replenish after a while,
					 * and notifies the client when it
					 * manages to replenish some slots. In
					 * any case we break early to avoid
					 * crashes. */
					break;
				}
				IFRATE(rate_ctx.new.txrepl++);
			}

			a.m = m;
			a.addr = addr;
			a.len = len;
			a.qevent = (nm_i == event);
			/* When not in txqdisc mode, we should ask
			 * notifications when NS_REPORT is set, or roughly
			 * every half ring. To optimize this, we set a
			 * notification event when the client runs out of
			 * TX ring space, or when transmission fails. In
			 * the latter case we also break early.
			 */
			tx_ret = nm_os_generic_xmit_frame(&a);
			if (unlikely(tx_ret)) {
				if (!gna->txqdisc) {
					/*
					 * No room for this mbuf in the device driver.
					 * Request a notification FOR A PREVIOUS MBUF,
					 * then call generic_netmap_tx_clean(kring) to do the
					 * double check and see if we can free more buffers.
					 * If there is space continue, else break;
					 * NOTE: the double check is necessary if the problem
					 * occurs in the txsync call after selrecord().
					 * Also, we need some way to tell the caller that not
					 * all buffers were queued onto the device (this was
					 * not a problem with native netmap driver where space
					 * is preallocated). The bridge has a similar problem
					 * and we solve it there by dropping the excess packets.
					 */
					generic_set_tx_event(kring, nm_i);
					if (generic_netmap_tx_clean(kring, gna->txqdisc)) {
						/* space now available */
						continue;
					} else {
						break;
					}
				}

				/* In txqdisc mode, the netmap-aware qdisc
				 * queue has the same length as the number of
				 * netmap slots (N). Since tail is advanced
				 * only when packets are dequeued, qdisc
				 * queue overrun cannot happen, so
				 * nm_os_generic_xmit_frame() did not fail
				 * because of that.
				 * However, packets can be dropped because
				 * carrier is off, or because our qdisc is
				 * being deactivated, or possibly for other
				 * reasons. In these cases, we just let the
				 * packet to be dropped. */
				IFRATE(rate_ctx.new.txdrop++);
			}

			slot->flags &= ~(NS_REPORT | NS_BUF_CHANGED);
			nm_i = nm_next(nm_i, lim);
			IFRATE(rate_ctx.new.txpkt++);
		}
		if (a.head != NULL) {
			a.addr = NULL;
			nm_os_generic_xmit_frame(&a);
		}
		/* Update hwcur to the next slot to transmit. Here nm_i
		 * is not necessarily head, we could break early. */
		kring->nr_hwcur = nm_i;
	}

	/*
	 * Second, reclaim completed buffers
	 */
	if (!gna->txqdisc && (flags & NAF_FORCE_RECLAIM || nm_kr_txempty(kring))) {
		/* No more available slots? Set a notification event
		 * on a netmap slot that will be cleaned in the future.
		 * No doublecheck is performed, since txsync() will be
		 * called twice by netmap_poll().
		 */
		generic_set_tx_event(kring, nm_i);
	}

	generic_netmap_tx_clean(kring, gna->txqdisc);

	return 0;
}


/*
 * This handler is registered (through nm_os_catch_rx())
 * within the attached network interface
 * in the RX subsystem, so that every mbuf passed up by
 * the driver can be stolen to the network stack.
 * Stolen packets are put in a queue where the
 * generic_netmap_rxsync() callback can extract them.
 * Returns 1 if the packet was stolen, 0 otherwise.
 */
int
generic_rx_handler(struct ifnet *ifp, struct mbuf *m)
{
	struct netmap_adapter *na = NA(ifp);
	struct netmap_generic_adapter *gna = (struct netmap_generic_adapter *)na;
	struct netmap_kring *kring;
	u_int work_done;
	u_int r = MBUF_RXQ(m); /* receive ring number */

	if (r >= na->num_rx_rings) {
		r = r % na->num_rx_rings;
	}

	kring = na->rx_rings[r];

	if (kring->nr_mode == NKR_NETMAP_OFF) {
		/* We must not intercept this mbuf. */
		return 0;
	}

	/* limit the size of the queue */
	if (unlikely(!gna->rxsg && MBUF_LEN(m) > NETMAP_BUF_SIZE(na))) {
		/* This may happen when GRO/LRO features are enabled for
		 * the NIC driver when the generic adapter does not
		 * support RX scatter-gather. */
		nm_prlim(2, "Warning: driver pushed up big packet "
				"(size=%d)", (int)MBUF_LEN(m));
		m_freem(m);
	} else if (unlikely(mbq_len(&kring->rx_queue) > 1024)) {
		m_freem(m);
	} else {
		mbq_safe_enqueue(&kring->rx_queue, m);
	}

	if (netmap_generic_mit < 32768) {
		/* no rx mitigation, pass notification up */
		netmap_generic_irq(na, r, &work_done);
	} else {
		/* same as send combining, filter notification if there is a
		 * pending timer, otherwise pass it up and start a timer.
		 */
		if (likely(nm_os_mitigation_active(&gna->mit[r]))) {
			/* Record that there is some pending work. */
			gna->mit[r].mit_pending = 1;
		} else {
			netmap_generic_irq(na, r, &work_done);
			nm_os_mitigation_start(&gna->mit[r]);
		}
	}

	/* We have intercepted the mbuf. */
	return 1;
}

/*
 * generic_netmap_rxsync() extracts mbufs from the queue filled by
 * generic_netmap_rx_handler() and puts their content in the netmap
 * receive ring.
 * Access must be protected because the rx handler is asynchronous,
 */
static int
generic_netmap_rxsync(struct netmap_kring *kring, int flags)
{
	struct netmap_ring *ring = kring->ring;
	struct netmap_adapter *na = kring->na;
	u_int nm_i;	/* index into the netmap ring */ //j,
	u_int n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	int force_update = (flags & NAF_FORCE_READ) || kring->nr_kflags & NKR_PENDINTR;

	/* Adapter-specific variables. */
	u_int nm_buf_len = NETMAP_BUF_SIZE(na);
	struct mbq tmpq;
	struct mbuf *m;
	int avail; /* in bytes */
	int mlen;
	int copy;

	if (head > lim)
		return netmap_ring_reinit(kring);

	IFRATE(rate_ctx.new.rxsync++);

	/*
	 * First part: skip past packets that userspace has released.
	 * This can possibly make room for the second part.
	 */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {
		/* Userspace has released some packets. */
		for (n = 0; nm_i != head; n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];

			slot->flags &= ~NS_BUF_CHANGED;
			nm_i = nm_next(nm_i, lim);
		}
		kring->nr_hwcur = head;
	}

	/*
	 * Second part: import newly received packets.
	 */
	if (!netmap_no_pendintr && !force_update) {
		return 0;
	}

	nm_i = kring->nr_hwtail; /* First empty slot in the receive ring. */

	/* Compute the available space (in bytes) in this netmap ring.
	 * The first slot that is not considered in is the one before
	 * nr_hwcur. */

	avail = nm_prev(kring->nr_hwcur, lim) - nm_i;
	if (avail < 0)
		avail += lim + 1;
	avail *= nm_buf_len;

	/* First pass: While holding the lock on the RX mbuf queue,
	 * extract as many mbufs as they fit the available space,
	 * and put them in a temporary queue.
	 * To avoid performing a per-mbuf division (mlen / nm_buf_len) to
	 * to update avail, we do the update in a while loop that we
	 * also use to set the RX slots, but without performing the copy. */
	mbq_init(&tmpq);
	mbq_lock(&kring->rx_queue);
	for (n = 0;; n++) {
		m = mbq_peek(&kring->rx_queue);
		if (!m) {
			/* No more packets from the driver. */
			break;
		}

		mlen = MBUF_LEN(m);
		if (mlen > avail) {
			/* No more space in the ring. */
			break;
		}

		mbq_dequeue(&kring->rx_queue);

		while (mlen) {
			copy = nm_buf_len;
			if (mlen < copy) {
				copy = mlen;
			}
			mlen -= copy;
			avail -= nm_buf_len;

			ring->slot[nm_i].len = copy;
			ring->slot[nm_i].flags = (mlen ? NS_MOREFRAG : 0);
			nm_i = nm_next(nm_i, lim);
		}

		mbq_enqueue(&tmpq, m);
	}
	mbq_unlock(&kring->rx_queue);

	/* Second pass: Drain the temporary queue, going over the used RX slots,
	 * and perform the copy out of the RX queue lock. */
	nm_i = kring->nr_hwtail;

	for (;;) {
		void *nmaddr;
		int ofs = 0;
		int morefrag;

		m = mbq_dequeue(&tmpq);
		if (!m)	{
			break;
		}

		do {
			nmaddr = NMB(na, &ring->slot[nm_i]);
			/* We only check the address here on generic rx rings. */
			if (nmaddr == NETMAP_BUF_BASE(na)) { /* Bad buffer */
				m_freem(m);
				mbq_purge(&tmpq);
				mbq_fini(&tmpq);
				return netmap_ring_reinit(kring);
			}

			copy = ring->slot[nm_i].len;
			m_copydata(m, ofs, copy, nmaddr);
			ofs += copy;
			morefrag = ring->slot[nm_i].flags & NS_MOREFRAG;
			nm_i = nm_next(nm_i, lim);
		} while (morefrag);

		m_freem(m);
	}

	mbq_fini(&tmpq);

	if (n) {
		kring->nr_hwtail = nm_i;
		IFRATE(rate_ctx.new.rxpkt += n);
	}
	kring->nr_kflags &= ~NKR_PENDINTR;

	return 0;
}

static void
generic_netmap_dtor(struct netmap_adapter *na)
{
	struct netmap_generic_adapter *gna = (struct netmap_generic_adapter*)na;
	struct ifnet *ifp = netmap_generic_getifp(gna);
	struct netmap_adapter *prev_na = gna->prev;

	if (prev_na != NULL) {
		netmap_adapter_put(prev_na);
		if (nm_iszombie(na)) {
		        /*
		         * The driver has been removed without releasing
		         * the reference so we need to do it here.
		         */
		        netmap_adapter_put(prev_na);
		}
		nm_prinf("Native netmap adapter %p restored", prev_na);
	}
	NM_RESTORE_NA(ifp, prev_na);
	/*
	 * netmap_detach_common(), that it's called after this function,
	 * overrides WNA(ifp) if na->ifp is not NULL.
	 */
	na->ifp = NULL;
	nm_prinf("Emulated netmap adapter for %s destroyed", na->name);
}

int
na_is_generic(struct netmap_adapter *na)
{
	return na->nm_register == generic_netmap_register;
}

/*
 * generic_netmap_attach() makes it possible to use netmap on
 * a device without native netmap support.
 * This is less performant than native support but potentially
 * faster than raw sockets or similar schemes.
 *
 * In this "emulated" mode, netmap rings do not necessarily
 * have the same size as those in the NIC. We use a default
 * value and possibly override it if the OS has ways to fetch the
 * actual configuration.
 */
int
generic_netmap_attach(struct ifnet *ifp)
{
	struct netmap_adapter *na;
	struct netmap_generic_adapter *gna;
	int retval;
	u_int num_tx_desc, num_rx_desc;

#ifdef __FreeBSD__
	if (ifp->if_type == IFT_LOOP) {
		nm_prerr("if_loop is not supported by %s", __func__);
		return EINVAL;
	}
#endif

	if (NM_NA_CLASH(ifp)) {
		/* If NA(ifp) is not null but there is no valid netmap
		 * adapter it means that someone else is using the same
		 * pointer (e.g. ax25_ptr on linux). This happens for
		 * instance when also PF_RING is in use. */
		nm_prerr("Error: netmap adapter hook is busy");
		return EBUSY;
	}

	num_tx_desc = num_rx_desc = netmap_generic_ringsize; /* starting point */

	nm_os_generic_find_num_desc(ifp, &num_tx_desc, &num_rx_desc); /* ignore errors */
	if (num_tx_desc == 0 || num_rx_desc == 0) {
		nm_prerr("Device has no hw slots (tx %u, rx %u)", num_tx_desc, num_rx_desc);
		return EINVAL;
	}

	gna = nm_os_malloc(sizeof(*gna));
	if (gna == NULL) {
		nm_prerr("no memory on attach, give up");
		return ENOMEM;
	}
	na = (struct netmap_adapter *)gna;
	strlcpy(na->name, ifp->if_xname, sizeof(na->name));
	na->ifp = ifp;
	na->num_tx_desc = num_tx_desc;
	na->num_rx_desc = num_rx_desc;
	na->rx_buf_maxsize = 32768;
	na->nm_register = &generic_netmap_register;
	na->nm_txsync = &generic_netmap_txsync;
	na->nm_rxsync = &generic_netmap_rxsync;
	na->nm_dtor = &generic_netmap_dtor;
	/* when using generic, NAF_NETMAP_ON is set so we force
	 * NAF_SKIP_INTR to use the regular interrupt handler
	 */
	na->na_flags = NAF_SKIP_INTR | NAF_HOST_RINGS;

	nm_prdis("[GNA] num_tx_queues(%d), real_num_tx_queues(%d), len(%lu)",
			ifp->num_tx_queues, ifp->real_num_tx_queues,
			ifp->tx_queue_len);
	nm_prdis("[GNA] num_rx_queues(%d), real_num_rx_queues(%d)",
			ifp->num_rx_queues, ifp->real_num_rx_queues);

	nm_os_generic_find_num_queues(ifp, &na->num_tx_rings, &na->num_rx_rings);

	retval = netmap_attach_common(na);
	if (retval) {
		nm_os_free(gna);
		return retval;
	}

	if (NM_NA_VALID(ifp)) {
		gna->prev = NA(ifp); /* save old na */
		netmap_adapter_get(gna->prev);
	}
	NM_ATTACH_NA(ifp, na);

	nm_os_generic_set_features(gna);

	nm_prinf("Emulated adapter for %s created (prev was %p)", na->name, gna->prev);

	return retval;
}
