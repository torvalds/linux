/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2001 Eduardo Horvath.
 * Copyright (c) 2001-2003 Thomas Moestl
 * Copyright (c) 2007-2009 Marius Strobl <marius@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: gem.c,v 1.21 2002/06/01 23:50:58 lukem Exp
 *	from: FreeBSD: if_gem.c 182060 2008-08-23 15:03:26Z marius
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * driver for Sun Cassini/Cassini+ and National Semiconductor DP83065
 * Saturn Gigabit Ethernet controllers
 */

#if 0
#define	CAS_DEBUG
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/refcount.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/taskqueue.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <machine/bus.h>
#if defined(__powerpc__) || defined(__sparc64__)
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <machine/ofw_machdep.h>
#endif
#include <machine/resource.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/cas/if_casreg.h>
#include <dev/cas/if_casvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "miibus_if.h"

#define RINGASSERT(n , min, max)					\
	CTASSERT(powerof2(n) && (n) >= (min) && (n) <= (max))

RINGASSERT(CAS_NRXCOMP, 128, 32768);
RINGASSERT(CAS_NRXDESC, 32, 8192);
RINGASSERT(CAS_NRXDESC2, 32, 8192);
RINGASSERT(CAS_NTXDESC, 32, 8192);

#undef RINGASSERT

#define	CCDASSERT(m, a)							\
	CTASSERT((offsetof(struct cas_control_data, m) & ((a) - 1)) == 0)

CCDASSERT(ccd_rxcomps, CAS_RX_COMP_ALIGN);
CCDASSERT(ccd_rxdescs, CAS_RX_DESC_ALIGN);
CCDASSERT(ccd_rxdescs2, CAS_RX_DESC_ALIGN);

#undef CCDASSERT

#define	CAS_TRIES	10000

/*
 * According to documentation, the hardware has support for basic TCP
 * checksum offloading only, in practice this can be also used for UDP
 * however (i.e. the problem of previous Sun NICs that a checksum of 0x0
 * is not converted to 0xffff no longer exists).
 */
#define	CAS_CSUM_FEATURES	(CSUM_TCP | CSUM_UDP)

static inline void cas_add_rxdesc(struct cas_softc *sc, u_int idx);
static int	cas_attach(struct cas_softc *sc);
static int	cas_bitwait(struct cas_softc *sc, bus_addr_t r, uint32_t clr,
		    uint32_t set);
static void	cas_cddma_callback(void *xsc, bus_dma_segment_t *segs,
		    int nsegs, int error);
static void	cas_detach(struct cas_softc *sc);
static int	cas_disable_rx(struct cas_softc *sc);
static int	cas_disable_tx(struct cas_softc *sc);
static void	cas_eint(struct cas_softc *sc, u_int status);
static void	cas_free(struct mbuf *m);
static void	cas_init(void *xsc);
static void	cas_init_locked(struct cas_softc *sc);
static void	cas_init_regs(struct cas_softc *sc);
static int	cas_intr(void *v);
static void	cas_intr_task(void *arg, int pending __unused);
static int	cas_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data);
static int	cas_load_txmbuf(struct cas_softc *sc, struct mbuf **m_head);
static int	cas_mediachange(struct ifnet *ifp);
static void	cas_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr);
static void	cas_meminit(struct cas_softc *sc);
static void	cas_mifinit(struct cas_softc *sc);
static int	cas_mii_readreg(device_t dev, int phy, int reg);
static void	cas_mii_statchg(device_t dev);
static int	cas_mii_writereg(device_t dev, int phy, int reg, int val);
static void	cas_reset(struct cas_softc *sc);
static int	cas_reset_rx(struct cas_softc *sc);
static int	cas_reset_tx(struct cas_softc *sc);
static void	cas_resume(struct cas_softc *sc);
static u_int	cas_descsize(u_int sz);
static void	cas_rint(struct cas_softc *sc);
static void	cas_rint_timeout(void *arg);
static inline void cas_rxcksum(struct mbuf *m, uint16_t cksum);
static inline void cas_rxcompinit(struct cas_rx_comp *rxcomp);
static u_int	cas_rxcompsize(u_int sz);
static void	cas_rxdma_callback(void *xsc, bus_dma_segment_t *segs,
		    int nsegs, int error);
static void	cas_setladrf(struct cas_softc *sc);
static void	cas_start(struct ifnet *ifp);
static void	cas_stop(struct ifnet *ifp);
static void	cas_suspend(struct cas_softc *sc);
static void	cas_tick(void *arg);
static void	cas_tint(struct cas_softc *sc);
static void	cas_tx_task(void *arg, int pending __unused);
static inline void cas_txkick(struct cas_softc *sc);
static void	cas_watchdog(struct cas_softc *sc);

static devclass_t cas_devclass;

MODULE_DEPEND(cas, ether, 1, 1, 1);
MODULE_DEPEND(cas, miibus, 1, 1, 1);

#ifdef CAS_DEBUG
#include <sys/ktr.h>
#define	KTR_CAS		KTR_SPARE2
#endif

static int
cas_attach(struct cas_softc *sc)
{
	struct cas_txsoft *txs;
	struct ifnet *ifp;
	int error, i;
	uint32_t v;

	/* Set up ifnet structure. */
	ifp = sc->sc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL)
		return (ENOSPC);
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(sc->sc_dev),
	    device_get_unit(sc->sc_dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = cas_start;
	ifp->if_ioctl = cas_ioctl;
	ifp->if_init = cas_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, CAS_TXQUEUELEN);
	ifp->if_snd.ifq_drv_maxlen = CAS_TXQUEUELEN;
	IFQ_SET_READY(&ifp->if_snd);

	callout_init_mtx(&sc->sc_tick_ch, &sc->sc_mtx, 0);
	callout_init_mtx(&sc->sc_rx_ch, &sc->sc_mtx, 0);
	/* Create local taskq. */
	TASK_INIT(&sc->sc_intr_task, 0, cas_intr_task, sc);
	TASK_INIT(&sc->sc_tx_task, 1, cas_tx_task, ifp);
	sc->sc_tq = taskqueue_create_fast("cas_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->sc_tq);
	if (sc->sc_tq == NULL) {
		device_printf(sc->sc_dev, "could not create taskqueue\n");
		error = ENXIO;
		goto fail_ifnet;
	}
	error = taskqueue_start_threads(&sc->sc_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->sc_dev));
	if (error != 0) {
		device_printf(sc->sc_dev, "could not start threads\n");
		goto fail_taskq;
	}

	/* Make sure the chip is stopped. */
	cas_reset(sc);

	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev), 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE, 0, BUS_SPACE_MAXSIZE, 0, NULL, NULL,
	    &sc->sc_pdmatag);
	if (error != 0)
		goto fail_taskq;

	error = bus_dma_tag_create(sc->sc_pdmatag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    CAS_PAGE_SIZE, 1, CAS_PAGE_SIZE, 0, NULL, NULL, &sc->sc_rdmatag);
	if (error != 0)
		goto fail_ptag;

	error = bus_dma_tag_create(sc->sc_pdmatag, 1, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    MCLBYTES * CAS_NTXSEGS, CAS_NTXSEGS, MCLBYTES,
	    BUS_DMA_ALLOCNOW, NULL, NULL, &sc->sc_tdmatag);
	if (error != 0)
		goto fail_rtag;

	error = bus_dma_tag_create(sc->sc_pdmatag, CAS_TX_DESC_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL,
	    sizeof(struct cas_control_data), 1,
	    sizeof(struct cas_control_data), 0,
	    NULL, NULL, &sc->sc_cdmatag);
	if (error != 0)
		goto fail_ttag;

	/*
	 * Allocate the control data structures, create and load the
	 * DMA map for it.
	 */
	if ((error = bus_dmamem_alloc(sc->sc_cdmatag,
	    (void **)&sc->sc_control_data,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc->sc_cddmamap)) != 0) {
		device_printf(sc->sc_dev,
		    "unable to allocate control data, error = %d\n", error);
		goto fail_ctag;
	}

	sc->sc_cddma = 0;
	if ((error = bus_dmamap_load(sc->sc_cdmatag, sc->sc_cddmamap,
	    sc->sc_control_data, sizeof(struct cas_control_data),
	    cas_cddma_callback, sc, 0)) != 0 || sc->sc_cddma == 0) {
		device_printf(sc->sc_dev,
		    "unable to load control data DMA map, error = %d\n",
		    error);
		goto fail_cmem;
	}

	/*
	 * Initialize the transmit job descriptors.
	 */
	STAILQ_INIT(&sc->sc_txfreeq);
	STAILQ_INIT(&sc->sc_txdirtyq);

	/*
	 * Create the transmit buffer DMA maps.
	 */
	error = ENOMEM;
	for (i = 0; i < CAS_TXQUEUELEN; i++) {
		txs = &sc->sc_txsoft[i];
		txs->txs_mbuf = NULL;
		txs->txs_ndescs = 0;
		if ((error = bus_dmamap_create(sc->sc_tdmatag, 0,
		    &txs->txs_dmamap)) != 0) {
			device_printf(sc->sc_dev,
			    "unable to create TX DMA map %d, error = %d\n",
			    i, error);
			goto fail_txd;
		}
		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}

	/*
	 * Allocate the receive buffers, create and load the DMA maps
	 * for them.
	 */
	for (i = 0; i < CAS_NRXDESC; i++) {
		if ((error = bus_dmamem_alloc(sc->sc_rdmatag,
		    &sc->sc_rxdsoft[i].rxds_buf, BUS_DMA_WAITOK,
		    &sc->sc_rxdsoft[i].rxds_dmamap)) != 0) {
			device_printf(sc->sc_dev,
			    "unable to allocate RX buffer %d, error = %d\n",
			    i, error);
			goto fail_rxmem;
		}

		sc->sc_rxdptr = i;
		sc->sc_rxdsoft[i].rxds_paddr = 0;
		if ((error = bus_dmamap_load(sc->sc_rdmatag,
		    sc->sc_rxdsoft[i].rxds_dmamap, sc->sc_rxdsoft[i].rxds_buf,
		    CAS_PAGE_SIZE, cas_rxdma_callback, sc, 0)) != 0 ||
		    sc->sc_rxdsoft[i].rxds_paddr == 0) {
			device_printf(sc->sc_dev,
			    "unable to load RX DMA map %d, error = %d\n",
			    i, error);
			goto fail_rxmap;
		}
	}

	if ((sc->sc_flags & CAS_SERDES) == 0) {
		CAS_WRITE_4(sc, CAS_PCS_DATAPATH, CAS_PCS_DATAPATH_MII);
		CAS_BARRIER(sc, CAS_PCS_DATAPATH, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		cas_mifinit(sc);
		/*
		 * Look for an external PHY.
		 */
		error = ENXIO;
		v = CAS_READ_4(sc, CAS_MIF_CONF);
		if ((v & CAS_MIF_CONF_MDI1) != 0) {
			v |= CAS_MIF_CONF_PHY_SELECT;
			CAS_WRITE_4(sc, CAS_MIF_CONF, v);
			CAS_BARRIER(sc, CAS_MIF_CONF, 4,
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			/* Enable/unfreeze the GMII pins of Saturn. */
			if (sc->sc_variant == CAS_SATURN) {
				CAS_WRITE_4(sc, CAS_SATURN_PCFG,
				    CAS_READ_4(sc, CAS_SATURN_PCFG) &
				    ~CAS_SATURN_PCFG_FSI);
				CAS_BARRIER(sc, CAS_SATURN_PCFG, 4,
				    BUS_SPACE_BARRIER_READ |
				    BUS_SPACE_BARRIER_WRITE);
				DELAY(10000);
			}
			error = mii_attach(sc->sc_dev, &sc->sc_miibus, ifp,
			    cas_mediachange, cas_mediastatus, BMSR_DEFCAPMASK,
			    MII_PHY_ANY, MII_OFFSET_ANY, MIIF_DOPAUSE);
		}
		/*
		 * Fall back on an internal PHY if no external PHY was found.
		 */
		if (error != 0 && (v & CAS_MIF_CONF_MDI0) != 0) {
			v &= ~CAS_MIF_CONF_PHY_SELECT;
			CAS_WRITE_4(sc, CAS_MIF_CONF, v);
			CAS_BARRIER(sc, CAS_MIF_CONF, 4,
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			/* Freeze the GMII pins of Saturn for saving power. */
			if (sc->sc_variant == CAS_SATURN) {
				CAS_WRITE_4(sc, CAS_SATURN_PCFG,
				    CAS_READ_4(sc, CAS_SATURN_PCFG) |
				    CAS_SATURN_PCFG_FSI);
				CAS_BARRIER(sc, CAS_SATURN_PCFG, 4,
				    BUS_SPACE_BARRIER_READ |
				    BUS_SPACE_BARRIER_WRITE);
				DELAY(10000);
			}
			error = mii_attach(sc->sc_dev, &sc->sc_miibus, ifp,
			    cas_mediachange, cas_mediastatus, BMSR_DEFCAPMASK,
			    MII_PHY_ANY, MII_OFFSET_ANY, MIIF_DOPAUSE);
		}
	} else {
		/*
		 * Use the external PCS SERDES.
		 */
		CAS_WRITE_4(sc, CAS_PCS_DATAPATH, CAS_PCS_DATAPATH_SERDES);
		CAS_BARRIER(sc, CAS_PCS_DATAPATH, 4, BUS_SPACE_BARRIER_WRITE);
		/* Enable/unfreeze the SERDES pins of Saturn. */
		if (sc->sc_variant == CAS_SATURN) {
			CAS_WRITE_4(sc, CAS_SATURN_PCFG, 0);
			CAS_BARRIER(sc, CAS_SATURN_PCFG, 4,
			    BUS_SPACE_BARRIER_WRITE);
		}
		CAS_WRITE_4(sc, CAS_PCS_SERDES_CTRL, CAS_PCS_SERDES_CTRL_ESD);
		CAS_BARRIER(sc, CAS_PCS_SERDES_CTRL, 4,
		    BUS_SPACE_BARRIER_WRITE);
		CAS_WRITE_4(sc, CAS_PCS_CONF, CAS_PCS_CONF_EN);
		CAS_BARRIER(sc, CAS_PCS_CONF, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		error = mii_attach(sc->sc_dev, &sc->sc_miibus, ifp,
		    cas_mediachange, cas_mediastatus, BMSR_DEFCAPMASK,
		    CAS_PHYAD_EXTERNAL, MII_OFFSET_ANY, MIIF_DOPAUSE);
	}
	if (error != 0) {
		device_printf(sc->sc_dev, "attaching PHYs failed\n");
		goto fail_rxmap;
	}
	sc->sc_mii = device_get_softc(sc->sc_miibus);

	/*
	 * From this point forward, the attachment cannot fail.  A failure
	 * before this point releases all resources that may have been
	 * allocated.
	 */

	/* Announce FIFO sizes. */
	v = CAS_READ_4(sc, CAS_TX_FIFO_SIZE);
	device_printf(sc->sc_dev, "%ukB RX FIFO, %ukB TX FIFO\n",
	    CAS_RX_FIFO_SIZE / 1024, v / 16);

	/* Attach the interface. */
	ether_ifattach(ifp, sc->sc_enaddr);

	/*
	 * Tell the upper layer(s) we support long frames/checksum offloads.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities = IFCAP_VLAN_MTU;
	if ((sc->sc_flags & CAS_NO_CSUM) == 0) {
		ifp->if_capabilities |= IFCAP_HWCSUM;
		ifp->if_hwassist = CAS_CSUM_FEATURES;
	}
	ifp->if_capenable = ifp->if_capabilities;

	return (0);

	/*
	 * Free any resources we've allocated during the failed attach
	 * attempt.  Do this in reverse order and fall through.
	 */
 fail_rxmap:
	for (i = 0; i < CAS_NRXDESC; i++)
		if (sc->sc_rxdsoft[i].rxds_paddr != 0)
			bus_dmamap_unload(sc->sc_rdmatag,
			    sc->sc_rxdsoft[i].rxds_dmamap);
 fail_rxmem:
	for (i = 0; i < CAS_NRXDESC; i++)
		if (sc->sc_rxdsoft[i].rxds_buf != NULL)
			bus_dmamem_free(sc->sc_rdmatag,
			    sc->sc_rxdsoft[i].rxds_buf,
			    sc->sc_rxdsoft[i].rxds_dmamap);
 fail_txd:
	for (i = 0; i < CAS_TXQUEUELEN; i++)
		if (sc->sc_txsoft[i].txs_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_tdmatag,
			    sc->sc_txsoft[i].txs_dmamap);
	bus_dmamap_unload(sc->sc_cdmatag, sc->sc_cddmamap);
 fail_cmem:
	bus_dmamem_free(sc->sc_cdmatag, sc->sc_control_data,
	    sc->sc_cddmamap);
 fail_ctag:
	bus_dma_tag_destroy(sc->sc_cdmatag);
 fail_ttag:
	bus_dma_tag_destroy(sc->sc_tdmatag);
 fail_rtag:
	bus_dma_tag_destroy(sc->sc_rdmatag);
 fail_ptag:
	bus_dma_tag_destroy(sc->sc_pdmatag);
 fail_taskq:
	taskqueue_free(sc->sc_tq);
 fail_ifnet:
	if_free(ifp);
	return (error);
}

static void
cas_detach(struct cas_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	int i;

	ether_ifdetach(ifp);
	CAS_LOCK(sc);
	cas_stop(ifp);
	CAS_UNLOCK(sc);
	callout_drain(&sc->sc_tick_ch);
	callout_drain(&sc->sc_rx_ch);
	taskqueue_drain(sc->sc_tq, &sc->sc_intr_task);
	taskqueue_drain(sc->sc_tq, &sc->sc_tx_task);
	if_free(ifp);
	taskqueue_free(sc->sc_tq);
	device_delete_child(sc->sc_dev, sc->sc_miibus);

	for (i = 0; i < CAS_NRXDESC; i++)
		if (sc->sc_rxdsoft[i].rxds_dmamap != NULL)
			bus_dmamap_sync(sc->sc_rdmatag,
			    sc->sc_rxdsoft[i].rxds_dmamap,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (i = 0; i < CAS_NRXDESC; i++)
		if (sc->sc_rxdsoft[i].rxds_paddr != 0)
			bus_dmamap_unload(sc->sc_rdmatag,
			    sc->sc_rxdsoft[i].rxds_dmamap);
	for (i = 0; i < CAS_NRXDESC; i++)
		if (sc->sc_rxdsoft[i].rxds_buf != NULL)
			bus_dmamem_free(sc->sc_rdmatag,
			    sc->sc_rxdsoft[i].rxds_buf,
			    sc->sc_rxdsoft[i].rxds_dmamap);
	for (i = 0; i < CAS_TXQUEUELEN; i++)
		if (sc->sc_txsoft[i].txs_dmamap != NULL)
			bus_dmamap_destroy(sc->sc_tdmatag,
			    sc->sc_txsoft[i].txs_dmamap);
	CAS_CDSYNC(sc, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_cdmatag, sc->sc_cddmamap);
	bus_dmamem_free(sc->sc_cdmatag, sc->sc_control_data,
	    sc->sc_cddmamap);
	bus_dma_tag_destroy(sc->sc_cdmatag);
	bus_dma_tag_destroy(sc->sc_tdmatag);
	bus_dma_tag_destroy(sc->sc_rdmatag);
	bus_dma_tag_destroy(sc->sc_pdmatag);
}

static void
cas_suspend(struct cas_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;

	CAS_LOCK(sc);
	cas_stop(ifp);
	CAS_UNLOCK(sc);
}

static void
cas_resume(struct cas_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;

	CAS_LOCK(sc);
	/*
	 * On resume all registers have to be initialized again like
	 * after power-on.
	 */
	sc->sc_flags &= ~CAS_INITED;
	if (ifp->if_flags & IFF_UP)
		cas_init_locked(sc);
	CAS_UNLOCK(sc);
}

static inline void
cas_rxcksum(struct mbuf *m, uint16_t cksum)
{
	struct ether_header *eh;
	struct ip *ip;
	struct udphdr *uh;
	uint16_t *opts;
	int32_t hlen, len, pktlen;
	uint32_t temp32;

	pktlen = m->m_pkthdr.len;
	if (pktlen < sizeof(struct ether_header) + sizeof(struct ip))
		return;
	eh = mtod(m, struct ether_header *);
	if (eh->ether_type != htons(ETHERTYPE_IP))
		return;
	ip = (struct ip *)(eh + 1);
	if (ip->ip_v != IPVERSION)
		return;

	hlen = ip->ip_hl << 2;
	pktlen -= sizeof(struct ether_header);
	if (hlen < sizeof(struct ip))
		return;
	if (ntohs(ip->ip_len) < hlen)
		return;
	if (ntohs(ip->ip_len) != pktlen)
		return;
	if (ip->ip_off & htons(IP_MF | IP_OFFMASK))
		return;	/* Cannot handle fragmented packet. */

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		if (pktlen < (hlen + sizeof(struct tcphdr)))
			return;
		break;
	case IPPROTO_UDP:
		if (pktlen < (hlen + sizeof(struct udphdr)))
			return;
		uh = (struct udphdr *)((uint8_t *)ip + hlen);
		if (uh->uh_sum == 0)
			return; /* no checksum */
		break;
	default:
		return;
	}

	cksum = ~cksum;
	/* checksum fixup for IP options */
	len = hlen - sizeof(struct ip);
	if (len > 0) {
		opts = (uint16_t *)(ip + 1);
		for (; len > 0; len -= sizeof(uint16_t), opts++) {
			temp32 = cksum - *opts;
			temp32 = (temp32 >> 16) + (temp32 & 65535);
			cksum = temp32 & 65535;
		}
	}
	m->m_pkthdr.csum_flags |= CSUM_DATA_VALID;
	m->m_pkthdr.csum_data = cksum;
}

static void
cas_cddma_callback(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct cas_softc *sc = xsc;

	if (error != 0)
		return;
	if (nsegs != 1)
		panic("%s: bad control buffer segment count", __func__);
	sc->sc_cddma = segs[0].ds_addr;
}

static void
cas_rxdma_callback(void *xsc, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct cas_softc *sc = xsc;

	if (error != 0)
		return;
	if (nsegs != 1)
		panic("%s: bad RX buffer segment count", __func__);
	sc->sc_rxdsoft[sc->sc_rxdptr].rxds_paddr = segs[0].ds_addr;
}

static void
cas_tick(void *arg)
{
	struct cas_softc *sc = arg;
	struct ifnet *ifp = sc->sc_ifp;
	uint32_t v;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * Unload collision and error counters.
	 */
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
	    CAS_READ_4(sc, CAS_MAC_NORM_COLL_CNT) +
	    CAS_READ_4(sc, CAS_MAC_FIRST_COLL_CNT));
	v = CAS_READ_4(sc, CAS_MAC_EXCESS_COLL_CNT) +
	    CAS_READ_4(sc, CAS_MAC_LATE_COLL_CNT);
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, v);
	if_inc_counter(ifp, IFCOUNTER_OERRORS, v);
	if_inc_counter(ifp, IFCOUNTER_IERRORS,
	    CAS_READ_4(sc, CAS_MAC_RX_LEN_ERR_CNT) +
	    CAS_READ_4(sc, CAS_MAC_RX_ALIGN_ERR) +
	    CAS_READ_4(sc, CAS_MAC_RX_CRC_ERR_CNT) +
	    CAS_READ_4(sc, CAS_MAC_RX_CODE_VIOL));

	/*
	 * Then clear the hardware counters.
	 */
	CAS_WRITE_4(sc, CAS_MAC_NORM_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_FIRST_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_EXCESS_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_LATE_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_LEN_ERR_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_ALIGN_ERR, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_CRC_ERR_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_CODE_VIOL, 0);

	mii_tick(sc->sc_mii);

	if (sc->sc_txfree != CAS_MAXTXFREE)
		cas_tint(sc);

	cas_watchdog(sc);

	callout_reset(&sc->sc_tick_ch, hz, cas_tick, sc);
}

static int
cas_bitwait(struct cas_softc *sc, bus_addr_t r, uint32_t clr, uint32_t set)
{
	int i;
	uint32_t reg;

	for (i = CAS_TRIES; i--; DELAY(100)) {
		reg = CAS_READ_4(sc, r);
		if ((reg & clr) == 0 && (reg & set) == set)
			return (1);
	}
	return (0);
}

static void
cas_reset(struct cas_softc *sc)
{

#ifdef CAS_DEBUG
	CTR2(KTR_CAS, "%s: %s", device_get_name(sc->sc_dev), __func__);
#endif
	/* Disable all interrupts in order to avoid spurious ones. */
	CAS_WRITE_4(sc, CAS_INTMASK, 0xffffffff);

	cas_reset_rx(sc);
	cas_reset_tx(sc);

	/*
	 * Do a full reset modulo the result of the last auto-negotiation
	 * when using the SERDES.
	 */
	CAS_WRITE_4(sc, CAS_RESET, CAS_RESET_RX | CAS_RESET_TX |
	    ((sc->sc_flags & CAS_SERDES) != 0 ? CAS_RESET_PCS_DIS : 0));
	CAS_BARRIER(sc, CAS_RESET, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	DELAY(3000);
	if (!cas_bitwait(sc, CAS_RESET, CAS_RESET_RX | CAS_RESET_TX, 0))
		device_printf(sc->sc_dev, "cannot reset device\n");
}

static void
cas_stop(struct ifnet *ifp)
{
	struct cas_softc *sc = ifp->if_softc;
	struct cas_txsoft *txs;

#ifdef CAS_DEBUG
	CTR2(KTR_CAS, "%s: %s", device_get_name(sc->sc_dev), __func__);
#endif

	callout_stop(&sc->sc_tick_ch);
	callout_stop(&sc->sc_rx_ch);

	/* Disable all interrupts in order to avoid spurious ones. */
	CAS_WRITE_4(sc, CAS_INTMASK, 0xffffffff);

	cas_reset_tx(sc);
	cas_reset_rx(sc);

	/*
	 * Release any queued transmit buffers.
	 */
	while ((txs = STAILQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
		STAILQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);
		if (txs->txs_ndescs != 0) {
			bus_dmamap_sync(sc->sc_tdmatag, txs->txs_dmamap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_tdmatag, txs->txs_dmamap);
			if (txs->txs_mbuf != NULL) {
				m_freem(txs->txs_mbuf);
				txs->txs_mbuf = NULL;
			}
		}
		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_flags &= ~CAS_LINK;
	sc->sc_wdog_timer = 0;
}

static int
cas_reset_rx(struct cas_softc *sc)
{

	/*
	 * Resetting while DMA is in progress can cause a bus hang, so we
	 * disable DMA first.
	 */
	(void)cas_disable_rx(sc);
	CAS_WRITE_4(sc, CAS_RX_CONF, 0);
	CAS_BARRIER(sc, CAS_RX_CONF, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (!cas_bitwait(sc, CAS_RX_CONF, CAS_RX_CONF_RXDMA_EN, 0))
		device_printf(sc->sc_dev, "cannot disable RX DMA\n");

	/* Finally, reset the ERX. */
	CAS_WRITE_4(sc, CAS_RESET, CAS_RESET_RX |
	    ((sc->sc_flags & CAS_SERDES) != 0 ? CAS_RESET_PCS_DIS : 0));
	CAS_BARRIER(sc, CAS_RESET, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (!cas_bitwait(sc, CAS_RESET, CAS_RESET_RX, 0)) {
		device_printf(sc->sc_dev, "cannot reset receiver\n");
		return (1);
	}
	return (0);
}

static int
cas_reset_tx(struct cas_softc *sc)
{

	/*
	 * Resetting while DMA is in progress can cause a bus hang, so we
	 * disable DMA first.
	 */
	(void)cas_disable_tx(sc);
	CAS_WRITE_4(sc, CAS_TX_CONF, 0);
	CAS_BARRIER(sc, CAS_TX_CONF, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (!cas_bitwait(sc, CAS_TX_CONF, CAS_TX_CONF_TXDMA_EN, 0))
		device_printf(sc->sc_dev, "cannot disable TX DMA\n");

	/* Finally, reset the ETX. */
	CAS_WRITE_4(sc, CAS_RESET, CAS_RESET_TX |
	    ((sc->sc_flags & CAS_SERDES) != 0 ? CAS_RESET_PCS_DIS : 0));
	CAS_BARRIER(sc, CAS_RESET, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (!cas_bitwait(sc, CAS_RESET, CAS_RESET_TX, 0)) {
		device_printf(sc->sc_dev, "cannot reset transmitter\n");
		return (1);
	}
	return (0);
}

static int
cas_disable_rx(struct cas_softc *sc)
{

	CAS_WRITE_4(sc, CAS_MAC_RX_CONF,
	    CAS_READ_4(sc, CAS_MAC_RX_CONF) & ~CAS_MAC_RX_CONF_EN);
	CAS_BARRIER(sc, CAS_MAC_RX_CONF, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (cas_bitwait(sc, CAS_MAC_RX_CONF, CAS_MAC_RX_CONF_EN, 0))
		return (1);
	if (bootverbose)
		device_printf(sc->sc_dev, "cannot disable RX MAC\n");
	return (0);
}

static int
cas_disable_tx(struct cas_softc *sc)
{

	CAS_WRITE_4(sc, CAS_MAC_TX_CONF,
	    CAS_READ_4(sc, CAS_MAC_TX_CONF) & ~CAS_MAC_TX_CONF_EN);
	CAS_BARRIER(sc, CAS_MAC_TX_CONF, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (cas_bitwait(sc, CAS_MAC_TX_CONF, CAS_MAC_TX_CONF_EN, 0))
		return (1);
	if (bootverbose)
		device_printf(sc->sc_dev, "cannot disable TX MAC\n");
	return (0);
}

static inline void
cas_rxcompinit(struct cas_rx_comp *rxcomp)
{

	rxcomp->crc_word1 = 0;
	rxcomp->crc_word2 = 0;
	rxcomp->crc_word3 =
	    htole64(CAS_SET(ETHER_HDR_LEN + sizeof(struct ip), CAS_RC3_CSO));
	rxcomp->crc_word4 = htole64(CAS_RC4_ZERO);
}

static void
cas_meminit(struct cas_softc *sc)
{
	int i;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * Initialize the transmit descriptor ring.
	 */
	for (i = 0; i < CAS_NTXDESC; i++) {
		sc->sc_txdescs[i].cd_flags = 0;
		sc->sc_txdescs[i].cd_buf_ptr = 0;
	}
	sc->sc_txfree = CAS_MAXTXFREE;
	sc->sc_txnext = 0;
	sc->sc_txwin = 0;

	/*
	 * Initialize the receive completion ring.
	 */
	for (i = 0; i < CAS_NRXCOMP; i++)
		cas_rxcompinit(&sc->sc_rxcomps[i]);
	sc->sc_rxcptr = 0;

	/*
	 * Initialize the first receive descriptor ring.  We leave
	 * the second one zeroed as we don't actually use it.
	 */
	for (i = 0; i < CAS_NRXDESC; i++)
		CAS_INIT_RXDESC(sc, i, i);
	sc->sc_rxdptr = 0;

	CAS_CDSYNC(sc, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static u_int
cas_descsize(u_int sz)
{

	switch (sz) {
	case 32:
		return (CAS_DESC_32);
	case 64:
		return (CAS_DESC_64);
	case 128:
		return (CAS_DESC_128);
	case 256:
		return (CAS_DESC_256);
	case 512:
		return (CAS_DESC_512);
	case 1024:
		return (CAS_DESC_1K);
	case 2048:
		return (CAS_DESC_2K);
	case 4096:
		return (CAS_DESC_4K);
	case 8192:
		return (CAS_DESC_8K);
	default:
		printf("%s: invalid descriptor ring size %d\n", __func__, sz);
		return (CAS_DESC_32);
	}
}

static u_int
cas_rxcompsize(u_int sz)
{

	switch (sz) {
	case 128:
		return (CAS_RX_CONF_COMP_128);
	case 256:
		return (CAS_RX_CONF_COMP_256);
	case 512:
		return (CAS_RX_CONF_COMP_512);
	case 1024:
		return (CAS_RX_CONF_COMP_1K);
	case 2048:
		return (CAS_RX_CONF_COMP_2K);
	case 4096:
		return (CAS_RX_CONF_COMP_4K);
	case 8192:
		return (CAS_RX_CONF_COMP_8K);
	case 16384:
		return (CAS_RX_CONF_COMP_16K);
	case 32768:
		return (CAS_RX_CONF_COMP_32K);
	default:
		printf("%s: invalid dcompletion ring size %d\n", __func__, sz);
		return (CAS_RX_CONF_COMP_128);
	}
}

static void
cas_init(void *xsc)
{
	struct cas_softc *sc = xsc;

	CAS_LOCK(sc);
	cas_init_locked(sc);
	CAS_UNLOCK(sc);
}

/*
 * Initialization of interface; set up initialization block
 * and transmit/receive descriptor rings.
 */
static void
cas_init_locked(struct cas_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	uint32_t v;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

#ifdef CAS_DEBUG
	CTR2(KTR_CAS, "%s: %s: calling stop", device_get_name(sc->sc_dev),
	    __func__);
#endif
	/*
	 * Initialization sequence.  The numbered steps below correspond
	 * to the sequence outlined in section 6.3.5.1 in the Ethernet
	 * Channel Engine manual (part of the PCIO manual).
	 * See also the STP2002-STQ document from Sun Microsystems.
	 */

	/* step 1 & 2.  Reset the Ethernet Channel. */
	cas_stop(ifp);
	cas_reset(sc);
#ifdef CAS_DEBUG
	CTR2(KTR_CAS, "%s: %s: restarting", device_get_name(sc->sc_dev),
	    __func__);
#endif

	if ((sc->sc_flags & CAS_SERDES) == 0)
		/* Re-initialize the MIF. */
		cas_mifinit(sc);

	/* step 3.  Setup data structures in host memory. */
	cas_meminit(sc);

	/* step 4.  TX MAC registers & counters */
	cas_init_regs(sc);

	/* step 5.  RX MAC registers & counters */

	/* step 6 & 7.  Program Ring Base Addresses. */
	CAS_WRITE_4(sc, CAS_TX_DESC3_BASE_HI,
	    (((uint64_t)CAS_CDTXDADDR(sc, 0)) >> 32));
	CAS_WRITE_4(sc, CAS_TX_DESC3_BASE_LO,
	    CAS_CDTXDADDR(sc, 0) & 0xffffffff);

	CAS_WRITE_4(sc, CAS_RX_COMP_BASE_HI,
	    (((uint64_t)CAS_CDRXCADDR(sc, 0)) >> 32));
	CAS_WRITE_4(sc, CAS_RX_COMP_BASE_LO,
	    CAS_CDRXCADDR(sc, 0) & 0xffffffff);

	CAS_WRITE_4(sc, CAS_RX_DESC_BASE_HI,
	    (((uint64_t)CAS_CDRXDADDR(sc, 0)) >> 32));
	CAS_WRITE_4(sc, CAS_RX_DESC_BASE_LO,
	    CAS_CDRXDADDR(sc, 0) & 0xffffffff);

	if ((sc->sc_flags & CAS_REG_PLUS) != 0) {
		CAS_WRITE_4(sc, CAS_RX_DESC2_BASE_HI,
		    (((uint64_t)CAS_CDRXD2ADDR(sc, 0)) >> 32));
		CAS_WRITE_4(sc, CAS_RX_DESC2_BASE_LO,
		    CAS_CDRXD2ADDR(sc, 0) & 0xffffffff);
	}

#ifdef CAS_DEBUG
	CTR5(KTR_CAS,
	    "loading TXDR %lx, RXCR %lx, RXDR %lx, RXD2R %lx, cddma %lx",
	    CAS_CDTXDADDR(sc, 0), CAS_CDRXCADDR(sc, 0), CAS_CDRXDADDR(sc, 0),
	    CAS_CDRXD2ADDR(sc, 0), sc->sc_cddma);
#endif

	/* step 8.  Global Configuration & Interrupt Masks */

	/* Disable weighted round robin. */
	CAS_WRITE_4(sc, CAS_CAW, CAS_CAW_RR_DIS);

	/*
	 * Enable infinite bursts for revisions without PCI issues if
	 * applicable.  Doing so greatly improves the TX performance on
	 * !__sparc64__ (on sparc64, setting CAS_INF_BURST improves TX
	 * performance only marginally but hurts RX throughput quite a bit).
	 */
	CAS_WRITE_4(sc, CAS_INF_BURST,
#if !defined(__sparc64__)
	    (sc->sc_flags & CAS_TABORT) == 0 ? CAS_INF_BURST_EN :
#endif
	    0);

	/* Set up interrupts. */
	CAS_WRITE_4(sc, CAS_INTMASK,
	    ~(CAS_INTR_TX_INT_ME | CAS_INTR_TX_TAG_ERR |
	    CAS_INTR_RX_DONE | CAS_INTR_RX_BUF_NA | CAS_INTR_RX_TAG_ERR |
	    CAS_INTR_RX_COMP_FULL | CAS_INTR_RX_BUF_AEMPTY |
	    CAS_INTR_RX_COMP_AFULL | CAS_INTR_RX_LEN_MMATCH |
	    CAS_INTR_PCI_ERROR_INT
#ifdef CAS_DEBUG
	    | CAS_INTR_PCS_INT | CAS_INTR_MIF
#endif
	    ));
	/* Don't clear top level interrupts when CAS_STATUS_ALIAS is read. */
	CAS_WRITE_4(sc, CAS_CLEAR_ALIAS, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_MASK, ~CAS_MAC_RX_OVERFLOW);
	CAS_WRITE_4(sc, CAS_MAC_TX_MASK,
	    ~(CAS_MAC_TX_UNDERRUN | CAS_MAC_TX_MAX_PKT_ERR));
#ifdef CAS_DEBUG
	CAS_WRITE_4(sc, CAS_MAC_CTRL_MASK,
	    ~(CAS_MAC_CTRL_PAUSE_RCVD | CAS_MAC_CTRL_PAUSE |
	    CAS_MAC_CTRL_NON_PAUSE));
#else
	CAS_WRITE_4(sc, CAS_MAC_CTRL_MASK,
	    CAS_MAC_CTRL_PAUSE_RCVD | CAS_MAC_CTRL_PAUSE |
	    CAS_MAC_CTRL_NON_PAUSE);
#endif

	/* Enable PCI error interrupts. */
	CAS_WRITE_4(sc, CAS_ERROR_MASK,
	    ~(CAS_ERROR_DTRTO | CAS_ERROR_OTHER | CAS_ERROR_DMAW_ZERO |
	    CAS_ERROR_DMAR_ZERO | CAS_ERROR_RTRTO));

	/* Enable PCI error interrupts in BIM configuration. */
	CAS_WRITE_4(sc, CAS_BIM_CONF,
	    CAS_BIM_CONF_DPAR_EN | CAS_BIM_CONF_RMA_EN | CAS_BIM_CONF_RTA_EN);

	/*
	 * step 9.  ETX Configuration: encode receive descriptor ring size,
	 * enable DMA and disable pre-interrupt writeback completion.
	 */
	v = cas_descsize(CAS_NTXDESC) << CAS_TX_CONF_DESC3_SHFT;
	CAS_WRITE_4(sc, CAS_TX_CONF, v | CAS_TX_CONF_TXDMA_EN |
	    CAS_TX_CONF_RDPP_DIS | CAS_TX_CONF_PICWB_DIS);

	/* step 10.  ERX Configuration */

	/*
	 * Encode receive completion and descriptor ring sizes, set the
	 * swivel offset.
	 */
	v = cas_rxcompsize(CAS_NRXCOMP) << CAS_RX_CONF_COMP_SHFT;
	v |= cas_descsize(CAS_NRXDESC) << CAS_RX_CONF_DESC_SHFT;
	if ((sc->sc_flags & CAS_REG_PLUS) != 0)
		v |= cas_descsize(CAS_NRXDESC2) << CAS_RX_CONF_DESC2_SHFT;
	CAS_WRITE_4(sc, CAS_RX_CONF,
	    v | (ETHER_ALIGN << CAS_RX_CONF_SOFF_SHFT));

	/* Set the PAUSE thresholds.  We use the maximum OFF threshold. */
	CAS_WRITE_4(sc, CAS_RX_PTHRS,
	    (111 << CAS_RX_PTHRS_XOFF_SHFT) | (15 << CAS_RX_PTHRS_XON_SHFT));

	/* RX blanking */
	CAS_WRITE_4(sc, CAS_RX_BLANK,
	    (15 << CAS_RX_BLANK_TIME_SHFT) | (5 << CAS_RX_BLANK_PKTS_SHFT));

	/* Set RX_COMP_AFULL threshold to half of the RX completions. */
	CAS_WRITE_4(sc, CAS_RX_AEMPTY_THRS,
	    (CAS_NRXCOMP / 2) << CAS_RX_AEMPTY_COMP_SHFT);

	/* Initialize the RX page size register as appropriate for 8k. */
	CAS_WRITE_4(sc, CAS_RX_PSZ,
	    (CAS_RX_PSZ_8K << CAS_RX_PSZ_SHFT) |
	    (4 << CAS_RX_PSZ_MB_CNT_SHFT) |
	    (CAS_RX_PSZ_MB_STRD_2K << CAS_RX_PSZ_MB_STRD_SHFT) |
	    (CAS_RX_PSZ_MB_OFF_64 << CAS_RX_PSZ_MB_OFF_SHFT));

	/* Disable RX random early detection. */
	CAS_WRITE_4(sc,	CAS_RX_RED, 0);

	/* Zero the RX reassembly DMA table. */
	for (v = 0; v <= CAS_RX_REAS_DMA_ADDR_LC; v++) {
		CAS_WRITE_4(sc,	CAS_RX_REAS_DMA_ADDR, v);
		CAS_WRITE_4(sc,	CAS_RX_REAS_DMA_DATA_LO, 0);
		CAS_WRITE_4(sc,	CAS_RX_REAS_DMA_DATA_MD, 0);
		CAS_WRITE_4(sc,	CAS_RX_REAS_DMA_DATA_HI, 0);
	}

	/* Ensure the RX control FIFO and RX IPP FIFO addresses are zero. */
	CAS_WRITE_4(sc, CAS_RX_CTRL_FIFO, 0);
	CAS_WRITE_4(sc, CAS_RX_IPP_ADDR, 0);

	/* Finally, enable RX DMA. */
	CAS_WRITE_4(sc, CAS_RX_CONF,
	    CAS_READ_4(sc, CAS_RX_CONF) | CAS_RX_CONF_RXDMA_EN);

	/* step 11.  Configure Media. */

	/* step 12.  RX_MAC Configuration Register */
	v = CAS_READ_4(sc, CAS_MAC_RX_CONF);
	v &= ~(CAS_MAC_RX_CONF_STRPPAD | CAS_MAC_RX_CONF_EN);
	v |= CAS_MAC_RX_CONF_STRPFCS;
	sc->sc_mac_rxcfg = v;
	/*
	 * Clear the RX filter and reprogram it.  This will also set the
	 * current RX MAC configuration and enable it.
	 */
	cas_setladrf(sc);

	/* step 13.  TX_MAC Configuration Register */
	v = CAS_READ_4(sc, CAS_MAC_TX_CONF);
	v |= CAS_MAC_TX_CONF_EN;
	(void)cas_disable_tx(sc);
	CAS_WRITE_4(sc, CAS_MAC_TX_CONF, v);

	/* step 14.  Issue Transmit Pending command. */

	/* step 15.  Give the receiver a swift kick. */
	CAS_WRITE_4(sc, CAS_RX_KICK, CAS_NRXDESC - 4);
	CAS_WRITE_4(sc, CAS_RX_COMP_TAIL, 0);
	if ((sc->sc_flags & CAS_REG_PLUS) != 0)
		CAS_WRITE_4(sc, CAS_RX_KICK2, CAS_NRXDESC2 - 4);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	mii_mediachg(sc->sc_mii);

	/* Start the one second timer. */
	sc->sc_wdog_timer = 0;
	callout_reset(&sc->sc_tick_ch, hz, cas_tick, sc);
}

static int
cas_load_txmbuf(struct cas_softc *sc, struct mbuf **m_head)
{
	bus_dma_segment_t txsegs[CAS_NTXSEGS];
	struct cas_txsoft *txs;
	struct ip *ip;
	struct mbuf *m;
	uint64_t cflags;
	int error, nexttx, nsegs, offset, seg;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	/* Get a work queue entry. */
	if ((txs = STAILQ_FIRST(&sc->sc_txfreeq)) == NULL) {
		/* Ran out of descriptors. */
		return (ENOBUFS);
	}

	cflags = 0;
	if (((*m_head)->m_pkthdr.csum_flags & CAS_CSUM_FEATURES) != 0) {
		if (M_WRITABLE(*m_head) == 0) {
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			*m_head = m;
			if (m == NULL)
				return (ENOBUFS);
		}
		offset = sizeof(struct ether_header);
		m = m_pullup(*m_head, offset + sizeof(struct ip));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		ip = (struct ip *)(mtod(m, caddr_t) + offset);
		offset += (ip->ip_hl << 2);
		cflags = (offset << CAS_TD_CKSUM_START_SHFT) |
		    ((offset + m->m_pkthdr.csum_data) <<
		    CAS_TD_CKSUM_STUFF_SHFT) | CAS_TD_CKSUM_EN;
		*m_head = m;
	}

	error = bus_dmamap_load_mbuf_sg(sc->sc_tdmatag, txs->txs_dmamap,
	    *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, CAS_NTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->sc_tdmatag,
		    txs->txs_dmamap, *m_head, txsegs, &nsegs,
		    BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	/* If nsegs is wrong then the stack is corrupt. */
	KASSERT(nsegs <= CAS_NTXSEGS,
	    ("%s: too many DMA segments (%d)", __func__, nsegs));
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/*
	 * Ensure we have enough descriptors free to describe
	 * the packet.  Note, we always reserve one descriptor
	 * at the end of the ring as a termination point, in
	 * order to prevent wrap-around.
	 */
	if (nsegs > sc->sc_txfree - 1) {
		txs->txs_ndescs = 0;
		bus_dmamap_unload(sc->sc_tdmatag, txs->txs_dmamap);
		return (ENOBUFS);
	}

	txs->txs_ndescs = nsegs;
	txs->txs_firstdesc = sc->sc_txnext;
	nexttx = txs->txs_firstdesc;
	for (seg = 0; seg < nsegs; seg++, nexttx = CAS_NEXTTX(nexttx)) {
#ifdef CAS_DEBUG
		CTR6(KTR_CAS,
		    "%s: mapping seg %d (txd %d), len %lx, addr %#lx (%#lx)",
		    __func__, seg, nexttx, txsegs[seg].ds_len,
		    txsegs[seg].ds_addr, htole64(txsegs[seg].ds_addr));
#endif
		sc->sc_txdescs[nexttx].cd_buf_ptr =
		    htole64(txsegs[seg].ds_addr);
		KASSERT(txsegs[seg].ds_len <
		    CAS_TD_BUF_LEN_MASK >> CAS_TD_BUF_LEN_SHFT,
		    ("%s: segment size too large!", __func__));
		sc->sc_txdescs[nexttx].cd_flags =
		    htole64(txsegs[seg].ds_len << CAS_TD_BUF_LEN_SHFT);
		txs->txs_lastdesc = nexttx;
	}

	/* Set EOF on the last descriptor. */
#ifdef CAS_DEBUG
	CTR3(KTR_CAS, "%s: end of frame at segment %d, TX %d",
	    __func__, seg, nexttx);
#endif
	sc->sc_txdescs[txs->txs_lastdesc].cd_flags |=
	    htole64(CAS_TD_END_OF_FRAME);

	/* Lastly set SOF on the first descriptor. */
#ifdef CAS_DEBUG
	CTR3(KTR_CAS, "%s: start of frame at segment %d, TX %d",
	    __func__, seg, nexttx);
#endif
	if (sc->sc_txwin += nsegs > CAS_MAXTXFREE * 2 / 3) {
		sc->sc_txwin = 0;
		sc->sc_txdescs[txs->txs_firstdesc].cd_flags |=
		    htole64(cflags | CAS_TD_START_OF_FRAME | CAS_TD_INT_ME);
	} else
		sc->sc_txdescs[txs->txs_firstdesc].cd_flags |=
		    htole64(cflags | CAS_TD_START_OF_FRAME);

	/* Sync the DMA map. */
	bus_dmamap_sync(sc->sc_tdmatag, txs->txs_dmamap,
	    BUS_DMASYNC_PREWRITE);

#ifdef CAS_DEBUG
	CTR4(KTR_CAS, "%s: setting firstdesc=%d, lastdesc=%d, ndescs=%d",
	    __func__, txs->txs_firstdesc, txs->txs_lastdesc,
	    txs->txs_ndescs);
#endif
	STAILQ_REMOVE_HEAD(&sc->sc_txfreeq, txs_q);
	STAILQ_INSERT_TAIL(&sc->sc_txdirtyq, txs, txs_q);
	txs->txs_mbuf = *m_head;

	sc->sc_txnext = CAS_NEXTTX(txs->txs_lastdesc);
	sc->sc_txfree -= txs->txs_ndescs;

	return (0);
}

static void
cas_init_regs(struct cas_softc *sc)
{
	int i;
	const u_char *laddr = IF_LLADDR(sc->sc_ifp);

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	/* These registers are not cleared on reset. */
	if ((sc->sc_flags & CAS_INITED) == 0) {
		/* magic values */
		CAS_WRITE_4(sc, CAS_MAC_IPG0, 0);
		CAS_WRITE_4(sc, CAS_MAC_IPG1, 8);
		CAS_WRITE_4(sc, CAS_MAC_IPG2, 4);

		/* min frame length */
		CAS_WRITE_4(sc, CAS_MAC_MIN_FRAME, ETHER_MIN_LEN);
		/* max frame length and max burst size */
		CAS_WRITE_4(sc, CAS_MAC_MAX_BF,
		    ((ETHER_MAX_LEN_JUMBO + ETHER_VLAN_ENCAP_LEN) <<
		    CAS_MAC_MAX_BF_FRM_SHFT) |
		    (0x2000 << CAS_MAC_MAX_BF_BST_SHFT));

		/* more magic values */
		CAS_WRITE_4(sc, CAS_MAC_PREAMBLE_LEN, 0x7);
		CAS_WRITE_4(sc, CAS_MAC_JAM_SIZE, 0x4);
		CAS_WRITE_4(sc, CAS_MAC_ATTEMPT_LIMIT, 0x10);
		CAS_WRITE_4(sc, CAS_MAC_CTRL_TYPE, 0x8808);

		/* random number seed */
		CAS_WRITE_4(sc, CAS_MAC_RANDOM_SEED,
		    ((laddr[5] << 8) | laddr[4]) & 0x3ff);

		/* secondary MAC addresses: 0:0:0:0:0:0 */
		for (i = CAS_MAC_ADDR3; i <= CAS_MAC_ADDR41;
		    i += CAS_MAC_ADDR4 - CAS_MAC_ADDR3)
			CAS_WRITE_4(sc, i, 0);

		/* MAC control address: 01:80:c2:00:00:01 */
		CAS_WRITE_4(sc, CAS_MAC_ADDR42, 0x0001);
		CAS_WRITE_4(sc, CAS_MAC_ADDR43, 0xc200);
		CAS_WRITE_4(sc, CAS_MAC_ADDR44, 0x0180);

		/* MAC filter address: 0:0:0:0:0:0 */
		CAS_WRITE_4(sc, CAS_MAC_AFILTER0, 0);
		CAS_WRITE_4(sc, CAS_MAC_AFILTER1, 0);
		CAS_WRITE_4(sc, CAS_MAC_AFILTER2, 0);
		CAS_WRITE_4(sc, CAS_MAC_AFILTER_MASK1_2, 0);
		CAS_WRITE_4(sc, CAS_MAC_AFILTER_MASK0, 0);

		/* Zero the hash table. */
		for (i = CAS_MAC_HASH0; i <= CAS_MAC_HASH15;
		    i += CAS_MAC_HASH1 - CAS_MAC_HASH0)
			CAS_WRITE_4(sc, i, 0);

		sc->sc_flags |= CAS_INITED;
	}

	/* Counters need to be zeroed. */
	CAS_WRITE_4(sc, CAS_MAC_NORM_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_FIRST_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_EXCESS_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_LATE_COLL_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_DEFER_TMR_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_PEAK_ATTEMPTS, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_FRAME_COUNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_LEN_ERR_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_ALIGN_ERR, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_CRC_ERR_CNT, 0);
	CAS_WRITE_4(sc, CAS_MAC_RX_CODE_VIOL, 0);

	/* Set XOFF PAUSE time. */
	CAS_WRITE_4(sc, CAS_MAC_SPC, 0x1BF0 << CAS_MAC_SPC_TIME_SHFT);

	/* Set the station address. */
	CAS_WRITE_4(sc, CAS_MAC_ADDR0, (laddr[4] << 8) | laddr[5]);
	CAS_WRITE_4(sc, CAS_MAC_ADDR1, (laddr[2] << 8) | laddr[3]);
	CAS_WRITE_4(sc, CAS_MAC_ADDR2, (laddr[0] << 8) | laddr[1]);

	/* Enable MII outputs. */
	CAS_WRITE_4(sc, CAS_MAC_XIF_CONF, CAS_MAC_XIF_CONF_TX_OE);
}

static void
cas_tx_task(void *arg, int pending __unused)
{
	struct ifnet *ifp;

	ifp = (struct ifnet *)arg;
	cas_start(ifp);
}

static inline void
cas_txkick(struct cas_softc *sc)
{

	/*
	 * Update the TX kick register.  This register has to point to the
	 * descriptor after the last valid one and for optimum performance
	 * should be incremented in multiples of 4 (the DMA engine fetches/
	 * updates descriptors in batches of 4).
	 */
#ifdef CAS_DEBUG
	CTR3(KTR_CAS, "%s: %s: kicking TX %d",
	    device_get_name(sc->sc_dev), __func__, sc->sc_txnext);
#endif
	CAS_CDSYNC(sc, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CAS_WRITE_4(sc, CAS_TX_KICK3, sc->sc_txnext);
}

static void
cas_start(struct ifnet *ifp)
{
	struct cas_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int kicked, ntx;

	CAS_LOCK(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->sc_flags & CAS_LINK) == 0) {
		CAS_UNLOCK(sc);
		return;
	}

	if (sc->sc_txfree < CAS_MAXTXFREE / 4)
		cas_tint(sc);

#ifdef CAS_DEBUG
	CTR4(KTR_CAS, "%s: %s: txfree %d, txnext %d",
	    device_get_name(sc->sc_dev), __func__, sc->sc_txfree,
	    sc->sc_txnext);
#endif
	ntx = 0;
	kicked = 0;
	for (; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) && sc->sc_txfree > 1;) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		if (cas_load_txmbuf(sc, &m) != 0) {
			if (m == NULL)
				break;
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			IFQ_DRV_PREPEND(&ifp->if_snd, m);
			break;
		}
		if ((sc->sc_txnext % 4) == 0) {
			cas_txkick(sc);
			kicked = 1;
		} else
			kicked = 0;
		ntx++;
		BPF_MTAP(ifp, m);
	}

	if (ntx > 0) {
		if (kicked == 0)
			cas_txkick(sc);
#ifdef CAS_DEBUG
		CTR2(KTR_CAS, "%s: packets enqueued, OWN on %d",
		    device_get_name(sc->sc_dev), sc->sc_txnext);
#endif

		/* Set a watchdog timer in case the chip flakes out. */
		sc->sc_wdog_timer = 5;
#ifdef CAS_DEBUG
		CTR3(KTR_CAS, "%s: %s: watchdog %d",
		    device_get_name(sc->sc_dev), __func__,
		    sc->sc_wdog_timer);
#endif
	}

	CAS_UNLOCK(sc);
}

static void
cas_tint(struct cas_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct cas_txsoft *txs;
	int progress;
	uint32_t txlast;
#ifdef CAS_DEBUG
	int i;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	CTR2(KTR_CAS, "%s: %s", device_get_name(sc->sc_dev), __func__);
#endif

	/*
	 * Go through our TX list and free mbufs for those
	 * frames that have been transmitted.
	 */
	progress = 0;
	CAS_CDSYNC(sc, BUS_DMASYNC_POSTREAD);
	while ((txs = STAILQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
#ifdef CAS_DEBUG
		if ((ifp->if_flags & IFF_DEBUG) != 0) {
			printf("    txsoft %p transmit chain:\n", txs);
			for (i = txs->txs_firstdesc;; i = CAS_NEXTTX(i)) {
				printf("descriptor %d: ", i);
				printf("cd_flags: 0x%016llx\t",
				    (long long)le64toh(
				    sc->sc_txdescs[i].cd_flags));
				printf("cd_buf_ptr: 0x%016llx\n",
				    (long long)le64toh(
				    sc->sc_txdescs[i].cd_buf_ptr));
				if (i == txs->txs_lastdesc)
					break;
			}
		}
#endif

		/*
		 * In theory, we could harvest some descriptors before
		 * the ring is empty, but that's a bit complicated.
		 *
		 * CAS_TX_COMPn points to the last descriptor
		 * processed + 1.
		 */
		txlast = CAS_READ_4(sc, CAS_TX_COMP3);
#ifdef CAS_DEBUG
		CTR4(KTR_CAS, "%s: txs->txs_firstdesc = %d, "
		    "txs->txs_lastdesc = %d, txlast = %d",
		    __func__, txs->txs_firstdesc, txs->txs_lastdesc, txlast);
#endif
		if (txs->txs_firstdesc <= txs->txs_lastdesc) {
			if ((txlast >= txs->txs_firstdesc) &&
			    (txlast <= txs->txs_lastdesc))
				break;
		} else {
			/* Ick -- this command wraps. */
			if ((txlast >= txs->txs_firstdesc) ||
			    (txlast <= txs->txs_lastdesc))
				break;
		}

#ifdef CAS_DEBUG
		CTR1(KTR_CAS, "%s: releasing a descriptor", __func__);
#endif
		STAILQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);

		sc->sc_txfree += txs->txs_ndescs;

		bus_dmamap_sync(sc->sc_tdmatag, txs->txs_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_tdmatag, txs->txs_dmamap);
		if (txs->txs_mbuf != NULL) {
			m_freem(txs->txs_mbuf);
			txs->txs_mbuf = NULL;
		}

		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		progress = 1;
	}

#ifdef CAS_DEBUG
	CTR5(KTR_CAS, "%s: CAS_TX_SM1 %x CAS_TX_SM2 %x CAS_TX_DESC_BASE %llx "
	    "CAS_TX_COMP3 %x",
	    __func__, CAS_READ_4(sc, CAS_TX_SM1), CAS_READ_4(sc, CAS_TX_SM2),
	    ((long long)CAS_READ_4(sc, CAS_TX_DESC3_BASE_HI) << 32) |
	    CAS_READ_4(sc, CAS_TX_DESC3_BASE_LO),
	    CAS_READ_4(sc, CAS_TX_COMP3));
#endif

	if (progress) {
		/* We freed some descriptors, so reset IFF_DRV_OACTIVE. */
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if (STAILQ_EMPTY(&sc->sc_txdirtyq))
			sc->sc_wdog_timer = 0;
	}

#ifdef CAS_DEBUG
	CTR3(KTR_CAS, "%s: %s: watchdog %d",
	    device_get_name(sc->sc_dev), __func__, sc->sc_wdog_timer);
#endif
}

static void
cas_rint_timeout(void *arg)
{
	struct cas_softc *sc = arg;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	cas_rint(sc);
}

static void
cas_rint(struct cas_softc *sc)
{
	struct cas_rxdsoft *rxds, *rxds2;
	struct ifnet *ifp = sc->sc_ifp;
	struct mbuf *m, *m2;
	uint64_t word1, word2, word3, word4;
	uint32_t rxhead;
	u_int idx, idx2, len, off, skip;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	callout_stop(&sc->sc_rx_ch);

#ifdef CAS_DEBUG
	CTR2(KTR_CAS, "%s: %s", device_get_name(sc->sc_dev), __func__);
#endif

#define	PRINTWORD(n, delimiter)						\
	printf("word ## n: 0x%016llx%c", (long long)word ## n, delimiter)

#define	SKIPASSERT(n)							\
	KASSERT(sc->sc_rxcomps[sc->sc_rxcptr].crc_word ## n == 0,	\
	    ("%s: word ## n not 0", __func__))

#define	WORDTOH(n)							\
	word ## n = le64toh(sc->sc_rxcomps[sc->sc_rxcptr].crc_word ## n)

	/*
	 * Read the completion head register once.  This limits
	 * how long the following loop can execute.
	 */
	rxhead = CAS_READ_4(sc, CAS_RX_COMP_HEAD);
#ifdef CAS_DEBUG
	CTR4(KTR_CAS, "%s: sc->sc_rxcptr %d, sc->sc_rxdptr %d, head %d",
	    __func__, sc->sc_rxcptr, sc->sc_rxdptr, rxhead);
#endif
	skip = 0;
	CAS_CDSYNC(sc, BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	for (; sc->sc_rxcptr != rxhead;
	    sc->sc_rxcptr = CAS_NEXTRXCOMP(sc->sc_rxcptr)) {
		if (skip != 0) {
			SKIPASSERT(1);
			SKIPASSERT(2);
			SKIPASSERT(3);

			--skip;
			goto skip;
		}

		WORDTOH(1);
		WORDTOH(2);
		WORDTOH(3);
		WORDTOH(4);

#ifdef CAS_DEBUG
		if ((ifp->if_flags & IFF_DEBUG) != 0) {
			printf("    completion %d: ", sc->sc_rxcptr);
			PRINTWORD(1, '\t');
			PRINTWORD(2, '\t');
			PRINTWORD(3, '\t');
			PRINTWORD(4, '\n');
		}
#endif

		if (__predict_false(
		    (word1 & CAS_RC1_TYPE_MASK) == CAS_RC1_TYPE_HW ||
		    (word4 & CAS_RC4_ZERO) != 0)) {
			/*
			 * The descriptor is still marked as owned, although
			 * it is supposed to have completed.  This has been
			 * observed on some machines.  Just exiting here
			 * might leave the packet sitting around until another
			 * one arrives to trigger a new interrupt, which is
			 * generally undesirable, so set up a timeout.
			 */
			callout_reset(&sc->sc_rx_ch, CAS_RXOWN_TICKS,
			    cas_rint_timeout, sc);
			break;
		}

		if (__predict_false(
		    (word4 & (CAS_RC4_BAD | CAS_RC4_LEN_MMATCH)) != 0)) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			device_printf(sc->sc_dev,
			    "receive error: CRC error\n");
			continue;
		}

		KASSERT(CAS_GET(word1, CAS_RC1_DATA_SIZE) == 0 ||
		    CAS_GET(word2, CAS_RC2_HDR_SIZE) == 0,
		    ("%s: data and header present", __func__));
		KASSERT((word1 & CAS_RC1_SPLIT_PKT) == 0 ||
		    CAS_GET(word2, CAS_RC2_HDR_SIZE) == 0,
		    ("%s: split and header present", __func__));
		KASSERT(CAS_GET(word1, CAS_RC1_DATA_SIZE) == 0 ||
		    (word1 & CAS_RC1_RELEASE_HDR) == 0,
		    ("%s: data present but header release", __func__));
		KASSERT(CAS_GET(word2, CAS_RC2_HDR_SIZE) == 0 ||
		    (word1 & CAS_RC1_RELEASE_DATA) == 0,
		    ("%s: header present but data release", __func__));

		if ((len = CAS_GET(word2, CAS_RC2_HDR_SIZE)) != 0) {
			idx = CAS_GET(word2, CAS_RC2_HDR_INDEX);
			off = CAS_GET(word2, CAS_RC2_HDR_OFF);
#ifdef CAS_DEBUG
			CTR4(KTR_CAS, "%s: hdr at idx %d, off %d, len %d",
			    __func__, idx, off, len);
#endif
			rxds = &sc->sc_rxdsoft[idx];
			MGETHDR(m, M_NOWAIT, MT_DATA);
			if (m != NULL) {
				refcount_acquire(&rxds->rxds_refcount);
				bus_dmamap_sync(sc->sc_rdmatag,
				    rxds->rxds_dmamap, BUS_DMASYNC_POSTREAD);
				m_extadd(m, (char *)rxds->rxds_buf +
				    off * 256 + ETHER_ALIGN, len, cas_free,
				    sc, (void *)(uintptr_t)idx,
				    M_RDONLY, EXT_NET_DRV);
				if ((m->m_flags & M_EXT) == 0) {
					m_freem(m);
					m = NULL;
				}
			}
			if (m != NULL) {
				m->m_pkthdr.rcvif = ifp;
				m->m_pkthdr.len = m->m_len = len;
				if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
				if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
					cas_rxcksum(m, CAS_GET(word4,
					    CAS_RC4_TCP_CSUM));
				/* Pass it on. */
				CAS_UNLOCK(sc);
				(*ifp->if_input)(ifp, m);
				CAS_LOCK(sc);
			} else
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);

			if ((word1 & CAS_RC1_RELEASE_HDR) != 0 &&
			    refcount_release(&rxds->rxds_refcount) != 0)
				cas_add_rxdesc(sc, idx);
		} else if ((len = CAS_GET(word1, CAS_RC1_DATA_SIZE)) != 0) {
			idx = CAS_GET(word1, CAS_RC1_DATA_INDEX);
			off = CAS_GET(word1, CAS_RC1_DATA_OFF);
#ifdef CAS_DEBUG
			CTR4(KTR_CAS, "%s: data at idx %d, off %d, len %d",
			    __func__, idx, off, len);
#endif
			rxds = &sc->sc_rxdsoft[idx];
			MGETHDR(m, M_NOWAIT, MT_DATA);
			if (m != NULL) {
				refcount_acquire(&rxds->rxds_refcount);
				off += ETHER_ALIGN;
				m->m_len = min(CAS_PAGE_SIZE - off, len);
				bus_dmamap_sync(sc->sc_rdmatag,
				    rxds->rxds_dmamap, BUS_DMASYNC_POSTREAD);
				m_extadd(m, (char *)rxds->rxds_buf + off,
				    m->m_len, cas_free, sc,
				    (void *)(uintptr_t)idx, M_RDONLY,
				    EXT_NET_DRV);
				if ((m->m_flags & M_EXT) == 0) {
					m_freem(m);
					m = NULL;
				}
			}
			idx2 = 0;
			m2 = NULL;
			rxds2 = NULL;
			if ((word1 & CAS_RC1_SPLIT_PKT) != 0) {
				KASSERT((word1 & CAS_RC1_RELEASE_NEXT) != 0,
				    ("%s: split but no release next",
				    __func__));

				idx2 = CAS_GET(word2, CAS_RC2_NEXT_INDEX);
#ifdef CAS_DEBUG
				CTR2(KTR_CAS, "%s: split at idx %d",
				    __func__, idx2);
#endif
				rxds2 = &sc->sc_rxdsoft[idx2];
				if (m != NULL) {
					MGET(m2, M_NOWAIT, MT_DATA);
					if (m2 != NULL) {
						refcount_acquire(
						    &rxds2->rxds_refcount);
						m2->m_len = len - m->m_len;
						bus_dmamap_sync(
						    sc->sc_rdmatag,
						    rxds2->rxds_dmamap,
						    BUS_DMASYNC_POSTREAD);
						m_extadd(m2,
						    (char *)rxds2->rxds_buf,
						    m2->m_len, cas_free, sc,
						    (void *)(uintptr_t)idx2,
						    M_RDONLY, EXT_NET_DRV);
						if ((m2->m_flags & M_EXT) ==
						    0) {
							m_freem(m2);
							m2 = NULL;
						}
					}
				}
				if (m2 != NULL)
					m->m_next = m2;
				else if (m != NULL) {
					m_freem(m);
					m = NULL;
				}
			}
			if (m != NULL) {
				m->m_pkthdr.rcvif = ifp;
				m->m_pkthdr.len = len;
				if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
				if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
					cas_rxcksum(m, CAS_GET(word4,
					    CAS_RC4_TCP_CSUM));
				/* Pass it on. */
				CAS_UNLOCK(sc);
				(*ifp->if_input)(ifp, m);
				CAS_LOCK(sc);
			} else
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);

			if ((word1 & CAS_RC1_RELEASE_DATA) != 0 &&
			    refcount_release(&rxds->rxds_refcount) != 0)
				cas_add_rxdesc(sc, idx);
			if ((word1 & CAS_RC1_SPLIT_PKT) != 0 &&
			    refcount_release(&rxds2->rxds_refcount) != 0)
				cas_add_rxdesc(sc, idx2);
		}

		skip = CAS_GET(word1, CAS_RC1_SKIP);

 skip:
		cas_rxcompinit(&sc->sc_rxcomps[sc->sc_rxcptr]);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
	}
	CAS_CDSYNC(sc, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CAS_WRITE_4(sc, CAS_RX_COMP_TAIL, sc->sc_rxcptr);

#undef PRINTWORD
#undef SKIPASSERT
#undef WORDTOH

#ifdef CAS_DEBUG
	CTR4(KTR_CAS, "%s: done sc->sc_rxcptr %d, sc->sc_rxdptr %d, head %d",
	    __func__, sc->sc_rxcptr, sc->sc_rxdptr,
	    CAS_READ_4(sc, CAS_RX_COMP_HEAD));
#endif
}

static void
cas_free(struct mbuf *m)
{
	struct cas_rxdsoft *rxds;
	struct cas_softc *sc;
	u_int idx, locked;

	sc = m->m_ext.ext_arg1;
	idx = (uintptr_t)m->m_ext.ext_arg2;
	rxds = &sc->sc_rxdsoft[idx];
	if (refcount_release(&rxds->rxds_refcount) == 0)
		return;

	/*
	 * NB: this function can be called via m_freem(9) within
	 * this driver!
	 */
	if ((locked = CAS_LOCK_OWNED(sc)) == 0)
		CAS_LOCK(sc);
	cas_add_rxdesc(sc, idx);
	if (locked == 0)
		CAS_UNLOCK(sc);
}

static inline void
cas_add_rxdesc(struct cas_softc *sc, u_int idx)
{

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	bus_dmamap_sync(sc->sc_rdmatag, sc->sc_rxdsoft[idx].rxds_dmamap,
	    BUS_DMASYNC_PREREAD);
	CAS_UPDATE_RXDESC(sc, sc->sc_rxdptr, idx);
	sc->sc_rxdptr = CAS_NEXTRXDESC(sc->sc_rxdptr);

	/*
	 * Update the RX kick register.  This register has to point to the
	 * descriptor after the last valid one (before the current batch)
	 * and for optimum performance should be incremented in multiples
	 * of 4 (the DMA engine fetches/updates descriptors in batches of 4).
	 */
	if ((sc->sc_rxdptr % 4) == 0) {
		CAS_CDSYNC(sc, BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		CAS_WRITE_4(sc, CAS_RX_KICK,
		    (sc->sc_rxdptr + CAS_NRXDESC - 4) & CAS_NRXDESC_MASK);
	}
}

static void
cas_eint(struct cas_softc *sc, u_int status)
{
	struct ifnet *ifp = sc->sc_ifp;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);

	device_printf(sc->sc_dev, "%s: status 0x%x", __func__, status);
	if ((status & CAS_INTR_PCI_ERROR_INT) != 0) {
		status = CAS_READ_4(sc, CAS_ERROR_STATUS);
		printf(", PCI bus error 0x%x", status);
		if ((status & CAS_ERROR_OTHER) != 0) {
			status = pci_read_config(sc->sc_dev, PCIR_STATUS, 2);
			printf(", PCI status 0x%x", status);
			pci_write_config(sc->sc_dev, PCIR_STATUS, status, 2);
		}
	}
	printf("\n");

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	cas_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		taskqueue_enqueue(sc->sc_tq, &sc->sc_tx_task);
}

static int
cas_intr(void *v)
{
	struct cas_softc *sc = v;

	if (__predict_false((CAS_READ_4(sc, CAS_STATUS_ALIAS) &
	    CAS_INTR_SUMMARY) == 0))
		return (FILTER_STRAY);

	/* Disable interrupts. */
	CAS_WRITE_4(sc, CAS_INTMASK, 0xffffffff);
	taskqueue_enqueue(sc->sc_tq, &sc->sc_intr_task);

	return (FILTER_HANDLED);
}

static void
cas_intr_task(void *arg, int pending __unused)
{
	struct cas_softc *sc = arg;
	struct ifnet *ifp = sc->sc_ifp;
	uint32_t status, status2;

	CAS_LOCK_ASSERT(sc, MA_NOTOWNED);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	status = CAS_READ_4(sc, CAS_STATUS);
	if (__predict_false((status & CAS_INTR_SUMMARY) == 0))
		goto done;

	CAS_LOCK(sc);
#ifdef CAS_DEBUG
	CTR4(KTR_CAS, "%s: %s: cplt %x, status %x",
	    device_get_name(sc->sc_dev), __func__,
	    (status >> CAS_STATUS_TX_COMP3_SHFT), (u_int)status);

	/*
	 * PCS interrupts must be cleared, otherwise no traffic is passed!
	 */
	if ((status & CAS_INTR_PCS_INT) != 0) {
		status2 =
		    CAS_READ_4(sc, CAS_PCS_INTR_STATUS) |
		    CAS_READ_4(sc, CAS_PCS_INTR_STATUS);
		if ((status2 & CAS_PCS_INTR_LINK) != 0)
			device_printf(sc->sc_dev,
			    "%s: PCS link status changed\n", __func__);
	}
	if ((status & CAS_MAC_CTRL_STATUS) != 0) {
		status2 = CAS_READ_4(sc, CAS_MAC_CTRL_STATUS);
		if ((status2 & CAS_MAC_CTRL_PAUSE) != 0)
			device_printf(sc->sc_dev,
			    "%s: PAUSE received (PAUSE time %d slots)\n",
			    __func__,
			    (status2 & CAS_MAC_CTRL_STATUS_PT_MASK) >>
			    CAS_MAC_CTRL_STATUS_PT_SHFT);
		if ((status2 & CAS_MAC_CTRL_PAUSE) != 0)
			device_printf(sc->sc_dev,
			    "%s: transited to PAUSE state\n", __func__);
		if ((status2 & CAS_MAC_CTRL_NON_PAUSE) != 0)
			device_printf(sc->sc_dev,
			    "%s: transited to non-PAUSE state\n", __func__);
	}
	if ((status & CAS_INTR_MIF) != 0)
		device_printf(sc->sc_dev, "%s: MIF interrupt\n", __func__);
#endif

	if (__predict_false((status &
	    (CAS_INTR_TX_TAG_ERR | CAS_INTR_RX_TAG_ERR |
	    CAS_INTR_RX_LEN_MMATCH | CAS_INTR_PCI_ERROR_INT)) != 0)) {
		cas_eint(sc, status);
		CAS_UNLOCK(sc);
		return;
	}

	if (__predict_false(status & CAS_INTR_TX_MAC_INT)) {
		status2 = CAS_READ_4(sc, CAS_MAC_TX_STATUS);
		if ((status2 &
		    (CAS_MAC_TX_UNDERRUN | CAS_MAC_TX_MAX_PKT_ERR)) != 0)
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		else if ((status2 & ~CAS_MAC_TX_FRAME_XMTD) != 0)
			device_printf(sc->sc_dev,
			    "MAC TX fault, status %x\n", status2);
	}

	if (__predict_false(status & CAS_INTR_RX_MAC_INT)) {
		status2 = CAS_READ_4(sc, CAS_MAC_RX_STATUS);
		if ((status2 & CAS_MAC_RX_OVERFLOW) != 0)
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		else if ((status2 & ~CAS_MAC_RX_FRAME_RCVD) != 0)
			device_printf(sc->sc_dev,
			    "MAC RX fault, status %x\n", status2);
	}

	if ((status &
	    (CAS_INTR_RX_DONE | CAS_INTR_RX_BUF_NA | CAS_INTR_RX_COMP_FULL |
	    CAS_INTR_RX_BUF_AEMPTY | CAS_INTR_RX_COMP_AFULL)) != 0) {
		cas_rint(sc);
#ifdef CAS_DEBUG
		if (__predict_false((status &
		    (CAS_INTR_RX_BUF_NA | CAS_INTR_RX_COMP_FULL |
		    CAS_INTR_RX_BUF_AEMPTY | CAS_INTR_RX_COMP_AFULL)) != 0))
			device_printf(sc->sc_dev,
			    "RX fault, status %x\n", status);
#endif
	}

	if ((status &
	    (CAS_INTR_TX_INT_ME | CAS_INTR_TX_ALL | CAS_INTR_TX_DONE)) != 0)
		cas_tint(sc);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		CAS_UNLOCK(sc);
		return;
	} else if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		taskqueue_enqueue(sc->sc_tq, &sc->sc_tx_task);
	CAS_UNLOCK(sc);

	status = CAS_READ_4(sc, CAS_STATUS_ALIAS);
	if (__predict_false((status & CAS_INTR_SUMMARY) != 0)) {
		taskqueue_enqueue(sc->sc_tq, &sc->sc_intr_task);
		return;
	}

 done:
	/* Re-enable interrupts. */
	CAS_WRITE_4(sc, CAS_INTMASK,
	    ~(CAS_INTR_TX_INT_ME | CAS_INTR_TX_TAG_ERR |
	    CAS_INTR_RX_DONE | CAS_INTR_RX_BUF_NA | CAS_INTR_RX_TAG_ERR |
	    CAS_INTR_RX_COMP_FULL | CAS_INTR_RX_BUF_AEMPTY |
	    CAS_INTR_RX_COMP_AFULL | CAS_INTR_RX_LEN_MMATCH |
	    CAS_INTR_PCI_ERROR_INT
#ifdef CAS_DEBUG
	    | CAS_INTR_PCS_INT | CAS_INTR_MIF
#endif
	));
}

static void
cas_watchdog(struct cas_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

#ifdef CAS_DEBUG
	CTR4(KTR_CAS,
	    "%s: CAS_RX_CONF %x CAS_MAC_RX_STATUS %x CAS_MAC_RX_CONF %x",
	    __func__, CAS_READ_4(sc, CAS_RX_CONF),
	    CAS_READ_4(sc, CAS_MAC_RX_STATUS),
	    CAS_READ_4(sc, CAS_MAC_RX_CONF));
	CTR4(KTR_CAS,
	    "%s: CAS_TX_CONF %x CAS_MAC_TX_STATUS %x CAS_MAC_TX_CONF %x",
	    __func__, CAS_READ_4(sc, CAS_TX_CONF),
	    CAS_READ_4(sc, CAS_MAC_TX_STATUS),
	    CAS_READ_4(sc, CAS_MAC_TX_CONF));
#endif

	if (sc->sc_wdog_timer == 0 || --sc->sc_wdog_timer != 0)
		return;

	if ((sc->sc_flags & CAS_LINK) != 0)
		device_printf(sc->sc_dev, "device timeout\n");
	else if (bootverbose)
		device_printf(sc->sc_dev, "device timeout (no link)\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	/* Try to get more packets going. */
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	cas_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		taskqueue_enqueue(sc->sc_tq, &sc->sc_tx_task);
}

static void
cas_mifinit(struct cas_softc *sc)
{

	/* Configure the MIF in frame mode. */
	CAS_WRITE_4(sc, CAS_MIF_CONF,
	    CAS_READ_4(sc, CAS_MIF_CONF) & ~CAS_MIF_CONF_BB_MODE);
	CAS_BARRIER(sc, CAS_MIF_CONF, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/*
 * MII interface
 *
 * The MII interface supports at least three different operating modes:
 *
 * Bitbang mode is implemented using data, clock and output enable registers.
 *
 * Frame mode is implemented by loading a complete frame into the frame
 * register and polling the valid bit for completion.
 *
 * Polling mode uses the frame register but completion is indicated by
 * an interrupt.
 *
 */
static int
cas_mii_readreg(device_t dev, int phy, int reg)
{
	struct cas_softc *sc;
	int n;
	uint32_t v;

#ifdef CAS_DEBUG_PHY
	printf("%s: phy %d reg %d\n", __func__, phy, reg);
#endif

	sc = device_get_softc(dev);
	if ((sc->sc_flags & CAS_SERDES) != 0) {
		switch (reg) {
		case MII_BMCR:
			reg = CAS_PCS_CTRL;
			break;
		case MII_BMSR:
			reg = CAS_PCS_STATUS;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return (0);
		case MII_ANAR:
			reg = CAS_PCS_ANAR;
			break;
		case MII_ANLPAR:
			reg = CAS_PCS_ANLPAR;
			break;
		case MII_EXTSR:
			return (EXTSR_1000XFDX | EXTSR_1000XHDX);
		default:
			device_printf(sc->sc_dev,
			    "%s: unhandled register %d\n", __func__, reg);
			return (0);
		}
		return (CAS_READ_4(sc, reg));
	}

	/* Construct the frame command. */
	v = CAS_MIF_FRAME_READ |
	    (phy << CAS_MIF_FRAME_PHY_SHFT) |
	    (reg << CAS_MIF_FRAME_REG_SHFT);

	CAS_WRITE_4(sc, CAS_MIF_FRAME, v);
	CAS_BARRIER(sc, CAS_MIF_FRAME, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	for (n = 0; n < 100; n++) {
		DELAY(1);
		v = CAS_READ_4(sc, CAS_MIF_FRAME);
		if (v & CAS_MIF_FRAME_TA_LSB)
			return (v & CAS_MIF_FRAME_DATA);
	}

	device_printf(sc->sc_dev, "%s: timed out\n", __func__);
	return (0);
}

static int
cas_mii_writereg(device_t dev, int phy, int reg, int val)
{
	struct cas_softc *sc;
	int n;
	uint32_t v;

#ifdef CAS_DEBUG_PHY
	printf("%s: phy %d reg %d val %x\n", phy, reg, val, __func__);
#endif

	sc = device_get_softc(dev);
	if ((sc->sc_flags & CAS_SERDES) != 0) {
		switch (reg) {
		case MII_BMSR:
			reg = CAS_PCS_STATUS;
			break;
		case MII_BMCR:
			reg = CAS_PCS_CTRL;
			if ((val & CAS_PCS_CTRL_RESET) == 0)
				break;
			CAS_WRITE_4(sc, CAS_PCS_CTRL, val);
			CAS_BARRIER(sc, CAS_PCS_CTRL, 4,
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			if (!cas_bitwait(sc, CAS_PCS_CTRL,
			    CAS_PCS_CTRL_RESET, 0))
				device_printf(sc->sc_dev,
				    "cannot reset PCS\n");
			/* FALLTHROUGH */
		case MII_ANAR:
			CAS_WRITE_4(sc, CAS_PCS_CONF, 0);
			CAS_BARRIER(sc, CAS_PCS_CONF, 4,
			    BUS_SPACE_BARRIER_WRITE);
			CAS_WRITE_4(sc, CAS_PCS_ANAR, val);
			CAS_BARRIER(sc, CAS_PCS_ANAR, 4,
			    BUS_SPACE_BARRIER_WRITE);
			CAS_WRITE_4(sc, CAS_PCS_SERDES_CTRL,
			    CAS_PCS_SERDES_CTRL_ESD);
			CAS_BARRIER(sc, CAS_PCS_CONF, 4,
			    BUS_SPACE_BARRIER_WRITE);
			CAS_WRITE_4(sc, CAS_PCS_CONF,
			    CAS_PCS_CONF_EN);
			CAS_BARRIER(sc, CAS_PCS_CONF, 4,
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			return (0);
		case MII_ANLPAR:
			reg = CAS_PCS_ANLPAR;
			break;
		default:
			device_printf(sc->sc_dev,
			    "%s: unhandled register %d\n", __func__, reg);
			return (0);
		}
		CAS_WRITE_4(sc, reg, val);
		CAS_BARRIER(sc, reg, 4,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		return (0);
	}

	/* Construct the frame command. */
	v = CAS_MIF_FRAME_WRITE |
	    (phy << CAS_MIF_FRAME_PHY_SHFT) |
	    (reg << CAS_MIF_FRAME_REG_SHFT) |
	    (val & CAS_MIF_FRAME_DATA);

	CAS_WRITE_4(sc, CAS_MIF_FRAME, v);
	CAS_BARRIER(sc, CAS_MIF_FRAME, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	for (n = 0; n < 100; n++) {
		DELAY(1);
		v = CAS_READ_4(sc, CAS_MIF_FRAME);
		if (v & CAS_MIF_FRAME_TA_LSB)
			return (1);
	}

	device_printf(sc->sc_dev, "%s: timed out\n", __func__);
	return (0);
}

static void
cas_mii_statchg(device_t dev)
{
	struct cas_softc *sc;
	struct ifnet *ifp;
	int gigabit;
	uint32_t rxcfg, txcfg, v;

	sc = device_get_softc(dev);
	ifp = sc->sc_ifp;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

#ifdef CAS_DEBUG
	if ((ifp->if_flags & IFF_DEBUG) != 0)
		device_printf(sc->sc_dev, "%s: status changen", __func__);
#endif

	if ((sc->sc_mii->mii_media_status & IFM_ACTIVE) != 0 &&
	    IFM_SUBTYPE(sc->sc_mii->mii_media_active) != IFM_NONE)
		sc->sc_flags |= CAS_LINK;
	else
		sc->sc_flags &= ~CAS_LINK;

	switch (IFM_SUBTYPE(sc->sc_mii->mii_media_active)) {
	case IFM_1000_SX:
	case IFM_1000_LX:
	case IFM_1000_CX:
	case IFM_1000_T:
		gigabit = 1;
		break;
	default:
		gigabit = 0;
	}

	/*
	 * The configuration done here corresponds to the steps F) and
	 * G) and as far as enabling of RX and TX MAC goes also step H)
	 * of the initialization sequence outlined in section 11.2.1 of
	 * the Cassini+ ASIC Specification.
	 */

	rxcfg = sc->sc_mac_rxcfg;
	rxcfg &= ~CAS_MAC_RX_CONF_CARR;
	txcfg = CAS_MAC_TX_CONF_EN_IPG0 | CAS_MAC_TX_CONF_NGU |
	    CAS_MAC_TX_CONF_NGUL;
	if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) & IFM_FDX) != 0)
		txcfg |= CAS_MAC_TX_CONF_ICARR | CAS_MAC_TX_CONF_ICOLLIS;
	else if (gigabit != 0) {
		rxcfg |= CAS_MAC_RX_CONF_CARR;
		txcfg |= CAS_MAC_TX_CONF_CARR;
	}
	(void)cas_disable_tx(sc);
	CAS_WRITE_4(sc, CAS_MAC_TX_CONF, txcfg);
	(void)cas_disable_rx(sc);
	CAS_WRITE_4(sc, CAS_MAC_RX_CONF, rxcfg);

	v = CAS_READ_4(sc, CAS_MAC_CTRL_CONF) &
	    ~(CAS_MAC_CTRL_CONF_TXP | CAS_MAC_CTRL_CONF_RXP);
	if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) &
	    IFM_ETH_RXPAUSE) != 0)
		v |= CAS_MAC_CTRL_CONF_RXP;
	if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) &
	    IFM_ETH_TXPAUSE) != 0)
		v |= CAS_MAC_CTRL_CONF_TXP;
	CAS_WRITE_4(sc, CAS_MAC_CTRL_CONF, v);

	/*
	 * All supported chips have a bug causing incorrect checksum
	 * to be calculated when letting them strip the FCS in half-
	 * duplex mode.  In theory we could disable FCS stripping and
	 * manually adjust the checksum accordingly.  It seems to make
	 * more sense to optimze for the common case and just disable
	 * hardware checksumming in half-duplex mode though.
	 */
	if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) & IFM_FDX) == 0) {
		ifp->if_capenable &= ~IFCAP_HWCSUM;
		ifp->if_hwassist = 0;
	} else if ((sc->sc_flags & CAS_NO_CSUM) == 0) {
		ifp->if_capenable = ifp->if_capabilities;
		ifp->if_hwassist = CAS_CSUM_FEATURES;
	}

	if (sc->sc_variant == CAS_SATURN) {
		if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) & IFM_FDX) == 0)
			/* silicon bug workaround */
			CAS_WRITE_4(sc, CAS_MAC_PREAMBLE_LEN, 0x41);
		else
			CAS_WRITE_4(sc, CAS_MAC_PREAMBLE_LEN, 0x7);
	}

	if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) & IFM_FDX) == 0 &&
	    gigabit != 0)
		CAS_WRITE_4(sc, CAS_MAC_SLOT_TIME,
		    CAS_MAC_SLOT_TIME_CARR);
	else
		CAS_WRITE_4(sc, CAS_MAC_SLOT_TIME,
		    CAS_MAC_SLOT_TIME_NORM);

	/* XIF Configuration */
	v = CAS_MAC_XIF_CONF_TX_OE | CAS_MAC_XIF_CONF_LNKLED;
	if ((sc->sc_flags & CAS_SERDES) == 0) {
		if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) & IFM_FDX) == 0)
			v |= CAS_MAC_XIF_CONF_NOECHO;
		v |= CAS_MAC_XIF_CONF_BUF_OE;
	}
	if (gigabit != 0)
		v |= CAS_MAC_XIF_CONF_GMII;
	if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) & IFM_FDX) != 0)
		v |= CAS_MAC_XIF_CONF_FDXLED;
	CAS_WRITE_4(sc, CAS_MAC_XIF_CONF, v);

	sc->sc_mac_rxcfg = rxcfg;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
	    (sc->sc_flags & CAS_LINK) != 0) {
		CAS_WRITE_4(sc, CAS_MAC_TX_CONF,
		    txcfg | CAS_MAC_TX_CONF_EN);
		CAS_WRITE_4(sc, CAS_MAC_RX_CONF,
		    rxcfg | CAS_MAC_RX_CONF_EN);
	}
}

static int
cas_mediachange(struct ifnet *ifp)
{
	struct cas_softc *sc = ifp->if_softc;
	int error;

	/* XXX add support for serial media. */

	CAS_LOCK(sc);
	error = mii_mediachg(sc->sc_mii);
	CAS_UNLOCK(sc);
	return (error);
}

static void
cas_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct cas_softc *sc = ifp->if_softc;

	CAS_LOCK(sc);
	if ((ifp->if_flags & IFF_UP) == 0) {
		CAS_UNLOCK(sc);
		return;
	}

	mii_pollstat(sc->sc_mii);
	ifmr->ifm_active = sc->sc_mii->mii_media_active;
	ifmr->ifm_status = sc->sc_mii->mii_media_status;
	CAS_UNLOCK(sc);
}

static int
cas_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct cas_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error;

	error = 0;
	switch (cmd) {
	case SIOCSIFFLAGS:
		CAS_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc->sc_ifflags) &
			    (IFF_ALLMULTI | IFF_PROMISC)) != 0)
				cas_setladrf(sc);
			else
				cas_init_locked(sc);
		} else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			cas_stop(ifp);
		sc->sc_ifflags = ifp->if_flags;
		CAS_UNLOCK(sc);
		break;
	case SIOCSIFCAP:
		CAS_LOCK(sc);
		if ((sc->sc_flags & CAS_NO_CSUM) != 0) {
			error = EINVAL;
			CAS_UNLOCK(sc);
			break;
		}
		ifp->if_capenable = ifr->ifr_reqcap;
		if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
			ifp->if_hwassist = CAS_CSUM_FEATURES;
		else
			ifp->if_hwassist = 0;
		CAS_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		CAS_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			cas_setladrf(sc);
		CAS_UNLOCK(sc);
		break;
	case SIOCSIFMTU:
		if ((ifr->ifr_mtu < ETHERMIN) ||
		    (ifr->ifr_mtu > ETHERMTU_JUMBO))
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii->mii_media, cmd);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
cas_setladrf(struct cas_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct ifmultiaddr *inm;
	int i;
	uint32_t hash[16];
	uint32_t crc, v;

	CAS_LOCK_ASSERT(sc, MA_OWNED);

	/*
	 * Turn off the RX MAC and the hash filter as required by the Sun
	 * Cassini programming restrictions.
	 */
	v = sc->sc_mac_rxcfg & ~(CAS_MAC_RX_CONF_HFILTER |
	    CAS_MAC_RX_CONF_EN);
	CAS_WRITE_4(sc, CAS_MAC_RX_CONF, v);
	CAS_BARRIER(sc, CAS_MAC_RX_CONF, 4,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (!cas_bitwait(sc, CAS_MAC_RX_CONF, CAS_MAC_RX_CONF_HFILTER |
	    CAS_MAC_RX_CONF_EN, 0))
		device_printf(sc->sc_dev,
		    "cannot disable RX MAC or hash filter\n");

	v &= ~(CAS_MAC_RX_CONF_PROMISC | CAS_MAC_RX_CONF_PGRP);
	if ((ifp->if_flags & IFF_PROMISC) != 0) {
		v |= CAS_MAC_RX_CONF_PROMISC;
		goto chipit;
	}
	if ((ifp->if_flags & IFF_ALLMULTI) != 0) {
		v |= CAS_MAC_RX_CONF_PGRP;
		goto chipit;
	}

	/*
	 * Set up multicast address filter by passing all multicast
	 * addresses through a crc generator, and then using the high
	 * order 8 bits as an index into the 256 bit logical address
	 * filter.  The high order 4 bits selects the word, while the
	 * other 4 bits select the bit within the word (where bit 0
	 * is the MSB).
	 */

	/* Clear the hash table. */
	memset(hash, 0, sizeof(hash));

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(inm, &ifp->if_multiaddrs, ifma_link) {
		if (inm->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_le(LLADDR((struct sockaddr_dl *)
		    inm->ifma_addr), ETHER_ADDR_LEN);

		/* We just want the 8 most significant bits. */
		crc >>= 24;

		/* Set the corresponding bit in the filter. */
		hash[crc >> 4] |= 1 << (15 - (crc & 15));
	}
	if_maddr_runlock(ifp);

	v |= CAS_MAC_RX_CONF_HFILTER;

	/* Now load the hash table into the chip (if we are using it). */
	for (i = 0; i < 16; i++)
		CAS_WRITE_4(sc,
		    CAS_MAC_HASH0 + i * (CAS_MAC_HASH1 - CAS_MAC_HASH0),
		    hash[i]);

 chipit:
	sc->sc_mac_rxcfg = v;
	CAS_WRITE_4(sc, CAS_MAC_RX_CONF, v | CAS_MAC_RX_CONF_EN);
}

static int	cas_pci_attach(device_t dev);
static int	cas_pci_detach(device_t dev);
static int	cas_pci_probe(device_t dev);
static int	cas_pci_resume(device_t dev);
static int	cas_pci_suspend(device_t dev);

static device_method_t cas_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cas_pci_probe),
	DEVMETHOD(device_attach,	cas_pci_attach),
	DEVMETHOD(device_detach,	cas_pci_detach),
	DEVMETHOD(device_suspend,	cas_pci_suspend),
	DEVMETHOD(device_resume,	cas_pci_resume),
	/* Use the suspend handler here, it is all that is required. */
	DEVMETHOD(device_shutdown,	cas_pci_suspend),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	cas_mii_readreg),
	DEVMETHOD(miibus_writereg,	cas_mii_writereg),
	DEVMETHOD(miibus_statchg,	cas_mii_statchg),

	DEVMETHOD_END
};

static driver_t cas_pci_driver = {
	"cas",
	cas_pci_methods,
	sizeof(struct cas_softc)
};

static const struct cas_pci_dev {
	uint32_t	cpd_devid;
	uint8_t		cpd_revid;
	int		cpd_variant;
	const char	*cpd_desc;
} cas_pci_devlist[] = {
	{ 0x0035100b, 0x0, CAS_SATURN, "NS DP83065 Saturn Gigabit Ethernet" },
	{ 0xabba108e, 0x10, CAS_CASPLUS, "Sun Cassini+ Gigabit Ethernet" },
	{ 0xabba108e, 0x0, CAS_CAS, "Sun Cassini Gigabit Ethernet" },
	{ 0, 0, 0, NULL }
};

DRIVER_MODULE(cas, pci, cas_pci_driver, cas_devclass, 0, 0);
MODULE_PNP_INFO("W32:vendor/device", pci, cas, cas_pci_devlist,
    nitems(cas_pci_devlist) - 1);
DRIVER_MODULE(miibus, cas, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(cas, pci, 1, 1, 1);

static int
cas_pci_probe(device_t dev)
{
	int i;

	for (i = 0; cas_pci_devlist[i].cpd_desc != NULL; i++) {
		if (pci_get_devid(dev) == cas_pci_devlist[i].cpd_devid &&
		    pci_get_revid(dev) >= cas_pci_devlist[i].cpd_revid) {
			device_set_desc(dev, cas_pci_devlist[i].cpd_desc);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static struct resource_spec cas_pci_res_spec[] = {
	{ SYS_RES_IRQ, 0, RF_SHAREABLE | RF_ACTIVE },	/* CAS_RES_INTR */
	{ SYS_RES_MEMORY, PCIR_BAR(0), RF_ACTIVE },	/* CAS_RES_MEM */
	{ -1, 0 }
};

#define	CAS_LOCAL_MAC_ADDRESS	"local-mac-address"
#define	CAS_PHY_INTERFACE	"phy-interface"
#define	CAS_PHY_TYPE		"phy-type"
#define	CAS_PHY_TYPE_PCS	"pcs"

static int
cas_pci_attach(device_t dev)
{
	char buf[sizeof(CAS_LOCAL_MAC_ADDRESS)];
	struct cas_softc *sc;
	int i;
#if !(defined(__powerpc__) || defined(__sparc64__))
	u_char enaddr[4][ETHER_ADDR_LEN];
	u_int j, k, lma, pcs[4], phy;
#endif

	sc = device_get_softc(dev);
	sc->sc_variant = CAS_UNKNOWN;
	for (i = 0; cas_pci_devlist[i].cpd_desc != NULL; i++) {
		if (pci_get_devid(dev) == cas_pci_devlist[i].cpd_devid &&
		    pci_get_revid(dev) >= cas_pci_devlist[i].cpd_revid) {
			sc->sc_variant = cas_pci_devlist[i].cpd_variant;
			break;
		}
	}
	if (sc->sc_variant == CAS_UNKNOWN) {
		device_printf(dev, "unknown adaptor\n");
		return (ENXIO);
	}

	/* PCI configuration */
	pci_write_config(dev, PCIR_COMMAND,
	    pci_read_config(dev, PCIR_COMMAND, 2) | PCIM_CMD_BUSMASTEREN |
	    PCIM_CMD_MWRICEN | PCIM_CMD_PERRESPEN | PCIM_CMD_SERRESPEN, 2);

	sc->sc_dev = dev;
	if (sc->sc_variant == CAS_CAS && pci_get_devid(dev) < 0x02)
		/* Hardware checksumming may hang TX. */
		sc->sc_flags |= CAS_NO_CSUM;
	if (sc->sc_variant == CAS_CASPLUS || sc->sc_variant == CAS_SATURN)
		sc->sc_flags |= CAS_REG_PLUS;
	if (sc->sc_variant == CAS_CAS ||
	    (sc->sc_variant == CAS_CASPLUS && pci_get_revid(dev) < 0x11))
		sc->sc_flags |= CAS_TABORT;
	if (bootverbose)
		device_printf(dev, "flags=0x%x\n", sc->sc_flags);

	if (bus_alloc_resources(dev, cas_pci_res_spec, sc->sc_res)) {
		device_printf(dev, "failed to allocate resources\n");
		bus_release_resources(dev, cas_pci_res_spec, sc->sc_res);
		return (ENXIO);
	}

	CAS_LOCK_INIT(sc, device_get_nameunit(dev));

#if defined(__powerpc__) || defined(__sparc64__)
	OF_getetheraddr(dev, sc->sc_enaddr);
	if (OF_getprop(ofw_bus_get_node(dev), CAS_PHY_INTERFACE, buf,
	    sizeof(buf)) > 0 || OF_getprop(ofw_bus_get_node(dev),
	    CAS_PHY_TYPE, buf, sizeof(buf)) > 0) {
		buf[sizeof(buf) - 1] = '\0';
		if (strcmp(buf, CAS_PHY_TYPE_PCS) == 0)
			sc->sc_flags |= CAS_SERDES;
	}
#else
	/*
	 * Dig out VPD (vital product data) and read the MAC address as well
	 * as the PHY type.  The VPD resides in the PCI Expansion ROM (PCI
	 * FCode) and can't be accessed via the PCI capability pointer.
	 * SUNW,pci-ce and SUNW,pci-qge use the Enhanced VPD format described
	 * in the free US Patent 7149820.
	 */

#define	PCI_ROMHDR_SIZE			0x1c
#define	PCI_ROMHDR_SIG			0x00
#define	PCI_ROMHDR_SIG_MAGIC		0xaa55		/* little endian */
#define	PCI_ROMHDR_PTR_DATA		0x18
#define	PCI_ROM_SIZE			0x18
#define	PCI_ROM_SIG			0x00
#define	PCI_ROM_SIG_MAGIC		0x52494350	/* "PCIR", endian */
							/* reversed */
#define	PCI_ROM_VENDOR			0x04
#define	PCI_ROM_DEVICE			0x06
#define	PCI_ROM_PTR_VPD			0x08
#define	PCI_VPDRES_BYTE0		0x00
#define	PCI_VPDRES_ISLARGE(x)		((x) & 0x80)
#define	PCI_VPDRES_LARGE_NAME(x)	((x) & 0x7f)
#define	PCI_VPDRES_LARGE_LEN_LSB	0x01
#define	PCI_VPDRES_LARGE_LEN_MSB	0x02
#define	PCI_VPDRES_LARGE_SIZE		0x03
#define	PCI_VPDRES_TYPE_ID_STRING	0x02		/* large */
#define	PCI_VPDRES_TYPE_VPD		0x10		/* large */
#define	PCI_VPD_KEY0			0x00
#define	PCI_VPD_KEY1			0x01
#define	PCI_VPD_LEN			0x02
#define	PCI_VPD_SIZE			0x03

#define	CAS_ROM_READ_1(sc, offs)					\
	CAS_READ_1((sc), CAS_PCI_ROM_OFFSET + (offs))
#define	CAS_ROM_READ_2(sc, offs)					\
	CAS_READ_2((sc), CAS_PCI_ROM_OFFSET + (offs))
#define	CAS_ROM_READ_4(sc, offs)					\
	CAS_READ_4((sc), CAS_PCI_ROM_OFFSET + (offs))

	lma = phy = 0;
	memset(enaddr, 0, sizeof(enaddr));
	memset(pcs, 0, sizeof(pcs));

	/* Enable PCI Expansion ROM access. */
	CAS_WRITE_4(sc, CAS_BIM_LDEV_OEN,
	    CAS_BIM_LDEV_OEN_PAD | CAS_BIM_LDEV_OEN_PROM);

	/* Read PCI Expansion ROM header. */
	if (CAS_ROM_READ_2(sc, PCI_ROMHDR_SIG) != PCI_ROMHDR_SIG_MAGIC ||
	    (i = CAS_ROM_READ_2(sc, PCI_ROMHDR_PTR_DATA)) <
	    PCI_ROMHDR_SIZE) {
		device_printf(dev, "unexpected PCI Expansion ROM header\n");
		goto fail_prom;
	}

	/* Read PCI Expansion ROM data. */
	if (CAS_ROM_READ_4(sc, i + PCI_ROM_SIG) != PCI_ROM_SIG_MAGIC ||
	    CAS_ROM_READ_2(sc, i + PCI_ROM_VENDOR) != pci_get_vendor(dev) ||
	    CAS_ROM_READ_2(sc, i + PCI_ROM_DEVICE) != pci_get_device(dev) ||
	    (j = CAS_ROM_READ_2(sc, i + PCI_ROM_PTR_VPD)) <
	    i + PCI_ROM_SIZE) {
		device_printf(dev, "unexpected PCI Expansion ROM data\n");
		goto fail_prom;
	}

	/* Read PCI VPD. */
 next:
	if (PCI_VPDRES_ISLARGE(CAS_ROM_READ_1(sc,
	    j + PCI_VPDRES_BYTE0)) == 0) {
		device_printf(dev, "no large PCI VPD\n");
		goto fail_prom;
	}

	i = (CAS_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_LEN_MSB) << 8) |
	    CAS_ROM_READ_1(sc, j + PCI_VPDRES_LARGE_LEN_LSB);
	switch (PCI_VPDRES_LARGE_NAME(CAS_ROM_READ_1(sc,
	    j + PCI_VPDRES_BYTE0))) {
	case PCI_VPDRES_TYPE_ID_STRING:
		/* Skip identifier string. */
		j += PCI_VPDRES_LARGE_SIZE + i;
		goto next;
	case PCI_VPDRES_TYPE_VPD:
		for (j += PCI_VPDRES_LARGE_SIZE; i > 0;
		    i -= PCI_VPD_SIZE + CAS_ROM_READ_1(sc, j + PCI_VPD_LEN),
		    j += PCI_VPD_SIZE + CAS_ROM_READ_1(sc, j + PCI_VPD_LEN)) {
			if (CAS_ROM_READ_1(sc, j + PCI_VPD_KEY0) != 'Z')
				/* no Enhanced VPD */
				continue;
			if (CAS_ROM_READ_1(sc, j + PCI_VPD_SIZE) != 'I')
				/* no instance property */
				continue;
			if (CAS_ROM_READ_1(sc, j + PCI_VPD_SIZE + 3) == 'B') {
				/* byte array */
				if (CAS_ROM_READ_1(sc,
				    j + PCI_VPD_SIZE + 4) != ETHER_ADDR_LEN)
					continue;
				bus_read_region_1(sc->sc_res[CAS_RES_MEM],
				    CAS_PCI_ROM_OFFSET + j + PCI_VPD_SIZE + 5,
				    buf, sizeof(buf));
				buf[sizeof(buf) - 1] = '\0';
				if (strcmp(buf, CAS_LOCAL_MAC_ADDRESS) != 0)
					continue;
				bus_read_region_1(sc->sc_res[CAS_RES_MEM],
				    CAS_PCI_ROM_OFFSET + j + PCI_VPD_SIZE +
				    5 + sizeof(CAS_LOCAL_MAC_ADDRESS),
				    enaddr[lma], sizeof(enaddr[lma]));
				lma++;
				if (lma == 4 && phy == 4)
					break;
			} else if (CAS_ROM_READ_1(sc, j + PCI_VPD_SIZE + 3) ==
			   'S') {
				/* string */
				if (CAS_ROM_READ_1(sc,
				    j + PCI_VPD_SIZE + 4) !=
				    sizeof(CAS_PHY_TYPE_PCS))
					continue;
				bus_read_region_1(sc->sc_res[CAS_RES_MEM],
				    CAS_PCI_ROM_OFFSET + j + PCI_VPD_SIZE + 5,
				    buf, sizeof(buf));
				buf[sizeof(buf) - 1] = '\0';
				if (strcmp(buf, CAS_PHY_INTERFACE) == 0)
					k = sizeof(CAS_PHY_INTERFACE);
				else if (strcmp(buf, CAS_PHY_TYPE) == 0)
					k = sizeof(CAS_PHY_TYPE);
				else
					continue;
				bus_read_region_1(sc->sc_res[CAS_RES_MEM],
				    CAS_PCI_ROM_OFFSET + j + PCI_VPD_SIZE +
				    5 + k, buf, sizeof(buf));
				buf[sizeof(buf) - 1] = '\0';
				if (strcmp(buf, CAS_PHY_TYPE_PCS) == 0)
					pcs[phy] = 1;
				phy++;
				if (lma == 4 && phy == 4)
					break;
			}
		}
		break;
	default:
		device_printf(dev, "unexpected PCI VPD\n");
		goto fail_prom;
	}

 fail_prom:
	CAS_WRITE_4(sc, CAS_BIM_LDEV_OEN, 0);

	if (lma == 0) {
		device_printf(dev, "could not determine Ethernet address\n");
		goto fail;
	}
	i = 0;
	if (lma > 1 && pci_get_slot(dev) < nitems(enaddr))
		i = pci_get_slot(dev);
	memcpy(sc->sc_enaddr, enaddr[i], ETHER_ADDR_LEN);

	if (phy == 0) {
		device_printf(dev, "could not determine PHY type\n");
		goto fail;
	}
	i = 0;
	if (phy > 1 && pci_get_slot(dev) < nitems(pcs))
		i = pci_get_slot(dev);
	if (pcs[i] != 0)
		sc->sc_flags |= CAS_SERDES;
#endif

	if (cas_attach(sc) != 0) {
		device_printf(dev, "could not be attached\n");
		goto fail;
	}

	if (bus_setup_intr(dev, sc->sc_res[CAS_RES_INTR], INTR_TYPE_NET |
	    INTR_MPSAFE, cas_intr, NULL, sc, &sc->sc_ih) != 0) {
		device_printf(dev, "failed to set up interrupt\n");
		cas_detach(sc);
		goto fail;
	}
	return (0);

 fail:
	CAS_LOCK_DESTROY(sc);
	bus_release_resources(dev, cas_pci_res_spec, sc->sc_res);
	return (ENXIO);
}

static int
cas_pci_detach(device_t dev)
{
	struct cas_softc *sc;

	sc = device_get_softc(dev);
	bus_teardown_intr(dev, sc->sc_res[CAS_RES_INTR], sc->sc_ih);
	cas_detach(sc);
	CAS_LOCK_DESTROY(sc);
	bus_release_resources(dev, cas_pci_res_spec, sc->sc_res);
	return (0);
}

static int
cas_pci_suspend(device_t dev)
{

	cas_suspend(device_get_softc(dev));
	return (0);
}

static int
cas_pci_resume(device_t dev)
{

	cas_resume(device_get_softc(dev));
	return (0);
}
