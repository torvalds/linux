/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2011-2014 Luigi Rizzo. All rights reserved.
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

/*
 * $FreeBSD$
 *
 * netmap support for: re
 *
 * For more details on netmap support please see ixgbe_netmap.h
 */


#include <net/netmap.h>
#include <sys/selinfo.h>
#include <vm/vm.h>
#include <vm/pmap.h>    /* vtophys ? */
#include <dev/netmap/netmap_kern.h>


/*
 * Register/unregister. We are already under netmap lock.
 */
static int
re_netmap_reg(struct netmap_adapter *na, int onoff)
{
	struct ifnet *ifp = na->ifp;
	struct rl_softc *adapter = ifp->if_softc;

	RL_LOCK(adapter);
	re_stop(adapter); /* also clears IFF_DRV_RUNNING */
	if (onoff) {
		nm_set_native_flags(na);
	} else {
		nm_clear_native_flags(na);
	}
	re_init_locked(adapter);	/* also enables intr */
	RL_UNLOCK(adapter);
	return (ifp->if_drv_flags & IFF_DRV_RUNNING ? 0 : 1);
}


/*
 * Reconcile kernel and user view of the transmit ring.
 */
static int
re_netmap_txsync(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct ifnet *ifp = na->ifp;
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap ring */
	u_int nic_i;	/* index into the NIC ring */
	u_int n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;

	/* device-specific */
	struct rl_softc *sc = ifp->if_softc;
	struct rl_txdesc *txd = sc->rl_ldata.rl_tx_desc;

	bus_dmamap_sync(sc->rl_ldata.rl_tx_list_tag,
	    sc->rl_ldata.rl_tx_list_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE); // XXX extra postwrite ?

	/*
	 * First part: process new packets to send.
	 */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {	/* we have new packets to send */
		nic_i = sc->rl_ldata.rl_tx_prodidx;
		// XXX or netmap_idx_k2n(kring, nm_i);

		for (n = 0; nm_i != head; n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			u_int len = slot->len;
			uint64_t paddr;
			void *addr = PNMB(na, slot, &paddr);

			/* device-specific */
			struct rl_desc *desc = &sc->rl_ldata.rl_tx_list[nic_i];
			int cmd = slot->len | RL_TDESC_CMD_EOF |
				RL_TDESC_CMD_OWN | RL_TDESC_CMD_SOF ;

			NM_CHECK_ADDR_LEN(na, addr, len);

			if (nic_i == lim)	/* mark end of ring */
				cmd |= RL_TDESC_CMD_EOR;

			if (slot->flags & NS_BUF_CHANGED) {
				/* buffer has changed, reload map */
				desc->rl_bufaddr_lo = htole32(RL_ADDR_LO(paddr));
				desc->rl_bufaddr_hi = htole32(RL_ADDR_HI(paddr));
				netmap_reload_map(na, sc->rl_ldata.rl_tx_mtag,
					txd[nic_i].tx_dmamap, addr);
			}
			slot->flags &= ~(NS_REPORT | NS_BUF_CHANGED);

			/* Fill the slot in the NIC ring. */
			desc->rl_cmdstat = htole32(cmd);

			/* make sure changes to the buffer are synced */
			bus_dmamap_sync(sc->rl_ldata.rl_tx_mtag,
				txd[nic_i].tx_dmamap,
				BUS_DMASYNC_PREWRITE);

			nm_i = nm_next(nm_i, lim);
			nic_i = nm_next(nic_i, lim);
		}
		sc->rl_ldata.rl_tx_prodidx = nic_i;
		kring->nr_hwcur = head;

		/* synchronize the NIC ring */
		bus_dmamap_sync(sc->rl_ldata.rl_tx_list_tag,
			sc->rl_ldata.rl_tx_list_map,
			BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/* start ? */
		CSR_WRITE_1(sc, sc->rl_txstart, RL_TXSTART_START);
	}

	/*
	 * Second part: reclaim buffers for completed transmissions.
	 */
	if (flags & NAF_FORCE_RECLAIM || nm_kr_txempty(kring)) {
		nic_i = sc->rl_ldata.rl_tx_considx;
		for (n = 0; nic_i != sc->rl_ldata.rl_tx_prodidx;
		    n++, nic_i = RL_TX_DESC_NXT(sc, nic_i)) {
			uint32_t cmdstat =
				le32toh(sc->rl_ldata.rl_tx_list[nic_i].rl_cmdstat);
			if (cmdstat & RL_TDESC_STAT_OWN)
				break;
		}
		if (n > 0) {
			sc->rl_ldata.rl_tx_considx = nic_i;
			sc->rl_ldata.rl_tx_free += n;
			kring->nr_hwtail = nm_prev(netmap_idx_n2k(kring, nic_i), lim);
		}
	}

	return 0;
}


/*
 * Reconcile kernel and user view of the receive ring.
 */
static int
re_netmap_rxsync(struct netmap_kring *kring, int flags)
{
	struct netmap_adapter *na = kring->na;
	struct ifnet *ifp = na->ifp;
	struct netmap_ring *ring = kring->ring;
	u_int nm_i;	/* index into the netmap ring */
	u_int nic_i;	/* index into the NIC ring */
	u_int n;
	u_int const lim = kring->nkr_num_slots - 1;
	u_int const head = kring->rhead;
	int force_update = (flags & NAF_FORCE_READ) || kring->nr_kflags & NKR_PENDINTR;

	/* device-specific */
	struct rl_softc *sc = ifp->if_softc;
	struct rl_rxdesc *rxd = sc->rl_ldata.rl_rx_desc;

	if (head > lim)
		return netmap_ring_reinit(kring);

	bus_dmamap_sync(sc->rl_ldata.rl_rx_list_tag,
			sc->rl_ldata.rl_rx_list_map,
			BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	/*
	 * First part: import newly received packets.
	 *
	 * This device uses all the buffers in the ring, so we need
	 * another termination condition in addition to RL_RDESC_STAT_OWN
	 * cleared (all buffers could have it cleared). The easiest one
	 * is to stop right before nm_hwcur.
	 */
	if (netmap_no_pendintr || force_update) {
		uint32_t stop_i = nm_prev(kring->nr_hwcur, lim);

		nic_i = sc->rl_ldata.rl_rx_prodidx; /* next pkt to check */
		nm_i = netmap_idx_n2k(kring, nic_i);

		while (nm_i != stop_i) {
			struct rl_desc *cur_rx = &sc->rl_ldata.rl_rx_list[nic_i];
			uint32_t rxstat = le32toh(cur_rx->rl_cmdstat);
			uint32_t total_len;

			if ((rxstat & RL_RDESC_STAT_OWN) != 0)
				break;
			total_len = rxstat & sc->rl_rxlenmask;
			/* XXX subtract crc */
			total_len = (total_len < 4) ? 0 : total_len - 4;
			ring->slot[nm_i].len = total_len;
			ring->slot[nm_i].flags = 0;
			/*  sync was in re_newbuf() */
			bus_dmamap_sync(sc->rl_ldata.rl_rx_mtag,
			    rxd[nic_i].rx_dmamap, BUS_DMASYNC_POSTREAD);
			// if_inc_counter(sc->rl_ifp, IFCOUNTER_IPACKETS, 1);
			nm_i = nm_next(nm_i, lim);
			nic_i = nm_next(nic_i, lim);
		}
		sc->rl_ldata.rl_rx_prodidx = nic_i;
		kring->nr_hwtail = nm_i;
		kring->nr_kflags &= ~NKR_PENDINTR;
	}

	/*
	 * Second part: skip past packets that userspace has released.
	 */
	nm_i = kring->nr_hwcur;
	if (nm_i != head) {
		nic_i = netmap_idx_k2n(kring, nm_i);
		for (n = 0; nm_i != head; n++) {
			struct netmap_slot *slot = &ring->slot[nm_i];
			uint64_t paddr;
			void *addr = PNMB(na, slot, &paddr);

			struct rl_desc *desc = &sc->rl_ldata.rl_rx_list[nic_i];
			int cmd = NETMAP_BUF_SIZE(na) | RL_RDESC_CMD_OWN;

			if (addr == NETMAP_BUF_BASE(na)) /* bad buf */
				goto ring_reset;

			if (nic_i == lim)	/* mark end of ring */
				cmd |= RL_RDESC_CMD_EOR;

			if (slot->flags & NS_BUF_CHANGED) {
				/* buffer has changed, reload map */
				desc->rl_bufaddr_lo = htole32(RL_ADDR_LO(paddr));
				desc->rl_bufaddr_hi = htole32(RL_ADDR_HI(paddr));
				netmap_reload_map(na, sc->rl_ldata.rl_rx_mtag,
					rxd[nic_i].rx_dmamap, addr);
				slot->flags &= ~NS_BUF_CHANGED;
			}
			desc->rl_cmdstat = htole32(cmd);
			bus_dmamap_sync(sc->rl_ldata.rl_rx_mtag,
			    rxd[nic_i].rx_dmamap,
			    BUS_DMASYNC_PREREAD);
			nm_i = nm_next(nm_i, lim);
			nic_i = nm_next(nic_i, lim);
		}
		kring->nr_hwcur = head;

		bus_dmamap_sync(sc->rl_ldata.rl_rx_list_tag,
		    sc->rl_ldata.rl_rx_list_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	return 0;

ring_reset:
	return netmap_ring_reinit(kring);
}


/*
 * Additional routines to init the tx and rx rings.
 * In other drivers we do that inline in the main code.
 */
static void
re_netmap_tx_init(struct rl_softc *sc)
{
	struct rl_txdesc *txd;
	struct rl_desc *desc;
	int i, n;
	struct netmap_adapter *na = NA(sc->rl_ifp);
	struct netmap_slot *slot;

	slot = netmap_reset(na, NR_TX, 0, 0);
	/* slot is NULL if we are not in native netmap mode */
	if (!slot)
		return;
	/* in netmap mode, overwrite addresses and maps */
	txd = sc->rl_ldata.rl_tx_desc;
	desc = sc->rl_ldata.rl_tx_list;
	n = sc->rl_ldata.rl_tx_desc_cnt;

	/* l points in the netmap ring, i points in the NIC ring */
	for (i = 0; i < n; i++) {
		uint64_t paddr;
		int l = netmap_idx_n2k(na->tx_rings[0], i);
		void *addr = PNMB(na, slot + l, &paddr);

		desc[i].rl_bufaddr_lo = htole32(RL_ADDR_LO(paddr));
		desc[i].rl_bufaddr_hi = htole32(RL_ADDR_HI(paddr));
		netmap_load_map(na, sc->rl_ldata.rl_tx_mtag,
			txd[i].tx_dmamap, addr);
	}
}

static void
re_netmap_rx_init(struct rl_softc *sc)
{
	struct netmap_adapter *na = NA(sc->rl_ifp);
	struct netmap_slot *slot = netmap_reset(na, NR_RX, 0, 0);
	struct rl_desc *desc = sc->rl_ldata.rl_rx_list;
	uint32_t cmdstat;
	uint32_t nic_i, max_avail;
	uint32_t const n = sc->rl_ldata.rl_rx_desc_cnt;

	if (!slot)
		return;
	/*
	 * Do not release the slots owned by userspace,
	 * and also keep one empty.
	 */
	max_avail = n - 1 - nm_kr_rxspace(na->rx_rings[0]);
	for (nic_i = 0; nic_i < n; nic_i++) {
		void *addr;
		uint64_t paddr;
		uint32_t nm_i = netmap_idx_n2k(na->rx_rings[0], nic_i);

		addr = PNMB(na, slot + nm_i, &paddr);

		netmap_reload_map(na, sc->rl_ldata.rl_rx_mtag,
		    sc->rl_ldata.rl_rx_desc[nic_i].rx_dmamap, addr);
		bus_dmamap_sync(sc->rl_ldata.rl_rx_mtag,
		    sc->rl_ldata.rl_rx_desc[nic_i].rx_dmamap, BUS_DMASYNC_PREREAD);
		desc[nic_i].rl_bufaddr_lo = htole32(RL_ADDR_LO(paddr));
		desc[nic_i].rl_bufaddr_hi = htole32(RL_ADDR_HI(paddr));
		cmdstat = NETMAP_BUF_SIZE(na);
		if (nic_i == n - 1) /* mark the end of ring */
			cmdstat |= RL_RDESC_CMD_EOR;
		if (nic_i < max_avail)
			cmdstat |= RL_RDESC_CMD_OWN;
		desc[nic_i].rl_cmdstat = htole32(cmdstat);
	}
}


static void
re_netmap_attach(struct rl_softc *sc)
{
	struct netmap_adapter na;

	bzero(&na, sizeof(na));

	na.ifp = sc->rl_ifp;
	na.na_flags = NAF_BDG_MAYSLEEP;
	na.num_tx_desc = sc->rl_ldata.rl_tx_desc_cnt;
	na.num_rx_desc = sc->rl_ldata.rl_rx_desc_cnt;
	na.nm_txsync = re_netmap_txsync;
	na.nm_rxsync = re_netmap_rxsync;
	na.nm_register = re_netmap_reg;
	na.num_tx_rings = na.num_rx_rings = 1;
	netmap_attach(&na);
}

/* end of file */
