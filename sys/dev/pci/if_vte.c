/*	$OpenBSD: if_vte.c,v 1.28 2024/05/24 06:02:57 jsg Exp $	*/
/*-
 * Copyright (c) 2010, Pyun YongHyeon <yongari@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

/* Driver for DM&P Electronics, Inc, Vortex86 RDC R6040 FastEthernet. */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/miivar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/if_vtereg.h>

int	vte_match(struct device *, void *, void *);
void	vte_attach(struct device *, struct device *, void *);
int	vte_detach(struct device *, int);

int	vte_miibus_readreg(struct device *, int, int);
void	vte_miibus_writereg(struct device *, int, int, int);
void	vte_miibus_statchg(struct device *);

int	vte_init(struct ifnet *);
void	vte_start(struct ifnet *);
int	vte_ioctl(struct ifnet *, u_long, caddr_t);
void	vte_watchdog(struct ifnet *);
int	vte_mediachange(struct ifnet *);
void	vte_mediastatus(struct ifnet *, struct ifmediareq *);

int	vte_intr(void *);
int	vte_dma_alloc(struct vte_softc *);
void	vte_dma_free(struct vte_softc *);
struct vte_txdesc *
	    vte_encap(struct vte_softc *, struct mbuf **);
void	vte_get_macaddr(struct vte_softc *);
int	vte_init_rx_ring(struct vte_softc *);
int	vte_init_tx_ring(struct vte_softc *);
void	vte_mac_config(struct vte_softc *);
int	vte_newbuf(struct vte_softc *, struct vte_rxdesc *, int);
void	vte_reset(struct vte_softc *);
void	vte_rxeof(struct vte_softc *);
void	vte_iff(struct vte_softc *);
void	vte_start_mac(struct vte_softc *);
void	vte_stats_clear(struct vte_softc *);
void	vte_stats_update(struct vte_softc *);
void	vte_stop(struct vte_softc *);
void	vte_stop_mac(struct vte_softc *);
void	vte_tick(void *);
void	vte_txeof(struct vte_softc *);

const struct pci_matchid vte_devices[] = {
	{ PCI_VENDOR_RDC, PCI_PRODUCT_RDC_R6040_ETHER }
};

const struct cfattach vte_ca = {
	sizeof(struct vte_softc), vte_match, vte_attach
};

struct cfdriver vte_cd = {
	NULL, "vte", DV_IFNET
};

int vtedebug = 0;
#define	DPRINTF(x)	do { if (vtedebug) printf x; } while (0)

int
vte_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct vte_softc *sc = (struct vte_softc *)dev;
	int i;

	CSR_WRITE_2(sc, VTE_MMDIO, MMDIO_READ |
	    (phy << MMDIO_PHY_ADDR_SHIFT) | (reg << MMDIO_REG_ADDR_SHIFT));
	for (i = VTE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		if ((CSR_READ_2(sc, VTE_MMDIO) & MMDIO_READ) == 0)
			break;
	}

	if (i == 0) {
		printf("%s: phy read timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
		return (0);
	}

	return (CSR_READ_2(sc, VTE_MMRD));
}

void
vte_miibus_writereg(struct device *dev, int phy, int reg, int val)
{
	struct vte_softc *sc = (struct vte_softc *)dev;
	int i;

	CSR_WRITE_2(sc, VTE_MMWD, val);
	CSR_WRITE_2(sc, VTE_MMDIO, MMDIO_WRITE |
	    (phy << MMDIO_PHY_ADDR_SHIFT) | (reg << MMDIO_REG_ADDR_SHIFT));
	for (i = VTE_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		if ((CSR_READ_2(sc, VTE_MMDIO) & MMDIO_WRITE) == 0)
			break;
	}

	if (i == 0)
		printf("%s: phy write timeout: phy %d, reg %d\n",
		    sc->sc_dev.dv_xname, phy, reg);
}

void
vte_miibus_statchg(struct device *dev)
{
	struct vte_softc *sc = (struct vte_softc *)dev;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mii_data *mii;
	uint16_t val;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	mii = &sc->sc_miibus;

	sc->vte_flags &= ~VTE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->vte_flags |= VTE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* Stop RX/TX MACs. */
	vte_stop_mac(sc);
	/* Program MACs with resolved duplex and flow control. */
	if ((sc->vte_flags & VTE_FLAG_LINK) != 0) {
		/*
		 * Timer waiting time : (63 + TIMER * 64) MII clock.
		 * MII clock : 25MHz(100Mbps) or 2.5MHz(10Mbps).
		 */
		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
			val = 18 << VTE_IM_TIMER_SHIFT;
		else
			val = 1 << VTE_IM_TIMER_SHIFT;
		sc->vte_int_rx_mod = VTE_IM_RX_BUNDLE_DEFAULT;
		val |= sc->vte_int_rx_mod << VTE_IM_BUNDLE_SHIFT;
		/* 48.6us for 100Mbps, 50.8us for 10Mbps */
		CSR_WRITE_2(sc, VTE_MRICR, val);

		if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
			val = 18 << VTE_IM_TIMER_SHIFT;
		else
			val = 1 << VTE_IM_TIMER_SHIFT;
		sc->vte_int_tx_mod = VTE_IM_TX_BUNDLE_DEFAULT;
		val |= sc->vte_int_tx_mod << VTE_IM_BUNDLE_SHIFT;
		/* 48.6us for 100Mbps, 50.8us for 10Mbps */
		CSR_WRITE_2(sc, VTE_MTICR, val);

		vte_mac_config(sc);
		vte_start_mac(sc);
	}
}

void
vte_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vte_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

int
vte_mediachange(struct ifnet *ifp)
{
	struct vte_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	int error;

	if (mii->mii_instance != 0) {
		struct mii_softc *miisc;

		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	error = mii_mediachg(mii);

	return (error);
}

int
vte_match(struct device *dev, void *match, void *aux)
{
	return pci_matchbyid((struct pci_attach_args *)aux, vte_devices,
	    sizeof(vte_devices) / sizeof(vte_devices[0]));
}

void
vte_get_macaddr(struct vte_softc *sc)
{
	uint16_t mid;

	/*
	 * It seems there is no way to reload station address and
	 * it is supposed to be set by BIOS.
	 */
	mid = CSR_READ_2(sc, VTE_MID0L);
	sc->vte_eaddr[0] = (mid >> 0) & 0xFF;
	sc->vte_eaddr[1] = (mid >> 8) & 0xFF;
	mid = CSR_READ_2(sc, VTE_MID0M);
	sc->vte_eaddr[2] = (mid >> 0) & 0xFF;
	sc->vte_eaddr[3] = (mid >> 8) & 0xFF;
	mid = CSR_READ_2(sc, VTE_MID0H);
	sc->vte_eaddr[4] = (mid >> 0) & 0xFF;
	sc->vte_eaddr[5] = (mid >> 8) & 0xFF;
}

void
vte_attach(struct device *parent, struct device *self, void *aux)
{
	struct vte_softc *sc = (struct vte_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr;
	struct ifnet *ifp;
	pcireg_t memtype;
	int error = 0;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, VTE_PCI_LOMEM);
	if (pci_mapreg_map(pa, VTE_PCI_LOMEM, memtype, 0, &sc->sc_mem_bt,
	    &sc->sc_mem_bh, NULL, &sc->sc_mem_size, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		goto fail;
	}

  	/*
	 * Allocate IRQ
	 */
	intrstr = pci_intr_string(pc, ih);
	sc->sc_irq_handle = pci_intr_establish(pc, ih, IPL_NET, vte_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_irq_handle == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;
	sc->sc_pct = pa->pa_pc;
	sc->sc_pcitag = pa->pa_tag;

	/* Reset the ethernet controller. */
	vte_reset(sc);

	error = vte_dma_alloc(sc);
	if (error)
		goto fail;

	/* Load station address. */
	vte_get_macaddr(sc);

	ifp = &sc->sc_arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = vte_ioctl;
	ifp->if_start = vte_start;
	ifp->if_watchdog = vte_watchdog;
	ifq_init_maxlen(&ifp->if_snd, VTE_TX_RING_CNT - 1);
	bcopy(sc->vte_eaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/*
	 * Set up MII bus.
	 * BIOS would have initialized VTE_MPSCCR to catch PHY
	 * status changes so driver may be able to extract
	 * configured PHY address.  Since it's common to see BIOS
	 * fails to initialize the register(including the sample
	 * board I have), let mii(4) probe it.  This is more
	 * reliable than relying on BIOS's initialization.
	 *
	 * Advertising flow control capability to mii(4) was
	 * intentionally disabled due to severe problems in TX
	 * pause frame generation.  See vte_rxeof() for more
	 * details.
	 */
	sc->sc_miibus.mii_ifp = ifp;
	sc->sc_miibus.mii_readreg = vte_miibus_readreg;
	sc->sc_miibus.mii_writereg = vte_miibus_writereg;
	sc->sc_miibus.mii_statchg = vte_miibus_statchg;
	
	ifmedia_init(&sc->sc_miibus.mii_media, 0, vte_mediachange,
	    vte_mediastatus);
	mii_attach(self, &sc->sc_miibus, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);

	if (LIST_FIRST(&sc->sc_miibus.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL,
		    0, NULL);
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_miibus.mii_media, IFM_ETHER | IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	timeout_set(&sc->vte_tick_ch, vte_tick, sc);
	return;
fail:
	vte_detach(&sc->sc_dev, 0);
}

int
vte_detach(struct device *self, int flags)
{
	struct vte_softc *sc = (struct vte_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();
	vte_stop(sc);
	splx(s);

	mii_detach(&sc->sc_miibus, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_miibus.mii_media, IFM_INST_ANY);
	
	ether_ifdetach(ifp);
	if_detach(ifp);
	vte_dma_free(sc);

	if (sc->sc_irq_handle != NULL) {
		pci_intr_disestablish(sc->sc_pct, sc->sc_irq_handle);
		sc->sc_irq_handle = NULL;
	}

	return (0);
}

int
vte_dma_alloc(struct vte_softc *sc)
{
	struct vte_txdesc *txd;
	struct vte_rxdesc *rxd;
	int error, i, nsegs;

	/* Create DMA stuffs for TX ring */
	error = bus_dmamap_create(sc->sc_dmat, VTE_TX_RING_SZ, 1,
	    VTE_TX_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->vte_cdata.vte_tx_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for TX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, VTE_TX_RING_SZ, ETHER_ALIGN,
	    0, &sc->vte_cdata.vte_tx_ring_seg, 1, &nsegs, 
	    BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->vte_cdata.vte_tx_ring_seg,
	    nsegs, VTE_TX_RING_SZ, (caddr_t *)&sc->vte_cdata.vte_tx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/*  Load the DMA map for Tx ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->vte_cdata.vte_tx_ring_map,
	    sc->vte_cdata.vte_tx_ring, VTE_TX_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Tx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)&sc->vte_cdata.vte_tx_ring, 1);
		return (error);
	}

	sc->vte_cdata.vte_tx_ring_paddr = 
	    sc->vte_cdata.vte_tx_ring_map->dm_segs[0].ds_addr;

	/* Create DMA stuffs for RX ring */
	error = bus_dmamap_create(sc->sc_dmat, VTE_RX_RING_SZ, 1,
	    VTE_RX_RING_SZ, 0, BUS_DMA_NOWAIT, &sc->vte_cdata.vte_rx_ring_map);
	if (error)
		return (ENOBUFS);

	/* Allocate DMA'able memory for RX ring */
	error = bus_dmamem_alloc(sc->sc_dmat, VTE_RX_RING_SZ, ETHER_ALIGN,
	    0, &sc->vte_cdata.vte_rx_ring_seg, 1, &nsegs,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO);
	if (error) {
		printf("%s: could not allocate DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	error = bus_dmamem_map(sc->sc_dmat, &sc->vte_cdata.vte_rx_ring_seg,
	    nsegs, VTE_RX_RING_SZ, (caddr_t *)&sc->vte_cdata.vte_rx_ring,
	    BUS_DMA_NOWAIT);
	if (error)
		return (ENOBUFS);

	/* Load the DMA map for Rx ring. */
	error = bus_dmamap_load(sc->sc_dmat, sc->vte_cdata.vte_rx_ring_map,
	    sc->vte_cdata.vte_rx_ring, VTE_RX_RING_SZ, NULL, BUS_DMA_WAITOK);
	if (error) {
		printf("%s: could not load DMA'able memory for Rx ring.\n",
		    sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->vte_cdata.vte_rx_ring, 1);
		return (error);
	}

	sc->vte_cdata.vte_rx_ring_paddr =
	    sc->vte_cdata.vte_rx_ring_map->dm_segs[0].ds_addr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		txd = &sc->vte_cdata.vte_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &txd->tx_dmamap);
		if (error) {
			printf("%s: could not create Tx dmamap.\n",
			    sc->sc_dev.dv_xname);
			return (error);
		}
	}

	/* Create DMA maps for Rx buffers. */
	error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
	    BUS_DMA_NOWAIT, &sc->vte_cdata.vte_rx_sparemap);
	if (error) {
		printf("%s: could not create spare Rx dmamap.\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}
	for (i = 0; i < VTE_RX_RING_CNT; i++) {
		rxd = &sc->vte_cdata.vte_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &rxd->rx_dmamap);
		if (error) {
			printf("%s: could not create Rx dmamap.\n",
			    sc->sc_dev.dv_xname);
			return (error);
		}
	}

	return (0);
}

void
vte_dma_free(struct vte_softc *sc)
{
	struct vte_txdesc *txd;
	struct vte_rxdesc *rxd;
	int i;

	/* TX buffers. */
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		txd = &sc->vte_cdata.vte_txdesc[i];
		if (txd->tx_dmamap != NULL) {
			bus_dmamap_destroy(sc->sc_dmat, txd->tx_dmamap);
			txd->tx_dmamap = NULL;
		}
	}
	/* Rx buffers */
	for (i = 0; i < VTE_RX_RING_CNT; i++) {
		rxd = &sc->vte_cdata.vte_rxdesc[i];
		if (rxd->rx_dmamap != NULL) {
			bus_dmamap_destroy(sc->sc_dmat, rxd->rx_dmamap);
			rxd->rx_dmamap = NULL;
		}
	}
	if (sc->vte_cdata.vte_rx_sparemap != NULL) {
		bus_dmamap_destroy(sc->sc_dmat, sc->vte_cdata.vte_rx_sparemap);
		sc->vte_cdata.vte_rx_sparemap = NULL;
	}
	/* TX descriptor ring. */
	if (sc->vte_cdata.vte_tx_ring_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->vte_cdata.vte_tx_ring_map);
	if (sc->vte_cdata.vte_tx_ring_map != NULL &&
	    sc->vte_cdata.vte_tx_ring != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->vte_cdata.vte_tx_ring, 1);
	sc->vte_cdata.vte_tx_ring = NULL;
	sc->vte_cdata.vte_tx_ring_map = NULL;
	/* RX ring. */
	if (sc->vte_cdata.vte_rx_ring_map != NULL)
		bus_dmamap_unload(sc->sc_dmat, sc->vte_cdata.vte_rx_ring_map);
	if (sc->vte_cdata.vte_rx_ring_map != NULL &&
	    sc->vte_cdata.vte_rx_ring != NULL)
		bus_dmamem_free(sc->sc_dmat,
		    (bus_dma_segment_t *)sc->vte_cdata.vte_rx_ring, 1);
	sc->vte_cdata.vte_rx_ring = NULL;
	sc->vte_cdata.vte_rx_ring_map = NULL;
}

struct vte_txdesc *
vte_encap(struct vte_softc *sc, struct mbuf **m_head)
{
	struct vte_txdesc *txd;
	struct mbuf *m, *n;
	int copy, error, padlen;

	txd = &sc->vte_cdata.vte_txdesc[sc->vte_cdata.vte_tx_prod];
	m = *m_head;
	/*
	 * Controller doesn't auto-pad, so we have to make sure pad
	 * short frames out to the minimum frame length.
	 */
	if (m->m_pkthdr.len < VTE_MIN_FRAMELEN)
		padlen = VTE_MIN_FRAMELEN - m->m_pkthdr.len;
	else
		padlen = 0;

	/*
	 * Controller does not support multi-fragmented TX buffers.
	 * Controller spends most of its TX processing time in
	 * de-fragmenting TX buffers.  Either faster CPU or more
	 * advanced controller DMA engine is required to speed up
	 * TX path processing.
	 * To mitigate the de-fragmenting issue, perform deep copy
	 * from fragmented mbuf chains to a pre-allocated mbuf
	 * cluster with extra cost of kernel memory.  For frames
	 * that is composed of single TX buffer, the deep copy is
	 * bypassed.
	 */
	copy = 0;
	if (m->m_next != NULL)
		copy++;
	if (padlen > 0 && (padlen > m_trailingspace(m)))
		copy++;
	if (copy != 0) {
		/* Avoid expensive m_defrag(9) and do deep copy. */
		n = sc->vte_cdata.vte_txmbufs[sc->vte_cdata.vte_tx_prod];
		m_copydata(m, 0, m->m_pkthdr.len, mtod(n, char *));
		n->m_pkthdr.len = m->m_pkthdr.len;
		n->m_len = m->m_pkthdr.len;
		m = n;
		txd->tx_flags |= VTE_TXMBUF;
	}

	if (padlen > 0) {
		/* Zero out the bytes in the pad area. */
		bzero(mtod(m, char *) + m->m_pkthdr.len, padlen);
		m->m_pkthdr.len += padlen;
		m->m_len = m->m_pkthdr.len;
	}

	error = bus_dmamap_load_mbuf(sc->sc_dmat, txd->tx_dmamap, m, 
	    BUS_DMA_NOWAIT);

	if (error != 0) {
		txd->tx_flags &= ~VTE_TXMBUF;
		return (NULL);
	}

	bus_dmamap_sync(sc->sc_dmat, txd->tx_dmamap, 0, 
	    txd->tx_dmamap->dm_mapsize, BUS_DMASYNC_PREWRITE);

	txd->tx_desc->dtlen = 
	    htole16(VTE_TX_LEN(txd->tx_dmamap->dm_segs[0].ds_len));
	txd->tx_desc->dtbp = htole32(txd->tx_dmamap->dm_segs[0].ds_addr);
	sc->vte_cdata.vte_tx_cnt++;
	/* Update producer index. */
	VTE_DESC_INC(sc->vte_cdata.vte_tx_prod, VTE_TX_RING_CNT);

	/* Finally hand over ownership to controller. */
	txd->tx_desc->dtst = htole16(VTE_DTST_TX_OWN);
	txd->tx_m = m;

	return (txd);
}

void
vte_start(struct ifnet *ifp)
{
	struct vte_softc *sc = ifp->if_softc;
	struct vte_txdesc *txd;
	struct mbuf *m_head;
	int enq = 0;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

	for (;;) {
		/* Reserve one free TX descriptor. */
		if (sc->vte_cdata.vte_tx_cnt >= VTE_TX_RING_CNT - 1) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if ((txd = vte_encap(sc, &m_head)) == NULL) {
			break;
		}

		enq++;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf != NULL)
			bpf_mtap_ether(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif
		/* Free consumed TX frame. */
		if ((txd->tx_flags & VTE_TXMBUF) != 0)
			m_freem(m_head);
	}

	if (enq > 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->vte_cdata.vte_tx_ring_map, 0,
		    sc->vte_cdata.vte_tx_ring_map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);
		CSR_WRITE_2(sc, VTE_TX_POLL, TX_POLL_START);
		ifp->if_timer = VTE_TX_TIMEOUT;
	}
}

void
vte_watchdog(struct ifnet *ifp)
{
	struct vte_softc *sc = ifp->if_softc;

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;
	vte_init(ifp);

	if (!ifq_empty(&ifp->if_snd))
		vte_start(ifp);
}

int
vte_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vte_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_miibus;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			vte_init(ifp);
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				vte_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vte_stop(sc);
		}
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			vte_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
vte_mac_config(struct vte_softc *sc)
{
	struct mii_data *mii;
	uint16_t mcr;

	mii = &sc->sc_miibus;
	mcr = CSR_READ_2(sc, VTE_MCR0);
	mcr &= ~(MCR0_FC_ENB | MCR0_FULL_DUPLEX);
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		mcr |= MCR0_FULL_DUPLEX;
#ifdef notyet
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			mcr |= MCR0_FC_ENB;
		/*
		 * The data sheet is not clear whether the controller
		 * honors received pause frames or not.  The is no
		 * separate control bit for RX pause frame so just
		 * enable MCR0_FC_ENB bit.
		 */
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			mcr |= MCR0_FC_ENB;
#endif
	}
	CSR_WRITE_2(sc, VTE_MCR0, mcr);
}

void
vte_stats_clear(struct vte_softc *sc)
{

	/* Reading counter registers clears its contents. */
	CSR_READ_2(sc, VTE_CNT_RX_DONE);
	CSR_READ_2(sc, VTE_CNT_MECNT0);
	CSR_READ_2(sc, VTE_CNT_MECNT1);
	CSR_READ_2(sc, VTE_CNT_MECNT2);
	CSR_READ_2(sc, VTE_CNT_MECNT3);
	CSR_READ_2(sc, VTE_CNT_TX_DONE);
	CSR_READ_2(sc, VTE_CNT_MECNT4);
	CSR_READ_2(sc, VTE_CNT_PAUSE);
}

void
vte_stats_update(struct vte_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct vte_hw_stats *stat;
	uint16_t value;

	stat = &sc->vte_stats;

	CSR_READ_2(sc, VTE_MECISR);
	/* RX stats. */
	stat->rx_frames += CSR_READ_2(sc, VTE_CNT_RX_DONE);
	value = CSR_READ_2(sc, VTE_CNT_MECNT0);
	stat->rx_bcast_frames += (value >> 8);
	stat->rx_mcast_frames += (value & 0xFF);
	value = CSR_READ_2(sc, VTE_CNT_MECNT1);
	stat->rx_runts += (value >> 8);
	stat->rx_crcerrs += (value & 0xFF);
	value = CSR_READ_2(sc, VTE_CNT_MECNT2);
	stat->rx_long_frames += (value & 0xFF);
	value = CSR_READ_2(sc, VTE_CNT_MECNT3);
	stat->rx_fifo_full += (value >> 8);
	stat->rx_desc_unavail += (value & 0xFF);

	/* TX stats. */
	stat->tx_frames += CSR_READ_2(sc, VTE_CNT_TX_DONE);
	value = CSR_READ_2(sc, VTE_CNT_MECNT4);
	stat->tx_underruns += (value >> 8);
	stat->tx_late_colls += (value & 0xFF);

	value = CSR_READ_2(sc, VTE_CNT_PAUSE);
	stat->tx_pause_frames += (value >> 8);
	stat->rx_pause_frames += (value & 0xFF);

	/* Update ifp counters. */
	ifp->if_collisions = stat->tx_late_colls;
	ifp->if_oerrors = stat->tx_late_colls + stat->tx_underruns;
	ifp->if_ierrors = stat->rx_crcerrs + stat->rx_runts +
	    stat->rx_long_frames + stat->rx_fifo_full;
}

int
vte_intr(void *arg)
{
	struct vte_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	uint16_t status;
	int n;
	int claimed = 0;

	/* Reading VTE_MISR acknowledges interrupts. */
	status = CSR_READ_2(sc, VTE_MISR);
	if ((status & VTE_INTRS) == 0)
		return (0);

	/* Disable interrupts. */
	CSR_WRITE_2(sc, VTE_MIER, 0);
	for (n = 8; (status & VTE_INTRS) != 0;) {
		if ((ifp->if_flags & IFF_RUNNING) == 0)
			break;
		claimed = 1;
		if (status & (MISR_RX_DONE | MISR_RX_DESC_UNAVAIL |
		    MISR_RX_FIFO_FULL))
			vte_rxeof(sc);
		if (status & MISR_TX_DONE)
			vte_txeof(sc);
		if (status & MISR_EVENT_CNT_OFLOW)
			vte_stats_update(sc);
		if (!ifq_empty(&ifp->if_snd))
			vte_start(ifp);
		if (--n > 0)
			status = CSR_READ_2(sc, VTE_MISR);
		else
			break;
	}

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, VTE_MIER, VTE_INTRS);

	return (claimed);
}

void
vte_txeof(struct vte_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct vte_txdesc *txd;
	uint16_t status;
	int cons, prog;

	if (sc->vte_cdata.vte_tx_cnt == 0)
		return;
	bus_dmamap_sync(sc->sc_dmat, sc->vte_cdata.vte_tx_ring_map, 0,
	    sc->vte_cdata.vte_tx_ring_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
	cons = sc->vte_cdata.vte_tx_cons;
	/*
	 * Go through our TX list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (prog = 0; sc->vte_cdata.vte_tx_cnt > 0; prog++) {
		txd = &sc->vte_cdata.vte_txdesc[cons];
		status = letoh16(txd->tx_desc->dtst);
		if (status & VTE_DTST_TX_OWN)
			break;
		sc->vte_cdata.vte_tx_cnt--;
		/* Reclaim transmitted mbufs. */
		bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
		if ((txd->tx_flags & VTE_TXMBUF) == 0)
			m_freem(txd->tx_m);
		txd->tx_flags &= ~VTE_TXMBUF;
		txd->tx_m = NULL;
		prog++;
		VTE_DESC_INC(cons, VTE_TX_RING_CNT);
	}

	if (prog > 0) {
		ifq_clr_oactive(&ifp->if_snd);
		sc->vte_cdata.vte_tx_cons = cons;
		/*
		 * Unarm watchdog timer only when there is no pending
		 * frames in TX queue.
		 */
		if (sc->vte_cdata.vte_tx_cnt == 0)
			ifp->if_timer = 0;
	}
}

int
vte_newbuf(struct vte_softc *sc, struct vte_rxdesc *rxd, int init)
{
	struct mbuf *m;
	bus_dmamap_t map;
	int error;

	MGETHDR(m, init ? M_WAITOK : M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return (ENOBUFS);
	MCLGET(m, init ? M_WAITOK : M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		m_freem(m);
		return (ENOBUFS);
	}
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, sizeof(uint32_t));

	error = bus_dmamap_load_mbuf(sc->sc_dmat,
	    sc->vte_cdata.vte_rx_sparemap, m, BUS_DMA_NOWAIT);

	if (error != 0) {
		if (!error) {
			bus_dmamap_unload(sc->sc_dmat,
			    sc->vte_cdata.vte_rx_sparemap);
			error = EFBIG;
			printf("%s: too many segments?!\n", 
			    sc->sc_dev.dv_xname);
		}
		m_freem(m);

		if (init)
			printf("%s: can't load RX mbuf\n", sc->sc_dev.dv_xname);
		return (error);
	}

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->sc_dmat, rxd->rx_dmamap, 0,
		    rxd->rx_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->vte_cdata.vte_rx_sparemap;
	sc->vte_cdata.vte_rx_sparemap = map;

	rxd->rx_m = m;
	rxd->rx_desc->drbp = htole32(rxd->rx_dmamap->dm_segs[0].ds_addr);
	rxd->rx_desc->drlen = 
	    htole16(VTE_RX_LEN(rxd->rx_dmamap->dm_segs[0].ds_len));
	rxd->rx_desc->drst = htole16(VTE_DRST_RX_OWN);

	return (0);
}

void
vte_rxeof(struct vte_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct vte_rxdesc *rxd;
	struct mbuf *m;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	uint16_t status, total_len;
	int cons, prog;

	bus_dmamap_sync(sc->sc_dmat, sc->vte_cdata.vte_rx_ring_map, 0,
	    sc->vte_cdata.vte_rx_ring_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
	cons = sc->vte_cdata.vte_rx_cons;
	for (prog = 0; (ifp->if_flags & IFF_RUNNING) != 0; prog++,
	    VTE_DESC_INC(cons, VTE_RX_RING_CNT)) {
		rxd = &sc->vte_cdata.vte_rxdesc[cons];
		status = letoh16(rxd->rx_desc->drst);
		if (status & VTE_DRST_RX_OWN)
			break;
		total_len = VTE_RX_LEN(letoh16(rxd->rx_desc->drlen));
		m = rxd->rx_m;
		if ((status & VTE_DRST_RX_OK) == 0) {
			/* Discard errored frame. */
			rxd->rx_desc->drlen =
			    htole16(MCLBYTES - sizeof(uint32_t));
			rxd->rx_desc->drst = htole16(VTE_DRST_RX_OWN);
			continue;
		}
		if (vte_newbuf(sc, rxd, 0) != 0) {
			ifp->if_iqdrops++;
			rxd->rx_desc->drlen =
			    htole16(MCLBYTES - sizeof(uint32_t));
			rxd->rx_desc->drst = htole16(VTE_DRST_RX_OWN);
			continue;
		}

		/*
		 * It seems there is no way to strip FCS bytes.
		 */
		m->m_pkthdr.len = m->m_len = total_len - ETHER_CRC_LEN;
		ml_enqueue(&ml, m);
	}

	if_input(ifp, &ml);

	if (prog > 0) {
		/* Update the consumer index. */
		sc->vte_cdata.vte_rx_cons = cons;
		/*
		 * Sync updated RX descriptors such that controller see
		 * modified RX buffer addresses.
		 */
		bus_dmamap_sync(sc->sc_dmat, sc->vte_cdata.vte_rx_ring_map, 0,
		    sc->vte_cdata.vte_rx_ring_map->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);
#ifdef notyet
		/*
		 * Update residue counter.  Controller does not
		 * keep track of number of available RX descriptors
		 * such that driver should have to update VTE_MRDCR
		 * to make controller know how many free RX
		 * descriptors were added to controller.  This is
		 * a similar mechanism used in VIA velocity
		 * controllers and it indicates controller just
		 * polls OWN bit of current RX descriptor pointer.
		 * A couple of severe issues were seen on sample
		 * board where the controller continuously emits TX
		 * pause frames once RX pause threshold crossed.
		 * Once triggered it never recovered form that
		 * state, I couldn't find a way to make it back to
		 * work at least.  This issue effectively
		 * disconnected the system from network.  Also, the
		 * controller used 00:00:00:00:00:00 as source
		 * station address of TX pause frame. Probably this
		 * is one of reason why vendor recommends not to
		 * enable flow control on R6040 controller.
		 */
		CSR_WRITE_2(sc, VTE_MRDCR, prog |
		    (((VTE_RX_RING_CNT * 2) / 10) <<
		    VTE_MRDCR_RX_PAUSE_THRESH_SHIFT));
#endif
	}
}

void
vte_tick(void *arg)
{
	struct vte_softc *sc = arg;
	struct mii_data *mii = &sc->sc_miibus;
	int s;

	s = splnet();
	mii_tick(mii);
	vte_stats_update(sc);
	timeout_add_sec(&sc->vte_tick_ch, 1);
	splx(s);
}

void
vte_reset(struct vte_softc *sc)
{
	uint16_t mcr, mdcsc;
	int i;

	mdcsc = CSR_READ_2(sc, VTE_MDCSC);
	mcr = CSR_READ_2(sc, VTE_MCR1);
	CSR_WRITE_2(sc, VTE_MCR1, mcr | MCR1_MAC_RESET);
	for (i = VTE_RESET_TIMEOUT; i > 0; i--) {
		DELAY(10);
		if ((CSR_READ_2(sc, VTE_MCR1) & MCR1_MAC_RESET) == 0)
			break;
	}
	if (i == 0)
		printf("%s: reset timeout(0x%04x)!\n", sc->sc_dev.dv_xname,
		    mcr);
	/*
	 * Follow the guide of vendor recommended way to reset MAC.
	 * Vendor confirms relying on MCR1_MAC_RESET of VTE_MCR1 is
	 * not reliable so manually reset internal state machine.
	 */
	CSR_WRITE_2(sc, VTE_MACSM, 0x0002);
	CSR_WRITE_2(sc, VTE_MACSM, 0);
	DELAY(5000);

	/*
	 * On some SoCs (like Vortex86DX3) MDC speed control register value
	 * needs to be restored to original value instead of default one,
	 * otherwise some PHY registers may fail to be read.
	 */
	if (mdcsc != MDCSC_DEFAULT)
		CSR_WRITE_2(sc, VTE_MDCSC, mdcsc);
}

int
vte_init(struct ifnet *ifp)
{
	struct vte_softc *sc = ifp->if_softc;
	bus_addr_t paddr;
	uint8_t *eaddr;
	int error;

	/*
	 * Cancel any pending I/O.
	 */
	vte_stop(sc);
	/*
	 * Reset the chip to a known state.
	 */
	vte_reset(sc);

	/* Initialize RX descriptors. */
	error = vte_init_rx_ring(sc);
	if (error != 0) {
		printf("%s: no memory for Rx buffers.\n", sc->sc_dev.dv_xname);
		vte_stop(sc);
		return (error);
	}
	error = vte_init_tx_ring(sc);
	if (error != 0) {
		printf("%s: no memory for Tx buffers.\n", sc->sc_dev.dv_xname);
		vte_stop(sc);
		return (error);
	}

	/*
	 * Reprogram the station address.  Controller supports up
	 * to 4 different station addresses so driver programs the
	 * first station address as its own ethernet address and
	 * configure the remaining three addresses as perfect
	 * multicast addresses.
	 */
	eaddr = LLADDR(ifp->if_sadl);
	CSR_WRITE_2(sc, VTE_MID0L, eaddr[1] << 8 | eaddr[0]);
	CSR_WRITE_2(sc, VTE_MID0M, eaddr[3] << 8 | eaddr[2]);
	CSR_WRITE_2(sc, VTE_MID0H, eaddr[5] << 8 | eaddr[4]);

	/* Set TX descriptor base addresses. */
	paddr = sc->vte_cdata.vte_tx_ring_paddr;
	CSR_WRITE_2(sc, VTE_MTDSA1, paddr >> 16);
	CSR_WRITE_2(sc, VTE_MTDSA0, paddr & 0xFFFF);
	/* Set RX descriptor base addresses. */
	paddr = sc->vte_cdata.vte_rx_ring_paddr;
	CSR_WRITE_2(sc, VTE_MRDSA1, paddr >> 16);
	CSR_WRITE_2(sc, VTE_MRDSA0, paddr & 0xFFFF);
	/*
	 * Initialize RX descriptor residue counter and set RX
	 * pause threshold to 20% of available RX descriptors.
	 * See comments on vte_rxeof() for details on flow control
	 * issues.
	 */
	CSR_WRITE_2(sc, VTE_MRDCR, (VTE_RX_RING_CNT & VTE_MRDCR_RESIDUE_MASK) |
	    (((VTE_RX_RING_CNT * 2) / 10) << VTE_MRDCR_RX_PAUSE_THRESH_SHIFT));

	/*
	 * Always use maximum frame size that controller can
	 * support.  Otherwise received frames that has longer
	 * frame length than vte(4) MTU would be silently dropped
	 * in controller.  This would break path-MTU discovery as
	 * sender wouldn't get any responses from receiver. The
	 * RX buffer size should be multiple of 4.
	 * Note, jumbo frames are silently ignored by controller
	 * and even MAC counters do not detect them.
	 */
	CSR_WRITE_2(sc, VTE_MRBSR, VTE_RX_BUF_SIZE_MAX);

	/* Configure FIFO. */
	CSR_WRITE_2(sc, VTE_MBCR, MBCR_FIFO_XFER_LENGTH_16 |
	    MBCR_TX_FIFO_THRESH_64 | MBCR_RX_FIFO_THRESH_16 |
	    MBCR_SDRAM_BUS_REQ_TIMER_DEFAULT);

	/*
	 * Configure TX/RX MACs.  Actual resolved duplex and flow
	 * control configuration is done after detecting a valid
	 * link.  Note, we don't generate early interrupt here
	 * as well since FreeBSD does not have interrupt latency
	 * problems like Windows.
	 */
	CSR_WRITE_2(sc, VTE_MCR0, MCR0_ACCPT_LONG_PKT);
	/*
	 * We manually keep track of PHY status changes to
	 * configure resolved duplex and flow control since only
	 * duplex configuration can be automatically reflected to
	 * MCR0.
	 */
	CSR_WRITE_2(sc, VTE_MCR1, MCR1_PKT_LENGTH_1537 |
	    MCR1_EXCESS_COL_RETRY_16);

	/* Initialize RX filter. */
	vte_iff(sc);

	/* Disable TX/RX interrupt moderation control. */
	CSR_WRITE_2(sc, VTE_MRICR, 0);
	CSR_WRITE_2(sc, VTE_MTICR, 0);

	/* Enable MAC event counter interrupts. */
	CSR_WRITE_2(sc, VTE_MECIER, VTE_MECIER_INTRS);
	/* Clear MAC statistics. */
	vte_stats_clear(sc);

	/* Acknowledge all pending interrupts and clear it. */
	CSR_WRITE_2(sc, VTE_MIER, VTE_INTRS);
	CSR_WRITE_2(sc, VTE_MISR, 0);

	sc->vte_flags &= ~VTE_FLAG_LINK;
	/* Switch to the current media. */
	vte_mediachange(ifp);

	timeout_add_sec(&sc->vte_tick_ch, 1);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	return (0);
}

void
vte_stop(struct vte_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct vte_txdesc *txd;
	struct vte_rxdesc *rxd;
	int i;

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;
	sc->vte_flags &= ~VTE_FLAG_LINK;
	timeout_del(&sc->vte_tick_ch);
	vte_stats_update(sc);
	/* Disable interrupts. */
	CSR_WRITE_2(sc, VTE_MIER, 0);
	CSR_WRITE_2(sc, VTE_MECIER, 0);
	/* Stop RX/TX MACs. */
	vte_stop_mac(sc);
	/* Clear interrupts. */
	CSR_READ_2(sc, VTE_MISR);
	/*
	 * Free TX/RX mbufs still in the queues.
	 */
	for (i = 0; i < VTE_RX_RING_CNT; i++) {
		rxd = &sc->vte_cdata.vte_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		txd = &sc->vte_cdata.vte_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_unload(sc->sc_dmat, txd->tx_dmamap);
			if ((txd->tx_flags & VTE_TXMBUF) == 0)
				m_freem(txd->tx_m);
			txd->tx_m = NULL;
			txd->tx_flags &= ~VTE_TXMBUF;
		}
	}
	/* Free TX mbuf pools used for deep copy. */
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		if (sc->vte_cdata.vte_txmbufs[i] != NULL) {
			m_freem(sc->vte_cdata.vte_txmbufs[i]);
			sc->vte_cdata.vte_txmbufs[i] = NULL;
		}
	}
}

void
vte_start_mac(struct vte_softc *sc)
{
	uint16_t mcr;
	int i;

	/* Enable RX/TX MACs. */
	mcr = CSR_READ_2(sc, VTE_MCR0);
	if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) !=
	    (MCR0_RX_ENB | MCR0_TX_ENB)) {
		mcr |= MCR0_RX_ENB | MCR0_TX_ENB;
		CSR_WRITE_2(sc, VTE_MCR0, mcr);
		for (i = VTE_TIMEOUT; i > 0; i--) {
			mcr = CSR_READ_2(sc, VTE_MCR0);
			if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) ==
			    (MCR0_RX_ENB | MCR0_TX_ENB))
				break;
			DELAY(10);
		}
		if (i == 0)
			printf("%s: could not enable RX/TX MAC(0x%04x)!\n",
			    sc->sc_dev.dv_xname, mcr);
	}
}

void
vte_stop_mac(struct vte_softc *sc)
{
	uint16_t mcr;
	int i;

	/* Disable RX/TX MACs. */
	mcr = CSR_READ_2(sc, VTE_MCR0);
	if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) != 0) {
		mcr &= ~(MCR0_RX_ENB | MCR0_TX_ENB);
		CSR_WRITE_2(sc, VTE_MCR0, mcr);
		for (i = VTE_TIMEOUT; i > 0; i--) {
			mcr = CSR_READ_2(sc, VTE_MCR0);
			if ((mcr & (MCR0_RX_ENB | MCR0_TX_ENB)) == 0)
				break;
			DELAY(10);
		}
		if (i == 0)
			printf("%s: could not disable RX/TX MAC(0x%04x)!\n",
			    sc->sc_dev.dv_xname, mcr);
	}
}

int
vte_init_tx_ring(struct vte_softc *sc)
{
	struct vte_tx_desc *desc;
	struct vte_txdesc *txd;
	bus_addr_t addr;
	int i;

	sc->vte_cdata.vte_tx_prod = 0;
	sc->vte_cdata.vte_tx_cons = 0;
	sc->vte_cdata.vte_tx_cnt = 0;

	/* Pre-allocate TX mbufs for deep copy. */
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		MGETHDR(sc->vte_cdata.vte_txmbufs[i], 
		    M_DONTWAIT, MT_DATA);
		if (sc->vte_cdata.vte_txmbufs[i] == NULL)
			return (ENOBUFS);
		MCLGET(sc->vte_cdata.vte_txmbufs[i], M_DONTWAIT);
		if (!(sc->vte_cdata.vte_txmbufs[i]->m_flags & M_EXT)) {
			m_freem(sc->vte_cdata.vte_txmbufs[i]);
			sc->vte_cdata.vte_txmbufs[i] = NULL;
			return (ENOBUFS);
		}
		sc->vte_cdata.vte_txmbufs[i]->m_pkthdr.len = MCLBYTES;
		sc->vte_cdata.vte_txmbufs[i]->m_len = MCLBYTES;
	}
	desc = sc->vte_cdata.vte_tx_ring;
	bzero(desc, VTE_TX_RING_SZ);
	for (i = 0; i < VTE_TX_RING_CNT; i++) {
		txd = &sc->vte_cdata.vte_txdesc[i];
		txd->tx_m = NULL;
		if (i != VTE_TX_RING_CNT - 1)
			addr = sc->vte_cdata.vte_tx_ring_paddr +
			    sizeof(struct vte_tx_desc) * (i + 1);
		else
			addr = sc->vte_cdata.vte_tx_ring_paddr +
			    sizeof(struct vte_tx_desc) * 0;
		desc = &sc->vte_cdata.vte_tx_ring[i];
		desc->dtnp = htole32(addr);
		txd->tx_desc = desc;
	}

	bus_dmamap_sync(sc->sc_dmat, sc->vte_cdata.vte_tx_ring_map, 0,
	    sc->vte_cdata.vte_tx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);
	return (0);
}

int
vte_init_rx_ring(struct vte_softc *sc)
{
	struct vte_rx_desc *desc;
	struct vte_rxdesc *rxd;
	bus_addr_t addr;
	int i;

	sc->vte_cdata.vte_rx_cons = 0;
	desc = sc->vte_cdata.vte_rx_ring;
	bzero(desc, VTE_RX_RING_SZ);
	for (i = 0; i < VTE_RX_RING_CNT; i++) {
		rxd = &sc->vte_cdata.vte_rxdesc[i];
		rxd->rx_m = NULL;
		if (i != VTE_RX_RING_CNT - 1)
			addr = sc->vte_cdata.vte_rx_ring_paddr +
			    sizeof(struct vte_rx_desc) * (i + 1);
		else
			addr = sc->vte_cdata.vte_rx_ring_paddr +
			    sizeof(struct vte_rx_desc) * 0;
		desc = &sc->vte_cdata.vte_rx_ring[i];
		desc->drnp = htole32(addr);
		rxd->rx_desc = desc;
		if (vte_newbuf(sc, rxd, 1) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sc_dmat, sc->vte_cdata.vte_rx_ring_map, 0,
	    sc->vte_cdata.vte_rx_ring_map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	return (0);
}

void
vte_iff(struct vte_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint8_t *eaddr;
	uint32_t crc;
	uint16_t rxfilt_perf[VTE_RXFILT_PERFECT_CNT][3];
	uint16_t mchash[4], mcr;
	int i, nperf;

	bzero(mchash, sizeof(mchash));
	for (i = 0; i < VTE_RXFILT_PERFECT_CNT; i++) {
		rxfilt_perf[i][0] = 0xFFFF;
		rxfilt_perf[i][1] = 0xFFFF;
		rxfilt_perf[i][2] = 0xFFFF;
	}

	mcr = CSR_READ_2(sc, VTE_MCR0);
	mcr &= ~(MCR0_PROMISC | MCR0_BROADCAST_DIS | MCR0_MULTICAST);
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			mcr |= MCR0_PROMISC;
		else
			mcr |= MCR0_MULTICAST;
		mchash[0] = mchash[1] = mchash[2] = mchash[3] = 0xFFFF;
	} else {
		nperf = 0;
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			/*
			 * Program the first 3 multicast groups into
			 * the perfect filter.  For all others, use the
			 * hash table.
			 */
			if (nperf < VTE_RXFILT_PERFECT_CNT) {
				eaddr = enm->enm_addrlo;
				rxfilt_perf[nperf][0] = 
				    eaddr[1] << 8 | eaddr[0];
				rxfilt_perf[nperf][1] = 
				    eaddr[3] << 8 | eaddr[2];
				rxfilt_perf[nperf][2] = 
				    eaddr[5] << 8 | eaddr[4];
				nperf++;
				continue;
			}
			crc = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN);
			mchash[crc >> 30] |= 1 << ((crc >> 26) & 0x0F);
			ETHER_NEXT_MULTI(step, enm);
		}
		if (mchash[0] != 0 || mchash[1] != 0 || mchash[2] != 0 ||
		    mchash[3] != 0)
			mcr |= MCR0_MULTICAST;
	}

	/* Program multicast hash table. */
	CSR_WRITE_2(sc, VTE_MAR0, mchash[0]);
	CSR_WRITE_2(sc, VTE_MAR1, mchash[1]);
	CSR_WRITE_2(sc, VTE_MAR2, mchash[2]);
	CSR_WRITE_2(sc, VTE_MAR3, mchash[3]);
	/* Program perfect filter table. */
	for (i = 0; i < VTE_RXFILT_PERFECT_CNT; i++) {
		CSR_WRITE_2(sc, VTE_RXFILTER_PEEFECT_BASE + 8 * i + 0,
		    rxfilt_perf[i][0]);
		CSR_WRITE_2(sc, VTE_RXFILTER_PEEFECT_BASE + 8 * i + 2,
		    rxfilt_perf[i][1]);
		CSR_WRITE_2(sc, VTE_RXFILTER_PEEFECT_BASE + 8 * i + 4,
		    rxfilt_perf[i][2]);
	}
	CSR_WRITE_2(sc, VTE_MCR0, mcr);
	CSR_READ_2(sc, VTE_MCR0);
}
