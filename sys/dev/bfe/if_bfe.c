/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 Stuart Walsh<stu@ipng.org.uk>
 * and Duncan Barclay<dmlb@dmlb.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS 'AS IS' AND
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


#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include <dev/bfe/if_bfereg.h>

MODULE_DEPEND(bfe, pci, 1, 1, 1);
MODULE_DEPEND(bfe, ether, 1, 1, 1);
MODULE_DEPEND(bfe, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#define BFE_DEVDESC_MAX		64	/* Maximum device description length */

static struct bfe_type bfe_devs[] = {
	{ BCOM_VENDORID, BCOM_DEVICEID_BCM4401,
		"Broadcom BCM4401 Fast Ethernet" },
	{ BCOM_VENDORID, BCOM_DEVICEID_BCM4401B0,
		"Broadcom BCM4401-B0 Fast Ethernet" },
		{ 0, 0, NULL }
};

static int  bfe_probe				(device_t);
static int  bfe_attach				(device_t);
static int  bfe_detach				(device_t);
static int  bfe_suspend				(device_t);
static int  bfe_resume				(device_t);
static void bfe_release_resources	(struct bfe_softc *);
static void bfe_intr				(void *);
static int  bfe_encap				(struct bfe_softc *, struct mbuf **);
static void bfe_start				(struct ifnet *);
static void bfe_start_locked			(struct ifnet *);
static int  bfe_ioctl				(struct ifnet *, u_long, caddr_t);
static void bfe_init				(void *);
static void bfe_init_locked			(void *);
static void bfe_stop				(struct bfe_softc *);
static void bfe_watchdog			(struct bfe_softc *);
static int  bfe_shutdown			(device_t);
static void bfe_tick				(void *);
static void bfe_txeof				(struct bfe_softc *);
static void bfe_rxeof				(struct bfe_softc *);
static void bfe_set_rx_mode			(struct bfe_softc *);
static int  bfe_list_rx_init		(struct bfe_softc *);
static void bfe_list_tx_init		(struct bfe_softc *);
static void bfe_discard_buf		(struct bfe_softc *, int);
static int  bfe_list_newbuf			(struct bfe_softc *, int);
static void bfe_rx_ring_free		(struct bfe_softc *);

static void bfe_pci_setup			(struct bfe_softc *, u_int32_t);
static int  bfe_ifmedia_upd			(struct ifnet *);
static void bfe_ifmedia_sts			(struct ifnet *, struct ifmediareq *);
static int  bfe_miibus_readreg		(device_t, int, int);
static int  bfe_miibus_writereg		(device_t, int, int, int);
static void bfe_miibus_statchg		(device_t);
static int  bfe_wait_bit			(struct bfe_softc *, u_int32_t, u_int32_t,
		u_long, const int);
static void bfe_get_config			(struct bfe_softc *sc);
static void bfe_read_eeprom			(struct bfe_softc *, u_int8_t *);
static void bfe_stats_update		(struct bfe_softc *);
static void bfe_clear_stats			(struct bfe_softc *);
static int  bfe_readphy				(struct bfe_softc *, u_int32_t, u_int32_t*);
static int  bfe_writephy			(struct bfe_softc *, u_int32_t, u_int32_t);
static int  bfe_resetphy			(struct bfe_softc *);
static int  bfe_setupphy			(struct bfe_softc *);
static void bfe_chip_reset			(struct bfe_softc *);
static void bfe_chip_halt			(struct bfe_softc *);
static void bfe_core_reset			(struct bfe_softc *);
static void bfe_core_disable		(struct bfe_softc *);
static int  bfe_dma_alloc			(struct bfe_softc *);
static void bfe_dma_free		(struct bfe_softc *sc);
static void bfe_dma_map				(void *, bus_dma_segment_t *, int, int);
static void bfe_cam_write			(struct bfe_softc *, u_char *, int);
static int  sysctl_bfe_stats		(SYSCTL_HANDLER_ARGS);

static device_method_t bfe_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bfe_probe),
	DEVMETHOD(device_attach,	bfe_attach),
	DEVMETHOD(device_detach,	bfe_detach),
	DEVMETHOD(device_shutdown,	bfe_shutdown),
	DEVMETHOD(device_suspend,	bfe_suspend),
	DEVMETHOD(device_resume,	bfe_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	bfe_miibus_readreg),
	DEVMETHOD(miibus_writereg,	bfe_miibus_writereg),
	DEVMETHOD(miibus_statchg,	bfe_miibus_statchg),

	DEVMETHOD_END
};

static driver_t bfe_driver = {
	"bfe",
	bfe_methods,
	sizeof(struct bfe_softc)
};

static devclass_t bfe_devclass;

DRIVER_MODULE(bfe, pci, bfe_driver, bfe_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, bfe, bfe_devs,
    nitems(bfe_devs) - 1);
DRIVER_MODULE(miibus, bfe, miibus_driver, miibus_devclass, 0, 0);

/*
 * Probe for a Broadcom 4401 chip.
 */
static int
bfe_probe(device_t dev)
{
	struct bfe_type *t;

	t = bfe_devs;

	while (t->bfe_name != NULL) {
		if (pci_get_vendor(dev) == t->bfe_vid &&
		    pci_get_device(dev) == t->bfe_did) {
			device_set_desc(dev, t->bfe_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

struct bfe_dmamap_arg {
	bus_addr_t	bfe_busaddr;
};

static int
bfe_dma_alloc(struct bfe_softc *sc)
{
	struct bfe_dmamap_arg ctx;
	struct bfe_rx_data *rd;
	struct bfe_tx_data *td;
	int error, i;

	/*
	 * parent tag.  Apparently the chip cannot handle any DMA address
	 * greater than 1GB.
	 */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->bfe_dev), /* parent */
	    1, 0,			/* alignment, boundary */
	    BFE_DMA_MAXADDR, 		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->bfe_parent_tag);
	if (error != 0) {
		device_printf(sc->bfe_dev, "cannot create parent DMA tag.\n");
		goto fail;
	}

	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(sc->bfe_parent_tag, /* parent */
	    BFE_TX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR, 		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BFE_TX_LIST_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    BFE_TX_LIST_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->bfe_tx_tag);
	if (error != 0) {
		device_printf(sc->bfe_dev, "cannot create Tx ring DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(sc->bfe_parent_tag, /* parent */
	    BFE_RX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR, 		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BFE_RX_LIST_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    BFE_RX_LIST_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->bfe_rx_tag);
	if (error != 0) {
		device_printf(sc->bfe_dev, "cannot create Rx ring DMA tag.\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(sc->bfe_parent_tag, /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR, 		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * BFE_MAXTXSEGS,	/* maxsize */
	    BFE_MAXTXSEGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->bfe_txmbuf_tag);
	if (error != 0) {
		device_printf(sc->bfe_dev,
		    "cannot create Tx buffer DMA tag.\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(sc->bfe_parent_tag, /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR, 		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->bfe_rxmbuf_tag);
	if (error != 0) {
		device_printf(sc->bfe_dev,
		    "cannot create Rx buffer DMA tag.\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load DMA map. */
	error = bus_dmamem_alloc(sc->bfe_tx_tag, (void *)&sc->bfe_tx_list,
	  BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->bfe_tx_map);
	if (error != 0) {
		device_printf(sc->bfe_dev,
		    "cannot allocate DMA'able memory for Tx ring.\n");
		goto fail;
	}
	ctx.bfe_busaddr = 0;
	error = bus_dmamap_load(sc->bfe_tx_tag, sc->bfe_tx_map,
	    sc->bfe_tx_list, BFE_TX_LIST_SIZE, bfe_dma_map, &ctx,
	    BUS_DMA_NOWAIT);
	if (error != 0 || ctx.bfe_busaddr == 0) {
		device_printf(sc->bfe_dev,
		    "cannot load DMA'able memory for Tx ring.\n");
		goto fail;
	}
	sc->bfe_tx_dma = BFE_ADDR_LO(ctx.bfe_busaddr);

	error = bus_dmamem_alloc(sc->bfe_rx_tag, (void *)&sc->bfe_rx_list,
	  BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT, &sc->bfe_rx_map);
	if (error != 0) {
		device_printf(sc->bfe_dev,
		    "cannot allocate DMA'able memory for Rx ring.\n");
		goto fail;
	}
	ctx.bfe_busaddr = 0;
	error = bus_dmamap_load(sc->bfe_rx_tag, sc->bfe_rx_map,
	    sc->bfe_rx_list, BFE_RX_LIST_SIZE, bfe_dma_map, &ctx,
	    BUS_DMA_NOWAIT);
	if (error != 0 || ctx.bfe_busaddr == 0) {
		device_printf(sc->bfe_dev,
		    "cannot load DMA'able memory for Rx ring.\n");
		goto fail;
	}
	sc->bfe_rx_dma = BFE_ADDR_LO(ctx.bfe_busaddr);

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < BFE_TX_LIST_CNT; i++) {
		td = &sc->bfe_tx_ring[i];
		td->bfe_mbuf = NULL;
		td->bfe_map = NULL;
		error = bus_dmamap_create(sc->bfe_txmbuf_tag, 0, &td->bfe_map);
		if (error != 0) {
			device_printf(sc->bfe_dev,
			    "cannot create DMA map for Tx.\n");
			goto fail;
		}
	}

	/* Create spare DMA map for Rx buffers. */
	error = bus_dmamap_create(sc->bfe_rxmbuf_tag, 0, &sc->bfe_rx_sparemap);
	if (error != 0) {
		device_printf(sc->bfe_dev, "cannot create spare DMA map for Rx.\n");
		goto fail;
	}
	/* Create DMA maps for Rx buffers. */
	for (i = 0; i < BFE_RX_LIST_CNT; i++) {
		rd = &sc->bfe_rx_ring[i];
		rd->bfe_mbuf = NULL;
		rd->bfe_map = NULL;
		rd->bfe_ctrl = 0;
		error = bus_dmamap_create(sc->bfe_rxmbuf_tag, 0, &rd->bfe_map);
		if (error != 0) {
			device_printf(sc->bfe_dev,
			    "cannot create DMA map for Rx.\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
bfe_dma_free(struct bfe_softc *sc)
{
	struct bfe_tx_data *td;
	struct bfe_rx_data *rd;
	int i;

	/* Tx ring. */
	if (sc->bfe_tx_tag != NULL) {
		if (sc->bfe_tx_dma != 0)
			bus_dmamap_unload(sc->bfe_tx_tag, sc->bfe_tx_map);
		if (sc->bfe_tx_list != NULL)
			bus_dmamem_free(sc->bfe_tx_tag, sc->bfe_tx_list,
			    sc->bfe_tx_map);
		sc->bfe_tx_dma = 0;
		sc->bfe_tx_list = NULL;
		bus_dma_tag_destroy(sc->bfe_tx_tag);
		sc->bfe_tx_tag = NULL;
	}

	/* Rx ring. */
	if (sc->bfe_rx_tag != NULL) {
		if (sc->bfe_rx_dma != 0)
			bus_dmamap_unload(sc->bfe_rx_tag, sc->bfe_rx_map);
		if (sc->bfe_rx_list != NULL)
			bus_dmamem_free(sc->bfe_rx_tag, sc->bfe_rx_list,
			    sc->bfe_rx_map);
		sc->bfe_rx_dma = 0;
		sc->bfe_rx_list = NULL;
		bus_dma_tag_destroy(sc->bfe_rx_tag);
		sc->bfe_rx_tag = NULL;
	}

	/* Tx buffers. */
	if (sc->bfe_txmbuf_tag != NULL) {
		for (i = 0; i < BFE_TX_LIST_CNT; i++) {
			td = &sc->bfe_tx_ring[i];
			if (td->bfe_map != NULL) {
				bus_dmamap_destroy(sc->bfe_txmbuf_tag,
				    td->bfe_map);
				td->bfe_map = NULL;
			}
		}
		bus_dma_tag_destroy(sc->bfe_txmbuf_tag);
		sc->bfe_txmbuf_tag = NULL;
	}

	/* Rx buffers. */
	if (sc->bfe_rxmbuf_tag != NULL) {
		for (i = 0; i < BFE_RX_LIST_CNT; i++) {
			rd = &sc->bfe_rx_ring[i];
			if (rd->bfe_map != NULL) {
				bus_dmamap_destroy(sc->bfe_rxmbuf_tag,
				    rd->bfe_map);
				rd->bfe_map = NULL;
			}
		}
		if (sc->bfe_rx_sparemap != NULL) {
			bus_dmamap_destroy(sc->bfe_rxmbuf_tag,
			    sc->bfe_rx_sparemap);
			sc->bfe_rx_sparemap = NULL;
		}
		bus_dma_tag_destroy(sc->bfe_rxmbuf_tag);
		sc->bfe_rxmbuf_tag = NULL;
	}

	if (sc->bfe_parent_tag != NULL) {
		bus_dma_tag_destroy(sc->bfe_parent_tag);
		sc->bfe_parent_tag = NULL;
	}
}

static int
bfe_attach(device_t dev)
{
	struct ifnet *ifp = NULL;
	struct bfe_softc *sc;
	int error = 0, rid;

	sc = device_get_softc(dev);
	mtx_init(&sc->bfe_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
			MTX_DEF);
	callout_init_mtx(&sc->bfe_stat_co, &sc->bfe_mtx, 0);

	sc->bfe_dev = dev;

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	rid = PCIR_BAR(0);
	sc->bfe_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
			RF_ACTIVE);
	if (sc->bfe_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate interrupt */
	rid = 0;

	sc->bfe_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
			RF_SHAREABLE | RF_ACTIVE);
	if (sc->bfe_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	if (bfe_dma_alloc(sc) != 0) {
		device_printf(dev, "failed to allocate DMA resources\n");
		error = ENXIO;
		goto fail;
	}

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "stats", CTLTYPE_INT | CTLFLAG_RW, sc, 0, sysctl_bfe_stats,
	    "I", "Statistics");

	/* Set up ifnet structure */
	ifp = sc->bfe_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "failed to if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = bfe_ioctl;
	ifp->if_start = bfe_start;
	ifp->if_init = bfe_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, BFE_TX_QLEN);
	ifp->if_snd.ifq_drv_maxlen = BFE_TX_QLEN;
	IFQ_SET_READY(&ifp->if_snd);

	bfe_get_config(sc);

	/* Reset the chip and turn on the PHY */
	BFE_LOCK(sc);
	bfe_chip_reset(sc);
	BFE_UNLOCK(sc);

	error = mii_attach(dev, &sc->bfe_miibus, ifp, bfe_ifmedia_upd,
	    bfe_ifmedia_sts, BMSR_DEFCAPMASK, sc->bfe_phyaddr, MII_OFFSET_ANY,
	    0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ether_ifattach(ifp, sc->bfe_enaddr);

	/*
	 * Tell the upper layer(s) we support long frames.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable |= IFCAP_VLAN_MTU;

	/*
	 * Hook interrupt last to avoid having to lock softc
	 */
	error = bus_setup_intr(dev, sc->bfe_irq, INTR_TYPE_NET | INTR_MPSAFE,
			NULL, bfe_intr, sc, &sc->bfe_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}
fail:
	if (error != 0)
		bfe_detach(dev);
	return (error);
}

static int
bfe_detach(device_t dev)
{
	struct bfe_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	ifp = sc->bfe_ifp;

	if (device_is_attached(dev)) {
		BFE_LOCK(sc);
		sc->bfe_flags |= BFE_FLAG_DETACH;
		bfe_stop(sc);
		BFE_UNLOCK(sc);
		callout_drain(&sc->bfe_stat_co);
		if (ifp != NULL)
			ether_ifdetach(ifp);
	}

	BFE_LOCK(sc);
	bfe_chip_reset(sc);
	BFE_UNLOCK(sc);

	bus_generic_detach(dev);
	if (sc->bfe_miibus != NULL)
		device_delete_child(dev, sc->bfe_miibus);

	bfe_release_resources(sc);
	bfe_dma_free(sc);
	mtx_destroy(&sc->bfe_mtx);

	return (0);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
bfe_shutdown(device_t dev)
{
	struct bfe_softc *sc;

	sc = device_get_softc(dev);
	BFE_LOCK(sc);
	bfe_stop(sc);

	BFE_UNLOCK(sc);

	return (0);
}

static int
bfe_suspend(device_t dev)
{
	struct bfe_softc *sc;

	sc = device_get_softc(dev);
	BFE_LOCK(sc);
	bfe_stop(sc);
	BFE_UNLOCK(sc);

	return (0);
}

static int
bfe_resume(device_t dev)
{
	struct bfe_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->bfe_ifp;
	BFE_LOCK(sc);
	bfe_chip_reset(sc);
	if (ifp->if_flags & IFF_UP) {
		bfe_init_locked(sc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
		    !IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			bfe_start_locked(ifp);
	}
	BFE_UNLOCK(sc);

	return (0);
}

static int
bfe_miibus_readreg(device_t dev, int phy, int reg)
{
	struct bfe_softc *sc;
	u_int32_t ret;

	sc = device_get_softc(dev);
	bfe_readphy(sc, reg, &ret);

	return (ret);
}

static int
bfe_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct bfe_softc *sc;

	sc = device_get_softc(dev);
	bfe_writephy(sc, reg, val);

	return (0);
}

static void
bfe_miibus_statchg(device_t dev)
{
	struct bfe_softc *sc;
	struct mii_data *mii;
	u_int32_t val, flow;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->bfe_miibus);

	sc->bfe_flags &= ~BFE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->bfe_flags |= BFE_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	/* XXX Should stop Rx/Tx engine prior to touching MAC. */
	val = CSR_READ_4(sc, BFE_TX_CTRL);
	val &= ~BFE_TX_DUPLEX;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		val |= BFE_TX_DUPLEX;
		flow = 0;
#ifdef notyet
		flow = CSR_READ_4(sc, BFE_RXCONF);
		flow &= ~BFE_RXCONF_FLOW;
		if ((IFM_OPTIONS(sc->sc_mii->mii_media_active) &
		    IFM_ETH_RXPAUSE) != 0)
			flow |= BFE_RXCONF_FLOW;
		CSR_WRITE_4(sc, BFE_RXCONF, flow);
		/*
		 * It seems that the hardware has Tx pause issues
		 * so enable only Rx pause.
		 */
		flow = CSR_READ_4(sc, BFE_MAC_FLOW);
		flow &= ~BFE_FLOW_PAUSE_ENAB;
		CSR_WRITE_4(sc, BFE_MAC_FLOW, flow);
#endif
	}
	CSR_WRITE_4(sc, BFE_TX_CTRL, val);
}

static void
bfe_tx_ring_free(struct bfe_softc *sc)
{
	int i;

	for(i = 0; i < BFE_TX_LIST_CNT; i++) {
		if (sc->bfe_tx_ring[i].bfe_mbuf != NULL) {
			bus_dmamap_sync(sc->bfe_txmbuf_tag,
			    sc->bfe_tx_ring[i].bfe_map, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->bfe_txmbuf_tag,
			    sc->bfe_tx_ring[i].bfe_map);
			m_freem(sc->bfe_tx_ring[i].bfe_mbuf);
			sc->bfe_tx_ring[i].bfe_mbuf = NULL;
		}
	}
	bzero(sc->bfe_tx_list, BFE_TX_LIST_SIZE);
	bus_dmamap_sync(sc->bfe_tx_tag, sc->bfe_tx_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
bfe_rx_ring_free(struct bfe_softc *sc)
{
	int i;

	for (i = 0; i < BFE_RX_LIST_CNT; i++) {
		if (sc->bfe_rx_ring[i].bfe_mbuf != NULL) {
			bus_dmamap_sync(sc->bfe_rxmbuf_tag,
			    sc->bfe_rx_ring[i].bfe_map, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->bfe_rxmbuf_tag,
			    sc->bfe_rx_ring[i].bfe_map);
			m_freem(sc->bfe_rx_ring[i].bfe_mbuf);
			sc->bfe_rx_ring[i].bfe_mbuf = NULL;
		}
	}
	bzero(sc->bfe_rx_list, BFE_RX_LIST_SIZE);
	bus_dmamap_sync(sc->bfe_rx_tag, sc->bfe_rx_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static int
bfe_list_rx_init(struct bfe_softc *sc)
{
	struct bfe_rx_data *rd;
	int i;

	sc->bfe_rx_prod = sc->bfe_rx_cons = 0;
	bzero(sc->bfe_rx_list, BFE_RX_LIST_SIZE);
	for (i = 0; i < BFE_RX_LIST_CNT; i++) {
		rd = &sc->bfe_rx_ring[i];
		rd->bfe_mbuf = NULL;
		rd->bfe_ctrl = 0;
		if (bfe_list_newbuf(sc, i) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->bfe_rx_tag, sc->bfe_rx_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, BFE_DMARX_PTR, (i * sizeof(struct bfe_desc)));

	return (0);
}

static void
bfe_list_tx_init(struct bfe_softc *sc)
{
	int i;

	sc->bfe_tx_cnt = sc->bfe_tx_prod = sc->bfe_tx_cons = 0;
	bzero(sc->bfe_tx_list, BFE_TX_LIST_SIZE);
	for (i = 0; i < BFE_TX_LIST_CNT; i++)
		sc->bfe_tx_ring[i].bfe_mbuf = NULL;

	bus_dmamap_sync(sc->bfe_tx_tag, sc->bfe_tx_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
bfe_discard_buf(struct bfe_softc *sc, int c)
{
	struct bfe_rx_data *r;
	struct bfe_desc *d;

	r = &sc->bfe_rx_ring[c];
	d = &sc->bfe_rx_list[c];
	d->bfe_ctrl = htole32(r->bfe_ctrl);
}

static int
bfe_list_newbuf(struct bfe_softc *sc, int c)
{
	struct bfe_rxheader *rx_header;
	struct bfe_desc *d;
	struct bfe_rx_data *r;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	u_int32_t ctrl;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	if (bus_dmamap_load_mbuf_sg(sc->bfe_rxmbuf_tag, sc->bfe_rx_sparemap,
	    m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));
	r = &sc->bfe_rx_ring[c];
	if (r->bfe_mbuf != NULL) {
		bus_dmamap_sync(sc->bfe_rxmbuf_tag, r->bfe_map,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->bfe_rxmbuf_tag, r->bfe_map);
	}
	map = r->bfe_map;
	r->bfe_map = sc->bfe_rx_sparemap;
	sc->bfe_rx_sparemap = map;
	r->bfe_mbuf = m;

	rx_header = mtod(m, struct bfe_rxheader *);
	rx_header->len = 0;
	rx_header->flags = 0;
	bus_dmamap_sync(sc->bfe_rxmbuf_tag, r->bfe_map, BUS_DMASYNC_PREREAD);
	
	ctrl = segs[0].ds_len & BFE_DESC_LEN;
	KASSERT(ctrl > ETHER_MAX_LEN + 32, ("%s: buffer size too small(%d)!",
	    __func__, ctrl));
	if (c == BFE_RX_LIST_CNT - 1)
		ctrl |= BFE_DESC_EOT;
	r->bfe_ctrl = ctrl;

	d = &sc->bfe_rx_list[c];
	d->bfe_ctrl = htole32(ctrl);
	/* The chip needs all addresses to be added to BFE_PCI_DMA. */
	d->bfe_addr = htole32(BFE_ADDR_LO(segs[0].ds_addr) + BFE_PCI_DMA);

	return (0);
}

static void
bfe_get_config(struct bfe_softc *sc)
{
	u_int8_t eeprom[128];

	bfe_read_eeprom(sc, eeprom);

	sc->bfe_enaddr[0] = eeprom[79];
	sc->bfe_enaddr[1] = eeprom[78];
	sc->bfe_enaddr[2] = eeprom[81];
	sc->bfe_enaddr[3] = eeprom[80];
	sc->bfe_enaddr[4] = eeprom[83];
	sc->bfe_enaddr[5] = eeprom[82];

	sc->bfe_phyaddr = eeprom[90] & 0x1f;
	sc->bfe_mdc_port = (eeprom[90] >> 14) & 0x1;

	sc->bfe_core_unit = 0;
	sc->bfe_dma_offset = BFE_PCI_DMA;
}

static void
bfe_pci_setup(struct bfe_softc *sc, u_int32_t cores)
{
	u_int32_t bar_orig, pci_rev, val;

	bar_orig = pci_read_config(sc->bfe_dev, BFE_BAR0_WIN, 4);
	pci_write_config(sc->bfe_dev, BFE_BAR0_WIN, BFE_REG_PCI, 4);
	pci_rev = CSR_READ_4(sc, BFE_SBIDHIGH) & BFE_RC_MASK;

	val = CSR_READ_4(sc, BFE_SBINTVEC);
	val |= cores;
	CSR_WRITE_4(sc, BFE_SBINTVEC, val);

	val = CSR_READ_4(sc, BFE_SSB_PCI_TRANS_2);
	val |= BFE_SSB_PCI_PREF | BFE_SSB_PCI_BURST;
	CSR_WRITE_4(sc, BFE_SSB_PCI_TRANS_2, val);

	pci_write_config(sc->bfe_dev, BFE_BAR0_WIN, bar_orig, 4);
}

static void
bfe_clear_stats(struct bfe_softc *sc)
{
	uint32_t reg;

	BFE_LOCK_ASSERT(sc);

	CSR_WRITE_4(sc, BFE_MIB_CTRL, BFE_MIB_CLR_ON_READ);
	for (reg = BFE_TX_GOOD_O; reg <= BFE_TX_PAUSE; reg += 4)
		CSR_READ_4(sc, reg);
	for (reg = BFE_RX_GOOD_O; reg <= BFE_RX_NPAUSE; reg += 4)
		CSR_READ_4(sc, reg);
}

static int
bfe_resetphy(struct bfe_softc *sc)
{
	u_int32_t val;

	bfe_writephy(sc, 0, BMCR_RESET);
	DELAY(100);
	bfe_readphy(sc, 0, &val);
	if (val & BMCR_RESET) {
		device_printf(sc->bfe_dev, "PHY Reset would not complete.\n");
		return (ENXIO);
	}
	return (0);
}

static void
bfe_chip_halt(struct bfe_softc *sc)
{
	BFE_LOCK_ASSERT(sc);
	/* disable interrupts - not that it actually does..*/
	CSR_WRITE_4(sc, BFE_IMASK, 0);
	CSR_READ_4(sc, BFE_IMASK);

	CSR_WRITE_4(sc, BFE_ENET_CTRL, BFE_ENET_DISABLE);
	bfe_wait_bit(sc, BFE_ENET_CTRL, BFE_ENET_DISABLE, 200, 1);

	CSR_WRITE_4(sc, BFE_DMARX_CTRL, 0);
	CSR_WRITE_4(sc, BFE_DMATX_CTRL, 0);
	DELAY(10);
}

static void
bfe_chip_reset(struct bfe_softc *sc)
{
	u_int32_t val;

	BFE_LOCK_ASSERT(sc);

	/* Set the interrupt vector for the enet core */
	bfe_pci_setup(sc, BFE_INTVEC_ENET0);

	/* is core up? */
	val = CSR_READ_4(sc, BFE_SBTMSLOW) &
	    (BFE_RESET | BFE_REJECT | BFE_CLOCK);
	if (val == BFE_CLOCK) {
		/* It is, so shut it down */
		CSR_WRITE_4(sc, BFE_RCV_LAZY, 0);
		CSR_WRITE_4(sc, BFE_ENET_CTRL, BFE_ENET_DISABLE);
		bfe_wait_bit(sc, BFE_ENET_CTRL, BFE_ENET_DISABLE, 100, 1);
		CSR_WRITE_4(sc, BFE_DMATX_CTRL, 0);
		if (CSR_READ_4(sc, BFE_DMARX_STAT) & BFE_STAT_EMASK)
			bfe_wait_bit(sc, BFE_DMARX_STAT, BFE_STAT_SIDLE,
			    100, 0);
		CSR_WRITE_4(sc, BFE_DMARX_CTRL, 0);
	}

	bfe_core_reset(sc);
	bfe_clear_stats(sc);

	/*
	 * We want the phy registers to be accessible even when
	 * the driver is "downed" so initialize MDC preamble, frequency,
	 * and whether internal or external phy here.
	 */

	/* 4402 has 62.5Mhz SB clock and internal phy */
	CSR_WRITE_4(sc, BFE_MDIO_CTRL, 0x8d);

	/* Internal or external PHY? */
	val = CSR_READ_4(sc, BFE_DEVCTRL);
	if (!(val & BFE_IPP))
		CSR_WRITE_4(sc, BFE_ENET_CTRL, BFE_ENET_EPSEL);
	else if (CSR_READ_4(sc, BFE_DEVCTRL) & BFE_EPR) {
		BFE_AND(sc, BFE_DEVCTRL, ~BFE_EPR);
		DELAY(100);
	}

	/* Enable CRC32 generation and set proper LED modes */
	BFE_OR(sc, BFE_MAC_CTRL, BFE_CTRL_CRC32_ENAB | BFE_CTRL_LED);

	/* Reset or clear powerdown control bit  */
	BFE_AND(sc, BFE_MAC_CTRL, ~BFE_CTRL_PDOWN);

	CSR_WRITE_4(sc, BFE_RCV_LAZY, ((1 << BFE_LAZY_FC_SHIFT) &
				BFE_LAZY_FC_MASK));

	/*
	 * We don't want lazy interrupts, so just send them at
	 * the end of a frame, please
	 */
	BFE_OR(sc, BFE_RCV_LAZY, 0);

	/* Set max lengths, accounting for VLAN tags */
	CSR_WRITE_4(sc, BFE_RXMAXLEN, ETHER_MAX_LEN+32);
	CSR_WRITE_4(sc, BFE_TXMAXLEN, ETHER_MAX_LEN+32);

	/* Set watermark XXX - magic */
	CSR_WRITE_4(sc, BFE_TX_WMARK, 56);

	/*
	 * Initialise DMA channels
	 * - not forgetting dma addresses need to be added to BFE_PCI_DMA
	 */
	CSR_WRITE_4(sc, BFE_DMATX_CTRL, BFE_TX_CTRL_ENABLE);
	CSR_WRITE_4(sc, BFE_DMATX_ADDR, sc->bfe_tx_dma + BFE_PCI_DMA);

	CSR_WRITE_4(sc, BFE_DMARX_CTRL, (BFE_RX_OFFSET << BFE_RX_CTRL_ROSHIFT) |
			BFE_RX_CTRL_ENABLE);
	CSR_WRITE_4(sc, BFE_DMARX_ADDR, sc->bfe_rx_dma + BFE_PCI_DMA);

	bfe_resetphy(sc);
	bfe_setupphy(sc);
}

static void
bfe_core_disable(struct bfe_softc *sc)
{
	if ((CSR_READ_4(sc, BFE_SBTMSLOW)) & BFE_RESET)
		return;

	/*
	 * Set reject, wait for it set, then wait for the core to stop
	 * being busy, then set reset and reject and enable the clocks.
	 */
	CSR_WRITE_4(sc, BFE_SBTMSLOW, (BFE_REJECT | BFE_CLOCK));
	bfe_wait_bit(sc, BFE_SBTMSLOW, BFE_REJECT, 1000, 0);
	bfe_wait_bit(sc, BFE_SBTMSHIGH, BFE_BUSY, 1000, 1);
	CSR_WRITE_4(sc, BFE_SBTMSLOW, (BFE_FGC | BFE_CLOCK | BFE_REJECT |
				BFE_RESET));
	CSR_READ_4(sc, BFE_SBTMSLOW);
	DELAY(10);
	/* Leave reset and reject set */
	CSR_WRITE_4(sc, BFE_SBTMSLOW, (BFE_REJECT | BFE_RESET));
	DELAY(10);
}

static void
bfe_core_reset(struct bfe_softc *sc)
{
	u_int32_t val;

	/* Disable the core */
	bfe_core_disable(sc);

	/* and bring it back up */
	CSR_WRITE_4(sc, BFE_SBTMSLOW, (BFE_RESET | BFE_CLOCK | BFE_FGC));
	CSR_READ_4(sc, BFE_SBTMSLOW);
	DELAY(10);

	/* Chip bug, clear SERR, IB and TO if they are set. */
	if (CSR_READ_4(sc, BFE_SBTMSHIGH) & BFE_SERR)
		CSR_WRITE_4(sc, BFE_SBTMSHIGH, 0);
	val = CSR_READ_4(sc, BFE_SBIMSTATE);
	if (val & (BFE_IBE | BFE_TO))
		CSR_WRITE_4(sc, BFE_SBIMSTATE, val & ~(BFE_IBE | BFE_TO));

	/* Clear reset and allow it to move through the core */
	CSR_WRITE_4(sc, BFE_SBTMSLOW, (BFE_CLOCK | BFE_FGC));
	CSR_READ_4(sc, BFE_SBTMSLOW);
	DELAY(10);

	/* Leave the clock set */
	CSR_WRITE_4(sc, BFE_SBTMSLOW, BFE_CLOCK);
	CSR_READ_4(sc, BFE_SBTMSLOW);
	DELAY(10);
}

static void
bfe_cam_write(struct bfe_softc *sc, u_char *data, int index)
{
	u_int32_t val;

	val  = ((u_int32_t) data[2]) << 24;
	val |= ((u_int32_t) data[3]) << 16;
	val |= ((u_int32_t) data[4]) <<  8;
	val |= ((u_int32_t) data[5]);
	CSR_WRITE_4(sc, BFE_CAM_DATA_LO, val);
	val = (BFE_CAM_HI_VALID |
			(((u_int32_t) data[0]) << 8) |
			(((u_int32_t) data[1])));
	CSR_WRITE_4(sc, BFE_CAM_DATA_HI, val);
	CSR_WRITE_4(sc, BFE_CAM_CTRL, (BFE_CAM_WRITE |
				((u_int32_t) index << BFE_CAM_INDEX_SHIFT)));
	bfe_wait_bit(sc, BFE_CAM_CTRL, BFE_CAM_BUSY, 10000, 1);
}

static void
bfe_set_rx_mode(struct bfe_softc *sc)
{
	struct ifnet *ifp = sc->bfe_ifp;
	struct ifmultiaddr  *ifma;
	u_int32_t val;
	int i = 0;

	BFE_LOCK_ASSERT(sc);

	val = CSR_READ_4(sc, BFE_RXCONF);

	if (ifp->if_flags & IFF_PROMISC)
		val |= BFE_RXCONF_PROMISC;
	else
		val &= ~BFE_RXCONF_PROMISC;

	if (ifp->if_flags & IFF_BROADCAST)
		val &= ~BFE_RXCONF_DBCAST;
	else
		val |= BFE_RXCONF_DBCAST;


	CSR_WRITE_4(sc, BFE_CAM_CTRL, 0);
	bfe_cam_write(sc, IF_LLADDR(sc->bfe_ifp), i++);

	if (ifp->if_flags & IFF_ALLMULTI)
		val |= BFE_RXCONF_ALLMULTI;
	else {
		val &= ~BFE_RXCONF_ALLMULTI;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			bfe_cam_write(sc,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr), i++);
		}
		if_maddr_runlock(ifp);
	}

	CSR_WRITE_4(sc, BFE_RXCONF, val);
	BFE_OR(sc, BFE_CAM_CTRL, BFE_CAM_ENABLE);
}

static void
bfe_dma_map(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct bfe_dmamap_arg *ctx;

	if (error != 0)
		return;

	KASSERT(nseg == 1, ("%s : %d segments returned!", __func__, nseg));

	ctx = (struct bfe_dmamap_arg *)arg;
	ctx->bfe_busaddr = segs[0].ds_addr;
}

static void
bfe_release_resources(struct bfe_softc *sc)
{

	if (sc->bfe_intrhand != NULL)
		bus_teardown_intr(sc->bfe_dev, sc->bfe_irq, sc->bfe_intrhand);

	if (sc->bfe_irq != NULL)
		bus_release_resource(sc->bfe_dev, SYS_RES_IRQ, 0, sc->bfe_irq);

	if (sc->bfe_res != NULL)
		bus_release_resource(sc->bfe_dev, SYS_RES_MEMORY, PCIR_BAR(0),
		    sc->bfe_res);

	if (sc->bfe_ifp != NULL)
		if_free(sc->bfe_ifp);
}

static void
bfe_read_eeprom(struct bfe_softc *sc, u_int8_t *data)
{
	long i;
	u_int16_t *ptr = (u_int16_t *)data;

	for(i = 0; i < 128; i += 2)
		ptr[i/2] = CSR_READ_4(sc, 4096 + i);
}

static int
bfe_wait_bit(struct bfe_softc *sc, u_int32_t reg, u_int32_t bit,
		u_long timeout, const int clear)
{
	u_long i;

	for (i = 0; i < timeout; i++) {
		u_int32_t val = CSR_READ_4(sc, reg);

		if (clear && !(val & bit))
			break;
		if (!clear && (val & bit))
			break;
		DELAY(10);
	}
	if (i == timeout) {
		device_printf(sc->bfe_dev,
		    "BUG!  Timeout waiting for bit %08x of register "
		    "%x to %s.\n", bit, reg, (clear ? "clear" : "set"));
		return (-1);
	}
	return (0);
}

static int
bfe_readphy(struct bfe_softc *sc, u_int32_t reg, u_int32_t *val)
{
	int err;

	/* Clear MII ISR */
	CSR_WRITE_4(sc, BFE_EMAC_ISTAT, BFE_EMAC_INT_MII);
	CSR_WRITE_4(sc, BFE_MDIO_DATA, (BFE_MDIO_SB_START |
				(BFE_MDIO_OP_READ << BFE_MDIO_OP_SHIFT) |
				(sc->bfe_phyaddr << BFE_MDIO_PMD_SHIFT) |
				(reg << BFE_MDIO_RA_SHIFT) |
				(BFE_MDIO_TA_VALID << BFE_MDIO_TA_SHIFT)));
	err = bfe_wait_bit(sc, BFE_EMAC_ISTAT, BFE_EMAC_INT_MII, 100, 0);
	*val = CSR_READ_4(sc, BFE_MDIO_DATA) & BFE_MDIO_DATA_DATA;

	return (err);
}

static int
bfe_writephy(struct bfe_softc *sc, u_int32_t reg, u_int32_t val)
{
	int status;

	CSR_WRITE_4(sc, BFE_EMAC_ISTAT, BFE_EMAC_INT_MII);
	CSR_WRITE_4(sc, BFE_MDIO_DATA, (BFE_MDIO_SB_START |
				(BFE_MDIO_OP_WRITE << BFE_MDIO_OP_SHIFT) |
				(sc->bfe_phyaddr << BFE_MDIO_PMD_SHIFT) |
				(reg << BFE_MDIO_RA_SHIFT) |
				(BFE_MDIO_TA_VALID << BFE_MDIO_TA_SHIFT) |
				(val & BFE_MDIO_DATA_DATA)));
	status = bfe_wait_bit(sc, BFE_EMAC_ISTAT, BFE_EMAC_INT_MII, 100, 0);

	return (status);
}

/*
 * XXX - I think this is handled by the PHY driver, but it can't hurt to do it
 * twice
 */
static int
bfe_setupphy(struct bfe_softc *sc)
{
	u_int32_t val;

	/* Enable activity LED */
	bfe_readphy(sc, 26, &val);
	bfe_writephy(sc, 26, val & 0x7fff);
	bfe_readphy(sc, 26, &val);

	/* Enable traffic meter LED mode */
	bfe_readphy(sc, 27, &val);
	bfe_writephy(sc, 27, val | (1 << 6));

	return (0);
}

static void
bfe_stats_update(struct bfe_softc *sc)
{
	struct bfe_hw_stats *stats;
	struct ifnet *ifp;
	uint32_t mib[BFE_MIB_CNT];
	uint32_t reg, *val;

	BFE_LOCK_ASSERT(sc);

	val = mib;
	CSR_WRITE_4(sc, BFE_MIB_CTRL, BFE_MIB_CLR_ON_READ);
	for (reg = BFE_TX_GOOD_O; reg <= BFE_TX_PAUSE; reg += 4)
		*val++ = CSR_READ_4(sc, reg);
	for (reg = BFE_RX_GOOD_O; reg <= BFE_RX_NPAUSE; reg += 4)
		*val++ = CSR_READ_4(sc, reg);

	ifp = sc->bfe_ifp;
	stats = &sc->bfe_stats;
	/* Tx stat. */
	stats->tx_good_octets += mib[MIB_TX_GOOD_O];
	stats->tx_good_frames += mib[MIB_TX_GOOD_P];
	stats->tx_octets += mib[MIB_TX_O];
	stats->tx_frames += mib[MIB_TX_P];
	stats->tx_bcast_frames += mib[MIB_TX_BCAST];
	stats->tx_mcast_frames += mib[MIB_TX_MCAST];
	stats->tx_pkts_64 += mib[MIB_TX_64];
	stats->tx_pkts_65_127 += mib[MIB_TX_65_127];
	stats->tx_pkts_128_255 += mib[MIB_TX_128_255];
	stats->tx_pkts_256_511 += mib[MIB_TX_256_511];
	stats->tx_pkts_512_1023 += mib[MIB_TX_512_1023];
	stats->tx_pkts_1024_max += mib[MIB_TX_1024_MAX];
	stats->tx_jabbers += mib[MIB_TX_JABBER];
	stats->tx_oversize_frames += mib[MIB_TX_OSIZE];
	stats->tx_frag_frames += mib[MIB_TX_FRAG];
	stats->tx_underruns += mib[MIB_TX_URUNS];
	stats->tx_colls += mib[MIB_TX_TCOLS];
	stats->tx_single_colls += mib[MIB_TX_SCOLS];
	stats->tx_multi_colls += mib[MIB_TX_MCOLS];
	stats->tx_excess_colls += mib[MIB_TX_ECOLS];
	stats->tx_late_colls += mib[MIB_TX_LCOLS];
	stats->tx_deferrals += mib[MIB_TX_DEFERED];
	stats->tx_carrier_losts += mib[MIB_TX_CLOST];
	stats->tx_pause_frames += mib[MIB_TX_PAUSE];
	/* Rx stat. */
	stats->rx_good_octets += mib[MIB_RX_GOOD_O];
	stats->rx_good_frames += mib[MIB_RX_GOOD_P];
	stats->rx_octets += mib[MIB_RX_O];
	stats->rx_frames += mib[MIB_RX_P];
	stats->rx_bcast_frames += mib[MIB_RX_BCAST];
	stats->rx_mcast_frames += mib[MIB_RX_MCAST];
	stats->rx_pkts_64 += mib[MIB_RX_64];
	stats->rx_pkts_65_127 += mib[MIB_RX_65_127];
	stats->rx_pkts_128_255 += mib[MIB_RX_128_255];
	stats->rx_pkts_256_511 += mib[MIB_RX_256_511];
	stats->rx_pkts_512_1023 += mib[MIB_RX_512_1023];
	stats->rx_pkts_1024_max += mib[MIB_RX_1024_MAX];
	stats->rx_jabbers += mib[MIB_RX_JABBER];
	stats->rx_oversize_frames += mib[MIB_RX_OSIZE];
	stats->rx_frag_frames += mib[MIB_RX_FRAG];
	stats->rx_missed_frames += mib[MIB_RX_MISS];
	stats->rx_crc_align_errs += mib[MIB_RX_CRCA];
	stats->rx_runts += mib[MIB_RX_USIZE];
	stats->rx_crc_errs += mib[MIB_RX_CRC];
	stats->rx_align_errs += mib[MIB_RX_ALIGN];
	stats->rx_symbol_errs += mib[MIB_RX_SYM];
	stats->rx_pause_frames += mib[MIB_RX_PAUSE];
	stats->rx_control_frames += mib[MIB_RX_NPAUSE];

	/* Update counters in ifnet. */
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, (u_long)mib[MIB_TX_GOOD_P]);
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (u_long)mib[MIB_TX_TCOLS]);
	if_inc_counter(ifp, IFCOUNTER_OERRORS, (u_long)mib[MIB_TX_URUNS] +
	    (u_long)mib[MIB_TX_ECOLS] +
	    (u_long)mib[MIB_TX_DEFERED] +
	    (u_long)mib[MIB_TX_CLOST]);

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, (u_long)mib[MIB_RX_GOOD_P]);

	if_inc_counter(ifp, IFCOUNTER_IERRORS, mib[MIB_RX_JABBER] +
	    mib[MIB_RX_MISS] +
	    mib[MIB_RX_CRCA] +
	    mib[MIB_RX_USIZE] +
	    mib[MIB_RX_CRC] +
	    mib[MIB_RX_ALIGN] +
	    mib[MIB_RX_SYM]);
}

static void
bfe_txeof(struct bfe_softc *sc)
{
	struct bfe_tx_data *r;
	struct ifnet *ifp;
	int i, chipidx;

	BFE_LOCK_ASSERT(sc);

	ifp = sc->bfe_ifp;

	chipidx = CSR_READ_4(sc, BFE_DMATX_STAT) & BFE_STAT_CDMASK;
	chipidx /= sizeof(struct bfe_desc);

	i = sc->bfe_tx_cons;
	if (i == chipidx)
		return;
	bus_dmamap_sync(sc->bfe_tx_tag, sc->bfe_tx_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	/* Go through the mbufs and free those that have been transmitted */
	for (; i != chipidx; BFE_INC(i, BFE_TX_LIST_CNT)) {
		r = &sc->bfe_tx_ring[i];
		sc->bfe_tx_cnt--;
		if (r->bfe_mbuf == NULL)
			continue;
		bus_dmamap_sync(sc->bfe_txmbuf_tag, r->bfe_map,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->bfe_txmbuf_tag, r->bfe_map);

		m_freem(r->bfe_mbuf);
		r->bfe_mbuf = NULL;
	}

	if (i != sc->bfe_tx_cons) {
		/* we freed up some mbufs */
		sc->bfe_tx_cons = i;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	}

	if (sc->bfe_tx_cnt == 0)
		sc->bfe_watchdog_timer = 0;
}

/* Pass a received packet up the stack */
static void
bfe_rxeof(struct bfe_softc *sc)
{
	struct mbuf *m;
	struct ifnet *ifp;
	struct bfe_rxheader *rxheader;
	struct bfe_rx_data *r;
	int cons, prog;
	u_int32_t status, current, len, flags;

	BFE_LOCK_ASSERT(sc);
	cons = sc->bfe_rx_cons;
	status = CSR_READ_4(sc, BFE_DMARX_STAT);
	current = (status & BFE_STAT_CDMASK) / sizeof(struct bfe_desc);

	ifp = sc->bfe_ifp;

	bus_dmamap_sync(sc->bfe_rx_tag, sc->bfe_rx_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; current != cons; prog++,
	    BFE_INC(cons, BFE_RX_LIST_CNT)) {
		r = &sc->bfe_rx_ring[cons];
		m = r->bfe_mbuf;
		/*
		 * Rx status should be read from mbuf such that we can't
		 * delay bus_dmamap_sync(9). This hardware limiation
		 * results in inefficent mbuf usage as bfe(4) couldn't
		 * reuse mapped buffer from errored frame. 
		 */
		if (bfe_list_newbuf(sc, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			bfe_discard_buf(sc, cons);
			continue;
		}
		rxheader = mtod(m, struct bfe_rxheader*);
		len = le16toh(rxheader->len);
		flags = le16toh(rxheader->flags);

		/* Remove CRC bytes. */
		len -= ETHER_CRC_LEN;

		/* flag an error and try again */
		if ((len > ETHER_MAX_LEN+32) || (flags & BFE_RX_FLAG_ERRORS)) {
			m_freem(m);
			continue;
		}

		/* Make sure to skip header bytes written by hardware. */
		m_adj(m, BFE_RX_OFFSET);
		m->m_len = m->m_pkthdr.len = len;

		m->m_pkthdr.rcvif = ifp;
		BFE_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		BFE_LOCK(sc);
	}

	if (prog > 0) {
		sc->bfe_rx_cons = cons;
		bus_dmamap_sync(sc->bfe_rx_tag, sc->bfe_rx_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

static void
bfe_intr(void *xsc)
{
	struct bfe_softc *sc = xsc;
	struct ifnet *ifp;
	u_int32_t istat;

	ifp = sc->bfe_ifp;

	BFE_LOCK(sc);

	istat = CSR_READ_4(sc, BFE_ISTAT);

	/*
	 * Defer unsolicited interrupts - This is necessary because setting the
	 * chips interrupt mask register to 0 doesn't actually stop the
	 * interrupts
	 */
	istat &= BFE_IMASK_DEF;
	CSR_WRITE_4(sc, BFE_ISTAT, istat);
	CSR_READ_4(sc, BFE_ISTAT);

	/* not expecting this interrupt, disregard it */
	if (istat == 0 || (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		BFE_UNLOCK(sc);
		return;
	}

	/* A packet was received */
	if (istat & BFE_ISTAT_RX)
		bfe_rxeof(sc);

	/* A packet was sent */
	if (istat & BFE_ISTAT_TX)
		bfe_txeof(sc);

	if (istat & BFE_ISTAT_ERRORS) {

		if (istat & BFE_ISTAT_DSCE) {
			device_printf(sc->bfe_dev, "Descriptor Error\n");
			bfe_stop(sc);
			BFE_UNLOCK(sc);
			return;
		}

		if (istat & BFE_ISTAT_DPE) {
			device_printf(sc->bfe_dev,
			    "Descriptor Protocol Error\n");
			bfe_stop(sc);
			BFE_UNLOCK(sc);
			return;
		}
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		bfe_init_locked(sc);
	}

	/* We have packets pending, fire them out */
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		bfe_start_locked(ifp);

	BFE_UNLOCK(sc);
}

static int
bfe_encap(struct bfe_softc *sc, struct mbuf **m_head)
{
	struct bfe_desc *d;
	struct bfe_tx_data *r, *r1;
	struct mbuf *m;
	bus_dmamap_t map;
	bus_dma_segment_t txsegs[BFE_MAXTXSEGS];
	uint32_t cur, si;
	int error, i, nsegs;

	BFE_LOCK_ASSERT(sc);

	M_ASSERTPKTHDR((*m_head));

	si = cur = sc->bfe_tx_prod;
	r = &sc->bfe_tx_ring[cur];
	error = bus_dmamap_load_mbuf_sg(sc->bfe_txmbuf_tag, r->bfe_map, *m_head,
	    txsegs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, BFE_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->bfe_txmbuf_tag, r->bfe_map,
		    *m_head, txsegs, &nsegs, 0);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	if (nsegs == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	if (sc->bfe_tx_cnt + nsegs > BFE_TX_LIST_CNT - 1) {
		bus_dmamap_unload(sc->bfe_txmbuf_tag, r->bfe_map);
		return (ENOBUFS);
	}

	for (i = 0; i < nsegs; i++) {
		d = &sc->bfe_tx_list[cur];
		d->bfe_ctrl = htole32(txsegs[i].ds_len & BFE_DESC_LEN);
		d->bfe_ctrl |= htole32(BFE_DESC_IOC);
		if (cur == BFE_TX_LIST_CNT - 1)
			/*
			 * Tell the chip to wrap to the start of
			 * the descriptor list.
			 */
			d->bfe_ctrl |= htole32(BFE_DESC_EOT);
		/* The chip needs all addresses to be added to BFE_PCI_DMA. */
		d->bfe_addr = htole32(BFE_ADDR_LO(txsegs[i].ds_addr) +
		    BFE_PCI_DMA);
		BFE_INC(cur, BFE_TX_LIST_CNT);
	}

	/* Update producer index. */
	sc->bfe_tx_prod = cur;

	/* Set EOF on the last descriptor. */
	cur = (cur + BFE_TX_LIST_CNT - 1) % BFE_TX_LIST_CNT;
	d = &sc->bfe_tx_list[cur];
	d->bfe_ctrl |= htole32(BFE_DESC_EOF);

	/* Lastly set SOF on the first descriptor to avoid races. */
	d = &sc->bfe_tx_list[si];
	d->bfe_ctrl |= htole32(BFE_DESC_SOF);

	r1 = &sc->bfe_tx_ring[cur];
	map = r->bfe_map;
	r->bfe_map = r1->bfe_map;
	r1->bfe_map = map;
	r1->bfe_mbuf = *m_head;
	sc->bfe_tx_cnt += nsegs;

	bus_dmamap_sync(sc->bfe_txmbuf_tag, map, BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Set up to transmit a packet.
 */
static void
bfe_start(struct ifnet *ifp)
{
	BFE_LOCK((struct bfe_softc *)ifp->if_softc);
	bfe_start_locked(ifp);
	BFE_UNLOCK((struct bfe_softc *)ifp->if_softc);
}

/*
 * Set up to transmit a packet. The softc is already locked.
 */
static void
bfe_start_locked(struct ifnet *ifp)
{
	struct bfe_softc *sc;
	struct mbuf *m_head;
	int queued;

	sc = ifp->if_softc;

	BFE_LOCK_ASSERT(sc);

	/*
	 * Not much point trying to send if the link is down
	 * or we have nothing to send.
	 */
	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->bfe_flags & BFE_FLAG_LINK) == 0)
		return;

	for (queued = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->bfe_tx_cnt < BFE_TX_LIST_CNT - 1;) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/*
		 * Pack the data into the tx ring.  If we dont have
		 * enough room, let the chip drain the ring.
		 */
		if (bfe_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		queued++;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, m_head);
	}

	if (queued) {
		bus_dmamap_sync(sc->bfe_tx_tag, sc->bfe_tx_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/* Transmit - twice due to apparent hardware bug */
		CSR_WRITE_4(sc, BFE_DMATX_PTR,
		    sc->bfe_tx_prod * sizeof(struct bfe_desc));
		/*
		 * XXX It seems the following write is not necessary
		 * to kick Tx command. What might be required would be
		 * a way flushing PCI posted write. Reading the register
		 * back ensures the flush operation. In addition,
		 * hardware will execute PCI posted write in the long
		 * run and watchdog timer for the kick command was set
		 * to 5 seconds. Therefore I think the second write
		 * access is not necessary or could be replaced with
		 * read operation.
		 */
		CSR_WRITE_4(sc, BFE_DMATX_PTR,
		    sc->bfe_tx_prod * sizeof(struct bfe_desc));

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		sc->bfe_watchdog_timer = 5;
	}
}

static void
bfe_init(void *xsc)
{
	BFE_LOCK((struct bfe_softc *)xsc);
	bfe_init_locked(xsc);
	BFE_UNLOCK((struct bfe_softc *)xsc);
}

static void
bfe_init_locked(void *xsc)
{
	struct bfe_softc *sc = (struct bfe_softc*)xsc;
	struct ifnet *ifp = sc->bfe_ifp;
	struct mii_data *mii;

	BFE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->bfe_miibus);

	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	bfe_stop(sc);
	bfe_chip_reset(sc);

	if (bfe_list_rx_init(sc) == ENOBUFS) {
		device_printf(sc->bfe_dev,
		    "%s: Not enough memory for list buffers\n", __func__);
		bfe_stop(sc);
		return;
	}
	bfe_list_tx_init(sc);

	bfe_set_rx_mode(sc);

	/* Enable the chip and core */
	BFE_OR(sc, BFE_ENET_CTRL, BFE_ENET_ENABLE);
	/* Enable interrupts */
	CSR_WRITE_4(sc, BFE_IMASK, BFE_IMASK_DEF);

	/* Clear link state and change media. */
	sc->bfe_flags &= ~BFE_FLAG_LINK;
	mii_mediachg(mii);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->bfe_stat_co, hz, bfe_tick, sc);
}

/*
 * Set media options.
 */
static int
bfe_ifmedia_upd(struct ifnet *ifp)
{
	struct bfe_softc *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;
	BFE_LOCK(sc);

	mii = device_get_softc(sc->bfe_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	BFE_UNLOCK(sc);

	return (error);
}

/*
 * Report current media status.
 */
static void
bfe_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct bfe_softc *sc = ifp->if_softc;
	struct mii_data *mii;

	BFE_LOCK(sc);
	mii = device_get_softc(sc->bfe_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	BFE_UNLOCK(sc);
}

static int
bfe_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct bfe_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	struct mii_data *mii;
	int error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		BFE_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				bfe_set_rx_mode(sc);
			else if ((sc->bfe_flags & BFE_FLAG_DETACH) == 0)
				bfe_init_locked(sc);
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			bfe_stop(sc);
		BFE_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		BFE_LOCK(sc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			bfe_set_rx_mode(sc);
		BFE_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->bfe_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
bfe_watchdog(struct bfe_softc *sc)
{
	struct ifnet *ifp;

	BFE_LOCK_ASSERT(sc);

	if (sc->bfe_watchdog_timer == 0 || --sc->bfe_watchdog_timer)
		return;

	ifp = sc->bfe_ifp;

	device_printf(sc->bfe_dev, "watchdog timeout -- resetting\n");

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	bfe_init_locked(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		bfe_start_locked(ifp);
}

static void
bfe_tick(void *xsc)
{
	struct bfe_softc *sc = xsc;
	struct mii_data *mii;

	BFE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->bfe_miibus);
	mii_tick(mii);
	bfe_stats_update(sc);
	bfe_watchdog(sc);
	callout_reset(&sc->bfe_stat_co, hz, bfe_tick, sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
bfe_stop(struct bfe_softc *sc)
{
	struct ifnet *ifp;

	BFE_LOCK_ASSERT(sc);

	ifp = sc->bfe_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->bfe_flags &= ~BFE_FLAG_LINK;
	callout_stop(&sc->bfe_stat_co);
	sc->bfe_watchdog_timer = 0;

	bfe_chip_halt(sc);
	bfe_tx_ring_free(sc);
	bfe_rx_ring_free(sc);
}

static int
sysctl_bfe_stats(SYSCTL_HANDLER_ARGS)
{
	struct bfe_softc *sc;
	struct bfe_hw_stats *stats;
	int error, result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);

	if (error != 0 || req->newptr == NULL)
		return (error);

	if (result != 1)
		return (error);

	sc = (struct bfe_softc *)arg1;
	stats = &sc->bfe_stats;

	printf("%s statistics:\n", device_get_nameunit(sc->bfe_dev));
	printf("Transmit good octets : %ju\n",
	    (uintmax_t)stats->tx_good_octets);
	printf("Transmit good frames : %ju\n",
	    (uintmax_t)stats->tx_good_frames);
	printf("Transmit octets : %ju\n",
	    (uintmax_t)stats->tx_octets);
	printf("Transmit frames : %ju\n",
	    (uintmax_t)stats->tx_frames);
	printf("Transmit broadcast frames : %ju\n",
	    (uintmax_t)stats->tx_bcast_frames);
	printf("Transmit multicast frames : %ju\n",
	    (uintmax_t)stats->tx_mcast_frames);
	printf("Transmit frames 64 bytes : %ju\n",
	    (uint64_t)stats->tx_pkts_64);
	printf("Transmit frames 65 to 127 bytes : %ju\n",
	    (uint64_t)stats->tx_pkts_65_127);
	printf("Transmit frames 128 to 255 bytes : %ju\n",
	    (uint64_t)stats->tx_pkts_128_255);
	printf("Transmit frames 256 to 511 bytes : %ju\n",
	    (uint64_t)stats->tx_pkts_256_511);
	printf("Transmit frames 512 to 1023 bytes : %ju\n",
	    (uint64_t)stats->tx_pkts_512_1023);
	printf("Transmit frames 1024 to max bytes : %ju\n",
	    (uint64_t)stats->tx_pkts_1024_max);
	printf("Transmit jabber errors : %u\n", stats->tx_jabbers);
	printf("Transmit oversized frames : %ju\n",
	    (uint64_t)stats->tx_oversize_frames);
	printf("Transmit fragmented frames : %ju\n",
	    (uint64_t)stats->tx_frag_frames);
	printf("Transmit underruns : %u\n", stats->tx_colls);
	printf("Transmit total collisions : %u\n", stats->tx_single_colls);
	printf("Transmit single collisions : %u\n", stats->tx_single_colls);
	printf("Transmit multiple collisions : %u\n", stats->tx_multi_colls);
	printf("Transmit excess collisions : %u\n", stats->tx_excess_colls);
	printf("Transmit late collisions : %u\n", stats->tx_late_colls);
	printf("Transmit deferrals : %u\n", stats->tx_deferrals);
	printf("Transmit carrier losts : %u\n", stats->tx_carrier_losts);
	printf("Transmit pause frames : %u\n", stats->tx_pause_frames);

	printf("Receive good octets : %ju\n",
	    (uintmax_t)stats->rx_good_octets);
	printf("Receive good frames : %ju\n",
	    (uintmax_t)stats->rx_good_frames);
	printf("Receive octets : %ju\n",
	    (uintmax_t)stats->rx_octets);
	printf("Receive frames : %ju\n",
	    (uintmax_t)stats->rx_frames);
	printf("Receive broadcast frames : %ju\n",
	    (uintmax_t)stats->rx_bcast_frames);
	printf("Receive multicast frames : %ju\n",
	    (uintmax_t)stats->rx_mcast_frames);
	printf("Receive frames 64 bytes : %ju\n",
	    (uint64_t)stats->rx_pkts_64);
	printf("Receive frames 65 to 127 bytes : %ju\n",
	    (uint64_t)stats->rx_pkts_65_127);
	printf("Receive frames 128 to 255 bytes : %ju\n",
	    (uint64_t)stats->rx_pkts_128_255);
	printf("Receive frames 256 to 511 bytes : %ju\n",
	    (uint64_t)stats->rx_pkts_256_511);
	printf("Receive frames 512 to 1023 bytes : %ju\n",
	    (uint64_t)stats->rx_pkts_512_1023);
	printf("Receive frames 1024 to max bytes : %ju\n",
	    (uint64_t)stats->rx_pkts_1024_max);
	printf("Receive jabber errors : %u\n", stats->rx_jabbers);
	printf("Receive oversized frames : %ju\n",
	    (uint64_t)stats->rx_oversize_frames);
	printf("Receive fragmented frames : %ju\n",
	    (uint64_t)stats->rx_frag_frames);
	printf("Receive missed frames : %u\n", stats->rx_missed_frames);
	printf("Receive CRC align errors : %u\n", stats->rx_crc_align_errs);
	printf("Receive undersized frames : %u\n", stats->rx_runts);
	printf("Receive CRC errors : %u\n", stats->rx_crc_errs);
	printf("Receive align errors : %u\n", stats->rx_align_errs);
	printf("Receive symbol errors : %u\n", stats->rx_symbol_errs);
	printf("Receive pause frames : %u\n", stats->rx_pause_frames);
	printf("Receive control frames : %u\n", stats->rx_control_frames);

	return (error);
}
