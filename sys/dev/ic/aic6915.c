/*	$OpenBSD: aic6915.c,v 1.25 2023/11/10 15:51:20 bluhm Exp $	*/
/*	$NetBSD: aic6915.c,v 1.15 2005/12/24 20:27:29 perry Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * Device driver for the Adaptec AIC-6915 (``Starfire'')
 * 10/100 Ethernet controller.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/endian.h>
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
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>

#include <dev/ic/aic6915.h>

void	sf_start(struct ifnet *);
void	sf_watchdog(struct ifnet *);
int	sf_ioctl(struct ifnet *, u_long, caddr_t);
int	sf_init(struct ifnet *);
void	sf_stop(struct ifnet *, int);

void	sf_txintr(struct sf_softc *);
void	sf_rxintr(struct sf_softc *);
void	sf_stats_update(struct sf_softc *);

void	sf_reset(struct sf_softc *);
void	sf_macreset(struct sf_softc *);
void	sf_rxdrain(struct sf_softc *);
int	sf_add_rxbuf(struct sf_softc *, int);
uint8_t	sf_read_eeprom(struct sf_softc *, int);
void	sf_set_filter(struct sf_softc *);

int	sf_mii_read(struct device *, int, int);
void	sf_mii_write(struct device *, int, int, int);
void	sf_mii_statchg(struct device *);

void	sf_tick(void *);

int	sf_mediachange(struct ifnet *);
void	sf_mediastatus(struct ifnet *, struct ifmediareq *);

uint32_t sf_reg_read(struct sf_softc *, bus_addr_t);
void	sf_reg_write(struct sf_softc *, bus_addr_t , uint32_t);

void	sf_set_filter_perfect(struct sf_softc *, int , uint8_t *);
void	sf_set_filter_hash(struct sf_softc *, uint8_t *);

struct cfdriver sf_cd = {
	NULL, "sf", DV_IFNET
};

#define	sf_funcreg_read(sc, reg)					\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh_func, (reg))
#define	sf_funcreg_write(sc, reg, val)					\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh_func, (reg), (val))

uint32_t
sf_reg_read(struct sf_softc *sc, bus_addr_t reg)
{

	if (__predict_false(sc->sc_iomapped)) {
		bus_space_write_4(sc->sc_st, sc->sc_sh, SF_IndirectIoAccess,
		    reg);
		return (bus_space_read_4(sc->sc_st, sc->sc_sh,
		    SF_IndirectIoDataPort));
	}

	return (bus_space_read_4(sc->sc_st, sc->sc_sh, reg));
}

void
sf_reg_write(struct sf_softc *sc, bus_addr_t reg, uint32_t val)
{

	if (__predict_false(sc->sc_iomapped)) {
		bus_space_write_4(sc->sc_st, sc->sc_sh, SF_IndirectIoAccess,
		    reg);
		bus_space_write_4(sc->sc_st, sc->sc_sh, SF_IndirectIoDataPort,
		    val);
		return;
	}

	bus_space_write_4(sc->sc_st, sc->sc_sh, reg, val);
}

#define	sf_genreg_read(sc, reg)						\
	sf_reg_read((sc), (reg) + SF_GENREG_OFFSET)
#define	sf_genreg_write(sc, reg, val)					\
	sf_reg_write((sc), (reg) + SF_GENREG_OFFSET, (val))

/*
 * sf_attach:
 *
 *	Attach a Starfire interface to the system.
 */
void
sf_attach(struct sf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int i, rseg, error;
	bus_dma_segment_t seg;
	u_int8_t enaddr[ETHER_ADDR_LEN];

	timeout_set(&sc->sc_mii_timeout, sf_tick, sc);

	/*
	 * If we're I/O mapped, the functional register handle is
	 * the same as the base handle.  If we're memory mapped,
	 * carve off a chunk of the register space for the functional
	 * registers, to save on arithmetic later.
	 */
	if (sc->sc_iomapped)
		sc->sc_sh_func = sc->sc_sh;
	else {
		if ((error = bus_space_subregion(sc->sc_st, sc->sc_sh,
		    SF_GENREG_OFFSET, SF_FUNCREG_SIZE, &sc->sc_sh_func)) != 0) {
			printf("%s: unable to sub-region functional "
			    "registers, error = %d\n", sc->sc_dev.dv_xname,
			    error);
			return;
		}
	}

	/*
	 * Initialize the transmit threshold for this interface.  The
	 * manual describes the default as 4 * 16 bytes.  We start out
	 * at 10 * 16 bytes, to avoid a bunch of initial underruns on
	 * several platforms.
	 */
	sc->sc_txthresh = 10;

	/*
	 * Allocate the control data structures, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct sf_control_data), PAGE_SIZE, 0, &seg, 1, &rseg,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to allocate control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &seg, rseg,
	    sizeof(struct sf_control_data), (caddr_t *)&sc->sc_control_data,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat,
	    sizeof(struct sf_control_data), 1,
	    sizeof(struct sf_control_data), 0, BUS_DMA_NOWAIT,
	    &sc->sc_cddmamap)) != 0) {
		printf("%s: unable to create control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_cddmamap,
	    sc->sc_control_data, sizeof(struct sf_control_data), NULL,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("%s: unable to load control data DMA map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	/*
	 * Create the transmit buffer DMA maps.
	 */
	for (i = 0; i < SF_NTXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    SF_NTXFRAGS, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &sc->sc_txsoft[i].ds_dmamap)) != 0) {
			printf("%s: unable to create tx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_4;
		}
	}

	/*
	 * Create the receive buffer DMA maps.
	 */
	for (i = 0; i < SF_NRXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &sc->sc_rxsoft[i].ds_dmamap)) != 0) {
			printf("%s: unable to create rx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_5;
		}
	}

	/*
	 * Reset the chip to a known state.
	 */
	sf_reset(sc);

	/*
	 * Read the Ethernet address from the EEPROM.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		enaddr[i] = sf_read_eeprom(sc, (15 + (ETHER_ADDR_LEN - 1)) - i);

	printf(", address %s\n", ether_sprintf(enaddr));

#ifdef DEBUG
	if (sf_funcreg_read(sc, SF_PciDeviceConfig) & PDC_System64)
		printf("%s: 64-bit PCI slot detected\n", sc->sc_dev.dv_xname);
#endif

	/*
	 * Initialize our media structures and probe the MII.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = sf_mii_read;
	sc->sc_mii.mii_writereg = sf_mii_write;
	sc->sc_mii.mii_statchg = sf_mii_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, IFM_IMASK, sf_mediachange,
	    sf_mediastatus);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
	bcopy(enaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sf_ioctl;
	ifp->if_start = sf_start;
	ifp->if_watchdog = sf_watchdog;
	ifq_init_maxlen(&ifp->if_snd, SF_NTXDESC_MASK);

	/*
	 * Attach the interface.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);
	return;

	/*
	 * Free any resources we've allocated during the failed attach
	 * attempt.  Do this in reverse order an fall through.
	 */
 fail_5:
	for (i = 0; i < SF_NRXDESC; i++) {
		if (sc->sc_rxsoft[i].ds_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->sc_rxsoft[i].ds_dmamap);
	}
 fail_4:
	for (i = 0; i < SF_NTXDESC; i++) {
		if (sc->sc_txsoft[i].ds_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->sc_txsoft[i].ds_dmamap);
	}
	bus_dmamap_unload(sc->sc_dmat, sc->sc_cddmamap);
 fail_3:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_cddmamap);
 fail_2:
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t) sc->sc_control_data,
	    sizeof(struct sf_control_data));
 fail_1:
	bus_dmamem_free(sc->sc_dmat, &seg, rseg);
 fail_0:
	return;
}

/*
 * sf_start:		[ifnet interface function]
 *
 *	Start packet transmission on the interface.
 */
void
sf_start(struct ifnet *ifp)
{
	struct sf_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
	struct sf_txdesc0 *txd;
	struct sf_descsoft *ds;
	bus_dmamap_t dmamap;
	int error, producer, last = -1, opending, seg;

	/*
	 * Remember the previous number of pending transmits.
	 */
	opending = sc->sc_txpending;

	/*
	 * Find out where we're sitting.
	 */
	producer = SF_TXDINDEX_TO_HOST(
	    TDQPI_HiPrTxProducerIndex_get(
	    sf_funcreg_read(sc, SF_TxDescQueueProducerIndex)));

	/*
	 * Loop through the send queue, setting up transmit descriptors
	 * until we drain the queue, or use up all available transmit
	 * descriptors.  Leave a blank one at the end for sanity's sake.
	 */
	while (sc->sc_txpending < (SF_NTXDESC - 1)) {
		/*
		 * Grab a packet off the queue.
		 */
		m0 = ifq_deq_begin(&ifp->if_snd);
		if (m0 == NULL)
			break;
		m = NULL;

		/*
		 * Get the transmit descriptor.
		 */
		txd = &sc->sc_txdescs[producer];
		ds = &sc->sc_txsoft[producer];
		dmamap = ds->ds_dmamap;

		/*
		 * Load the DMA map.  If this fails, the packet either
		 * didn't fit in the allotted number of frags, or we were
		 * short on resources.  In this case, we'll copy and try
		 * again.
		 */
		if (bus_dmamap_load_mbuf(sc->sc_dmat, dmamap, m0,
		    BUS_DMA_WRITE|BUS_DMA_NOWAIT) != 0) {
			MGETHDR(m, M_DONTWAIT, MT_DATA);
			if (m == NULL) {
				ifq_deq_rollback(&ifp->if_snd, m0);
				printf("%s: unable to allocate Tx mbuf\n",
				    sc->sc_dev.dv_xname);
				break;
			}
			if (m0->m_pkthdr.len > MHLEN) {
				MCLGET(m, M_DONTWAIT);
				if ((m->m_flags & M_EXT) == 0) {
					ifq_deq_rollback(&ifp->if_snd, m0);
					printf("%s: unable to allocate Tx "
					    "cluster\n", sc->sc_dev.dv_xname);
					m_freem(m);
					break;
				}
			}
			m_copydata(m0, 0, m0->m_pkthdr.len, mtod(m, caddr_t));
			m->m_pkthdr.len = m->m_len = m0->m_pkthdr.len;
			error = bus_dmamap_load_mbuf(sc->sc_dmat, dmamap,
			    m, BUS_DMA_WRITE|BUS_DMA_NOWAIT);
			if (error) {
				ifq_deq_rollback(&ifp->if_snd, m0);
				printf("%s: unable to load Tx buffer, "
				    "error = %d\n", sc->sc_dev.dv_xname, error);
				m_freem(m);
				break;
			}
		}

		/*
		 * WE ARE NOW COMMITTED TO TRANSMITTING THE PACKET.
		 */
		ifq_deq_commit(&ifp->if_snd, m0);
		if (m != NULL) {
			m_freem(m0);
			m0 = m;
		}

		/* Initialize the descriptor. */
		txd->td_word0 =
		    htole32(TD_W0_ID | TD_W0_CRCEN | m0->m_pkthdr.len);
		if (producer == (SF_NTXDESC - 1))
			txd->td_word0 |= TD_W0_END;
		txd->td_word1 = htole32(dmamap->dm_nsegs);
		for (seg = 0; seg < dmamap->dm_nsegs; seg++) {
			txd->td_frags[seg].fr_addr =
			    htole32(dmamap->dm_segs[seg].ds_addr);
			txd->td_frags[seg].fr_len =
			    htole32(dmamap->dm_segs[seg].ds_len);
		}

		/* Sync the descriptor and the DMA map. */
		SF_CDTXDSYNC(sc, producer, BUS_DMASYNC_PREWRITE);
		bus_dmamap_sync(sc->sc_dmat, dmamap, 0, dmamap->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		/*
		 * Store a pointer to the packet so we can free it later.
		 */
		ds->ds_mbuf = m0;

		/* Advance the Tx pointer. */
		sc->sc_txpending++;
		last = producer;
		producer = SF_NEXTTX(producer);

#if NBPFILTER > 0
		/*
		 * Pass the packet to any BPF listeners.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif
	}

	if (sc->sc_txpending == (SF_NTXDESC - 1)) {
		/* No more slots left; notify upper layer. */
		ifq_set_oactive(&ifp->if_snd);
	}

	if (sc->sc_txpending != opending) {
		KASSERT(last != -1);
		/*
		 * We enqueued packets.  Cause a transmit interrupt to
		 * happen on the last packet we enqueued, and give the
		 * new descriptors to the chip by writing the new
		 * producer index.
		 */
		sc->sc_txdescs[last].td_word0 |= TD_W0_INTR;
		SF_CDTXDSYNC(sc, last, BUS_DMASYNC_PREWRITE);

		sf_funcreg_write(sc, SF_TxDescQueueProducerIndex,
		    TDQPI_HiPrTxProducerIndex(SF_TXDINDEX_TO_CHIP(producer)));

		/* Set a watchdog timer in case the chip flakes out. */
		ifp->if_timer = 5;
	}
}

/*
 * sf_watchdog:		[ifnet interface function]
 *
 *	Watchdog timer handler.
 */
void
sf_watchdog(struct ifnet *ifp)
{
	struct sf_softc *sc = ifp->if_softc;

	printf("%s: device timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;

	(void) sf_init(ifp);

	/* Try to get more packets going. */
	sf_start(ifp);
}

/*
 * sf_ioctl:		[ifnet interface function]
 *
 *	Handle control requests from the operator.
 */
int
sf_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct sf_softc *sc = (struct sf_softc *)ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			sf_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ((ifp->if_flags ^ sc->sc_flags) &
			     IFF_PROMISC)) {
				sf_set_filter(sc);
			} else {
				if (!(ifp->if_flags & IFF_RUNNING))
					sf_init(ifp);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				sf_stop(ifp, 1);
		}
		sc->sc_flags = ifp->if_flags;
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			sf_set_filter(sc);
		error = 0;
	}

	/* Try to get more packets going. */
	sf_start(ifp);

	splx(s);
	return (error);
}

/*
 * sf_intr:
 *
 *	Interrupt service routine.
 */
int
sf_intr(void *arg)
{
	struct sf_softc *sc = arg;
	uint32_t isr;
	int handled = 0, wantinit = 0;

	for (;;) {
		/* Reading clears all interrupts we're interested in. */
		isr = sf_funcreg_read(sc, SF_InterruptStatus);
		if ((isr & IS_PCIPadInt) == 0)
			break;

		handled = 1;

		/* Handle receive interrupts. */
		if (isr & IS_RxQ1DoneInt)
			sf_rxintr(sc);

		/* Handle transmit completion interrupts. */
		if (isr & (IS_TxDmaDoneInt|IS_TxQueueDoneInt))
			sf_txintr(sc);

		/* Handle abnormal interrupts. */
		if (isr & IS_AbnormalInterrupt) {
			/* Statistics. */
			if (isr & IS_StatisticWrapInt)
				sf_stats_update(sc);

			/* DMA errors. */
			if (isr & IS_DmaErrInt) {
				wantinit = 1;
				printf("%s: WARNING: DMA error\n",
				    sc->sc_dev.dv_xname);
			}

			/* Transmit FIFO underruns. */
			if (isr & IS_TxDataLowInt) {
				if (sc->sc_txthresh < 0xff)
					sc->sc_txthresh++;
#ifdef DEBUG
				printf("%s: transmit FIFO underrun, new "
				    "threshold: %d bytes\n",
				    sc->sc_dev.dv_xname,
				    sc->sc_txthresh * 16);
#endif
				sf_funcreg_write(sc, SF_TransmitFrameCSR,
				    sc->sc_TransmitFrameCSR |
				    TFCSR_TransmitThreshold(sc->sc_txthresh));
				sf_funcreg_write(sc, SF_TxDescQueueCtrl,
				    sc->sc_TxDescQueueCtrl |
				    TDQC_TxHighPriorityFifoThreshold(
							sc->sc_txthresh));
			}
		}
	}

	if (handled) {
		/* Reset the interface, if necessary. */
		if (wantinit)
			sf_init(&sc->sc_arpcom.ac_if);

		/* Try and get more packets going. */
		sf_start(&sc->sc_arpcom.ac_if);
	}

	return (handled);
}

/*
 * sf_txintr:
 *
 *	Helper -- handle transmit completion interrupts.
 */
void
sf_txintr(struct sf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct sf_descsoft *ds;
	uint32_t cqci, tcd;
	int consumer, producer, txidx;

 try_again:
	cqci = sf_funcreg_read(sc, SF_CompletionQueueConsumerIndex);

	consumer = CQCI_TxCompletionConsumerIndex_get(cqci);
	producer = CQPI_TxCompletionProducerIndex_get(
	    sf_funcreg_read(sc, SF_CompletionQueueProducerIndex));

	if (consumer == producer)
		return;

	ifq_clr_oactive(&ifp->if_snd);

	while (consumer != producer) {
		SF_CDTXCSYNC(sc, consumer, BUS_DMASYNC_POSTREAD);
		tcd = letoh32(sc->sc_txcomp[consumer].tcd_word0);

		txidx = SF_TCD_INDEX_TO_HOST(TCD_INDEX(tcd));
#ifdef DIAGNOSTIC
		if ((tcd & TCD_PR) == 0)
			printf("%s: Tx queue mismatch, index %d\n",
			    sc->sc_dev.dv_xname, txidx);
#endif
		/*
		 * NOTE: stats are updated later.  We're just
		 * releasing packets that have been DMA'd to
		 * the chip.
		 */
		ds = &sc->sc_txsoft[txidx];
		SF_CDTXDSYNC(sc, txidx, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap,
		    0, ds->ds_dmamap->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		m_freem(ds->ds_mbuf);
		ds->ds_mbuf = NULL;

		consumer = SF_NEXTTCD(consumer);
		sc->sc_txpending--;
	}

	/* XXXJRT -- should be KDASSERT() */
	KASSERT(sc->sc_txpending >= 0);

	/* If all packets are done, cancel the watchdog timer. */
	if (sc->sc_txpending == 0)
		ifp->if_timer = 0;

	/* Update the consumer index. */
	sf_funcreg_write(sc, SF_CompletionQueueConsumerIndex,
	    (cqci & ~CQCI_TxCompletionConsumerIndex(0x7ff)) |
	     CQCI_TxCompletionConsumerIndex(consumer));

	/* Double check for new completions. */
	goto try_again;
}

/*
 * sf_rxintr:
 *
 *	Helper -- handle receive interrupts.
 */
void
sf_rxintr(struct sf_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct sf_descsoft *ds;
	struct sf_rcd_full *rcd;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	uint32_t cqci, word0;
	int consumer, producer, bufproducer, rxidx, len;

	cqci = sf_funcreg_read(sc, SF_CompletionQueueConsumerIndex);

	consumer = CQCI_RxCompletionQ1ConsumerIndex_get(cqci);
	producer = CQPI_RxCompletionQ1ProducerIndex_get(
	    sf_funcreg_read(sc, SF_CompletionQueueProducerIndex));
	bufproducer = RXQ1P_RxDescQ1Producer_get(
	    sf_funcreg_read(sc, SF_RxDescQueue1Ptrs));

	if (consumer == producer)
		return;

	while (consumer != producer) {
		rcd = &sc->sc_rxcomp[consumer];
		SF_CDRXCSYNC(sc, consumer,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);
		SF_CDRXCSYNC(sc, consumer,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		word0 = letoh32(rcd->rcd_word0);
		rxidx = RCD_W0_EndIndex(word0);

		ds = &sc->sc_rxsoft[rxidx];

		consumer = SF_NEXTRCD(consumer);
		bufproducer = SF_NEXTRX(bufproducer);

		if ((word0 & RCD_W0_OK) == 0) {
			SF_INIT_RXDESC(sc, rxidx);
			continue;
		}

		bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
		    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		/*
		 * No errors; receive the packet.  Note that we have
		 * configured the Starfire to NOT transfer the CRC
		 * with the packet.
		 */
		len = RCD_W0_Length(word0);

#ifndef __STRICT_ALIGNMENT
		/*
		 * Allocate a new mbuf cluster.  If that fails, we are
		 * out of memory, and must drop the packet and recycle
		 * the buffer that's already attached to this descriptor.
		 */
		m = ds->ds_mbuf;
		if (sf_add_rxbuf(sc, rxidx) != 0) {
			ifp->if_ierrors++;
			SF_INIT_RXDESC(sc, rxidx);
			bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
			    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);
			continue;
		}
#else
		/*
		 * The Starfire's receive buffer must be 4-byte aligned.
		 * But this means that the data after the Ethernet header
		 * is misaligned.  We must allocate a new buffer and
		 * copy the data, shifted forward 2 bytes.
		 */
		MGETHDR(m, M_DONTWAIT, MT_DATA);
		if (m == NULL) {
 dropit:
			ifp->if_ierrors++;
			SF_INIT_RXDESC(sc, rxidx);
			bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
			    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);
			continue;
		}
		if (len > (MHLEN - 2)) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				goto dropit;
			}
		}
		m->m_data += 2;

		/*
		 * Note that we use cluster for incoming frames, so the
		 * buffer is virtually contiguous.
		 */
		memcpy(mtod(m, caddr_t), mtod(ds->ds_mbuf, caddr_t), len);

		/* Allow the receive descriptor to continue using its mbuf. */
		SF_INIT_RXDESC(sc, rxidx);
		bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
		    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);
#endif /* __STRICT_ALIGNMENT */

		m->m_pkthdr.len = m->m_len = len;

		ml_enqueue(&ml, m);
	}

	if_input(ifp, &ml);

	/* Update the chip's pointers. */
	sf_funcreg_write(sc, SF_CompletionQueueConsumerIndex,
	    (cqci & ~CQCI_RxCompletionQ1ConsumerIndex(0x7ff)) |
	     CQCI_RxCompletionQ1ConsumerIndex(consumer));
	sf_funcreg_write(sc, SF_RxDescQueue1Ptrs,
	    RXQ1P_RxDescQ1Producer(bufproducer));
}

/*
 * sf_tick:
 *
 *	One second timer, used to tick the MII and update stats.
 */
void
sf_tick(void *arg)
{
	struct sf_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	sf_stats_update(sc);
	splx(s);

	timeout_add_sec(&sc->sc_mii_timeout, 1);
}

/*
 * sf_stats_update:
 *
 *	Read the statistics counters.
 */
void
sf_stats_update(struct sf_softc *sc)
{
	struct sf_stats stats;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t *p;
	u_int i;

	p = &stats.TransmitOKFrames;
	for (i = 0; i < (sizeof(stats) / sizeof(uint32_t)); i++) {
		*p++ = sf_genreg_read(sc,
		    SF_STATS_BASE + (i * sizeof(uint32_t)));
		sf_genreg_write(sc, SF_STATS_BASE + (i * sizeof(uint32_t)), 0);
	}

	ifp->if_collisions += stats.SingleCollisionFrames +
	    stats.MultipleCollisionFrames;

	ifp->if_oerrors += stats.TransmitAbortDueToExcessiveCollisions +
	    stats.TransmitAbortDueToExcessingDeferral +
	    stats.FramesLostDueToInternalTransmitErrors;

	ifp->if_ierrors += stats.ReceiveCRCErrors + stats.AlignmentErrors +
	    stats.ReceiveFramesTooLong + stats.ReceiveFramesTooShort +
	    stats.ReceiveFramesJabbersError +
	    stats.FramesLostDueToInternalReceiveErrors;
}

/*
 * sf_reset:
 *
 *	Perform a soft reset on the Starfire.
 */
void
sf_reset(struct sf_softc *sc)
{
	int i;

	sf_funcreg_write(sc, SF_GeneralEthernetCtrl, 0);

	sf_macreset(sc);

	sf_funcreg_write(sc, SF_PciDeviceConfig, PDC_SoftReset);
	for (i = 0; i < 1000; i++) {
		delay(10);
		if ((sf_funcreg_read(sc, SF_PciDeviceConfig) &
		     PDC_SoftReset) == 0)
			break;
	}

	if (i == 1000) {
		printf("%s: reset failed to complete\n", sc->sc_dev.dv_xname);
		sf_funcreg_write(sc, SF_PciDeviceConfig, 0);
	}

	delay(1000);
}

/*
 * sf_macreset:
 *
 *	Reset the MAC portion of the Starfire.
 */
void
sf_macreset(struct sf_softc *sc)
{

	sf_genreg_write(sc, SF_MacConfig1, sc->sc_MacConfig1 | MC1_SoftRst);
	delay(1000);
	sf_genreg_write(sc, SF_MacConfig1, sc->sc_MacConfig1);
}

/*
 * sf_init:		[ifnet interface function]
 *
 *	Initialize the interface.  Must be called at splnet().
 */
int
sf_init(struct ifnet *ifp)
{
	struct sf_softc *sc = ifp->if_softc;
	struct sf_descsoft *ds;
	int error = 0;
	u_int i;

	/*
	 * Cancel any pending I/O.
	 */
	sf_stop(ifp, 0);

	/*
	 * Reset the Starfire to a known state.
	 */
	sf_reset(sc);

	/* Clear the stat counters. */
	for (i = 0; i < sizeof(struct sf_stats); i += sizeof(uint32_t))
		sf_genreg_write(sc, SF_STATS_BASE + i, 0);

	/*
	 * Initialize the transmit descriptor ring.
	 */
	memset(sc->sc_txdescs, 0, sizeof(sc->sc_txdescs));
	sf_funcreg_write(sc, SF_TxDescQueueHighAddr, 0);
	sf_funcreg_write(sc, SF_HiPrTxDescQueueBaseAddr, SF_CDTXDADDR(sc, 0));
	sf_funcreg_write(sc, SF_LoPrTxDescQueueBaseAddr, 0);

	/*
	 * Initialize the transmit completion ring.
	 */
	for (i = 0; i < SF_NTCD; i++) {
		sc->sc_txcomp[i].tcd_word0 = TCD_DMA_ID;
		SF_CDTXCSYNC(sc, i, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	}
	sf_funcreg_write(sc, SF_CompletionQueueHighAddr, 0);
	sf_funcreg_write(sc, SF_TxCompletionQueueCtrl, SF_CDTXCADDR(sc, 0));

	/*
	 * Initialize the receive descriptor ring.
	 */
	for (i = 0; i < SF_NRXDESC; i++) {
		ds = &sc->sc_rxsoft[i];
		if (ds->ds_mbuf == NULL) {
			if ((error = sf_add_rxbuf(sc, i)) != 0) {
				printf("%s: unable to allocate or map rx "
				    "buffer %d, error = %d\n",
				    sc->sc_dev.dv_xname, i, error);
				/*
				 * XXX Should attempt to run with fewer receive
				 * XXX buffers instead of just failing.
				 */
				sf_rxdrain(sc);
				goto out;
			}
		} else
			SF_INIT_RXDESC(sc, i);
	}
	sf_funcreg_write(sc, SF_RxDescQueueHighAddress, 0);
	sf_funcreg_write(sc, SF_RxDescQueue1LowAddress, SF_CDRXDADDR(sc, 0));
	sf_funcreg_write(sc, SF_RxDescQueue2LowAddress, 0);

	/*
	 * Initialize the receive completion ring.
	 */
	for (i = 0; i < SF_NRCD; i++) {
		sc->sc_rxcomp[i].rcd_word0 = RCD_W0_ID;
		sc->sc_rxcomp[i].rcd_word1 = 0;
		sc->sc_rxcomp[i].rcd_word2 = 0;
		sc->sc_rxcomp[i].rcd_timestamp = 0;
		SF_CDRXCSYNC(sc, i, BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	}
	sf_funcreg_write(sc, SF_RxCompletionQueue1Ctrl, SF_CDRXCADDR(sc, 0) |
	    RCQ1C_RxCompletionQ1Type(3));
	sf_funcreg_write(sc, SF_RxCompletionQueue2Ctrl, 0);

	/*
	 * Initialize the Tx CSR.
	 */
	sc->sc_TransmitFrameCSR = 0;
	sf_funcreg_write(sc, SF_TransmitFrameCSR,
	    sc->sc_TransmitFrameCSR |
	    TFCSR_TransmitThreshold(sc->sc_txthresh));

	/*
	 * Initialize the Tx descriptor control register.
	 */
	sc->sc_TxDescQueueCtrl = TDQC_SkipLength(0) |
	    TDQC_TxDmaBurstSize(4) |	/* default */
	    TDQC_MinFrameSpacing(3) |	/* 128 bytes */
	    TDQC_TxDescType(0);
	sf_funcreg_write(sc, SF_TxDescQueueCtrl,
	    sc->sc_TxDescQueueCtrl |
	    TDQC_TxHighPriorityFifoThreshold(sc->sc_txthresh));

	/*
	 * Initialize the Rx descriptor control registers.
	 */
	sf_funcreg_write(sc, SF_RxDescQueue1Ctrl,
	    RDQ1C_RxQ1BufferLength(MCLBYTES) |
	    RDQ1C_RxDescSpacing(0));
	sf_funcreg_write(sc, SF_RxDescQueue2Ctrl, 0);

	/*
	 * Initialize the Tx descriptor producer indices.
	 */
	sf_funcreg_write(sc, SF_TxDescQueueProducerIndex,
	    TDQPI_HiPrTxProducerIndex(0) |
	    TDQPI_LoPrTxProducerIndex(0));

	/*
	 * Initialize the Rx descriptor producer indices.
	 */
	sf_funcreg_write(sc, SF_RxDescQueue1Ptrs,
	    RXQ1P_RxDescQ1Producer(SF_NRXDESC - 1));
	sf_funcreg_write(sc, SF_RxDescQueue2Ptrs,
	    RXQ2P_RxDescQ2Producer(0));

	/*
	 * Initialize the Tx and Rx completion queue consumer indices.
	 */
	sf_funcreg_write(sc, SF_CompletionQueueConsumerIndex,
	    CQCI_TxCompletionConsumerIndex(0) |
	    CQCI_RxCompletionQ1ConsumerIndex(0));
	sf_funcreg_write(sc, SF_RxHiPrCompletionPtrs, 0);

	/*
	 * Initialize the Rx DMA control register.
	 */
	sf_funcreg_write(sc, SF_RxDmaCtrl,
	    RDC_RxHighPriorityThreshold(6) |	/* default */
	    RDC_RxBurstSize(4));		/* default */

	/*
	 * Set the receive filter.
	 */
	sc->sc_RxAddressFilteringCtl = 0;
	sf_set_filter(sc);

	/*
	 * Set MacConfig1.  When we set the media, MacConfig1 will
	 * actually be written and the MAC part reset.
	 */
	sc->sc_MacConfig1 = MC1_PadEn;

	/*
	 * Set the media.
	 */
	mii_mediachg(&sc->sc_mii);

	/*
	 * Initialize the interrupt register.
	 */
	sc->sc_InterruptEn = IS_PCIPadInt | IS_RxQ1DoneInt |
	    IS_TxQueueDoneInt | IS_TxDmaDoneInt | IS_DmaErrInt |
	    IS_StatisticWrapInt;
	sf_funcreg_write(sc, SF_InterruptEn, sc->sc_InterruptEn);

	sf_funcreg_write(sc, SF_PciDeviceConfig, PDC_IntEnable |
	    PDC_PCIMstDmaEn | (1 << PDC_FifoThreshold_SHIFT));

	/*
	 * Start the transmit and receive processes.
	 */
	sf_funcreg_write(sc, SF_GeneralEthernetCtrl,
	    GEC_TxDmaEn|GEC_RxDmaEn|GEC_TransmitEn|GEC_ReceiveEn);

	/* Start the on second clock. */
	timeout_add_sec(&sc->sc_mii_timeout, 1);

	/*
	 * Note that the interface is now running.
	 */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

 out:
	if (error) {
		ifp->if_flags &= ~IFF_RUNNING;
		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
		printf("%s: interface not running\n", sc->sc_dev.dv_xname);
	}
	return (error);
}

/*
 * sf_rxdrain:
 *
 *	Drain the receive queue.
 */
void
sf_rxdrain(struct sf_softc *sc)
{
	struct sf_descsoft *ds;
	int i;

	for (i = 0; i < SF_NRXDESC; i++) {
		ds = &sc->sc_rxsoft[i];
		if (ds->ds_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
			m_freem(ds->ds_mbuf);
			ds->ds_mbuf = NULL;
		}
	}
}

/*
 * sf_stop:		[ifnet interface function]
 *
 *	Stop transmission on the interface.
 */
void
sf_stop(struct ifnet *ifp, int disable)
{
	struct sf_softc *sc = ifp->if_softc;
	struct sf_descsoft *ds;
	int i;

	/* Stop the one second clock. */
	timeout_del(&sc->sc_mii_timeout);

	/* Down the MII. */
	mii_down(&sc->sc_mii);

	/* Disable interrupts. */
	sf_funcreg_write(sc, SF_InterruptEn, 0);

	/* Stop the transmit and receive processes. */
	sf_funcreg_write(sc, SF_GeneralEthernetCtrl, 0);

	/*
	 * Release any queued transmit buffers.
	 */
	for (i = 0; i < SF_NTXDESC; i++) {
		ds = &sc->sc_txsoft[i];
		if (ds->ds_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
			m_freem(ds->ds_mbuf);
			ds->ds_mbuf = NULL;
		}
	}
	sc->sc_txpending = 0;

	if (disable)
		sf_rxdrain(sc);

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
}

/*
 * sf_read_eeprom:
 *
 *	Read from the Starfire EEPROM.
 */
uint8_t
sf_read_eeprom(struct sf_softc *sc, int offset)
{
	uint32_t reg;

	reg = sf_genreg_read(sc, SF_EEPROM_BASE + (offset & ~3));

	return ((reg >> (8 * (offset & 3))) & 0xff);
}

/*
 * sf_add_rxbuf:
 *
 *	Add a receive buffer to the indicated descriptor.
 */
int
sf_add_rxbuf(struct sf_softc *sc, int idx)
{
	struct sf_descsoft *ds = &sc->sc_rxsoft[idx];
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
		panic("sf_add_rxbuf"); /* XXX */
	}

	bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
	    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	SF_INIT_RXDESC(sc, idx);

	return (0);
}

void
sf_set_filter_perfect(struct sf_softc *sc, int slot, uint8_t *enaddr)
{
	uint32_t reg0, reg1, reg2;

	reg0 = enaddr[5] | (enaddr[4] << 8);
	reg1 = enaddr[3] | (enaddr[2] << 8);
	reg2 = enaddr[1] | (enaddr[0] << 8);

	sf_genreg_write(sc, SF_PERFECT_BASE + (slot * 0x10) + 0, reg0);
	sf_genreg_write(sc, SF_PERFECT_BASE + (slot * 0x10) + 4, reg1);
	sf_genreg_write(sc, SF_PERFECT_BASE + (slot * 0x10) + 8, reg2);
}

void
sf_set_filter_hash(struct sf_softc *sc, uint8_t *enaddr)
{
	uint32_t hash, slot, reg;

	hash = ether_crc32_be(enaddr, ETHER_ADDR_LEN) >> 23;
	slot = hash >> 4;

	reg = sf_genreg_read(sc, SF_HASH_BASE + (slot * 0x10));
	reg |= 1 << (hash & 0xf);
	sf_genreg_write(sc, SF_HASH_BASE + (slot * 0x10), reg);
}

/*
 * sf_set_filter:
 *
 *	Set the Starfire receive filter.
 */
void
sf_set_filter(struct sf_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	int i;

	/* Start by clearing the perfect and hash tables. */
	for (i = 0; i < SF_PERFECT_SIZE; i += sizeof(uint32_t))
		sf_genreg_write(sc, SF_PERFECT_BASE + i, 0);

	for (i = 0; i < SF_HASH_SIZE; i += sizeof(uint32_t))
		sf_genreg_write(sc, SF_HASH_BASE + i, 0);

	/*
	 * Clear the perfect and hash mode bits.
	 */
	sc->sc_RxAddressFilteringCtl &=
	    ~(RAFC_PerfectFilteringMode(3) | RAFC_HashFilteringMode(3));

	if (ifp->if_flags & IFF_BROADCAST)
		sc->sc_RxAddressFilteringCtl |= RAFC_PassBroadcast;
	else
		sc->sc_RxAddressFilteringCtl &= ~RAFC_PassBroadcast;

	if (ifp->if_flags & IFF_PROMISC) {
		sc->sc_RxAddressFilteringCtl |= RAFC_PromiscuousMode;
		goto allmulti;
	} else
		sc->sc_RxAddressFilteringCtl &= ~RAFC_PromiscuousMode;

	/*
	 * Set normal perfect filtering mode.
	 */
	sc->sc_RxAddressFilteringCtl |= RAFC_PerfectFilteringMode(1);

	/*
	 * First, write the station address to the perfect filter
	 * table.
	 */
	sf_set_filter_perfect(sc, 0, LLADDR(ifp->if_sadl));

	if (ac->ac_multirangecnt > 0)
		goto allmulti;

	/*
	 * Now set the hash bits for each multicast address in our
	 * list.
	 */
	ETHER_FIRST_MULTI(step, ac, enm);
	if (enm == NULL)
		goto done;
	while (enm != NULL) {
		sf_set_filter_hash(sc, enm->enm_addrlo);
		ETHER_NEXT_MULTI(step, enm);
	}

	/*
	 * Set "hash only multicast dest, match regardless of VLAN ID".
	 */
	sc->sc_RxAddressFilteringCtl |= RAFC_HashFilteringMode(2);
	goto done;

 allmulti:
	/*
	 * XXX RAFC_PassMulticast is sub-optimal if using VLAN mode.
	 */
	sc->sc_RxAddressFilteringCtl |= RAFC_PassMulticast;
	ifp->if_flags |= IFF_ALLMULTI;

 done:
	sf_funcreg_write(sc, SF_RxAddressFilteringCtl,
	    sc->sc_RxAddressFilteringCtl);
}

/*
 * sf_mii_read:		[mii interface function]
 *
 *	Read from the MII.
 */
int
sf_mii_read(struct device *self, int phy, int reg)
{
	struct sf_softc *sc = (void *) self;
	uint32_t v;
	int i;

	for (i = 0; i < 1000; i++) {
		v = sf_genreg_read(sc, SF_MII_PHY_REG(phy, reg));
		if (v & MiiDataValid)
			break;
		delay(1);
	}

	if ((v & MiiDataValid) == 0)
		return (0);

	if (MiiRegDataPort(v) == 0xffff)
		return (0);

	return (MiiRegDataPort(v));
}

/*
 * sf_mii_write:	[mii interface function]
 *
 *	Write to the MII.
 */
void
sf_mii_write(struct device *self, int phy, int reg, int val)
{
	struct sf_softc *sc = (void *) self;
	int i;

	sf_genreg_write(sc, SF_MII_PHY_REG(phy, reg), val);

	for (i = 0; i < 1000; i++) {
		if ((sf_genreg_read(sc, SF_MII_PHY_REG(phy, reg)) &
		     MiiBusy) == 0)
			return;
		delay(1);
	}

	printf("%s: MII write timed out\n", sc->sc_dev.dv_xname);
}

/*
 * sf_mii_statchg:	[mii interface function]
 *
 *	Callback from the PHY when the media changes.
 */
void
sf_mii_statchg(struct device *self)
{
	struct sf_softc *sc = (void *) self;
	uint32_t ipg;

	if (sc->sc_mii.mii_media_active & IFM_FDX) {
		sc->sc_MacConfig1 |= MC1_FullDuplex;
		ipg = 0x15;
	} else {
		sc->sc_MacConfig1 &= ~MC1_FullDuplex;
		ipg = 0x11;
	}

	sf_genreg_write(sc, SF_MacConfig1, sc->sc_MacConfig1);
	sf_macreset(sc);

	sf_genreg_write(sc, SF_BkToBkIPG, ipg);
}

/*
 * sf_mediastatus:	[ifmedia interface function]
 *
 *	Callback from ifmedia to request current media status.
 */
void
sf_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sf_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

/*
 * sf_mediachange:	[ifmedia interface function]
 *
 *	Callback from ifmedia to request new media setting.
 */
int
sf_mediachange(struct ifnet *ifp)
{
	struct sf_softc *sc = ifp->if_softc;

	if (ifp->if_flags & IFF_UP)
		mii_mediachg(&sc->sc_mii);
	return (0);
}
