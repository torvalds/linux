/*-
 * Copyright (c) 2016 Hiroki Mori. All rights reserved.
 * Copyright (C) 2007 
 *	Oleksandr Tymoshenko <gonzo@freebsd.org>. All rights reserved.
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
 * THIS SOFTWFV IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE FV DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWFV, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: $
 * 
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * FV Ethernet interface driver
 * copy from mips/idt/if_kr.c and netbsd code
 */
#include <sys/param.h>
#include <sys/endian.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/socket.h>
#include <sys/taskqueue.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <net/bpf.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>                                              

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

/* Todo: move to options.arm */
/*#define FV_MDIO*/

#ifdef FV_MDIO
#include <dev/mdio/mdio.h>
#include <dev/etherswitch/miiproxy.h>
#include "mdio_if.h"
#endif

MODULE_DEPEND(are, ether, 1, 1, 1);
MODULE_DEPEND(are, miibus, 1, 1, 1);
#ifdef FV_MDIO
MODULE_DEPEND(are, mdio, 1, 1, 1);
#endif

#include "miibus_if.h"

#include <arm/ralink/if_fvreg.h>

#ifdef FV_DEBUG
void dump_txdesc(struct fv_softc *, int);
void dump_status_reg(struct fv_softc *);
#endif

static int fv_attach(device_t);
static int fv_detach(device_t);
static int fv_ifmedia_upd(struct ifnet *);
static void fv_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int fv_ioctl(struct ifnet *, u_long, caddr_t);
static void fv_init(void *);
static void fv_init_locked(struct fv_softc *);
static void fv_link_task(void *, int);
static int fv_miibus_readreg(device_t, int, int);
static void fv_miibus_statchg(device_t);
static int fv_miibus_writereg(device_t, int, int, int);
static int fv_probe(device_t);
static void fv_reset(struct fv_softc *);
static int fv_resume(device_t);
static int fv_rx_ring_init(struct fv_softc *);
static int fv_tx_ring_init(struct fv_softc *);
static int fv_shutdown(device_t);
static void fv_start(struct ifnet *);
static void fv_start_locked(struct ifnet *);
static void fv_stop(struct fv_softc *);
static int fv_suspend(device_t);

static void fv_rx(struct fv_softc *);
static void fv_tx(struct fv_softc *);
static void fv_intr(void *);
static void fv_tick(void *);

static void fv_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int fv_dma_alloc(struct fv_softc *);
static void fv_dma_free(struct fv_softc *);
static int fv_newbuf(struct fv_softc *, int);
static __inline void fv_fixup_rx(struct mbuf *);

static void fv_hinted_child(device_t bus, const char *dname, int dunit);

static void fv_setfilt(struct fv_softc *sc);

static device_method_t fv_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fv_probe),
	DEVMETHOD(device_attach,	fv_attach),
	DEVMETHOD(device_detach,	fv_detach),
	DEVMETHOD(device_suspend,	fv_suspend),
	DEVMETHOD(device_resume,	fv_resume),
	DEVMETHOD(device_shutdown,	fv_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	fv_miibus_readreg),
	DEVMETHOD(miibus_writereg,	fv_miibus_writereg),
#if !defined(FV_MDIO)
	DEVMETHOD(miibus_statchg,	fv_miibus_statchg),
#endif

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	DEVMETHOD(bus_hinted_child,	fv_hinted_child),

	DEVMETHOD_END
};

static driver_t fv_driver = {
	"fv",
	fv_methods,
	sizeof(struct fv_softc)
};

static devclass_t fv_devclass;

DRIVER_MODULE(fv, simplebus, fv_driver, fv_devclass, 0, 0);
#ifdef MII
DRIVER_MODULE(miibus, fv, miibus_driver, miibus_devclass, 0, 0);
#endif

static struct mtx miibus_mtx;
MTX_SYSINIT(miibus_mtx, &miibus_mtx, "are mii lock", MTX_DEF);

#ifdef FV_MDIO
static int fvmdio_probe(device_t);
static int fvmdio_attach(device_t);
static int fvmdio_detach(device_t);

/*
 * Declare an additional, separate driver for accessing the MDIO bus.
 */
static device_method_t fvmdio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		fvmdio_probe),
	DEVMETHOD(device_attach,	fvmdio_attach),
	DEVMETHOD(device_detach,	fvmdio_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
        
	/* MDIO access */
	DEVMETHOD(mdio_readreg,		fv_miibus_readreg),
	DEVMETHOD(mdio_writereg,	fv_miibus_writereg),
};

DEFINE_CLASS_0(fvmdio, fvmdio_driver, fvmdio_methods,
    sizeof(struct fv_softc));
static devclass_t fvmdio_devclass;

DRIVER_MODULE(miiproxy, fv, miiproxy_driver, miiproxy_devclass, 0, 0);
DRIVER_MODULE(fvmdio, simplebus, fvmdio_driver, fvmdio_devclass, 0, 0);
DRIVER_MODULE(mdio, fvmdio, mdio_driver, mdio_devclass, 0, 0);
#endif

/* setup frame code refer dc code */

static void
fv_setfilt(struct fv_softc *sc)
{
	uint16_t eaddr[(ETHER_ADDR_LEN+1)/2];
	struct fv_desc *sframe;
	int i;
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint16_t *sp;
	uint8_t *ma;

	ifp = sc->fv_ifp;

	i = sc->fv_cdata.fv_tx_prod;
	FV_INC(sc->fv_cdata.fv_tx_prod, FV_TX_RING_CNT);
	sc->fv_cdata.fv_tx_cnt++;
	sframe = &sc->fv_rdata.fv_tx_ring[i];
	sp = (uint16_t *)sc->fv_cdata.fv_sf_buff;
	memset(sp, 0xff, FV_SFRAME_LEN);
	
	sframe->fv_addr = sc->fv_rdata.fv_sf_paddr;
	sframe->fv_devcs = ADCTL_Tx_SETUP | FV_DMASIZE(FV_SFRAME_LEN);

	i = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		ma = LLADDR((struct sockaddr_dl *)ifma->ifma_addr);
		sp[i] = sp[i+1] = (ma[1] << 8 | ma[0]);
		i += 2;
		sp[i] = sp[i+1] = (ma[3] << 8 | ma[2]);
		i += 2;
		sp[i] = sp[i+1] = (ma[5] << 8 | ma[4]);
		i += 2;
	}
	if_maddr_runlock(ifp);

	bcopy(IF_LLADDR(sc->fv_ifp), eaddr, ETHER_ADDR_LEN);
	sp[90] = sp[91] = eaddr[0];
	sp[92] = sp[93] = eaddr[1];
	sp[94] = sp[95] = eaddr[2];

	sframe->fv_stat = ADSTAT_OWN;
	bus_dmamap_sync(sc->fv_cdata.fv_tx_ring_tag,
	    sc->fv_cdata.fv_tx_ring_map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->fv_cdata.fv_sf_tag,
	    sc->fv_cdata.fv_sf_buff_map, BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, CSR_TXPOLL, 0xFFFFFFFF);
	DELAY(10000);
}

static int 
fv_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fv,ethernet"))
		return (ENXIO);

	device_set_desc(dev, "FV Ethernet interface");
	return (BUS_PROBE_DEFAULT);
}

static int
fv_attach(device_t dev)
{
	struct ifnet		*ifp;
	struct fv_softc		*sc;
	int			error = 0, rid;
	int			unit;
	int			i;

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	sc->fv_dev = dev;
	sc->fv_ofw = ofw_bus_get_node(dev); 

	i = OF_getprop(sc->fv_ofw, "local-mac-address", (void *)&sc->fv_eaddr, 6);
	if (i != 6) {
		/* hardcode macaddress */
		sc->fv_eaddr[0] = 0x00;
		sc->fv_eaddr[1] = 0x0C;
		sc->fv_eaddr[2] = 0x42;
		sc->fv_eaddr[3] = 0x09;
		sc->fv_eaddr[4] = 0x5E;
		sc->fv_eaddr[5] = 0x6B;
	}

	mtx_init(&sc->fv_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->fv_stat_callout, &sc->fv_mtx, 0);
	TASK_INIT(&sc->fv_link_task, 0, fv_link_task, sc);

	/* Map control/status registers. */
	sc->fv_rid = 0;
	sc->fv_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->fv_rid, 
	    RF_ACTIVE | RF_SHAREABLE);

	if (sc->fv_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->fv_btag = rman_get_bustag(sc->fv_res);
	sc->fv_bhandle = rman_get_bushandle(sc->fv_res);

	/* Allocate interrupts */
	rid = 0;
	sc->fv_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, 
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->fv_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate ifnet structure. */
	ifp = sc->fv_ifp = if_alloc(IFT_ETHER);

	if (ifp == NULL) {
		device_printf(dev, "couldn't allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = fv_ioctl;
	ifp->if_start = fv_start;
	ifp->if_init = fv_init;

	/* ifqmaxlen is sysctl value in net/if.c */
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	ifp->if_capenable = ifp->if_capabilities;

	if (fv_dma_alloc(sc) != 0) {
		error = ENXIO;
		goto fail;
	}

	/* TODO: calculate prescale */
/*
	CSR_WRITE_4(sc, FV_ETHMCP, (165000000 / (1250000 + 1)) & ~1);

	CSR_WRITE_4(sc, FV_MIIMCFG, FV_MIIMCFG_R);
	DELAY(1000);
	CSR_WRITE_4(sc, FV_MIIMCFG, 0);
*/
	CSR_WRITE_4(sc, CSR_BUSMODE, BUSMODE_SWR);
	DELAY(1000);

#ifdef FV_MDIO
	sc->fv_miiproxy = mii_attach_proxy(sc->fv_dev);
#endif

#ifdef MII
	/* Do MII setup. */
	error = mii_attach(dev, &sc->fv_miibus, ifp, fv_ifmedia_upd,
	    fv_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}
#else
	ifmedia_init(&sc->fv_ifmedia, 0, fv_ifmedia_upd, fv_ifmedia_sts);

	ifmedia_add(&sc->fv_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->fv_ifmedia, IFM_ETHER | IFM_AUTO);
#endif

	/* Call MI attach routine. */
	ether_ifattach(ifp, sc->fv_eaddr);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->fv_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, fv_intr, sc, &sc->fv_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error) 
		fv_detach(dev);

	return (error);
}

static int
fv_detach(device_t dev)
{
	struct fv_softc		*sc = device_get_softc(dev);
	struct ifnet		*ifp = sc->fv_ifp;

	KASSERT(mtx_initialized(&sc->fv_mtx), ("vr mutex not initialized"));

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		FV_LOCK(sc);
		sc->fv_detach = 1;
		fv_stop(sc);
		FV_UNLOCK(sc);
		taskqueue_drain(taskqueue_swi, &sc->fv_link_task);
		ether_ifdetach(ifp);
	}
#ifdef MII
	if (sc->fv_miibus)
		device_delete_child(dev, sc->fv_miibus);
#endif
	bus_generic_detach(dev);

	if (sc->fv_intrhand)
		bus_teardown_intr(dev, sc->fv_irq, sc->fv_intrhand);
	if (sc->fv_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->fv_irq);

	if (sc->fv_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->fv_rid, 
		    sc->fv_res);

	if (ifp)
		if_free(ifp);

	fv_dma_free(sc);

	mtx_destroy(&sc->fv_mtx);

	return (0);

}

static int
fv_suspend(device_t dev)
{

	panic("%s", __func__);
	return 0;
}

static int
fv_resume(device_t dev)
{

	panic("%s", __func__);
	return 0;
}

static int
fv_shutdown(device_t dev)
{
	struct fv_softc	*sc;

	sc = device_get_softc(dev);

	FV_LOCK(sc);
	fv_stop(sc);
	FV_UNLOCK(sc);

	return (0);
}

static int
fv_miibus_readbits(struct fv_softc *sc, int count)
{
	int result;

	result = 0;
	while(count--) {
		result <<= 1;
		CSR_WRITE_4(sc, CSR_MIIMNG, MII_RD);
		DELAY(10);
		CSR_WRITE_4(sc, CSR_MIIMNG, MII_RD | MII_CLK);
		DELAY(10);
		if (CSR_READ_4(sc, CSR_MIIMNG) & MII_DIN)
			result |= 1;
	}

	return (result);
}

static int
fv_miibus_writebits(struct fv_softc *sc, int data, int count)
{
	int bit;

	while(count--) {
		bit = ((data) >> count) & 0x1 ? MII_DOUT : 0;
		CSR_WRITE_4(sc, CSR_MIIMNG, bit | MII_WR);
		DELAY(10);
		CSR_WRITE_4(sc, CSR_MIIMNG, bit | MII_WR | MII_CLK);
		DELAY(10);
	}

	return (0);
}

static void
fv_miibus_turnaround(struct fv_softc *sc, int cmd)
{
	if (cmd == MII_WRCMD) {
		fv_miibus_writebits(sc, 0x02, 2);
	} else {
		fv_miibus_readbits(sc, 1);
	}
}

static int
fv_miibus_readreg(device_t dev, int phy, int reg)
{
	struct fv_softc * sc = device_get_softc(dev);
	int		result;

	mtx_lock(&miibus_mtx);
	fv_miibus_writebits(sc, MII_PREAMBLE, 32);
	fv_miibus_writebits(sc, MII_RDCMD, 4);
	fv_miibus_writebits(sc, phy, 5);
	fv_miibus_writebits(sc, reg, 5);
	fv_miibus_turnaround(sc, MII_RDCMD);
	result = fv_miibus_readbits(sc, 16);
	fv_miibus_turnaround(sc, MII_RDCMD);
	mtx_unlock(&miibus_mtx);

	return (result);
}

static int
fv_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct fv_softc * sc = device_get_softc(dev);

	mtx_lock(&miibus_mtx);
	fv_miibus_writebits(sc, MII_PREAMBLE, 32);
	fv_miibus_writebits(sc, MII_WRCMD, 4);
	fv_miibus_writebits(sc, phy, 5);
	fv_miibus_writebits(sc, reg, 5);
	fv_miibus_turnaround(sc, MII_WRCMD);
	fv_miibus_writebits(sc, data, 16);
	mtx_unlock(&miibus_mtx);

	return (0);
}

#if !defined(FV_MDIO)
static void
fv_miibus_statchg(device_t dev)
{
	struct fv_softc		*sc;

	sc = device_get_softc(dev);
	taskqueue_enqueue(taskqueue_swi, &sc->fv_link_task);
}
#endif

static void
fv_link_task(void *arg, int pending)
{
#ifdef MII
	struct fv_softc		*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	/* int			lfdx, mfdx; */

	sc = (struct fv_softc *)arg;

	FV_LOCK(sc);
	mii = device_get_softc(sc->fv_miibus);
	ifp = sc->fv_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		FV_UNLOCK(sc);
		return;
	}

	if (mii->mii_media_status & IFM_ACTIVE) {
		if (IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE)
			sc->fv_link_status = 1;
	} else
		sc->fv_link_status = 0;

	FV_UNLOCK(sc);
#endif
}

static void
fv_reset(struct fv_softc *sc)
{
	int		i;

	CSR_WRITE_4(sc, CSR_BUSMODE, BUSMODE_SWR);

	/*
	 * The chip doesn't take itself out of reset automatically.
	 * We need to do so after 2us.
	 */
	DELAY(1000);
	CSR_WRITE_4(sc, CSR_BUSMODE, 0);

	for (i = 0; i < 1000; i++) {
		/*
		 * Wait a bit for the reset to complete before peeking
		 * at the chip again.
		 */
		DELAY(1000);
		if ((CSR_READ_4(sc, CSR_BUSMODE) & BUSMODE_SWR) == 0)
			break;
	}

	if (CSR_READ_4(sc, CSR_BUSMODE) & BUSMODE_SWR)
		device_printf(sc->fv_dev, "reset time out\n");

	DELAY(1000);
}

static void
fv_init(void *xsc)
{
	struct fv_softc	 *sc = xsc;

	FV_LOCK(sc);
	fv_init_locked(sc);
	FV_UNLOCK(sc);
}

static void
fv_init_locked(struct fv_softc *sc)
{
	struct ifnet		*ifp = sc->fv_ifp;
#ifdef MII
	struct mii_data		*mii;
#endif

	FV_LOCK_ASSERT(sc);

#ifdef MII
	mii = device_get_softc(sc->fv_miibus);
#endif

	fv_stop(sc);
	fv_reset(sc);

	/* Init circular RX list. */
	if (fv_rx_ring_init(sc) != 0) {
		device_printf(sc->fv_dev,
		    "initialization failed: no memory for rx buffers\n");
		fv_stop(sc);
		return;
	}

	/* Init tx descriptors. */
	fv_tx_ring_init(sc);

	/*
	 * Initialize the BUSMODE register.
	 */
	CSR_WRITE_4(sc, CSR_BUSMODE,
	    /* XXX: not sure if this is a good thing or not... */
	    BUSMODE_BAR | BUSMODE_PBL_32LW);

	/*
	 * Initialize the interrupt mask and enable interrupts.
	 */
	/* normal interrupts */
	sc->sc_inten =  STATUS_TI | STATUS_TU | STATUS_RI | STATUS_NIS;

	/* abnormal interrupts */
	sc->sc_inten |= STATUS_TPS | STATUS_TJT | STATUS_UNF |
	    STATUS_RU | STATUS_RPS | STATUS_SE | STATUS_AIS;

	sc->sc_rxint_mask = STATUS_RI|STATUS_RU;
	sc->sc_txint_mask = STATUS_TI|STATUS_UNF|STATUS_TJT;

	sc->sc_rxint_mask &= sc->sc_inten;
	sc->sc_txint_mask &= sc->sc_inten;

	CSR_WRITE_4(sc, CSR_INTEN, sc->sc_inten);
	CSR_WRITE_4(sc, CSR_STATUS, 0xffffffff);

	/*
	 * Give the transmit and receive rings to the chip.
	 */
	CSR_WRITE_4(sc, CSR_TXLIST, FV_TX_RING_ADDR(sc, 0));
	CSR_WRITE_4(sc, CSR_RXLIST, FV_RX_RING_ADDR(sc, 0));

	/*
	 * Set the station address.
	 */
	fv_setfilt(sc);


	/*
	 * Write out the opmode.
	 */
	CSR_WRITE_4(sc, CSR_OPMODE, OPMODE_SR | OPMODE_ST |
	    OPMODE_TR_128 | OPMODE_FDX | OPMODE_SPEED);
	/*
	 * Start the receive process.
	 */
	CSR_WRITE_4(sc, CSR_RXPOLL, RXPOLL_RPD);

	sc->fv_link_status = 1;
#ifdef MII
	mii_mediachg(mii);
#endif

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->fv_stat_callout, hz, fv_tick, sc);
}

static void
fv_start(struct ifnet *ifp)
{
	struct fv_softc	 *sc;

	sc = ifp->if_softc;

	FV_LOCK(sc);
	fv_start_locked(ifp);
	FV_UNLOCK(sc);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 * Use Implicit Chain implementation.
 */
static int
fv_encap(struct fv_softc *sc, struct mbuf **m_head)
{
	struct fv_txdesc	*txd;
	struct fv_desc		*desc;
	struct mbuf		*m;
	bus_dma_segment_t	txsegs[FV_MAXFRAGS];
	int			error, i, nsegs, prod, si;
	int			padlen;
	int			txstat;

	FV_LOCK_ASSERT(sc);

	/*
	 * Some VIA Rhine wants packet buffers to be longword
	 * aligned, but very often our mbufs aren't. Rather than
	 * waste time trying to decide when to copy and when not
	 * to copy, just do it all the time.
	 */
	m = m_defrag(*m_head, M_NOWAIT);
	if (m == NULL) {
		device_printf(sc->fv_dev, "fv_encap m_defrag error\n");
		m_freem(*m_head);
		*m_head = NULL;
		return (ENOBUFS);
	}
	*m_head = m;

	/*
	 * The Rhine chip doesn't auto-pad, so we have to make
	 * sure to pad short frames out to the minimum frame length
	 * ourselves.
	 */
	if ((*m_head)->m_pkthdr.len < FV_MIN_FRAMELEN) {
		m = *m_head;
		padlen = FV_MIN_FRAMELEN - m->m_pkthdr.len;
		if (M_WRITABLE(m) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			if (m == NULL) {
				device_printf(sc->fv_dev, "fv_encap m_dup error\n");
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		if (m->m_next != NULL || M_TRAILINGSPACE(m) < padlen) {
			m = m_defrag(m, M_NOWAIT);
			if (m == NULL) {
				device_printf(sc->fv_dev, "fv_encap m_defrag error\n");
				m_freem(*m_head);
				*m_head = NULL;
				return (ENOBUFS);
			}
		}
		/*
		 * Manually pad short frames, and zero the pad space
		 * to avoid leaking data.
		 */
		bzero(mtod(m, char *) + m->m_pkthdr.len, padlen);
		m->m_pkthdr.len += padlen;
		m->m_len = m->m_pkthdr.len;
		*m_head = m;
	}

	prod = sc->fv_cdata.fv_tx_prod;
	txd = &sc->fv_cdata.fv_txdesc[prod];
	error = bus_dmamap_load_mbuf_sg(sc->fv_cdata.fv_tx_tag, txd->tx_dmamap,
	    *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		device_printf(sc->fv_dev, "fv_encap EFBIG error\n");
		m = m_defrag(*m_head, M_NOWAIT);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->fv_cdata.fv_tx_tag,
		    txd->tx_dmamap, *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
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

	/* Check number of available descriptors. */
	if (sc->fv_cdata.fv_tx_cnt + nsegs >= (FV_TX_RING_CNT - 1)) {
		bus_dmamap_unload(sc->fv_cdata.fv_tx_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}

	txd->tx_m = *m_head;
	bus_dmamap_sync(sc->fv_cdata.fv_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	si = prod;

	/* 
	 * Make a list of descriptors for this packet. 
	 */
	desc = NULL;
	for (i = 0; i < nsegs; i++) {
		desc = &sc->fv_rdata.fv_tx_ring[prod];
		desc->fv_stat = ADSTAT_OWN;
		desc->fv_devcs = txsegs[i].ds_len;
		/* end of descriptor */
		if (prod == FV_TX_RING_CNT - 1)
			desc->fv_devcs |= ADCTL_ER;
		desc->fv_addr = txsegs[i].ds_addr;

		++sc->fv_cdata.fv_tx_cnt;
		FV_INC(prod, FV_TX_RING_CNT);
	}

	/* 
	 * Set mark last fragment with Last/Intr flag
	 */
	if (desc) {
		desc->fv_devcs |= ADCTL_Tx_IC;
		desc->fv_devcs |= ADCTL_Tx_LS;
	}

	/* Update producer index. */
	sc->fv_cdata.fv_tx_prod = prod;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->fv_cdata.fv_tx_ring_tag,
	    sc->fv_cdata.fv_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	txstat = (CSR_READ_4(sc, CSR_STATUS) >> 20) & 7;
	if (txstat == 0 || txstat == 6) {
		/* Transmit Process Stat is stop or suspended */
		desc = &sc->fv_rdata.fv_tx_ring[si];
		desc->fv_devcs |= ADCTL_Tx_FS;
	}
	else {
		/* Get previous descriptor */
		si = (si + FV_TX_RING_CNT - 1) % FV_TX_RING_CNT;
		desc = &sc->fv_rdata.fv_tx_ring[si];
		/* join remain data and flugs */
		desc->fv_devcs &= ~ADCTL_Tx_IC;
		desc->fv_devcs &= ~ADCTL_Tx_LS;
	}


	return (0);
}

static void
fv_start_locked(struct ifnet *ifp)
{
	struct fv_softc		*sc;
	struct mbuf		*m_head;
	int			enq;
	int			txstat;

	sc = ifp->if_softc;

	FV_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || sc->fv_link_status == 0 )
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->fv_cdata.fv_tx_cnt < FV_TX_RING_CNT - 2; ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (fv_encap(sc, &m_head)) {
			if (m_head == NULL)
				break;
			IFQ_DRV_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		enq++;
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		ETHER_BPF_MTAP(ifp, m_head);
	}

	if (enq > 0) {
		txstat = (CSR_READ_4(sc, CSR_STATUS) >> 20) & 7;
		if (txstat == 0 || txstat == 6)
			CSR_WRITE_4(sc, CSR_TXPOLL, TXPOLL_TPD);
	}
}

static void
fv_stop(struct fv_softc *sc)
{
	struct ifnet	    *ifp;

	FV_LOCK_ASSERT(sc);

	ifp = sc->fv_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	callout_stop(&sc->fv_stat_callout);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, CSR_INTEN, 0);

	/* Stop the transmit and receive processes. */
	CSR_WRITE_4(sc, CSR_OPMODE, 0);
	CSR_WRITE_4(sc, CSR_RXLIST, 0);
	CSR_WRITE_4(sc, CSR_TXLIST, 0);

}


static int
fv_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct fv_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
#ifdef MII
	struct mii_data		*mii;
#endif
	int			error;
	int			csr;

	switch (command) {
	case SIOCSIFFLAGS:
		FV_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ sc->fv_if_flags) &
				    IFF_PROMISC) {
					csr = CSR_READ_4(sc, CSR_OPMODE);
					CSR_WRITE_4(sc, CSR_OPMODE, csr |
					    OPMODE_PM | OPMODE_PR);
				}
				if ((ifp->if_flags ^ sc->fv_if_flags) &
				    IFF_ALLMULTI) {
					csr = CSR_READ_4(sc, CSR_OPMODE);
					CSR_WRITE_4(sc, CSR_OPMODE, csr |
					    OPMODE_PM);
				}
			} else {
				if (sc->fv_detach == 0)
					fv_init_locked(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				fv_stop(sc);
		}
		sc->fv_if_flags = ifp->if_flags;
		FV_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
#if 0
		FV_LOCK(sc);
		fv_set_filter(sc);
		FV_UNLOCK(sc);
#endif
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
#ifdef MII
		mii = device_get_softc(sc->fv_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
#else
		error = ifmedia_ioctl(ifp, ifr, &sc->fv_ifmedia, command);
#endif
		break;
	case SIOCSIFCAP:
		error = 0;
#if 0
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_HWCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_HWCSUM;
			if ((IFCAP_HWCSUM & ifp->if_capenable) &&
			    (IFCAP_HWCSUM & ifp->if_capabilities))
				ifp->if_hwassist = FV_CSUM_FEATURES;
			else
				ifp->if_hwassist = 0;
		}
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if (IFCAP_VLAN_HWTAGGING & ifp->if_capenable &&
			    IFCAP_VLAN_HWTAGGING & ifp->if_capabilities &&
			    ifp->if_drv_flags & IFF_DRV_RUNNING) {
				FV_LOCK(sc);
				fv_vlan_setup(sc);
				FV_UNLOCK(sc);
			}
		}
		VLAN_CAPABILITIES(ifp);
#endif
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

/*
 * Set media options.
 */
static int
fv_ifmedia_upd(struct ifnet *ifp)
{
#ifdef MII
	struct fv_softc		*sc;
	struct mii_data		*mii;
	struct mii_softc	*miisc;
	int			error;

	sc = ifp->if_softc;
	FV_LOCK(sc);
	mii = device_get_softc(sc->fv_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	FV_UNLOCK(sc);

	return (error);
#else
	return (0);
#endif
}

/*
 * Report current media status.
 */
static void
fv_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
#ifdef MII
	struct fv_softc		*sc = ifp->if_softc;
	struct mii_data		*mii;

	mii = device_get_softc(sc->fv_miibus);
	FV_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	FV_UNLOCK(sc);
#else
	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
#endif
}

struct fv_dmamap_arg {
	bus_addr_t	fv_busaddr;
};

static void
fv_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct fv_dmamap_arg	*ctx;

	if (error != 0)
		return;
	ctx = arg;
	ctx->fv_busaddr = segs[0].ds_addr;
}

static int
fv_dma_alloc(struct fv_softc *sc)
{
	struct fv_dmamap_arg	ctx;
	struct fv_txdesc	*txd;
	struct fv_rxdesc	*rxd;
	int			error, i;

	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->fv_dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->fv_cdata.fv_parent_tag);
	if (error != 0) {
		device_printf(sc->fv_dev, "failed to create parent DMA tag\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(
	    sc->fv_cdata.fv_parent_tag,	/* parent */
	    FV_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    FV_TX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    FV_TX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->fv_cdata.fv_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->fv_dev, "failed to create Tx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(
	    sc->fv_cdata.fv_parent_tag,	/* parent */
	    FV_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    FV_RX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    FV_RX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->fv_cdata.fv_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->fv_dev, "failed to create Rx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(
	    sc->fv_cdata.fv_parent_tag,	/* parent */
	    1, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * FV_MAXFRAGS,	/* maxsize */
	    FV_MAXFRAGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->fv_cdata.fv_tx_tag);
	if (error != 0) {
		device_printf(sc->fv_dev, "failed to create Tx DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(
	    sc->fv_cdata.fv_parent_tag,	/* parent */
	    FV_RX_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->fv_cdata.fv_rx_tag);
	if (error != 0) {
		device_printf(sc->fv_dev, "failed to create Rx DMA tag\n");
		goto fail;
	}

	/* Create tag for setup frame buffers. */
	error = bus_dma_tag_create(
	    sc->fv_cdata.fv_parent_tag,	/* parent */
	    sizeof(uint32_t), 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    FV_SFRAME_LEN + FV_MIN_FRAMELEN,			/* maxsize */
	    1,				/* nsegments */
	    FV_SFRAME_LEN + FV_MIN_FRAMELEN,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->fv_cdata.fv_sf_tag);
	if (error != 0) {
		device_printf(sc->fv_dev, "failed to create setup frame DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->fv_cdata.fv_tx_ring_tag,
	    (void **)&sc->fv_rdata.fv_tx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->fv_cdata.fv_tx_ring_map);
	if (error != 0) {
		device_printf(sc->fv_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.fv_busaddr = 0;
	error = bus_dmamap_load(sc->fv_cdata.fv_tx_ring_tag,
	    sc->fv_cdata.fv_tx_ring_map, sc->fv_rdata.fv_tx_ring,
	    FV_TX_RING_SIZE, fv_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.fv_busaddr == 0) {
		device_printf(sc->fv_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc->fv_rdata.fv_tx_ring_paddr = ctx.fv_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->fv_cdata.fv_rx_ring_tag,
	    (void **)&sc->fv_rdata.fv_rx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->fv_cdata.fv_rx_ring_map);
	if (error != 0) {
		device_printf(sc->fv_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.fv_busaddr = 0;
	error = bus_dmamap_load(sc->fv_cdata.fv_rx_ring_tag,
	    sc->fv_cdata.fv_rx_ring_map, sc->fv_rdata.fv_rx_ring,
	    FV_RX_RING_SIZE, fv_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.fv_busaddr == 0) {
		device_printf(sc->fv_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc->fv_rdata.fv_rx_ring_paddr = ctx.fv_busaddr;

	/* Allocate DMA'able memory and load the DMA map for setup frame. */
	error = bus_dmamem_alloc(sc->fv_cdata.fv_sf_tag,
	    (void **)&sc->fv_cdata.fv_sf_buff, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->fv_cdata.fv_sf_buff_map);
	if (error != 0) {
		device_printf(sc->fv_dev,
		    "failed to allocate DMA'able memory for setup frame\n");
		goto fail;
	}

	ctx.fv_busaddr = 0;
	error = bus_dmamap_load(sc->fv_cdata.fv_sf_tag,
	    sc->fv_cdata.fv_sf_buff_map, sc->fv_cdata.fv_sf_buff,
	    FV_SFRAME_LEN, fv_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.fv_busaddr == 0) {
		device_printf(sc->fv_dev,
		    "failed to load DMA'able memory for setup frame\n");
		goto fail;
	}
	sc->fv_rdata.fv_sf_paddr = ctx.fv_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < FV_TX_RING_CNT; i++) {
		txd = &sc->fv_cdata.fv_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->fv_cdata.fv_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->fv_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->fv_cdata.fv_rx_tag, 0,
	    &sc->fv_cdata.fv_rx_sparemap)) != 0) {
		device_printf(sc->fv_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < FV_RX_RING_CNT; i++) {
		rxd = &sc->fv_cdata.fv_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->fv_cdata.fv_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->fv_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
fv_dma_free(struct fv_softc *sc)
{
	struct fv_txdesc	*txd;
	struct fv_rxdesc	*rxd;
	int			i;

	/* Tx ring. */
	if (sc->fv_cdata.fv_tx_ring_tag) {
		if (sc->fv_rdata.fv_tx_ring_paddr)
			bus_dmamap_unload(sc->fv_cdata.fv_tx_ring_tag,
			    sc->fv_cdata.fv_tx_ring_map);
		if (sc->fv_rdata.fv_tx_ring)
			bus_dmamem_free(sc->fv_cdata.fv_tx_ring_tag,
			    sc->fv_rdata.fv_tx_ring,
			    sc->fv_cdata.fv_tx_ring_map);
		sc->fv_rdata.fv_tx_ring = NULL;
		sc->fv_rdata.fv_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->fv_cdata.fv_tx_ring_tag);
		sc->fv_cdata.fv_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->fv_cdata.fv_rx_ring_tag) {
		if (sc->fv_rdata.fv_rx_ring_paddr)
			bus_dmamap_unload(sc->fv_cdata.fv_rx_ring_tag,
			    sc->fv_cdata.fv_rx_ring_map);
		if (sc->fv_rdata.fv_rx_ring)
			bus_dmamem_free(sc->fv_cdata.fv_rx_ring_tag,
			    sc->fv_rdata.fv_rx_ring,
			    sc->fv_cdata.fv_rx_ring_map);
		sc->fv_rdata.fv_rx_ring = NULL;
		sc->fv_rdata.fv_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->fv_cdata.fv_rx_ring_tag);
		sc->fv_cdata.fv_rx_ring_tag = NULL;
	}
	/* Tx buffers. */
	if (sc->fv_cdata.fv_tx_tag) {
		for (i = 0; i < FV_TX_RING_CNT; i++) {
			txd = &sc->fv_cdata.fv_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc->fv_cdata.fv_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->fv_cdata.fv_tx_tag);
		sc->fv_cdata.fv_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc->fv_cdata.fv_rx_tag) {
		for (i = 0; i < FV_RX_RING_CNT; i++) {
			rxd = &sc->fv_cdata.fv_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc->fv_cdata.fv_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->fv_cdata.fv_rx_sparemap) {
			bus_dmamap_destroy(sc->fv_cdata.fv_rx_tag,
			    sc->fv_cdata.fv_rx_sparemap);
			sc->fv_cdata.fv_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc->fv_cdata.fv_rx_tag);
		sc->fv_cdata.fv_rx_tag = NULL;
	}

	if (sc->fv_cdata.fv_parent_tag) {
		bus_dma_tag_destroy(sc->fv_cdata.fv_parent_tag);
		sc->fv_cdata.fv_parent_tag = NULL;
	}
}

/*
 * Initialize the transmit descriptors.
 */
static int
fv_tx_ring_init(struct fv_softc *sc)
{
	struct fv_ring_data	*rd;
	struct fv_txdesc	*txd;
	bus_addr_t		addr;
	int			i;

	sc->fv_cdata.fv_tx_prod = 0;
	sc->fv_cdata.fv_tx_cons = 0;
	sc->fv_cdata.fv_tx_cnt = 0;
	sc->fv_cdata.fv_tx_pkts = 0;

	rd = &sc->fv_rdata;
	bzero(rd->fv_tx_ring, FV_TX_RING_SIZE);
	for (i = 0; i < FV_TX_RING_CNT; i++) {
		if (i == FV_TX_RING_CNT - 1)
			addr = FV_TX_RING_ADDR(sc, 0);
		else
			addr = FV_TX_RING_ADDR(sc, i + 1);
		rd->fv_tx_ring[i].fv_stat = 0;
		rd->fv_tx_ring[i].fv_devcs = 0;
		rd->fv_tx_ring[i].fv_addr = 0;
		rd->fv_tx_ring[i].fv_link = addr;
		txd = &sc->fv_cdata.fv_txdesc[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->fv_cdata.fv_tx_ring_tag,
	    sc->fv_cdata.fv_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
fv_rx_ring_init(struct fv_softc *sc)
{
	struct fv_ring_data	*rd;
	struct fv_rxdesc	*rxd;
	int			i;

	sc->fv_cdata.fv_rx_cons = 0;

	rd = &sc->fv_rdata;
	bzero(rd->fv_rx_ring, FV_RX_RING_SIZE);
	for (i = 0; i < FV_RX_RING_CNT; i++) {
		rxd = &sc->fv_cdata.fv_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->desc = &rd->fv_rx_ring[i];
		rd->fv_rx_ring[i].fv_stat = ADSTAT_OWN;
		rd->fv_rx_ring[i].fv_devcs = 0;
		if (i == FV_RX_RING_CNT - 1)
			rd->fv_rx_ring[i].fv_devcs |= ADCTL_ER;
		rd->fv_rx_ring[i].fv_addr = 0;
		if (fv_newbuf(sc, i) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->fv_cdata.fv_rx_ring_tag,
	    sc->fv_cdata.fv_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
fv_newbuf(struct fv_softc *sc, int idx)
{
	struct fv_desc		*desc;
	struct fv_rxdesc	*rxd;
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int			nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/* tcp header boundary alignment margin */
	m_adj(m, 4);

	if (bus_dmamap_load_mbuf_sg(sc->fv_cdata.fv_rx_tag,
	    sc->fv_cdata.fv_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc->fv_cdata.fv_rxdesc[idx];
	if (rxd->rx_m != NULL) {
/* This code make bug. Make scranble on buffer data. 
		bus_dmamap_sync(sc->fv_cdata.fv_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
*/
		bus_dmamap_unload(sc->fv_cdata.fv_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->fv_cdata.fv_rx_sparemap;
	sc->fv_cdata.fv_rx_sparemap = map;
	bus_dmamap_sync(sc->fv_cdata.fv_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	desc = rxd->desc;
	desc->fv_addr = segs[0].ds_addr;
	desc->fv_devcs |= FV_DMASIZE(segs[0].ds_len);
	rxd->saved_ca = desc->fv_addr ;
	rxd->saved_ctl = desc->fv_stat ;

	return (0);
}

static __inline void
fv_fixup_rx(struct mbuf *m)
{
	int		i;
	uint16_t	*src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - 1;

	for (i = 0; i < m->m_len / sizeof(uint16_t); i++) {
		*dst++ = *src++;
	}

	if (m->m_len % sizeof(uint16_t))
		*(uint8_t *)dst = *(uint8_t *)src;

	m->m_data -= ETHER_ALIGN;
}


static void
fv_tx(struct fv_softc *sc)
{
	struct fv_txdesc	*txd;
	struct fv_desc		*cur_tx;
	struct ifnet		*ifp;
	uint32_t		ctl, devcs;
	int			cons, prod, prev_cons;

	FV_LOCK_ASSERT(sc);

	cons = sc->fv_cdata.fv_tx_cons;
	prod = sc->fv_cdata.fv_tx_prod;
	if (cons == prod)
		return;

	bus_dmamap_sync(sc->fv_cdata.fv_tx_ring_tag,
	    sc->fv_cdata.fv_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	ifp = sc->fv_ifp;
	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	prev_cons = cons;
	for (; cons != prod; FV_INC(cons, FV_TX_RING_CNT)) {
		cur_tx = &sc->fv_rdata.fv_tx_ring[cons];
		ctl = cur_tx->fv_stat;
		devcs = cur_tx->fv_devcs;
		/* Check if descriptor has "finished" flag */
		if (FV_DMASIZE(devcs) == 0)
			break;

		sc->fv_cdata.fv_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		txd = &sc->fv_cdata.fv_txdesc[cons];

		if ((ctl & ADSTAT_Tx_ES) == 0)
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		else if (ctl & ADSTAT_Tx_UF) {   /* only underflow not check collision */
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		}

		bus_dmamap_sync(sc->fv_cdata.fv_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->fv_cdata.fv_tx_tag, txd->tx_dmamap);

		/* Free only if it's first descriptor in list */
		if (txd->tx_m)
			m_freem(txd->tx_m);
		txd->tx_m = NULL;

		/* reset descriptor */
		cur_tx->fv_stat = 0;
		cur_tx->fv_devcs = 0;
		cur_tx->fv_addr = 0;
	}

	sc->fv_cdata.fv_tx_cons = cons;

	bus_dmamap_sync(sc->fv_cdata.fv_tx_ring_tag,
	    sc->fv_cdata.fv_tx_ring_map, BUS_DMASYNC_PREWRITE);
}


static void
fv_rx(struct fv_softc *sc)
{
	struct fv_rxdesc	*rxd;
	struct ifnet		*ifp = sc->fv_ifp;
	int			cons, prog, packet_len, error;
	struct fv_desc		*cur_rx;
	struct mbuf		*m;

	FV_LOCK_ASSERT(sc);

	cons = sc->fv_cdata.fv_rx_cons;

	bus_dmamap_sync(sc->fv_cdata.fv_rx_ring_tag,
	    sc->fv_cdata.fv_rx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; prog < FV_RX_RING_CNT; FV_INC(cons, FV_RX_RING_CNT)) {
		cur_rx = &sc->fv_rdata.fv_rx_ring[cons];
		rxd = &sc->fv_cdata.fv_rxdesc[cons];
		m = rxd->rx_m;

		if ((cur_rx->fv_stat & ADSTAT_OWN) == ADSTAT_OWN)
		       break;	
		
		prog++;

		if (cur_rx->fv_stat & (ADSTAT_ES | ADSTAT_Rx_TL)) {
			device_printf(sc->fv_dev, 
			    "Receive Descriptor error %x\n", cur_rx->fv_stat);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			packet_len = 0;
		} else {
			packet_len = ADSTAT_Rx_LENGTH(cur_rx->fv_stat);
		}
	
		/* Assume it's error */
		error = 1;

		if (packet_len < 64)
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		else if ((cur_rx->fv_stat & ADSTAT_Rx_DE) == 0) {
			error = 0;
			bus_dmamap_sync(sc->fv_cdata.fv_rx_tag, rxd->rx_dmamap,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			m = rxd->rx_m;
			/* Skip 4 bytes of CRC */
			m->m_pkthdr.len = m->m_len = packet_len - ETHER_CRC_LEN;

			fv_fixup_rx(m);
			m->m_pkthdr.rcvif = ifp;
			if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

			FV_UNLOCK(sc);
			(*ifp->if_input)(ifp, m);
			FV_LOCK(sc);
		}

		if (error) {
			/* Restore CONTROL and CA values, reset DEVCS */
			cur_rx->fv_stat = rxd->saved_ctl;
			cur_rx->fv_addr = rxd->saved_ca;
			cur_rx->fv_devcs = 0;
		}
		else {
			/* Reinit descriptor */
			cur_rx->fv_stat = ADSTAT_OWN;
			cur_rx->fv_devcs = 0;
			if (cons == FV_RX_RING_CNT - 1)
				cur_rx->fv_devcs |= ADCTL_ER;
			cur_rx->fv_addr = 0;
			if (fv_newbuf(sc, cons) != 0) {
				device_printf(sc->fv_dev, 
				    "Failed to allocate buffer\n");
				break;
			}
		}

		bus_dmamap_sync(sc->fv_cdata.fv_rx_ring_tag,
		    sc->fv_cdata.fv_rx_ring_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	}

	if (prog > 0) {
		sc->fv_cdata.fv_rx_cons = cons;

		bus_dmamap_sync(sc->fv_cdata.fv_rx_ring_tag,
		    sc->fv_cdata.fv_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

static void
fv_intr(void *arg)
{
	struct fv_softc		*sc = arg;
	uint32_t		status;
	struct ifnet		*ifp = sc->fv_ifp;

	FV_LOCK(sc);

	status = CSR_READ_4(sc, CSR_STATUS);
	/* mask out interrupts */
	while((status & sc->sc_inten) != 0) {
		if (status) {
			CSR_WRITE_4(sc, CSR_STATUS, status);
		}
		if (status & STATUS_UNF) {
			device_printf(sc->fv_dev, "Transmit Underflow\n");
		}
		if (status & sc->sc_rxint_mask) {
			fv_rx(sc);
		}
		if (status & sc->sc_txint_mask) {
			fv_tx(sc);
		}
		if (status & STATUS_AIS) {
			device_printf(sc->fv_dev, "Abnormal Interrupt %x\n",
			    status);
		}
		CSR_WRITE_4(sc, CSR_FULLDUP, FULLDUP_CS | 
		    (1 << FULLDUP_TT_SHIFT) | (3 << FULLDUP_NTP_SHIFT) | 
		    (2 << FULLDUP_RT_SHIFT) | (2 << FULLDUP_NRP_SHIFT));


		status = CSR_READ_4(sc, CSR_STATUS);
	}

	/* Try to get more packets going. */
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		fv_start_locked(ifp);

	FV_UNLOCK(sc);
}

static void
fv_tick(void *xsc)
{
	struct fv_softc		*sc = xsc;
#ifdef MII
	struct mii_data		*mii;

	FV_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->fv_miibus);
	mii_tick(mii);
#endif
	callout_reset(&sc->fv_stat_callout, hz, fv_tick, sc);
}

static void
fv_hinted_child(device_t bus, const char *dname, int dunit)
{
	BUS_ADD_CHILD(bus, 0, dname, dunit);
	device_printf(bus, "hinted child %s%d\n", dname, dunit);
}

#ifdef FV_MDIO
static int
fvmdio_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "fv,mdio"))
		return (ENXIO);

	device_set_desc(dev, "FV built-in ethernet interface, MDIO controller");
	return(0);
}

static int
fvmdio_attach(device_t dev)
{
	struct fv_softc	*sc;
	int	error;

	sc = device_get_softc(dev);
	sc->fv_dev = dev;
	sc->fv_rid = 0;
	sc->fv_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->fv_rid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->fv_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->fv_btag = rman_get_bustag(sc->fv_res);
	sc->fv_bhandle = rman_get_bushandle(sc->fv_res);

        bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	error = bus_generic_attach(dev);
fail:
	return(error);
}

static int
fvmdio_detach(device_t dev)
{
	return(0);
}
#endif

#ifdef FV_DEBUG
void
dump_txdesc(struct fv_softc *sc, int pos)
{
	struct fv_desc		*desc;

	desc = &sc->fv_rdata.fv_tx_ring[pos];
	device_printf(sc->fv_dev, "CSR_TXLIST %08x\n", CSR_READ_4(sc, CSR_TXLIST));
	device_printf(sc->fv_dev, "%d TDES0:%08x TDES1:%08x TDES2:%08x TDES3:%08x\n",
	    pos, desc->fv_stat, desc->fv_devcs, desc->fv_addr, desc->fv_link);
}

void 
dump_status_reg(struct fv_softc *sc)
{
	uint32_t		status;

	/* mask out interrupts */

	status = CSR_READ_4(sc, CSR_STATUS);
	device_printf(sc->fv_dev, "CSR5 Status Register EB:%d TS:%d RS:%d NIS:%d AIS:%d ER:%d SE:%d LNF:%d TM:%d RWT:%d RPS:%d RU:%d RI:%d UNF:%d LNP/ANC:%d TJT:%d TU:%d TPS:%d TI:%d\n", 
	    (status >> 23 ) & 7,
	    (status >> 20 ) & 7,
	    (status >> 17 ) & 7,
	    (status >> 16 ) & 1,
	    (status >> 15 ) & 1,
	    (status >> 14 ) & 1,
	    (status >> 13 ) & 1,
	    (status >> 12 ) & 1,
	    (status >> 11 ) & 1,
	    (status >> 9 ) & 1,
	    (status >> 8 ) & 1,
	    (status >> 7 ) & 1,
	    (status >> 6 ) & 1,
	    (status >> 5 ) & 1,
	    (status >> 4 ) & 1,
	    (status >> 3 ) & 1,
	    (status >> 2 ) & 1,
	    (status >> 1 ) & 1,
	    (status >> 0 ) & 1);

}
#endif
