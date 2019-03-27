/*
 * Copyright (C) 2016-2018 Vincenzo Maffione
 * Copyright (C) 2015 Stefano Garzarella
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
 *
 * $FreeBSD$
 */

/*
 * common headers
 */
#if defined(__FreeBSD__)
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/types.h>
#include <sys/selinfo.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/if_var.h>
#include <machine/bus.h>

#define usleep_range(_1, _2) \
        pause_sbt("sync-kloop-sleep", SBT_1US * _1, SBT_1US * 1, C_ABSOLUTE)

#elif defined(linux)
#include <bsd_glue.h>
#include <linux/file.h>
#include <linux/eventfd.h>
#endif

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <net/netmap_virt.h>
#include <dev/netmap/netmap_mem2.h>

/* Support for eventfd-based notifications. */
#if defined(linux)
#define SYNC_KLOOP_POLL
#endif

/* Write kring pointers (hwcur, hwtail) to the CSB.
 * This routine is coupled with ptnetmap_guest_read_kring_csb(). */
static inline void
sync_kloop_kernel_write(struct nm_csb_ktoa __user *ptr, uint32_t hwcur,
			   uint32_t hwtail)
{
	/* Issue a first store-store barrier to make sure writes to the
	 * netmap ring do not overcome updates on ktoa->hwcur and ktoa->hwtail. */
	nm_stst_barrier();

	/*
	 * The same scheme used in nm_sync_kloop_appl_write() applies here.
	 * We allow the application to read a value of hwcur more recent than the value
	 * of hwtail, since this would anyway result in a consistent view of the
	 * ring state (and hwcur can never wraparound hwtail, since hwcur must be
	 * behind head).
	 *
	 * The following memory barrier scheme is used to make this happen:
	 *
	 *          Application            Kernel
	 *
	 *          STORE(hwcur)           LOAD(hwtail)
	 *          wmb() <------------->  rmb()
	 *          STORE(hwtail)          LOAD(hwcur)
	 */
	CSB_WRITE(ptr, hwcur, hwcur);
	nm_stst_barrier();
	CSB_WRITE(ptr, hwtail, hwtail);
}

/* Read kring pointers (head, cur, sync_flags) from the CSB.
 * This routine is coupled with ptnetmap_guest_write_kring_csb(). */
static inline void
sync_kloop_kernel_read(struct nm_csb_atok __user *ptr,
			  struct netmap_ring *shadow_ring,
			  uint32_t num_slots)
{
	/*
	 * We place a memory barrier to make sure that the update of head never
	 * overtakes the update of cur.
	 * (see explanation in sync_kloop_kernel_write).
	 */
	CSB_READ(ptr, head, shadow_ring->head);
	nm_ldld_barrier();
	CSB_READ(ptr, cur, shadow_ring->cur);
	CSB_READ(ptr, sync_flags, shadow_ring->flags);

	/* Make sure that loads from atok->head and atok->cur are not delayed
	 * after the loads from the netmap ring. */
	nm_ldld_barrier();
}

/* Enable or disable application --> kernel kicks. */
static inline void
csb_ktoa_kick_enable(struct nm_csb_ktoa __user *csb_ktoa, uint32_t val)
{
	CSB_WRITE(csb_ktoa, kern_need_kick, val);
}

#ifdef SYNC_KLOOP_POLL
/* Are application interrupt enabled or disabled? */
static inline uint32_t
csb_atok_intr_enabled(struct nm_csb_atok __user *csb_atok)
{
	uint32_t v;

	CSB_READ(csb_atok, appl_need_kick, v);

	return v;
}
#endif  /* SYNC_KLOOP_POLL */

static inline void
sync_kloop_kring_dump(const char *title, const struct netmap_kring *kring)
{
	nm_prinf("%s, kring %s, hwcur %d, rhead %d, "
		"rcur %d, rtail %d, hwtail %d",
		title, kring->name, kring->nr_hwcur, kring->rhead,
		kring->rcur, kring->rtail, kring->nr_hwtail);
}

/* Arguments for netmap_sync_kloop_tx_ring() and
 * netmap_sync_kloop_rx_ring().
 */
struct sync_kloop_ring_args {
	struct netmap_kring *kring;
	struct nm_csb_atok *csb_atok;
	struct nm_csb_ktoa *csb_ktoa;
#ifdef SYNC_KLOOP_POLL
	struct eventfd_ctx *irq_ctx;
#endif /* SYNC_KLOOP_POLL */
	/* Are we busy waiting rather than using a schedule() loop ? */
	bool busy_wait;
	/* Are we processing in the context of VM exit ? */
	bool direct;
};

static void
netmap_sync_kloop_tx_ring(const struct sync_kloop_ring_args *a)
{
	struct netmap_kring *kring = a->kring;
	struct nm_csb_atok *csb_atok = a->csb_atok;
	struct nm_csb_ktoa *csb_ktoa = a->csb_ktoa;
	struct netmap_ring shadow_ring; /* shadow copy of the netmap_ring */
	bool more_txspace = false;
	uint32_t num_slots;
	int batch;

	if (unlikely(nm_kr_tryget(kring, 1, NULL))) {
		return;
	}

	num_slots = kring->nkr_num_slots;

	/* Disable application --> kernel notifications. */
	if (!a->direct) {
		csb_ktoa_kick_enable(csb_ktoa, 0);
	}
	/* Copy the application kring pointers from the CSB */
	sync_kloop_kernel_read(csb_atok, &shadow_ring, num_slots);

	for (;;) {
		batch = shadow_ring.head - kring->nr_hwcur;
		if (batch < 0)
			batch += num_slots;

#ifdef PTN_TX_BATCH_LIM
		if (batch > PTN_TX_BATCH_LIM(num_slots)) {
			/* If application moves ahead too fast, let's cut the move so
			 * that we don't exceed our batch limit. */
			uint32_t head_lim = kring->nr_hwcur + PTN_TX_BATCH_LIM(num_slots);

			if (head_lim >= num_slots)
				head_lim -= num_slots;
			nm_prdis(1, "batch: %d head: %d head_lim: %d", batch, shadow_ring.head,
					head_lim);
			shadow_ring.head = head_lim;
			batch = PTN_TX_BATCH_LIM(num_slots);
		}
#endif /* PTN_TX_BATCH_LIM */

		if (nm_kr_txspace(kring) <= (num_slots >> 1)) {
			shadow_ring.flags |= NAF_FORCE_RECLAIM;
		}

		/* Netmap prologue */
		shadow_ring.tail = kring->rtail;
		if (unlikely(nm_txsync_prologue(kring, &shadow_ring) >= num_slots)) {
			/* Reinit ring and enable notifications. */
			netmap_ring_reinit(kring);
			if (!a->busy_wait) {
				csb_ktoa_kick_enable(csb_ktoa, 1);
			}
			break;
		}

		if (unlikely(netmap_debug & NM_DEBUG_TXSYNC)) {
			sync_kloop_kring_dump("pre txsync", kring);
		}

		if (unlikely(kring->nm_sync(kring, shadow_ring.flags))) {
			if (!a->busy_wait) {
				/* Reenable notifications. */
				csb_ktoa_kick_enable(csb_ktoa, 1);
			}
			nm_prerr("txsync() failed");
			break;
		}

		/*
		 * Finalize
		 * Copy kernel hwcur and hwtail into the CSB for the application sync(), and
		 * do the nm_sync_finalize.
		 */
		sync_kloop_kernel_write(csb_ktoa, kring->nr_hwcur,
				kring->nr_hwtail);
		if (kring->rtail != kring->nr_hwtail) {
			/* Some more room available in the parent adapter. */
			kring->rtail = kring->nr_hwtail;
			more_txspace = true;
		}

		if (unlikely(netmap_debug & NM_DEBUG_TXSYNC)) {
			sync_kloop_kring_dump("post txsync", kring);
		}

		/* Interrupt the application if needed. */
#ifdef SYNC_KLOOP_POLL
		if (a->irq_ctx && more_txspace && csb_atok_intr_enabled(csb_atok)) {
			/* We could disable kernel --> application kicks here,
			 * to avoid spurious interrupts. */
			eventfd_signal(a->irq_ctx, 1);
			more_txspace = false;
		}
#endif /* SYNC_KLOOP_POLL */

		/* Read CSB to see if there is more work to do. */
		sync_kloop_kernel_read(csb_atok, &shadow_ring, num_slots);
		if (shadow_ring.head == kring->rhead) {
			if (a->busy_wait) {
				break;
			}
			/*
			 * No more packets to transmit. We enable notifications and
			 * go to sleep, waiting for a kick from the application when new
			 * new slots are ready for transmission.
			 */
			/* Reenable notifications. */
			csb_ktoa_kick_enable(csb_ktoa, 1);
			/* Double check, with store-load memory barrier. */
			nm_stld_barrier();
			sync_kloop_kernel_read(csb_atok, &shadow_ring, num_slots);
			if (shadow_ring.head != kring->rhead) {
				/* We won the race condition, there are more packets to
				 * transmit. Disable notifications and do another cycle */
				csb_ktoa_kick_enable(csb_ktoa, 0);
				continue;
			}
			break;
		}

		if (nm_kr_txempty(kring)) {
			/* No more available TX slots. We stop waiting for a notification
			 * from the backend (netmap_tx_irq). */
			nm_prdis(1, "TX ring");
			break;
		}
	}

	nm_kr_put(kring);

#ifdef SYNC_KLOOP_POLL
	if (a->irq_ctx && more_txspace && csb_atok_intr_enabled(csb_atok)) {
		eventfd_signal(a->irq_ctx, 1);
	}
#endif /* SYNC_KLOOP_POLL */
}

/* RX cycle without receive any packets */
#define SYNC_LOOP_RX_DRY_CYCLES_MAX	2

static inline int
sync_kloop_norxslots(struct netmap_kring *kring, uint32_t g_head)
{
	return (NM_ACCESS_ONCE(kring->nr_hwtail) == nm_prev(g_head,
				kring->nkr_num_slots - 1));
}

static void
netmap_sync_kloop_rx_ring(const struct sync_kloop_ring_args *a)
{

	struct netmap_kring *kring = a->kring;
	struct nm_csb_atok *csb_atok = a->csb_atok;
	struct nm_csb_ktoa *csb_ktoa = a->csb_ktoa;
	struct netmap_ring shadow_ring; /* shadow copy of the netmap_ring */
	int dry_cycles = 0;
	bool some_recvd = false;
	uint32_t num_slots;

	if (unlikely(nm_kr_tryget(kring, 1, NULL))) {
		return;
	}

	num_slots = kring->nkr_num_slots;

	/* Get RX csb_atok and csb_ktoa pointers from the CSB. */
	num_slots = kring->nkr_num_slots;

	/* Disable notifications. */
	if (!a->direct) {
		csb_ktoa_kick_enable(csb_ktoa, 0);
	}
	/* Copy the application kring pointers from the CSB */
	sync_kloop_kernel_read(csb_atok, &shadow_ring, num_slots);

	for (;;) {
		uint32_t hwtail;

		/* Netmap prologue */
		shadow_ring.tail = kring->rtail;
		if (unlikely(nm_rxsync_prologue(kring, &shadow_ring) >= num_slots)) {
			/* Reinit ring and enable notifications. */
			netmap_ring_reinit(kring);
			if (!a->busy_wait) {
				csb_ktoa_kick_enable(csb_ktoa, 1);
			}
			break;
		}

		if (unlikely(netmap_debug & NM_DEBUG_RXSYNC)) {
			sync_kloop_kring_dump("pre rxsync", kring);
		}

		if (unlikely(kring->nm_sync(kring, shadow_ring.flags))) {
			if (!a->busy_wait) {
				/* Reenable notifications. */
				csb_ktoa_kick_enable(csb_ktoa, 1);
			}
			nm_prerr("rxsync() failed");
			break;
		}

		/*
		 * Finalize
		 * Copy kernel hwcur and hwtail into the CSB for the application sync()
		 */
		hwtail = NM_ACCESS_ONCE(kring->nr_hwtail);
		sync_kloop_kernel_write(csb_ktoa, kring->nr_hwcur, hwtail);
		if (kring->rtail != hwtail) {
			kring->rtail = hwtail;
			some_recvd = true;
			dry_cycles = 0;
		} else {
			dry_cycles++;
		}

		if (unlikely(netmap_debug & NM_DEBUG_RXSYNC)) {
			sync_kloop_kring_dump("post rxsync", kring);
		}

#ifdef SYNC_KLOOP_POLL
		/* Interrupt the application if needed. */
		if (a->irq_ctx && some_recvd && csb_atok_intr_enabled(csb_atok)) {
			/* We could disable kernel --> application kicks here,
			 * to avoid spurious interrupts. */
			eventfd_signal(a->irq_ctx, 1);
			some_recvd = false;
		}
#endif /* SYNC_KLOOP_POLL */

		/* Read CSB to see if there is more work to do. */
		sync_kloop_kernel_read(csb_atok, &shadow_ring, num_slots);
		if (sync_kloop_norxslots(kring, shadow_ring.head)) {
			if (a->busy_wait) {
				break;
			}
			/*
			 * No more slots available for reception. We enable notification and
			 * go to sleep, waiting for a kick from the application when new receive
			 * slots are available.
			 */
			/* Reenable notifications. */
			csb_ktoa_kick_enable(csb_ktoa, 1);
			/* Double check, with store-load memory barrier. */
			nm_stld_barrier();
			sync_kloop_kernel_read(csb_atok, &shadow_ring, num_slots);
			if (!sync_kloop_norxslots(kring, shadow_ring.head)) {
				/* We won the race condition, more slots are available. Disable
				 * notifications and do another cycle. */
				csb_ktoa_kick_enable(csb_ktoa, 0);
				continue;
			}
			break;
		}

		hwtail = NM_ACCESS_ONCE(kring->nr_hwtail);
		if (unlikely(hwtail == kring->rhead ||
					dry_cycles >= SYNC_LOOP_RX_DRY_CYCLES_MAX)) {
			/* No more packets to be read from the backend. We stop and
			 * wait for a notification from the backend (netmap_rx_irq). */
			nm_prdis(1, "nr_hwtail: %d rhead: %d dry_cycles: %d",
					hwtail, kring->rhead, dry_cycles);
			break;
		}
	}

	nm_kr_put(kring);

#ifdef SYNC_KLOOP_POLL
	/* Interrupt the application if needed. */
	if (a->irq_ctx && some_recvd && csb_atok_intr_enabled(csb_atok)) {
		eventfd_signal(a->irq_ctx, 1);
	}
#endif /* SYNC_KLOOP_POLL */
}

#ifdef SYNC_KLOOP_POLL
struct sync_kloop_poll_ctx;
struct sync_kloop_poll_entry {
	/* Support for receiving notifications from
	 * a netmap ring or from the application. */
	struct file *filp;
	wait_queue_t wait;
	wait_queue_head_t *wqh;

	/* Support for sending notifications to the application. */
	struct eventfd_ctx *irq_ctx;
	struct file *irq_filp;

	/* Arguments for the ring processing function. Useful
	 * in case of custom wake-up function. */
	struct sync_kloop_ring_args *args;
	struct sync_kloop_poll_ctx *parent;

};

struct sync_kloop_poll_ctx {
	poll_table wait_table;
	unsigned int next_entry;
	int (*next_wake_fun)(wait_queue_t *, unsigned, int, void *);
	unsigned int num_entries;
	unsigned int num_tx_rings;
	unsigned int num_rings;
	/* First num_tx_rings entries are for the TX kicks.
	 * Then the RX kicks entries follow. The last two
	 * entries are for TX irq, and RX irq. */
	struct sync_kloop_poll_entry entries[0];
};

static void
sync_kloop_poll_table_queue_proc(struct file *file, wait_queue_head_t *wqh,
				poll_table *pt)
{
	struct sync_kloop_poll_ctx *poll_ctx =
		container_of(pt, struct sync_kloop_poll_ctx, wait_table);
	struct sync_kloop_poll_entry *entry = poll_ctx->entries +
						poll_ctx->next_entry;

	BUG_ON(poll_ctx->next_entry >= poll_ctx->num_entries);
	entry->wqh = wqh;
	entry->filp = file;
	/* Use the default wake up function. */
	if (poll_ctx->next_wake_fun == NULL) {
		init_waitqueue_entry(&entry->wait, current);
	} else {
		init_waitqueue_func_entry(&entry->wait,
		    poll_ctx->next_wake_fun);
	}
	add_wait_queue(wqh, &entry->wait);
}

static int
sync_kloop_tx_kick_wake_fun(wait_queue_t *wait, unsigned mode,
    int wake_flags, void *key)
{
	struct sync_kloop_poll_entry *entry =
	    container_of(wait, struct sync_kloop_poll_entry, wait);

	netmap_sync_kloop_tx_ring(entry->args);

	return 0;
}

static int
sync_kloop_tx_irq_wake_fun(wait_queue_t *wait, unsigned mode,
    int wake_flags, void *key)
{
	struct sync_kloop_poll_entry *entry =
	    container_of(wait, struct sync_kloop_poll_entry, wait);
	struct sync_kloop_poll_ctx *poll_ctx = entry->parent;
	int i;

	for (i = 0; i < poll_ctx->num_tx_rings; i++) {
		struct eventfd_ctx *irq_ctx = poll_ctx->entries[i].irq_ctx;

		if (irq_ctx) {
			eventfd_signal(irq_ctx, 1);
		}
	}

	return 0;
}

static int
sync_kloop_rx_kick_wake_fun(wait_queue_t *wait, unsigned mode,
    int wake_flags, void *key)
{
	struct sync_kloop_poll_entry *entry =
	    container_of(wait, struct sync_kloop_poll_entry, wait);

	netmap_sync_kloop_rx_ring(entry->args);

	return 0;
}

static int
sync_kloop_rx_irq_wake_fun(wait_queue_t *wait, unsigned mode,
    int wake_flags, void *key)
{
	struct sync_kloop_poll_entry *entry =
	    container_of(wait, struct sync_kloop_poll_entry, wait);
	struct sync_kloop_poll_ctx *poll_ctx = entry->parent;
	int i;

	for (i = poll_ctx->num_tx_rings; i < poll_ctx->num_rings; i++) {
		struct eventfd_ctx *irq_ctx = poll_ctx->entries[i].irq_ctx;

		if (irq_ctx) {
			eventfd_signal(irq_ctx, 1);
		}
	}

	return 0;
}
#endif  /* SYNC_KLOOP_POLL */

int
netmap_sync_kloop(struct netmap_priv_d *priv, struct nmreq_header *hdr)
{
	struct nmreq_sync_kloop_start *req =
		(struct nmreq_sync_kloop_start *)(uintptr_t)hdr->nr_body;
	struct nmreq_opt_sync_kloop_eventfds *eventfds_opt = NULL;
#ifdef SYNC_KLOOP_POLL
	struct sync_kloop_poll_ctx *poll_ctx = NULL;
#endif  /* SYNC_KLOOP_POLL */
	int num_rx_rings, num_tx_rings, num_rings;
	struct sync_kloop_ring_args *args = NULL;
	uint32_t sleep_us = req->sleep_us;
	struct nm_csb_atok* csb_atok_base;
	struct nm_csb_ktoa* csb_ktoa_base;
	struct netmap_adapter *na;
	struct nmreq_option *opt;
	bool na_could_sleep = false;
	bool busy_wait = true;
	bool direct_tx = false;
	bool direct_rx = false;
	int err = 0;
	int i;

	if (sleep_us > 1000000) {
		/* We do not accept sleeping for more than a second. */
		return EINVAL;
	}

	if (priv->np_nifp == NULL) {
		return ENXIO;
	}
	mb(); /* make sure following reads are not from cache */

	na = priv->np_na;
	if (!nm_netmap_on(na)) {
		return ENXIO;
	}

	NMG_LOCK();
	/* Make sure the application is working in CSB mode. */
	if (!priv->np_csb_atok_base || !priv->np_csb_ktoa_base) {
		NMG_UNLOCK();
		nm_prerr("sync-kloop on %s requires "
				"NETMAP_REQ_OPT_CSB option", na->name);
		return EINVAL;
	}

	csb_atok_base = priv->np_csb_atok_base;
	csb_ktoa_base = priv->np_csb_ktoa_base;

	/* Make sure that no kloop is currently running. */
	if (priv->np_kloop_state & NM_SYNC_KLOOP_RUNNING) {
		err = EBUSY;
	}
	priv->np_kloop_state |= NM_SYNC_KLOOP_RUNNING;
	NMG_UNLOCK();
	if (err) {
		return err;
	}

	num_rx_rings = priv->np_qlast[NR_RX] - priv->np_qfirst[NR_RX];
	num_tx_rings = priv->np_qlast[NR_TX] - priv->np_qfirst[NR_TX];
	num_rings = num_tx_rings + num_rx_rings;

	args = nm_os_malloc(num_rings * sizeof(args[0]));
	if (!args) {
		err = ENOMEM;
		goto out;
	}

	/* Prepare the arguments for netmap_sync_kloop_tx_ring()
	 * and netmap_sync_kloop_rx_ring(). */
	for (i = 0; i < num_tx_rings; i++) {
		struct sync_kloop_ring_args *a = args + i;

		a->kring = NMR(na, NR_TX)[i + priv->np_qfirst[NR_TX]];
		a->csb_atok = csb_atok_base + i;
		a->csb_ktoa = csb_ktoa_base + i;
		a->busy_wait = busy_wait;
		a->direct = direct_tx;
	}
	for (i = 0; i < num_rx_rings; i++) {
		struct sync_kloop_ring_args *a = args + num_tx_rings + i;

		a->kring = NMR(na, NR_RX)[i + priv->np_qfirst[NR_RX]];
		a->csb_atok = csb_atok_base + num_tx_rings + i;
		a->csb_ktoa = csb_ktoa_base + num_tx_rings + i;
		a->busy_wait = busy_wait;
		a->direct = direct_rx;
	}

	/* Validate notification options. */
	opt = nmreq_findoption((struct nmreq_option *)(uintptr_t)hdr->nr_options,
				NETMAP_REQ_OPT_SYNC_KLOOP_MODE);
	if (opt != NULL) {
		struct nmreq_opt_sync_kloop_mode *mode_opt =
		    (struct nmreq_opt_sync_kloop_mode *)opt;

		direct_tx = !!(mode_opt->mode & NM_OPT_SYNC_KLOOP_DIRECT_TX);
		direct_rx = !!(mode_opt->mode & NM_OPT_SYNC_KLOOP_DIRECT_RX);
		if (mode_opt->mode & ~(NM_OPT_SYNC_KLOOP_DIRECT_TX |
		    NM_OPT_SYNC_KLOOP_DIRECT_RX)) {
			opt->nro_status = err = EINVAL;
			goto out;
		}
		opt->nro_status = 0;
	}
	opt = nmreq_findoption((struct nmreq_option *)(uintptr_t)hdr->nr_options,
				NETMAP_REQ_OPT_SYNC_KLOOP_EVENTFDS);
	if (opt != NULL) {
		err = nmreq_checkduplicate(opt);
		if (err) {
			opt->nro_status = err;
			goto out;
		}
		if (opt->nro_size != sizeof(*eventfds_opt) +
			sizeof(eventfds_opt->eventfds[0]) * num_rings) {
			/* Option size not consistent with the number of
			 * entries. */
			opt->nro_status = err = EINVAL;
			goto out;
		}
#ifdef SYNC_KLOOP_POLL
		eventfds_opt = (struct nmreq_opt_sync_kloop_eventfds *)opt;
		opt->nro_status = 0;

		/* Check if some ioeventfd entry is not defined, and force sleep
		 * synchronization in that case. */
		busy_wait = false;
		for (i = 0; i < num_rings; i++) {
			if (eventfds_opt->eventfds[i].ioeventfd < 0) {
				busy_wait = true;
				break;
			}
		}

		if (busy_wait && (direct_tx || direct_rx)) {
			/* For direct processing we need all the
			 * ioeventfds to be valid. */
			opt->nro_status = err = EINVAL;
			goto out;
		}

		/* We need 2 poll entries for TX and RX notifications coming
		 * from the netmap adapter, plus one entries per ring for the
		 * notifications coming from the application. */
		poll_ctx = nm_os_malloc(sizeof(*poll_ctx) +
				(num_rings + 2) * sizeof(poll_ctx->entries[0]));
		init_poll_funcptr(&poll_ctx->wait_table,
					sync_kloop_poll_table_queue_proc);
		poll_ctx->num_entries = 2 + num_rings;
		poll_ctx->num_tx_rings = num_tx_rings;
		poll_ctx->num_rings = num_rings;
		poll_ctx->next_entry = 0;
		poll_ctx->next_wake_fun = NULL;

		if (direct_tx && (na->na_flags & NAF_BDG_MAYSLEEP)) {
			/* In direct mode, VALE txsync is called from
			 * wake-up context, where it is not possible
			 * to sleep.
			 */
			na->na_flags &= ~NAF_BDG_MAYSLEEP;
			na_could_sleep = true;
		}

		for (i = 0; i < num_rings + 2; i++) {
			poll_ctx->entries[i].args = args + i;
			poll_ctx->entries[i].parent = poll_ctx;
		}

		/* Poll for notifications coming from the applications through
		 * eventfds. */
		for (i = 0; i < num_rings; i++, poll_ctx->next_entry++) {
			struct eventfd_ctx *irq = NULL;
			struct file *filp = NULL;
			unsigned long mask;
			bool tx_ring = (i < num_tx_rings);

			if (eventfds_opt->eventfds[i].irqfd >= 0) {
				filp = eventfd_fget(
				    eventfds_opt->eventfds[i].irqfd);
				if (IS_ERR(filp)) {
					err = PTR_ERR(filp);
					goto out;
				}
				irq = eventfd_ctx_fileget(filp);
				if (IS_ERR(irq)) {
					err = PTR_ERR(irq);
					goto out;
				}
			}
			poll_ctx->entries[i].irq_filp = filp;
			poll_ctx->entries[i].irq_ctx = irq;
			poll_ctx->entries[i].args->busy_wait = busy_wait;
			/* Don't let netmap_sync_kloop_*x_ring() use
			 * IRQs in direct mode. */
			poll_ctx->entries[i].args->irq_ctx =
			    ((tx_ring && direct_tx) ||
			    (!tx_ring && direct_rx)) ? NULL :
			    poll_ctx->entries[i].irq_ctx;
			poll_ctx->entries[i].args->direct =
			    (tx_ring ? direct_tx : direct_rx);

			if (!busy_wait) {
				filp = eventfd_fget(
				    eventfds_opt->eventfds[i].ioeventfd);
				if (IS_ERR(filp)) {
					err = PTR_ERR(filp);
					goto out;
				}
				if (tx_ring && direct_tx) {
					/* Override the wake up function
					 * so that it can directly call
					 * netmap_sync_kloop_tx_ring().
					 */
					poll_ctx->next_wake_fun =
					    sync_kloop_tx_kick_wake_fun;
				} else if (!tx_ring && direct_rx) {
					/* Same for direct RX. */
					poll_ctx->next_wake_fun =
					    sync_kloop_rx_kick_wake_fun;
				} else {
					poll_ctx->next_wake_fun = NULL;
				}
				mask = filp->f_op->poll(filp,
				    &poll_ctx->wait_table);
				if (mask & POLLERR) {
					err = EINVAL;
					goto out;
				}
			}
		}

		/* Poll for notifications coming from the netmap rings bound to
		 * this file descriptor. */
		if (!busy_wait) {
			NMG_LOCK();
			/* In direct mode, override the wake up function so
			 * that it can forward the netmap_tx_irq() to the
			 * guest. */
			poll_ctx->next_wake_fun = direct_tx ?
			    sync_kloop_tx_irq_wake_fun : NULL;
			poll_wait(priv->np_filp, priv->np_si[NR_TX],
			    &poll_ctx->wait_table);
			poll_ctx->next_entry++;

			poll_ctx->next_wake_fun = direct_rx ?
			    sync_kloop_rx_irq_wake_fun : NULL;
			poll_wait(priv->np_filp, priv->np_si[NR_RX],
			    &poll_ctx->wait_table);
			poll_ctx->next_entry++;
			NMG_UNLOCK();
		}
#else   /* SYNC_KLOOP_POLL */
		opt->nro_status = EOPNOTSUPP;
		goto out;
#endif  /* SYNC_KLOOP_POLL */
	}

	nm_prinf("kloop busy_wait %u, direct_tx %u, direct_rx %u, "
	    "na_could_sleep %u", busy_wait, direct_tx, direct_rx,
	    na_could_sleep);

	/* Main loop. */
	for (;;) {
		if (unlikely(NM_ACCESS_ONCE(priv->np_kloop_state) & NM_SYNC_KLOOP_STOPPING)) {
			break;
		}

#ifdef SYNC_KLOOP_POLL
		if (!busy_wait) {
			/* It is important to set the task state as
			 * interruptible before processing any TX/RX ring,
			 * so that if a notification on ring Y comes after
			 * we have processed ring Y, but before we call
			 * schedule(), we don't miss it. This is true because
			 * the wake up function will change the the task state,
			 * and therefore the schedule_timeout() call below
			 * will observe the change).
			 */
			set_current_state(TASK_INTERRUPTIBLE);
		}
#endif  /* SYNC_KLOOP_POLL */

		/* Process all the TX rings bound to this file descriptor. */
		for (i = 0; !direct_tx && i < num_tx_rings; i++) {
			struct sync_kloop_ring_args *a = args + i;
			netmap_sync_kloop_tx_ring(a);
		}

		/* Process all the RX rings bound to this file descriptor. */
		for (i = 0; !direct_rx && i < num_rx_rings; i++) {
			struct sync_kloop_ring_args *a = args + num_tx_rings + i;
			netmap_sync_kloop_rx_ring(a);
		}

		if (busy_wait) {
			/* Default synchronization method: sleep for a while. */
			usleep_range(sleep_us, sleep_us);
		}
#ifdef SYNC_KLOOP_POLL
		else {
			/* Yield to the scheduler waiting for a notification
			 * to come either from netmap or the application. */
			schedule_timeout(msecs_to_jiffies(3000));
		}
#endif /* SYNC_KLOOP_POLL */
	}
out:
#ifdef SYNC_KLOOP_POLL
	if (poll_ctx) {
		/* Stop polling from netmap and the eventfds, and deallocate
		 * the poll context. */
		if (!busy_wait) {
			__set_current_state(TASK_RUNNING);
		}
		for (i = 0; i < poll_ctx->next_entry; i++) {
			struct sync_kloop_poll_entry *entry =
						poll_ctx->entries + i;

			if (entry->wqh)
				remove_wait_queue(entry->wqh, &entry->wait);
			/* We did not get a reference to the eventfds, but
			 * don't do that on netmap file descriptors (since
			 * a reference was not taken. */
			if (entry->filp && entry->filp != priv->np_filp)
				fput(entry->filp);
			if (entry->irq_ctx)
				eventfd_ctx_put(entry->irq_ctx);
			if (entry->irq_filp)
				fput(entry->irq_filp);
		}
		nm_os_free(poll_ctx);
		poll_ctx = NULL;
	}
#endif /* SYNC_KLOOP_POLL */

	if (args) {
		nm_os_free(args);
		args = NULL;
	}

	/* Reset the kloop state. */
	NMG_LOCK();
	priv->np_kloop_state = 0;
	if (na_could_sleep) {
		na->na_flags |= NAF_BDG_MAYSLEEP;
	}
	NMG_UNLOCK();

	return err;
}

int
netmap_sync_kloop_stop(struct netmap_priv_d *priv)
{
	struct netmap_adapter *na;
	bool running = true;
	int err = 0;

	if (priv->np_nifp == NULL) {
		return ENXIO;
	}
	mb(); /* make sure following reads are not from cache */

	na = priv->np_na;
	if (!nm_netmap_on(na)) {
		return ENXIO;
	}

	/* Set the kloop stopping flag. */
	NMG_LOCK();
	priv->np_kloop_state |= NM_SYNC_KLOOP_STOPPING;
	NMG_UNLOCK();

	/* Send a notification to the kloop, in case it is blocked in
	 * schedule_timeout(). We can use either RX or TX, because the
	 * kloop is waiting on both. */
	nm_os_selwakeup(priv->np_si[NR_RX]);

	/* Wait for the kloop to actually terminate. */
	while (running) {
		usleep_range(1000, 1500);
		NMG_LOCK();
		running = (NM_ACCESS_ONCE(priv->np_kloop_state)
				& NM_SYNC_KLOOP_RUNNING);
		NMG_UNLOCK();
	}

	return err;
}

#ifdef WITH_PTNETMAP
/*
 * Guest ptnetmap txsync()/rxsync() routines, used in ptnet device drivers.
 * These routines are reused across the different operating systems supported
 * by netmap.
 */

/*
 * Reconcile host and guest views of the transmit ring.
 *
 * Guest user wants to transmit packets up to the one before ring->head,
 * and guest kernel knows tx_ring->hwcur is the first packet unsent
 * by the host kernel.
 *
 * We push out as many packets as possible, and possibly
 * reclaim buffers from previously completed transmission.
 *
 * Notifications from the host are enabled only if the user guest would
 * block (no space in the ring).
 */
bool
netmap_pt_guest_txsync(struct nm_csb_atok *atok, struct nm_csb_ktoa *ktoa,
			struct netmap_kring *kring, int flags)
{
	bool notify = false;

	/* Disable notifications */
	atok->appl_need_kick = 0;

	/*
	 * First part: tell the host to process the new packets,
	 * updating the CSB.
	 */
	kring->nr_hwcur = ktoa->hwcur;
	nm_sync_kloop_appl_write(atok, kring->rcur, kring->rhead);

        /* Ask for a kick from a guest to the host if needed. */
	if (((kring->rhead != kring->nr_hwcur || nm_kr_wouldblock(kring))
		&& NM_ACCESS_ONCE(ktoa->kern_need_kick)) ||
			(flags & NAF_FORCE_RECLAIM)) {
		atok->sync_flags = flags;
		notify = true;
	}

	/*
	 * Second part: reclaim buffers for completed transmissions.
	 */
	if (nm_kr_wouldblock(kring) || (flags & NAF_FORCE_RECLAIM)) {
		nm_sync_kloop_appl_read(ktoa, &kring->nr_hwtail,
					&kring->nr_hwcur);
	}

        /*
         * No more room in the ring for new transmissions. The user thread will
	 * go to sleep and we need to be notified by the host when more free
	 * space is available.
         */
	if (nm_kr_wouldblock(kring) && !(kring->nr_kflags & NKR_NOINTR)) {
		/* Reenable notifications. */
		atok->appl_need_kick = 1;
                /* Double check, with store-load memory barrier. */
		nm_stld_barrier();
		nm_sync_kloop_appl_read(ktoa, &kring->nr_hwtail,
					&kring->nr_hwcur);
                /* If there is new free space, disable notifications */
		if (unlikely(!nm_kr_wouldblock(kring))) {
			atok->appl_need_kick = 0;
		}
	}

	nm_prdis(1, "%s CSB(head:%u cur:%u hwtail:%u) KRING(head:%u cur:%u tail:%u)",
		kring->name, atok->head, atok->cur, ktoa->hwtail,
		kring->rhead, kring->rcur, kring->nr_hwtail);

	return notify;
}

/*
 * Reconcile host and guest view of the receive ring.
 *
 * Update hwcur/hwtail from host (reading from CSB).
 *
 * If guest user has released buffers up to the one before ring->head, we
 * also give them to the host.
 *
 * Notifications from the host are enabled only if the user guest would
 * block (no more completed slots in the ring).
 */
bool
netmap_pt_guest_rxsync(struct nm_csb_atok *atok, struct nm_csb_ktoa *ktoa,
			struct netmap_kring *kring, int flags)
{
	bool notify = false;

        /* Disable notifications */
	atok->appl_need_kick = 0;

	/*
	 * First part: import newly received packets, by updating the kring
	 * hwtail to the hwtail known from the host (read from the CSB).
	 * This also updates the kring hwcur.
	 */
	nm_sync_kloop_appl_read(ktoa, &kring->nr_hwtail, &kring->nr_hwcur);
	kring->nr_kflags &= ~NKR_PENDINTR;

	/*
	 * Second part: tell the host about the slots that guest user has
	 * released, by updating cur and head in the CSB.
	 */
	if (kring->rhead != kring->nr_hwcur) {
		nm_sync_kloop_appl_write(atok, kring->rcur, kring->rhead);
	}

        /*
         * No more completed RX slots. The user thread will go to sleep and
	 * we need to be notified by the host when more RX slots have been
	 * completed.
         */
	if (nm_kr_wouldblock(kring) && !(kring->nr_kflags & NKR_NOINTR)) {
		/* Reenable notifications. */
                atok->appl_need_kick = 1;
                /* Double check, with store-load memory barrier. */
		nm_stld_barrier();
		nm_sync_kloop_appl_read(ktoa, &kring->nr_hwtail,
					&kring->nr_hwcur);
                /* If there are new slots, disable notifications. */
		if (!nm_kr_wouldblock(kring)) {
                        atok->appl_need_kick = 0;
                }
        }

	/* Ask for a kick from the guest to the host if needed. */
	if ((kring->rhead != kring->nr_hwcur || nm_kr_wouldblock(kring))
		&& NM_ACCESS_ONCE(ktoa->kern_need_kick)) {
		atok->sync_flags = flags;
		notify = true;
	}

	nm_prdis(1, "%s CSB(head:%u cur:%u hwtail:%u) KRING(head:%u cur:%u tail:%u)",
		kring->name, atok->head, atok->cur, ktoa->hwtail,
		kring->rhead, kring->rcur, kring->nr_hwtail);

	return notify;
}

/*
 * Callbacks for ptnet drivers: nm_krings_create, nm_krings_delete, nm_dtor.
 */
int
ptnet_nm_krings_create(struct netmap_adapter *na)
{
	struct netmap_pt_guest_adapter *ptna =
			(struct netmap_pt_guest_adapter *)na; /* Upcast. */
	struct netmap_adapter *na_nm = &ptna->hwup.up;
	struct netmap_adapter *na_dr = &ptna->dr.up;
	int ret;

	if (ptna->backend_users) {
		return 0;
	}

	/* Create krings on the public netmap adapter. */
	ret = netmap_hw_krings_create(na_nm);
	if (ret) {
		return ret;
	}

	/* Copy krings into the netmap adapter private to the driver. */
	na_dr->tx_rings = na_nm->tx_rings;
	na_dr->rx_rings = na_nm->rx_rings;

	return 0;
}

void
ptnet_nm_krings_delete(struct netmap_adapter *na)
{
	struct netmap_pt_guest_adapter *ptna =
			(struct netmap_pt_guest_adapter *)na; /* Upcast. */
	struct netmap_adapter *na_nm = &ptna->hwup.up;
	struct netmap_adapter *na_dr = &ptna->dr.up;

	if (ptna->backend_users) {
		return;
	}

	na_dr->tx_rings = NULL;
	na_dr->rx_rings = NULL;

	netmap_hw_krings_delete(na_nm);
}

void
ptnet_nm_dtor(struct netmap_adapter *na)
{
	struct netmap_pt_guest_adapter *ptna =
			(struct netmap_pt_guest_adapter *)na;

	netmap_mem_put(ptna->dr.up.nm_mem);
	memset(&ptna->dr, 0, sizeof(ptna->dr));
	netmap_mem_pt_guest_ifp_del(na->nm_mem, na->ifp);
}

int
netmap_pt_guest_attach(struct netmap_adapter *arg,
		       unsigned int nifp_offset, unsigned int memid)
{
	struct netmap_pt_guest_adapter *ptna;
	struct ifnet *ifp = arg ? arg->ifp : NULL;
	int error;

	/* get allocator */
	arg->nm_mem = netmap_mem_pt_guest_new(ifp, nifp_offset, memid);
	if (arg->nm_mem == NULL)
		return ENOMEM;
	arg->na_flags |= NAF_MEM_OWNER;
	error = netmap_attach_ext(arg, sizeof(struct netmap_pt_guest_adapter), 1);
	if (error)
		return error;

	/* get the netmap_pt_guest_adapter */
	ptna = (struct netmap_pt_guest_adapter *) NA(ifp);

	/* Initialize a separate pass-through netmap adapter that is going to
	 * be used by the ptnet driver only, and so never exposed to netmap
         * applications. We only need a subset of the available fields. */
	memset(&ptna->dr, 0, sizeof(ptna->dr));
	ptna->dr.up.ifp = ifp;
	ptna->dr.up.nm_mem = netmap_mem_get(ptna->hwup.up.nm_mem);
        ptna->dr.up.nm_config = ptna->hwup.up.nm_config;

	ptna->backend_users = 0;

	return 0;
}

#endif /* WITH_PTNETMAP */
