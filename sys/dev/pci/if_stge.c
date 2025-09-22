/*	$OpenBSD: if_stge.c,v 1.74 2024/05/24 06:02:57 jsg Exp $	*/
/*	$NetBSD: if_stge.c,v 1.27 2005/05/16 21:35:32 bouyer Exp $	*/

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
 * Device driver for the Sundance Tech. TC9021 10/100/1000
 * Ethernet controller.
 */

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/mbuf.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/queue.h>

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
#include <dev/mii/mii_bitbang.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_stgereg.h>

void	stge_start(struct ifnet *);
void	stge_watchdog(struct ifnet *);
int	stge_ioctl(struct ifnet *, u_long, caddr_t);
int	stge_init(struct ifnet *);
void	stge_stop(struct ifnet *, int);

void	stge_reset(struct stge_softc *);
void	stge_rxdrain(struct stge_softc *);
int	stge_add_rxbuf(struct stge_softc *, int);
void	stge_read_eeprom(struct stge_softc *, int, uint16_t *);
void	stge_tick(void *);

void	stge_stats_update(struct stge_softc *);

void	stge_iff(struct stge_softc *);

int	stge_intr(void *);
void	stge_txintr(struct stge_softc *);
void	stge_rxintr(struct stge_softc *);

int	stge_mii_readreg(struct device *, int, int);
void	stge_mii_writereg(struct device *, int, int, int);
void	stge_mii_statchg(struct device *);

int	stge_mediachange(struct ifnet *);
void	stge_mediastatus(struct ifnet *, struct ifmediareq *);

int	stge_match(struct device *, void *, void *);
void	stge_attach(struct device *, struct device *, void *);

int	stge_copy_small = 0;

const struct cfattach stge_ca = {
	sizeof(struct stge_softc), stge_match, stge_attach,
};

struct cfdriver stge_cd = {
	NULL, "stge", DV_IFNET
};

uint32_t stge_mii_bitbang_read(struct device *);
void	stge_mii_bitbang_write(struct device *, uint32_t);

const struct mii_bitbang_ops stge_mii_bitbang_ops = {
	stge_mii_bitbang_read,
	stge_mii_bitbang_write,
	{
		PC_MgmtData,		/* MII_BIT_MDO */
		PC_MgmtData,		/* MII_BIT_MDI */
		PC_MgmtClk,		/* MII_BIT_MDC */
		PC_MgmtDir,		/* MII_BIT_DIR_HOST_PHY */
		0,			/* MII_BIT_DIR_PHY_HOST */
	}
};

/*
 * Devices supported by this driver.
 */
const struct pci_matchid stge_devices[] = {
	{ PCI_VENDOR_ANTARES, PCI_PRODUCT_ANTARES_TC9021 },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DGE550T },
	{ PCI_VENDOR_SUNDANCE, PCI_PRODUCT_SUNDANCE_ST1023 },
	{ PCI_VENDOR_SUNDANCE, PCI_PRODUCT_SUNDANCE_ST2021 },
	{ PCI_VENDOR_SUNDANCE, PCI_PRODUCT_SUNDANCE_TC9021 },
	{ PCI_VENDOR_SUNDANCE, PCI_PRODUCT_SUNDANCE_TC9021_ALT },
	{ PCI_VENDOR_TAMARACK, PCI_PRODUCT_TAMARACK_TC9021 },
	{ PCI_VENDOR_TAMARACK, PCI_PRODUCT_TAMARACK_TC9021_ALT }
};

int
stge_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, stge_devices,
	    sizeof(stge_devices) / sizeof(stge_devices[0])));
}

void
stge_attach(struct device *parent, struct device *self, void *aux)
{
	struct stge_softc *sc = (struct stge_softc *) self;
	struct pci_attach_args *pa = aux;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	bus_space_tag_t iot, memt;
	bus_space_handle_t ioh, memh;
	bus_dma_segment_t seg;
	bus_size_t iosize;
	int ioh_valid, memh_valid;
	int i, rseg, error;

	timeout_set(&sc->sc_timeout, stge_tick, sc);

	sc->sc_rev = PCI_REVISION(pa->pa_class);

	/*
	 * Map the device.
	 */
	ioh_valid = (pci_mapreg_map(pa, STGE_PCI_IOBA,
	    PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, NULL, &iosize, 0) == 0);
	memh_valid = (pci_mapreg_map(pa, STGE_PCI_MMBA,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &memt, &memh, NULL, &iosize, 0) == 0);

	if (memh_valid) {
		sc->sc_st = memt;
		sc->sc_sh = memh;
	} else if (ioh_valid) {
		sc->sc_st = iot;
		sc->sc_sh = ioh;
	} else {
		printf(": unable to map device registers\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	/* Get it out of power save mode if needed. */
	pci_set_powerstate(pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map and establish our interrupt.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		goto fail_0;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, stge_intr, sc,
				       sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_0;
	}
	printf(": %s", intrstr);

	/*
	 * Allocate the control data structures, and create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_dmat,
	    sizeof(struct stge_control_data), PAGE_SIZE, 0, &seg, 1, &rseg,
	    0)) != 0) {
		printf("%s: unable to allocate control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_0;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, &seg, rseg,
	    sizeof(struct stge_control_data), (caddr_t *)&sc->sc_control_data,
	    BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map control data, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_1;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat,
	    sizeof(struct stge_control_data), 1,
	    sizeof(struct stge_control_data), 0, 0, &sc->sc_cddmamap)) != 0) {
		printf("%s: unable to create control data DMA map, "
		    "error = %d\n", sc->sc_dev.dv_xname, error);
		goto fail_2;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_cddmamap,
	    sc->sc_control_data, sizeof(struct stge_control_data), NULL,
	    0)) != 0) {
		printf("%s: unable to load control data DMA map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		goto fail_3;
	}

	/*
	 * Create the transmit buffer DMA maps.  Note that rev B.3
	 * and earlier seem to have a bug regarding multi-fragment
	 * packets.  We need to limit the number of Tx segments on
	 * such chips to 1.
	 */
	for (i = 0; i < STGE_NTXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat,
		    STGE_JUMBO_FRAMELEN, STGE_NTXFRAGS, MCLBYTES, 0, 0,
		    &sc->sc_txsoft[i].ds_dmamap)) != 0) {
			printf("%s: unable to create tx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_4;
		}
	}

	/*
	 * Create the receive buffer DMA maps.
	 */
	for (i = 0; i < STGE_NRXDESC; i++) {
		if ((error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, 0, &sc->sc_rxsoft[i].ds_dmamap)) != 0) {
			printf("%s: unable to create rx DMA map %d, "
			    "error = %d\n", sc->sc_dev.dv_xname, i, error);
			goto fail_5;
		}
		sc->sc_rxsoft[i].ds_mbuf = NULL;
	}

	/*
	 * Determine if we're copper or fiber.  It affects how we
	 * reset the card.
	 */
	if (CSR_READ_4(sc, STGE_AsicCtrl) & AC_PhyMedia)
		sc->sc_usefiber = 1;
	else
		sc->sc_usefiber = 0;

	/*
	 * Reset the chip to a known state.
	 */
	stge_reset(sc);

	/*
	 * Reading the station address from the EEPROM doesn't seem
	 * to work, at least on my sample boards.  Instead, since
	 * the reset sequence does AutoInit, read it from the station
	 * address registers. For Sundance 1023 you can only read it
	 * from EEPROM.
	 */
	if (PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT_SUNDANCE_ST1023) {
		sc->sc_arpcom.ac_enaddr[0] = CSR_READ_2(sc,
		    STGE_StationAddress0) & 0xff;
		sc->sc_arpcom.ac_enaddr[1] = CSR_READ_2(sc,
		    STGE_StationAddress0) >> 8;
		sc->sc_arpcom.ac_enaddr[2] = CSR_READ_2(sc,
		    STGE_StationAddress1) & 0xff;
		sc->sc_arpcom.ac_enaddr[3] = CSR_READ_2(sc,
		    STGE_StationAddress1) >> 8;
		sc->sc_arpcom.ac_enaddr[4] = CSR_READ_2(sc,
		    STGE_StationAddress2) & 0xff;
		sc->sc_arpcom.ac_enaddr[5] = CSR_READ_2(sc,
		    STGE_StationAddress2) >> 8;
		sc->sc_stge1023 = 0;
	} else {
		uint16_t myaddr[ETHER_ADDR_LEN / 2];
		for (i = 0; i < ETHER_ADDR_LEN / 2; i++) {
			stge_read_eeprom(sc, STGE_EEPROM_StationAddress0 + i, 
			    &myaddr[i]);
			myaddr[i] = letoh16(myaddr[i]);
		}
		(void)memcpy(sc->sc_arpcom.ac_enaddr, myaddr,
		    sizeof(sc->sc_arpcom.ac_enaddr));
		sc->sc_stge1023 = 1;
	}

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/*
	 * Read some important bits from the PhyCtrl register.
	 */
	sc->sc_PhyCtrl = CSR_READ_1(sc, STGE_PhyCtrl) &
	    (PC_PhyDuplexPolarity | PC_PhyLnkPolarity);

	/*
	 * Initialize our media structures and probe the MII.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = stge_mii_readreg;
	sc->sc_mii.mii_writereg = stge_mii_writereg;
	sc->sc_mii.mii_statchg = stge_mii_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, stge_mediachange,
	    stge_mediastatus);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, MIIF_DOPAUSE);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	ifp = &sc->sc_arpcom.ac_if;
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, sizeof ifp->if_xname);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = stge_ioctl;
	ifp->if_start = stge_start;
	ifp->if_watchdog = stge_watchdog;
#ifdef STGE_JUMBO
	ifp->if_hardmtu = STGE_JUMBO_MTU;
#endif
	ifq_init_maxlen(&ifp->if_snd, STGE_NTXDESC - 1);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

#if NVLAN > 0
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
#endif

	/*
	 * The manual recommends disabling early transmit, so we
	 * do.  It's disabled anyway, if using IP checksumming,
	 * since the entire packet must be in the FIFO in order
	 * for the chip to perform the checksum.
	 */
	sc->sc_txthresh = 0x0fff;

	/*
	 * Disable MWI if the PCI layer tells us to.
	 */
	sc->sc_DMACtrl = 0;
#ifdef fake
	if ((pa->pa_flags & PCI_FLAGS_MWI_OKAY) == 0)
		sc->sc_DMACtrl |= DMAC_MWIDisable;
#endif

#ifdef STGE_CHECKSUM
	/*
	 * We can do IPv4/TCPv4/UDPv4 checksums in hardware.
	 */
	sc->sc_arpcom.ac_if.if_capabilities |= IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4;
#endif

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
 fail_5:
	for (i = 0; i < STGE_NRXDESC; i++) {
		if (sc->sc_rxsoft[i].ds_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->sc_rxsoft[i].ds_dmamap);
	}
 fail_4:
	for (i = 0; i < STGE_NTXDESC; i++) {
		if (sc->sc_txsoft[i].ds_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_dmat,
			    sc->sc_txsoft[i].ds_dmamap);
	}
	bus_dmamap_unload(sc->sc_dmat, sc->sc_cddmamap);
 fail_3:
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_cddmamap);
 fail_2:
	bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_control_data,
	    sizeof(struct stge_control_data));
 fail_1:
	bus_dmamem_free(sc->sc_dmat, &seg, rseg);
 fail_0:
	bus_space_unmap(sc->sc_st, sc->sc_sh, iosize);
	return;
}

static void
stge_dma_wait(struct stge_softc *sc)
{
	int i;

	for (i = 0; i < STGE_TIMEOUT; i++) {
		delay(2);
		if ((CSR_READ_4(sc, STGE_DMACtrl) & DMAC_TxDMAInProg) == 0)
			break;
	}

	if (i == STGE_TIMEOUT)
		printf("%s: DMA wait timed out\n", sc->sc_dev.dv_xname);
}

/*
 * stge_start:		[ifnet interface function]
 *
 *	Start packet transmission on the interface.
 */
void
stge_start(struct ifnet *ifp)
{
	struct stge_softc *sc = ifp->if_softc;
	struct mbuf *m0;
	struct stge_descsoft *ds;
	struct stge_tfd *tfd;
	bus_dmamap_t dmamap;
	int error, firsttx, nexttx, opending, seg, totlen;
	uint64_t csum_flags = 0, tfc;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	/*
	 * Remember the previous number of pending transmissions
	 * and the first descriptor we will use.
	 */
	opending = sc->sc_txpending;
	firsttx = STGE_NEXTTX(sc->sc_txlast);

	/*
	 * Loop through the send queue, setting up transmit descriptors
	 * until we drain the queue, or use up all available transmit
	 * descriptors.
	 */
	for (;;) {
		/*
		 * Grab a packet off the queue.
		 */
		m0 = ifq_deq_begin(&ifp->if_snd);
		if (m0 == NULL)
			break;

		/*
		 * Leave one unused descriptor at the end of the
		 * list to prevent wrapping completely around.
		 */
		if (sc->sc_txpending == (STGE_NTXDESC - 1)) {
			ifq_deq_rollback(&ifp->if_snd, m0);
			break;
		}

		/*
		 * Get the last and next available transmit descriptor.
		 */
		nexttx = STGE_NEXTTX(sc->sc_txlast);
		tfd = &sc->sc_txdescs[nexttx];
		ds = &sc->sc_txsoft[nexttx];

		dmamap = ds->ds_dmamap;

		/*
		 * Load the DMA map.  If this fails, the packet either
		 * didn't fit in the allotted number of segments, or we
		 * were short on resources.  For the too-many-segments
		 * case, we simply report an error and drop the packet,
		 * since we can't sanely copy a jumbo packet to a single
		 * buffer.
		 */
		error = bus_dmamap_load_mbuf(sc->sc_dmat, dmamap, m0,
		    BUS_DMA_NOWAIT);
		if (error) {
			if (error == EFBIG) {
				printf("%s: Tx packet consumes too many "
				    "DMA segments (%u), dropping...\n",
				    sc->sc_dev.dv_xname, dmamap->dm_nsegs);
				ifq_deq_commit(&ifp->if_snd, m0);
				m_freem(m0);
				continue;
			}
			/*
			 * Short on resources, just stop for now.
			 */
			ifq_deq_rollback(&ifp->if_snd, m0);
			break;
		}

		ifq_deq_commit(&ifp->if_snd, m0);

		/*
		 * WE ARE NOW COMMITTED TO TRANSMITTING THE PACKET.
		 */

		/* Sync the DMA map. */
		bus_dmamap_sync(sc->sc_dmat, dmamap, 0, dmamap->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		/* Initialize the fragment list. */
		for (totlen = 0, seg = 0; seg < dmamap->dm_nsegs; seg++) {
			tfd->tfd_frags[seg].frag_word0 =
			    htole64(FRAG_ADDR(dmamap->dm_segs[seg].ds_addr) |
			    FRAG_LEN(dmamap->dm_segs[seg].ds_len));
			totlen += dmamap->dm_segs[seg].ds_len;
		}

#ifdef STGE_CHECKSUM
		/*
		 * Initialize checksumming flags in the descriptor.
		 * Byte-swap constants so the compiler can optimize.
		 */
		if (m0->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
			csum_flags |= TFD_IPChecksumEnable;

		if (m0->m_pkthdr.csum_flags & M_TCP_CSUM_OUT)
			csum_flags |= TFD_TCPChecksumEnable;
		else if (m0->m_pkthdr.csum_flags & M_UDP_CSUM_OUT)
			csum_flags |= TFD_UDPChecksumEnable;
#endif

		/*
		 * Initialize the descriptor and give it to the chip.
		 */
		tfc = TFD_FrameId(nexttx) | TFD_WordAlign(/*totlen & */3) |
		    TFD_FragCount(seg) | csum_flags;
		if ((nexttx & STGE_TXINTR_SPACING_MASK) == 0)
			tfc |= TFD_TxDMAIndicate;

#if NVLAN > 0
		/* Check if we have a VLAN tag to insert. */
		if (m0->m_flags & M_VLANTAG)
			tfc |= (TFD_VLANTagInsert |
			    TFD_VID(m0->m_pkthdr.ether_vtag));
#endif

		tfd->tfd_control = htole64(tfc);

		/* Sync the descriptor. */
		STGE_CDTXSYNC(sc, nexttx,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

		/*
		 * Kick the transmit DMA logic.
		 */
		CSR_WRITE_4(sc, STGE_DMACtrl,
		    sc->sc_DMACtrl | DMAC_TxDMAPollNow);

		/*
		 * Store a pointer to the packet so we can free it later.
		 */
		ds->ds_mbuf = m0;

		/* Advance the tx pointer. */
		sc->sc_txpending++;
		sc->sc_txlast = nexttx;

#if NBPFILTER > 0
		/*
		 * Pass the packet to any BPF listeners.
		 */
		if (ifp->if_bpf)
			bpf_mtap_ether(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif /* NBPFILTER > 0 */
	}

	if (sc->sc_txpending == (STGE_NTXDESC - 1)) {
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

		/* Set a watchdog timer in case the chip flakes out. */
		ifp->if_timer = 5;
	}
}

/*
 * stge_watchdog:	[ifnet interface function]
 *
 *	Watchdog timer handler.
 */
void
stge_watchdog(struct ifnet *ifp)
{
	struct stge_softc *sc = ifp->if_softc;

	/*
	 * Sweep up first, since we don't interrupt every frame.
	 */
	stge_txintr(sc);
	if (sc->sc_txpending != 0) {
		printf("%s: device timeout\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;

		(void) stge_init(ifp);

		/* Try to get more packets going. */
		stge_start(ifp);
	}
}

/*
 * stge_ioctl:		[ifnet interface function]
 *
 *	Handle control requests from the operator.
 */
int
stge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct stge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			stge_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				stge_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				stge_stop(ifp, 1);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			stge_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

/*
 * stge_intr:
 *
 *	Interrupt service routine.
 */
int
stge_intr(void *arg)
{
	struct stge_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint32_t txstat;
	int wantinit;
	uint16_t isr;

	if ((CSR_READ_2(sc, STGE_IntStatus) & IS_InterruptStatus) == 0)
		return (0);

	for (wantinit = 0; wantinit == 0;) {
		isr = CSR_READ_2(sc, STGE_IntStatusAck);
		if ((isr & sc->sc_IntEnable) == 0)
			break;

		/* Host interface errors. */
		if (isr & IS_HostError) {
			printf("%s: Host interface error\n",
			    sc->sc_dev.dv_xname);
			wantinit = 1;
			continue;
		}

		/* Receive interrupts. */
		if (isr & (IS_RxDMAComplete|IS_RFDListEnd)) {
			stge_rxintr(sc);
			if (isr & IS_RFDListEnd) {
				printf("%s: receive ring overflow\n",
				    sc->sc_dev.dv_xname);
				/*
				 * XXX Should try to recover from this
				 * XXX more gracefully.
				 */
				wantinit = 1;
			}
		}

		/* Transmit interrupts. */
		if (isr & (IS_TxDMAComplete|IS_TxComplete))
			stge_txintr(sc);

		/* Statistics overflow. */
		if (isr & IS_UpdateStats)
			stge_stats_update(sc);

		/* Transmission errors. */
		if (isr & IS_TxComplete) {
			for (;;) {
				txstat = CSR_READ_4(sc, STGE_TxStatus);
				if ((txstat & TS_TxComplete) == 0)
					break;
				if (txstat & TS_TxUnderrun) {
					sc->sc_txthresh++;
					if (sc->sc_txthresh > 0x0fff)
						sc->sc_txthresh = 0x0fff;
					printf("%s: transmit underrun, new "
					    "threshold: %d bytes\n",
					    sc->sc_dev.dv_xname,
					    sc->sc_txthresh << 5);
				}
				if (txstat & TS_MaxCollisions)
					printf("%s: excessive collisions\n",
					    sc->sc_dev.dv_xname);
			}
			wantinit = 1;
		}

	}

	if (wantinit)
		stge_init(ifp);

	CSR_WRITE_2(sc, STGE_IntEnable, sc->sc_IntEnable);

	/* Try to get more packets going. */
	stge_start(ifp);

	return (1);
}

/*
 * stge_txintr:
 *
 *	Helper; handle transmit interrupts.
 */
void
stge_txintr(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct stge_descsoft *ds;
	uint64_t control;
	int i;

	ifq_clr_oactive(&ifp->if_snd);

	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (i = sc->sc_txdirty; sc->sc_txpending != 0;
	     i = STGE_NEXTTX(i), sc->sc_txpending--) {
		ds = &sc->sc_txsoft[i];

		STGE_CDTXSYNC(sc, i,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		control = letoh64(sc->sc_txdescs[i].tfd_control);
		if ((control & TFD_TFDDone) == 0)
			break;

		bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap,
		    0, ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
		m_freem(ds->ds_mbuf);
		ds->ds_mbuf = NULL;
	}

	/* Update the dirty transmit buffer pointer. */
	sc->sc_txdirty = i;

	/*
	 * If there are no more pending transmissions, cancel the watchdog
	 * timer.
	 */
	if (sc->sc_txpending == 0)
		ifp->if_timer = 0;
}

/*
 * stge_rxintr:
 *
 *	Helper; handle receive interrupts.
 */
void
stge_rxintr(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct stge_descsoft *ds;
	struct mbuf *m, *tailm;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	uint64_t status;
	int i, len;

	for (i = sc->sc_rxptr;; i = STGE_NEXTRX(i)) {
		ds = &sc->sc_rxsoft[i];

		STGE_CDRXSYNC(sc, i,
		    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

		status = letoh64(sc->sc_rxdescs[i].rfd_status);

		if ((status & RFD_RFDDone) == 0)
			break;

		if (__predict_false(sc->sc_rxdiscard)) {
			STGE_INIT_RXDESC(sc, i);
			if (status & RFD_FrameEnd) {
				/* Reset our state. */
				sc->sc_rxdiscard = 0;
			}
			continue;
		}

		bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
		    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);

		m = ds->ds_mbuf;

		/*
		 * Add a new receive buffer to the ring.
		 */
		if (stge_add_rxbuf(sc, i) != 0) {
			/*
			 * Failed, throw away what we've done so
			 * far, and discard the rest of the packet.
			 */
			ifp->if_ierrors++;
			bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
			    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
			STGE_INIT_RXDESC(sc, i);
			if ((status & RFD_FrameEnd) == 0)
				sc->sc_rxdiscard = 1;
			m_freem(sc->sc_rxhead);
			STGE_RXCHAIN_RESET(sc);
			continue;
		}

#ifdef DIAGNOSTIC
		if (status & RFD_FrameStart) {
			KASSERT(sc->sc_rxhead == NULL);
			KASSERT(sc->sc_rxtailp == &sc->sc_rxhead);
		}
#endif

		STGE_RXCHAIN_LINK(sc, m);

		/*
		 * If this is not the end of the packet, keep
		 * looking.
		 */
		if ((status & RFD_FrameEnd) == 0) {
			sc->sc_rxlen += m->m_len;
			continue;
		}

		/*
		 * Okay, we have the entire packet now...
		 */
		*sc->sc_rxtailp = NULL;
		m = sc->sc_rxhead;
		tailm = sc->sc_rxtail;

		STGE_RXCHAIN_RESET(sc);

		/*
		 * If the packet had an error, drop it.  Note we
		 * count the error later in the periodic stats update.
		 */
		if (status & (RFD_RxFIFOOverrun | RFD_RxRuntFrame |
			      RFD_RxAlignmentError | RFD_RxFCSError |
			      RFD_RxLengthError)) {
			m_freem(m);
			continue;
		}

		/*
		 * No errors.
		 *
		 * Note we have configured the chip to not include
		 * the CRC at the end of the packet.
		 */
		len = RFD_RxDMAFrameLen(status);
		tailm->m_len = len - sc->sc_rxlen;

		/*
		 * If the packet is small enough to fit in a
		 * single header mbuf, allocate one and copy
		 * the data into it.  This greatly reduces
		 * memory consumption when we receive lots
		 * of small packets.
		 */
		if (stge_copy_small != 0 && len <= (MHLEN - 2)) {
			struct mbuf *nm;
			MGETHDR(nm, M_DONTWAIT, MT_DATA);
			if (nm == NULL) {
				ifp->if_ierrors++;
				m_freem(m);
				continue;
			}
			nm->m_data += 2;
			nm->m_pkthdr.len = nm->m_len = len;
			m_copydata(m, 0, len, mtod(nm, caddr_t));
			m_freem(m);
			m = nm;
		}

		/*
		 * Set the incoming checksum information for the packet.
		 */
		if ((status & RFD_IPDetected) &&
		    (!(status & RFD_IPError)))
			m->m_pkthdr.csum_flags |= M_IPV4_CSUM_IN_OK;
		if ((status & RFD_TCPDetected) &&
		    (!(status & RFD_TCPError)))
			m->m_pkthdr.csum_flags |= M_TCP_CSUM_IN_OK;
		else if ((status & RFD_UDPDetected) &&
		    (!(status & RFD_UDPError)))
			m->m_pkthdr.csum_flags |= M_UDP_CSUM_IN_OK;

#if NVLAN > 0
		/* Check for VLAN tagged packets. */
		if (status & RFD_VLANDetected) {
			m->m_pkthdr.ether_vtag = RFD_TCI(status);
			m->m_flags |= M_VLANTAG;
		}
#endif

		m->m_pkthdr.len = len;

		ml_enqueue(&ml, m);
	}

	/* Update the receive pointer. */
	sc->sc_rxptr = i;

	if_input(ifp, &ml);
}

/*
 * stge_tick:
 *
 *	One second timer, used to tick the MII.
 */
void
stge_tick(void *arg)
{
	struct stge_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	stge_stats_update(sc);
	splx(s);

	timeout_add_sec(&sc->sc_timeout, 1);
}

/*
 * stge_stats_update:
 *
 *	Read the TC9021 statistics counters.
 */
void
stge_stats_update(struct stge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	(void) CSR_READ_4(sc, STGE_OctetRcvOk);

	ifp->if_ierrors +=
	    (u_int) CSR_READ_2(sc, STGE_FramesLostRxErrors);

	(void) CSR_READ_4(sc, STGE_OctetXmtdOk);

	ifp->if_collisions +=
	    CSR_READ_4(sc, STGE_LateCollisions) +
	    CSR_READ_4(sc, STGE_MultiColFrames) +
	    CSR_READ_4(sc, STGE_SingleColFrames);

	ifp->if_oerrors +=
	    (u_int) CSR_READ_2(sc, STGE_FramesAbortXSColls) +
	    (u_int) CSR_READ_2(sc, STGE_FramesWEXDeferal);
}

/*
 * stge_reset:
 *
 *	Perform a soft reset on the TC9021.
 */
void
stge_reset(struct stge_softc *sc)
{
	uint32_t ac;
	int i;

	ac = CSR_READ_4(sc, STGE_AsicCtrl);

	/*
	 * Only assert RstOut if we're fiber.  We need GMII clocks
	 * to be present in order for the reset to complete on fiber
	 * cards.
	 */
	CSR_WRITE_4(sc, STGE_AsicCtrl,
	    ac | AC_GlobalReset | AC_RxReset | AC_TxReset |
	    AC_DMA | AC_FIFO | AC_Network | AC_Host | AC_AutoInit |
	    (sc->sc_usefiber ? AC_RstOut : 0));

	delay(50000);

	for (i = 0; i < STGE_TIMEOUT; i++) {
		delay(5000);
		if ((CSR_READ_4(sc, STGE_AsicCtrl) & AC_ResetBusy) == 0)
			break;
	}

	if (i == STGE_TIMEOUT)
		printf("%s: reset failed to complete\n", sc->sc_dev.dv_xname);

	delay(1000);
}

/*
 * stge_init:		[ ifnet interface function ]
 *
 *	Initialize the interface.  Must be called at splnet().
 */
int
stge_init(struct ifnet *ifp)
{
	struct stge_softc *sc = ifp->if_softc;
	struct stge_descsoft *ds;
	int i, error = 0;

	/*
	 * Cancel any pending I/O.
	 */
	stge_stop(ifp, 0);

	/*
	 * Reset the chip to a known state.
	 */
	stge_reset(sc);

	/*
	 * Initialize the transmit descriptor ring.
	 */
	memset(sc->sc_txdescs, 0, sizeof(sc->sc_txdescs));
	for (i = 0; i < STGE_NTXDESC; i++) {
		sc->sc_txdescs[i].tfd_next = htole64(
		    STGE_CDTXADDR(sc, STGE_NEXTTX(i)));
		sc->sc_txdescs[i].tfd_control = htole64(TFD_TFDDone);
	}
	sc->sc_txpending = 0;
	sc->sc_txdirty = 0;
	sc->sc_txlast = STGE_NTXDESC - 1;

	/*
	 * Initialize the receive descriptor and receive job
	 * descriptor rings.
	 */
	for (i = 0; i < STGE_NRXDESC; i++) {
		ds = &sc->sc_rxsoft[i];
		if (ds->ds_mbuf == NULL) {
			if ((error = stge_add_rxbuf(sc, i)) != 0) {
				printf("%s: unable to allocate or map rx "
				    "buffer %d, error = %d\n",
				    sc->sc_dev.dv_xname, i, error);
				/*
				 * XXX Should attempt to run with fewer receive
				 * XXX buffers instead of just failing.
				 */
				stge_rxdrain(sc);
				goto out;
			}
		} else
			STGE_INIT_RXDESC(sc, i);
	}
	sc->sc_rxptr = 0;
	sc->sc_rxdiscard = 0;
	STGE_RXCHAIN_RESET(sc);

	/* Set the station address. */
	if (sc->sc_stge1023) {
		CSR_WRITE_2(sc, STGE_StationAddress0,
		    sc->sc_arpcom.ac_enaddr[0] | sc->sc_arpcom.ac_enaddr[1] << 8);
		CSR_WRITE_2(sc, STGE_StationAddress1,
		    sc->sc_arpcom.ac_enaddr[2] | sc->sc_arpcom.ac_enaddr[3] << 8);
		CSR_WRITE_2(sc, STGE_StationAddress2,
		    sc->sc_arpcom.ac_enaddr[4] | sc->sc_arpcom.ac_enaddr[5] << 8);
	} else {
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			CSR_WRITE_1(sc, STGE_StationAddress0 + i,
			    sc->sc_arpcom.ac_enaddr[i]);
	}

	/*
	 * Set the statistics masks.  Disable all the RMON stats,
	 * and disable selected stats in the non-RMON stats registers.
	 */
	CSR_WRITE_4(sc, STGE_RMONStatisticsMask, 0xffffffff);
	CSR_WRITE_4(sc, STGE_StatisticsMask,
	    (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4) | (1U << 5) |
	    (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9) | (1U << 10) |
	    (1U << 13) | (1U << 14) | (1U << 15) | (1U << 19) | (1U << 20) |
	    (1U << 21));

	/* Program promiscuous mode and multicast filters. */
	stge_iff(sc);

	/*
	 * Give the transmit and receive ring to the chip.
	 */
	CSR_WRITE_4(sc, STGE_TFDListPtrHi, 0); /* NOTE: 32-bit DMA */
	CSR_WRITE_4(sc, STGE_TFDListPtrLo,
	    STGE_CDTXADDR(sc, sc->sc_txdirty));

	CSR_WRITE_4(sc, STGE_RFDListPtrHi, 0); /* NOTE: 32-bit DMA */
	CSR_WRITE_4(sc, STGE_RFDListPtrLo,
	    STGE_CDRXADDR(sc, sc->sc_rxptr));

	/*
	 * Initialize the Tx auto-poll period.  It's OK to make this number
	 * large (255 is the max, but we use 127) -- we explicitly kick the
	 * transmit engine when there's actually a packet.
	 */
	CSR_WRITE_1(sc, STGE_TxDMAPollPeriod, 127);

	/* ..and the Rx auto-poll period. */
	CSR_WRITE_1(sc, STGE_RxDMAPollPeriod, 64);

	/* Initialize the Tx start threshold. */
	CSR_WRITE_2(sc, STGE_TxStartThresh, sc->sc_txthresh);

	/* RX DMA thresholds, from linux */
	CSR_WRITE_1(sc, STGE_RxDMABurstThresh, 0x30);
	CSR_WRITE_1(sc, STGE_RxDMAUrgentThresh, 0x30);

	/* Rx early threshold, from Linux */
	CSR_WRITE_2(sc, STGE_RxEarlyThresh, 0x7ff);

	/* Tx DMA thresholds, from Linux */
	CSR_WRITE_1(sc, STGE_TxDMABurstThresh, 0x30);
	CSR_WRITE_1(sc, STGE_TxDMAUrgentThresh, 0x04);

	/*
	 * Initialize the Rx DMA interrupt control register.  We
	 * request an interrupt after every incoming packet, but
	 * defer it for 32us (64 * 512 ns).  When the number of
	 * interrupts pending reaches 8, we stop deferring the
	 * interrupt, and signal it immediately.
	 */
	CSR_WRITE_4(sc, STGE_RxDMAIntCtrl,
	    RDIC_RxFrameCount(8) | RDIC_RxDMAWaitTime(512));

	/*
	 * Initialize the interrupt mask.
	 */
	sc->sc_IntEnable = IS_HostError | IS_TxComplete | IS_UpdateStats |
	    IS_TxDMAComplete | IS_RxDMAComplete | IS_RFDListEnd;
	CSR_WRITE_2(sc, STGE_IntStatus, 0xffff);
	CSR_WRITE_2(sc, STGE_IntEnable, sc->sc_IntEnable);

	/*
	 * Configure the DMA engine.
	 * XXX Should auto-tune TxBurstLimit.
	 */
	CSR_WRITE_4(sc, STGE_DMACtrl, sc->sc_DMACtrl |
	    DMAC_TxBurstLimit(3));

	/*
	 * Send a PAUSE frame when we reach 29,696 bytes in the Rx
	 * FIFO, and send an un-PAUSE frame when we reach 3056 bytes
	 * in the Rx FIFO.
	 */
	CSR_WRITE_2(sc, STGE_FlowOnTresh, 29696 / 16);
	CSR_WRITE_2(sc, STGE_FlowOffThresh, 3056 / 16);

	/*
	 * Set the maximum frame size.
	 */
#ifdef STGE_JUMBO
	CSR_WRITE_2(sc, STGE_MaxFrameSize, STGE_JUMBO_FRAMELEN);
#else
	CSR_WRITE_2(sc, STGE_MaxFrameSize, ETHER_MAX_LEN);
#endif

	/*
	 * Initialize MacCtrl -- do it before setting the media,
	 * as setting the media will actually program the register.
	 *
	 * Note: We have to poke the IFS value before poking
	 * anything else.
	 */
	sc->sc_MACCtrl = MC_IFSSelect(0);
	CSR_WRITE_4(sc, STGE_MACCtrl, sc->sc_MACCtrl);

	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		sc->sc_MACCtrl |= MC_AutoVLANuntagging;

	sc->sc_MACCtrl |= MC_StatisticsEnable | MC_TxEnable | MC_RxEnable;

	if (sc->sc_rev >= 6) {		/* >= B.2 */
		/* Multi-frag frame bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0200);

		/* Tx Poll Now bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0010);

		/* Rx Poll Now bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0020);
	}

	/*
	 * Set the current media.
	 */
	mii_mediachg(&sc->sc_mii);

	/*
	 * Start the one second MII clock.
	 */
	timeout_add_sec(&sc->sc_timeout, 1);

	/*
	 * ...all done!
	 */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

 out:
	if (error)
		printf("%s: interface not running\n", sc->sc_dev.dv_xname);
	return (error);
}

/*
 * stge_drain:
 *
 *	Drain the receive queue.
 */
void
stge_rxdrain(struct stge_softc *sc)
{
	struct stge_descsoft *ds;
	int i;

	for (i = 0; i < STGE_NRXDESC; i++) {
		ds = &sc->sc_rxsoft[i];
		if (ds->ds_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
			ds->ds_mbuf->m_next = NULL;
			m_freem(ds->ds_mbuf);
			ds->ds_mbuf = NULL;
		}
	}
}

/*
 * stge_stop:		[ ifnet interface function ]
 *
 *	Stop transmission on the interface.
 */
void
stge_stop(struct ifnet *ifp, int disable)
{
	struct stge_softc *sc = ifp->if_softc;
	struct stge_descsoft *ds;
	int i;

	/*
	 * Stop the one second clock.
	 */
	timeout_del(&sc->sc_timeout);

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	/* Down the MII. */
	mii_down(&sc->sc_mii);

	/*
	 * Disable interrupts.
	 */
	CSR_WRITE_2(sc, STGE_IntEnable, 0);

	/*
	 * Stop receiver, transmitter, and stats update.
	 */
	CSR_WRITE_4(sc, STGE_MACCtrl,
	    MC_StatisticsDisable | MC_TxDisable | MC_RxDisable);

	/*
	 * Stop the transmit and receive DMA.
	 */
	stge_dma_wait(sc);
	CSR_WRITE_4(sc, STGE_TFDListPtrHi, 0);
	CSR_WRITE_4(sc, STGE_TFDListPtrLo, 0);
	CSR_WRITE_4(sc, STGE_RFDListPtrHi, 0);
	CSR_WRITE_4(sc, STGE_RFDListPtrLo, 0);

	/*
	 * Release any queued transmit buffers.
	 */
	for (i = 0; i < STGE_NTXDESC; i++) {
		ds = &sc->sc_txsoft[i];
		if (ds->ds_mbuf != NULL) {
			bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);
			m_freem(ds->ds_mbuf);
			ds->ds_mbuf = NULL;
		}
	}

	if (disable)
		stge_rxdrain(sc);
}

static int
stge_eeprom_wait(struct stge_softc *sc)
{
	int i;

	for (i = 0; i < STGE_TIMEOUT; i++) {
		delay(1000);
		if ((CSR_READ_2(sc, STGE_EepromCtrl) & EC_EepromBusy) == 0)
			return (0);
	}
	return (1);
}

/*
 * stge_read_eeprom:
 *
 *	Read data from the serial EEPROM.
 */
void
stge_read_eeprom(struct stge_softc *sc, int offset, uint16_t *data)
{

	if (stge_eeprom_wait(sc))
		printf("%s: EEPROM failed to come ready\n",
		    sc->sc_dev.dv_xname);

	CSR_WRITE_2(sc, STGE_EepromCtrl,
	    EC_EepromAddress(offset) | EC_EepromOpcode(EC_OP_RR));
	if (stge_eeprom_wait(sc))
		printf("%s: EEPROM read timed out\n",
		    sc->sc_dev.dv_xname);
	*data = CSR_READ_2(sc, STGE_EepromData);
}

/*
 * stge_add_rxbuf:
 *
 *	Add a receive buffer to the indicated descriptor.
 */
int
stge_add_rxbuf(struct stge_softc *sc, int idx)
{
	struct stge_descsoft *ds = &sc->sc_rxsoft[idx];
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

	m->m_data = m->m_ext.ext_buf + 2;
	m->m_len = MCLBYTES - 2;

	if (ds->ds_mbuf != NULL)
		bus_dmamap_unload(sc->sc_dmat, ds->ds_dmamap);

	ds->ds_mbuf = m;

	error = bus_dmamap_load(sc->sc_dmat, ds->ds_dmamap,
	    m->m_ext.ext_buf, m->m_ext.ext_size, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: can't load rx DMA map %d, error = %d\n",
		    sc->sc_dev.dv_xname, idx, error);
		panic("stge_add_rxbuf");	/* XXX */
	}

	bus_dmamap_sync(sc->sc_dmat, ds->ds_dmamap, 0,
	    ds->ds_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	STGE_INIT_RXDESC(sc, idx);

	return (0);
}

/*
 * stge_iff:
 *
 *	Set up the receive filter.
 */
void
stge_iff(struct stge_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc;
	uint32_t mchash[2];

	memset(mchash, 0, sizeof(mchash));
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast packets.
	 * Always accept frames destined to our station address.
	 */
	sc->sc_ReceiveMode = RM_ReceiveBroadcast | RM_ReceiveUnicast;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			sc->sc_ReceiveMode |= RM_ReceiveAllFrames;
		else
			sc->sc_ReceiveMode |= RM_ReceiveMulticast;
	} else {
		/*
		 * Set up the multicast address filter by passing all
		 * multicast addresses through a CRC generator, and then
		 * using the low-order 6 bits as an index into the 64 bit
		 * multicast hash table.  The high order bits select the
		 * register, while the rest of the bits select the bit
		 * within the register.
		 */
		sc->sc_ReceiveMode |= RM_ReceiveMulticastHash;

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN);

			/* Just want the 6 least significant bits. */
			crc &= 0x3f;

			/* Set the corresponding bit in the hash table. */
			mchash[crc >> 5] |= 1 << (crc & 0x1f);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, STGE_HashTable0, mchash[0]);
	CSR_WRITE_4(sc, STGE_HashTable1, mchash[1]);
	CSR_WRITE_2(sc, STGE_ReceiveMode, sc->sc_ReceiveMode);
}

/*
 * stge_mii_readreg:	[mii interface function]
 *
 *	Read a PHY register on the MII of the TC9021.
 */
int
stge_mii_readreg(struct device *self, int phy, int reg)
{

	return (mii_bitbang_readreg(self, &stge_mii_bitbang_ops, phy, reg));
}

/*
 * stge_mii_writereg:	[mii interface function]
 *
 *	Write a PHY register on the MII of the TC9021.
 */
void
stge_mii_writereg(struct device *self, int phy, int reg, int val)
{

	mii_bitbang_writereg(self, &stge_mii_bitbang_ops, phy, reg, val);
}

/*
 * stge_mii_statchg:	[mii interface function]
 *
 *	Callback from MII layer when media changes.
 */
void
stge_mii_statchg(struct device *self)
{
	struct stge_softc *sc = (struct stge_softc *) self;
	struct mii_data *mii = &sc->sc_mii;

	sc->sc_MACCtrl &= ~(MC_DuplexSelect | MC_RxFlowControlEnable |
	    MC_TxFlowControlEnable);

	if (((mii->mii_media_active & IFM_GMASK) & IFM_FDX) != 0)
		sc->sc_MACCtrl |= MC_DuplexSelect;

	if (((mii->mii_media_active & IFM_GMASK) & IFM_ETH_RXPAUSE) != 0)
		sc->sc_MACCtrl |= MC_RxFlowControlEnable;
	if (((mii->mii_media_active & IFM_GMASK) & IFM_ETH_TXPAUSE) != 0)
		sc->sc_MACCtrl |= MC_TxFlowControlEnable;

	CSR_WRITE_4(sc, STGE_MACCtrl, sc->sc_MACCtrl);
}

/*
 * sste_mii_bitbang_read: [mii bit-bang interface function]
 *
 *	Read the MII serial port for the MII bit-bang module.
 */
uint32_t
stge_mii_bitbang_read(struct device *self)
{
	struct stge_softc *sc = (void *) self;

	return (CSR_READ_1(sc, STGE_PhyCtrl));
}

/*
 * stge_mii_bitbang_write: [mii big-bang interface function]
 *
 *	Write the MII serial port for the MII bit-bang module.
 */
void
stge_mii_bitbang_write(struct device *self, uint32_t val)
{
	struct stge_softc *sc = (void *) self;

	CSR_WRITE_1(sc, STGE_PhyCtrl, val | sc->sc_PhyCtrl);
}

/*
 * stge_mediastatus:	[ifmedia interface function]
 *
 *	Get the current interface media status.
 */
void
stge_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct stge_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

/*
 * stge_mediachange:	[ifmedia interface function]
 *
 *	Set hardware to newly-selected media.
 */
int
stge_mediachange(struct ifnet *ifp)
{
	struct stge_softc *sc = ifp->if_softc;

	if (ifp->if_flags & IFF_UP)
		mii_mediachg(&sc->sc_mii);
	return (0);
}
