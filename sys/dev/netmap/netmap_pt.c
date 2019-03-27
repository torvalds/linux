/*
 * Copyright (C) 2015 Stefano Garzarella
 * Copyright (C) 2016 Vincenzo Maffione
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

//#define usleep_range(_1, _2)
#define usleep_range(_1, _2) \
	pause_sbt("ptnetmap-sleep", SBT_1US * _1, SBT_1US * 1, C_ABSOLUTE)

#elif defined(linux)
#include <bsd_glue.h>
#endif

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <net/netmap_virt.h>
#include <dev/netmap/netmap_mem2.h>

#ifdef WITH_PTNETMAP_HOST

/* RX cycle without receive any packets */
#define PTN_RX_DRY_CYCLES_MAX	10

/* Limit Batch TX to half ring.
 * Currently disabled, since it does not manage NS_MOREFRAG, which
 * results in random drops in the VALE txsync. */
//#define PTN_TX_BATCH_LIM(_n)	((_n >> 1))

//#define BUSY_WAIT

#define NETMAP_PT_DEBUG  /* Enables communication debugging. */
#ifdef NETMAP_PT_DEBUG
#define DBG(x) x
#else
#define DBG(x)
#endif


#undef RATE
//#define RATE  /* Enables communication statistics. */
#ifdef RATE
#define IFRATE(x) x
struct rate_batch_stats {
    unsigned long sync;
    unsigned long sync_dry;
    unsigned long pkt;
};

struct rate_stats {
    unsigned long gtxk;     /* Guest --> Host Tx kicks. */
    unsigned long grxk;     /* Guest --> Host Rx kicks. */
    unsigned long htxk;     /* Host --> Guest Tx kicks. */
    unsigned long hrxk;     /* Host --> Guest Rx Kicks. */
    unsigned long btxwu;    /* Backend Tx wake-up. */
    unsigned long brxwu;    /* Backend Rx wake-up. */
    struct rate_batch_stats txbs;
    struct rate_batch_stats rxbs;
};

struct rate_context {
    struct timer_list timer;
    struct rate_stats new;
    struct rate_stats old;
};

#define RATE_PERIOD  2
static void
rate_callback(unsigned long arg)
{
    struct rate_context * ctx = (struct rate_context *)arg;
    struct rate_stats cur = ctx->new;
    struct rate_batch_stats *txbs = &cur.txbs;
    struct rate_batch_stats *rxbs = &cur.rxbs;
    struct rate_batch_stats *txbs_old = &ctx->old.txbs;
    struct rate_batch_stats *rxbs_old = &ctx->old.rxbs;
    uint64_t tx_batch, rx_batch;
    unsigned long txpkts, rxpkts;
    unsigned long gtxk, grxk;
    int r;

    txpkts = txbs->pkt - txbs_old->pkt;
    rxpkts = rxbs->pkt - rxbs_old->pkt;

    tx_batch = ((txbs->sync - txbs_old->sync) > 0) ?
	       txpkts / (txbs->sync - txbs_old->sync): 0;
    rx_batch = ((rxbs->sync - rxbs_old->sync) > 0) ?
	       rxpkts / (rxbs->sync - rxbs_old->sync): 0;

    /* Fix-up gtxk and grxk estimates. */
    gtxk = (cur.gtxk - ctx->old.gtxk) - (cur.btxwu - ctx->old.btxwu);
    grxk = (cur.grxk - ctx->old.grxk) - (cur.brxwu - ctx->old.brxwu);

    printk("txpkts  = %lu Hz\n", txpkts/RATE_PERIOD);
    printk("gtxk    = %lu Hz\n", gtxk/RATE_PERIOD);
    printk("htxk    = %lu Hz\n", (cur.htxk - ctx->old.htxk)/RATE_PERIOD);
    printk("btxw    = %lu Hz\n", (cur.btxwu - ctx->old.btxwu)/RATE_PERIOD);
    printk("rxpkts  = %lu Hz\n", rxpkts/RATE_PERIOD);
    printk("grxk    = %lu Hz\n", grxk/RATE_PERIOD);
    printk("hrxk    = %lu Hz\n", (cur.hrxk - ctx->old.hrxk)/RATE_PERIOD);
    printk("brxw    = %lu Hz\n", (cur.brxwu - ctx->old.brxwu)/RATE_PERIOD);
    printk("txbatch = %llu avg\n", tx_batch);
    printk("rxbatch = %llu avg\n", rx_batch);
    printk("\n");

    ctx->old = cur;
    r = mod_timer(&ctx->timer, jiffies +
            msecs_to_jiffies(RATE_PERIOD * 1000));
    if (unlikely(r))
        D("[ptnetmap] Error: mod_timer()\n");
}

static void
rate_batch_stats_update(struct rate_batch_stats *bf, uint32_t pre_tail,
		        uint32_t act_tail, uint32_t num_slots)
{
    int n = (int)act_tail - pre_tail;

    if (n) {
        if (n < 0)
            n += num_slots;

        bf->sync++;
        bf->pkt += n;
    } else {
        bf->sync_dry++;
    }
}

#else /* !RATE */
#define IFRATE(x)
#endif /* RATE */

struct ptnetmap_state {
	/* Kthreads. */
	struct nm_kctx **kctxs;

	/* Shared memory with the guest (TX/RX) */
	struct ptnet_csb_gh __user *csb_gh;
	struct ptnet_csb_hg __user *csb_hg;

	bool stopped;

	/* Netmap adapter wrapping the backend. */
	struct netmap_pt_host_adapter *pth_na;

	IFRATE(struct rate_context rate_ctx;)
};

static inline void
ptnetmap_kring_dump(const char *title, const struct netmap_kring *kring)
{
	D("%s - name: %s hwcur: %d hwtail: %d rhead: %d rcur: %d"
		" rtail: %d head: %d cur: %d tail: %d",
		title, kring->name, kring->nr_hwcur,
		kring->nr_hwtail, kring->rhead, kring->rcur, kring->rtail,
		kring->ring->head, kring->ring->cur, kring->ring->tail);
}

/*
 * TX functions to set/get and to handle host/guest kick.
 */


/* Enable or disable guest --> host kicks. */
static inline void
pthg_kick_enable(struct ptnet_csb_hg __user *pthg, uint32_t val)
{
    CSB_WRITE(pthg, host_need_kick, val);
}

/* Are guest interrupt enabled or disabled? */
static inline uint32_t
ptgh_intr_enabled(struct ptnet_csb_gh __user *ptgh)
{
    uint32_t v;

    CSB_READ(ptgh, guest_need_kick, v);

    return v;
}

/* Handle TX events: from the guest or from the backend */
static void
ptnetmap_tx_handler(void *data, int is_kthread)
{
    struct netmap_kring *kring = data;
    struct netmap_pt_host_adapter *pth_na =
		(struct netmap_pt_host_adapter *)kring->na->na_private;
    struct ptnetmap_state *ptns = pth_na->ptns;
    struct ptnet_csb_gh __user *ptgh;
    struct ptnet_csb_hg __user *pthg;
    struct netmap_ring shadow_ring; /* shadow copy of the netmap_ring */
    bool more_txspace = false;
    struct nm_kctx *kth;
    uint32_t num_slots;
    int batch;
    IFRATE(uint32_t pre_tail);

    if (unlikely(!ptns)) {
        D("ERROR ptnetmap state is NULL");
        return;
    }

    if (unlikely(ptns->stopped)) {
        RD(1, "backend netmap is being stopped");
        return;
    }

    if (unlikely(nm_kr_tryget(kring, 1, NULL))) {
        D("ERROR nm_kr_tryget()");
        return;
    }

    /* This is a guess, to be fixed in the rate callback. */
    IFRATE(ptns->rate_ctx.new.gtxk++);

    /* Get TX ptgh/pthg pointer from the CSB. */
    ptgh = ptns->csb_gh + kring->ring_id;
    pthg = ptns->csb_hg + kring->ring_id;
    kth = ptns->kctxs[kring->ring_id];

    num_slots = kring->nkr_num_slots;

    /* Disable guest --> host notifications. */
    pthg_kick_enable(pthg, 0);
    /* Copy the guest kring pointers from the CSB */
    ptnetmap_host_read_kring_csb(ptgh, &shadow_ring, num_slots);

    for (;;) {
	/* If guest moves ahead too fast, let's cut the move so
	 * that we don't exceed our batch limit. */
        batch = shadow_ring.head - kring->nr_hwcur;
        if (batch < 0)
            batch += num_slots;

#ifdef PTN_TX_BATCH_LIM
        if (batch > PTN_TX_BATCH_LIM(num_slots)) {
            uint32_t head_lim = kring->nr_hwcur + PTN_TX_BATCH_LIM(num_slots);

            if (head_lim >= num_slots)
                head_lim -= num_slots;
            ND(1, "batch: %d head: %d head_lim: %d", batch, shadow_ring.head,
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
            pthg_kick_enable(pthg, 1);
            break;
        }

        if (unlikely(netmap_verbose & NM_VERB_TXSYNC)) {
            ptnetmap_kring_dump("pre txsync", kring);
	}

        IFRATE(pre_tail = kring->rtail);
        if (unlikely(kring->nm_sync(kring, shadow_ring.flags))) {
            /* Reenable notifications. */
            pthg_kick_enable(pthg, 1);
            D("ERROR txsync()");
	    break;
        }

        /*
         * Finalize
         * Copy host hwcur and hwtail into the CSB for the guest sync(), and
	 * do the nm_sync_finalize.
         */
        ptnetmap_host_write_kring_csb(pthg, kring->nr_hwcur,
				      kring->nr_hwtail);
        if (kring->rtail != kring->nr_hwtail) {
	    /* Some more room available in the parent adapter. */
	    kring->rtail = kring->nr_hwtail;
	    more_txspace = true;
        }

        IFRATE(rate_batch_stats_update(&ptns->rate_ctx.new.txbs, pre_tail,
				       kring->rtail, num_slots));

        if (unlikely(netmap_verbose & NM_VERB_TXSYNC)) {
            ptnetmap_kring_dump("post txsync", kring);
	}

#ifndef BUSY_WAIT
        /* Interrupt the guest if needed. */
        if (more_txspace && ptgh_intr_enabled(ptgh) && is_kthread) {
            /* Disable guest kick to avoid sending unnecessary kicks */
            nm_os_kctx_send_irq(kth);
            IFRATE(ptns->rate_ctx.new.htxk++);
            more_txspace = false;
        }
#endif
        /* Read CSB to see if there is more work to do. */
        ptnetmap_host_read_kring_csb(ptgh, &shadow_ring, num_slots);
#ifndef BUSY_WAIT
        if (shadow_ring.head == kring->rhead) {
            /*
             * No more packets to transmit. We enable notifications and
             * go to sleep, waiting for a kick from the guest when new
             * new slots are ready for transmission.
             */
            if (is_kthread) {
                usleep_range(1,1);
            }
            /* Reenable notifications. */
            pthg_kick_enable(pthg, 1);
            /* Doublecheck. */
            ptnetmap_host_read_kring_csb(ptgh, &shadow_ring, num_slots);
            if (shadow_ring.head != kring->rhead) {
		/* We won the race condition, there are more packets to
		 * transmit. Disable notifications and do another cycle */
		pthg_kick_enable(pthg, 0);
		continue;
	    }
	    break;
        }

	if (nm_kr_txempty(kring)) {
	    /* No more available TX slots. We stop waiting for a notification
	     * from the backend (netmap_tx_irq). */
            ND(1, "TX ring");
            break;
        }
#endif
        if (unlikely(ptns->stopped)) {
            D("backend netmap is being stopped");
            break;
        }
    }

    nm_kr_put(kring);

    if (more_txspace && ptgh_intr_enabled(ptgh) && is_kthread) {
        nm_os_kctx_send_irq(kth);
        IFRATE(ptns->rate_ctx.new.htxk++);
    }
}

/* Called on backend nm_notify when there is no worker thread. */
static void
ptnetmap_tx_nothread_notify(void *data)
{
	struct netmap_kring *kring = data;
	struct netmap_pt_host_adapter *pth_na =
		(struct netmap_pt_host_adapter *)kring->na->na_private;
	struct ptnetmap_state *ptns = pth_na->ptns;

	if (unlikely(!ptns)) {
		D("ERROR ptnetmap state is NULL");
		return;
	}

	if (unlikely(ptns->stopped)) {
		D("backend netmap is being stopped");
		return;
	}

	/* We cannot access the CSB here (to check ptgh->guest_need_kick),
	 * unless we switch address space to the one of the guest. For now
	 * we unconditionally inject an interrupt. */
        nm_os_kctx_send_irq(ptns->kctxs[kring->ring_id]);
        IFRATE(ptns->rate_ctx.new.htxk++);
        ND(1, "%s interrupt", kring->name);
}

/*
 * We need RX kicks from the guest when (tail == head-1), where we wait
 * for the guest to refill.
 */
#ifndef BUSY_WAIT
static inline int
ptnetmap_norxslots(struct netmap_kring *kring, uint32_t g_head)
{
    return (NM_ACCESS_ONCE(kring->nr_hwtail) == nm_prev(g_head,
    			    kring->nkr_num_slots - 1));
}
#endif /* !BUSY_WAIT */

/* Handle RX events: from the guest or from the backend */
static void
ptnetmap_rx_handler(void *data, int is_kthread)
{
    struct netmap_kring *kring = data;
    struct netmap_pt_host_adapter *pth_na =
		(struct netmap_pt_host_adapter *)kring->na->na_private;
    struct ptnetmap_state *ptns = pth_na->ptns;
    struct ptnet_csb_gh __user *ptgh;
    struct ptnet_csb_hg __user *pthg;
    struct netmap_ring shadow_ring; /* shadow copy of the netmap_ring */
    struct nm_kctx *kth;
    uint32_t num_slots;
    int dry_cycles = 0;
    bool some_recvd = false;
    IFRATE(uint32_t pre_tail);

    if (unlikely(!ptns || !ptns->pth_na)) {
        D("ERROR ptnetmap state %p, ptnetmap host adapter %p", ptns,
	  ptns ? ptns->pth_na : NULL);
        return;
    }

    if (unlikely(ptns->stopped)) {
        RD(1, "backend netmap is being stopped");
	return;
    }

    if (unlikely(nm_kr_tryget(kring, 1, NULL))) {
        D("ERROR nm_kr_tryget()");
	return;
    }

    /* This is a guess, to be fixed in the rate callback. */
    IFRATE(ptns->rate_ctx.new.grxk++);

    /* Get RX ptgh and pthg pointers from the CSB. */
    ptgh = ptns->csb_gh + (pth_na->up.num_tx_rings + kring->ring_id);
    pthg = ptns->csb_hg + (pth_na->up.num_tx_rings + kring->ring_id);
    kth = ptns->kctxs[pth_na->up.num_tx_rings + kring->ring_id];

    num_slots = kring->nkr_num_slots;

    /* Disable notifications. */
    pthg_kick_enable(pthg, 0);
    /* Copy the guest kring pointers from the CSB */
    ptnetmap_host_read_kring_csb(ptgh, &shadow_ring, num_slots);

    for (;;) {
	uint32_t hwtail;

        /* Netmap prologue */
	shadow_ring.tail = kring->rtail;
        if (unlikely(nm_rxsync_prologue(kring, &shadow_ring) >= num_slots)) {
            /* Reinit ring and enable notifications. */
            netmap_ring_reinit(kring);
            pthg_kick_enable(pthg, 1);
            break;
        }

        if (unlikely(netmap_verbose & NM_VERB_RXSYNC)) {
            ptnetmap_kring_dump("pre rxsync", kring);
	}

        IFRATE(pre_tail = kring->rtail);
        if (unlikely(kring->nm_sync(kring, shadow_ring.flags))) {
            /* Reenable notifications. */
            pthg_kick_enable(pthg, 1);
            D("ERROR rxsync()");
	    break;
        }
        /*
         * Finalize
         * Copy host hwcur and hwtail into the CSB for the guest sync()
         */
	hwtail = NM_ACCESS_ONCE(kring->nr_hwtail);
        ptnetmap_host_write_kring_csb(pthg, kring->nr_hwcur, hwtail);
        if (kring->rtail != hwtail) {
	    kring->rtail = hwtail;
            some_recvd = true;
            dry_cycles = 0;
        } else {
            dry_cycles++;
        }

        IFRATE(rate_batch_stats_update(&ptns->rate_ctx.new.rxbs, pre_tail,
	                               kring->rtail, num_slots));

        if (unlikely(netmap_verbose & NM_VERB_RXSYNC)) {
            ptnetmap_kring_dump("post rxsync", kring);
	}

#ifndef BUSY_WAIT
	/* Interrupt the guest if needed. */
        if (some_recvd && ptgh_intr_enabled(ptgh)) {
            /* Disable guest kick to avoid sending unnecessary kicks */
            nm_os_kctx_send_irq(kth);
            IFRATE(ptns->rate_ctx.new.hrxk++);
            some_recvd = false;
        }
#endif
        /* Read CSB to see if there is more work to do. */
        ptnetmap_host_read_kring_csb(ptgh, &shadow_ring, num_slots);
#ifndef BUSY_WAIT
        if (ptnetmap_norxslots(kring, shadow_ring.head)) {
            /*
             * No more slots available for reception. We enable notification and
             * go to sleep, waiting for a kick from the guest when new receive
	     * slots are available.
             */
            usleep_range(1,1);
            /* Reenable notifications. */
            pthg_kick_enable(pthg, 1);
            /* Doublecheck. */
            ptnetmap_host_read_kring_csb(ptgh, &shadow_ring, num_slots);
            if (!ptnetmap_norxslots(kring, shadow_ring.head)) {
		/* We won the race condition, more slots are available. Disable
		 * notifications and do another cycle. */
                pthg_kick_enable(pthg, 0);
                continue;
	    }
            break;
        }

	hwtail = NM_ACCESS_ONCE(kring->nr_hwtail);
        if (unlikely(hwtail == kring->rhead ||
		     dry_cycles >= PTN_RX_DRY_CYCLES_MAX)) {
	    /* No more packets to be read from the backend. We stop and
	     * wait for a notification from the backend (netmap_rx_irq). */
            ND(1, "nr_hwtail: %d rhead: %d dry_cycles: %d",
	       hwtail, kring->rhead, dry_cycles);
            break;
        }
#endif
        if (unlikely(ptns->stopped)) {
            D("backend netmap is being stopped");
            break;
        }
    }

    nm_kr_put(kring);

    /* Interrupt the guest if needed. */
    if (some_recvd && ptgh_intr_enabled(ptgh)) {
        nm_os_kctx_send_irq(kth);
        IFRATE(ptns->rate_ctx.new.hrxk++);
    }
}

#ifdef NETMAP_PT_DEBUG
static void
ptnetmap_print_configuration(struct ptnetmap_cfg *cfg)
{
	int k;

	D("ptnetmap configuration:");
	D("  CSB @%p@:%p, num_rings=%u, cfgtype %08x", cfg->csb_gh,
	  cfg->csb_hg, cfg->num_rings, cfg->cfgtype);
	for (k = 0; k < cfg->num_rings; k++) {
		switch (cfg->cfgtype) {
		case PTNETMAP_CFGTYPE_QEMU: {
			struct ptnetmap_cfgentry_qemu *e =
				(struct ptnetmap_cfgentry_qemu *)(cfg+1) + k;
			D("    ring #%d: ioeventfd=%lu, irqfd=%lu", k,
				(unsigned long)e->ioeventfd,
				(unsigned long)e->irqfd);
			break;
		}

		case PTNETMAP_CFGTYPE_BHYVE:
		{
			struct ptnetmap_cfgentry_bhyve *e =
				(struct ptnetmap_cfgentry_bhyve *)(cfg+1) + k;
			D("    ring #%d: wchan=%lu, ioctl_fd=%lu, "
			  "ioctl_cmd=%lu, msix_msg_data=%lu, msix_addr=%lu",
				k, (unsigned long)e->wchan,
				(unsigned long)e->ioctl_fd,
				(unsigned long)e->ioctl_cmd,
				(unsigned long)e->ioctl_data.msg_data,
				(unsigned long)e->ioctl_data.addr);
			break;
		}
		}
	}

}
#endif /* NETMAP_PT_DEBUG */

/* Copy actual state of the host ring into the CSB for the guest init */
static int
ptnetmap_kring_snapshot(struct netmap_kring *kring,
			struct ptnet_csb_gh __user *ptgh,
			struct ptnet_csb_hg __user *pthg)
{
    if (CSB_WRITE(ptgh, head, kring->rhead))
        goto err;
    if (CSB_WRITE(ptgh, cur, kring->rcur))
        goto err;

    if (CSB_WRITE(pthg, hwcur, kring->nr_hwcur))
        goto err;
    if (CSB_WRITE(pthg, hwtail, NM_ACCESS_ONCE(kring->nr_hwtail)))
        goto err;

    DBG(ptnetmap_kring_dump("ptnetmap_kring_snapshot", kring);)

    return 0;
err:
    return EFAULT;
}

static struct netmap_kring *
ptnetmap_kring(struct netmap_pt_host_adapter *pth_na, int k)
{
	if (k < pth_na->up.num_tx_rings) {
		return pth_na->up.tx_rings[k];
	}
	return pth_na->up.rx_rings[k - pth_na->up.num_tx_rings];
}

static int
ptnetmap_krings_snapshot(struct netmap_pt_host_adapter *pth_na)
{
	struct ptnetmap_state *ptns = pth_na->ptns;
	struct netmap_kring *kring;
	unsigned int num_rings;
	int err = 0, k;

	num_rings = pth_na->up.num_tx_rings +
		    pth_na->up.num_rx_rings;

	for (k = 0; k < num_rings; k++) {
		kring = ptnetmap_kring(pth_na, k);
		err |= ptnetmap_kring_snapshot(kring, ptns->csb_gh + k,
						ptns->csb_hg + k);
	}

	return err;
}

/*
 * Functions to create kernel contexts, and start/stop the workers.
 */

static int
ptnetmap_create_kctxs(struct netmap_pt_host_adapter *pth_na,
		      struct ptnetmap_cfg *cfg, int use_tx_kthreads)
{
	struct ptnetmap_state *ptns = pth_na->ptns;
	struct nm_kctx_cfg nmk_cfg;
	unsigned int num_rings;
	uint8_t *cfg_entries = (uint8_t *)(cfg + 1);
	unsigned int expected_cfgtype = 0;
	int k;

#if defined(__FreeBSD__)
	expected_cfgtype = PTNETMAP_CFGTYPE_BHYVE;
#elif defined(linux)
	expected_cfgtype = PTNETMAP_CFGTYPE_QEMU;
#endif
	if (cfg->cfgtype != expected_cfgtype) {
		D("Unsupported cfgtype %u", cfg->cfgtype);
		return EINVAL;
	}

	num_rings = pth_na->up.num_tx_rings +
		    pth_na->up.num_rx_rings;

	for (k = 0; k < num_rings; k++) {
		nmk_cfg.attach_user = 1; /* attach kthread to user process */
		nmk_cfg.worker_private = ptnetmap_kring(pth_na, k);
		nmk_cfg.type = k;
		if (k < pth_na->up.num_tx_rings) {
			nmk_cfg.worker_fn = ptnetmap_tx_handler;
			nmk_cfg.use_kthread = use_tx_kthreads;
			nmk_cfg.notify_fn = ptnetmap_tx_nothread_notify;
		} else {
			nmk_cfg.worker_fn = ptnetmap_rx_handler;
			nmk_cfg.use_kthread = 1;
		}

		ptns->kctxs[k] = nm_os_kctx_create(&nmk_cfg,
				cfg_entries + k * cfg->entry_size);
		if (ptns->kctxs[k] == NULL) {
			goto err;
		}
	}

	return 0;
err:
	for (k = 0; k < num_rings; k++) {
		if (ptns->kctxs[k]) {
			nm_os_kctx_destroy(ptns->kctxs[k]);
			ptns->kctxs[k] = NULL;
		}
	}
	return EFAULT;
}

static int
ptnetmap_start_kctx_workers(struct netmap_pt_host_adapter *pth_na)
{
	struct ptnetmap_state *ptns = pth_na->ptns;
	int num_rings;
	int error;
	int k;

	if (!ptns) {
		D("BUG ptns is NULL");
		return EFAULT;
	}

	ptns->stopped = false;

	num_rings = ptns->pth_na->up.num_tx_rings +
		    ptns->pth_na->up.num_rx_rings;
	for (k = 0; k < num_rings; k++) {
		//nm_os_kctx_worker_setaff(ptns->kctxs[k], xxx);
		error = nm_os_kctx_worker_start(ptns->kctxs[k]);
		if (error) {
			return error;
		}
	}

	return 0;
}

static void
ptnetmap_stop_kctx_workers(struct netmap_pt_host_adapter *pth_na)
{
	struct ptnetmap_state *ptns = pth_na->ptns;
	int num_rings;
	int k;

	if (!ptns) {
		/* Nothing to do. */
		return;
	}

	ptns->stopped = true;

	num_rings = ptns->pth_na->up.num_tx_rings +
		    ptns->pth_na->up.num_rx_rings;
	for (k = 0; k < num_rings; k++) {
		nm_os_kctx_worker_stop(ptns->kctxs[k]);
	}
}

static int nm_unused_notify(struct netmap_kring *, int);
static int nm_pt_host_notify(struct netmap_kring *, int);

/* Create ptnetmap state and switch parent adapter to ptnetmap mode. */
static int
ptnetmap_create(struct netmap_pt_host_adapter *pth_na,
		struct ptnetmap_cfg *cfg)
{
    int use_tx_kthreads = ptnetmap_tx_workers; /* snapshot */
    struct ptnetmap_state *ptns;
    unsigned int num_rings;
    int ret, i;

    /* Check if ptnetmap state is already there. */
    if (pth_na->ptns) {
        D("ERROR adapter %p already in ptnetmap mode", pth_na->parent);
        return EINVAL;
    }

    num_rings = pth_na->up.num_tx_rings + pth_na->up.num_rx_rings;

    if (num_rings != cfg->num_rings) {
        D("ERROR configuration mismatch, expected %u rings, found %u",
           num_rings, cfg->num_rings);
        return EINVAL;
    }

    if (!use_tx_kthreads && na_is_generic(pth_na->parent)) {
        D("ERROR ptnetmap direct transmission not supported with "
	  "passed-through emulated adapters");
        return EOPNOTSUPP;
    }

    ptns = nm_os_malloc(sizeof(*ptns) + num_rings * sizeof(*ptns->kctxs));
    if (!ptns) {
        return ENOMEM;
    }

    ptns->kctxs = (struct nm_kctx **)(ptns + 1);
    ptns->stopped = true;

    /* Cross-link data structures. */
    pth_na->ptns = ptns;
    ptns->pth_na = pth_na;

    /* Store the CSB address provided by the hypervisor. */
    ptns->csb_gh = cfg->csb_gh;
    ptns->csb_hg = cfg->csb_hg;

    DBG(ptnetmap_print_configuration(cfg));

    /* Create kernel contexts. */
    if ((ret = ptnetmap_create_kctxs(pth_na, cfg, use_tx_kthreads))) {
        D("ERROR ptnetmap_create_kctxs()");
        goto err;
    }
    /* Copy krings state into the CSB for the guest initialization */
    if ((ret = ptnetmap_krings_snapshot(pth_na))) {
        D("ERROR ptnetmap_krings_snapshot()");
        goto err;
    }

    /* Overwrite parent nm_notify krings callback, and
     * clear NAF_BDG_MAYSLEEP if needed. */
    pth_na->parent->na_private = pth_na;
    pth_na->parent_nm_notify = pth_na->parent->nm_notify;
    pth_na->parent->nm_notify = nm_unused_notify;
    pth_na->parent_na_flags = pth_na->parent->na_flags;
    if (!use_tx_kthreads) {
        /* VALE port txsync is executed under spinlock on Linux, so
         * we need to make sure the bridge cannot sleep. */
        pth_na->parent->na_flags &= ~NAF_BDG_MAYSLEEP;
    }

    for (i = 0; i < pth_na->parent->num_rx_rings; i++) {
        pth_na->up.rx_rings[i]->save_notify =
        	pth_na->up.rx_rings[i]->nm_notify;
        pth_na->up.rx_rings[i]->nm_notify = nm_pt_host_notify;
    }
    for (i = 0; i < pth_na->parent->num_tx_rings; i++) {
        pth_na->up.tx_rings[i]->save_notify =
        	pth_na->up.tx_rings[i]->nm_notify;
        pth_na->up.tx_rings[i]->nm_notify = nm_pt_host_notify;
    }

#ifdef RATE
    memset(&ptns->rate_ctx, 0, sizeof(ptns->rate_ctx));
    setup_timer(&ptns->rate_ctx.timer, &rate_callback,
            (unsigned long)&ptns->rate_ctx);
    if (mod_timer(&ptns->rate_ctx.timer, jiffies + msecs_to_jiffies(1500)))
        D("[ptn] Error: mod_timer()\n");
#endif

    DBG(D("[%s] ptnetmap configuration DONE", pth_na->up.name));

    return 0;

err:
    pth_na->ptns = NULL;
    nm_os_free(ptns);
    return ret;
}

/* Switch parent adapter back to normal mode and destroy
 * ptnetmap state. */
static void
ptnetmap_delete(struct netmap_pt_host_adapter *pth_na)
{
    struct ptnetmap_state *ptns = pth_na->ptns;
    int num_rings;
    int i;

    if (!ptns) {
	/* Nothing to do. */
        return;
    }

    /* Restore parent adapter callbacks. */
    pth_na->parent->nm_notify = pth_na->parent_nm_notify;
    pth_na->parent->na_private = NULL;
    pth_na->parent->na_flags = pth_na->parent_na_flags;

    for (i = 0; i < pth_na->parent->num_rx_rings; i++) {
        pth_na->up.rx_rings[i]->nm_notify =
        	pth_na->up.rx_rings[i]->save_notify;
        pth_na->up.rx_rings[i]->save_notify = NULL;
    }
    for (i = 0; i < pth_na->parent->num_tx_rings; i++) {
        pth_na->up.tx_rings[i]->nm_notify =
        	pth_na->up.tx_rings[i]->save_notify;
        pth_na->up.tx_rings[i]->save_notify = NULL;
    }

    /* Destroy kernel contexts. */
    num_rings = ptns->pth_na->up.num_tx_rings +
                ptns->pth_na->up.num_rx_rings;
    for (i = 0; i < num_rings; i++) {
        nm_os_kctx_destroy(ptns->kctxs[i]);
	ptns->kctxs[i] = NULL;
    }

    IFRATE(del_timer(&ptns->rate_ctx.timer));

    nm_os_free(ptns);

    pth_na->ptns = NULL;

    DBG(D("[%s] ptnetmap deleted", pth_na->up.name));
}

/*
 * Called by netmap_ioctl().
 * Operation is indicated in nr_name.
 *
 * Called without NMG_LOCK.
 */
int
ptnetmap_ctl(const char *nr_name, int create, struct netmap_adapter *na)
{
	struct netmap_pt_host_adapter *pth_na;
	struct ptnetmap_cfg *cfg = NULL;
	int error = 0;

	DBG(D("name: %s", nr_name));

	if (!nm_ptnetmap_host_on(na)) {
		D("ERROR Netmap adapter %p is not a ptnetmap host adapter",
			na);
		return ENXIO;
	}
	pth_na = (struct netmap_pt_host_adapter *)na;

	NMG_LOCK();
	if (create) {
		/* Read hypervisor configuration from userspace. */
		/* TODO */
		if (!cfg) {
			goto out;
		}
		/* Create ptnetmap state (kctxs, ...) and switch parent
		 * adapter to ptnetmap mode. */
		error = ptnetmap_create(pth_na, cfg);
		nm_os_free(cfg);
		if (error) {
			goto out;
		}
		/* Start kthreads. */
		error = ptnetmap_start_kctx_workers(pth_na);
		if (error)
			ptnetmap_delete(pth_na);
	} else {
		/* Stop kthreads. */
		ptnetmap_stop_kctx_workers(pth_na);
		/* Switch parent adapter back to normal mode and destroy
		 * ptnetmap state (kthreads, ...). */
		ptnetmap_delete(pth_na);
	}
out:
	NMG_UNLOCK();

	return error;
}

/* nm_notify callbacks for ptnetmap */
static int
nm_pt_host_notify(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct netmap_pt_host_adapter *pth_na =
		(struct netmap_pt_host_adapter *)na->na_private;
	struct ptnetmap_state *ptns;
	int k;

	/* First check that the passthrough port is not being destroyed. */
	if (unlikely(!pth_na)) {
		return NM_IRQ_COMPLETED;
	}

	ptns = pth_na->ptns;
	if (unlikely(!ptns || ptns->stopped)) {
		return NM_IRQ_COMPLETED;
	}

	k = kring->ring_id;

	/* Notify kthreads (wake up if needed) */
	if (kring->tx == NR_TX) {
		ND(1, "TX backend irq");
		IFRATE(ptns->rate_ctx.new.btxwu++);
	} else {
		k += pth_na->up.num_tx_rings;
		ND(1, "RX backend irq");
		IFRATE(ptns->rate_ctx.new.brxwu++);
	}
	nm_os_kctx_worker_wakeup(ptns->kctxs[k]);

	return NM_IRQ_COMPLETED;
}

static int
nm_unused_notify(struct netmap_kring *kring, int flags)
{
    D("BUG this should never be called");
    return ENXIO;
}

/* nm_config callback for bwrap */
static int
nm_pt_host_config(struct netmap_adapter *na, struct nm_config_info *info)
{
    struct netmap_pt_host_adapter *pth_na =
        (struct netmap_pt_host_adapter *)na;
    struct netmap_adapter *parent = pth_na->parent;
    int error;

    //XXX: maybe calling parent->nm_config is better

    /* forward the request */
    error = netmap_update_config(parent);

    info->num_rx_rings = na->num_rx_rings = parent->num_rx_rings;
    info->num_tx_rings = na->num_tx_rings = parent->num_tx_rings;
    info->num_tx_descs = na->num_tx_desc = parent->num_tx_desc;
    info->num_rx_descs = na->num_rx_desc = parent->num_rx_desc;
    info->rx_buf_maxsize = na->rx_buf_maxsize = parent->rx_buf_maxsize;

    return error;
}

/* nm_krings_create callback for ptnetmap */
static int
nm_pt_host_krings_create(struct netmap_adapter *na)
{
    struct netmap_pt_host_adapter *pth_na =
        (struct netmap_pt_host_adapter *)na;
    struct netmap_adapter *parent = pth_na->parent;
    enum txrx t;
    int error;

    DBG(D("%s", pth_na->up.name));

    /* create the parent krings */
    error = parent->nm_krings_create(parent);
    if (error) {
        return error;
    }

    /* A ptnetmap host adapter points the very same krings
     * as its parent adapter. These pointer are used in the
     * TX/RX worker functions. */
    na->tx_rings = parent->tx_rings;
    na->rx_rings = parent->rx_rings;
    na->tailroom = parent->tailroom;

    for_rx_tx(t) {
	struct netmap_kring *kring;

	/* Parent's kring_create function will initialize
	 * its own na->si. We have to init our na->si here. */
	nm_os_selinfo_init(&na->si[t]);

	/* Force the mem_rings_create() method to create the
	 * host rings independently on what the regif asked for:
	 * these rings are needed by the guest ptnetmap adapter
	 * anyway. */
	kring = NMR(na, t)[nma_get_nrings(na, t)];
	kring->nr_kflags |= NKR_NEEDRING;
    }

    return 0;
}

/* nm_krings_delete callback for ptnetmap */
static void
nm_pt_host_krings_delete(struct netmap_adapter *na)
{
    struct netmap_pt_host_adapter *pth_na =
        (struct netmap_pt_host_adapter *)na;
    struct netmap_adapter *parent = pth_na->parent;

    DBG(D("%s", pth_na->up.name));

    parent->nm_krings_delete(parent);

    na->tx_rings = na->rx_rings = na->tailroom = NULL;
}

/* nm_register callback */
static int
nm_pt_host_register(struct netmap_adapter *na, int onoff)
{
    struct netmap_pt_host_adapter *pth_na =
        (struct netmap_pt_host_adapter *)na;
    struct netmap_adapter *parent = pth_na->parent;
    int error;
    DBG(D("%s onoff %d", pth_na->up.name, onoff));

    if (onoff) {
        /* netmap_do_regif has been called on the ptnetmap na.
         * We need to pass the information about the
         * memory allocator to the parent before
         * putting it in netmap mode
         */
        parent->na_lut = na->na_lut;
    }

    /* forward the request to the parent */
    error = parent->nm_register(parent, onoff);
    if (error)
        return error;


    if (onoff) {
        na->na_flags |= NAF_NETMAP_ON | NAF_PTNETMAP_HOST;
    } else {
        ptnetmap_delete(pth_na);
        na->na_flags &= ~(NAF_NETMAP_ON | NAF_PTNETMAP_HOST);
    }

    return 0;
}

/* nm_dtor callback */
static void
nm_pt_host_dtor(struct netmap_adapter *na)
{
    struct netmap_pt_host_adapter *pth_na =
        (struct netmap_pt_host_adapter *)na;
    struct netmap_adapter *parent = pth_na->parent;

    DBG(D("%s", pth_na->up.name));

    /* The equivalent of NETMAP_PT_HOST_DELETE if the hypervisor
     * didn't do it. */
    ptnetmap_stop_kctx_workers(pth_na);
    ptnetmap_delete(pth_na);

    parent->na_flags &= ~NAF_BUSY;

    netmap_adapter_put(pth_na->parent);
    pth_na->parent = NULL;
}

/* check if nmr is a request for a ptnetmap adapter that we can satisfy */
int
netmap_get_pt_host_na(struct nmreq_header *hdr, struct netmap_adapter **na,
		struct netmap_mem_d *nmd, int create)
{
    struct nmreq_register *req = (struct nmreq_register *)(uintptr_t)hdr->nr_body;
    struct nmreq_register preq;
    struct netmap_adapter *parent; /* target adapter */
    struct netmap_pt_host_adapter *pth_na;
    struct ifnet *ifp = NULL;
    int error;

    /* Check if it is a request for a ptnetmap adapter */
    if ((req->nr_flags & (NR_PTNETMAP_HOST)) == 0) {
        return 0;
    }

    D("Requesting a ptnetmap host adapter");

    pth_na = nm_os_malloc(sizeof(*pth_na));
    if (pth_na == NULL) {
        D("ERROR malloc");
        return ENOMEM;
    }

    /* first, try to find the adapter that we want to passthrough
     * We use the same req, after we have turned off the ptnetmap flag.
     * In this way we can potentially passthrough everything netmap understands.
     */
    memcpy(&preq, req, sizeof(preq));
    preq.nr_flags &= ~(NR_PTNETMAP_HOST);
    hdr->nr_body = (uintptr_t)&preq;
    error = netmap_get_na(hdr, &parent, &ifp, nmd, create);
    hdr->nr_body = (uintptr_t)req;
    if (error) {
        D("parent lookup failed: %d", error);
        goto put_out_noputparent;
    }
    DBG(D("found parent: %s", parent->name));

    /* make sure the interface is not already in use */
    if (NETMAP_OWNED_BY_ANY(parent)) {
        D("NIC %s busy, cannot ptnetmap", parent->name);
        error = EBUSY;
        goto put_out;
    }

    pth_na->parent = parent;

    /* Follow netmap_attach()-like operations for the host
     * ptnetmap adapter. */

    //XXX pth_na->up.na_flags = parent->na_flags;
    pth_na->up.num_rx_rings = parent->num_rx_rings;
    pth_na->up.num_tx_rings = parent->num_tx_rings;
    pth_na->up.num_tx_desc = parent->num_tx_desc;
    pth_na->up.num_rx_desc = parent->num_rx_desc;

    pth_na->up.nm_dtor = nm_pt_host_dtor;
    pth_na->up.nm_register = nm_pt_host_register;

    /* Reuse parent's adapter txsync and rxsync methods. */
    pth_na->up.nm_txsync = parent->nm_txsync;
    pth_na->up.nm_rxsync = parent->nm_rxsync;

    pth_na->up.nm_krings_create = nm_pt_host_krings_create;
    pth_na->up.nm_krings_delete = nm_pt_host_krings_delete;
    pth_na->up.nm_config = nm_pt_host_config;

    /* Set the notify method only or convenience, it will never
     * be used, since - differently from default krings_create - we
     * ptnetmap krings_create callback inits kring->nm_notify
     * directly. */
    pth_na->up.nm_notify = nm_unused_notify;

    pth_na->up.nm_mem = netmap_mem_get(parent->nm_mem);

    pth_na->up.na_flags |= NAF_HOST_RINGS;

    error = netmap_attach_common(&pth_na->up);
    if (error) {
        D("ERROR netmap_attach_common()");
        goto put_out;
    }

    *na = &pth_na->up;
    /* set parent busy, because attached for ptnetmap */
    parent->na_flags |= NAF_BUSY;
    strncpy(pth_na->up.name, parent->name, sizeof(pth_na->up.name));
    strcat(pth_na->up.name, "-PTN");
    netmap_adapter_get(*na);

    DBG(D("%s ptnetmap request DONE", pth_na->up.name));

    /* drop the reference to the ifp, if any */
    if (ifp)
        if_rele(ifp);

    return 0;

put_out:
    netmap_adapter_put(parent);
    if (ifp)
	if_rele(ifp);
put_out_noputparent:
    nm_os_free(pth_na);
    return error;
}
#endif /* WITH_PTNETMAP_HOST */

#ifdef WITH_PTNETMAP_GUEST
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
netmap_pt_guest_txsync(struct ptnet_csb_gh *ptgh, struct ptnet_csb_hg *pthg,
			struct netmap_kring *kring, int flags)
{
	bool notify = false;

	/* Disable notifications */
	ptgh->guest_need_kick = 0;

	/*
	 * First part: tell the host (updating the CSB) to process the new
	 * packets.
	 */
	kring->nr_hwcur = pthg->hwcur;
	ptnetmap_guest_write_kring_csb(ptgh, kring->rcur, kring->rhead);

        /* Ask for a kick from a guest to the host if needed. */
	if (((kring->rhead != kring->nr_hwcur || nm_kr_txempty(kring))
		&& NM_ACCESS_ONCE(pthg->host_need_kick)) ||
			(flags & NAF_FORCE_RECLAIM)) {
		ptgh->sync_flags = flags;
		notify = true;
	}

	/*
	 * Second part: reclaim buffers for completed transmissions.
	 */
	if (nm_kr_txempty(kring) || (flags & NAF_FORCE_RECLAIM)) {
                ptnetmap_guest_read_kring_csb(pthg, kring);
	}

        /*
         * No more room in the ring for new transmissions. The user thread will
	 * go to sleep and we need to be notified by the host when more free
	 * space is available.
         */
	if (nm_kr_txempty(kring) && !(kring->nr_kflags & NKR_NOINTR)) {
		/* Reenable notifications. */
		ptgh->guest_need_kick = 1;
                /* Double check */
                ptnetmap_guest_read_kring_csb(pthg, kring);
                /* If there is new free space, disable notifications */
		if (unlikely(!nm_kr_txempty(kring))) {
			ptgh->guest_need_kick = 0;
		}
	}

	ND(1, "%s CSB(head:%u cur:%u hwtail:%u) KRING(head:%u cur:%u tail:%u)",
		kring->name, ptgh->head, ptgh->cur, pthg->hwtail,
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
netmap_pt_guest_rxsync(struct ptnet_csb_gh *ptgh, struct ptnet_csb_hg *pthg,
			struct netmap_kring *kring, int flags)
{
	bool notify = false;

        /* Disable notifications */
	ptgh->guest_need_kick = 0;

	/*
	 * First part: import newly received packets, by updating the kring
	 * hwtail to the hwtail known from the host (read from the CSB).
	 * This also updates the kring hwcur.
	 */
        ptnetmap_guest_read_kring_csb(pthg, kring);
	kring->nr_kflags &= ~NKR_PENDINTR;

	/*
	 * Second part: tell the host about the slots that guest user has
	 * released, by updating cur and head in the CSB.
	 */
	if (kring->rhead != kring->nr_hwcur) {
		ptnetmap_guest_write_kring_csb(ptgh, kring->rcur,
					       kring->rhead);
                /* Ask for a kick from the guest to the host if needed. */
		if (NM_ACCESS_ONCE(pthg->host_need_kick)) {
			ptgh->sync_flags = flags;
			notify = true;
		}
	}

        /*
         * No more completed RX slots. The user thread will go to sleep and
	 * we need to be notified by the host when more RX slots have been
	 * completed.
         */
	if (nm_kr_rxempty(kring) && !(kring->nr_kflags & NKR_NOINTR)) {
		/* Reenable notifications. */
                ptgh->guest_need_kick = 1;
                /* Double check */
                ptnetmap_guest_read_kring_csb(pthg, kring);
                /* If there are new slots, disable notifications. */
		if (!nm_kr_rxempty(kring)) {
                        ptgh->guest_need_kick = 0;
                }
        }

	ND(1, "%s CSB(head:%u cur:%u hwtail:%u) KRING(head:%u cur:%u tail:%u)",
		kring->name, ptgh->head, ptgh->cur, pthg->hwtail,
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

	if (ptna->backend_regifs) {
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

	if (ptna->backend_regifs) {
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

	ptna->backend_regifs = 0;

	return 0;
}

#endif /* WITH_PTNETMAP_GUEST */
