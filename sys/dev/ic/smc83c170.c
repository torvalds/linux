/*	$OpenBSD: smc83c170.c,v 1.32 2024/11/05 18:58:59 miod Exp $	*/
/*	$NetBSD: smc83c170.c,v 1.59 2005/02/27 00:27:02 perry Exp $	*/

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Device driver for the Standard Microsystems Corp. 83C170
 * Ethernet PCI Integrated Controller (EPIC/100).
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>
#include <dev/mii/lxtphyreg.h>

#include <dev/ic/smc83c170reg.h>
#include <dev/ic/smc83c170var.h>

void	epic_start(struct ifnet *);
void	epic_watchdog(struct ifnet *);
int	epic_ioctl(struct ifnet *, u_long, caddr_t);
int	epic_init(struct ifnet *);
void	epic_stop(struct ifnet *, int);

void	epic_reset(struct epic_softc *);
void	epic_rxdrain(struct epic_softc *);
int	epic_add_rxbuf(struct epic_softc *, int);
void	epic_read_eeprom(struct epic_softc *, int, int, u_int16_t *);
void	epic_set_mchash(struct epic_softc *);
void	epic_fixup_clock_source(struct epic_softc *);
int	epic_mii_read(struct device *, int, int);
void	epic_mii_write(struct device *, int, int, int);
int	epic_mii_wait(struct epic_softc *, u_int32_t);
void	epic_tick(void *);

void	epic_statchg(struct device *);
int	epic_mediachange(struct ifnet *);
void	epic_mediastatus(struct ifnet *, struct ifmediareq *);

struct cfdriver epic_cd = {
	NULL, "epic", DV_IFNET
};

#define	INTMASK	(INTSTAT_FATAL_INT | INTSTAT_TXU | \
	    INTSTAT_TXC | INTSTAT_RXE | INTSTAT_RQE | INTSTAT_RCC)

int	epic_copy_small = 0;

#define	ETHER_PAD_LEN (ETHER_MIN_LEN - ETHER_CRC_LEN)

/*
 * Attach an EPIC interface to the system.
 */
void
epic_attach(struct epic_softc *sc, const char *intrstr)
{
	bus_space_tag_t st = sc->sc_st;
	bus_space_handle_t sh = sc->sc_sh;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int rseg, error, miiflags;
	u_int i;
	bus_dma_segment_t seg;
	u_int8_t enaddr[ETHER_ADDR_LEN], devname[12 + 1];
	u_int16_t myea[ETHER_ADDR_LEN / 2], mydevname[6];
	char *nullbuf;

	timeout_set(&sc->sc_mii_timeout, epic_tick, sc);

	/*
	 * Allocate the control data structures, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct epic_control_data) + ETHER_PAD_LEN, PAGE_SIZE, 0,
	    &seg, 1, &rseg, BUS_DMA_NOWAIT)) != 0) {
		printf(": unable to allocate control data, error = %d\n",
		    error);
		goto fail_0;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &seg, rseg,
	    sizeof(struct epic_control_data) + ETHER_PAD_LEN,
	    (caddr_t *)&sc->sc_control_data,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf(": unable to map control data, error = %d\n", error);
		goto fail_1;
	}
	nullbuf =
	    (char *)sc->sc_control_data + sizeof(struct epic_control_data);
	memset(nullbuf, 0, ETHER_PAD_LEN);

	if ((error = bus_dmamap_create(sc->sc_dmat,
	    sizeof(struct epic_control_data), 1,
	    sizeof(struct epic_control_data), 0, BUS_DMA_NOWAIT,
	    &sc->sc_cddmamap)) != 0) {
		printf(": unable to create control data DMA map, error = %d\n",
		    error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_cddmamap,
	    sc->sc_control_data, sizeof(struct epic_control_data), NULL,
	    BUS_DMA_NOWAIT)) != 0) {
		printf(": unable to load control data DMA map, error = %d\n",
		    error);
		goto fail_3;
	}

	/*
	 * Create the transmit buffer DMA maps.
	 */
	for (i = 0; i < EPIC_NTXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    EPIC_NFRAGS, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &EPIC_DSTX(sc, i)->ds_dmamap)) != 0) {
			printf(": unable to create tx DMA map %d, error = %d\n",
			    i, error);
			goto fail_4;
		}
	}

	/*
	 * Create the receive buffer DMA maps.
	 */
	for (i = 0; i < EPIC_NRXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &EPIC_DSRX(sc, i)->ds_dmamap)) != 0) {
			printf(": unable to create rx DMA map %d, error = %d\n",
			    i, error);
			goto fail_5;
		}
		EPIC_DSRX(sc, i)->ds_mbuf = NULL;
	}

	/*
	 * create and map the pad buffer
	 */
	if ((error = bus_dmamap_create(sc->sc_dmat, ETHER_PAD_LEN, 1,
	    ETHER_PAD_LEN, 0, BUS_DMA_NOWAIT,&sc->sc_nulldmamap)) != 0) {
		printf(": unable to create pad buffer DMA map, error = %d\n",
		    error);
		goto fail_5;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_nulldmamap,
	    nullbuf, ETHER_PAD_LEN, NULL, BUS_DMA_NOWAIT)) != 0) {
		printf(": unable to load pad buffer DMA map, error = %d\n",
		    error);
		goto fail_6;
	}
	bus_dmamap_sync(sc->sc_dmat, sc->sc_nulldmamap, 0, ETHER_PAD_LEN,
	    BUS_DMASYNC_PREWRITE);

	/*
	 * Bring the chip out of low-power mode and reset it to a known state.
	 */
	bus_space_write_4(st, sh, EPIC_GENCTL, 0);
	epic_reset(sc);

	/*
	 * Read the Ethernet address from the EEPROM.
	 */
	epic_read_eeprom(sc, 0, (sizeof(myea) / sizeof(myea[0])), myea);
	for (i = 0; i < sizeof(myea)/ sizeof(myea[0]); i++) {
		enaddr[i * 2]     = myea[i] & 0xff;
		enaddr[i * 2 + 1] = myea[i] >> 8;
	}

	/*
	 * ...and the device name.
	 */
	epic_read_eeprom(sc, 0x2c, (sizeof(mydevname) / sizeof(mydevname[0])),
	    mydevname);
	for (i = 0; i < sizeof(mydevname) / sizeof(mydevname[0]); i++) {
		devname[i * 2]     = mydevname[i] & 0xff;
		devname[i * 2 + 1] = mydevname[i] >> 8;
	}

	devname[sizeof(devname) - 1] = ' ';
	for (i = sizeof(devname) - 1; devname[i] == ' '; i--) {
		devname[i] = '\0';
		if (i == 0)
			break;
	}

	printf(", %s : %s, address %s\n", devname, intrstr,
	    ether_sprintf(enaddr));

	miiflags = 0;
	if (sc->sc_hwflags & EPIC_HAS_MII_FIBER)
		miiflags |= MIIF_HAVEFIBER;

	/*
	 * Initialize our media structures and probe the MII.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = epic_mii_read;
	sc->sc_mii.mii_writereg = epic_mii_write;
	sc->sc_mii.mii_statchg = epic_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, IFM_IMASK, epic_mediachange,
	    epic_mediastatus);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, miiflags);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	if (sc->sc_hwflags & EPIC_HAS_BNC) {
		/* use the next free media instance */
		sc->sc_serinst = sc->sc_mii.mii_instance++;
		ifmedia_add(&sc->sc_mii.mii_media,
			    IFM_MAKEWORD(IFM_ETHER, IFM_10_2, 0,
					 sc->sc_serinst),
			    0, NULL);
	} else
		sc->sc_serinst = -1;

	bcopy(enaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = epic_ioctl;
	ifp->if_start = epic_start;
	ifp->if_watchdog = epic_watchdog;
	ifq_init_maxlen(&ifp->if_snd, EPIC_NTXDESC - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/*
	 * Attach the interface.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);
	return;

	/*
	 * Free any resources we've allocated during the failed attach
	 * attempt.  Do this in reverse order and fall through.
	 */
 fail_6:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_nulldmamap);
 fail_5:
	for (i = 0; i < EPIC_NRXDESC; i++) {
		if (EPIC_DSRX(sc, i)->ds_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    EPIC_DSRX(sc, i)->ds_dmamap);
	}
 fail_4:
	for (i = 0; i < EPIC_NTXDESC; i++) {
		if (EPIC_DSTX(sc, i)->ds_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    EPIC_DSTX(sc, i)->ds_dmamap);
	}
	bus_dmamap_unload(sc->sc_dmat, sc->sc_cddmamap);
 fail_3:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_cddmamap);
 fail_2:
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_control_data,
	    sizeof(struct epic_control_data));
 fail_1:
	bus_dmamem_free(sc->sc_dmat, &seg, rseg);
 fail_0:
	return;
}

/*
 * Start packet transmission on the interface.
 * [ifnet interface function]
 */
void
epic_start(struct ifnet *ifp)
{
	struct epic_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
	struct epic_txdesc *txd;
	struct epic_descsoft *ds;
	struct epic_fraglist *fr;
	bus_dmamap_t dmamap;
	int error, firsttx, nexttx, opending, seg;
	u_int len;

	/*
	 * Remember the previous txpending and the first transmit
	 * descriptor we use.
	 */
	opending = sc->sc_txpending;
	firsttx = EPIC_NEXTTX(sc->sc_txlast);

	/*
	 * Loop through the send queue, setting up transmit descriptors
	 * until we drain the queue, or use up all available transmit
	 * descriptors.
	 */
	while (sc->sc_txpending < EPIC_NTXDESC) {
		/*
		 * Grab a packet off the queue.
		 */
		m0 = ifq_deq_begin(&ifp->if_snd);
		if (m0 == NULL)
			break;
		m = NULL;

		/*
		 * Get the last and next available transmit descriptor.
		 */
		nexttx = EPIC_NEXTTX(sc->sc_txlast);
		txd = EPIC_CDTX(sc, nexttx);
		fr = EPIC_CDFL(sc, nexttx);
		ds = EPIC_DSTX(sc, nexttx);
		dmamap = ds->ds_dmamap;

		/*
		 * Load the DMA map.  If this fails, the packet either
		 * didn't fit in the allotted number of frags, or we were
		 * short on resources.  In this case, we'll copy and try
		 * again.
		 */
		if ((error = bus_dmamap_load_mbuf(sc->sc_dmat, dmamap, m0,
		    BUS_DMA_WRITE|BUS_DMA_NOWAIT)) != 0 ||
		    (m0->m_pkthdr.len < ETHER_PAD_LEN &&
		    dmamap-> dm_nsegs == EPIC_NFRAGS)) {
			if (error == 0)
				bus_dmamap_unload(sc->sc_dmat, dmamap);

			MGETHDR(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				ifq_deq_rollback(&ifp->if_snd, m0);
				break;
			}
			if (m0->m_pkthdr.len > MHLEN) {
				MCLGET(m, M_DONTWAIT);
				if ((m->m_flags & M_EXT) == 0) {
					m_freem(m);
					ifq_deq_rollback(&ifp->if_snd, m0);
					break;
				}
			}
			m_copydata(m0, 0, m0->m_pkthdr.len, mtod(m, caddr_t));
			m->m_pkthdr.len = m->m_len = m0->m_pkthdr.len;
			error = bus_dmamap_load_mbuf(sc->sc_dmat, dmamap,
			    m, BUS_DMA_WRITE|BUS_DMA_NOWAIT);
			if (error) {
				ifq_deq_rollback(&ifp->if_snd, m0);
				break;
			}
		}
		ifq_deq_commit(&ifp->if_snd, m0);
		if (m != NULL) {
			m_freem(m0);
			m0 = m;
		}

		/* Initialize the fraglist. */
		for (seg = 0; seg < dmamap->dm_nsegs; seg++) {
			fr->ef_frags[seg].ef_addr =
			    dmamap->dm_segs[seg].ds_addr;
			fr->ef_frags[seg].ef_length =
			    dmamap->dm_segs[seg].ds_len;
		}
		len = m0->m_pkthdr.len;
		if (len < ETHER_PAD_LEN) {
			fr->ef_frags[seg].ef_addr = sc->sc_nulldma;
			fr->ef_frags[seg].ef_length = ETHER_PAD_LEN - len;
			len = ETHER_PAD_LEN;
			seg++;
		}
		fr->ef_nfrags = seg;

		EPIC_CDFLSYNC(sc, nexttx, BUS_DMASYNC_PREWRITE);

		/* Sync the DMA map. */
		bus_dmamap_sync(sc->sc_dmat, dmamap, 0, dmamap->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		/*
		 * Store a pointer to the packet so we can free it later.
		 */
		ds->ds_mbuf = m0;

		/*
		 * Fill in the transmit descriptor.
		 */
		txd->et_control = ET_TXCTL_LASTDESC | ET_TXCTL_FRAGLIST;

		/*
		 * If this is the first descriptor we're enqueueing,
		 * don't give it to the EPIC yet.  That could cause
		 * a race condition.  We'll do it below.
		 */
		if (nexttx == firsttx)
			txd->et_txstatus = TXSTAT_TXLENGTH(len);
		else
			txd->et_txstatus =
			    TXSTAT_TXLENGTH(len) | ET_TXSTAT_OWNER;

		EPIC_CDTXSYNC(sc, nexttx,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/* Advance the tx pointer. */
		sc->sc_txpending++;
		sc->sc_txlast = nexttx;

#if NBPFILTER > 0
		/*
		 * Pass the packet to any BPF listeners.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
	}

	if (sc->sc_txpending == EPIC_NTXDESC) {
		/* No more slots left; notify upper layer. */
		ifq_set_oactive(&ifp->if_snd);
	}

	if (sc->sc_txpending != opending) {
		/*
		 * We enqueued packets.  If the transmitter was idle,
		 * reset the txdirty pointer.
		 */
		if (opending == 0)
			sc->sc_txdirty = firsttx;

		/*
		 * Cause a transmit interrupt to happen on the
		 * last packet we enqueued.
		 */
		EPIC_CDTX(sc, sc->sc_txlast)->et_control |= ET_TXCTL_IAF;
		EPIC_CDTXSYNC(sc, sc->sc_txlast,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/*
		 * The entire packet chain is set up.  Give the
		 * first descriptor to the EPIC now.
		 */
		EPIC_CDTX(sc, firsttx)->et_txstatus |= ET_TXSTAT_OWNER;
		EPIC_CDTXSYNC(sc, firsttx,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/* Start the transmitter. */
		bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_COMMAND,
		    COMMAND_TXQUEUED);

		/* Set a watchdog timer in case the chip flakes out. */
		ifp->if_timer = 5;
	}
}

/*
 * Watchdog timer handler.
 * [ifnet interface function]
 */
void
epic_watchdog(struct ifnet *ifp)
{
	struct epic_softc *sc = ifp->if_softc;

	printf("%s: device timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;

	(void) epic_init(ifp);
}

/*
 * Handle control requests from the operator.
 * [ifnet interface function]
 */
int
epic_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct epic_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		epic_init(ifp);
		break;

	case SIOCSIFFLAGS:
		/*
		 * If interface is marked up and not running, then start it.
		 * If it is marked down and running, stop it.
		 * XXX If it's up then re-initialize it. This is so flags
		 * such as IFF_PROMISC are handled.
		 */
		if (ifp->if_flags & IFF_UP)
			epic_init(ifp);
		else if (ifp->if_flags & IFF_RUNNING)
			epic_stop(ifp, 1);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING) {
			mii_pollstat(&sc->sc_mii);
			epic_set_mchash(sc);
		}
		error = 0;
	}

	splx(s);
	return (error);
}

/*
 * Interrupt handler.
 */
int
epic_intr(void *arg)
{
	struct epic_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct epic_rxdesc *rxd;
	struct epic_txdesc *txd;
	struct epic_descsoft *ds;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	u_int32_t intstat, rxstatus, txstatus;
	int i, claimed = 0;
	u_int len;

	/*
	 * Get the interrupt status from the EPIC.
	 */
	intstat = bus_space_read_4(sc->sc_st, sc->sc_sh, EPIC_INTSTAT);
	if ((intstat & INTSTAT_INT_ACTV) == 0)
		return (claimed);

	claimed = 1;

	/*
	 * Acknowledge the interrupt.
	 */
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_INTSTAT,
	    intstat & INTMASK);

	/*
	 * Check for receive interrupts.
	 */
	if (intstat & (INTSTAT_RCC | INTSTAT_RXE | INTSTAT_RQE)) {
		for (i = sc->sc_rxptr;; i = EPIC_NEXTRX(i)) {
			rxd = EPIC_CDRX(sc, i);
			ds = EPIC_DSRX(sc, i);

			EPIC_CDRXSYNC(sc, i,
			    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

			rxstatus = rxd->er_rxstatus;
			if (rxstatus & ER_RXSTAT_OWNER) {
				/*
				 * We have processed all of the
				 * receive buffers.
				 */
				break;
			}

			/*
			 * Make sure the packet arrived intact.  If an error
			 * occurred, update stats and reset the descriptor.
			 * The buffer will be reused the next time the
			 * descriptor comes up in the ring.
			 */
			if ((rxstatus & ER_RXSTAT_PKTINTACT) == 0) {
				if (rxstatus & ER_RXSTAT_CRCERROR)
					printf("%s: CRC error\n",
					    sc->sc_dev.dv_xname);
				if (rxstatus & ER_RXSTAT_ALIGNERROR)
					printf("%s: alignment error\n",
					    sc->sc_dev.dv_xname);
				ifp->if_ierrors++;
				EPIC_INIT_RXDESC(sc, i);
				continue;
			}

			bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
			    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

			/*
			 * The EPIC includes the CRC with every packet;
			 * trim it.
			 */
			len = RXSTAT_RXLENGTH(rxstatus) - ETHER_CRC_LEN;

			if (len < sizeof(struct ether_header)) {
				/*
				 * Runt packet; drop it now.
				 */
				ifp->if_ierrors++;
				EPIC_INIT_RXDESC(sc, i);
				bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
				    ds->ds_dmamap->dm_mapsize,
				    BUS_DMASYNC_PREREAD);
				continue;
			}

			/*
			 * If the packet is small enough to fit in a
			 * single header mbuf, allocate one and copy
			 * the data into it.  This greatly reduces
			 * memory consumption when we receive lots
			 * of small packets.
			 *
			 * Otherwise, we add a new buffer to the receive
			 * chain.  If this fails, we drop the packet and
			 * recycle the old buffer.
			 */
			if (epic_copy_small != 0 && len <= MHLEN) {
				MGETHDR(m, M_DONTWAIT, MT_DATA);
				if (m == NULL)
					goto dropit;
				memcpy(mtod(m, caddr_t),
				    mtod(ds->ds_mbuf, caddr_t), len);
				EPIC_INIT_RXDESC(sc, i);
				bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
				    ds->ds_dmamap->dm_mapsize,
				    BUS_DMASYNC_PREREAD);
			} else {
				m = ds->ds_mbuf;
				if (epic_add_rxbuf(sc, i) != 0) {
 dropit:
					ifp->if_ierrors++;
					EPIC_INIT_RXDESC(sc, i);
					bus_dmamap_sync(sc->sc_dmat,
					    ds->ds_dmamap, 0,
					    ds->ds_dmamap->dm_mapsize,
					    BUS_DMASYNC_PREREAD);
					continue;
				}
			}

			m->m_pkthdr.len = m->m_len = len;

			ml_enqueue(&ml, m);
		}

		/* Update the receive pointer. */
		sc->sc_rxptr = i;

		/*
		 * Check for receive queue underflow.
		 */
		if (intstat & INTSTAT_RQE) {
			printf("%s: receiver queue empty\n",
			    sc->sc_dev.dv_xname);
			/*
			 * Ring is already built; just restart the
			 * receiver.
			 */
			bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_PRCDAR,
			    EPIC_CDRXADDR(sc, sc->sc_rxptr));
			bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_COMMAND,
			    COMMAND_RXQUEUED | COMMAND_START_RX);
		}
	}

	if_input(ifp, &ml);

	/*
	 * Check for transmission complete interrupts.
	 */
	if (intstat & (INTSTAT_TXC | INTSTAT_TXU)) {
		ifq_clr_oactive(&ifp->if_snd);
		for (i = sc->sc_txdirty; sc->sc_txpending != 0;
		     i = EPIC_NEXTTX(i), sc->sc_txpending--) {
			txd = EPIC_CDTX(sc, i);
			ds = EPIC_DSTX(sc, i);

			EPIC_CDTXSYNC(sc, i,
			    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

			txstatus = txd->et_txstatus;
			if (txstatus & ET_TXSTAT_OWNER)
				break;

			EPIC_CDFLSYNC(sc, i, BUS_DMASYNC_POSTWRITE);

			bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap,
			    0, ds->ds_dmamap->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
			m_freem(ds->ds_mbuf);
			ds->ds_mbuf = NULL;

			/*
			 * Check for errors and collisions.
			 */
			if ((txstatus & ET_TXSTAT_PACKETTX) == 0)
				ifp->if_oerrors++;
			ifp->if_collisions +=
			    TXSTAT_COLLISIONS(txstatus);
			if (txstatus & ET_TXSTAT_CARSENSELOST)
				printf("%s: lost carrier\n",
				    sc->sc_dev.dv_xname);
		}

		/* Update the dirty transmit buffer pointer. */
		sc->sc_txdirty = i;

		/*
		 * Cancel the watchdog timer if there are no pending
		 * transmissions.
		 */
		if (sc->sc_txpending == 0)
			ifp->if_timer = 0;

		/*
		 * Kick the transmitter after a DMA underrun.
		 */
		if (intstat & INTSTAT_TXU) {
			printf("%s: transmit underrun\n", sc->sc_dev.dv_xname);
			bus_space_write_4(sc->sc_st, sc->sc_sh,
			    EPIC_COMMAND, COMMAND_TXUGO);
			if (sc->sc_txpending)
				bus_space_write_4(sc->sc_st, sc->sc_sh,
				    EPIC_COMMAND, COMMAND_TXQUEUED);
		}

		/*
		 * Try to get more packets going.
		 */
		epic_start(ifp);
	}

	/*
	 * Check for fatal interrupts.
	 */
	if (intstat & INTSTAT_FATAL_INT) {
		if (intstat & INTSTAT_PTA)
			printf("%s: PCI target abort error\n",
			    sc->sc_dev.dv_xname);
		else if (intstat & INTSTAT_PMA)
			printf("%s: PCI master abort error\n",
			    sc->sc_dev.dv_xname);
		else if (intstat & INTSTAT_APE)
			printf("%s: PCI address parity error\n",
			    sc->sc_dev.dv_xname);
		else if (intstat & INTSTAT_DPE)
			printf("%s: PCI data parity error\n",
			    sc->sc_dev.dv_xname);
		else
			printf("%s: unknown fatal error\n",
			    sc->sc_dev.dv_xname);
		(void) epic_init(ifp);
	}

	return (claimed);
}

/*
 * One second timer, used to tick the MII.
 */
void
epic_tick(void *arg)
{
	struct epic_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_mii_timeout, 1);
}

/*
 * Fixup the clock source on the EPIC.
 */
void
epic_fixup_clock_source(struct epic_softc *sc)
{
	int i;

	/*
	 * According to SMC Application Note 7-15, the EPIC's clock
	 * source is incorrect following a reset.  This manifests itself
	 * as failure to recognize when host software has written to
	 * a register on the EPIC.  The appnote recommends issuing at
	 * least 16 consecutive writes to the CLOCK TEST bit to correctly
	 * configure the clock source.
	 */
	for (i = 0; i < 16; i++)
		bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_TEST,
		    TEST_CLOCKTEST);
}

/*
 * Perform a soft reset on the EPIC.
 */
void
epic_reset(struct epic_softc *sc)
{

	epic_fixup_clock_source(sc);

	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_GENCTL, 0);
	delay(100);
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_GENCTL, GENCTL_SOFTRESET);
	delay(100);

	epic_fixup_clock_source(sc);
}

/*
 * Initialize the interface.  Must be called at splnet().
 */
int
epic_init(struct ifnet *ifp)
{
	struct epic_softc *sc = ifp->if_softc;
	bus_space_tag_t st = sc->sc_st;
	bus_space_handle_t sh = sc->sc_sh;
	struct epic_txdesc *txd;
	struct epic_descsoft *ds;
	u_int32_t genctl, reg0;
	int i, error = 0;

	/*
	 * Cancel any pending I/O.
	 */
	epic_stop(ifp, 0);

	/*
	 * Reset the EPIC to a known state.
	 */
	epic_reset(sc);

	/*
	 * Magical mystery initialization.
	 */
	bus_space_write_4(st, sh, EPIC_TXTEST, 0);

	/*
	 * Initialize the EPIC genctl register:
	 *
	 *	- 64 byte receive FIFO threshold
	 *	- automatic advance to next receive frame
	 */
	genctl = GENCTL_RX_FIFO_THRESH0 | GENCTL_ONECOPY;
#if BYTE_ORDER == BIG_ENDIAN
	genctl |= GENCTL_BIG_ENDIAN;
#endif
	bus_space_write_4(st, sh, EPIC_GENCTL, genctl);

	/*
	 * Reset the MII bus and PHY.
	 */
	reg0 = bus_space_read_4(st, sh, EPIC_NVCTL);
	bus_space_write_4(st, sh, EPIC_NVCTL, reg0 | NVCTL_GPIO1 | NVCTL_GPOE1);
	bus_space_write_4(st, sh, EPIC_MIICFG, MIICFG_ENASER);
	bus_space_write_4(st, sh, EPIC_GENCTL, genctl | GENCTL_RESET_PHY);
	delay(100);
	bus_space_write_4(st, sh, EPIC_GENCTL, genctl);
	delay(1000);
	bus_space_write_4(st, sh, EPIC_NVCTL, reg0);

	/*
	 * Initialize Ethernet address.
	 */
	reg0 = sc->sc_arpcom.ac_enaddr[1] << 8 | sc->sc_arpcom.ac_enaddr[0];
	bus_space_write_4(st, sh, EPIC_LAN0, reg0);
	reg0 = sc->sc_arpcom.ac_enaddr[3] << 8 | sc->sc_arpcom.ac_enaddr[2];
	bus_space_write_4(st, sh, EPIC_LAN1, reg0);
	reg0 = sc->sc_arpcom.ac_enaddr[5] << 8 | sc->sc_arpcom.ac_enaddr[4];
	bus_space_write_4(st, sh, EPIC_LAN2, reg0);

	/*
	 * Initialize receive control.  Remember the external buffer
	 * size setting.
	 */
	reg0 = bus_space_read_4(st, sh, EPIC_RXCON) &
	    (RXCON_EXTBUFSIZESEL1 | RXCON_EXTBUFSIZESEL0);
	reg0 |= (RXCON_RXMULTICAST | RXCON_RXBROADCAST);
	if (ifp->if_flags & IFF_PROMISC)
		reg0 |= RXCON_PROMISCMODE;
	bus_space_write_4(st, sh, EPIC_RXCON, reg0);

	/* Set the current media. */
	epic_mediachange(ifp);

	/* Set up the multicast hash table. */
	epic_set_mchash(sc);

	/*
	 * Initialize the transmit descriptor ring.  txlast is initialized
	 * to the end of the list so that it will wrap around to the first
	 * descriptor when the first packet is transmitted.
	 */
	for (i = 0; i < EPIC_NTXDESC; i++) {
		txd = EPIC_CDTX(sc, i);
		memset(txd, 0, sizeof(struct epic_txdesc));
		txd->et_bufaddr = EPIC_CDFLADDR(sc, i);
		txd->et_nextdesc = EPIC_CDTXADDR(sc, EPIC_NEXTTX(i));
		EPIC_CDTXSYNC(sc, i, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	}
	sc->sc_txpending = 0;
	sc->sc_txdirty = 0;
	sc->sc_txlast = EPIC_NTXDESC - 1;

	/*
	 * Initialize the receive descriptor ring.
	 */
	for (i = 0; i < EPIC_NRXDESC; i++) {
		ds = EPIC_DSRX(sc, i);
		if (ds->ds_mbuf == NULL) {
			if ((error = epic_add_rxbuf(sc, i)) != 0) {
				printf("%s: unable to allocate or map rx "
				    "buffer %d error = %d\n",
				    sc->sc_dev.dv_xname, i, error);
				/*
				 * XXX Should attempt to run with fewer receive
				 * XXX buffers instead of just failing.
				 */
				epic_rxdrain(sc);
				goto out;
			}
		} else
			EPIC_INIT_RXDESC(sc, i);
	}
	sc->sc_rxptr = 0;

	/*
	 * Initialize the interrupt mask and enable interrupts.
	 */
	bus_space_write_4(st, sh, EPIC_INTMASK, INTMASK);
	bus_space_write_4(st, sh, EPIC_GENCTL, genctl | GENCTL_INTENA);

	/*
	 * Give the transmit and receive rings to the EPIC.
	 */
	bus_space_write_4(st, sh, EPIC_PTCDAR,
	    EPIC_CDTXADDR(sc, EPIC_NEXTTX(sc->sc_txlast)));
	bus_space_write_4(st, sh, EPIC_PRCDAR,
	    EPIC_CDRXADDR(sc, sc->sc_rxptr));

	/*
	 * Set the EPIC in motion.
	 */
	bus_space_write_4(st, sh, EPIC_COMMAND,
	    COMMAND_RXQUEUED | COMMAND_START_RX);

	/*
	 * ...all done!
	 */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Start the one second clock.
	 */
	timeout_add_sec(&sc->sc_mii_timeout, 1);

	/*
	 * Attempt to start output on the interface.
	 */
	epic_start(ifp);

 out:
	if (error)
		printf("%s: interface not running\n", sc->sc_dev.dv_xname);
	return (error);
}

/*
 * Drain the receive queue.
 */
void
epic_rxdrain(struct epic_softc *sc)
{
	struct epic_descsoft *ds;
	int i;

	for (i = 0; i < EPIC_NRXDESC; i++) {
		ds = EPIC_DSRX(sc, i);
		if (ds->ds_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
			m_freem(ds->ds_mbuf);
			ds->ds_mbuf = NULL;
		}
	}
}

/*
 * Stop transmission on the interface.
 */
void
epic_stop(struct ifnet *ifp, int disable)
{
	struct epic_softc *sc = ifp->if_softc;
	bus_space_tag_t st = sc->sc_st;
	bus_space_handle_t sh = sc->sc_sh;
	struct epic_descsoft *ds;
	u_int32_t reg;
	int i;

	/*
	 * Stop the one second clock.
	 */
	timeout_del(&sc->sc_mii_timeout);

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	/* Down the MII. */
	mii_down(&sc->sc_mii);

	/* Paranoia... */
	epic_fixup_clock_source(sc);

	/*
	 * Disable interrupts.
	 */
	reg = bus_space_read_4(st, sh, EPIC_GENCTL);
	bus_space_write_4(st, sh, EPIC_GENCTL, reg & ~GENCTL_INTENA);
	bus_space_write_4(st, sh, EPIC_INTMASK, 0);

	/*
	 * Stop the DMA engine and take the receiver off-line.
	 */
	bus_space_write_4(st, sh, EPIC_COMMAND, COMMAND_STOP_RDMA |
	    COMMAND_STOP_TDMA | COMMAND_STOP_RX);

	/*
	 * Release any queued transmit buffers.
	 */
	for (i = 0; i < EPIC_NTXDESC; i++) {
		ds = EPIC_DSTX(sc, i);
		if (ds->ds_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
			m_freem(ds->ds_mbuf);
			ds->ds_mbuf = NULL;
		}
	}

	if (disable)
		epic_rxdrain(sc);
}

/*
 * Read the EPIC Serial EEPROM.
 */
void
epic_read_eeprom(struct epic_softc *sc, int word, int wordcnt, u_int16_t *data)
{
	bus_space_tag_t st = sc->sc_st;
	bus_space_handle_t sh = sc->sc_sh;
	u_int16_t reg;
	int i, x;

#define	EEPROM_WAIT_READY(st, sh) \
	while ((bus_space_read_4((st), (sh), EPIC_EECTL) & EECTL_EERDY) == 0) \
		/* nothing */

	/*
	 * Enable the EEPROM.
	 */
	bus_space_write_4(st, sh, EPIC_EECTL, EECTL_ENABLE);
	EEPROM_WAIT_READY(st, sh);

	for (i = 0; i < wordcnt; i++) {
		/* Send CHIP SELECT for one clock tick. */
		bus_space_write_4(st, sh, EPIC_EECTL, EECTL_ENABLE|EECTL_EECS);
		EEPROM_WAIT_READY(st, sh);

		/* Shift in the READ opcode. */
		for (x = 3; x > 0; x--) {
			reg = EECTL_ENABLE|EECTL_EECS;
			if (EPIC_EEPROM_OPC_READ & (1 << (x - 1)))
				reg |= EECTL_EEDI;
			bus_space_write_4(st, sh, EPIC_EECTL, reg);
			EEPROM_WAIT_READY(st, sh);
			bus_space_write_4(st, sh, EPIC_EECTL, reg|EECTL_EESK);
			EEPROM_WAIT_READY(st, sh);
			bus_space_write_4(st, sh, EPIC_EECTL, reg);
			EEPROM_WAIT_READY(st, sh);
		}

		/* Shift in address. */
		for (x = 6; x > 0; x--) {
			reg = EECTL_ENABLE|EECTL_EECS;
			if ((word + i) & (1 << (x - 1)))
				reg |= EECTL_EEDI;
			bus_space_write_4(st, sh, EPIC_EECTL, reg);
			EEPROM_WAIT_READY(st, sh);
			bus_space_write_4(st, sh, EPIC_EECTL, reg|EECTL_EESK);
			EEPROM_WAIT_READY(st, sh);
			bus_space_write_4(st, sh, EPIC_EECTL, reg);
			EEPROM_WAIT_READY(st, sh);
		}

		/* Shift out data. */
		reg = EECTL_ENABLE|EECTL_EECS;
		data[i] = 0;
		for (x = 16; x > 0; x--) {
			bus_space_write_4(st, sh, EPIC_EECTL, reg|EECTL_EESK);
			EEPROM_WAIT_READY(st, sh);
			if (bus_space_read_4(st, sh, EPIC_EECTL) & EECTL_EEDO)
				data[i] |= (1 << (x - 1));
			bus_space_write_4(st, sh, EPIC_EECTL, reg);
			EEPROM_WAIT_READY(st, sh);
		}

		/* Clear CHIP SELECT. */
		bus_space_write_4(st, sh, EPIC_EECTL, EECTL_ENABLE);
		EEPROM_WAIT_READY(st, sh);
	}

	/*
	 * Disable the EEPROM.
	 */
	bus_space_write_4(st, sh, EPIC_EECTL, 0);

#undef EEPROM_WAIT_READY
}

/*
 * Add a receive buffer to the indicated descriptor.
 */
int
epic_add_rxbuf(struct epic_softc *sc, int idx)
{
	struct epic_descsoft *ds = EPIC_DSRX(sc, idx);
	struct mbuf *m;
	int error;

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);

	MCLGET(m, M_DONTWAIT);
	if ((m->m_flags & M_EXT) == 0) {
		m_freem(m);
		return (ENOBUFS);
	}

	if (ds->ds_mbuf != NULL)
		bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);

	ds->ds_mbuf = m;

	error = bus_dmamap_load(sc->sc_dmat, ds->ds_dmamap,
	    m->m_ext.ext_buf, m->m_ext.ext_size, NULL,
	    BUS_DMA_READ|BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: can't load rx DMA map %d, error = %d\n",
		    sc->sc_dev.dv_xname, idx, error);
		panic("epic_add_rxbuf");	/* XXX */
	}

	bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
	    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	EPIC_INIT_RXDESC(sc, idx);

	return (0);
}

/*
 * Set the EPIC multicast hash table.
 *
 * NOTE: We rely on a recently-updated mii_media_active here!
 */
void
epic_set_mchash(struct epic_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int32_t hash, mchash[4];

	/*
	 * Set up the multicast address filter by passing all multicast
	 * addresses through a CRC generator, and then using the low-order
	 * 6 bits as an index into the 64 bit multicast hash table (only
	 * the lower 16 bits of each 32 bit multicast hash register are
	 * valid).  The high order bits select the register, while the
	 * rest of the bits select the bit within the register.
	 */

	if (ifp->if_flags & IFF_PROMISC)
		goto allmulti;

	if (IFM_SUBTYPE(sc->sc_mii.mii_media_active) == IFM_10_T) {
		/* XXX hardware bug in 10Mbps mode. */
		goto allmulti;
	}

	if (ac->ac_multirangecnt > 0)
		goto allmulti;

	mchash[0] = mchash[1] = mchash[2] = mchash[3] = 0;

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		hash = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN);
		hash >>= 26;

		/* Set the corresponding bit in the hash table. */
		mchash[hash >> 4] |= 1 << (hash & 0xf);

		ETHER_NEXT_MULTI(step, enm);
	}

	ifp->if_flags &= ~IFF_ALLMULTI;
	goto sethash;

 allmulti:
	ifp->if_flags |= IFF_ALLMULTI;
	mchash[0] = mchash[1] = mchash[2] = mchash[3] = 0xffff;

 sethash:
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MC0, mchash[0]);
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MC1, mchash[1]);
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MC2, mchash[2]);
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MC3, mchash[3]);
}

/*
 * Wait for the MII to become ready.
 */
int
epic_mii_wait(struct epic_softc *sc, u_int32_t rw)
{
	int i;

	for (i = 0; i < 50; i++) {
		if ((bus_space_read_4(sc->sc_st, sc->sc_sh, EPIC_MMCTL) & rw)
		    == 0)
			break;
		delay(2);
	}
	if (i == 50) {
		printf("%s: MII timed out\n", sc->sc_dev.dv_xname);
		return (1);
	}

	return (0);
}

/*
 * Read from the MII.
 */
int
epic_mii_read(struct device *self, int phy, int reg)
{
	struct epic_softc *sc = (struct epic_softc *)self;

	if (epic_mii_wait(sc, MMCTL_WRITE))
		return (0);

	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MMCTL,
	    MMCTL_ARG(phy, reg, MMCTL_READ));

	if (epic_mii_wait(sc, MMCTL_READ))
		return (0);

	return (bus_space_read_4(sc->sc_st, sc->sc_sh, EPIC_MMDATA) &
	    MMDATA_MASK);
}

/*
 * Write to the MII.
 */
void
epic_mii_write(struct device *self, int phy, int reg, int val)
{
	struct epic_softc *sc = (struct epic_softc *)self;

	if (epic_mii_wait(sc, MMCTL_WRITE))
		return;

	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MMDATA, val);
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MMCTL,
	    MMCTL_ARG(phy, reg, MMCTL_WRITE));
}

/*
 * Callback from PHY when media changes.
 */
void
epic_statchg(struct device *self)
{
	struct epic_softc *sc = (struct epic_softc *)self;
	u_int32_t txcon, miicfg;

	/*
	 * Update loopback bits in TXCON to reflect duplex mode.
	 */
	txcon = bus_space_read_4(sc->sc_st, sc->sc_sh, EPIC_TXCON);
	if (sc->sc_mii.mii_media_active & IFM_FDX)
		txcon |= (TXCON_LOOPBACK_D1|TXCON_LOOPBACK_D2);
	else
		txcon &= ~(TXCON_LOOPBACK_D1|TXCON_LOOPBACK_D2);
	bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_TXCON, txcon);

	/* On some cards we need manually set fullduplex led */
	if (sc->sc_hwflags & EPIC_DUPLEXLED_ON_694) {
		miicfg = bus_space_read_4(sc->sc_st, sc->sc_sh, EPIC_MIICFG);
		if (IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX)
			miicfg |= MIICFG_ENABLE;
		else
			miicfg &= ~MIICFG_ENABLE;
		bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MIICFG, miicfg);
	}

	/*
	 * There is a multicast filter bug in 10Mbps mode.  Kick the
	 * multicast filter in case the speed changed.
	 */
	epic_set_mchash(sc);
}

/*
 * Callback from ifmedia to request current media status.
 */
void
epic_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct epic_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

/*
 * Callback from ifmedia to request new media setting.
 */
int
epic_mediachange(struct ifnet *ifp)
{
	struct epic_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;
	struct ifmedia *ifm = &mii->mii_media;
	uint64_t media = ifm->ifm_cur->ifm_media;
	u_int32_t miicfg;
	struct mii_softc *miisc;
	int cfg;

	if (!(ifp->if_flags & IFF_UP))
		return (0);

	if (IFM_INST(media) != sc->sc_serinst) {
		/* If we're not selecting serial interface, select MII mode */
#ifdef EPICMEDIADEBUG
		printf("%s: parallel mode\n", ifp->if_xname);
#endif
		miicfg = bus_space_read_4(sc->sc_st, sc->sc_sh, EPIC_MIICFG);
		miicfg &= ~MIICFG_SERMODEENA;
		bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MIICFG, miicfg);
	}

	mii_mediachg(mii);

	if (IFM_INST(media) == sc->sc_serinst) {
		/* select serial interface */
#ifdef EPICMEDIADEBUG
		printf("%s: serial mode\n", ifp->if_xname);
#endif
		miicfg = bus_space_read_4(sc->sc_st, sc->sc_sh, EPIC_MIICFG);
		miicfg |= (MIICFG_SERMODEENA | MIICFG_ENABLE);
		bus_space_write_4(sc->sc_st, sc->sc_sh, EPIC_MIICFG, miicfg);

		/* There is no driver to fill this */
		mii->mii_media_active = media;
		mii->mii_media_status = 0;

		epic_statchg(&sc->sc_dev);
		return (0);
	}

	/* Lookup selected PHY */
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
		if (IFM_INST(media) == miisc->mii_inst)
			break;
	}
	if (!miisc) {
		printf("epic_mediachange: can't happen\n"); /* ??? panic */
		return (0);
	}
#ifdef EPICMEDIADEBUG
	printf("%s: using phy %s\n", ifp->if_xname,
	       miisc->mii_dev.dv_xname);
#endif

	if (miisc->mii_flags & MIIF_HAVEFIBER) {
		/* XXX XXX assume it's a Level1 - should check */

		/* We have to powerup fiber transceivers */
		cfg = PHY_READ(miisc, MII_LXTPHY_CONFIG);
		if (IFM_SUBTYPE(media) == IFM_100_FX) {
#ifdef EPICMEDIADEBUG
			printf("%s: power up fiber\n", ifp->if_xname);
#endif
			cfg |= (CONFIG_LEDC1 | CONFIG_LEDC0);
		} else {
#ifdef EPICMEDIADEBUG
			printf("%s: power down fiber\n", ifp->if_xname);
#endif
			cfg &= ~(CONFIG_LEDC1 | CONFIG_LEDC0);
		}
		PHY_WRITE(miisc, MII_LXTPHY_CONFIG, cfg);
	}

	return (0);
}
