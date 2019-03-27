/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2001 Wind River Systems
 * Copyright (c) 1997, 1998, 1999, 2000, 2001
 *	Bill Paul <william.paul@windriver.com>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Level 1 LXT1001 gigabit ethernet driver for FreeBSD. Public
 * documentation not available, but ask me nicely.
 *
 * The Level 1 chip is used on some D-Link, SMC and Addtron NICs.
 * It's a 64-bit PCI part that supports TCP/IP checksum offload,
 * VLAN tagging/insertion, GMII and TBI (1000baseX) ports. There
 * are three supported methods for data transfer between host and
 * NIC: programmed I/O, traditional scatter/gather DMA and Packet
 * Propulsion Technology (tm) DMA. The latter mechanism is a form
 * of double buffer DMA where the packet data is copied to a
 * pre-allocated DMA buffer who's physical address has been loaded
 * into a table at device initialization time. The rationale is that
 * the virtual to physical address translation needed for normal
 * scatter/gather DMA is more expensive than the data copy needed
 * for double buffering. This may be true in Windows NT and the like,
 * but it isn't true for us, at least on the x86 arch. This driver
 * uses the scatter/gather I/O method for both TX and RX.
 *
 * The LXT1001 only supports TCP/IP checksum offload on receive.
 * Also, the VLAN tagging is done using a 16-entry table which allows
 * the chip to perform hardware filtering based on VLAN tags. Sadly,
 * our vlan support doesn't currently play well with this kind of
 * hardware support.
 *
 * Special thanks to:
 * - Jeff James at Intel, for arranging to have the LXT1001 manual
 *   released (at long last)
 * - Beny Chen at D-Link, for actually sending it to me
 * - Brad Short and Keith Alexis at SMC, for sending me sample
 *   SMC9462SX and SMC9462TX adapters for testing
 * - Paul Saab at Y!, for not killing me (though it remains to be seen
 *   if in fact he did me much of a favor)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net/bpf.h>

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#define LGE_USEIOSPACE

#include <dev/lge/if_lgereg.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Various supported device vendors/types and their names.
 */
static const struct lge_type lge_devs[] = {
	{ LGE_VENDORID, LGE_DEVICEID, "Level 1 Gigabit Ethernet" },
	{ 0, 0, NULL }
};

static int lge_probe(device_t);
static int lge_attach(device_t);
static int lge_detach(device_t);

static int lge_alloc_jumbo_mem(struct lge_softc *);
static void lge_free_jumbo_mem(struct lge_softc *);
static void *lge_jalloc(struct lge_softc *);
static void lge_jfree(struct mbuf *);

static int lge_newbuf(struct lge_softc *, struct lge_rx_desc *, struct mbuf *);
static int lge_encap(struct lge_softc *, struct mbuf *, u_int32_t *);
static void lge_rxeof(struct lge_softc *, int);
static void lge_rxeoc(struct lge_softc *);
static void lge_txeof(struct lge_softc *);
static void lge_intr(void *);
static void lge_tick(void *);
static void lge_start(struct ifnet *);
static void lge_start_locked(struct ifnet *);
static int lge_ioctl(struct ifnet *, u_long, caddr_t);
static void lge_init(void *);
static void lge_init_locked(struct lge_softc *);
static void lge_stop(struct lge_softc *);
static void lge_watchdog(struct lge_softc *);
static int lge_shutdown(device_t);
static int lge_ifmedia_upd(struct ifnet *);
static void lge_ifmedia_upd_locked(struct ifnet *);
static void lge_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void lge_eeprom_getword(struct lge_softc *, int, u_int16_t *);
static void lge_read_eeprom(struct lge_softc *, caddr_t, int, int, int);

static int lge_miibus_readreg(device_t, int, int);
static int lge_miibus_writereg(device_t, int, int, int);
static void lge_miibus_statchg(device_t);

static void lge_setmulti(struct lge_softc *);
static void lge_reset(struct lge_softc *);
static int lge_list_rx_init(struct lge_softc *);
static int lge_list_tx_init(struct lge_softc *);

#ifdef LGE_USEIOSPACE
#define LGE_RES			SYS_RES_IOPORT
#define LGE_RID			LGE_PCI_LOIO
#else
#define LGE_RES			SYS_RES_MEMORY
#define LGE_RID			LGE_PCI_LOMEM
#endif

static device_method_t lge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		lge_probe),
	DEVMETHOD(device_attach,	lge_attach),
	DEVMETHOD(device_detach,	lge_detach),
	DEVMETHOD(device_shutdown,	lge_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	lge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	lge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	lge_miibus_statchg),

	DEVMETHOD_END
};

static driver_t lge_driver = {
	"lge",
	lge_methods,
	sizeof(struct lge_softc)
};

static devclass_t lge_devclass;

DRIVER_MODULE(lge, pci, lge_driver, lge_devclass, 0, 0);
DRIVER_MODULE(miibus, lge, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(lge, pci, 1, 1, 1);
MODULE_DEPEND(lge, ether, 1, 1, 1);
MODULE_DEPEND(lge, miibus, 1, 1, 1);

#define LGE_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | (x))

#define LGE_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, LGE_MEAR, CSR_READ_4(sc, LGE_MEAR) | x)

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, LGE_MEAR, CSR_READ_4(sc, LGE_MEAR) & ~x)

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
static void
lge_eeprom_getword(sc, addr, dest)
	struct lge_softc	*sc;
	int			addr;
	u_int16_t		*dest;
{
	int			i;
	u_int32_t		val;

	CSR_WRITE_4(sc, LGE_EECTL, LGE_EECTL_CMD_READ|
	    LGE_EECTL_SINGLEACCESS|((addr >> 1) << 8));

	for (i = 0; i < LGE_TIMEOUT; i++)
		if (!(CSR_READ_4(sc, LGE_EECTL) & LGE_EECTL_CMD_READ))
			break;

	if (i == LGE_TIMEOUT) {
		device_printf(sc->lge_dev, "EEPROM read timed out\n");
		return;
	}

	val = CSR_READ_4(sc, LGE_EEDATA);

	if (addr & 1)
		*dest = (val >> 16) & 0xFFFF;
	else
		*dest = val & 0xFFFF;

	return;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static void
lge_read_eeprom(sc, dest, off, cnt, swap)
	struct lge_softc	*sc;
	caddr_t			dest;
	int			off;
	int			cnt;
	int			swap;
{
	int			i;
	u_int16_t		word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		lge_eeprom_getword(sc, off + i, &word);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}

	return;
}

static int
lge_miibus_readreg(dev, phy, reg)
	device_t		dev;
	int			phy, reg;
{
	struct lge_softc	*sc;
	int			i;

	sc = device_get_softc(dev);

	/*
	 * If we have a non-PCS PHY, pretend that the internal
	 * autoneg stuff at PHY address 0 isn't there so that
	 * the miibus code will find only the GMII PHY.
	 */
	if (sc->lge_pcs == 0 && phy == 0)
		return(0);

	CSR_WRITE_4(sc, LGE_GMIICTL, (phy << 8) | reg | LGE_GMIICMD_READ);

	for (i = 0; i < LGE_TIMEOUT; i++)
		if (!(CSR_READ_4(sc, LGE_GMIICTL) & LGE_GMIICTL_CMDBUSY))
			break;

	if (i == LGE_TIMEOUT) {
		device_printf(sc->lge_dev, "PHY read timed out\n");
		return(0);
	}

	return(CSR_READ_4(sc, LGE_GMIICTL) >> 16);
}

static int
lge_miibus_writereg(dev, phy, reg, data)
	device_t		dev;
	int			phy, reg, data;
{
	struct lge_softc	*sc;
	int			i;

	sc = device_get_softc(dev);

	CSR_WRITE_4(sc, LGE_GMIICTL,
	    (data << 16) | (phy << 8) | reg | LGE_GMIICMD_WRITE);

	for (i = 0; i < LGE_TIMEOUT; i++)
		if (!(CSR_READ_4(sc, LGE_GMIICTL) & LGE_GMIICTL_CMDBUSY))
			break;

	if (i == LGE_TIMEOUT) {
		device_printf(sc->lge_dev, "PHY write timed out\n");
		return(0);
	}

	return(0);
}

static void
lge_miibus_statchg(dev)
	device_t		dev;
{
	struct lge_softc	*sc;
	struct mii_data		*mii;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->lge_miibus);

	LGE_CLRBIT(sc, LGE_GMIIMODE, LGE_GMIIMODE_SPEED);
	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:
	case IFM_1000_SX:
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_1000);
		break;
	case IFM_100_TX:
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_100);
		break;
	case IFM_10_T:
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_10);
		break;
	default:
		/*
		 * Choose something, even if it's wrong. Clearing
		 * all the bits will hose autoneg on the internal
		 * PHY.
		 */
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_SPEED_1000);
		break;
	}

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		LGE_SETBIT(sc, LGE_GMIIMODE, LGE_GMIIMODE_FDX);
	} else {
		LGE_CLRBIT(sc, LGE_GMIIMODE, LGE_GMIIMODE_FDX);
	}

	return;
}

static void
lge_setmulti(sc)
	struct lge_softc	*sc;
{
	struct ifnet		*ifp;
	struct ifmultiaddr	*ifma;
	u_int32_t		h = 0, hashes[2] = { 0, 0 };

	ifp = sc->lge_ifp;
	LGE_LOCK_ASSERT(sc);

	/* Make sure multicast hash table is enabled. */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_MCAST);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		CSR_WRITE_4(sc, LGE_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, LGE_MAR1, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, LGE_MAR0, 0);
	CSR_WRITE_4(sc, LGE_MAR1, 0);

	/* now program new ones */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
	}
	if_maddr_runlock(ifp);

	CSR_WRITE_4(sc, LGE_MAR0, hashes[0]);
	CSR_WRITE_4(sc, LGE_MAR1, hashes[1]);

	return;
}

static void
lge_reset(sc)
	struct lge_softc	*sc;
{
	int			i;

	LGE_SETBIT(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL0|LGE_MODE1_SOFTRST);

	for (i = 0; i < LGE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, LGE_MODE1) & LGE_MODE1_SOFTRST))
			break;
	}

	if (i == LGE_TIMEOUT)
		device_printf(sc->lge_dev, "reset never completed\n");

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

        return;
}

/*
 * Probe for a Level 1 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
lge_probe(dev)
	device_t		dev;
{
	const struct lge_type	*t;

	t = lge_devs;

	while(t->lge_name != NULL) {
		if ((pci_get_vendor(dev) == t->lge_vid) &&
		    (pci_get_device(dev) == t->lge_did)) {
			device_set_desc(dev, t->lge_name);
			return(BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return(ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
lge_attach(dev)
	device_t		dev;
{
	u_char			eaddr[ETHER_ADDR_LEN];
	struct lge_softc	*sc;
	struct ifnet		*ifp = NULL;
	int			error = 0, rid;

	sc = device_get_softc(dev);
	sc->lge_dev = dev;
	
	mtx_init(&sc->lge_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->lge_stat_callout, &sc->lge_mtx, 0);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	rid = LGE_RID;
	sc->lge_res = bus_alloc_resource_any(dev, LGE_RES, &rid, RF_ACTIVE);

	if (sc->lge_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->lge_btag = rman_get_bustag(sc->lge_res);
	sc->lge_bhandle = rman_get_bushandle(sc->lge_res);

	/* Allocate interrupt */
	rid = 0;
	sc->lge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->lge_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Reset the adapter. */
	lge_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	lge_read_eeprom(sc, (caddr_t)&eaddr[0], LGE_EE_NODEADDR_0, 1, 0);
	lge_read_eeprom(sc, (caddr_t)&eaddr[2], LGE_EE_NODEADDR_1, 1, 0);
	lge_read_eeprom(sc, (caddr_t)&eaddr[4], LGE_EE_NODEADDR_2, 1, 0);

	sc->lge_ldata = contigmalloc(sizeof(struct lge_list_data), M_DEVBUF,
	    M_NOWAIT | M_ZERO, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->lge_ldata == NULL) {
		device_printf(dev, "no memory for list buffers!\n");
		error = ENXIO;
		goto fail;
	}

	/* Try to allocate memory for jumbo buffers. */
	if (lge_alloc_jumbo_mem(sc)) {
		device_printf(dev, "jumbo buffer allocation failed\n");
		error = ENXIO;
		goto fail;
	}

	ifp = sc->lge_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = lge_ioctl;
	ifp->if_start = lge_start;
	ifp->if_init = lge_init;
	ifp->if_snd.ifq_maxlen = LGE_TX_LIST_CNT - 1;
	ifp->if_capabilities = IFCAP_RXCSUM;
	ifp->if_capenable = ifp->if_capabilities;

	if (CSR_READ_4(sc, LGE_GMIIMODE) & LGE_GMIIMODE_PCSENH)
		sc->lge_pcs = 1;
	else
		sc->lge_pcs = 0;

	/*
	 * Do MII setup.
	 */
	error = mii_attach(dev, &sc->lge_miibus, ifp, lge_ifmedia_upd,
	    lge_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	error = bus_setup_intr(dev, sc->lge_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, lge_intr, sc, &sc->lge_intrhand);

	if (error) {
		ether_ifdetach(ifp);
		device_printf(dev, "couldn't set up irq\n");
		goto fail;
	}
	return (0);

fail:
	lge_free_jumbo_mem(sc);
	if (sc->lge_ldata)
		contigfree(sc->lge_ldata,
		    sizeof(struct lge_list_data), M_DEVBUF);
	if (ifp)
		if_free(ifp);
	if (sc->lge_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->lge_irq);
	if (sc->lge_res)
		bus_release_resource(dev, LGE_RES, LGE_RID, sc->lge_res);
	mtx_destroy(&sc->lge_mtx);
	return(error);
}

static int
lge_detach(dev)
	device_t		dev;
{
	struct lge_softc	*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	ifp = sc->lge_ifp;

	LGE_LOCK(sc);
	lge_reset(sc);
	lge_stop(sc);
	LGE_UNLOCK(sc);
	callout_drain(&sc->lge_stat_callout);
	ether_ifdetach(ifp);

	bus_generic_detach(dev);
	device_delete_child(dev, sc->lge_miibus);

	bus_teardown_intr(dev, sc->lge_irq, sc->lge_intrhand);
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->lge_irq);
	bus_release_resource(dev, LGE_RES, LGE_RID, sc->lge_res);

	contigfree(sc->lge_ldata, sizeof(struct lge_list_data), M_DEVBUF);
	if_free(ifp);
	lge_free_jumbo_mem(sc);
	mtx_destroy(&sc->lge_mtx);

	return(0);
}

/*
 * Initialize the transmit descriptors.
 */
static int
lge_list_tx_init(sc)
	struct lge_softc	*sc;
{
	struct lge_list_data	*ld;
	struct lge_ring_data	*cd;
	int			i;

	cd = &sc->lge_cdata;
	ld = sc->lge_ldata;
	for (i = 0; i < LGE_TX_LIST_CNT; i++) {
		ld->lge_tx_list[i].lge_mbuf = NULL;
		ld->lge_tx_list[i].lge_ctl = 0;
	}

	cd->lge_tx_prod = cd->lge_tx_cons = 0;

	return(0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arralge the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
lge_list_rx_init(sc)
	struct lge_softc	*sc;
{
	struct lge_list_data	*ld;
	struct lge_ring_data	*cd;
	int			i;

	ld = sc->lge_ldata;
	cd = &sc->lge_cdata;

	cd->lge_rx_prod = cd->lge_rx_cons = 0;

	CSR_WRITE_4(sc, LGE_RXDESC_ADDR_HI, 0);

	for (i = 0; i < LGE_RX_LIST_CNT; i++) {
		if (CSR_READ_1(sc, LGE_RXCMDFREE_8BIT) == 0)
			break;
		if (lge_newbuf(sc, &ld->lge_rx_list[i], NULL) == ENOBUFS)
			return(ENOBUFS);
	}

	/* Clear possible 'rx command queue empty' interrupt. */
	CSR_READ_4(sc, LGE_ISR);

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
lge_newbuf(sc, c, m)
	struct lge_softc	*sc;
	struct lge_rx_desc	*c;
	struct mbuf		*m;
{
	struct mbuf		*m_new = NULL;
	char			*buf = NULL;

	if (m == NULL) {
		MGETHDR(m_new, M_NOWAIT, MT_DATA);
		if (m_new == NULL) {
			device_printf(sc->lge_dev, "no memory for rx list "
			    "-- packet dropped!\n");
			return(ENOBUFS);
		}

		/* Allocate the jumbo buffer */
		buf = lge_jalloc(sc);
		if (buf == NULL) {
#ifdef LGE_VERBOSE
			device_printf(sc->lge_dev, "jumbo allocation failed "
			    "-- packet dropped!\n");
#endif
			m_freem(m_new);
			return(ENOBUFS);
		}
		/* Attach the buffer to the mbuf */
		m_new->m_len = m_new->m_pkthdr.len = LGE_JUMBO_FRAMELEN;
		m_extadd(m_new, buf, LGE_JUMBO_FRAMELEN, lge_jfree, sc, NULL,
		    0, EXT_NET_DRV);
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = LGE_JUMBO_FRAMELEN;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	/*
	 * Adjust alignment so packet payload begins on a
	 * longword boundary. Mandatory for Alpha, useful on
	 * x86 too.
	*/
	m_adj(m_new, ETHER_ALIGN);

	c->lge_mbuf = m_new;
	c->lge_fragptr_hi = 0;
	c->lge_fragptr_lo = vtophys(mtod(m_new, caddr_t));
	c->lge_fraglen = m_new->m_len;
	c->lge_ctl = m_new->m_len | LGE_RXCTL_WANTINTR | LGE_FRAGCNT(1);
	c->lge_sts = 0;

	/*
	 * Put this buffer in the RX command FIFO. To do this,
	 * we just write the physical address of the descriptor
	 * into the RX descriptor address registers. Note that
	 * there are two registers, one high DWORD and one low
	 * DWORD, which lets us specify a 64-bit address if
	 * desired. We only use a 32-bit address for now.
	 * Writing to the low DWORD register is what actually
	 * causes the command to be issued, so we do that
	 * last.
	 */
	CSR_WRITE_4(sc, LGE_RXDESC_ADDR_LO, vtophys(c));
	LGE_INC(sc->lge_cdata.lge_rx_prod, LGE_RX_LIST_CNT);

	return(0);
}

static int
lge_alloc_jumbo_mem(sc)
	struct lge_softc	*sc;
{
	caddr_t			ptr;
	int			i;
	struct lge_jpool_entry   *entry;

	/* Grab a big chunk o' storage. */
	sc->lge_cdata.lge_jumbo_buf = contigmalloc(LGE_JMEM, M_DEVBUF,
	    M_NOWAIT, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->lge_cdata.lge_jumbo_buf == NULL) {
		device_printf(sc->lge_dev, "no memory for jumbo buffers!\n");
		return(ENOBUFS);
	}

	SLIST_INIT(&sc->lge_jfree_listhead);
	SLIST_INIT(&sc->lge_jinuse_listhead);

	/*
	 * Now divide it up into 9K pieces and save the addresses
	 * in an array.
	 */
	ptr = sc->lge_cdata.lge_jumbo_buf;
	for (i = 0; i < LGE_JSLOTS; i++) {
		sc->lge_cdata.lge_jslots[i] = ptr;
		ptr += LGE_JLEN;
		entry = malloc(sizeof(struct lge_jpool_entry),
		    M_DEVBUF, M_NOWAIT);
		if (entry == NULL) {
			device_printf(sc->lge_dev, "no memory for jumbo "
			    "buffer queue!\n");
			return(ENOBUFS);
		}
		entry->slot = i;
		SLIST_INSERT_HEAD(&sc->lge_jfree_listhead,
		    entry, jpool_entries);
	}

	return(0);
}

static void
lge_free_jumbo_mem(sc)
	struct lge_softc	*sc;
{
	struct lge_jpool_entry	*entry;

	if (sc->lge_cdata.lge_jumbo_buf == NULL)
		return;

	while ((entry = SLIST_FIRST(&sc->lge_jinuse_listhead))) {
		device_printf(sc->lge_dev,
		    "asked to free buffer that is in use!\n");
		SLIST_REMOVE_HEAD(&sc->lge_jinuse_listhead, jpool_entries);
		SLIST_INSERT_HEAD(&sc->lge_jfree_listhead, entry,
		    jpool_entries);
	}
	while (!SLIST_EMPTY(&sc->lge_jfree_listhead)) {
		entry = SLIST_FIRST(&sc->lge_jfree_listhead);
		SLIST_REMOVE_HEAD(&sc->lge_jfree_listhead, jpool_entries);
		free(entry, M_DEVBUF);
	}

	contigfree(sc->lge_cdata.lge_jumbo_buf, LGE_JMEM, M_DEVBUF);

	return;
}

/*
 * Allocate a jumbo buffer.
 */
static void *
lge_jalloc(sc)
	struct lge_softc	*sc;
{
	struct lge_jpool_entry   *entry;
	
	entry = SLIST_FIRST(&sc->lge_jfree_listhead);
	
	if (entry == NULL) {
#ifdef LGE_VERBOSE
		device_printf(sc->lge_dev, "no free jumbo buffers\n");
#endif
		return(NULL);
	}

	SLIST_REMOVE_HEAD(&sc->lge_jfree_listhead, jpool_entries);
	SLIST_INSERT_HEAD(&sc->lge_jinuse_listhead, entry, jpool_entries);
	return(sc->lge_cdata.lge_jslots[entry->slot]);
}

/*
 * Release a jumbo buffer.
 */
static void
lge_jfree(struct mbuf *m)
{
	struct lge_softc	*sc;
	int		        i;
	struct lge_jpool_entry   *entry;

	/* Extract the softc struct pointer. */
	sc = m->m_ext.ext_arg1;

	if (sc == NULL)
		panic("lge_jfree: can't find softc pointer!");

	/* calculate the slot this buffer belongs to */
	i = ((vm_offset_t)m->m_ext.ext_buf
	     - (vm_offset_t)sc->lge_cdata.lge_jumbo_buf) / LGE_JLEN;

	if ((i < 0) || (i >= LGE_JSLOTS))
		panic("lge_jfree: asked to free buffer that we don't manage!");

	entry = SLIST_FIRST(&sc->lge_jinuse_listhead);
	if (entry == NULL)
		panic("lge_jfree: buffer not in use!");
	entry->slot = i;
	SLIST_REMOVE_HEAD(&sc->lge_jinuse_listhead, jpool_entries);
	SLIST_INSERT_HEAD(&sc->lge_jfree_listhead, entry, jpool_entries);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
lge_rxeof(sc, cnt)
	struct lge_softc	*sc;
	int			cnt;
{
        struct mbuf		*m;
        struct ifnet		*ifp;
	struct lge_rx_desc	*cur_rx;
	int			c, i, total_len = 0;
	u_int32_t		rxsts, rxctl;

	ifp = sc->lge_ifp;

	/* Find out how many frames were processed. */
	c = cnt;
	i = sc->lge_cdata.lge_rx_cons;

	/* Suck them in. */
	while(c) {
		struct mbuf		*m0 = NULL;

		cur_rx = &sc->lge_ldata->lge_rx_list[i];
		rxctl = cur_rx->lge_ctl;
		rxsts = cur_rx->lge_sts;
		m = cur_rx->lge_mbuf;
		cur_rx->lge_mbuf = NULL;
		total_len = LGE_RXBYTES(cur_rx);
		LGE_INC(i, LGE_RX_LIST_CNT);
		c--;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxctl & LGE_RXCTL_ERRMASK) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			lge_newbuf(sc, &LGE_RXTAIL(sc), m);
			continue;
		}

		if (lge_newbuf(sc, &LGE_RXTAIL(sc), NULL) == ENOBUFS) {
			m0 = m_devget(mtod(m, char *), total_len, ETHER_ALIGN,
			    ifp, NULL);
			lge_newbuf(sc, &LGE_RXTAIL(sc), m);
			if (m0 == NULL) {
				device_printf(sc->lge_dev, "no receive buffers "
				    "available -- packet dropped!\n");
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				continue;
			}
			m = m0;
		} else {
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = m->m_len = total_len;
		}

		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

		/* Do IP checksum checking. */
		if (rxsts & LGE_RXSTS_ISIP)
			m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
		if (!(rxsts & LGE_RXSTS_IPCSUMERR))
			m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
		if ((rxsts & LGE_RXSTS_ISTCP &&
		    !(rxsts & LGE_RXSTS_TCPCSUMERR)) ||
		    (rxsts & LGE_RXSTS_ISUDP &&
		    !(rxsts & LGE_RXSTS_UDPCSUMERR))) {
			m->m_pkthdr.csum_flags |=
			    CSUM_DATA_VALID|CSUM_PSEUDO_HDR;
			m->m_pkthdr.csum_data = 0xffff;
		}

		LGE_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		LGE_LOCK(sc);
	}

	sc->lge_cdata.lge_rx_cons = i;

	return;
}

static void
lge_rxeoc(sc)
	struct lge_softc	*sc;
{
	struct ifnet		*ifp;

	ifp = sc->lge_ifp;
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	lge_init_locked(sc);
	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

static void
lge_txeof(sc)
	struct lge_softc	*sc;
{
	struct lge_tx_desc	*cur_tx = NULL;
	struct ifnet		*ifp;
	u_int32_t		idx, txdone;

	ifp = sc->lge_ifp;

	/* Clear the timeout timer. */
	sc->lge_timer = 0;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	idx = sc->lge_cdata.lge_tx_cons;
	txdone = CSR_READ_1(sc, LGE_TXDMADONE_8BIT);

	while (idx != sc->lge_cdata.lge_tx_prod && txdone) {
		cur_tx = &sc->lge_ldata->lge_tx_list[idx];

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		if (cur_tx->lge_mbuf != NULL) {
			m_freem(cur_tx->lge_mbuf);
			cur_tx->lge_mbuf = NULL;
		}
		cur_tx->lge_ctl = 0;

		txdone--;
		LGE_INC(idx, LGE_TX_LIST_CNT);
		sc->lge_timer = 0;
	}

	sc->lge_cdata.lge_tx_cons = idx;

	if (cur_tx != NULL)
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	return;
}

static void
lge_tick(xsc)
	void			*xsc;
{
	struct lge_softc	*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;

	sc = xsc;
	ifp = sc->lge_ifp;
	LGE_LOCK_ASSERT(sc);

	CSR_WRITE_4(sc, LGE_STATSIDX, LGE_STATS_SINGLE_COLL_PKTS);
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, CSR_READ_4(sc, LGE_STATSVAL));
	CSR_WRITE_4(sc, LGE_STATSIDX, LGE_STATS_MULTI_COLL_PKTS);
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, CSR_READ_4(sc, LGE_STATSVAL));

	if (!sc->lge_link) {
		mii = device_get_softc(sc->lge_miibus);
		mii_tick(mii);
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
			sc->lge_link++;
			if (bootverbose &&
		  	    (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX||
			    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T))
				device_printf(sc->lge_dev, "gigabit link up\n");
			if (ifp->if_snd.ifq_head != NULL)
				lge_start_locked(ifp);
		}
	}

	if (sc->lge_timer != 0 && --sc->lge_timer == 0)
		lge_watchdog(sc);
	callout_reset(&sc->lge_stat_callout, hz, lge_tick, sc);

	return;
}

static void
lge_intr(arg)
	void			*arg;
{
	struct lge_softc	*sc;
	struct ifnet		*ifp;
	u_int32_t		status;

	sc = arg;
	ifp = sc->lge_ifp;
	LGE_LOCK(sc);

	/* Suppress unwanted interrupts */
	if (!(ifp->if_flags & IFF_UP)) {
		lge_stop(sc);
		LGE_UNLOCK(sc);
		return;
	}

	for (;;) {
		/*
		 * Reading the ISR register clears all interrupts, and
		 * clears the 'interrupts enabled' bit in the IMR
		 * register.
		 */
		status = CSR_READ_4(sc, LGE_ISR);

		if ((status & LGE_INTRS) == 0)
			break;

		if ((status & (LGE_ISR_TXCMDFIFO_EMPTY|LGE_ISR_TXDMA_DONE)))
			lge_txeof(sc);

		if (status & LGE_ISR_RXDMA_DONE)
			lge_rxeof(sc, LGE_RX_DMACNT(status));

		if (status & LGE_ISR_RXCMDFIFO_EMPTY)
			lge_rxeoc(sc);

		if (status & LGE_ISR_PHY_INTR) {
			sc->lge_link = 0;
			callout_stop(&sc->lge_stat_callout);
			lge_tick(sc);
		}
	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_SETRST_CTL0|LGE_IMR_INTR_ENB);

	if (ifp->if_snd.ifq_head != NULL)
		lge_start_locked(ifp);

	LGE_UNLOCK(sc);
	return;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
lge_encap(sc, m_head, txidx)
	struct lge_softc	*sc;
	struct mbuf		*m_head;
	u_int32_t		*txidx;
{
	struct lge_frag		*f = NULL;
	struct lge_tx_desc	*cur_tx;
	struct mbuf		*m;
	int			frag = 0, tot_len = 0;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	m = m_head;
	cur_tx = &sc->lge_ldata->lge_tx_list[*txidx];
	frag = 0;

	for (m = m_head; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			tot_len += m->m_len;
			f = &cur_tx->lge_frags[frag];
			f->lge_fraglen = m->m_len;
			f->lge_fragptr_lo = vtophys(mtod(m, vm_offset_t));
			f->lge_fragptr_hi = 0;
			frag++;
		}
	}

	if (m != NULL)
		return(ENOBUFS);

	cur_tx->lge_mbuf = m_head;
	cur_tx->lge_ctl = LGE_TXCTL_WANTINTR|LGE_FRAGCNT(frag)|tot_len;
	LGE_INC((*txidx), LGE_TX_LIST_CNT);

	/* Queue for transmit */
	CSR_WRITE_4(sc, LGE_TXDESC_ADDR_LO, vtophys(cur_tx));

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

static void
lge_start(ifp)
	struct ifnet		*ifp;
{
	struct lge_softc	*sc;

	sc = ifp->if_softc;
	LGE_LOCK(sc);
	lge_start_locked(ifp);
	LGE_UNLOCK(sc);
}

static void
lge_start_locked(ifp)
	struct ifnet		*ifp;
{
	struct lge_softc	*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		idx;

	sc = ifp->if_softc;

	if (!sc->lge_link)
		return;

	idx = sc->lge_cdata.lge_tx_prod;

	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;

	while(sc->lge_ldata->lge_tx_list[idx].lge_mbuf == NULL) {
		if (CSR_READ_1(sc, LGE_TXCMDFREE_8BIT) == 0)
			break;

		IF_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		if (lge_encap(sc, m_head, &idx)) {
			IF_PREPEND(&ifp->if_snd, m_head);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, m_head);
	}

	sc->lge_cdata.lge_tx_prod = idx;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	sc->lge_timer = 5;

	return;
}

static void
lge_init(xsc)
	void			*xsc;
{
	struct lge_softc	*sc = xsc;

	LGE_LOCK(sc);
	lge_init_locked(sc);
	LGE_UNLOCK(sc);
}

static void
lge_init_locked(sc)
	struct lge_softc	*sc;
{
	struct ifnet		*ifp = sc->lge_ifp;

	LGE_LOCK_ASSERT(sc);
	if (ifp->if_drv_flags & IFF_DRV_RUNNING)
		return;

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	lge_stop(sc);
	lge_reset(sc);

	/* Set MAC address */
	CSR_WRITE_4(sc, LGE_PAR0, *(u_int32_t *)(&IF_LLADDR(sc->lge_ifp)[0]));
	CSR_WRITE_4(sc, LGE_PAR1, *(u_int32_t *)(&IF_LLADDR(sc->lge_ifp)[4]));

	/* Init circular RX list. */
	if (lge_list_rx_init(sc) == ENOBUFS) {
		device_printf(sc->lge_dev, "initialization failed: no "
		    "memory for rx buffers\n");
		lge_stop(sc);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	lge_list_tx_init(sc);

	/* Set initial value for MODE1 register. */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_UCAST|
	    LGE_MODE1_TX_CRC|LGE_MODE1_TXPAD|
	    LGE_MODE1_RX_FLOWCTL|LGE_MODE1_SETRST_CTL0|
	    LGE_MODE1_SETRST_CTL1|LGE_MODE1_SETRST_CTL2);

	 /* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		CSR_WRITE_4(sc, LGE_MODE1,
		    LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_PROMISC);
	} else {
		CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_PROMISC);
	}

	/*
	 * Set the capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST) {
		CSR_WRITE_4(sc, LGE_MODE1,
		    LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_BCAST);
	} else {
		CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_BCAST);
	}

	/* Packet padding workaround? */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RMVPAD);

	/* No error frames */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_ERRPKTS);

	/* Receive large frames */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_GIANTS);

	/* Workaround: disable RX/TX flow control */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_TX_FLOWCTL);
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_FLOWCTL);

	/* Make sure to strip CRC from received frames */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_CRC);

	/* Turn off magic packet mode */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_MPACK_ENB);

	/* Turn off all VLAN stuff */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_VLAN_RX|LGE_MODE1_VLAN_TX|
	    LGE_MODE1_VLAN_STRIP|LGE_MODE1_VLAN_INSERT);

	/* Workarond: FIFO overflow */
	CSR_WRITE_2(sc, LGE_RXFIFO_HIWAT, 0x3FFF);
	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_SETRST_CTL1|LGE_IMR_RXFIFO_WAT);

	/*
	 * Load the multicast filter.
	 */
	lge_setmulti(sc);

	/*
	 * Enable hardware checksum validation for all received IPv4
	 * packets, do not reject packets with bad checksums.
	 */
	CSR_WRITE_4(sc, LGE_MODE2, LGE_MODE2_RX_IPCSUM|
	    LGE_MODE2_RX_TCPCSUM|LGE_MODE2_RX_UDPCSUM|
	    LGE_MODE2_RX_ERRCSUM);

	/*
	 * Enable the delivery of PHY interrupts based on
	 * link/speed/duplex status chalges.
	 */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL0|LGE_MODE1_GMIIPOLL);

	/* Enable receiver and transmitter. */
	CSR_WRITE_4(sc, LGE_RXDESC_ADDR_HI, 0);
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_RX_ENB);

	CSR_WRITE_4(sc, LGE_TXDESC_ADDR_HI, 0);
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_SETRST_CTL1|LGE_MODE1_TX_ENB);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_SETRST_CTL0|
	    LGE_IMR_SETRST_CTL1|LGE_IMR_INTR_ENB|LGE_INTRS);

	lge_ifmedia_upd_locked(ifp);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->lge_stat_callout, hz, lge_tick, sc);

	return;
}

/*
 * Set media options.
 */
static int
lge_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct lge_softc	*sc;

	sc = ifp->if_softc;
	LGE_LOCK(sc);
	lge_ifmedia_upd_locked(ifp);
	LGE_UNLOCK(sc);

	return(0);
}

static void
lge_ifmedia_upd_locked(ifp)
	struct ifnet		*ifp;
{
	struct lge_softc	*sc;
	struct mii_data		*mii;
	struct mii_softc	*miisc;

	sc = ifp->if_softc;

	LGE_LOCK_ASSERT(sc);
	mii = device_get_softc(sc->lge_miibus);
	sc->lge_link = 0;
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	mii_mediachg(mii);
}

/*
 * Report current media status.
 */
static void
lge_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct lge_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;

	LGE_LOCK(sc);
	mii = device_get_softc(sc->lge_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	LGE_UNLOCK(sc);

	return;
}

static int
lge_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct lge_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct mii_data		*mii;
	int			error = 0;

	switch(command) {
	case SIOCSIFMTU:
		LGE_LOCK(sc);
		if (ifr->ifr_mtu > LGE_JUMBO_MTU)
			error = EINVAL;
		else
			ifp->if_mtu = ifr->ifr_mtu;
		LGE_UNLOCK(sc);
		break;
	case SIOCSIFFLAGS:
		LGE_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->lge_if_flags & IFF_PROMISC)) {
				CSR_WRITE_4(sc, LGE_MODE1,
				    LGE_MODE1_SETRST_CTL1|
				    LGE_MODE1_RX_PROMISC);
			} else if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->lge_if_flags & IFF_PROMISC) {
				CSR_WRITE_4(sc, LGE_MODE1,
				    LGE_MODE1_RX_PROMISC);
			} else {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				lge_init_locked(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				lge_stop(sc);
		}
		sc->lge_if_flags = ifp->if_flags;
		LGE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		LGE_LOCK(sc);
		lge_setmulti(sc);
		LGE_UNLOCK(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->lge_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return(error);
}

static void
lge_watchdog(sc)
	struct lge_softc	*sc;
{
	struct ifnet		*ifp;

	LGE_LOCK_ASSERT(sc);
	ifp = sc->lge_ifp;

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if_printf(ifp, "watchdog timeout\n");

	lge_stop(sc);
	lge_reset(sc);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	lge_init_locked(sc);

	if (ifp->if_snd.ifq_head != NULL)
		lge_start_locked(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
lge_stop(sc)
	struct lge_softc	*sc;
{
	int			i;
	struct ifnet		*ifp;

	LGE_LOCK_ASSERT(sc);
	ifp = sc->lge_ifp;
	sc->lge_timer = 0;
	callout_stop(&sc->lge_stat_callout);
	CSR_WRITE_4(sc, LGE_IMR, LGE_IMR_INTR_ENB);

	/* Disable receiver and transmitter. */
	CSR_WRITE_4(sc, LGE_MODE1, LGE_MODE1_RX_ENB|LGE_MODE1_TX_ENB);
	sc->lge_link = 0;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < LGE_RX_LIST_CNT; i++) {
		if (sc->lge_ldata->lge_rx_list[i].lge_mbuf != NULL) {
			m_freem(sc->lge_ldata->lge_rx_list[i].lge_mbuf);
			sc->lge_ldata->lge_rx_list[i].lge_mbuf = NULL;
		}
	}
	bzero((char *)&sc->lge_ldata->lge_rx_list,
		sizeof(sc->lge_ldata->lge_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < LGE_TX_LIST_CNT; i++) {
		if (sc->lge_ldata->lge_tx_list[i].lge_mbuf != NULL) {
			m_freem(sc->lge_ldata->lge_tx_list[i].lge_mbuf);
			sc->lge_ldata->lge_tx_list[i].lge_mbuf = NULL;
		}
	}

	bzero((char *)&sc->lge_ldata->lge_tx_list,
		sizeof(sc->lge_ldata->lge_tx_list));

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
lge_shutdown(dev)
	device_t		dev;
{
	struct lge_softc	*sc;

	sc = device_get_softc(dev);

	LGE_LOCK(sc);
	lge_reset(sc);
	lge_stop(sc);
	LGE_UNLOCK(sc);

	return (0);
}
