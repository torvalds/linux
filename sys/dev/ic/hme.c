/*	$OpenBSD: hme.c,v 1.83 2020/12/12 11:48:52 jan Exp $	*/
/*	$NetBSD: hme.c,v 1.21 2001/07/07 15:59:37 thorpej Exp $	*/

/*-
 * Copyright (c) 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * HME Ethernet module driver.
 */

#include "bpfilter.h"

#undef HMEDEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h> 
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/ioctl.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <machine/bus.h>

#include <dev/ic/hmereg.h>
#include <dev/ic/hmevar.h>

struct cfdriver hme_cd = {
	NULL, "hme", DV_IFNET
};

#define	HME_RX_OFFSET	2

void		hme_start(struct ifnet *);
void		hme_stop(struct hme_softc *, int);
int		hme_ioctl(struct ifnet *, u_long, caddr_t);
void		hme_tick(void *);
void		hme_watchdog(struct ifnet *);
void		hme_init(struct hme_softc *);
void		hme_meminit(struct hme_softc *);
void		hme_mifinit(struct hme_softc *);
void		hme_reset(struct hme_softc *);
void		hme_iff(struct hme_softc *);
void		hme_fill_rx_ring(struct hme_softc *);
int		hme_newbuf(struct hme_softc *, struct hme_sxd *);

/* MII methods & callbacks */
static int	hme_mii_readreg(struct device *, int, int);
static void	hme_mii_writereg(struct device *, int, int, int);
static void	hme_mii_statchg(struct device *);

int		hme_mediachange(struct ifnet *);
void		hme_mediastatus(struct ifnet *, struct ifmediareq *);

int		hme_eint(struct hme_softc *, u_int);
int		hme_rint(struct hme_softc *);
int		hme_tint(struct hme_softc *);

void
hme_config(struct hme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii = &sc->sc_mii;
	struct mii_softc *child;
	bus_dma_tag_t dmatag = sc->sc_dmatag;
	bus_dma_segment_t seg;
	bus_size_t size;
	int rseg, error, i;

	/*
	 * HME common initialization.
	 *
	 * hme_softc fields that must be initialized by the front-end:
	 *
	 * the bus tag:
	 *	sc_bustag
	 *
	 * the dma bus tag:
	 *	sc_dmatag
	 *
	 * the bus handles:
	 *	sc_seb		(Shared Ethernet Block registers)
	 *	sc_erx		(Receiver Unit registers)
	 *	sc_etx		(Transmitter Unit registers)
	 *	sc_mac		(MAC registers)
	 *	sc_mif		(Management Interface registers)
	 *
	 * the maximum bus burst size:
	 *	sc_burst
	 *
	 * the local Ethernet address:
	 *	sc_arpcom.ac_enaddr
	 *
	 */

	/* Make sure the chip is stopped. */
	hme_stop(sc, 0);

	for (i = 0; i < HME_TX_RING_SIZE; i++) {
		if (bus_dmamap_create(sc->sc_dmatag, MCLBYTES, HME_TX_NSEGS,
		    MCLBYTES, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &sc->sc_txd[i].sd_map) != 0) {
			sc->sc_txd[i].sd_map = NULL;
			goto fail;
		}
	}
	for (i = 0; i < HME_RX_RING_SIZE; i++) {
		if (bus_dmamap_create(sc->sc_dmatag, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &sc->sc_rxd[i].sd_map) != 0) {
			sc->sc_rxd[i].sd_map = NULL;
			goto fail;
		}
	}
	if (bus_dmamap_create(sc->sc_dmatag, MCLBYTES, 1, MCLBYTES, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_rxmap_spare) != 0) {
		sc->sc_rxmap_spare = NULL;
		goto fail;
	}

	/*
	 * Allocate DMA capable memory
	 * Buffer descriptors must be aligned on a 2048 byte boundary;
	 * take this into account when calculating the size. Note that
	 * the maximum number of descriptors (256) occupies 2048 bytes,
	 * so we allocate that much regardless of the number of descriptors.
	 */
	size = (HME_XD_SIZE * HME_RX_RING_MAX) +	/* RX descriptors */
	    (HME_XD_SIZE * HME_TX_RING_MAX);		/* TX descriptors */

	/* Allocate DMA buffer */
	if ((error = bus_dmamem_alloc(dmatag, size, 2048, 0, &seg, 1, &rseg,
	    BUS_DMA_NOWAIT)) != 0) {
		printf("\n%s: DMA buffer alloc error %d\n",
		    sc->sc_dev.dv_xname, error);
		return;
	}

	/* Map DMA memory in CPU addressable space */
	if ((error = bus_dmamem_map(dmatag, &seg, rseg, size,
	    &sc->sc_rb.rb_membase, BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("\n%s: DMA buffer map error %d\n",
		    sc->sc_dev.dv_xname, error);
		bus_dmamap_unload(dmatag, sc->sc_dmamap);
		bus_dmamem_free(dmatag, &seg, rseg);
		return;
	}

	if ((error = bus_dmamap_create(dmatag, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &sc->sc_dmamap)) != 0) {
		printf("\n%s: DMA map create error %d\n",
		    sc->sc_dev.dv_xname, error);
		return;
	}

	/* Load the buffer */
	if ((error = bus_dmamap_load(dmatag, sc->sc_dmamap,
	    sc->sc_rb.rb_membase, size, NULL,
	    BUS_DMA_NOWAIT|BUS_DMA_COHERENT)) != 0) {
		printf("\n%s: DMA buffer map load error %d\n",
		    sc->sc_dev.dv_xname, error);
		bus_dmamem_free(dmatag, &seg, rseg);
		return;
	}
	sc->sc_rb.rb_dmabase = sc->sc_dmamap->dm_segs[0].ds_addr;

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Initialize ifnet structure. */
	strlcpy(ifp->if_xname, sc->sc_dev.dv_xname, sizeof ifp->if_xname);
	ifp->if_softc = sc;
	ifp->if_start = hme_start;
	ifp->if_ioctl = hme_ioctl;
	ifp->if_watchdog = hme_watchdog;
	ifp->if_flags =
	    IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Initialize ifmedia structures and MII info */
	mii->mii_ifp = ifp;
	mii->mii_readreg = hme_mii_readreg; 
	mii->mii_writereg = hme_mii_writereg;
	mii->mii_statchg = hme_mii_statchg;

	ifmedia_init(&mii->mii_media, IFM_IMASK,
	    hme_mediachange, hme_mediastatus);

	hme_mifinit(sc);

	if (sc->sc_tcvr == -1)
		mii_attach(&sc->sc_dev, mii, 0xffffffff, MII_PHY_ANY,
		    MII_OFFSET_ANY, 0);
	else
		mii_attach(&sc->sc_dev, mii, 0xffffffff, sc->sc_tcvr,
		    MII_OFFSET_ANY, 0);

	child = LIST_FIRST(&mii->mii_phys);
	if (child == NULL) {
		/* No PHY attached */
		ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
	} else {
		/*
		 * Walk along the list of attached MII devices and
		 * establish an `MII instance' to `phy number'
		 * mapping. We'll use this mapping in media change
		 * requests to determine which phy to use to program
		 * the MIF configuration register.
		 */
		for (; child != NULL; child = LIST_NEXT(child, mii_list)) {
			/*
			 * Note: we support just two PHYs: the built-in
			 * internal device and an external on the MII
			 * connector.
			 */
			if (child->mii_phy > 1 || child->mii_inst > 1) {
				printf("%s: cannot accommodate MII device %s"
				    " at phy %d, instance %lld\n",
				    sc->sc_dev.dv_xname,
				    child->mii_dev.dv_xname,
				    child->mii_phy, child->mii_inst);
				continue;
			}

			sc->sc_phys[child->mii_inst] = child->mii_phy;
		}

		/*
		 * XXX - we can really do the following ONLY if the
		 * phy indeed has the auto negotiation capability!!
		 */
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);
	}

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->sc_tick_ch, hme_tick, sc);
	return;

fail:
	if (sc->sc_rxmap_spare != NULL)
		bus_dmamap_destroy(sc->sc_dmatag, sc->sc_rxmap_spare);
	for (i = 0; i < HME_TX_RING_SIZE; i++)
		if (sc->sc_txd[i].sd_map != NULL)
			bus_dmamap_destroy(sc->sc_dmatag, sc->sc_txd[i].sd_map);
	for (i = 0; i < HME_RX_RING_SIZE; i++)
		if (sc->sc_rxd[i].sd_map != NULL)
			bus_dmamap_destroy(sc->sc_dmatag, sc->sc_rxd[i].sd_map);
}

void
hme_unconfig(struct hme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int i;

	hme_stop(sc, 1);

	bus_dmamap_destroy(sc->sc_dmatag, sc->sc_rxmap_spare);
	for (i = 0; i < HME_TX_RING_SIZE; i++)
		if (sc->sc_txd[i].sd_map != NULL)
			bus_dmamap_destroy(sc->sc_dmatag, sc->sc_txd[i].sd_map);
	for (i = 0; i < HME_RX_RING_SIZE; i++)
		if (sc->sc_rxd[i].sd_map != NULL)
			bus_dmamap_destroy(sc->sc_dmatag, sc->sc_rxd[i].sd_map);

	/* Detach all PHYs */
	mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);
}

void
hme_tick(void *arg)
{
	struct hme_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mac = sc->sc_mac;
	int s;

	s = splnet();
	/*
	 * Unload collision counters
	 */
	ifp->if_collisions +=
	    bus_space_read_4(t, mac, HME_MACI_NCCNT) +
	    bus_space_read_4(t, mac, HME_MACI_FCCNT) +
	    bus_space_read_4(t, mac, HME_MACI_EXCNT) +
	    bus_space_read_4(t, mac, HME_MACI_LTCNT);

	/*
	 * then clear the hardware counters.
	 */
	bus_space_write_4(t, mac, HME_MACI_NCCNT, 0);
	bus_space_write_4(t, mac, HME_MACI_FCCNT, 0);
	bus_space_write_4(t, mac, HME_MACI_EXCNT, 0);
	bus_space_write_4(t, mac, HME_MACI_LTCNT, 0);

	/*
	 * If buffer allocation fails, the receive ring may become
	 * empty. There is no receive interrupt to recover from that.
	 */
	if (if_rxr_inuse(&sc->sc_rx_ring) == 0)
		hme_fill_rx_ring(sc);

	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick_ch, 1);
}

void
hme_reset(struct hme_softc *sc)
{
	int s;

	s = splnet();
	hme_init(sc);
	splx(s);
}

void
hme_stop(struct hme_softc *sc, int softonly)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t seb = sc->sc_seb;
	int n;

	timeout_del(&sc->sc_tick_ch);

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	if (!softonly) {
		mii_down(&sc->sc_mii);

		/* Mask all interrupts */
		bus_space_write_4(t, seb, HME_SEBI_IMASK, 0xffffffff);

		/* Reset transmitter and receiver */
		bus_space_write_4(t, seb, HME_SEBI_RESET,
		    (HME_SEB_RESET_ETX | HME_SEB_RESET_ERX));

		for (n = 0; n < 20; n++) {
			u_int32_t v = bus_space_read_4(t, seb, HME_SEBI_RESET);
			if ((v & (HME_SEB_RESET_ETX | HME_SEB_RESET_ERX)) == 0)
				break;
			DELAY(20);
		}
		if (n >= 20)
			printf("%s: hme_stop: reset failed\n", sc->sc_dev.dv_xname);
	}

	for (n = 0; n < HME_TX_RING_SIZE; n++) {
		if (sc->sc_txd[n].sd_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_dmatag, sc->sc_txd[n].sd_map,
			    0, sc->sc_txd[n].sd_map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmatag, sc->sc_txd[n].sd_map);
			m_freem(sc->sc_txd[n].sd_mbuf);
			sc->sc_txd[n].sd_mbuf = NULL;
		}
	}
	sc->sc_tx_prod = sc->sc_tx_cons = sc->sc_tx_cnt = 0;

	for (n = 0; n < HME_RX_RING_SIZE; n++) {
		if (sc->sc_rxd[n].sd_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_dmatag, sc->sc_rxd[n].sd_map,
			    0, sc->sc_rxd[n].sd_map->dm_mapsize,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmatag, sc->sc_rxd[n].sd_map);
			m_freem(sc->sc_rxd[n].sd_mbuf);
			sc->sc_rxd[n].sd_mbuf = NULL;
		}
	}
	sc->sc_rx_prod = sc->sc_rx_cons = 0;
}

void
hme_meminit(struct hme_softc *sc)
{
	bus_addr_t dma;
	caddr_t p;
	unsigned int i;
	struct hme_ring *hr = &sc->sc_rb;

	p = hr->rb_membase;
	dma = hr->rb_dmabase;

	/*
	 * Allocate transmit descriptors
	 */
	hr->rb_txd = p;
	hr->rb_txddma = dma;
	p += HME_TX_RING_SIZE * HME_XD_SIZE;
	dma += HME_TX_RING_SIZE * HME_XD_SIZE;
	/* We have reserved descriptor space until the next 2048 byte boundary.*/
	dma = (bus_addr_t)roundup((u_long)dma, 2048);
	p = (caddr_t)roundup((u_long)p, 2048);

	/*
	 * Allocate receive descriptors
	 */
	hr->rb_rxd = p;
	hr->rb_rxddma = dma;
	p += HME_RX_RING_SIZE * HME_XD_SIZE;
	dma += HME_RX_RING_SIZE * HME_XD_SIZE;
	/* Again move forward to the next 2048 byte boundary.*/
	dma = (bus_addr_t)roundup((u_long)dma, 2048);
	p = (caddr_t)roundup((u_long)p, 2048);

	/*
	 * Initialize transmit descriptors
	 */
	for (i = 0; i < HME_TX_RING_SIZE; i++) {
		HME_XD_SETADDR(sc->sc_pci, hr->rb_txd, i, 0);
		HME_XD_SETFLAGS(sc->sc_pci, hr->rb_txd, i, 0);
		sc->sc_txd[i].sd_mbuf = NULL;
	}

	/*
	 * Initialize receive descriptors
	 */
	for (i = 0; i < HME_RX_RING_SIZE; i++) {
		HME_XD_SETADDR(sc->sc_pci, hr->rb_rxd, i, 0);
		HME_XD_SETFLAGS(sc->sc_pci, hr->rb_rxd, i, 0);
		sc->sc_rxd[i].sd_mbuf = NULL;
	}

	if_rxr_init(&sc->sc_rx_ring, 2, HME_RX_RING_SIZE);
	hme_fill_rx_ring(sc);
}

/*
 * Initialization of interface; set up initialization block
 * and transmit/receive descriptor rings.
 */
void
hme_init(struct hme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t seb = sc->sc_seb;
	bus_space_handle_t etx = sc->sc_etx;
	bus_space_handle_t erx = sc->sc_erx;
	bus_space_handle_t mac = sc->sc_mac;
	u_int8_t *ea;
	u_int32_t v;

	/*
	 * Initialization sequence. The numbered steps below correspond
	 * to the sequence outlined in section 6.3.5.1 in the Ethernet
	 * Channel Engine manual (part of the PCIO manual).
	 * See also the STP2002-STQ document from Sun Microsystems.
	 */

	/* step 1 & 2. Reset the Ethernet Channel */
	hme_stop(sc, 0);

	/* Re-initialize the MIF */
	hme_mifinit(sc);

	/* step 3. Setup data structures in host memory */
	hme_meminit(sc);

	/* step 4. TX MAC registers & counters */
	bus_space_write_4(t, mac, HME_MACI_NCCNT, 0);
	bus_space_write_4(t, mac, HME_MACI_FCCNT, 0);
	bus_space_write_4(t, mac, HME_MACI_EXCNT, 0);
	bus_space_write_4(t, mac, HME_MACI_LTCNT, 0);
	bus_space_write_4(t, mac, HME_MACI_TXSIZE, ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN);

	/* Load station MAC address */
	ea = sc->sc_arpcom.ac_enaddr;
	bus_space_write_4(t, mac, HME_MACI_MACADDR0, (ea[0] << 8) | ea[1]);
	bus_space_write_4(t, mac, HME_MACI_MACADDR1, (ea[2] << 8) | ea[3]);
	bus_space_write_4(t, mac, HME_MACI_MACADDR2, (ea[4] << 8) | ea[5]);

	/*
	 * Init seed for backoff
	 * (source suggested by manual: low 10 bits of MAC address)
	 */ 
	v = ((ea[4] << 8) | ea[5]) & 0x3fff;
	bus_space_write_4(t, mac, HME_MACI_RANDSEED, v);


	/* Note: Accepting power-on default for other MAC registers here.. */


	/* step 5. RX MAC registers & counters */
	hme_iff(sc);

	/* step 6 & 7. Program Descriptor Ring Base Addresses */
	bus_space_write_4(t, etx, HME_ETXI_RING, sc->sc_rb.rb_txddma);
	bus_space_write_4(t, etx, HME_ETXI_RSIZE, HME_TX_RING_SIZE);

	bus_space_write_4(t, erx, HME_ERXI_RING, sc->sc_rb.rb_rxddma);
	bus_space_write_4(t, mac, HME_MACI_RXSIZE, ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN);

	/* step 8. Global Configuration & Interrupt Mask */
	bus_space_write_4(t, seb, HME_SEBI_IMASK,
	    ~(HME_SEB_STAT_HOSTTOTX | HME_SEB_STAT_RXTOHOST |
	      HME_SEB_STAT_TXALL | HME_SEB_STAT_TXPERR |
	      HME_SEB_STAT_RCNTEXP | HME_SEB_STAT_ALL_ERRORS));

	switch (sc->sc_burst) {
	default:
		v = 0;
		break;
	case 16:
		v = HME_SEB_CFG_BURST16;
		break;
	case 32:
		v = HME_SEB_CFG_BURST32;
		break;
	case 64:
		v = HME_SEB_CFG_BURST64;
		break;
	}
	bus_space_write_4(t, seb, HME_SEBI_CFG, v);

	/* step 9. ETX Configuration: use mostly default values */

	/* Enable DMA */
	v = bus_space_read_4(t, etx, HME_ETXI_CFG);
	v |= HME_ETX_CFG_DMAENABLE;
	bus_space_write_4(t, etx, HME_ETXI_CFG, v);

	/* Transmit Descriptor ring size: in increments of 16 */
	bus_space_write_4(t, etx, HME_ETXI_RSIZE, HME_TX_RING_SIZE / 16 - 1);

	/* step 10. ERX Configuration */
	v = bus_space_read_4(t, erx, HME_ERXI_CFG);
	v &= ~HME_ERX_CFG_RINGSIZE256;
#if HME_RX_RING_SIZE == 32
	v |= HME_ERX_CFG_RINGSIZE32;
#elif HME_RX_RING_SIZE == 64
	v |= HME_ERX_CFG_RINGSIZE64;
#elif HME_RX_RING_SIZE == 128
	v |= HME_ERX_CFG_RINGSIZE128;
#elif HME_RX_RING_SIZE == 256
	v |= HME_ERX_CFG_RINGSIZE256;
#else
# error	"RX ring size must be 32, 64, 128, or 256"
#endif
	/* Enable DMA */
	v |= HME_ERX_CFG_DMAENABLE | (HME_RX_OFFSET << 3);
	bus_space_write_4(t, erx, HME_ERXI_CFG, v);

	/* step 11. XIF Configuration */
	v = bus_space_read_4(t, mac, HME_MACI_XIF);
	v |= HME_MAC_XIF_OE;
	bus_space_write_4(t, mac, HME_MACI_XIF, v);

	/* step 12. RX_MAC Configuration Register */
	v = bus_space_read_4(t, mac, HME_MACI_RXCFG);
	v |= HME_MAC_RXCFG_ENABLE;
	bus_space_write_4(t, mac, HME_MACI_RXCFG, v);

	/* step 13. TX_MAC Configuration Register */
	v = bus_space_read_4(t, mac, HME_MACI_TXCFG);
	v |= (HME_MAC_TXCFG_ENABLE | HME_MAC_TXCFG_DGIVEUP);
	bus_space_write_4(t, mac, HME_MACI_TXCFG, v);

	/* Set the current media. */
	mii_mediachg(&sc->sc_mii);

	/* Start the one second timer. */
	timeout_add_sec(&sc->sc_tick_ch, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	hme_start(ifp);
}

void
hme_start(struct ifnet *ifp)
{
	struct hme_softc *sc = (struct hme_softc *)ifp->if_softc;
	struct hme_ring *hr = &sc->sc_rb;
	struct mbuf *m;
	u_int32_t flags;
	bus_dmamap_t map;
	u_int32_t frag, cur, i;
	int error;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	while (sc->sc_txd[sc->sc_tx_prod].sd_mbuf == NULL) {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		/*
		 * Encapsulate this packet and start it going...
		 * or fail...
		 */

		cur = frag = sc->sc_tx_prod;
		map = sc->sc_txd[cur].sd_map;

		error = bus_dmamap_load_mbuf(sc->sc_dmatag, map, m,
		    BUS_DMA_NOWAIT);
		if (error != 0 && error != EFBIG)
			goto drop;
		if (error != 0) {
			/* Too many fragments, linearize. */
			if (m_defrag(m, M_DONTWAIT))
				goto drop;
			error = bus_dmamap_load_mbuf(sc->sc_dmatag, map, m,
			    BUS_DMA_NOWAIT);
			if (error != 0)
				goto drop;
		}

		if ((HME_TX_RING_SIZE - (sc->sc_tx_cnt + map->dm_nsegs)) < 5) {
			bus_dmamap_unload(sc->sc_dmatag, map);
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* We are now committed to transmitting the packet. */
		ifq_deq_commit(&ifp->if_snd, m);

#if NBPFILTER > 0
		/*
		 * If BPF is listening on this interface, let it see the
		 * packet before we commit it to the wire.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		bus_dmamap_sync(sc->sc_dmatag, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);

		for (i = 0; i < map->dm_nsegs; i++) {
			flags = HME_XD_ENCODE_TSIZE(map->dm_segs[i].ds_len);
			if (i == 0)
				flags |= HME_XD_SOP;
			else
				flags |= HME_XD_OWN;

			HME_XD_SETADDR(sc->sc_pci, hr->rb_txd, frag,
			    map->dm_segs[i].ds_addr);
			HME_XD_SETFLAGS(sc->sc_pci, hr->rb_txd, frag, flags);

			cur = frag;
			if (++frag == HME_TX_RING_SIZE)
				frag = 0;
		}

		/* Set end of packet on last descriptor. */
		flags = HME_XD_GETFLAGS(sc->sc_pci, hr->rb_txd, cur);
		flags |= HME_XD_EOP;
		HME_XD_SETFLAGS(sc->sc_pci, hr->rb_txd, cur, flags);

		sc->sc_tx_cnt += map->dm_nsegs;
		sc->sc_txd[sc->sc_tx_prod].sd_map = sc->sc_txd[cur].sd_map;
		sc->sc_txd[cur].sd_map = map;
		sc->sc_txd[cur].sd_mbuf = m;

		/* Give first frame over to the hardware. */
		flags = HME_XD_GETFLAGS(sc->sc_pci, hr->rb_txd, sc->sc_tx_prod);
		flags |= HME_XD_OWN;
		HME_XD_SETFLAGS(sc->sc_pci, hr->rb_txd, sc->sc_tx_prod, flags);

		bus_space_write_4(sc->sc_bustag, sc->sc_etx, HME_ETXI_PENDING,
		    HME_ETX_TP_DMAWAKEUP);
		sc->sc_tx_prod = frag;

		ifp->if_timer = 5;
	}

	return;

 drop:
	ifq_deq_commit(&ifp->if_snd, m);
	m_freem(m);
	ifp->if_oerrors++;
}

/*
 * Transmit interrupt.
 */
int
hme_tint(struct hme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	unsigned int ri, txflags;
	struct hme_sxd *sd;
	int cnt = sc->sc_tx_cnt;

	/* Fetch current position in the transmit ring */
	ri = sc->sc_tx_cons;
	sd = &sc->sc_txd[ri];

	for (;;) {
		if (cnt <= 0)
			break;

		txflags = HME_XD_GETFLAGS(sc->sc_pci, sc->sc_rb.rb_txd, ri);

		if (txflags & HME_XD_OWN)
			break;

		ifq_clr_oactive(&ifp->if_snd);

		if (sd->sd_mbuf != NULL) {
			bus_dmamap_sync(sc->sc_dmatag, sd->sd_map,
			    0, sd->sd_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmatag, sd->sd_map);
			m_freem(sd->sd_mbuf);
			sd->sd_mbuf = NULL;
		}

		if (++ri == HME_TX_RING_SIZE) {
			ri = 0;
			sd = sc->sc_txd;
		} else
			sd++;

		--cnt;
	}

	sc->sc_tx_cnt = cnt;
	ifp->if_timer = cnt > 0 ? 5 : 0;

	/* Update ring */
	sc->sc_tx_cons = ri;

	hme_start(ifp);

	return (1);
}

/*
 * Receive interrupt.
 */
int
hme_rint(struct hme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	struct hme_sxd *sd;
	unsigned int ri, len;
	u_int32_t flags;

	ri = sc->sc_rx_cons;
	sd = &sc->sc_rxd[ri];

	/*
	 * Process all buffers with valid data.
	 */
	while (if_rxr_inuse(&sc->sc_rx_ring) > 0) {
		flags = HME_XD_GETFLAGS(sc->sc_pci, sc->sc_rb.rb_rxd, ri);
		if (flags & HME_XD_OWN)
			break;

		bus_dmamap_sync(sc->sc_dmatag, sd->sd_map,
		    0, sd->sd_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmatag, sd->sd_map);

		m = sd->sd_mbuf;
		sd->sd_mbuf = NULL;

		if (++ri == HME_RX_RING_SIZE) {
			ri = 0;
			sd = sc->sc_rxd;
		} else
			sd++;

		if_rxr_put(&sc->sc_rx_ring, 1);

		if (flags & HME_XD_OFL) {
			ifp->if_ierrors++;
			printf("%s: buffer overflow, ri=%d; flags=0x%x\n",
			    sc->sc_dev.dv_xname, ri, flags);
			m_freem(m);
			continue;
		}

		len = HME_XD_DECODE_RSIZE(flags);
		m->m_pkthdr.len = m->m_len = len;

		ml_enqueue(&ml, m);
	}

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);

	sc->sc_rx_cons = ri;
	hme_fill_rx_ring(sc);
	return (1);
}

int
hme_eint(struct hme_softc *sc, u_int status)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	if (status & HME_SEB_STAT_MIFIRQ) {
		printf("%s: XXXlink status changed\n", sc->sc_dev.dv_xname);
		status &= ~HME_SEB_STAT_MIFIRQ;
	}

	if (status & HME_SEB_STAT_DTIMEXP) {
		ifp->if_oerrors++;
		status &= ~HME_SEB_STAT_DTIMEXP;
	}

	if (status & HME_SEB_STAT_NORXD) {
		ifp->if_ierrors++;
		status &= ~HME_SEB_STAT_NORXD;
	}

	status &= ~(HME_SEB_STAT_RXTOHOST | HME_SEB_STAT_GOTFRAME |
	    HME_SEB_STAT_SENTFRAME | HME_SEB_STAT_HOSTTOTX |
	    HME_SEB_STAT_TXALL);

	if (status == 0)
		return (1);

#ifdef HME_DEBUG
	printf("%s: status=%b\n", sc->sc_dev.dv_xname, status, HME_SEB_STAT_BITS);
#endif
	return (1);
}

int
hme_intr(void *v)
{
	struct hme_softc *sc = (struct hme_softc *)v;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t seb = sc->sc_seb;
	u_int32_t status;
	int r = 0;

	status = bus_space_read_4(t, seb, HME_SEBI_STAT);
	if (status == 0xffffffff)
		return (0);

	if ((status & HME_SEB_STAT_ALL_ERRORS) != 0)
		r |= hme_eint(sc, status);

	if ((status & (HME_SEB_STAT_TXALL | HME_SEB_STAT_HOSTTOTX)) != 0)
		r |= hme_tint(sc);

	if ((status & HME_SEB_STAT_RXTOHOST) != 0)
		r |= hme_rint(sc);

	return (r);
}


void
hme_watchdog(struct ifnet *ifp)
{
	struct hme_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;

	hme_reset(sc);
}

/*
 * Initialize the MII Management Interface
 */
void
hme_mifinit(struct hme_softc *sc)
{
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mif = sc->sc_mif;
	bus_space_handle_t mac = sc->sc_mac;
	int phy;
	u_int32_t v;

	v = bus_space_read_4(t, mif, HME_MIFI_CFG);
	phy = HME_PHYAD_EXTERNAL;
	if (v & HME_MIF_CFG_MDI1)
		phy = sc->sc_tcvr = HME_PHYAD_EXTERNAL;
	else if (v & HME_MIF_CFG_MDI0)
		phy = sc->sc_tcvr = HME_PHYAD_INTERNAL;
	else
		sc->sc_tcvr = -1;

	/* Configure the MIF in frame mode, no poll, current phy select */
	v = 0;
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MIF_CFG_PHY;
	bus_space_write_4(t, mif, HME_MIFI_CFG, v);

	/* If an external transceiver is selected, enable its MII drivers */
	v = bus_space_read_4(t, mac, HME_MACI_XIF);
	v &= ~HME_MAC_XIF_MIIENABLE;
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MAC_XIF_MIIENABLE;
	bus_space_write_4(t, mac, HME_MACI_XIF, v);
}

/*
 * MII interface
 */
static int
hme_mii_readreg(struct device *self, int phy, int reg)
{
	struct hme_softc *sc = (struct hme_softc *)self;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mif = sc->sc_mif;
	bus_space_handle_t mac = sc->sc_mac;
	u_int32_t v, xif_cfg, mifi_cfg;
	int n;

	if (phy != HME_PHYAD_EXTERNAL && phy != HME_PHYAD_INTERNAL)
		return (0);

	/* Select the desired PHY in the MIF configuration register */
	v = mifi_cfg = bus_space_read_4(t, mif, HME_MIFI_CFG);
	v &= ~HME_MIF_CFG_PHY;
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MIF_CFG_PHY;
	bus_space_write_4(t, mif, HME_MIFI_CFG, v);

	/* Enable MII drivers on external transceiver */ 
	v = xif_cfg = bus_space_read_4(t, mac, HME_MACI_XIF);
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MAC_XIF_MIIENABLE;
	else
		v &= ~HME_MAC_XIF_MIIENABLE;
	bus_space_write_4(t, mac, HME_MACI_XIF, v);

	/* Construct the frame command */
	v = (MII_COMMAND_START << HME_MIF_FO_ST_SHIFT) |
	    HME_MIF_FO_TAMSB |
	    (MII_COMMAND_READ << HME_MIF_FO_OPC_SHIFT) |
	    (phy << HME_MIF_FO_PHYAD_SHIFT) |
	    (reg << HME_MIF_FO_REGAD_SHIFT);

	bus_space_write_4(t, mif, HME_MIFI_FO, v);
	for (n = 0; n < 100; n++) {
		DELAY(1);
		v = bus_space_read_4(t, mif, HME_MIFI_FO);
		if (v & HME_MIF_FO_TALSB) {
			v &= HME_MIF_FO_DATA;
			goto out;
		}
	}

	v = 0;
	printf("%s: mii_read timeout\n", sc->sc_dev.dv_xname);

out:
	/* Restore MIFI_CFG register */
	bus_space_write_4(t, mif, HME_MIFI_CFG, mifi_cfg);
	/* Restore XIF register */
	bus_space_write_4(t, mac, HME_MACI_XIF, xif_cfg);
	return (v);
}

static void
hme_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct hme_softc *sc = (void *)self;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mif = sc->sc_mif;
	bus_space_handle_t mac = sc->sc_mac;
	u_int32_t v, xif_cfg, mifi_cfg;
	int n;

	/* We can at most have two PHYs */
	if (phy != HME_PHYAD_EXTERNAL && phy != HME_PHYAD_INTERNAL)
		return;

	/* Select the desired PHY in the MIF configuration register */
	v = mifi_cfg = bus_space_read_4(t, mif, HME_MIFI_CFG);
	v &= ~HME_MIF_CFG_PHY;
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MIF_CFG_PHY;
	bus_space_write_4(t, mif, HME_MIFI_CFG, v);

	/* Enable MII drivers on external transceiver */ 
	v = xif_cfg = bus_space_read_4(t, mac, HME_MACI_XIF);
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MAC_XIF_MIIENABLE;
	else
		v &= ~HME_MAC_XIF_MIIENABLE;
	bus_space_write_4(t, mac, HME_MACI_XIF, v);

	/* Construct the frame command */
	v = (MII_COMMAND_START << HME_MIF_FO_ST_SHIFT)	|
	    HME_MIF_FO_TAMSB				|
	    (MII_COMMAND_WRITE << HME_MIF_FO_OPC_SHIFT)	|
	    (phy << HME_MIF_FO_PHYAD_SHIFT)		|
	    (reg << HME_MIF_FO_REGAD_SHIFT)		|
	    (val & HME_MIF_FO_DATA);

	bus_space_write_4(t, mif, HME_MIFI_FO, v);
	for (n = 0; n < 100; n++) {
		DELAY(1);
		v = bus_space_read_4(t, mif, HME_MIFI_FO);
		if (v & HME_MIF_FO_TALSB)
			goto out;
	}

	printf("%s: mii_write timeout\n", sc->sc_dev.dv_xname);
out:
	/* Restore MIFI_CFG register */
	bus_space_write_4(t, mif, HME_MIFI_CFG, mifi_cfg);
	/* Restore XIF register */
	bus_space_write_4(t, mac, HME_MACI_XIF, xif_cfg);
}

static void
hme_mii_statchg(struct device *dev)
{
	struct hme_softc *sc = (void *)dev;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mac = sc->sc_mac;
	u_int32_t v;

#ifdef HMEDEBUG
	if (sc->sc_debug)
		printf("hme_mii_statchg: status change\n", phy);
#endif

	/* Set the MAC Full Duplex bit appropriately */
	/* Apparently the hme chip is SIMPLEX if working in full duplex mode,
	   but not otherwise. */
	v = bus_space_read_4(t, mac, HME_MACI_TXCFG);
	if ((IFM_OPTIONS(sc->sc_mii.mii_media_active) & IFM_FDX) != 0) {
		v |= HME_MAC_TXCFG_FULLDPLX;
		sc->sc_arpcom.ac_if.if_flags |= IFF_SIMPLEX;
	} else {
		v &= ~HME_MAC_TXCFG_FULLDPLX;
		sc->sc_arpcom.ac_if.if_flags &= ~IFF_SIMPLEX;
	}
	bus_space_write_4(t, mac, HME_MACI_TXCFG, v);
}

int
hme_mediachange(struct ifnet *ifp)
{
	struct hme_softc *sc = ifp->if_softc;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mif = sc->sc_mif;
	bus_space_handle_t mac = sc->sc_mac;
	uint64_t instance = IFM_INST(sc->sc_mii.mii_media.ifm_cur->ifm_media);
	int phy = sc->sc_phys[instance];
	u_int32_t v;

#ifdef HMEDEBUG
	if (sc->sc_debug)
		printf("hme_mediachange: phy = %d\n", phy);
#endif
	if (IFM_TYPE(sc->sc_media.ifm_media) != IFM_ETHER)
		return (EINVAL);

	/* Select the current PHY in the MIF configuration register */
	v = bus_space_read_4(t, mif, HME_MIFI_CFG);
	v &= ~HME_MIF_CFG_PHY;
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MIF_CFG_PHY;
	bus_space_write_4(t, mif, HME_MIFI_CFG, v);

	/* If an external transceiver is selected, enable its MII drivers */
	v = bus_space_read_4(t, mac, HME_MACI_XIF);
	v &= ~HME_MAC_XIF_MIIENABLE;
	if (phy == HME_PHYAD_EXTERNAL)
		v |= HME_MAC_XIF_MIIENABLE;
	bus_space_write_4(t, mac, HME_MACI_XIF, v);

	return (mii_mediachg(&sc->sc_mii));
}

void
hme_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct hme_softc *sc = ifp->if_softc;

	if ((ifp->if_flags & IFF_UP) == 0)
		return;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
}

/*
 * Process an ioctl request.
 */
int
hme_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct hme_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			hme_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				hme_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				hme_stop(sc, 0);
		}
#ifdef HMEDEBUG
		sc->sc_debug = (ifp->if_flags & IFF_DEBUG) != 0 ? 1 : 0;
#endif
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, MCLBYTES, &sc->sc_rx_ring);
 		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			hme_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
hme_iff(struct hme_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct arpcom *ac = &sc->sc_arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	bus_space_tag_t t = sc->sc_bustag;
	bus_space_handle_t mac = sc->sc_mac;
	u_int32_t hash[4];
	u_int32_t rxcfg, crc;

	rxcfg = bus_space_read_4(t, mac, HME_MACI_RXCFG);
	rxcfg &= ~(HME_MAC_RXCFG_HENABLE | HME_MAC_RXCFG_PMISC);
	ifp->if_flags &= ~IFF_ALLMULTI;
	/* Clear hash table */
	hash[0] = hash[1] = hash[2] = hash[3] = 0;

	if (ifp->if_flags & IFF_PROMISC) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxcfg |= HME_MAC_RXCFG_PMISC;
	} else if (ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxcfg |= HME_MAC_RXCFG_HENABLE;
		hash[0] = hash[1] = hash[2] = hash[3] = 0xffff;
	} else {
		rxcfg |= HME_MAC_RXCFG_HENABLE;

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_le(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26; 

			/* Set the corresponding bit in the filter. */
			hash[crc >> 4] |= 1 << (crc & 0xf);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	/* Now load the hash table into the chip */
	bus_space_write_4(t, mac, HME_MACI_HASHTAB0, hash[0]);
	bus_space_write_4(t, mac, HME_MACI_HASHTAB1, hash[1]);
	bus_space_write_4(t, mac, HME_MACI_HASHTAB2, hash[2]);
	bus_space_write_4(t, mac, HME_MACI_HASHTAB3, hash[3]);
	bus_space_write_4(t, mac, HME_MACI_RXCFG, rxcfg);
}

void
hme_fill_rx_ring(struct hme_softc *sc)
{
	struct hme_sxd *sd;
	u_int slots;

	for (slots = if_rxr_get(&sc->sc_rx_ring, HME_RX_RING_SIZE);
	    slots > 0; slots--) {
		if (hme_newbuf(sc, &sc->sc_rxd[sc->sc_rx_prod]))
			break;

		sd = &sc->sc_rxd[sc->sc_rx_prod];
		HME_XD_SETADDR(sc->sc_pci, sc->sc_rb.rb_rxd, sc->sc_rx_prod,
		    sd->sd_map->dm_segs[0].ds_addr);
		HME_XD_SETFLAGS(sc->sc_pci, sc->sc_rb.rb_rxd, sc->sc_rx_prod,
		    HME_XD_OWN | HME_XD_ENCODE_RSIZE(HME_RX_PKTSIZE));

		if (++sc->sc_rx_prod == HME_RX_RING_SIZE)
			sc->sc_rx_prod = 0;
        }
	if_rxr_put(&sc->sc_rx_ring, slots);
}

int
hme_newbuf(struct hme_softc *sc, struct hme_sxd *d)
{
	struct mbuf *m;
	bus_dmamap_t map;

	/*
	 * All operations should be on local variables and/or rx spare map
	 * until we're sure everything is a success.
	 */

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (!m)
		return (ENOBUFS);

	if (bus_dmamap_load(sc->sc_dmatag, sc->sc_rxmap_spare,
	    mtod(m, caddr_t), MCLBYTES - HME_RX_OFFSET, NULL,
	    BUS_DMA_NOWAIT) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}

	/*
	 * At this point we have a new buffer loaded into the spare map.
	 * Just need to clear out the old mbuf/map and put the new one
	 * in place.
	 */

	map = d->sd_map;
	d->sd_map = sc->sc_rxmap_spare;
	sc->sc_rxmap_spare = map;

	bus_dmamap_sync(sc->sc_dmatag, d->sd_map, 0, d->sd_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	m->m_data += HME_RX_OFFSET;
	d->sd_mbuf = m;
	return (0);
}
