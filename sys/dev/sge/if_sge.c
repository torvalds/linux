/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2008-2010 Nikolay Denev <ndenev@gmail.com>
 * Copyright (c) 2007-2008 Alexander Pohoyda <alexander.pohoyda@gmx.net>
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL AUTHORS OR
 * THE VOICES IN THEIR HEADS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * SiS 190/191 PCI Ethernet NIC driver.
 *
 * Adapted to SiS 190 NIC by Alexander Pohoyda based on the original
 * SiS 900 driver by Bill Paul, using SiS 190/191 Solaris driver by
 * Masayuki Murayama and SiS 190/191 GNU/Linux driver by K.M. Liu
 * <kmliu@sis.com>.  Thanks to Pyun YongHyeon <pyunyh@gmail.com> for
 * review and very useful comments.
 *
 * Adapted to SiS 191 NIC by Nikolay Denev with further ideas from the
 * Linux and Solaris drivers.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <machine/bus.h>
#include <machine/in_cksum.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/sge/if_sgereg.h>

MODULE_DEPEND(sge, pci, 1, 1, 1);
MODULE_DEPEND(sge, ether, 1, 1, 1);
MODULE_DEPEND(sge, miibus, 1, 1, 1);

/* "device miibus0" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Various supported device vendors/types and their names.
 */
static struct sge_type sge_devs[] = {
	{ SIS_VENDORID, SIS_DEVICEID_190, "SiS190 Fast Ethernet" },
	{ SIS_VENDORID, SIS_DEVICEID_191, "SiS191 Fast/Gigabit Ethernet" },
	{ 0, 0, NULL }
};

static int	sge_probe(device_t);
static int	sge_attach(device_t);
static int	sge_detach(device_t);
static int	sge_shutdown(device_t);
static int	sge_suspend(device_t);
static int	sge_resume(device_t);

static int	sge_miibus_readreg(device_t, int, int);
static int	sge_miibus_writereg(device_t, int, int, int);
static void	sge_miibus_statchg(device_t);

static int	sge_newbuf(struct sge_softc *, int);
static int	sge_encap(struct sge_softc *, struct mbuf **);
static __inline void
		sge_discard_rxbuf(struct sge_softc *, int);
static void	sge_rxeof(struct sge_softc *);
static void	sge_txeof(struct sge_softc *);
static void	sge_intr(void *);
static void	sge_tick(void *);
static void	sge_start(struct ifnet *);
static void	sge_start_locked(struct ifnet *);
static int	sge_ioctl(struct ifnet *, u_long, caddr_t);
static void	sge_init(void *);
static void	sge_init_locked(struct sge_softc *);
static void	sge_stop(struct sge_softc *);
static void	sge_watchdog(struct sge_softc *);
static int	sge_ifmedia_upd(struct ifnet *);
static void	sge_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int	sge_get_mac_addr_apc(struct sge_softc *, uint8_t *);
static int	sge_get_mac_addr_eeprom(struct sge_softc *, uint8_t *);
static uint16_t	sge_read_eeprom(struct sge_softc *, int);

static void	sge_rxfilter(struct sge_softc *);
static void	sge_setvlan(struct sge_softc *);
static void	sge_reset(struct sge_softc *);
static int	sge_list_rx_init(struct sge_softc *);
static int	sge_list_rx_free(struct sge_softc *);
static int	sge_list_tx_init(struct sge_softc *);
static int	sge_list_tx_free(struct sge_softc *);

static int	sge_dma_alloc(struct sge_softc *);
static void	sge_dma_free(struct sge_softc *);
static void	sge_dma_map_addr(void *, bus_dma_segment_t *, int, int);

static device_method_t sge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		sge_probe),
	DEVMETHOD(device_attach,	sge_attach),
	DEVMETHOD(device_detach,	sge_detach),
	DEVMETHOD(device_suspend,	sge_suspend),
	DEVMETHOD(device_resume,	sge_resume),
	DEVMETHOD(device_shutdown,	sge_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	sge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	sge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	sge_miibus_statchg),

	DEVMETHOD_END
};

static driver_t sge_driver = {
	"sge", sge_methods, sizeof(struct sge_softc)
};

static devclass_t sge_devclass;

DRIVER_MODULE(sge, pci, sge_driver, sge_devclass, 0, 0);
DRIVER_MODULE(miibus, sge, miibus_driver, miibus_devclass, 0, 0);

/*
 * Register space access macros.
 */
#define	CSR_WRITE_4(sc, reg, val)	bus_write_4(sc->sge_res, reg, val)
#define	CSR_WRITE_2(sc, reg, val)	bus_write_2(sc->sge_res, reg, val)
#define	CSR_WRITE_1(cs, reg, val)	bus_write_1(sc->sge_res, reg, val)

#define	CSR_READ_4(sc, reg)		bus_read_4(sc->sge_res, reg)
#define	CSR_READ_2(sc, reg)		bus_read_2(sc->sge_res, reg)
#define	CSR_READ_1(sc, reg)		bus_read_1(sc->sge_res, reg)

/* Define to show Tx/Rx error status. */
#undef SGE_SHOW_ERRORS

#define	SGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

static void
sge_dma_map_addr(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	bus_addr_t *p;

	if (error != 0)
		return;
	KASSERT(nseg == 1, ("too many DMA segments, %d should be 1", nseg));
	p  = arg;
	*p = segs->ds_addr;
}

/*
 * Read a sequence of words from the EEPROM.
 */
static uint16_t
sge_read_eeprom(struct sge_softc *sc, int offset)
{
	uint32_t val;
	int i;

	KASSERT(offset <= EI_OFFSET, ("EEPROM offset too big"));
	CSR_WRITE_4(sc, ROMInterface,
	    EI_REQ | EI_OP_RD | (offset << EI_OFFSET_SHIFT));
	DELAY(500);
	for (i = 0; i < SGE_TIMEOUT; i++) {
		val = CSR_READ_4(sc, ROMInterface);
		if ((val & EI_REQ) == 0)
			break;
		DELAY(100);
	}
	if (i == SGE_TIMEOUT) {
		device_printf(sc->sge_dev,
		    "EEPROM read timeout : 0x%08x\n", val);
		return (0xffff);
	}

	return ((val & EI_DATA) >> EI_DATA_SHIFT);
}

static int
sge_get_mac_addr_eeprom(struct sge_softc *sc, uint8_t *dest)
{
	uint16_t val;
	int i;

	val = sge_read_eeprom(sc, EEPROMSignature);
	if (val == 0xffff || val == 0) {
		device_printf(sc->sge_dev,
		    "invalid EEPROM signature : 0x%04x\n", val);
		return (EINVAL);
	}

	for (i = 0; i < ETHER_ADDR_LEN; i += 2) {
		val = sge_read_eeprom(sc, EEPROMMACAddr + i / 2);
		dest[i + 0] = (uint8_t)val;
		dest[i + 1] = (uint8_t)(val >> 8);
	}

	if ((sge_read_eeprom(sc, EEPROMInfo) & 0x80) != 0)
		sc->sge_flags |= SGE_FLAG_RGMII;
	return (0);
}

/*
 * For SiS96x, APC CMOS RAM is used to store ethernet address.
 * APC CMOS RAM is accessed through ISA bridge.
 */
static int
sge_get_mac_addr_apc(struct sge_softc *sc, uint8_t *dest)
{
#if defined(__amd64__) || defined(__i386__)
	devclass_t pci;
	device_t bus, dev = NULL;
	device_t *kids;
	struct apc_tbl {
		uint16_t vid;
		uint16_t did;
	} *tp, apc_tbls[] = {
		{ SIS_VENDORID, 0x0965 },
		{ SIS_VENDORID, 0x0966 },
		{ SIS_VENDORID, 0x0968 }
	};
	uint8_t reg;
	int busnum, i, j, numkids;

	pci = devclass_find("pci");
	for (busnum = 0; busnum < devclass_get_maxunit(pci); busnum++) {
		bus = devclass_get_device(pci, busnum);
		if (!bus)
			continue;
		if (device_get_children(bus, &kids, &numkids) != 0)
			continue;
		for (i = 0; i < numkids; i++) {
			dev = kids[i];
			if (pci_get_class(dev) == PCIC_BRIDGE &&
			    pci_get_subclass(dev) == PCIS_BRIDGE_ISA) {
				tp = apc_tbls;
				for (j = 0; j < nitems(apc_tbls); j++) {
					if (pci_get_vendor(dev) == tp->vid &&
					    pci_get_device(dev) == tp->did) {
						free(kids, M_TEMP);
						goto apc_found;
					}
					tp++;
				}
			}
                }
		free(kids, M_TEMP);
	}
	device_printf(sc->sge_dev, "couldn't find PCI-ISA bridge\n");
	return (EINVAL);
apc_found:
	/* Enable port 0x78 and 0x79 to access APC registers. */
	reg = pci_read_config(dev, 0x48, 1);
	pci_write_config(dev, 0x48, reg & ~0x02, 1);
	DELAY(50);
	pci_read_config(dev, 0x48, 1);
	/* Read stored ethernet address. */
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		outb(0x78, 0x09 + i);
		dest[i] = inb(0x79);
	}
	outb(0x78, 0x12);
	if ((inb(0x79) & 0x80) != 0)
		sc->sge_flags |= SGE_FLAG_RGMII;
	/* Restore access to APC registers. */
	pci_write_config(dev, 0x48, reg, 1);

	return (0);
#else
	return (EINVAL);
#endif
}

static int
sge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct sge_softc *sc;
	uint32_t val;
	int i;

	sc = device_get_softc(dev);
	CSR_WRITE_4(sc, GMIIControl, (phy << GMI_PHY_SHIFT) |
	    (reg << GMI_REG_SHIFT) | GMI_OP_RD | GMI_REQ);
	DELAY(10);
	for (i = 0; i < SGE_TIMEOUT; i++) {
		val = CSR_READ_4(sc, GMIIControl);
		if ((val & GMI_REQ) == 0)
			break;
		DELAY(10);
	}
	if (i == SGE_TIMEOUT) {
		device_printf(sc->sge_dev, "PHY read timeout : %d\n", reg);
		return (0);
	}
	return ((val & GMI_DATA) >> GMI_DATA_SHIFT);
}

static int
sge_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct sge_softc *sc;
	uint32_t val;
	int i;

	sc = device_get_softc(dev);
	CSR_WRITE_4(sc, GMIIControl, (phy << GMI_PHY_SHIFT) |
	    (reg << GMI_REG_SHIFT) | (data << GMI_DATA_SHIFT) |
	    GMI_OP_WR | GMI_REQ);
	DELAY(10);
	for (i = 0; i < SGE_TIMEOUT; i++) {
		val = CSR_READ_4(sc, GMIIControl);
		if ((val & GMI_REQ) == 0)
			break;
		DELAY(10);
	}
	if (i == SGE_TIMEOUT)
		device_printf(sc->sge_dev, "PHY write timeout : %d\n", reg);
	return (0);
}

static void
sge_miibus_statchg(device_t dev)
{
	struct sge_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t ctl, speed;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->sge_miibus);
	ifp = sc->sge_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;
	speed = 0;
	sc->sge_flags &= ~SGE_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
			sc->sge_flags |= SGE_FLAG_LINK;
			speed = SC_SPEED_10;
			break;
		case IFM_100_TX:
			sc->sge_flags |= SGE_FLAG_LINK;
			speed = SC_SPEED_100;
			break;
		case IFM_1000_T:
			if ((sc->sge_flags & SGE_FLAG_FASTETHER) == 0) {
				sc->sge_flags |= SGE_FLAG_LINK;
				speed = SC_SPEED_1000;
			}
			break;
		default:
			break;
                }
        }
	if ((sc->sge_flags & SGE_FLAG_LINK) == 0)
		return;
	/* Reprogram MAC to resolved speed/duplex/flow-control parameters. */
	ctl = CSR_READ_4(sc, StationControl);
	ctl &= ~(0x0f000000 | SC_FDX | SC_SPEED_MASK);
	if (speed == SC_SPEED_1000) {
		ctl |= 0x07000000;
		sc->sge_flags |= SGE_FLAG_SPEED_1000;
	} else {
		ctl |= 0x04000000;
		sc->sge_flags &= ~SGE_FLAG_SPEED_1000;
	}
#ifdef notyet
	if ((sc->sge_flags & SGE_FLAG_GMII) != 0)
		ctl |= 0x03000000;
#endif
	ctl |= speed;
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		ctl |= SC_FDX;
		sc->sge_flags |= SGE_FLAG_FDX;
	} else
		sc->sge_flags &= ~SGE_FLAG_FDX;
	CSR_WRITE_4(sc, StationControl, ctl);
	if ((sc->sge_flags & SGE_FLAG_RGMII) != 0) {
		CSR_WRITE_4(sc, RGMIIDelay, 0x0441);
		CSR_WRITE_4(sc, RGMIIDelay, 0x0440);
	}
}

static void
sge_rxfilter(struct sge_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t crc, hashes[2];
	uint16_t rxfilt;

	SGE_LOCK_ASSERT(sc);

	ifp = sc->sge_ifp;
	rxfilt = CSR_READ_2(sc, RxMacControl);
	rxfilt &= ~(AcceptBroadcast | AcceptAllPhys | AcceptMulticast);
	rxfilt |= AcceptMyPhys;
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		rxfilt |= AcceptBroadcast;
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			rxfilt |= AcceptAllPhys;
		rxfilt |= AcceptMulticast;
		hashes[0] = 0xFFFFFFFF;
		hashes[1] = 0xFFFFFFFF;
	} else {
		rxfilt |= AcceptMulticast;
		hashes[0] = hashes[1] = 0;
		/* Now program new ones. */
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN);
			hashes[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);
		}
		if_maddr_runlock(ifp);
	}
	CSR_WRITE_2(sc, RxMacControl, rxfilt);
	CSR_WRITE_4(sc, RxHashTable, hashes[0]);
	CSR_WRITE_4(sc, RxHashTable2, hashes[1]);
}

static void
sge_setvlan(struct sge_softc *sc)
{
	struct ifnet *ifp;
	uint16_t rxfilt;

	SGE_LOCK_ASSERT(sc);

	ifp = sc->sge_ifp;
	if ((ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) == 0)
		return;
	rxfilt = CSR_READ_2(sc, RxMacControl);
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		rxfilt |= RXMAC_STRIP_VLAN;
	else
		rxfilt &= ~RXMAC_STRIP_VLAN;
	CSR_WRITE_2(sc, RxMacControl, rxfilt);
}

static void
sge_reset(struct sge_softc *sc)
{

	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);

	/* Soft reset. */
	CSR_WRITE_4(sc, IntrControl, 0x8000);
	CSR_READ_4(sc, IntrControl);
	DELAY(100);
	CSR_WRITE_4(sc, IntrControl, 0);
	/* Stop MAC. */
	CSR_WRITE_4(sc, TX_CTL, 0x1a00);
	CSR_WRITE_4(sc, RX_CTL, 0x1a00);

	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);

	CSR_WRITE_4(sc, GMIIControl, 0);
}

/*
 * Probe for an SiS chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
sge_probe(device_t dev)
{
	struct sge_type *t;

	t = sge_devs;
	while (t->sge_name != NULL) {
		if ((pci_get_vendor(dev) == t->sge_vid) &&
		    (pci_get_device(dev) == t->sge_did)) {
			device_set_desc(dev, t->sge_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return (ENXIO);
}

/*
 * Attach the interface.  Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
sge_attach(device_t dev)
{
	struct sge_softc *sc;
	struct ifnet *ifp;
	uint8_t eaddr[ETHER_ADDR_LEN];
	int error = 0, rid;

	sc = device_get_softc(dev);
	sc->sge_dev = dev;

	mtx_init(&sc->sge_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
        callout_init_mtx(&sc->sge_stat_ch, &sc->sge_mtx, 0);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	/* Allocate resources. */
	sc->sge_res_id = PCIR_BAR(0);
	sc->sge_res_type = SYS_RES_MEMORY;
	sc->sge_res = bus_alloc_resource_any(dev, sc->sge_res_type,
	    &sc->sge_res_id, RF_ACTIVE);
	if (sc->sge_res == NULL) {
		device_printf(dev, "couldn't allocate resource\n");
		error = ENXIO;
		goto fail;
	}

	rid = 0;
	sc->sge_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->sge_irq == NULL) {
		device_printf(dev, "couldn't allocate IRQ resources\n");
		error = ENXIO;
		goto fail;
	}
	sc->sge_rev = pci_get_revid(dev);
	if (pci_get_device(dev) == SIS_DEVICEID_190)
		sc->sge_flags |= SGE_FLAG_FASTETHER | SGE_FLAG_SIS190;
	/* Reset the adapter. */
	sge_reset(sc);

	/* Get MAC address from the EEPROM. */
	if ((pci_read_config(dev, 0x73, 1) & 0x01) != 0)
		sge_get_mac_addr_apc(sc, eaddr);
	else
		sge_get_mac_addr_eeprom(sc, eaddr);

	if ((error = sge_dma_alloc(sc)) != 0)
		goto fail;

	ifp = sc->sge_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet structure.\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sge_ioctl;
	ifp->if_start = sge_start;
	ifp->if_init = sge_init;
	ifp->if_snd.ifq_drv_maxlen = SGE_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_RXCSUM | IFCAP_TSO4;
	ifp->if_hwassist = SGE_CSUM_FEATURES | CSUM_TSO;
	ifp->if_capenable = ifp->if_capabilities;
	/*
	 * Do MII setup.
	 */
	error = mii_attach(dev, &sc->sge_miibus, ifp, sge_ifmedia_upd,
	    sge_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/* VLAN setup. */
	ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWCSUM |
	    IFCAP_VLAN_HWTSO | IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->sge_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, sge_intr, sc, &sc->sge_intrhand);
	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		sge_detach(dev);

	return (error);
}

/*
 * Shutdown hardware and free up resources.  This can be called any
 * time after the mutex has been initialized.  It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
sge_detach(device_t dev)
{
	struct sge_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->sge_ifp;
	/* These should only be active if attach succeeded. */
	if (device_is_attached(dev)) {
		ether_ifdetach(ifp);
		SGE_LOCK(sc);
		sge_stop(sc);
		SGE_UNLOCK(sc);
		callout_drain(&sc->sge_stat_ch);
	}
	if (sc->sge_miibus)
		device_delete_child(dev, sc->sge_miibus);
	bus_generic_detach(dev);

	if (sc->sge_intrhand)
		bus_teardown_intr(dev, sc->sge_irq, sc->sge_intrhand);
	if (sc->sge_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->sge_irq);
	if (sc->sge_res)
		bus_release_resource(dev, sc->sge_res_type, sc->sge_res_id,
		    sc->sge_res);
	if (ifp)
		if_free(ifp);
	sge_dma_free(sc);
	mtx_destroy(&sc->sge_mtx);

	return (0);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
sge_shutdown(device_t dev)
{
	struct sge_softc *sc;

	sc = device_get_softc(dev);
	SGE_LOCK(sc);
	sge_stop(sc);
	SGE_UNLOCK(sc);
	return (0);
}

static int
sge_suspend(device_t dev)
{
	struct sge_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	SGE_LOCK(sc);
	ifp = sc->sge_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		sge_stop(sc);
	SGE_UNLOCK(sc);
	return (0);
}

static int
sge_resume(device_t dev)
{
	struct sge_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	SGE_LOCK(sc);
	ifp = sc->sge_ifp;
	if ((ifp->if_flags & IFF_UP) != 0)
		sge_init_locked(sc);
	SGE_UNLOCK(sc);
	return (0);
}

static int
sge_dma_alloc(struct sge_softc *sc)
{
	struct sge_chain_data *cd;
	struct sge_list_data *ld;
	struct sge_rxdesc *rxd;
	struct sge_txdesc *txd;
	int error, i;

	cd = &sc->sge_cdata;
	ld = &sc->sge_ldata;
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sge_dev),
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    1,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL,			/* lockfunc */
	    NULL,			/* lockarg */
	    &cd->sge_tag);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not create parent DMA tag.\n");
		goto fail;
	}

	/* RX descriptor ring */
	error = bus_dma_tag_create(cd->sge_tag,
	    SGE_DESC_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    SGE_RX_RING_SZ, 1,		/* maxsize,nsegments */
	    SGE_RX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL,			/* lockfunc */
	    NULL,			/* lockarg */
	    &cd->sge_rx_tag);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not create Rx ring DMA tag.\n");
		goto fail;
	}
	/* Allocate DMA'able memory and load DMA map for RX ring. */
	error = bus_dmamem_alloc(cd->sge_rx_tag, (void **)&ld->sge_rx_ring,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &cd->sge_rx_dmamap);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not allocate DMA'able memory for Rx ring.\n");
		goto fail;
	}
	error = bus_dmamap_load(cd->sge_rx_tag, cd->sge_rx_dmamap,
	    ld->sge_rx_ring, SGE_RX_RING_SZ, sge_dma_map_addr,
	    &ld->sge_rx_paddr, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not load DMA'able memory for Rx ring.\n");
	}

	/* TX descriptor ring */
	error = bus_dma_tag_create(cd->sge_tag,
	    SGE_DESC_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    SGE_TX_RING_SZ, 1,		/* maxsize,nsegments */
	    SGE_TX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL,			/* lockfunc */
	    NULL,			/* lockarg */
	    &cd->sge_tx_tag);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not create Rx ring DMA tag.\n");
		goto fail;
	}
	/* Allocate DMA'able memory and load DMA map for TX ring. */
	error = bus_dmamem_alloc(cd->sge_tx_tag, (void **)&ld->sge_tx_ring,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &cd->sge_tx_dmamap);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not allocate DMA'able memory for Tx ring.\n");
		goto fail;
	}
	error = bus_dmamap_load(cd->sge_tx_tag, cd->sge_tx_dmamap,
	    ld->sge_tx_ring, SGE_TX_RING_SZ, sge_dma_map_addr,
	    &ld->sge_tx_paddr, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not load DMA'able memory for Rx ring.\n");
		goto fail;
	}

	/* Create DMA tag for Tx buffers. */
	error = bus_dma_tag_create(cd->sge_tag, 1, 0, BUS_SPACE_MAXADDR,
	    BUS_SPACE_MAXADDR, NULL, NULL, SGE_TSO_MAXSIZE, SGE_MAXTXSEGS,
	    SGE_TSO_MAXSEGSIZE, 0, NULL, NULL, &cd->sge_txmbuf_tag);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not create Tx mbuf DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for Rx buffers. */
	error = bus_dma_tag_create(cd->sge_tag, SGE_RX_BUF_ALIGN, 0,
	    BUS_SPACE_MAXADDR, BUS_SPACE_MAXADDR, NULL, NULL, MCLBYTES, 1,
	    MCLBYTES, 0, NULL, NULL, &cd->sge_rxmbuf_tag);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not create Rx mbuf DMA tag.\n");
		goto fail;
	}

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < SGE_TX_RING_CNT; i++) {
		txd = &cd->sge_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		txd->tx_ndesc = 0;
		error = bus_dmamap_create(cd->sge_txmbuf_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->sge_dev,
			    "could not create Tx DMA map.\n");
			goto fail;
		}
	}
	/* Create spare DMA map for Rx buffer. */
	error = bus_dmamap_create(cd->sge_rxmbuf_tag, 0, &cd->sge_rx_spare_map);
	if (error != 0) {
		device_printf(sc->sge_dev,
		    "could not create spare Rx DMA map.\n");
		goto fail;
	}
	/* Create DMA maps for Rx buffers. */
	for (i = 0; i < SGE_RX_RING_CNT; i++) {
		rxd = &cd->sge_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(cd->sge_rxmbuf_tag, 0,
		    &rxd->rx_dmamap);
		if (error) {
			device_printf(sc->sge_dev,
			    "could not create Rx DMA map.\n");
			goto fail;
		}
	}
fail:
	return (error);
}

static void
sge_dma_free(struct sge_softc *sc)
{
	struct sge_chain_data *cd;
	struct sge_list_data *ld;
	struct sge_rxdesc *rxd;
	struct sge_txdesc *txd;
	int i;

	cd = &sc->sge_cdata;
	ld = &sc->sge_ldata;
	/* Rx ring. */
	if (cd->sge_rx_tag != NULL) {
		if (ld->sge_rx_paddr != 0)
			bus_dmamap_unload(cd->sge_rx_tag, cd->sge_rx_dmamap);
		if (ld->sge_rx_ring != NULL)
			bus_dmamem_free(cd->sge_rx_tag, ld->sge_rx_ring,
			    cd->sge_rx_dmamap);
		ld->sge_rx_ring = NULL;
		ld->sge_rx_paddr = 0;
		bus_dma_tag_destroy(cd->sge_rx_tag);
		cd->sge_rx_tag = NULL;
	}
	/* Tx ring. */
	if (cd->sge_tx_tag != NULL) {
		if (ld->sge_tx_paddr != 0)
			bus_dmamap_unload(cd->sge_tx_tag, cd->sge_tx_dmamap);
		if (ld->sge_tx_ring != NULL)
			bus_dmamem_free(cd->sge_tx_tag, ld->sge_tx_ring,
			    cd->sge_tx_dmamap);
		ld->sge_tx_ring = NULL;
		ld->sge_tx_paddr = 0;
		bus_dma_tag_destroy(cd->sge_tx_tag);
		cd->sge_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (cd->sge_rxmbuf_tag != NULL) {
		for (i = 0; i < SGE_RX_RING_CNT; i++) {
			rxd = &cd->sge_rxdesc[i];
			if (rxd->rx_dmamap != NULL) {
				bus_dmamap_destroy(cd->sge_rxmbuf_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (cd->sge_rx_spare_map != NULL) {
			bus_dmamap_destroy(cd->sge_rxmbuf_tag,
			    cd->sge_rx_spare_map);
			cd->sge_rx_spare_map = NULL;
		}
		bus_dma_tag_destroy(cd->sge_rxmbuf_tag);
		cd->sge_rxmbuf_tag = NULL;
	}
	/* Tx buffers. */
	if (cd->sge_txmbuf_tag != NULL) {
		for (i = 0; i < SGE_TX_RING_CNT; i++) {
			txd = &cd->sge_txdesc[i];
			if (txd->tx_dmamap != NULL) {
				bus_dmamap_destroy(cd->sge_txmbuf_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(cd->sge_txmbuf_tag);
		cd->sge_txmbuf_tag = NULL;
	}
	if (cd->sge_tag != NULL)
		bus_dma_tag_destroy(cd->sge_tag);
	cd->sge_tag = NULL;
}

/*
 * Initialize the TX descriptors.
 */
static int
sge_list_tx_init(struct sge_softc *sc)
{
	struct sge_list_data *ld;
	struct sge_chain_data *cd;

	SGE_LOCK_ASSERT(sc);
	ld = &sc->sge_ldata;
	cd = &sc->sge_cdata;
	bzero(ld->sge_tx_ring, SGE_TX_RING_SZ);
	ld->sge_tx_ring[SGE_TX_RING_CNT - 1].sge_flags = htole32(RING_END);
	bus_dmamap_sync(cd->sge_tx_tag, cd->sge_tx_dmamap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	cd->sge_tx_prod = 0;
	cd->sge_tx_cons = 0;
	cd->sge_tx_cnt = 0;
	return (0);
}

static int
sge_list_tx_free(struct sge_softc *sc)
{
	struct sge_chain_data *cd;
	struct sge_txdesc *txd;
	int i;

	SGE_LOCK_ASSERT(sc);
	cd = &sc->sge_cdata;
	for (i = 0; i < SGE_TX_RING_CNT; i++) {
		txd = &cd->sge_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(cd->sge_txmbuf_tag, txd->tx_dmamap,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(cd->sge_txmbuf_tag, txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
			txd->tx_ndesc = 0;
		}
	}

	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them.  Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * has RING_END flag set.
 */
static int
sge_list_rx_init(struct sge_softc *sc)
{
	struct sge_chain_data *cd;
	int i;

	SGE_LOCK_ASSERT(sc);
	cd = &sc->sge_cdata;
	cd->sge_rx_cons = 0;
	bzero(sc->sge_ldata.sge_rx_ring, SGE_RX_RING_SZ);
	for (i = 0; i < SGE_RX_RING_CNT; i++) {
		if (sge_newbuf(sc, i) != 0)
			return (ENOBUFS);
	}
	bus_dmamap_sync(cd->sge_rx_tag, cd->sge_rx_dmamap,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	return (0);
}

static int
sge_list_rx_free(struct sge_softc *sc)
{
	struct sge_chain_data *cd;
	struct sge_rxdesc *rxd;
	int i;

	SGE_LOCK_ASSERT(sc);
	cd = &sc->sge_cdata;
	for (i = 0; i < SGE_RX_RING_CNT; i++) {
		rxd = &cd->sge_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(cd->sge_rxmbuf_tag, rxd->rx_dmamap,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(cd->sge_rxmbuf_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
sge_newbuf(struct sge_softc *sc, int prod)
{
	struct mbuf *m;
	struct sge_desc *desc;
	struct sge_chain_data *cd;
	struct sge_rxdesc *rxd;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int error, nsegs;

	SGE_LOCK_ASSERT(sc);

	cd = &sc->sge_cdata;
	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, SGE_RX_BUF_ALIGN);
	error = bus_dmamap_load_mbuf_sg(cd->sge_rxmbuf_tag,
	    cd->sge_rx_spare_map, m, segs, &nsegs, 0);
	if (error != 0) {
		m_freem(m);
		return (error);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));
	rxd = &cd->sge_rxdesc[prod];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(cd->sge_rxmbuf_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(cd->sge_rxmbuf_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = cd->sge_rx_spare_map;
	cd->sge_rx_spare_map = map;
	bus_dmamap_sync(cd->sge_rxmbuf_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;

	desc = &sc->sge_ldata.sge_rx_ring[prod];
	desc->sge_sts_size = 0;
	desc->sge_ptr = htole32(SGE_ADDR_LO(segs[0].ds_addr));
	desc->sge_flags = htole32(segs[0].ds_len);
	if (prod == SGE_RX_RING_CNT - 1)
		desc->sge_flags |= htole32(RING_END);
	desc->sge_cmdsts = htole32(RDC_OWN | RDC_INTR);
	return (0);
}

static __inline void
sge_discard_rxbuf(struct sge_softc *sc, int index)
{
	struct sge_desc *desc;

	desc = &sc->sge_ldata.sge_rx_ring[index];
	desc->sge_sts_size = 0;
	desc->sge_flags = htole32(MCLBYTES - SGE_RX_BUF_ALIGN);
	if (index == SGE_RX_RING_CNT - 1)
		desc->sge_flags |= htole32(RING_END);
	desc->sge_cmdsts = htole32(RDC_OWN | RDC_INTR);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static void
sge_rxeof(struct sge_softc *sc)
{
        struct ifnet *ifp;
        struct mbuf *m;
	struct sge_chain_data *cd;
	struct sge_desc	*cur_rx;
	uint32_t rxinfo, rxstat;
	int cons, prog;

	SGE_LOCK_ASSERT(sc);

	ifp = sc->sge_ifp;
	cd = &sc->sge_cdata;

	bus_dmamap_sync(cd->sge_rx_tag, cd->sge_rx_dmamap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	cons = cd->sge_rx_cons;
	for (prog = 0; prog < SGE_RX_RING_CNT; prog++,
	    SGE_INC(cons, SGE_RX_RING_CNT)) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
		cur_rx = &sc->sge_ldata.sge_rx_ring[cons];
		rxinfo = le32toh(cur_rx->sge_cmdsts);
		if ((rxinfo & RDC_OWN) != 0)
			break;
		rxstat = le32toh(cur_rx->sge_sts_size);
		if ((rxstat & RDS_CRCOK) == 0 || SGE_RX_ERROR(rxstat) != 0 ||
		    SGE_RX_NSEGS(rxstat) != 1) {
			/* XXX We don't support multi-segment frames yet. */
#ifdef SGE_SHOW_ERRORS
			device_printf(sc->sge_dev, "Rx error : 0x%b\n", rxstat,
			    RX_ERR_BITS);
#endif
			sge_discard_rxbuf(sc, cons);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			continue;
		}
		m = cd->sge_rxdesc[cons].rx_m;
		if (sge_newbuf(sc, cons) != 0) {
			sge_discard_rxbuf(sc, cons);
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			continue;
		}
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {
			if ((rxinfo & RDC_IP_CSUM) != 0 &&
			    (rxinfo & RDC_IP_CSUM_OK) != 0)
				m->m_pkthdr.csum_flags |=
				    CSUM_IP_CHECKED | CSUM_IP_VALID;
			if (((rxinfo & RDC_TCP_CSUM) != 0 &&
			    (rxinfo & RDC_TCP_CSUM_OK) != 0) ||
			    ((rxinfo & RDC_UDP_CSUM) != 0 &&
			    (rxinfo & RDC_UDP_CSUM_OK) != 0)) {
				m->m_pkthdr.csum_flags |=
				    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}
		/* Check for VLAN tagged frame. */
		if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (rxstat & RDS_VLAN) != 0) {
			m->m_pkthdr.ether_vtag = rxinfo & RDC_VLAN_MASK;
			m->m_flags |= M_VLANTAG;
		}
		/*
		 * Account for 10bytes auto padding which is used
		 * to align IP header on 32bit boundary.  Also note,
		 * CRC bytes is automatically removed by the
		 * hardware.
		 */
		m->m_data += SGE_RX_PAD_BYTES;
		m->m_pkthdr.len = m->m_len = SGE_RX_BYTES(rxstat) -
		    SGE_RX_PAD_BYTES;
		m->m_pkthdr.rcvif = ifp;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		SGE_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		SGE_LOCK(sc);
	}

	if (prog > 0) {
		bus_dmamap_sync(cd->sge_rx_tag, cd->sge_rx_dmamap,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		cd->sge_rx_cons = cons;
	}
}

/*
 * A frame was downloaded to the chip.  It's safe for us to clean up
 * the list buffers.
 */
static void
sge_txeof(struct sge_softc *sc)
{
	struct ifnet *ifp;
	struct sge_list_data *ld;
	struct sge_chain_data *cd;
	struct sge_txdesc *txd;
	uint32_t txstat;
	int cons, nsegs, prod;

	SGE_LOCK_ASSERT(sc);

	ifp = sc->sge_ifp;
	ld = &sc->sge_ldata;
	cd = &sc->sge_cdata;

	if (cd->sge_tx_cnt == 0)
		return;
	bus_dmamap_sync(cd->sge_tx_tag, cd->sge_tx_dmamap,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	cons = cd->sge_tx_cons;
	prod = cd->sge_tx_prod;
	for (; cons != prod;) {
		txstat = le32toh(ld->sge_tx_ring[cons].sge_cmdsts);
		if ((txstat & TDC_OWN) != 0)
			break;
		/*
		 * Only the first descriptor of multi-descriptor transmission
		 * is updated by controller.  Driver should skip entire
		 * chained buffers for the transmitted frame. In other words
		 * TDC_OWN bit is valid only at the first descriptor of a
		 * multi-descriptor transmission.
		 */
		if (SGE_TX_ERROR(txstat) != 0) {
#ifdef SGE_SHOW_ERRORS
			device_printf(sc->sge_dev, "Tx error : 0x%b\n",
			    txstat, TX_ERR_BITS);
#endif
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		} else {
#ifdef notyet
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (txstat & 0xFFFF) - 1);
#endif
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		}
		txd = &cd->sge_txdesc[cons];
		for (nsegs = 0; nsegs < txd->tx_ndesc; nsegs++) {
			ld->sge_tx_ring[cons].sge_cmdsts = 0;
			SGE_INC(cons, SGE_TX_RING_CNT);
		}
		/* Reclaim transmitted mbuf. */
		KASSERT(txd->tx_m != NULL,
		    ("%s: freeing NULL mbuf\n", __func__));
		bus_dmamap_sync(cd->sge_txmbuf_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(cd->sge_txmbuf_tag, txd->tx_dmamap);
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
		cd->sge_tx_cnt -= txd->tx_ndesc;
		KASSERT(cd->sge_tx_cnt >= 0,
		    ("%s: Active Tx desc counter was garbled\n", __func__));
		txd->tx_ndesc = 0;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	}
	cd->sge_tx_cons = cons;
	if (cd->sge_tx_cnt == 0)
		sc->sge_timer = 0;
}

static void
sge_tick(void *arg)
{
	struct sge_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;

	sc = arg;
	SGE_LOCK_ASSERT(sc);

	ifp = sc->sge_ifp;
	mii = device_get_softc(sc->sge_miibus);
	mii_tick(mii);
	if ((sc->sge_flags & SGE_FLAG_LINK) == 0) {
		sge_miibus_statchg(sc->sge_dev);
		if ((sc->sge_flags & SGE_FLAG_LINK) != 0 &&
		    !IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			sge_start_locked(ifp);
	}
	/*
	 * Reclaim transmitted frames here as we do not request
	 * Tx completion interrupt for every queued frames to
	 * reduce excessive interrupts.
	 */
	sge_txeof(sc);
	sge_watchdog(sc);
	callout_reset(&sc->sge_stat_ch, hz, sge_tick, sc);
}

static void
sge_intr(void *arg)
{
	struct sge_softc *sc;
	struct ifnet *ifp;
	uint32_t status;

	sc = arg;
	SGE_LOCK(sc);
	ifp = sc->sge_ifp;

	status = CSR_READ_4(sc, IntrStatus);
	if (status == 0xFFFFFFFF || (status & SGE_INTRS) == 0) {
		/* Not ours. */
		SGE_UNLOCK(sc);
		return;
	}
	/* Acknowledge interrupts. */
	CSR_WRITE_4(sc, IntrStatus, status);
	/* Disable further interrupts. */
	CSR_WRITE_4(sc, IntrMask, 0);
	/*
	 * It seems the controller supports some kind of interrupt
	 * moderation mechanism but we still don't know how to
	 * enable that.  To reduce number of generated interrupts
	 * under load we check pending interrupts in a loop.  This
	 * will increase number of register access and is not correct
	 * way to handle interrupt moderation but there seems to be
	 * no other way at this time.
	 */
	for (;;) {
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
			break;
		if ((status & (INTR_RX_DONE | INTR_RX_IDLE)) != 0) {
			sge_rxeof(sc);
			/* Wakeup Rx MAC. */
			if ((status & INTR_RX_IDLE) != 0)
				CSR_WRITE_4(sc, RX_CTL,
				    0x1a00 | 0x000c | RX_CTL_POLL | RX_CTL_ENB);
		}
		if ((status & (INTR_TX_DONE | INTR_TX_IDLE)) != 0)
			sge_txeof(sc);
		status = CSR_READ_4(sc, IntrStatus);
		if ((status & SGE_INTRS) == 0)
			break;
		/* Acknowledge interrupts. */
		CSR_WRITE_4(sc, IntrStatus, status);
	}
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		/* Re-enable interrupts */
		CSR_WRITE_4(sc, IntrMask, SGE_INTRS);
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			sge_start_locked(ifp);
	}
	SGE_UNLOCK(sc);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
sge_encap(struct sge_softc *sc, struct mbuf **m_head)
{
	struct mbuf *m;
	struct sge_desc *desc;
	struct sge_txdesc *txd;
	bus_dma_segment_t txsegs[SGE_MAXTXSEGS];
	uint32_t cflags, mss;
	int error, i, nsegs, prod, si;

	SGE_LOCK_ASSERT(sc);

	si = prod = sc->sge_cdata.sge_tx_prod;
	txd = &sc->sge_cdata.sge_txdesc[prod];
	if (((*m_head)->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		struct ether_header *eh;
		struct ip *ip;
		struct tcphdr *tcp;
		uint32_t ip_off, poff;

		if (M_WRITABLE(*m_head) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		ip_off = sizeof(struct ether_header);
		m = m_pullup(*m_head, ip_off);
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		eh = mtod(m, struct ether_header *);
		/* Check the existence of VLAN tag. */
		if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
			ip_off = sizeof(struct ether_vlan_header);
			m = m_pullup(m, ip_off);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
		}
		m = m_pullup(m, ip_off + sizeof(struct ip));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		ip = (struct ip *)(mtod(m, char *) + ip_off);
		poff = ip_off + (ip->ip_hl << 2);
		m = m_pullup(m, poff + sizeof(struct tcphdr));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		tcp = (struct tcphdr *)(mtod(m, char *) + poff);
		m = m_pullup(m, poff + (tcp->th_off << 2));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		/*
		 * Reset IP checksum and recompute TCP pseudo
		 * checksum that NDIS specification requires.
		 */
		ip = (struct ip *)(mtod(m, char *) + ip_off);
		ip->ip_sum = 0;
		tcp = (struct tcphdr *)(mtod(m, char *) + poff);
		tcp->th_sum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr,
		    htons(IPPROTO_TCP));
		*m_head = m;
	}

	error = bus_dmamap_load_mbuf_sg(sc->sge_cdata.sge_txmbuf_tag,
	    txd->tx_dmamap, *m_head, txsegs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, SGE_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->sge_cdata.sge_txmbuf_tag,
		    txd->tx_dmamap, *m_head, txsegs, &nsegs, 0);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);

	KASSERT(nsegs != 0, ("zero segment returned"));
	/* Check descriptor overrun. */
	if (sc->sge_cdata.sge_tx_cnt + nsegs >= SGE_TX_RING_CNT) {
		bus_dmamap_unload(sc->sge_cdata.sge_txmbuf_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}
	bus_dmamap_sync(sc->sge_cdata.sge_txmbuf_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	m = *m_head;
	cflags = 0;
	mss = 0;
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		cflags |= TDC_LS;
		mss = (uint32_t)m->m_pkthdr.tso_segsz;
		mss <<= 16;
	} else {
		if (m->m_pkthdr.csum_flags & CSUM_IP)
			cflags |= TDC_IP_CSUM;
		if (m->m_pkthdr.csum_flags & CSUM_TCP)
			cflags |= TDC_TCP_CSUM;
		if (m->m_pkthdr.csum_flags & CSUM_UDP)
			cflags |= TDC_UDP_CSUM;
	}
	for (i = 0; i < nsegs; i++) {
		desc = &sc->sge_ldata.sge_tx_ring[prod];
		if (i == 0) {
			desc->sge_sts_size = htole32(m->m_pkthdr.len | mss);
			desc->sge_cmdsts = 0;
		} else {
			desc->sge_sts_size = 0;
			desc->sge_cmdsts = htole32(TDC_OWN);
		}
		desc->sge_ptr = htole32(SGE_ADDR_LO(txsegs[i].ds_addr));
		desc->sge_flags = htole32(txsegs[i].ds_len);
		if (prod == SGE_TX_RING_CNT - 1)
			desc->sge_flags |= htole32(RING_END);
		sc->sge_cdata.sge_tx_cnt++;
		SGE_INC(prod, SGE_TX_RING_CNT);
	}
	/* Update producer index. */
	sc->sge_cdata.sge_tx_prod = prod;

	desc = &sc->sge_ldata.sge_tx_ring[si];
	/* Configure VLAN. */
	if((m->m_flags & M_VLANTAG) != 0) {
		cflags |= m->m_pkthdr.ether_vtag;
		desc->sge_sts_size |= htole32(TDS_INS_VLAN);
	}
	desc->sge_cmdsts |= htole32(TDC_DEF | TDC_CRC | TDC_PAD | cflags);
#if 1
	if ((sc->sge_flags & SGE_FLAG_SPEED_1000) != 0)
		desc->sge_cmdsts |= htole32(TDC_BST);
#else
	if ((sc->sge_flags & SGE_FLAG_FDX) == 0) {
		desc->sge_cmdsts |= htole32(TDC_COL | TDC_CRS | TDC_BKF);
		if ((sc->sge_flags & SGE_FLAG_SPEED_1000) != 0)
			desc->sge_cmdsts |= htole32(TDC_EXT | TDC_BST);
	}
#endif
	/* Request interrupt and give ownership to controller. */
	desc->sge_cmdsts |= htole32(TDC_OWN | TDC_INTR);
	txd->tx_m = m;
	txd->tx_ndesc = nsegs;
	return (0);
}

static void
sge_start(struct ifnet *ifp)
{
	struct sge_softc *sc;

	sc = ifp->if_softc;
	SGE_LOCK(sc);
	sge_start_locked(ifp);
	SGE_UNLOCK(sc);
}

static void
sge_start_locked(struct ifnet *ifp)
{
	struct sge_softc *sc;
	struct mbuf *m_head;
	int queued = 0;

	sc = ifp->if_softc;
	SGE_LOCK_ASSERT(sc);

	if ((sc->sge_flags & SGE_FLAG_LINK) == 0 ||
	    (ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return;

	for (queued = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		if (sc->sge_cdata.sge_tx_cnt > (SGE_TX_RING_CNT -
		    SGE_MAXTXSEGS)) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		if (sge_encap(sc, &m_head)) {
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

	if (queued > 0) {
		bus_dmamap_sync(sc->sge_cdata.sge_tx_tag,
		    sc->sge_cdata.sge_tx_dmamap,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		CSR_WRITE_4(sc, TX_CTL, 0x1a00 | TX_CTL_ENB | TX_CTL_POLL);
		sc->sge_timer = 5;
	}
}

static void
sge_init(void *arg)
{
	struct sge_softc *sc;

	sc = arg;
	SGE_LOCK(sc);
	sge_init_locked(sc);
	SGE_UNLOCK(sc);
}

static void
sge_init_locked(struct sge_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint16_t rxfilt;
	int i;

	SGE_LOCK_ASSERT(sc);
	ifp = sc->sge_ifp;
	mii = device_get_softc(sc->sge_miibus);
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;
	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	sge_stop(sc);
	sge_reset(sc);

	/* Init circular RX list. */
	if (sge_list_rx_init(sc) == ENOBUFS) {
		device_printf(sc->sge_dev, "no memory for Rx buffers\n");
		sge_stop(sc);
		return;
	}
	/* Init TX descriptors. */
	sge_list_tx_init(sc);
	/*
	 * Load the address of the RX and TX lists.
	 */
	CSR_WRITE_4(sc, TX_DESC, SGE_ADDR_LO(sc->sge_ldata.sge_tx_paddr));
	CSR_WRITE_4(sc, RX_DESC, SGE_ADDR_LO(sc->sge_ldata.sge_rx_paddr));

	CSR_WRITE_4(sc, TxMacControl, 0x60);
	CSR_WRITE_4(sc, RxWakeOnLan, 0);
	CSR_WRITE_4(sc, RxWakeOnLanData, 0);
	/* Allow receiving VLAN frames. */
	CSR_WRITE_2(sc, RxMPSControl, ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN +
	    SGE_RX_PAD_BYTES);

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, RxMacAddr + i, IF_LLADDR(ifp)[i]);
	/* Configure RX MAC. */
	rxfilt = RXMAC_STRIP_FCS | RXMAC_PAD_ENB | RXMAC_CSUM_ENB;
	CSR_WRITE_2(sc, RxMacControl, rxfilt);
	sge_rxfilter(sc);
	sge_setvlan(sc);

	/* Initialize default speed/duplex information. */
	if ((sc->sge_flags & SGE_FLAG_FASTETHER) == 0)
		sc->sge_flags |= SGE_FLAG_SPEED_1000;
	sc->sge_flags |= SGE_FLAG_FDX;
	if ((sc->sge_flags & SGE_FLAG_RGMII) != 0)
		CSR_WRITE_4(sc, StationControl, 0x04008001);
	else
		CSR_WRITE_4(sc, StationControl, 0x04000001);
	/*
	 * XXX Try to mitigate interrupts.
	 */
	CSR_WRITE_4(sc, IntrControl, 0x08880000);
#ifdef notyet
	if (sc->sge_intrcontrol != 0)
		CSR_WRITE_4(sc, IntrControl, sc->sge_intrcontrol);
	if (sc->sge_intrtimer != 0)
		CSR_WRITE_4(sc, IntrTimer, sc->sge_intrtimer);
#endif

	/*
	 * Clear and enable interrupts.
	 */
	CSR_WRITE_4(sc, IntrStatus, 0xFFFFFFFF);
	CSR_WRITE_4(sc, IntrMask, SGE_INTRS);

	/* Enable receiver and transmitter. */
	CSR_WRITE_4(sc, TX_CTL, 0x1a00 | TX_CTL_ENB);
	CSR_WRITE_4(sc, RX_CTL, 0x1a00 | 0x000c | RX_CTL_POLL | RX_CTL_ENB);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc->sge_flags &= ~SGE_FLAG_LINK;
	mii_mediachg(mii);
	callout_reset(&sc->sge_stat_ch, hz, sge_tick, sc);
}

/*
 * Set media options.
 */
static int
sge_ifmedia_upd(struct ifnet *ifp)
{
	struct sge_softc *sc;
	struct mii_data *mii;
		struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;
	SGE_LOCK(sc);
	mii = device_get_softc(sc->sge_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	SGE_UNLOCK(sc);

	return (error);
}

/*
 * Report current media status.
 */
static void
sge_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sge_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	SGE_LOCK(sc);
	mii = device_get_softc(sc->sge_miibus);
	if ((ifp->if_flags & IFF_UP) == 0) {
		SGE_UNLOCK(sc);
		return;
	}
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	SGE_UNLOCK(sc);
}

static int
sge_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct sge_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	int error = 0, mask, reinit;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;

	switch(command) {
	case SIOCSIFFLAGS:
		SGE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc->sge_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) != 0)
				sge_rxfilter(sc);
			else
				sge_init_locked(sc);
		} else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			sge_stop(sc);
		sc->sge_if_flags = ifp->if_flags;
		SGE_UNLOCK(sc);
		break;
	case SIOCSIFCAP:
		SGE_LOCK(sc);
		reinit = 0;
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_TXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= SGE_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~SGE_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_RXCSUM) != 0)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWCSUM) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((mask & IFCAP_TSO4) != 0 &&
		    (ifp->if_capabilities & IFCAP_TSO4) != 0) {
			ifp->if_capenable ^= IFCAP_TSO4;
			if ((ifp->if_capenable & IFCAP_TSO4) != 0)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}
		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTSO) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0) {
			/*
			 * Due to unknown reason, toggling VLAN hardware
			 * tagging require interface reinitialization.
			 */
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) == 0)
				ifp->if_capenable &=
				    ~(IFCAP_VLAN_HWTSO | IFCAP_VLAN_HWCSUM);
			reinit = 1;
		}
		if (reinit > 0 && (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			sge_init_locked(sc);
		}
		SGE_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		SGE_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			sge_rxfilter(sc);
		SGE_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->sge_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
sge_watchdog(struct sge_softc *sc)
{
	struct ifnet *ifp;

	SGE_LOCK_ASSERT(sc);
	if (sc->sge_timer == 0 || --sc->sge_timer > 0)
		return;

	ifp = sc->sge_ifp;
	if ((sc->sge_flags & SGE_FLAG_LINK) == 0) {
		if (1 || bootverbose)
			device_printf(sc->sge_dev,
			    "watchdog timeout (lost link)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		sge_init_locked(sc);
		return;
	}
	device_printf(sc->sge_dev, "watchdog timeout\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	sge_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&sc->sge_ifp->if_snd))
		sge_start_locked(ifp);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
sge_stop(struct sge_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->sge_ifp;

	SGE_LOCK_ASSERT(sc);

	sc->sge_timer = 0;
	callout_stop(&sc->sge_stat_ch);
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_READ_4(sc, IntrMask);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);
	/* Stop TX/RX MAC. */
	CSR_WRITE_4(sc, TX_CTL, 0x1a00);
	CSR_WRITE_4(sc, RX_CTL, 0x1a00);
	/* XXX Can we assume active DMA cycles gone? */
	DELAY(2000);
	CSR_WRITE_4(sc, IntrMask, 0);
	CSR_WRITE_4(sc, IntrStatus, 0xffffffff);

	sc->sge_flags &= ~SGE_FLAG_LINK;
	sge_list_rx_free(sc);
	sge_list_tx_free(sc);
}
