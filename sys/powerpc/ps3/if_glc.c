/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2010 Nathan Whitehorn
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_dl.h>

#include <machine/pio.h>
#include <machine/bus.h>
#include <machine/platform.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include "ps3bus.h"
#include "ps3-hvcall.h"
#include "if_glcreg.h"

static int	glc_probe(device_t);
static int	glc_attach(device_t);
static void	glc_init(void *xsc);
static void	glc_start(struct ifnet *ifp);
static int	glc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static void	glc_set_multicast(struct glc_softc *sc);
static int	glc_add_rxbuf(struct glc_softc *sc, int idx);
static int	glc_add_rxbuf_dma(struct glc_softc *sc, int idx);
static int	glc_encap(struct glc_softc *sc, struct mbuf **m_head,
		    bus_addr_t *pktdesc);
static int	glc_intr_filter(void *xsc);
static void	glc_intr(void *xsc);
static void	glc_tick(void *xsc);
static void	glc_media_status(struct ifnet *ifp, struct ifmediareq *ifmr);
static int	glc_media_change(struct ifnet *ifp);

static MALLOC_DEFINE(M_GLC, "gelic", "PS3 GELIC ethernet");

static device_method_t glc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		glc_probe),
	DEVMETHOD(device_attach,	glc_attach),

	{ 0, 0 }
};

static driver_t glc_driver = {
	"glc",
	glc_methods,
	sizeof(struct glc_softc)
};

static devclass_t glc_devclass;

DRIVER_MODULE(glc, ps3bus, glc_driver, glc_devclass, 0, 0);

static int 
glc_probe(device_t dev) 
{

	if (ps3bus_get_bustype(dev) != PS3_BUSTYPE_SYSBUS ||
	    ps3bus_get_devtype(dev) != PS3_DEVTYPE_GELIC)
		return (ENXIO);

	device_set_desc(dev, "Playstation 3 GELIC Network Controller");
	return (BUS_PROBE_SPECIFIC);
}

static void
glc_getphys(void *xaddr, bus_dma_segment_t *segs, int nsegs, int error)
{
	if (error != 0)
		return;

	*(bus_addr_t *)xaddr = segs[0].ds_addr;
}

static int 
glc_attach(device_t dev) 
{
	struct glc_softc *sc;
	struct glc_txsoft *txs;
	uint64_t mac64, val, junk;
	int i, err;

	sc = device_get_softc(dev);

	sc->sc_bus = ps3bus_get_bus(dev);
	sc->sc_dev = ps3bus_get_device(dev);
	sc->sc_self = dev;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->sc_tick_ch, &sc->sc_mtx, 0);
	sc->next_txdma_slot = 0;
	sc->bsy_txdma_slots = 0;
	sc->sc_next_rxdma_slot = 0;
	sc->first_used_txdma_slot = -1;

	/*
	 * Shut down existing tasks.
	 */

	lv1_net_stop_tx_dma(sc->sc_bus, sc->sc_dev, 0);
	lv1_net_stop_rx_dma(sc->sc_bus, sc->sc_dev, 0);

	sc->sc_ifp = if_alloc(IFT_ETHER);
	sc->sc_ifp->if_softc = sc;

	/*
	 * Get MAC address and VLAN id
	 */

	lv1_net_control(sc->sc_bus, sc->sc_dev, GELIC_GET_MAC_ADDRESS,
	    0, 0, 0, &mac64, &junk);
	memcpy(sc->sc_enaddr, &((uint8_t *)&mac64)[2], sizeof(sc->sc_enaddr));
	sc->sc_tx_vlan = sc->sc_rx_vlan = -1;
	err = lv1_net_control(sc->sc_bus, sc->sc_dev, GELIC_GET_VLAN_ID,
	    GELIC_VLAN_TX_ETHERNET, 0, 0, &val, &junk);
	if (err == 0)
		sc->sc_tx_vlan = val;
	err = lv1_net_control(sc->sc_bus, sc->sc_dev, GELIC_GET_VLAN_ID,
	    GELIC_VLAN_RX_ETHERNET, 0, 0, &val, &junk);
	if (err == 0)
		sc->sc_rx_vlan = val;

	/*
	 * Set up interrupt handler
	 */
	sc->sc_irqid = 0;
	sc->sc_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->sc_irqid,
	    RF_ACTIVE);
	if (sc->sc_irq == NULL) {
		device_printf(dev, "Could not allocate IRQ!\n");
		mtx_destroy(&sc->sc_mtx);
		return (ENXIO);
	}

	bus_setup_intr(dev, sc->sc_irq,
	    INTR_TYPE_NET | INTR_MPSAFE | INTR_ENTROPY,
	    glc_intr_filter, glc_intr, sc, &sc->sc_irqctx);
	sc->sc_hwirq_status = (uint64_t *)contigmalloc(8, M_GLC, M_ZERO, 0,
	    BUS_SPACE_MAXADDR_32BIT, 8, PAGE_SIZE);
	lv1_net_set_interrupt_status_indicator(sc->sc_bus, sc->sc_dev,
	    vtophys(sc->sc_hwirq_status), 0);
	lv1_net_set_interrupt_mask(sc->sc_bus, sc->sc_dev,
	    GELIC_INT_RXDONE | GELIC_INT_RXFRAME | GELIC_INT_PHY |
	    GELIC_INT_TX_CHAIN_END, 0);

	/*
	 * Set up DMA.
	 */

	err = bus_dma_tag_create(bus_get_dma_tag(dev), 32, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    129*sizeof(struct glc_dmadesc), 1, 128*sizeof(struct glc_dmadesc),
	    0, NULL,NULL, &sc->sc_dmadesc_tag);

	err = bus_dmamem_alloc(sc->sc_dmadesc_tag, (void **)&sc->sc_txdmadesc,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->sc_txdmadesc_map);
	err = bus_dmamap_load(sc->sc_dmadesc_tag, sc->sc_txdmadesc_map,
	    sc->sc_txdmadesc, 128*sizeof(struct glc_dmadesc), glc_getphys,
	    &sc->sc_txdmadesc_phys, 0);
	err = bus_dmamem_alloc(sc->sc_dmadesc_tag, (void **)&sc->sc_rxdmadesc,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->sc_rxdmadesc_map);
	err = bus_dmamap_load(sc->sc_dmadesc_tag, sc->sc_rxdmadesc_map,
	    sc->sc_rxdmadesc, 128*sizeof(struct glc_dmadesc), glc_getphys,
	    &sc->sc_rxdmadesc_phys, 0);

	err = bus_dma_tag_create(bus_get_dma_tag(dev), 128, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0, NULL,NULL,
	    &sc->sc_rxdma_tag);
	err = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, 16, BUS_SPACE_MAXSIZE_32BIT, 0, NULL,NULL,
	    &sc->sc_txdma_tag);

	/* init transmit descriptors */
	STAILQ_INIT(&sc->sc_txfreeq);
	STAILQ_INIT(&sc->sc_txdirtyq);

	/* create TX DMA maps */
	err = ENOMEM;
	for (i = 0; i < GLC_MAX_TX_PACKETS; i++) {
		txs = &sc->sc_txsoft[i];
		txs->txs_mbuf = NULL;
		err = bus_dmamap_create(sc->sc_txdma_tag, 0, &txs->txs_dmamap);
		if (err) {
			device_printf(dev,
			    "unable to create TX DMA map %d, error = %d\n",
			    i, err);
		}
		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}

	/* Create the receive buffer DMA maps. */
	for (i = 0; i < GLC_MAX_RX_PACKETS; i++) {
		err = bus_dmamap_create(sc->sc_rxdma_tag, 0,
		    &sc->sc_rxsoft[i].rxs_dmamap);
		if (err) {
			device_printf(dev,
			    "unable to create RX DMA map %d, error = %d\n",
			    i, err);
		}
		sc->sc_rxsoft[i].rxs_mbuf = NULL;
	}

	/*
	 * Attach to network stack
	 */

	if_initname(sc->sc_ifp, device_get_name(dev), device_get_unit(dev));
	sc->sc_ifp->if_mtu = ETHERMTU;
	sc->sc_ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	sc->sc_ifp->if_hwassist = CSUM_TCP | CSUM_UDP;
	sc->sc_ifp->if_capabilities = IFCAP_HWCSUM | IFCAP_RXCSUM;
	sc->sc_ifp->if_capenable = IFCAP_HWCSUM | IFCAP_RXCSUM;
	sc->sc_ifp->if_start = glc_start;
	sc->sc_ifp->if_ioctl = glc_ioctl;
	sc->sc_ifp->if_init = glc_init;

	ifmedia_init(&sc->sc_media, IFM_IMASK, glc_media_change,
	    glc_media_status);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_10_T, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_10_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_100_TX, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_100_TX | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_1000_T | IFM_FDX, 0, NULL);
	ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

	IFQ_SET_MAXLEN(&sc->sc_ifp->if_snd, GLC_MAX_TX_PACKETS);
	sc->sc_ifp->if_snd.ifq_drv_maxlen = GLC_MAX_TX_PACKETS;
	IFQ_SET_READY(&sc->sc_ifp->if_snd);

	ether_ifattach(sc->sc_ifp, sc->sc_enaddr);
	sc->sc_ifp->if_hwassist = 0;

	return (0);

	mtx_destroy(&sc->sc_mtx);
	if_free(sc->sc_ifp);
	return (ENXIO);
}

static void
glc_init_locked(struct glc_softc *sc)
{
	int i, error;
	struct glc_rxsoft *rxs;
	struct glc_txsoft *txs;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	lv1_net_stop_tx_dma(sc->sc_bus, sc->sc_dev, 0);
	lv1_net_stop_rx_dma(sc->sc_bus, sc->sc_dev, 0);

	glc_set_multicast(sc);

	for (i = 0; i < GLC_MAX_RX_PACKETS; i++) {
		rxs = &sc->sc_rxsoft[i];
		rxs->rxs_desc_slot = i;

		if (rxs->rxs_mbuf == NULL) {
			glc_add_rxbuf(sc, i);

			if (rxs->rxs_mbuf == NULL) {
				rxs->rxs_desc_slot = -1;
				break;
			}
		}

		glc_add_rxbuf_dma(sc, i);
		bus_dmamap_sync(sc->sc_dmadesc_tag, sc->sc_rxdmadesc_map,
		    BUS_DMASYNC_PREREAD);
	}

	/* Clear TX dirty queue */
	while ((txs = STAILQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
		STAILQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);
		bus_dmamap_unload(sc->sc_txdma_tag, txs->txs_dmamap);

		if (txs->txs_mbuf != NULL) {
			m_freem(txs->txs_mbuf);
			txs->txs_mbuf = NULL;
		}

		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}
	sc->first_used_txdma_slot = -1;
	sc->bsy_txdma_slots = 0;

	error = lv1_net_start_rx_dma(sc->sc_bus, sc->sc_dev,
	    sc->sc_rxsoft[0].rxs_desc, 0);
	if (error != 0)
		device_printf(sc->sc_self,
		    "lv1_net_start_rx_dma error: %d\n", error);

	sc->sc_ifp->if_drv_flags |= IFF_DRV_RUNNING;
	sc->sc_ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	sc->sc_ifpflags = sc->sc_ifp->if_flags;

	sc->sc_wdog_timer = 0;
	callout_reset(&sc->sc_tick_ch, hz, glc_tick, sc);
}

static void
glc_stop(void *xsc)
{
	struct glc_softc *sc = xsc;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	lv1_net_stop_tx_dma(sc->sc_bus, sc->sc_dev, 0);
	lv1_net_stop_rx_dma(sc->sc_bus, sc->sc_dev, 0);
}

static void
glc_init(void *xsc)
{
	struct glc_softc *sc = xsc;

	mtx_lock(&sc->sc_mtx);
	glc_init_locked(sc);
	mtx_unlock(&sc->sc_mtx);
}

static void
glc_tick(void *xsc)
{
	struct glc_softc *sc = xsc;

	mtx_assert(&sc->sc_mtx, MA_OWNED);

	/*
	 * XXX: Sometimes the RX queue gets stuck. Poke it periodically until
	 * we figure out why. This will fail harmlessly if the RX queue is
	 * already running.
	 */
	lv1_net_start_rx_dma(sc->sc_bus, sc->sc_dev,
	    sc->sc_rxsoft[sc->sc_next_rxdma_slot].rxs_desc, 0);

	if (sc->sc_wdog_timer == 0 || --sc->sc_wdog_timer != 0) {
		callout_reset(&sc->sc_tick_ch, hz, glc_tick, sc);
		return;
	}

	/* Problems */
	device_printf(sc->sc_self, "device timeout\n");

	glc_init_locked(sc);
}

static void
glc_start_locked(struct ifnet *ifp)
{
	struct glc_softc *sc = ifp->if_softc;
	bus_addr_t first, pktdesc;
	int kickstart = 0;
	int error;
	struct mbuf *mb_head;

	mtx_assert(&sc->sc_mtx, MA_OWNED);
	first = 0;

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	if (STAILQ_EMPTY(&sc->sc_txdirtyq))
		kickstart = 1;

	while (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, mb_head);

		if (mb_head == NULL)
			break;

		/* Check if the ring buffer is full */
		if (sc->bsy_txdma_slots > 125) {
			/* Put the packet back and stop */
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			IFQ_DRV_PREPEND(&ifp->if_snd, mb_head);
			break;
		}

		BPF_MTAP(ifp, mb_head);

		if (sc->sc_tx_vlan >= 0)
			mb_head = ether_vlanencap(mb_head, sc->sc_tx_vlan);

		if (glc_encap(sc, &mb_head, &pktdesc)) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		if (first == 0)
			first = pktdesc;
	}

	if (kickstart && first != 0) {
		error = lv1_net_start_tx_dma(sc->sc_bus, sc->sc_dev, first, 0);
		if (error != 0)
			device_printf(sc->sc_self,
			    "lv1_net_start_tx_dma error: %d\n", error);
		sc->sc_wdog_timer = 5;
	}
}

static void
glc_start(struct ifnet *ifp)
{
	struct glc_softc *sc = ifp->if_softc;

	mtx_lock(&sc->sc_mtx);
	glc_start_locked(ifp);
	mtx_unlock(&sc->sc_mtx);
}

static int
glc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct glc_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int err = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
                mtx_lock(&sc->sc_mtx);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			   ((ifp->if_flags ^ sc->sc_ifpflags) &
			    (IFF_ALLMULTI | IFF_PROMISC)) != 0)
				glc_set_multicast(sc);
			else
				glc_init_locked(sc);
		}
		else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			glc_stop(sc);
		sc->sc_ifpflags = ifp->if_flags;
		mtx_unlock(&sc->sc_mtx);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
                mtx_lock(&sc->sc_mtx);
		glc_set_multicast(sc);
                mtx_unlock(&sc->sc_mtx);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		err = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;
	default:
		err = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (err);
}

static void
glc_set_multicast(struct glc_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct ifmultiaddr *inm;
	uint64_t addr;
	int naddrs;

	/* Clear multicast filter */
	lv1_net_remove_multicast_address(sc->sc_bus, sc->sc_dev, 0, 1);

	/* Add broadcast */
	lv1_net_add_multicast_address(sc->sc_bus, sc->sc_dev,
	    0xffffffffffffL, 0);

	if ((ifp->if_flags & IFF_ALLMULTI) != 0) {
		lv1_net_add_multicast_address(sc->sc_bus, sc->sc_dev, 0, 1);
	} else {
		if_maddr_rlock(ifp);
		naddrs = 1; /* Include broadcast */
		CK_STAILQ_FOREACH(inm, &ifp->if_multiaddrs, ifma_link) {
			if (inm->ifma_addr->sa_family != AF_LINK)
				continue;
			addr = 0;
			memcpy(&((uint8_t *)(&addr))[2],
			    LLADDR((struct sockaddr_dl *)inm->ifma_addr),
			    ETHER_ADDR_LEN);

			lv1_net_add_multicast_address(sc->sc_bus, sc->sc_dev,
			    addr, 0);

			/*
			 * Filter can only hold 32 addresses, so fall back to
			 * the IFF_ALLMULTI case if we have too many.
			 */
			if (++naddrs >= 32) {
				lv1_net_add_multicast_address(sc->sc_bus,
				    sc->sc_dev, 0, 1);
				break;
			}
		}
		if_maddr_runlock(ifp);
	}
}

static int
glc_add_rxbuf(struct glc_softc *sc, int idx)
{
	struct glc_rxsoft *rxs = &sc->sc_rxsoft[idx];
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	int error, nsegs;
			
	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;

	if (rxs->rxs_mbuf != NULL) {
		bus_dmamap_sync(sc->sc_rxdma_tag, rxs->rxs_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_rxdma_tag, rxs->rxs_dmamap);
	}

	error = bus_dmamap_load_mbuf_sg(sc->sc_rxdma_tag, rxs->rxs_dmamap, m,
	    segs, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_self,
		    "cannot load RS DMA map %d, error = %d\n", idx, error);
		m_freem(m);
		return (error);
	}
	/* If nsegs is wrong then the stack is corrupt. */
	KASSERT(nsegs == 1,
	    ("%s: too many DMA segments (%d)", __func__, nsegs));
	rxs->rxs_mbuf = m;
	rxs->segment = segs[0];

	bus_dmamap_sync(sc->sc_rxdma_tag, rxs->rxs_dmamap, BUS_DMASYNC_PREREAD);

	return (0);
}

static int
glc_add_rxbuf_dma(struct glc_softc *sc, int idx)
{
	struct glc_rxsoft *rxs = &sc->sc_rxsoft[idx];
	
	bzero(&sc->sc_rxdmadesc[idx], sizeof(sc->sc_rxdmadesc[idx]));
	sc->sc_rxdmadesc[idx].paddr = rxs->segment.ds_addr;
	sc->sc_rxdmadesc[idx].len = rxs->segment.ds_len;
	sc->sc_rxdmadesc[idx].next = sc->sc_rxdmadesc_phys +
	    ((idx + 1) % GLC_MAX_RX_PACKETS)*sizeof(sc->sc_rxdmadesc[idx]);
	sc->sc_rxdmadesc[idx].cmd_stat = GELIC_DESCR_OWNED;

	rxs->rxs_desc_slot = idx;
	rxs->rxs_desc = sc->sc_rxdmadesc_phys + idx*sizeof(struct glc_dmadesc);

        return (0);
}

static int
glc_encap(struct glc_softc *sc, struct mbuf **m_head, bus_addr_t *pktdesc)
{
	bus_dma_segment_t segs[16];
	struct glc_txsoft *txs;
	struct mbuf *m;
	bus_addr_t firstslotphys;
	int i, idx, nsegs, nsegs_max;
	int err = 0;

	/* Max number of segments is the number of free DMA slots */
	nsegs_max = 128 - sc->bsy_txdma_slots;

	if (nsegs_max > 16 || sc->first_used_txdma_slot < 0)
		nsegs_max = 16;

	/* Get a work queue entry. */
	if ((txs = STAILQ_FIRST(&sc->sc_txfreeq)) == NULL) {
		/* Ran out of descriptors. */
		return (ENOBUFS);
	}

	nsegs = 0;
	for (m = *m_head; m != NULL; m = m->m_next)
		nsegs++;

	if (nsegs > nsegs_max) {
		m = m_collapse(*m_head, M_NOWAIT, nsegs_max);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
	}
	
	err = bus_dmamap_load_mbuf_sg(sc->sc_txdma_tag, txs->txs_dmamap,
	    *m_head, segs, &nsegs, BUS_DMA_NOWAIT);
	if (err != 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (err);
	}

	KASSERT(nsegs <= 128 - sc->bsy_txdma_slots,
	    ("GLC: Mapped too many (%d) DMA segments with %d available",
	    nsegs, 128 - sc->bsy_txdma_slots));

	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	txs->txs_ndescs = nsegs;
	txs->txs_firstdesc = sc->next_txdma_slot;

	idx = txs->txs_firstdesc;
	firstslotphys = sc->sc_txdmadesc_phys +
	    txs->txs_firstdesc*sizeof(struct glc_dmadesc);

	for (i = 0; i < nsegs; i++) {
		bzero(&sc->sc_txdmadesc[idx], sizeof(sc->sc_txdmadesc[idx]));
		sc->sc_txdmadesc[idx].paddr = segs[i].ds_addr;
		sc->sc_txdmadesc[idx].len = segs[i].ds_len;
		sc->sc_txdmadesc[idx].next = sc->sc_txdmadesc_phys +
		    ((idx + 1) % GLC_MAX_TX_PACKETS)*sizeof(struct glc_dmadesc);
		sc->sc_txdmadesc[idx].cmd_stat |= GELIC_CMDSTAT_NOIPSEC;

		if (i+1 == nsegs) {
			txs->txs_lastdesc = idx;
			sc->sc_txdmadesc[idx].next = 0;
			sc->sc_txdmadesc[idx].cmd_stat |= GELIC_CMDSTAT_LAST;
		}

		if ((*m_head)->m_pkthdr.csum_flags & CSUM_TCP)
			sc->sc_txdmadesc[idx].cmd_stat |= GELIC_CMDSTAT_CSUM_TCP;
		if ((*m_head)->m_pkthdr.csum_flags & CSUM_UDP)
			sc->sc_txdmadesc[idx].cmd_stat |= GELIC_CMDSTAT_CSUM_UDP;
		sc->sc_txdmadesc[idx].cmd_stat |= GELIC_DESCR_OWNED;

		idx = (idx + 1) % GLC_MAX_TX_PACKETS;
	}
	sc->next_txdma_slot = idx;
	sc->bsy_txdma_slots += nsegs;
	if (txs->txs_firstdesc != 0)
		idx = txs->txs_firstdesc - 1;
	else
		idx = GLC_MAX_TX_PACKETS - 1;

	if (sc->first_used_txdma_slot < 0)
		sc->first_used_txdma_slot = txs->txs_firstdesc;

	bus_dmamap_sync(sc->sc_txdma_tag, txs->txs_dmamap,
	    BUS_DMASYNC_PREWRITE);
	sc->sc_txdmadesc[idx].next = firstslotphys;

	STAILQ_REMOVE_HEAD(&sc->sc_txfreeq, txs_q);
	STAILQ_INSERT_TAIL(&sc->sc_txdirtyq, txs, txs_q);
	txs->txs_mbuf = *m_head;
	*pktdesc = firstslotphys;

	return (0);
}

static void
glc_rxintr(struct glc_softc *sc)
{
	int i, restart_rxdma, error;
	struct mbuf *m;
	struct ifnet *ifp = sc->sc_ifp;

	bus_dmamap_sync(sc->sc_dmadesc_tag, sc->sc_rxdmadesc_map,
	    BUS_DMASYNC_POSTREAD);

	restart_rxdma = 0;
	while ((sc->sc_rxdmadesc[sc->sc_next_rxdma_slot].cmd_stat &
	   GELIC_DESCR_OWNED) == 0) {
		i = sc->sc_next_rxdma_slot;
		sc->sc_next_rxdma_slot++;
		if (sc->sc_next_rxdma_slot >= GLC_MAX_RX_PACKETS)
			sc->sc_next_rxdma_slot = 0;

		if (sc->sc_rxdmadesc[i].cmd_stat & GELIC_CMDSTAT_CHAIN_END)
			restart_rxdma = 1;

		if (sc->sc_rxdmadesc[i].rxerror & GELIC_RXERRORS) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto requeue;
		}

		m = sc->sc_rxsoft[i].rxs_mbuf;
		if (sc->sc_rxdmadesc[i].data_stat & GELIC_RX_IPCSUM) {
			m->m_pkthdr.csum_flags |=
			    CSUM_IP_CHECKED | CSUM_IP_VALID;
		}
		if (sc->sc_rxdmadesc[i].data_stat & GELIC_RX_TCPUDPCSUM) {
			m->m_pkthdr.csum_flags |=
			    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
			m->m_pkthdr.csum_data = 0xffff;
		}

		if (glc_add_rxbuf(sc, i)) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto requeue;
		}

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		m->m_pkthdr.rcvif = ifp;
		m->m_len = sc->sc_rxdmadesc[i].valid_size;
		m->m_pkthdr.len = m->m_len;

		/*
		 * Remove VLAN tag. Even on early firmwares that do not allow
		 * multiple VLANs, the VLAN tag is still in place here.
		 */
		m_adj(m, 2);

		mtx_unlock(&sc->sc_mtx);
		(*ifp->if_input)(ifp, m);
		mtx_lock(&sc->sc_mtx);

	    requeue:
		glc_add_rxbuf_dma(sc, i);	
	}

	bus_dmamap_sync(sc->sc_dmadesc_tag, sc->sc_rxdmadesc_map,
	    BUS_DMASYNC_PREWRITE);

	if (restart_rxdma) {
		error = lv1_net_start_rx_dma(sc->sc_bus, sc->sc_dev,
		    sc->sc_rxsoft[sc->sc_next_rxdma_slot].rxs_desc, 0);
		if (error != 0)
			device_printf(sc->sc_self,
			    "lv1_net_start_rx_dma error: %d\n", error);
	}
}

static void
glc_txintr(struct glc_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct glc_txsoft *txs;
	int progress = 0, kickstart = 0, error;

	bus_dmamap_sync(sc->sc_dmadesc_tag, sc->sc_txdmadesc_map,
	    BUS_DMASYNC_POSTREAD);

	while ((txs = STAILQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
		if (sc->sc_txdmadesc[txs->txs_lastdesc].cmd_stat
		    & GELIC_DESCR_OWNED)
			break;

		STAILQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);
		bus_dmamap_unload(sc->sc_txdma_tag, txs->txs_dmamap);
		sc->bsy_txdma_slots -= txs->txs_ndescs;

		if (txs->txs_mbuf != NULL) {
			m_freem(txs->txs_mbuf);
			txs->txs_mbuf = NULL;
		}

		if ((sc->sc_txdmadesc[txs->txs_lastdesc].cmd_stat & 0xf0000000)
		    != 0) {
			lv1_net_stop_tx_dma(sc->sc_bus, sc->sc_dev, 0);
			kickstart = 1;
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		}

		if (sc->sc_txdmadesc[txs->txs_lastdesc].cmd_stat &
		    GELIC_CMDSTAT_CHAIN_END)
			kickstart = 1;

		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		progress = 1;
	}

	if (txs != NULL)
		sc->first_used_txdma_slot = txs->txs_firstdesc;
	else
		sc->first_used_txdma_slot = -1;

	if (kickstart || txs != NULL) {
		/* Speculatively (or necessarily) start the TX queue again */
		error = lv1_net_start_tx_dma(sc->sc_bus, sc->sc_dev,
		    sc->sc_txdmadesc_phys +
		    ((txs == NULL) ? 0 : txs->txs_firstdesc)*
		     sizeof(struct glc_dmadesc), 0);
		if (error != 0)
			device_printf(sc->sc_self,
			    "lv1_net_start_tx_dma error: %d\n", error);
	}

	if (progress) {
		/*
		 * We freed some descriptors, so reset IFF_DRV_OACTIVE
		 * and restart.
		 */
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		sc->sc_wdog_timer = STAILQ_EMPTY(&sc->sc_txdirtyq) ? 0 : 5;

		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) &&
		    !IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			glc_start_locked(ifp);
	}
}

static int
glc_intr_filter(void *xsc)
{
	struct glc_softc *sc = xsc; 

	powerpc_sync();
	atomic_set_64(&sc->sc_interrupt_status, *sc->sc_hwirq_status);
	return (FILTER_SCHEDULE_THREAD);
}

static void
glc_intr(void *xsc)
{
	struct glc_softc *sc = xsc; 
	uint64_t status, linkstat, junk;

	mtx_lock(&sc->sc_mtx);

	status = atomic_readandclear_64(&sc->sc_interrupt_status);

	if (status == 0) {
		mtx_unlock(&sc->sc_mtx);
		return;
	}

	if (status & (GELIC_INT_RXDONE | GELIC_INT_RXFRAME))
		glc_rxintr(sc);

	if (status & (GELIC_INT_TXDONE | GELIC_INT_TX_CHAIN_END))
		glc_txintr(sc);

	if (status & GELIC_INT_PHY) {
		lv1_net_control(sc->sc_bus, sc->sc_dev, GELIC_GET_LINK_STATUS,
		    GELIC_VLAN_TX_ETHERNET, 0, 0, &linkstat, &junk);

		linkstat = (linkstat & GELIC_LINK_UP) ?
		    LINK_STATE_UP : LINK_STATE_DOWN;
		if (linkstat != sc->sc_ifp->if_link_state)
			if_link_state_change(sc->sc_ifp, linkstat);
	}

	mtx_unlock(&sc->sc_mtx);
}

static void
glc_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct glc_softc *sc = ifp->if_softc; 
	uint64_t status, junk;

	ifmr->ifm_status = IFM_AVALID;
	ifmr->ifm_active = IFM_ETHER;

	lv1_net_control(sc->sc_bus, sc->sc_dev, GELIC_GET_LINK_STATUS,
	    GELIC_VLAN_TX_ETHERNET, 0, 0, &status, &junk);

	if (status & GELIC_LINK_UP)
		ifmr->ifm_status |= IFM_ACTIVE;

	if (status & GELIC_SPEED_10)
		ifmr->ifm_active |= IFM_10_T;
	else if (status & GELIC_SPEED_100)
		ifmr->ifm_active |= IFM_100_TX;
	else if (status & GELIC_SPEED_1000)
		ifmr->ifm_active |= IFM_1000_T;

	if (status & GELIC_FULL_DUPLEX)
		ifmr->ifm_active |= IFM_FDX;
	else
		ifmr->ifm_active |= IFM_HDX;
}

static int
glc_media_change(struct ifnet *ifp)
{
	struct glc_softc *sc = ifp->if_softc; 
	uint64_t mode, junk;
	int result;

	if (IFM_TYPE(sc->sc_media.ifm_media) != IFM_ETHER)
		return (EINVAL);

	switch (IFM_SUBTYPE(sc->sc_media.ifm_media)) {
	case IFM_AUTO:
		mode = GELIC_AUTO_NEG;
		break;
	case IFM_10_T:
		mode = GELIC_SPEED_10;
		break;
	case IFM_100_TX:
		mode = GELIC_SPEED_100;
		break;
	case IFM_1000_T:
		mode = GELIC_SPEED_1000 | GELIC_FULL_DUPLEX;
		break;
	default:
		return (EINVAL);
	}

	if (IFM_OPTIONS(sc->sc_media.ifm_media) & IFM_FDX)
		mode |= GELIC_FULL_DUPLEX;

	result = lv1_net_control(sc->sc_bus, sc->sc_dev, GELIC_SET_LINK_MODE,
	    GELIC_VLAN_TX_ETHERNET, mode, 0, &junk, &junk);

	return (result ? EIO : 0);
}

