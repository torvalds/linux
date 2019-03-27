/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2008 Nathan Whitehorn. All rights reserved.
 * Copyright 2003 by Peter Grehan. All rights reserved.
 * Copyright (C) 1998, 1999, 2000 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From:
 *   NetBSD: if_bm.c,v 1.9.2.1 2000/11/01 15:02:49 tv Exp
 */

/*
 * BMAC/BMAC+ Macio cell 10/100 ethernet driver
 * 	The low-cost, low-feature Apple variant of the Sun HME
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <machine/pio.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>
#include <machine/dbdma.h>

MODULE_DEPEND(bm, ether, 1, 1, 1);
MODULE_DEPEND(bm, miibus, 1, 1, 1);

/* "controller miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

#include "if_bmreg.h"
#include "if_bmvar.h"

static int bm_probe		(device_t);
static int bm_attach		(device_t);
static int bm_detach		(device_t);
static int bm_shutdown		(device_t);

static void bm_start		(struct ifnet *);
static void bm_start_locked	(struct ifnet *);
static int bm_encap 		(struct bm_softc *sc, struct mbuf **m_head);
static int bm_ioctl		(struct ifnet *, u_long, caddr_t);
static void bm_init		(void *);
static void bm_init_locked	(struct bm_softc *sc);
static void bm_chip_setup	(struct bm_softc *sc);
static void bm_stop		(struct bm_softc *sc);
static void bm_setladrf		(struct bm_softc *sc);
static void bm_dummypacket	(struct bm_softc *sc);
static void bm_txintr		(void *xsc);
static void bm_rxintr		(void *xsc);

static int bm_add_rxbuf		(struct bm_softc *sc, int i);
static int bm_add_rxbuf_dma	(struct bm_softc *sc, int i);
static void bm_enable_interrupts (struct bm_softc *sc);
static void bm_disable_interrupts (struct bm_softc *sc);
static void bm_tick		(void *xsc);

static int bm_ifmedia_upd	(struct ifnet *);
static void bm_ifmedia_sts	(struct ifnet *, struct ifmediareq *);

static int bm_miibus_readreg	(device_t, int, int);
static int bm_miibus_writereg	(device_t, int, int, int);
static void bm_miibus_statchg	(device_t);

/*
 * MII bit-bang glue
 */
static uint32_t bm_mii_bitbang_read(device_t);
static void bm_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops bm_mii_bitbang_ops = {
	bm_mii_bitbang_read,
	bm_mii_bitbang_write,
	{
		BM_MII_DATAOUT,	/* MII_BIT_MDO */
		BM_MII_DATAIN,	/* MII_BIT_MDI */
		BM_MII_CLK,	/* MII_BIT_MDC */
		BM_MII_OENABLE,	/* MII_BIT_DIR_HOST_PHY */
		0,		/* MII_BIT_DIR_PHY_HOST */
	}
};

static device_method_t bm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		bm_probe),
	DEVMETHOD(device_attach,	bm_attach),
	DEVMETHOD(device_detach,	bm_detach),
	DEVMETHOD(device_shutdown,	bm_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	bm_miibus_readreg),
	DEVMETHOD(miibus_writereg,	bm_miibus_writereg),
	DEVMETHOD(miibus_statchg,	bm_miibus_statchg),

	DEVMETHOD_END
};

static driver_t bm_macio_driver = {
	"bm",
	bm_methods,
	sizeof(struct bm_softc)
};

static devclass_t bm_devclass;

DRIVER_MODULE(bm, macio, bm_macio_driver, bm_devclass, 0, 0);
DRIVER_MODULE(miibus, bm, miibus_driver, miibus_devclass, 0, 0);

/*
 * MII internal routines
 */

/*
 * Write the MII serial port for the MII bit-bang module.
 */
static void
bm_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct bm_softc *sc;

	sc = device_get_softc(dev);

	CSR_WRITE_2(sc, BM_MII_CSR, val);
	CSR_BARRIER(sc, BM_MII_CSR, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/*
 * Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
bm_mii_bitbang_read(device_t dev)
{
	struct bm_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	reg = CSR_READ_2(sc, BM_MII_CSR);
	CSR_BARRIER(sc, BM_MII_CSR, 2,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (reg);
}

/*
 * MII bus i/f
 */
static int
bm_miibus_readreg(device_t dev, int phy, int reg)
{

	return (mii_bitbang_readreg(dev, &bm_mii_bitbang_ops, phy, reg));
}

static int
bm_miibus_writereg(device_t dev, int phy, int reg, int data)
{

	mii_bitbang_readreg(dev, &bm_mii_bitbang_ops, phy, reg);

	return (0);
}

static void
bm_miibus_statchg(device_t dev)
{
	struct bm_softc *sc = device_get_softc(dev);
	uint16_t reg;
	int new_duplex;

	reg = CSR_READ_2(sc, BM_TX_CONFIG);
	new_duplex = IFM_OPTIONS(sc->sc_mii->mii_media_active) & IFM_FDX;

	if (new_duplex != sc->sc_duplex) {
		/* Turn off TX MAC while we fiddle its settings */
		reg &= ~BM_ENABLE;

		CSR_WRITE_2(sc, BM_TX_CONFIG, reg);
		while (CSR_READ_2(sc, BM_TX_CONFIG) & BM_ENABLE)
			DELAY(10);
	}

	if (new_duplex && !sc->sc_duplex)
		reg |= BM_TX_IGNORECOLL | BM_TX_FULLDPX;
	else if (!new_duplex && sc->sc_duplex)
		reg &= ~(BM_TX_IGNORECOLL | BM_TX_FULLDPX);

	if (new_duplex != sc->sc_duplex) {
		/* Turn TX MAC back on */
		reg |= BM_ENABLE;

		CSR_WRITE_2(sc, BM_TX_CONFIG, reg);
		sc->sc_duplex = new_duplex;
	}
}

/*
 * ifmedia/mii callbacks
 */
static int
bm_ifmedia_upd(struct ifnet *ifp)
{
	struct bm_softc *sc = ifp->if_softc;
	int error;

	BM_LOCK(sc);
	error = mii_mediachg(sc->sc_mii);
	BM_UNLOCK(sc);
	return (error);
}

static void
bm_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifm)
{
	struct bm_softc *sc = ifp->if_softc;

	BM_LOCK(sc);
	mii_pollstat(sc->sc_mii);
	ifm->ifm_active = sc->sc_mii->mii_media_active;
	ifm->ifm_status = sc->sc_mii->mii_media_status;
	BM_UNLOCK(sc);
}

/*
 * Macio probe/attach
 */
static int
bm_probe(device_t dev)
{
	const char *dname = ofw_bus_get_name(dev);
	const char *dcompat = ofw_bus_get_compat(dev);

	/*
	 * BMAC+ cells have a name of "ethernet" and
	 * a compatible property of "bmac+"
	 */
	if (strcmp(dname, "bmac") == 0) {
		device_set_desc(dev, "Apple BMAC Ethernet Adaptor");
	} else if (strcmp(dcompat, "bmac+") == 0) {
		device_set_desc(dev, "Apple BMAC+ Ethernet Adaptor");
	} else
		return (ENXIO);

	return (0);
}

static int
bm_attach(device_t dev)
{
	phandle_t node;
	u_char *eaddr;
	struct ifnet *ifp;
	int error, cellid, i;
	struct bm_txsoft *txs;
	struct bm_softc *sc = device_get_softc(dev);

	ifp = sc->sc_ifp = if_alloc(IFT_ETHER);
	ifp->if_softc = sc;
	sc->sc_dev = dev;
	sc->sc_duplex = ~IFM_FDX;

	error = 0;
	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->sc_tick_ch, &sc->sc_mtx, 0);

	/* Check for an improved version of Paddington */
	sc->sc_streaming = 0;
	cellid = -1;
	node = ofw_bus_get_node(dev);

	OF_getprop(node, "cell-id", &cellid, sizeof(cellid));
	if (cellid >= 0xc4)
		sc->sc_streaming = 1;

	sc->sc_memrid = 0;
	sc->sc_memr = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_memrid, RF_ACTIVE);
	if (sc->sc_memr == NULL) {
		device_printf(dev, "Could not alloc chip registers!\n");
		return (ENXIO);
	}

	sc->sc_txdmarid = BM_TXDMA_REGISTERS;
	sc->sc_rxdmarid = BM_RXDMA_REGISTERS;

	sc->sc_txdmar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_txdmarid, RF_ACTIVE);
	sc->sc_rxdmar = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->sc_rxdmarid, RF_ACTIVE);

	if (sc->sc_txdmar == NULL || sc->sc_rxdmar == NULL) {
		device_printf(dev, "Could not map DBDMA registers!\n");
		return (ENXIO);
	}

	error = dbdma_allocate_channel(sc->sc_txdmar, 0, bus_get_dma_tag(dev),
	    BM_MAX_DMA_COMMANDS, &sc->sc_txdma);
	error += dbdma_allocate_channel(sc->sc_rxdmar, 0, bus_get_dma_tag(dev),
	    BM_MAX_DMA_COMMANDS, &sc->sc_rxdma);

	if (error) {
		device_printf(dev,"Could not allocate DBDMA channel!\n");
		return (ENXIO);
	}

	/* alloc DMA tags and buffers */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), 1, 0,
	    BUS_SPACE_MAXADDR_32BIT, BUS_SPACE_MAXADDR, NULL, NULL,
	    BUS_SPACE_MAXSIZE_32BIT, 0, BUS_SPACE_MAXSIZE_32BIT, 0, NULL,
	    NULL, &sc->sc_pdma_tag);

	if (error) {
		device_printf(dev,"Could not allocate DMA tag!\n");
		return (ENXIO);
	}

	error = bus_dma_tag_create(sc->sc_pdma_tag, 1, 0, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, 1, MCLBYTES,
	    BUS_DMA_ALLOCNOW, NULL, NULL, &sc->sc_rdma_tag);

	if (error) {
		device_printf(dev,"Could not allocate RX DMA channel!\n");
		return (ENXIO);
	}

	error = bus_dma_tag_create(sc->sc_pdma_tag, 1, 0, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES * BM_NTXSEGS, BM_NTXSEGS,
	    MCLBYTES, BUS_DMA_ALLOCNOW, NULL, NULL, &sc->sc_tdma_tag);

	if (error) {
		device_printf(dev,"Could not allocate TX DMA tag!\n");
		return (ENXIO);
	}

	/* init transmit descriptors */
	STAILQ_INIT(&sc->sc_txfreeq);
	STAILQ_INIT(&sc->sc_txdirtyq);

	/* create TX DMA maps */
	error = ENOMEM;
	for (i = 0; i < BM_MAX_TX_PACKETS; i++) {
		txs = &sc->sc_txsoft[i];
		txs->txs_mbuf = NULL;
		error = bus_dmamap_create(sc->sc_tdma_tag, 0, &txs->txs_dmamap);
		if (error) {
			device_printf(sc->sc_dev,
			    "unable to create TX DMA map %d, error = %d\n",
			    i, error);
		}
		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}

	/* Create the receive buffer DMA maps. */
	for (i = 0; i < BM_MAX_RX_PACKETS; i++) {
		error = bus_dmamap_create(sc->sc_rdma_tag, 0,
		    &sc->sc_rxsoft[i].rxs_dmamap);
		if (error) {
			device_printf(sc->sc_dev,
			    "unable to create RX DMA map %d, error = %d\n",
			    i, error);
		}
		sc->sc_rxsoft[i].rxs_mbuf = NULL;
	}

	/* alloc interrupt */
	bm_disable_interrupts(sc);

	sc->sc_txdmairqid = BM_TXDMA_INTERRUPT;
	sc->sc_txdmairq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_txdmairqid, RF_ACTIVE);

	if (error) {
		device_printf(dev,"Could not allocate TX interrupt!\n");
		return (ENXIO);
	}

	bus_setup_intr(dev,sc->sc_txdmairq,
	    INTR_TYPE_MISC | INTR_MPSAFE | INTR_ENTROPY, NULL, bm_txintr, sc,
	    &sc->sc_txihtx);

	sc->sc_rxdmairqid = BM_RXDMA_INTERRUPT;
	sc->sc_rxdmairq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->sc_rxdmairqid, RF_ACTIVE);

	if (error) {
		device_printf(dev,"Could not allocate RX interrupt!\n");
		return (ENXIO);
	}

	bus_setup_intr(dev,sc->sc_rxdmairq,
	    INTR_TYPE_MISC | INTR_MPSAFE | INTR_ENTROPY, NULL, bm_rxintr, sc,
	    &sc->sc_rxih);

	/*
	 * Get the ethernet address from OpenFirmware
	 */
	eaddr = sc->sc_enaddr;
	OF_getprop(node, "local-mac-address", eaddr, ETHER_ADDR_LEN);

	/*
	 * Setup MII
	 * On Apple BMAC controllers, we end up in a weird state of
	 * partially-completed autonegotiation on boot.  So we force
	 * autonegotation to try again.
	 */
	error = mii_attach(dev, &sc->sc_miibus, ifp, bm_ifmedia_upd,
	    bm_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY,
	    MIIF_FORCEANEG);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		return (error);
	}

	/* reset the adapter  */
	bm_chip_setup(sc);

	sc->sc_mii = device_get_softc(sc->sc_miibus);

	if_initname(ifp, device_get_name(sc->sc_dev),
	    device_get_unit(sc->sc_dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = bm_start;
	ifp->if_ioctl = bm_ioctl;
	ifp->if_init = bm_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, BM_MAX_TX_PACKETS);
	ifp->if_snd.ifq_drv_maxlen = BM_MAX_TX_PACKETS;
	IFQ_SET_READY(&ifp->if_snd);

	/* Attach the interface. */
	ether_ifattach(ifp, sc->sc_enaddr);
	ifp->if_hwassist = 0;

	gone_by_fcp101_dev(dev);

	return (0);
}

static int
bm_detach(device_t dev)
{
	struct bm_softc *sc = device_get_softc(dev);

	BM_LOCK(sc);
	bm_stop(sc);
	BM_UNLOCK(sc);

	callout_drain(&sc->sc_tick_ch);
	ether_ifdetach(sc->sc_ifp);
	bus_teardown_intr(dev, sc->sc_txdmairq, sc->sc_txihtx);
	bus_teardown_intr(dev, sc->sc_rxdmairq, sc->sc_rxih);

	dbdma_free_channel(sc->sc_txdma);
	dbdma_free_channel(sc->sc_rxdma);

	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_memrid, sc->sc_memr);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_txdmarid,
	    sc->sc_txdmar);
	bus_release_resource(dev, SYS_RES_MEMORY, sc->sc_rxdmarid,
	    sc->sc_rxdmar);

	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_txdmairqid,
	    sc->sc_txdmairq);
	bus_release_resource(dev, SYS_RES_IRQ, sc->sc_rxdmairqid,
	    sc->sc_rxdmairq);

	mtx_destroy(&sc->sc_mtx);
	if_free(sc->sc_ifp);

	return (0);
}

static int
bm_shutdown(device_t dev)
{
	struct bm_softc *sc;
	
	sc = device_get_softc(dev);

	BM_LOCK(sc);
	bm_stop(sc);
	BM_UNLOCK(sc);

	return (0);
}

static void
bm_dummypacket(struct bm_softc *sc)
{
	struct mbuf *m;
	struct ifnet *ifp;

	ifp = sc->sc_ifp;

	MGETHDR(m, M_NOWAIT, MT_DATA);

	if (m == NULL)
		return;

	bcopy(sc->sc_enaddr,
	    mtod(m, struct ether_header *)->ether_dhost, ETHER_ADDR_LEN);
	bcopy(sc->sc_enaddr,
	    mtod(m, struct ether_header *)->ether_shost, ETHER_ADDR_LEN);
	mtod(m, struct ether_header *)->ether_type = htons(3);
	mtod(m, unsigned char *)[14] = 0;
	mtod(m, unsigned char *)[15] = 0;
	mtod(m, unsigned char *)[16] = 0xE3;
	m->m_len = m->m_pkthdr.len = sizeof(struct ether_header) + 3;
	IF_ENQUEUE(&ifp->if_snd, m);
	bm_start_locked(ifp);
}

static void
bm_rxintr(void *xsc)
{
	struct bm_softc *sc = xsc;
	struct ifnet *ifp = sc->sc_ifp;
	struct mbuf *m;
	int i, prev_stop, new_stop;
	uint16_t status;

	BM_LOCK(sc);

	status = dbdma_get_chan_status(sc->sc_rxdma);
	if (status & DBDMA_STATUS_DEAD) {
		dbdma_reset(sc->sc_rxdma);
		BM_UNLOCK(sc);
		return;
	}
	if (!(status & DBDMA_STATUS_RUN)) {
		device_printf(sc->sc_dev,"Bad RX Interrupt!\n");
		BM_UNLOCK(sc);
		return;
	}

	prev_stop = sc->next_rxdma_slot - 1;
	if (prev_stop < 0)
		prev_stop = sc->rxdma_loop_slot - 1;

	if (prev_stop < 0) {
		BM_UNLOCK(sc);
		return;
	}

	new_stop = -1;
	dbdma_sync_commands(sc->sc_rxdma, BUS_DMASYNC_POSTREAD);

	for (i = sc->next_rxdma_slot; i < BM_MAX_RX_PACKETS; i++) {
		if (i == sc->rxdma_loop_slot)
			i = 0;

		if (i == prev_stop)
			break;

		status = dbdma_get_cmd_status(sc->sc_rxdma, i);

		if (status == 0)
			break;

		m = sc->sc_rxsoft[i].rxs_mbuf;

		if (bm_add_rxbuf(sc, i)) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			m = NULL;
			continue;
		}

		if (m == NULL)
			continue;

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		m->m_pkthdr.rcvif = ifp;
		m->m_len -= (dbdma_get_residuals(sc->sc_rxdma, i) + 2);
		m->m_pkthdr.len = m->m_len;

		/* Send up the stack */
		BM_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		BM_LOCK(sc);

		/* Clear all fields on this command */
		bm_add_rxbuf_dma(sc, i);

		new_stop = i;
	}

	/* Change the last packet we processed to the ring buffer terminator,
	 * and restore a receive buffer to the old terminator */
	if (new_stop >= 0) {
		dbdma_insert_stop(sc->sc_rxdma, new_stop);
		bm_add_rxbuf_dma(sc, prev_stop);
		if (i < sc->rxdma_loop_slot)
			sc->next_rxdma_slot = i;
		else
			sc->next_rxdma_slot = 0;
	}
	dbdma_sync_commands(sc->sc_rxdma, BUS_DMASYNC_PREWRITE);

	dbdma_wake(sc->sc_rxdma);

	BM_UNLOCK(sc);
}

static void
bm_txintr(void *xsc)
{
	struct bm_softc *sc = xsc;
	struct ifnet *ifp = sc->sc_ifp;
	struct bm_txsoft *txs;
	int progress = 0;

	BM_LOCK(sc);

	while ((txs = STAILQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
		if (!dbdma_get_cmd_status(sc->sc_txdma, txs->txs_lastdesc))
			break;

		STAILQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);
		bus_dmamap_unload(sc->sc_tdma_tag, txs->txs_dmamap);

		if (txs->txs_mbuf != NULL) {
			m_freem(txs->txs_mbuf);
			txs->txs_mbuf = NULL;
		}

		/* Set the first used TXDMA slot to the location of the
		 * STOP/NOP command associated with this packet. */

		sc->first_used_txdma_slot = txs->txs_stopdesc;

		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		progress = 1;
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
			bm_start_locked(ifp);
	}

	BM_UNLOCK(sc);
}

static void
bm_start(struct ifnet *ifp)
{
	struct bm_softc *sc = ifp->if_softc;

	BM_LOCK(sc);
	bm_start_locked(ifp);
	BM_UNLOCK(sc);
}

static void
bm_start_locked(struct ifnet *ifp)
{
	struct bm_softc *sc = ifp->if_softc;
	struct mbuf *mb_head;
	int prev_stop;
	int txqueued = 0;

	/*
	 * We lay out our DBDMA program in the following manner:
	 *	OUTPUT_MORE
	 *	...
	 *	OUTPUT_LAST (+ Interrupt)
	 *	STOP
	 *
	 * To extend the channel, we append a new program,
	 * then replace STOP with NOP and wake the channel.
	 * If we stalled on the STOP already, the program proceeds,
	 * if not it will sail through the NOP.
	 */

	while (!IFQ_DRV_IS_EMPTY(&ifp->if_snd)) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, mb_head);

		if (mb_head == NULL)
			break;

		prev_stop = sc->next_txdma_slot - 1;

		if (bm_encap(sc, &mb_head)) {
			/* Put the packet back and stop */
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			IFQ_DRV_PREPEND(&ifp->if_snd, mb_head);
			break;
		}

		dbdma_insert_nop(sc->sc_txdma, prev_stop);

		txqueued = 1;

		BPF_MTAP(ifp, mb_head);
	}

	dbdma_sync_commands(sc->sc_txdma, BUS_DMASYNC_PREWRITE);

	if (txqueued) {
		dbdma_wake(sc->sc_txdma);
		sc->sc_wdog_timer = 5;
	}
}

static int
bm_encap(struct bm_softc *sc, struct mbuf **m_head)
{
	bus_dma_segment_t segs[BM_NTXSEGS];
	struct bm_txsoft *txs;
	struct mbuf *m;
	int nsegs = BM_NTXSEGS;
	int error = 0;
	uint8_t branch_type;
	int i;

	/* Limit the command size to the number of free DBDMA slots */

	if (sc->next_txdma_slot >= sc->first_used_txdma_slot)
		nsegs = BM_MAX_DMA_COMMANDS - 2 - sc->next_txdma_slot +
		    sc->first_used_txdma_slot;  /* -2 for branch and indexing */
	else
		nsegs = sc->first_used_txdma_slot - sc->next_txdma_slot;

	/* Remove one slot for the STOP/NOP terminator */
	nsegs--;

	if (nsegs > BM_NTXSEGS)
		nsegs = BM_NTXSEGS;

	/* Get a work queue entry. */
	if ((txs = STAILQ_FIRST(&sc->sc_txfreeq)) == NULL) {
		/* Ran out of descriptors. */
		return (ENOBUFS);
	}

	error = bus_dmamap_load_mbuf_sg(sc->sc_tdma_tag, txs->txs_dmamap,
	    *m_head, segs, &nsegs, BUS_DMA_NOWAIT);

	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, nsegs);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;

		error = bus_dmamap_load_mbuf_sg(sc->sc_tdma_tag,
		    txs->txs_dmamap, *m_head, segs, &nsegs, BUS_DMA_NOWAIT);
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

	txs->txs_ndescs = nsegs;
	txs->txs_firstdesc = sc->next_txdma_slot;

	for (i = 0; i < nsegs; i++) {
		/* Loop back to the beginning if this is our last slot */
		if (sc->next_txdma_slot == (BM_MAX_DMA_COMMANDS - 1))
			branch_type = DBDMA_ALWAYS;
		else
			branch_type = DBDMA_NEVER;

		if (i+1 == nsegs)
			txs->txs_lastdesc = sc->next_txdma_slot;

		dbdma_insert_command(sc->sc_txdma, sc->next_txdma_slot++,
		    (i + 1 < nsegs) ? DBDMA_OUTPUT_MORE : DBDMA_OUTPUT_LAST,
		    0, segs[i].ds_addr, segs[i].ds_len,
		    (i + 1 < nsegs) ? DBDMA_NEVER : DBDMA_ALWAYS,
		    branch_type, DBDMA_NEVER, 0);

		if (branch_type == DBDMA_ALWAYS)
			sc->next_txdma_slot = 0;
	}

	/* We have a corner case where the STOP command is the last slot,
	 * but you can't branch in STOP commands. So add a NOP branch here
	 * and the STOP in slot 0. */

	if (sc->next_txdma_slot == (BM_MAX_DMA_COMMANDS - 1)) {
		dbdma_insert_branch(sc->sc_txdma, sc->next_txdma_slot, 0);
		sc->next_txdma_slot = 0;
	}

	txs->txs_stopdesc = sc->next_txdma_slot;
	dbdma_insert_stop(sc->sc_txdma, sc->next_txdma_slot++);

	STAILQ_REMOVE_HEAD(&sc->sc_txfreeq, txs_q);
	STAILQ_INSERT_TAIL(&sc->sc_txdirtyq, txs, txs_q);
	txs->txs_mbuf = *m_head;

	return (0);
}

static int
bm_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct bm_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error;

	error = 0;

	switch(cmd) {
	case SIOCSIFFLAGS:
		BM_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			   ((ifp->if_flags ^ sc->sc_ifpflags) &
			    (IFF_ALLMULTI | IFF_PROMISC)) != 0)
				bm_setladrf(sc);
			else
				bm_init_locked(sc);
		} else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			bm_stop(sc);
		sc->sc_ifpflags = ifp->if_flags;
		BM_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		BM_LOCK(sc);
		bm_setladrf(sc);
		BM_UNLOCK(sc);
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
bm_setladrf(struct bm_softc *sc)
{
	struct ifnet *ifp = sc->sc_ifp;
	struct ifmultiaddr *inm;
	uint16_t hash[4];
	uint16_t reg;
	uint32_t crc;

	reg = BM_CRC_ENABLE | BM_REJECT_OWN_PKTS;

	/* Turn off RX MAC while we fiddle its settings */
	CSR_WRITE_2(sc, BM_RX_CONFIG, reg);
	while (CSR_READ_2(sc, BM_RX_CONFIG) & BM_ENABLE)
		DELAY(10);

	if ((ifp->if_flags & IFF_PROMISC) != 0) {
		reg |= BM_PROMISC;

		CSR_WRITE_2(sc, BM_RX_CONFIG, reg);

		DELAY(15);

		reg = CSR_READ_2(sc, BM_RX_CONFIG);
		reg |= BM_ENABLE;
		CSR_WRITE_2(sc, BM_RX_CONFIG, reg);
		return;
	}

	if ((ifp->if_flags & IFF_ALLMULTI) != 0) {
		hash[3] = hash[2] = hash[1] = hash[0] = 0xffff;
	} else {
		/* Clear the hash table. */
		memset(hash, 0, sizeof(hash));

		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(inm, &ifp->if_multiaddrs, ifma_link) {
			if (inm->ifma_addr->sa_family != AF_LINK)
				continue;
			crc = ether_crc32_le(LLADDR((struct sockaddr_dl *)
			    inm->ifma_addr), ETHER_ADDR_LEN);

			/* We just want the 6 most significant bits */
			crc >>= 26;

			/* Set the corresponding bit in the filter. */
			hash[crc >> 4] |= 1 << (crc & 0xf);
		}
		if_maddr_runlock(ifp);
	}

	/* Write out new hash table */
	CSR_WRITE_2(sc, BM_HASHTAB0, hash[0]);
	CSR_WRITE_2(sc, BM_HASHTAB1, hash[1]);
	CSR_WRITE_2(sc, BM_HASHTAB2, hash[2]);
	CSR_WRITE_2(sc, BM_HASHTAB3, hash[3]);

	/* And turn the RX MAC back on, this time with the hash bit set */
	reg |= BM_HASH_FILTER_ENABLE;
	CSR_WRITE_2(sc, BM_RX_CONFIG, reg);

	while (!(CSR_READ_2(sc, BM_RX_CONFIG) & BM_HASH_FILTER_ENABLE))
		DELAY(10);

	reg = CSR_READ_2(sc, BM_RX_CONFIG);
	reg |= BM_ENABLE;
	CSR_WRITE_2(sc, BM_RX_CONFIG, reg);
}

static void
bm_init(void *xsc)
{
	struct bm_softc *sc = xsc;

	BM_LOCK(sc);
	bm_init_locked(sc);
	BM_UNLOCK(sc);
}

static void
bm_chip_setup(struct bm_softc *sc)
{
	uint16_t reg;
	uint16_t *eaddr_sect;

	eaddr_sect = (uint16_t *)(sc->sc_enaddr);
	dbdma_stop(sc->sc_txdma);
	dbdma_stop(sc->sc_rxdma);

	/* Reset chip */
	CSR_WRITE_2(sc, BM_RX_RESET, 0x0000);
	CSR_WRITE_2(sc, BM_TX_RESET, 0x0001);
	do {
		DELAY(10);
		reg = CSR_READ_2(sc, BM_TX_RESET);
	} while (reg & 0x0001);

	/* Some random junk. OS X uses the system time. We use
	 * the low 16 bits of the MAC address. */
	CSR_WRITE_2(sc,	BM_TX_RANDSEED, eaddr_sect[2]);

	/* Enable transmit */
	reg = CSR_READ_2(sc, BM_TX_IFC);
	reg |= BM_ENABLE;
	CSR_WRITE_2(sc, BM_TX_IFC, reg);

	CSR_READ_2(sc, BM_TX_PEAKCNT);
}

static void
bm_stop(struct bm_softc *sc)
{
	struct bm_txsoft *txs;
	uint16_t reg;

	/* Disable TX and RX MACs */
	reg = CSR_READ_2(sc, BM_TX_CONFIG);
	reg &= ~BM_ENABLE;
	CSR_WRITE_2(sc, BM_TX_CONFIG, reg);

	reg = CSR_READ_2(sc, BM_RX_CONFIG);
	reg &= ~BM_ENABLE;
	CSR_WRITE_2(sc, BM_RX_CONFIG, reg);

	DELAY(100);

	/* Stop DMA engine */
	dbdma_stop(sc->sc_rxdma);
	dbdma_stop(sc->sc_txdma);
	sc->next_rxdma_slot = 0;
	sc->rxdma_loop_slot = 0;

	/* Disable interrupts */
	bm_disable_interrupts(sc);

	/* Don't worry about pending transmits anymore */
	while ((txs = STAILQ_FIRST(&sc->sc_txdirtyq)) != NULL) {
		STAILQ_REMOVE_HEAD(&sc->sc_txdirtyq, txs_q);
		if (txs->txs_ndescs != 0) {
			bus_dmamap_sync(sc->sc_tdma_tag, txs->txs_dmamap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_tdma_tag, txs->txs_dmamap);
			if (txs->txs_mbuf != NULL) {
				m_freem(txs->txs_mbuf);
				txs->txs_mbuf = NULL;
			}
		}
		STAILQ_INSERT_TAIL(&sc->sc_txfreeq, txs, txs_q);
	}

	/* And we're down */
	sc->sc_ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_wdog_timer = 0;
	callout_stop(&sc->sc_tick_ch);
}

static void
bm_init_locked(struct bm_softc *sc)
{
	uint16_t reg;
	uint16_t *eaddr_sect;
	struct bm_rxsoft *rxs;
	int i;

	eaddr_sect = (uint16_t *)(sc->sc_enaddr);

	/* Zero RX slot info and stop DMA */
	dbdma_stop(sc->sc_rxdma);
	dbdma_stop(sc->sc_txdma);
	sc->next_rxdma_slot = 0;
	sc->rxdma_loop_slot = 0;

	/* Initialize TX/RX DBDMA programs */
	dbdma_insert_stop(sc->sc_rxdma, 0);
	dbdma_insert_stop(sc->sc_txdma, 0);
	dbdma_set_current_cmd(sc->sc_rxdma, 0);
	dbdma_set_current_cmd(sc->sc_txdma, 0);

	sc->next_rxdma_slot = 0;
	sc->next_txdma_slot = 1;
	sc->first_used_txdma_slot = 0;

	for (i = 0; i < BM_MAX_RX_PACKETS; i++) {
		rxs = &sc->sc_rxsoft[i];
		rxs->dbdma_slot = i;

		if (rxs->rxs_mbuf == NULL) {
			bm_add_rxbuf(sc, i);

			if (rxs->rxs_mbuf == NULL) {
				/* If we can't add anymore, mark the problem */
				rxs->dbdma_slot = -1;
				break;
			}
		}

		if (i > 0)
			bm_add_rxbuf_dma(sc, i);
	}

	/*
	 * Now terminate the RX ring buffer, and follow with the loop to
	 * the beginning.
	 */
	dbdma_insert_stop(sc->sc_rxdma, i - 1);
	dbdma_insert_branch(sc->sc_rxdma, i, 0);
	sc->rxdma_loop_slot = i;

	/* Now add in the first element of the RX DMA chain */
	bm_add_rxbuf_dma(sc, 0);

	dbdma_sync_commands(sc->sc_rxdma, BUS_DMASYNC_PREWRITE);
	dbdma_sync_commands(sc->sc_txdma, BUS_DMASYNC_PREWRITE);

	/* Zero collision counters */
	CSR_WRITE_2(sc, BM_TX_NCCNT, 0);
	CSR_WRITE_2(sc, BM_TX_FCCNT, 0);
	CSR_WRITE_2(sc, BM_TX_EXCNT, 0);
	CSR_WRITE_2(sc, BM_TX_LTCNT, 0);

	/* Zero receive counters */
	CSR_WRITE_2(sc, BM_RX_FRCNT, 0);
	CSR_WRITE_2(sc, BM_RX_LECNT, 0);
	CSR_WRITE_2(sc, BM_RX_AECNT, 0);
	CSR_WRITE_2(sc, BM_RX_FECNT, 0);
	CSR_WRITE_2(sc, BM_RXCV, 0);

	/* Prime transmit */
	CSR_WRITE_2(sc, BM_TX_THRESH, 0xff);

	CSR_WRITE_2(sc, BM_TXFIFO_CSR, 0);
	CSR_WRITE_2(sc, BM_TXFIFO_CSR, 0x0001);

	/* Prime receive */
	CSR_WRITE_2(sc, BM_RXFIFO_CSR, 0);
	CSR_WRITE_2(sc, BM_RXFIFO_CSR, 0x0001);

	/* Clear status reg */
	CSR_READ_2(sc, BM_STATUS);

	/* Zero hash filters */
	CSR_WRITE_2(sc, BM_HASHTAB0, 0);
	CSR_WRITE_2(sc, BM_HASHTAB1, 0);
	CSR_WRITE_2(sc, BM_HASHTAB2, 0);
	CSR_WRITE_2(sc, BM_HASHTAB3, 0);

	/* Write MAC address to chip */
	CSR_WRITE_2(sc, BM_MACADDR0, eaddr_sect[0]);
	CSR_WRITE_2(sc, BM_MACADDR1, eaddr_sect[1]);
	CSR_WRITE_2(sc, BM_MACADDR2, eaddr_sect[2]);

	/* Final receive engine setup */
	reg = BM_CRC_ENABLE | BM_REJECT_OWN_PKTS | BM_HASH_FILTER_ENABLE;
	CSR_WRITE_2(sc, BM_RX_CONFIG, reg);

	/* Now turn it all on! */
	dbdma_reset(sc->sc_rxdma);
	dbdma_reset(sc->sc_txdma);

	/* Enable RX and TX MACs. Setting the address filter has
	 * the side effect of enabling the RX MAC. */
	bm_setladrf(sc);

	reg = CSR_READ_2(sc, BM_TX_CONFIG);
	reg |= BM_ENABLE;
	CSR_WRITE_2(sc, BM_TX_CONFIG, reg);

	/*
	 * Enable interrupts, unwedge the controller with a dummy packet,
	 * and nudge the DMA queue.
	 */
	bm_enable_interrupts(sc);
	bm_dummypacket(sc);
	dbdma_wake(sc->sc_rxdma); /* Nudge RXDMA */

	sc->sc_ifp->if_drv_flags |= IFF_DRV_RUNNING;
	sc->sc_ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	sc->sc_ifpflags = sc->sc_ifp->if_flags;

	/* Resync PHY and MAC states */
	sc->sc_mii = device_get_softc(sc->sc_miibus);
	sc->sc_duplex = ~IFM_FDX;
	mii_mediachg(sc->sc_mii);

	/* Start the one second timer. */
	sc->sc_wdog_timer = 0;
	callout_reset(&sc->sc_tick_ch, hz, bm_tick, sc);
}

static void
bm_tick(void *arg)
{
	struct bm_softc *sc = arg;

	/* Read error counters */
	if_inc_counter(sc->sc_ifp, IFCOUNTER_COLLISIONS,
	    CSR_READ_2(sc, BM_TX_NCCNT) + CSR_READ_2(sc, BM_TX_FCCNT) +
	    CSR_READ_2(sc, BM_TX_EXCNT) + CSR_READ_2(sc, BM_TX_LTCNT));

	if_inc_counter(sc->sc_ifp, IFCOUNTER_IERRORS,
	    CSR_READ_2(sc, BM_RX_LECNT) + CSR_READ_2(sc, BM_RX_AECNT) +
	    CSR_READ_2(sc, BM_RX_FECNT));

	/* Zero collision counters */
	CSR_WRITE_2(sc, BM_TX_NCCNT, 0);
	CSR_WRITE_2(sc, BM_TX_FCCNT, 0);
	CSR_WRITE_2(sc, BM_TX_EXCNT, 0);
	CSR_WRITE_2(sc, BM_TX_LTCNT, 0);

	/* Zero receive counters */
	CSR_WRITE_2(sc, BM_RX_FRCNT, 0);
	CSR_WRITE_2(sc, BM_RX_LECNT, 0);
	CSR_WRITE_2(sc, BM_RX_AECNT, 0);
	CSR_WRITE_2(sc, BM_RX_FECNT, 0);
	CSR_WRITE_2(sc, BM_RXCV, 0);

	/* Check for link changes and run watchdog */
	mii_tick(sc->sc_mii);
	bm_miibus_statchg(sc->sc_dev);

	if (sc->sc_wdog_timer == 0 || --sc->sc_wdog_timer != 0) {
		callout_reset(&sc->sc_tick_ch, hz, bm_tick, sc);
		return;
	}

	/* Problems */
	device_printf(sc->sc_dev, "device timeout\n");

	bm_init_locked(sc);
}

static int
bm_add_rxbuf(struct bm_softc *sc, int idx)
{
	struct bm_rxsoft *rxs = &sc->sc_rxsoft[idx];
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	int error, nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = m->m_ext.ext_size;

	if (rxs->rxs_mbuf != NULL) {
		bus_dmamap_sync(sc->sc_rdma_tag, rxs->rxs_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_rdma_tag, rxs->rxs_dmamap);
	}

	error = bus_dmamap_load_mbuf_sg(sc->sc_rdma_tag, rxs->rxs_dmamap, m,
	    segs, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "cannot load RS DMA map %d, error = %d\n", idx, error);
		m_freem(m);
		return (error);
	}
	/* If nsegs is wrong then the stack is corrupt. */
	KASSERT(nsegs == 1,
	    ("%s: too many DMA segments (%d)", __func__, nsegs));
	rxs->rxs_mbuf = m;
	rxs->segment = segs[0];

	bus_dmamap_sync(sc->sc_rdma_tag, rxs->rxs_dmamap, BUS_DMASYNC_PREREAD);

	return (0);
}

static int
bm_add_rxbuf_dma(struct bm_softc *sc, int idx)
{
	struct bm_rxsoft *rxs = &sc->sc_rxsoft[idx];

	dbdma_insert_command(sc->sc_rxdma, idx, DBDMA_INPUT_LAST, 0,
	    rxs->segment.ds_addr, rxs->segment.ds_len, DBDMA_ALWAYS,
	    DBDMA_NEVER, DBDMA_NEVER, 0);

	return (0);
}

static void
bm_enable_interrupts(struct bm_softc *sc)
{
	CSR_WRITE_2(sc, BM_INTR_DISABLE,
	    (sc->sc_streaming) ? BM_INTR_NONE : BM_INTR_NORMAL);
}

static void
bm_disable_interrupts(struct bm_softc *sc)
{
	CSR_WRITE_2(sc, BM_INTR_DISABLE, BM_INTR_NONE);
}
