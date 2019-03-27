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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: $
 * 
 */

#include "opt_platform.h"
#include "opt_ar531x.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * AR531x Ethernet interface driver
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
#include <sys/kdb.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <net/bpf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#ifdef INTRNG
#include <machine/intr.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#ifdef ARE_MDIO
#include <dev/mdio/mdio.h>
#include <dev/etherswitch/miiproxy.h>
#include "mdio_if.h"
#endif

MODULE_DEPEND(are, ether, 1, 1, 1);
MODULE_DEPEND(are, miibus, 1, 1, 1);

#include "miibus_if.h"

#include <mips/atheros/ar531x/ar5315reg.h>
#include <mips/atheros/ar531x/ar5312reg.h>
#include <mips/atheros/ar531x/ar5315_setup.h>
#include <mips/atheros/ar531x/if_arereg.h>

#ifdef ARE_DEBUG
void dump_txdesc(struct are_softc *, int);
void dump_status_reg(struct are_softc *);
#endif

static int are_attach(device_t);
static int are_detach(device_t);
static int are_ifmedia_upd(struct ifnet *);
static void are_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static int are_ioctl(struct ifnet *, u_long, caddr_t);
static void are_init(void *);
static void are_init_locked(struct are_softc *);
static void are_link_task(void *, int);
static int are_miibus_readreg(device_t, int, int);
static void are_miibus_statchg(device_t);
static int are_miibus_writereg(device_t, int, int, int);
static int are_probe(device_t);
static void are_reset(struct are_softc *);
static int are_resume(device_t);
static int are_rx_ring_init(struct are_softc *);
static int are_tx_ring_init(struct are_softc *);
static int are_shutdown(device_t);
static void are_start(struct ifnet *);
static void are_start_locked(struct ifnet *);
static void are_stop(struct are_softc *);
static int are_suspend(device_t);

static void are_rx(struct are_softc *);
static void are_tx(struct are_softc *);
static void are_intr(void *);
static void are_tick(void *);

static void are_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int are_dma_alloc(struct are_softc *);
static void are_dma_free(struct are_softc *);
static int are_newbuf(struct are_softc *, int);
static __inline void are_fixup_rx(struct mbuf *);

static void are_hinted_child(device_t bus, const char *dname, int dunit);

static device_method_t are_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		are_probe),
	DEVMETHOD(device_attach,	are_attach),
	DEVMETHOD(device_detach,	are_detach),
	DEVMETHOD(device_suspend,	are_suspend),
	DEVMETHOD(device_resume,	are_resume),
	DEVMETHOD(device_shutdown,	are_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	are_miibus_readreg),
	DEVMETHOD(miibus_writereg,	are_miibus_writereg),
	DEVMETHOD(miibus_statchg,	are_miibus_statchg),

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
	DEVMETHOD(bus_hinted_child,	are_hinted_child),

	DEVMETHOD_END
};

static driver_t are_driver = {
	"are",
	are_methods,
	sizeof(struct are_softc)
};

static devclass_t are_devclass;

DRIVER_MODULE(are, nexus, are_driver, are_devclass, 0, 0);
#ifdef ARE_MII
DRIVER_MODULE(miibus, are, miibus_driver, miibus_devclass, 0, 0);
#endif

#ifdef ARE_MDIO
static int aremdio_probe(device_t);
static int aremdio_attach(device_t);
static int aremdio_detach(device_t);

/*
 * Declare an additional, separate driver for accessing the MDIO bus.
 */
static device_method_t aremdio_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aremdio_probe),
	DEVMETHOD(device_attach,	aremdio_attach),
	DEVMETHOD(device_detach,	aremdio_detach),

	/* bus interface */
	DEVMETHOD(bus_add_child,	device_add_child_ordered),
        
	/* MDIO access */
	DEVMETHOD(mdio_readreg,		are_miibus_readreg),
	DEVMETHOD(mdio_writereg,	are_miibus_writereg),
};

DEFINE_CLASS_0(aremdio, aremdio_driver, aremdio_methods,
    sizeof(struct are_softc));
static devclass_t aremdio_devclass;

DRIVER_MODULE(miiproxy, are, miiproxy_driver, miiproxy_devclass, 0, 0);
DRIVER_MODULE(aremdio, nexus, aremdio_driver, aremdio_devclass, 0, 0);
DRIVER_MODULE(mdio, aremdio, mdio_driver, mdio_devclass, 0, 0);
#endif


static int 
are_probe(device_t dev)
{

	device_set_desc(dev, "AR531x Ethernet interface");
	return (0);
}

static int
are_attach(device_t dev)
{
	struct ifnet		*ifp;
	struct are_softc		*sc;
	int			error = 0;
#ifdef INTRNG
	int			enetirq;
#else
	int			rid;
#endif
	int			unit;
	char *			local_macstr;
	int			count;
	int			i;

	sc = device_get_softc(dev);
	unit = device_get_unit(dev);
	sc->are_dev = dev;

	/* hardcode macaddress */
	sc->are_eaddr[0] = 0x00;
	sc->are_eaddr[1] = 0x0C;
	sc->are_eaddr[2] = 0x42;
	sc->are_eaddr[3] = 0x09;
	sc->are_eaddr[4] = 0x5E;
	sc->are_eaddr[5] = 0x6B;

	/* try to get from hints */
	if (!resource_string_value(device_get_name(dev),
		device_get_unit(dev), "macaddr", (const char **)&local_macstr)) {
		uint32_t tmpmac[ETHER_ADDR_LEN];

		/* Have a MAC address; should use it */
		device_printf(dev, "Overriding MAC address from environment: '%s'\n",
		    local_macstr);

		/* Extract out the MAC address */
		/* XXX this should all be a generic method */
		count = sscanf(local_macstr, "%x%*c%x%*c%x%*c%x%*c%x%*c%x",
		    &tmpmac[0], &tmpmac[1],
		    &tmpmac[2], &tmpmac[3],
		    &tmpmac[4], &tmpmac[5]);
		if (count == 6) {
			/* Valid! */
			for (i = 0; i < ETHER_ADDR_LEN; i++)
				sc->are_eaddr[i] = tmpmac[i];
		}
	}

	mtx_init(&sc->are_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->are_stat_callout, &sc->are_mtx, 0);
	TASK_INIT(&sc->are_link_task, 0, are_link_task, sc);

	/* Map control/status registers. */
	sc->are_rid = 0;
	sc->are_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->are_rid, 
	    RF_ACTIVE | RF_SHAREABLE);

	if (sc->are_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->are_btag = rman_get_bustag(sc->are_res);
	sc->are_bhandle = rman_get_bushandle(sc->are_res);

#ifndef INTRNG
	/* Allocate interrupts */
	rid = 0;
	sc->are_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, 
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->are_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}
#endif

	/* Allocate ifnet structure. */
	ifp = sc->are_ifp = if_alloc(IFT_ETHER);

	if (ifp == NULL) {
		device_printf(dev, "couldn't allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = are_ioctl;
	ifp->if_start = are_start;
	ifp->if_init = are_init;
	sc->are_if_flags = ifp->if_flags;

	/* ifqmaxlen is sysctl value in net/if.c */
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;

	ifp->if_capenable = ifp->if_capabilities;

	if (are_dma_alloc(sc) != 0) {
		error = ENXIO;
		goto fail;
	}

	CSR_WRITE_4(sc, CSR_BUSMODE, BUSMODE_SWR);
	DELAY(1000);

#ifdef ARE_MDIO
	sc->are_miiproxy = mii_attach_proxy(sc->are_dev);
#endif

#ifdef ARE_MII
	/* Do MII setup. */
	error = mii_attach(dev, &sc->are_miibus, ifp, are_ifmedia_upd,
	    are_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}
#else
	ifmedia_init(&sc->are_ifmedia, 0, are_ifmedia_upd, are_ifmedia_sts);

	ifmedia_add(&sc->are_ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
	ifmedia_set(&sc->are_ifmedia, IFM_ETHER | IFM_AUTO);
#endif

	/* Call MI attach routine. */
	ether_ifattach(ifp, sc->are_eaddr);

#ifdef INTRNG
	char *name;
	if (ar531x_soc >= AR531X_SOC_AR5315) {
		enetirq = AR5315_CPU_IRQ_ENET;
		name = "enet";
	} else {
		if (device_get_unit(dev) == 0) {
			enetirq = AR5312_IRQ_ENET0;
			name = "enet0";
		} else {
			enetirq = AR5312_IRQ_ENET1;
			name = "enet1";
		}
	}
	cpu_establish_hardintr(name, NULL, are_intr, sc, enetirq,
	    INTR_TYPE_NET, NULL);
#else
	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->are_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, are_intr, sc, &sc->are_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}
#endif

fail:
	if (error) 
		are_detach(dev);

	return (error);
}

static int
are_detach(device_t dev)
{
	struct are_softc		*sc = device_get_softc(dev);
	struct ifnet		*ifp = sc->are_ifp;

	KASSERT(mtx_initialized(&sc->are_mtx), ("vr mutex not initialized"));

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		ARE_LOCK(sc);
		sc->are_detach = 1;
		are_stop(sc);
		ARE_UNLOCK(sc);
		taskqueue_drain(taskqueue_swi, &sc->are_link_task);
		ether_ifdetach(ifp);
	}
#ifdef ARE_MII
	if (sc->are_miibus)
		device_delete_child(dev, sc->are_miibus);
#endif
	bus_generic_detach(dev);

	if (sc->are_intrhand)
		bus_teardown_intr(dev, sc->are_irq, sc->are_intrhand);
	if (sc->are_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->are_irq);

	if (sc->are_res)
		bus_release_resource(dev, SYS_RES_MEMORY, sc->are_rid, 
		    sc->are_res);

	if (ifp)
		if_free(ifp);

	are_dma_free(sc);

	mtx_destroy(&sc->are_mtx);

	return (0);

}

static int
are_suspend(device_t dev)
{

	panic("%s", __func__);
	return 0;
}

static int
are_resume(device_t dev)
{

	panic("%s", __func__);
	return 0;
}

static int
are_shutdown(device_t dev)
{
	struct are_softc	*sc;

	sc = device_get_softc(dev);

	ARE_LOCK(sc);
	are_stop(sc);
	ARE_UNLOCK(sc);

	return (0);
}

static int
are_miibus_readreg(device_t dev, int phy, int reg)
{
	struct are_softc * sc = device_get_softc(dev);
	uint32_t	addr;
	int		i;

	addr = (phy << MIIADDR_PHY_SHIFT) | (reg << MIIADDR_REG_SHIFT);
	CSR_WRITE_4(sc, CSR_MIIADDR, addr);
	for (i = 0; i < 100000000; i++) {
		if ((CSR_READ_4(sc, CSR_MIIADDR) & MIIADDR_BUSY) == 0)
			break;
	}

	return (CSR_READ_4(sc, CSR_MIIDATA) & 0xffff);
}

static int
are_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct are_softc * sc = device_get_softc(dev);
	uint32_t	addr;
	int		i;

	/* write the data register */
	CSR_WRITE_4(sc, CSR_MIIDATA, data);

	/* write the address to latch it in */
	addr = (phy << MIIADDR_PHY_SHIFT) | (reg << MIIADDR_REG_SHIFT) |
	    MIIADDR_WRITE;
	CSR_WRITE_4(sc, CSR_MIIADDR, addr);

	for (i = 0; i < 100000000; i++) {
		if ((CSR_READ_4(sc, CSR_MIIADDR) & MIIADDR_BUSY) == 0)
			break;
	}

	return (0);
}

static void
are_miibus_statchg(device_t dev)
{
	struct are_softc		*sc;

	sc = device_get_softc(dev);
	taskqueue_enqueue(taskqueue_swi, &sc->are_link_task);
}

static void
are_link_task(void *arg, int pending)
{
#ifdef ARE_MII
	struct are_softc		*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	/* int			lfdx, mfdx; */

	sc = (struct are_softc *)arg;

	ARE_LOCK(sc);
	mii = device_get_softc(sc->are_miibus);
	ifp = sc->are_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		ARE_UNLOCK(sc);
		return;
	}

	if (mii->mii_media_status & IFM_ACTIVE) {
		if (IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE)
			sc->are_link_status = 1;
	} else
		sc->are_link_status = 0;

	ARE_UNLOCK(sc);
#endif
}

static void
are_reset(struct are_softc *sc)
{
	int		i;

	CSR_WRITE_4(sc, CSR_BUSMODE, BUSMODE_SWR);

	/*
	 * The chip doesn't take itself out of reset automatically.
	 * We need to do so after 2us.
	 */
	DELAY(10);
	CSR_WRITE_4(sc, CSR_BUSMODE, 0);

	for (i = 0; i < 1000; i++) {
		/*
		 * Wait a bit for the reset to complete before peeking
		 * at the chip again.
		 */
		DELAY(10);
		if ((CSR_READ_4(sc, CSR_BUSMODE) & BUSMODE_SWR) == 0)
			break;
	}

	if (CSR_READ_4(sc, CSR_BUSMODE) & BUSMODE_SWR)
		device_printf(sc->are_dev, "reset time out\n");

	DELAY(1000);
}

static void
are_init(void *xsc)
{
	struct are_softc	 *sc = xsc;

	ARE_LOCK(sc);
	are_init_locked(sc);
	ARE_UNLOCK(sc);
}

static void
are_init_locked(struct are_softc *sc)
{
	struct ifnet		*ifp = sc->are_ifp;
#ifdef ARE_MII
	struct mii_data		*mii;
#endif

	ARE_LOCK_ASSERT(sc);

#ifdef ARE_MII
	mii = device_get_softc(sc->are_miibus);
#endif

	are_stop(sc);
	are_reset(sc);

	/* Init circular RX list. */
	if (are_rx_ring_init(sc) != 0) {
		device_printf(sc->are_dev,
		    "initialization failed: no memory for rx buffers\n");
		are_stop(sc);
		return;
	}

	/* Init tx descriptors. */
	are_tx_ring_init(sc);

	/*
	 * Initialize the BUSMODE register.
	 */
	CSR_WRITE_4(sc, CSR_BUSMODE,
	    /* XXX: not sure if this is a good thing or not... */
	    BUSMODE_BAR | BUSMODE_BLE | BUSMODE_PBL_4LW);

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
	CSR_WRITE_4(sc, CSR_TXLIST, ARE_TX_RING_ADDR(sc, 0));
	CSR_WRITE_4(sc, CSR_RXLIST, ARE_RX_RING_ADDR(sc, 0));

	/*
	 * Set the station address.
	 */
	CSR_WRITE_4(sc, CSR_MACHI, sc->are_eaddr[5] << 16 | sc->are_eaddr[4]);
	CSR_WRITE_4(sc, CSR_MACLO, sc->are_eaddr[3] << 24 |
	    sc->are_eaddr[2] << 16 | sc->are_eaddr[1] << 8 | sc->are_eaddr[0]);

	/*
	 * Start the mac.
	 */
	CSR_WRITE_4(sc, CSR_FLOWC, FLOWC_FCE);
	CSR_WRITE_4(sc, CSR_MACCTL, MACCTL_RE | MACCTL_TE |
	    MACCTL_PM | MACCTL_FDX | MACCTL_HBD | MACCTL_RA);

	/*
	 * Write out the opmode.
	 */
	CSR_WRITE_4(sc, CSR_OPMODE, OPMODE_SR | OPMODE_ST | OPMODE_SF |
	    OPMODE_TR_64);

	/*
	 * Start the receive process.
	 */
	CSR_WRITE_4(sc, CSR_RXPOLL, RXPOLL_RPD);

	sc->are_link_status = 1;
#ifdef ARE_MII
	mii_mediachg(mii);
#endif

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->are_stat_callout, hz, are_tick, sc);
}

static void
are_start(struct ifnet *ifp)
{
	struct are_softc	 *sc;

	sc = ifp->if_softc;

	ARE_LOCK(sc);
	are_start_locked(ifp);
	ARE_UNLOCK(sc);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
are_encap(struct are_softc *sc, struct mbuf **m_head)
{
	struct are_txdesc	*txd;
	struct are_desc		*desc, *prev_desc;
	struct mbuf		*m;
	bus_dma_segment_t	txsegs[ARE_MAXFRAGS];
	uint32_t		link_addr;
	int			error, i, nsegs, prod, si, prev_prod;
	int			txstat;
	int			startcount;
	int			padlen;

	startcount = sc->are_cdata.are_tx_cnt;

	ARE_LOCK_ASSERT(sc);

	/*
	 * Some VIA Rhine wants packet buffers to be longword
	 * aligned, but very often our mbufs aren't. Rather than
	 * waste time trying to decide when to copy and when not
	 * to copy, just do it all the time.
	 */
	m = m_defrag(*m_head, M_NOWAIT);
	if (m == NULL) {
		device_printf(sc->are_dev, "are_encap m_defrag error\n");
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
	if ((*m_head)->m_pkthdr.len < ARE_MIN_FRAMELEN) {
		m = *m_head;
		padlen = ARE_MIN_FRAMELEN - m->m_pkthdr.len;
		if (M_WRITABLE(m) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			if (m == NULL) {
				device_printf(sc->are_dev, "are_encap m_dup error\n");
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		if (m->m_next != NULL || M_TRAILINGSPACE(m) < padlen) {
			m = m_defrag(m, M_NOWAIT);
			if (m == NULL) {
				device_printf(sc->are_dev, "are_encap m_defrag error\n");
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

	prod = sc->are_cdata.are_tx_prod;
	txd = &sc->are_cdata.are_txdesc[prod];
	error = bus_dmamap_load_mbuf_sg(sc->are_cdata.are_tx_tag,
	    txd->tx_dmamap, *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		device_printf(sc->are_dev, "are_encap EFBIG error\n");
		m = m_defrag(*m_head, M_NOWAIT);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->are_cdata.are_tx_tag,
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
	if (sc->are_cdata.are_tx_cnt + nsegs >= (ARE_TX_RING_CNT - 1)) {
		bus_dmamap_unload(sc->are_cdata.are_tx_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}

	txd->tx_m = *m_head;
	bus_dmamap_sync(sc->are_cdata.are_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	si = prod;

	/* 
	 * Make a list of descriptors for this packet. DMA controller will
	 * walk through it while are_link is not zero. The last one should
	 * have COF flag set, to pickup next chain from NDPTR
	 */
	prev_prod = prod;
	desc = prev_desc = NULL;
	for (i = 0; i < nsegs; i++) {
		desc = &sc->are_rdata.are_tx_ring[prod];
		desc->are_stat = ADSTAT_OWN;
		desc->are_devcs = ARE_DMASIZE(txsegs[i].ds_len);
		desc->are_addr = txsegs[i].ds_addr;
		/* link with previous descriptor */
		/* end of descriptor */
		if (prod == ARE_TX_RING_CNT - 1)
			desc->are_devcs |= ADCTL_ER;

		sc->are_cdata.are_tx_cnt++;
		prev_desc = desc;
		ARE_INC(prod, ARE_TX_RING_CNT);
	}

	/* 
	 * Set mark last fragment with LD flag
	 */
	if (desc) {
		desc->are_devcs |= ADCTL_Tx_IC;
		desc->are_devcs |= ADCTL_Tx_LS;
	}

	/* Update producer index. */
	sc->are_cdata.are_tx_prod = prod;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->are_cdata.are_tx_ring_tag,
	    sc->are_cdata.are_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Start transmitting */
	/* Check if new list is queued in NDPTR */
	txstat = (CSR_READ_4(sc, CSR_STATUS) >> 20) & 7;
	if (startcount == 0 && (txstat == 0 || txstat == 6)) {
		desc = &sc->are_rdata.are_tx_ring[si];
		desc->are_devcs |= ADCTL_Tx_FS;
	}
	else {
		link_addr = ARE_TX_RING_ADDR(sc, si);
		/* Get previous descriptor */
		si = (si + ARE_TX_RING_CNT - 1) % ARE_TX_RING_CNT;
		desc = &sc->are_rdata.are_tx_ring[si];
		desc->are_devcs &= ~(ADCTL_Tx_IC | ADCTL_Tx_LS);
	}

	return (0);
}

static void
are_start_locked(struct ifnet *ifp)
{
	struct are_softc		*sc;
	struct mbuf		*m_head;
	int			enq;
	int			txstat;

	sc = ifp->if_softc;

	ARE_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || sc->are_link_status == 0 )
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->are_cdata.are_tx_cnt < ARE_TX_RING_CNT - 2; ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (are_encap(sc, &m_head)) {
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
		if (txstat == 0 || txstat == 6) {
			/* Transmit Process Stat is stop or suspended */
			CSR_WRITE_4(sc, CSR_TXPOLL, TXPOLL_TPD);
		}
	}
}

static void
are_stop(struct are_softc *sc)
{
	struct ifnet	    *ifp;

	ARE_LOCK_ASSERT(sc);

	ifp = sc->are_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	callout_stop(&sc->are_stat_callout);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, CSR_INTEN, 0);

	/* Stop the transmit and receive processes. */
	CSR_WRITE_4(sc, CSR_OPMODE, 0);
	CSR_WRITE_4(sc, CSR_RXLIST, 0);
	CSR_WRITE_4(sc, CSR_TXLIST, 0);
	CSR_WRITE_4(sc, CSR_MACCTL, 
	    CSR_READ_4(sc, CSR_MACCTL) & ~(MACCTL_TE | MACCTL_RE));

}

static int
are_set_filter(struct are_softc *sc)
{
	struct ifnet	    *ifp;
	int mchash[2];
	int macctl;

	ifp = sc->are_ifp;

	macctl = CSR_READ_4(sc, CSR_MACCTL);
	macctl &= ~(MACCTL_PR | MACCTL_PM);
	macctl |= MACCTL_HBD;

	if (ifp->if_flags & IFF_PROMISC)
		macctl |= MACCTL_PR;

	/* Todo: hash table set. 
	 * But I don't know how to use multicast hash table at this soc.
	 */

	/* this is allmulti */
	mchash[0] = mchash[1] = 0xffffffff;
	macctl |= MACCTL_PM;

	CSR_WRITE_4(sc, CSR_HTLO, mchash[0]);
	CSR_WRITE_4(sc, CSR_HTHI, mchash[1]);
	CSR_WRITE_4(sc, CSR_MACCTL, macctl);

	return 0;
}

static int
are_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct are_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
#ifdef ARE_MII
	struct mii_data		*mii;
#endif
	int			error;

	switch (command) {
	case SIOCSIFFLAGS:
		ARE_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ sc->are_if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					are_set_filter(sc);
			} else {
				if (sc->are_detach == 0)
					are_init_locked(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				are_stop(sc);
		}
		sc->are_if_flags = ifp->if_flags;
		ARE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		ARE_LOCK(sc);
		are_set_filter(sc);
		ARE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
#ifdef ARE_MII
		mii = device_get_softc(sc->are_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
#else
		error = ifmedia_ioctl(ifp, ifr, &sc->are_ifmedia, command);
#endif
		break;
	case SIOCSIFCAP:
		error = 0;
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
are_ifmedia_upd(struct ifnet *ifp)
{
#ifdef ARE_MII
	struct are_softc		*sc;
	struct mii_data		*mii;
	struct mii_softc	*miisc;
	int			error;

	sc = ifp->if_softc;
	ARE_LOCK(sc);
	mii = device_get_softc(sc->are_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	ARE_UNLOCK(sc);

	return (error);
#else
	return (0);
#endif
}

/*
 * Report current media status.
 */
static void
are_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
#ifdef ARE_MII
	struct are_softc		*sc = ifp->if_softc;
	struct mii_data		*mii;

	mii = device_get_softc(sc->are_miibus);
	ARE_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	ARE_UNLOCK(sc);
#else
	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
#endif
}

struct are_dmamap_arg {
	bus_addr_t	are_busaddr;
};

static void
are_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct are_dmamap_arg	*ctx;

	if (error != 0)
		return;
	ctx = arg;
	ctx->are_busaddr = segs[0].ds_addr;
}

static int
are_dma_alloc(struct are_softc *sc)
{
	struct are_dmamap_arg	ctx;
	struct are_txdesc	*txd;
	struct are_rxdesc	*rxd;
	int			error, i;

	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->are_dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->are_cdata.are_parent_tag);
	if (error != 0) {
		device_printf(sc->are_dev, "failed to create parent DMA tag\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(
	    sc->are_cdata.are_parent_tag,	/* parent */
	    ARE_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ARE_TX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    ARE_TX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->are_cdata.are_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->are_dev, "failed to create Tx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(
	    sc->are_cdata.are_parent_tag,	/* parent */
	    ARE_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ARE_RX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    ARE_RX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->are_cdata.are_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->are_dev, "failed to create Rx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(
	    sc->are_cdata.are_parent_tag,	/* parent */
	    sizeof(uint32_t), 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * ARE_MAXFRAGS,	/* maxsize */
	    ARE_MAXFRAGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->are_cdata.are_tx_tag);
	if (error != 0) {
		device_printf(sc->are_dev, "failed to create Tx DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(
	    sc->are_cdata.are_parent_tag,	/* parent */
	    ARE_RX_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->are_cdata.are_rx_tag);
	if (error != 0) {
		device_printf(sc->are_dev, "failed to create Rx DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->are_cdata.are_tx_ring_tag,
	    (void **)&sc->are_rdata.are_tx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->are_cdata.are_tx_ring_map);
	if (error != 0) {
		device_printf(sc->are_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.are_busaddr = 0;
	error = bus_dmamap_load(sc->are_cdata.are_tx_ring_tag,
	    sc->are_cdata.are_tx_ring_map, sc->are_rdata.are_tx_ring,
	    ARE_TX_RING_SIZE, are_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.are_busaddr == 0) {
		device_printf(sc->are_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc->are_rdata.are_tx_ring_paddr = ctx.are_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->are_cdata.are_rx_ring_tag,
	    (void **)&sc->are_rdata.are_rx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->are_cdata.are_rx_ring_map);
	if (error != 0) {
		device_printf(sc->are_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.are_busaddr = 0;
	error = bus_dmamap_load(sc->are_cdata.are_rx_ring_tag,
	    sc->are_cdata.are_rx_ring_map, sc->are_rdata.are_rx_ring,
	    ARE_RX_RING_SIZE, are_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.are_busaddr == 0) {
		device_printf(sc->are_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc->are_rdata.are_rx_ring_paddr = ctx.are_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < ARE_TX_RING_CNT; i++) {
		txd = &sc->are_cdata.are_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->are_cdata.are_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->are_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->are_cdata.are_rx_tag, 0,
	    &sc->are_cdata.are_rx_sparemap)) != 0) {
		device_printf(sc->are_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < ARE_RX_RING_CNT; i++) {
		rxd = &sc->are_cdata.are_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->are_cdata.are_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->are_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
are_dma_free(struct are_softc *sc)
{
	struct are_txdesc	*txd;
	struct are_rxdesc	*rxd;
	int			i;

	/* Tx ring. */
	if (sc->are_cdata.are_tx_ring_tag) {
		if (sc->are_rdata.are_tx_ring_paddr)
			bus_dmamap_unload(sc->are_cdata.are_tx_ring_tag,
			    sc->are_cdata.are_tx_ring_map);
		if (sc->are_rdata.are_tx_ring)
			bus_dmamem_free(sc->are_cdata.are_tx_ring_tag,
			    sc->are_rdata.are_tx_ring,
			    sc->are_cdata.are_tx_ring_map);
		sc->are_rdata.are_tx_ring = NULL;
		sc->are_rdata.are_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->are_cdata.are_tx_ring_tag);
		sc->are_cdata.are_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->are_cdata.are_rx_ring_tag) {
		if (sc->are_rdata.are_rx_ring_paddr)
			bus_dmamap_unload(sc->are_cdata.are_rx_ring_tag,
			    sc->are_cdata.are_rx_ring_map);
		if (sc->are_rdata.are_rx_ring)
			bus_dmamem_free(sc->are_cdata.are_rx_ring_tag,
			    sc->are_rdata.are_rx_ring,
			    sc->are_cdata.are_rx_ring_map);
		sc->are_rdata.are_rx_ring = NULL;
		sc->are_rdata.are_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->are_cdata.are_rx_ring_tag);
		sc->are_cdata.are_rx_ring_tag = NULL;
	}
	/* Tx buffers. */
	if (sc->are_cdata.are_tx_tag) {
		for (i = 0; i < ARE_TX_RING_CNT; i++) {
			txd = &sc->are_cdata.are_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc->are_cdata.are_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->are_cdata.are_tx_tag);
		sc->are_cdata.are_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc->are_cdata.are_rx_tag) {
		for (i = 0; i < ARE_RX_RING_CNT; i++) {
			rxd = &sc->are_cdata.are_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc->are_cdata.are_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->are_cdata.are_rx_sparemap) {
			bus_dmamap_destroy(sc->are_cdata.are_rx_tag,
			    sc->are_cdata.are_rx_sparemap);
			sc->are_cdata.are_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc->are_cdata.are_rx_tag);
		sc->are_cdata.are_rx_tag = NULL;
	}

	if (sc->are_cdata.are_parent_tag) {
		bus_dma_tag_destroy(sc->are_cdata.are_parent_tag);
		sc->are_cdata.are_parent_tag = NULL;
	}
}

/*
 * Initialize the transmit descriptors.
 */
static int
are_tx_ring_init(struct are_softc *sc)
{
	struct are_ring_data	*rd;
	struct are_txdesc	*txd;
	bus_addr_t		addr;
	int			i;

	sc->are_cdata.are_tx_prod = 0;
	sc->are_cdata.are_tx_cons = 0;
	sc->are_cdata.are_tx_cnt = 0;
	sc->are_cdata.are_tx_pkts = 0;

	rd = &sc->are_rdata;
	bzero(rd->are_tx_ring, ARE_TX_RING_SIZE);
	for (i = 0; i < ARE_TX_RING_CNT; i++) {
		if (i == ARE_TX_RING_CNT - 1)
			addr = ARE_TX_RING_ADDR(sc, 0);
		else
			addr = ARE_TX_RING_ADDR(sc, i + 1);
		rd->are_tx_ring[i].are_stat = 0;
		rd->are_tx_ring[i].are_devcs = 0;
		rd->are_tx_ring[i].are_addr = 0;
		rd->are_tx_ring[i].are_link = addr;
		txd = &sc->are_cdata.are_txdesc[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->are_cdata.are_tx_ring_tag,
	    sc->are_cdata.are_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
are_rx_ring_init(struct are_softc *sc)
{
	struct are_ring_data	*rd;
	struct are_rxdesc	*rxd;
	bus_addr_t		addr;
	int			i;

	sc->are_cdata.are_rx_cons = 0;

	rd = &sc->are_rdata;
	bzero(rd->are_rx_ring, ARE_RX_RING_SIZE);
	for (i = 0; i < ARE_RX_RING_CNT; i++) {
		rxd = &sc->are_cdata.are_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->desc = &rd->are_rx_ring[i];
		if (i == ARE_RX_RING_CNT - 1)
			addr = ARE_RX_RING_ADDR(sc, 0);
		else
			addr = ARE_RX_RING_ADDR(sc, i + 1);
		rd->are_rx_ring[i].are_stat = ADSTAT_OWN;
		rd->are_rx_ring[i].are_devcs = ADCTL_CH;
		if (i == ARE_RX_RING_CNT - 1)
			rd->are_rx_ring[i].are_devcs |= ADCTL_ER;
		rd->are_rx_ring[i].are_addr = 0;
		rd->are_rx_ring[i].are_link = addr;
		if (are_newbuf(sc, i) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->are_cdata.are_rx_ring_tag,
	    sc->are_cdata.are_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
are_newbuf(struct are_softc *sc, int idx)
{
	struct are_desc		*desc;
	struct are_rxdesc	*rxd;
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int			nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;

	/* tcp header boundary margin */
	m_adj(m, 4);

	if (bus_dmamap_load_mbuf_sg(sc->are_cdata.are_rx_tag,
	    sc->are_cdata.are_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc->are_cdata.are_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		/*
		 * THis is if_kr.c original code but make bug. Make scranble on buffer data.
		 * bus_dmamap_sync(sc->are_cdata.are_rx_tag, rxd->rx_dmamap,
		 *    BUS_DMASYNC_POSTREAD);
		 */
		bus_dmamap_unload(sc->are_cdata.are_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->are_cdata.are_rx_sparemap;
	sc->are_cdata.are_rx_sparemap = map;
	bus_dmamap_sync(sc->are_cdata.are_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	desc = rxd->desc;
	desc->are_addr = segs[0].ds_addr;
	desc->are_devcs |= ARE_DMASIZE(segs[0].ds_len);
	rxd->saved_ca = desc->are_addr ;
	rxd->saved_ctl = desc->are_stat ;

	return (0);
}

static __inline void
are_fixup_rx(struct mbuf *m)
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
are_tx(struct are_softc *sc)
{
	struct are_txdesc	*txd;
	struct are_desc		*cur_tx;
	struct ifnet		*ifp;
	uint32_t		ctl, devcs;
	int			cons, prod;

	ARE_LOCK_ASSERT(sc);

	cons = sc->are_cdata.are_tx_cons;
	prod = sc->are_cdata.are_tx_prod;
	if (cons == prod)
		return;

	bus_dmamap_sync(sc->are_cdata.are_tx_ring_tag,
	    sc->are_cdata.are_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	ifp = sc->are_ifp;
	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	for (; cons != prod; ARE_INC(cons, ARE_TX_RING_CNT)) {
		cur_tx = &sc->are_rdata.are_tx_ring[cons];
		ctl = cur_tx->are_stat;
		devcs = cur_tx->are_devcs;
		/* Check if descriptor has "finished" flag */
		if (ARE_DMASIZE(devcs) == 0)
			break;

		sc->are_cdata.are_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		txd = &sc->are_cdata.are_txdesc[cons];

		if ((ctl & ADSTAT_Tx_ES) == 0)
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		else {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		}

		bus_dmamap_sync(sc->are_cdata.are_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->are_cdata.are_tx_tag, txd->tx_dmamap);

		/* Free only if it's first descriptor in list */
		if (txd->tx_m)
			m_freem(txd->tx_m);
		txd->tx_m = NULL;

		/* reset descriptor */
		cur_tx->are_stat = 0;
		cur_tx->are_devcs = 0;
		cur_tx->are_addr = 0;
	}

	sc->are_cdata.are_tx_cons = cons;

	bus_dmamap_sync(sc->are_cdata.are_tx_ring_tag,
	    sc->are_cdata.are_tx_ring_map, BUS_DMASYNC_PREWRITE);
}


static void
are_rx(struct are_softc *sc)
{
	struct are_rxdesc	*rxd;
	struct ifnet		*ifp = sc->are_ifp;
	int			cons, prog, packet_len, error;
	struct are_desc		*cur_rx;
	struct mbuf		*m;

	ARE_LOCK_ASSERT(sc);

	cons = sc->are_cdata.are_rx_cons;

	bus_dmamap_sync(sc->are_cdata.are_rx_ring_tag,
	    sc->are_cdata.are_rx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; prog < ARE_RX_RING_CNT; ARE_INC(cons, ARE_RX_RING_CNT)) {
		cur_rx = &sc->are_rdata.are_rx_ring[cons];
		rxd = &sc->are_cdata.are_rxdesc[cons];
		m = rxd->rx_m;

		if ((cur_rx->are_stat & ADSTAT_OWN) == ADSTAT_OWN)
		       break;	

		prog++;

		packet_len = ADSTAT_Rx_LENGTH(cur_rx->are_stat);
		/* Assume it's error */
		error = 1;

		if (packet_len < 64)
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		else if ((cur_rx->are_stat & ADSTAT_Rx_DE) == 0) {
			error = 0;
			bus_dmamap_sync(sc->are_cdata.are_rx_tag, rxd->rx_dmamap,
			    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
			m = rxd->rx_m;
			/* Skip 4 bytes of CRC */
			m->m_pkthdr.len = m->m_len = packet_len - ETHER_CRC_LEN;
			are_fixup_rx(m);
			m->m_pkthdr.rcvif = ifp;
			if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

			ARE_UNLOCK(sc);
			(*ifp->if_input)(ifp, m);
			ARE_LOCK(sc);
		}

		if (error) {
			/* Restore CONTROL and CA values, reset DEVCS */
			cur_rx->are_stat = rxd->saved_ctl;
			cur_rx->are_addr = rxd->saved_ca;
			cur_rx->are_devcs = 0;
		}
		else {
			/* Reinit descriptor */
			cur_rx->are_stat = ADSTAT_OWN;
			cur_rx->are_devcs = 0;
			if (cons == ARE_RX_RING_CNT - 1)
				cur_rx->are_devcs |= ADCTL_ER;
			cur_rx->are_addr = 0;
			if (are_newbuf(sc, cons) != 0) {
				device_printf(sc->are_dev, 
				    "Failed to allocate buffer\n");
				break;
			}
		}

		bus_dmamap_sync(sc->are_cdata.are_rx_ring_tag,
		    sc->are_cdata.are_rx_ring_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	}

	if (prog > 0) {
		sc->are_cdata.are_rx_cons = cons;

		bus_dmamap_sync(sc->are_cdata.are_rx_ring_tag,
		    sc->are_cdata.are_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

static void
are_intr(void *arg)
{
	struct are_softc		*sc = arg;
	uint32_t		status;
	struct ifnet		*ifp = sc->are_ifp;

	ARE_LOCK(sc);

	/* mask out interrupts */

	status = CSR_READ_4(sc, CSR_STATUS);
	if (status) {
		CSR_WRITE_4(sc, CSR_STATUS, status);
	}
	if (status & sc->sc_rxint_mask) {
		are_rx(sc);
	}
	if (status & sc->sc_txint_mask) {
		are_tx(sc);
	}

	/* Try to get more packets going. */
	are_start(ifp);

	ARE_UNLOCK(sc);
}

static void
are_tick(void *xsc)
{
#ifdef ARE_MII
	struct are_softc		*sc = xsc;
	struct mii_data		*mii;

	ARE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->are_miibus);
	mii_tick(mii);
	callout_reset(&sc->are_stat_callout, hz, are_tick, sc);
#endif
}

static void
are_hinted_child(device_t bus, const char *dname, int dunit)
{
	BUS_ADD_CHILD(bus, 0, dname, dunit);
	device_printf(bus, "hinted child %s%d\n", dname, dunit);
}

#ifdef ARE_MDIO
static int
aremdio_probe(device_t dev)
{
	device_set_desc(dev, "Atheros AR531x built-in ethernet interface, MDIO controller");
	return(0);
}

static int
aremdio_attach(device_t dev)
{
	struct are_softc	*sc;
	int			error = 0;

	sc = device_get_softc(dev);
	sc->are_dev = dev;
	sc->are_rid = 0;
	sc->are_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, 
	    &sc->are_rid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->are_res == NULL) {
		device_printf(dev, "couldn't map memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->are_btag = rman_get_bustag(sc->are_res);
	sc->are_bhandle = rman_get_bushandle(sc->are_res);

	bus_generic_probe(dev);
	bus_enumerate_hinted_children(dev);
	error = bus_generic_attach(dev);
fail:
	return (error);
}

static int
aremdio_detach(device_t dev)
{
	return(0);
}
#endif

#ifdef ARE_DEBUG
void
dump_txdesc(struct are_softc *sc, int pos)
{
	struct are_desc		*desc;

	desc = &sc->are_rdata.are_tx_ring[pos];
	device_printf(sc->are_dev, "CSR_TXLIST %08x\n", CSR_READ_4(sc, CSR_TXLIST));
	device_printf(sc->are_dev, "CSR_HTBA %08x\n", CSR_READ_4(sc, CSR_HTBA));
	device_printf(sc->are_dev, "%d TDES0:%08x TDES1:%08x TDES2:%08x TDES3:%08x\n",
	    pos, desc->are_stat, desc->are_devcs, desc->are_addr, desc->are_link);
}

void 
dump_status_reg(struct are_softc *sc)
{
	uint32_t		status;

	/* mask out interrupts */

	device_printf(sc->are_dev, "CSR_HTBA %08x\n", CSR_READ_4(sc, CSR_HTBA));
	status = CSR_READ_4(sc, CSR_STATUS);
	device_printf(sc->are_dev, "CSR5 Status Register EB:%d TS:%d RS:%d NIS:%d AIS:%d ER:%d SE:%d LNF:%d TM:%d RWT:%d RPS:%d RU:%d RI:%d UNF:%d LNP/ANC:%d TJT:%d TU:%d TPS:%d TI:%d\n", 
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
