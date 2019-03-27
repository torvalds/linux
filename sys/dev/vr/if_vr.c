/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998
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
 * VIA Rhine fast ethernet PCI NIC driver
 *
 * Supports various network adapters based on the VIA Rhine
 * and Rhine II PCI controllers, including the D-Link DFE530TX.
 * Datasheets are available at http://www.via.com.tw.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The VIA Rhine controllers are similar in some respects to the
 * the DEC tulip chips, except less complicated. The controller
 * uses an MII bus and an external physical layer interface. The
 * receiver has a one entry perfect filter and a 64-bit hash table
 * multicast filter. Transmit and receive descriptors are similar
 * to the tulip.
 *
 * Some Rhine chips has a serious flaw in its transmit DMA mechanism:
 * transmit buffers must be longword aligned. Unfortunately,
 * FreeBSD doesn't guarantee that mbufs will be filled in starting
 * at longword boundaries, so we have to do a buffer copy before
 * transmission.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

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
#include <sys/taskqueue.h>

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

#include <dev/vr/if_vrreg.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

MODULE_DEPEND(vr, pci, 1, 1, 1);
MODULE_DEPEND(vr, ether, 1, 1, 1);
MODULE_DEPEND(vr, miibus, 1, 1, 1);

/* Define to show Rx/Tx error status. */
#undef	VR_SHOW_ERRORS
#define	VR_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

/*
 * Various supported device vendors/types, their names & quirks.
 */
#define VR_Q_NEEDALIGN		(1<<0)
#define VR_Q_CSUM		(1<<1)
#define VR_Q_CAM		(1<<2)

static const struct vr_type {
	u_int16_t		vr_vid;
	u_int16_t		vr_did;
	int			vr_quirks;
	const char		*vr_name;
} vr_devs[] = {
	{ VIA_VENDORID, VIA_DEVICEID_RHINE,
	    VR_Q_NEEDALIGN,
	    "VIA VT3043 Rhine I 10/100BaseTX" },
	{ VIA_VENDORID, VIA_DEVICEID_RHINE_II,
	    VR_Q_NEEDALIGN,
	    "VIA VT86C100A Rhine II 10/100BaseTX" },
	{ VIA_VENDORID, VIA_DEVICEID_RHINE_II_2,
	    0,
	    "VIA VT6102 Rhine II 10/100BaseTX" },
	{ VIA_VENDORID, VIA_DEVICEID_RHINE_III,
	    0,
	    "VIA VT6105 Rhine III 10/100BaseTX" },
	{ VIA_VENDORID, VIA_DEVICEID_RHINE_III_M,
	    VR_Q_CSUM,
	    "VIA VT6105M Rhine III 10/100BaseTX" },
	{ DELTA_VENDORID, DELTA_DEVICEID_RHINE_II,
	    VR_Q_NEEDALIGN,
	    "Delta Electronics Rhine II 10/100BaseTX" },
	{ ADDTRON_VENDORID, ADDTRON_DEVICEID_RHINE_II,
	    VR_Q_NEEDALIGN,
	    "Addtron Technology Rhine II 10/100BaseTX" },
	{ 0, 0, 0, NULL }
};

static int vr_probe(device_t);
static int vr_attach(device_t);
static int vr_detach(device_t);
static int vr_shutdown(device_t);
static int vr_suspend(device_t);
static int vr_resume(device_t);

static void vr_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int vr_dma_alloc(struct vr_softc *);
static void vr_dma_free(struct vr_softc *);
static __inline void vr_discard_rxbuf(struct vr_rxdesc *);
static int vr_newbuf(struct vr_softc *, int);

#ifndef __NO_STRICT_ALIGNMENT
static __inline void vr_fixup_rx(struct mbuf *);
#endif
static int vr_rxeof(struct vr_softc *);
static void vr_txeof(struct vr_softc *);
static void vr_tick(void *);
static int vr_error(struct vr_softc *, uint16_t);
static void vr_tx_underrun(struct vr_softc *);
static int vr_intr(void *);
static void vr_int_task(void *, int);
static void vr_start(struct ifnet *);
static void vr_start_locked(struct ifnet *);
static int vr_encap(struct vr_softc *, struct mbuf **);
static int vr_ioctl(struct ifnet *, u_long, caddr_t);
static void vr_init(void *);
static void vr_init_locked(struct vr_softc *);
static void vr_tx_start(struct vr_softc *);
static void vr_rx_start(struct vr_softc *);
static int vr_tx_stop(struct vr_softc *);
static int vr_rx_stop(struct vr_softc *);
static void vr_stop(struct vr_softc *);
static void vr_watchdog(struct vr_softc *);
static int vr_ifmedia_upd(struct ifnet *);
static void vr_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int vr_miibus_readreg(device_t, int, int);
static int vr_miibus_writereg(device_t, int, int, int);
static void vr_miibus_statchg(device_t);

static void vr_cam_mask(struct vr_softc *, uint32_t, int);
static int vr_cam_data(struct vr_softc *, int, int, uint8_t *);
static void vr_set_filter(struct vr_softc *);
static void vr_reset(const struct vr_softc *);
static int vr_tx_ring_init(struct vr_softc *);
static int vr_rx_ring_init(struct vr_softc *);
static void vr_setwol(struct vr_softc *);
static void vr_clrwol(struct vr_softc *);
static int vr_sysctl_stats(SYSCTL_HANDLER_ARGS);

static const struct vr_tx_threshold_table {
	int tx_cfg;
	int bcr_cfg;
	int value;
} vr_tx_threshold_tables[] = {
	{ VR_TXTHRESH_64BYTES, VR_BCR1_TXTHRESH64BYTES,	64 },
	{ VR_TXTHRESH_128BYTES, VR_BCR1_TXTHRESH128BYTES, 128 },
	{ VR_TXTHRESH_256BYTES, VR_BCR1_TXTHRESH256BYTES, 256 },
	{ VR_TXTHRESH_512BYTES, VR_BCR1_TXTHRESH512BYTES, 512 },
	{ VR_TXTHRESH_1024BYTES, VR_BCR1_TXTHRESH1024BYTES, 1024 },
	{ VR_TXTHRESH_STORENFWD, VR_BCR1_TXTHRESHSTORENFWD, 2048 }
};

static device_method_t vr_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		vr_probe),
	DEVMETHOD(device_attach,	vr_attach),
	DEVMETHOD(device_detach, 	vr_detach),
	DEVMETHOD(device_shutdown,	vr_shutdown),
	DEVMETHOD(device_suspend,	vr_suspend),
	DEVMETHOD(device_resume,	vr_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	vr_miibus_readreg),
	DEVMETHOD(miibus_writereg,	vr_miibus_writereg),
	DEVMETHOD(miibus_statchg,	vr_miibus_statchg),

	DEVMETHOD_END
};

static driver_t vr_driver = {
	"vr",
	vr_methods,
	sizeof(struct vr_softc)
};

static devclass_t vr_devclass;

DRIVER_MODULE(vr, pci, vr_driver, vr_devclass, 0, 0);
DRIVER_MODULE(miibus, vr, miibus_driver, miibus_devclass, 0, 0);

static int
vr_miibus_readreg(device_t dev, int phy, int reg)
{
	struct vr_softc		*sc;
	int			i;

	sc = device_get_softc(dev);

	/* Set the register address. */
	CSR_WRITE_1(sc, VR_MIIADDR, reg);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_READ_ENB);

	for (i = 0; i < VR_MII_TIMEOUT; i++) {
		DELAY(1);
		if ((CSR_READ_1(sc, VR_MIICMD) & VR_MIICMD_READ_ENB) == 0)
			break;
	}
	if (i == VR_MII_TIMEOUT)
		device_printf(sc->vr_dev, "phy read timeout %d:%d\n", phy, reg);

	return (CSR_READ_2(sc, VR_MIIDATA));
}

static int
vr_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct vr_softc		*sc;
	int			i;

	sc = device_get_softc(dev);

	/* Set the register address and data to write. */
	CSR_WRITE_1(sc, VR_MIIADDR, reg);
	CSR_WRITE_2(sc, VR_MIIDATA, data);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_WRITE_ENB);

	for (i = 0; i < VR_MII_TIMEOUT; i++) {
		DELAY(1);
		if ((CSR_READ_1(sc, VR_MIICMD) & VR_MIICMD_WRITE_ENB) == 0)
			break;
	}
	if (i == VR_MII_TIMEOUT)
		device_printf(sc->vr_dev, "phy write timeout %d:%d\n", phy,
		    reg);

	return (0);
}

/*
 * In order to fiddle with the
 * 'full-duplex' and '100Mbps' bits in the netconfig register, we
 * first have to put the transmit and/or receive logic in the idle state.
 */
static void
vr_miibus_statchg(device_t dev)
{
	struct vr_softc		*sc;
	struct mii_data		*mii;
	struct ifnet		*ifp;
	int			lfdx, mfdx;
	uint8_t			cr0, cr1, fc;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->vr_miibus);
	ifp = sc->vr_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->vr_flags &= ~(VR_F_LINK | VR_F_TXPAUSE);
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->vr_flags |= VR_F_LINK;
			break;
		default:
			break;
		}
	}

	if ((sc->vr_flags & VR_F_LINK) != 0) {
		cr0 = CSR_READ_1(sc, VR_CR0);
		cr1 = CSR_READ_1(sc, VR_CR1);
		mfdx = (cr1 & VR_CR1_FULLDUPLEX) != 0;
		lfdx = (IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0;
		if (mfdx != lfdx) {
			if ((cr0 & (VR_CR0_TX_ON | VR_CR0_RX_ON)) != 0) {
				if (vr_tx_stop(sc) != 0 ||
				    vr_rx_stop(sc) != 0) {
					device_printf(sc->vr_dev,
					    "%s: Tx/Rx shutdown error -- "
					    "resetting\n", __func__);
					sc->vr_flags |= VR_F_RESTART;
					VR_UNLOCK(sc);
					return;
				}
			}
			if (lfdx)
				cr1 |= VR_CR1_FULLDUPLEX;
			else
				cr1 &= ~VR_CR1_FULLDUPLEX;
			CSR_WRITE_1(sc, VR_CR1, cr1);
		}
		fc = 0;
		/* Configure flow-control. */
		if (sc->vr_revid >= REV_ID_VT6105_A0) {
			fc = CSR_READ_1(sc, VR_FLOWCR1);
			fc &= ~(VR_FLOWCR1_TXPAUSE | VR_FLOWCR1_RXPAUSE);
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_RXPAUSE) != 0)
				fc |= VR_FLOWCR1_RXPAUSE;
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_TXPAUSE) != 0) {
				fc |= VR_FLOWCR1_TXPAUSE;
				sc->vr_flags |= VR_F_TXPAUSE;
			}
			CSR_WRITE_1(sc, VR_FLOWCR1, fc);
		} else if (sc->vr_revid >= REV_ID_VT6102_A) {
			/* No Tx puase capability available for Rhine II. */
			fc = CSR_READ_1(sc, VR_MISC_CR0);
			fc &= ~VR_MISCCR0_RXPAUSE;
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_RXPAUSE) != 0)
				fc |= VR_MISCCR0_RXPAUSE;
			CSR_WRITE_1(sc, VR_MISC_CR0, fc);
		}
		vr_rx_start(sc);
		vr_tx_start(sc);
	} else {
		if (vr_tx_stop(sc) != 0 || vr_rx_stop(sc) != 0) {
			device_printf(sc->vr_dev,
			    "%s: Tx/Rx shutdown error -- resetting\n",
			    __func__);
			sc->vr_flags |= VR_F_RESTART;
		}
	}
}


static void
vr_cam_mask(struct vr_softc *sc, uint32_t mask, int type)
{

	if (type == VR_MCAST_CAM)
		CSR_WRITE_1(sc, VR_CAMCTL, VR_CAMCTL_ENA | VR_CAMCTL_MCAST);
	else
		CSR_WRITE_1(sc, VR_CAMCTL, VR_CAMCTL_ENA | VR_CAMCTL_VLAN);
	CSR_WRITE_4(sc, VR_CAMMASK, mask);
	CSR_WRITE_1(sc, VR_CAMCTL, 0);
}

static int
vr_cam_data(struct vr_softc *sc, int type, int idx, uint8_t *mac)
{
	int	i;

	if (type == VR_MCAST_CAM) {
		if (idx < 0 || idx >= VR_CAM_MCAST_CNT || mac == NULL)
			return (EINVAL);
		CSR_WRITE_1(sc, VR_CAMCTL, VR_CAMCTL_ENA | VR_CAMCTL_MCAST);
	} else
		CSR_WRITE_1(sc, VR_CAMCTL, VR_CAMCTL_ENA | VR_CAMCTL_VLAN);

	/* Set CAM entry address. */
	CSR_WRITE_1(sc, VR_CAMADDR, idx);
	/* Set CAM entry data. */
	if (type == VR_MCAST_CAM) {
		for (i = 0; i < ETHER_ADDR_LEN; i++)
			CSR_WRITE_1(sc, VR_MCAM0 + i, mac[i]);
	} else {
		CSR_WRITE_1(sc, VR_VCAM0, mac[0]);
		CSR_WRITE_1(sc, VR_VCAM1, mac[1]);
	}
	DELAY(10);
	/* Write CAM and wait for self-clear of VR_CAMCTL_WRITE bit. */
	CSR_WRITE_1(sc, VR_CAMCTL, VR_CAMCTL_ENA | VR_CAMCTL_WRITE);
	for (i = 0; i < VR_TIMEOUT; i++) {
		DELAY(1);
		if ((CSR_READ_1(sc, VR_CAMCTL) & VR_CAMCTL_WRITE) == 0)
			break;
	}

	if (i == VR_TIMEOUT)
		device_printf(sc->vr_dev, "%s: setting CAM filter timeout!\n",
		    __func__);
	CSR_WRITE_1(sc, VR_CAMCTL, 0);

	return (i == VR_TIMEOUT ? ETIMEDOUT : 0);
}

/*
 * Program the 64-bit multicast hash filter.
 */
static void
vr_set_filter(struct vr_softc *sc)
{
	struct ifnet		*ifp;
	int			h;
	uint32_t		hashes[2] = { 0, 0 };
	struct ifmultiaddr	*ifma;
	uint8_t			rxfilt;
	int			error, mcnt;
	uint32_t		cam_mask;

	VR_LOCK_ASSERT(sc);

	ifp = sc->vr_ifp;
	rxfilt = CSR_READ_1(sc, VR_RXCFG);
	rxfilt &= ~(VR_RXCFG_RX_PROMISC | VR_RXCFG_RX_BROAD |
	    VR_RXCFG_RX_MULTI);
	if (ifp->if_flags & IFF_BROADCAST)
		rxfilt |= VR_RXCFG_RX_BROAD;
	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxfilt |= VR_RXCFG_RX_MULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= VR_RXCFG_RX_PROMISC;
		CSR_WRITE_1(sc, VR_RXCFG, rxfilt);
		CSR_WRITE_4(sc, VR_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, VR_MAR1, 0xFFFFFFFF);
		return;
	}

	/* Now program new ones. */
	error = 0;
	mcnt = 0;
	if_maddr_rlock(ifp);
	if ((sc->vr_quirks & VR_Q_CAM) != 0) {
		/*
		 * For hardwares that have CAM capability, use
		 * 32 entries multicast perfect filter.
		 */
		cam_mask = 0;
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			error = vr_cam_data(sc, VR_MCAST_CAM, mcnt,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			if (error != 0) {
				cam_mask = 0;
				break;
			}
			cam_mask |= 1 << mcnt;
			mcnt++;
		}
		vr_cam_mask(sc, VR_MCAST_CAM, cam_mask);
	}

	if ((sc->vr_quirks & VR_Q_CAM) == 0 || error != 0) {
		/*
		 * If there are too many multicast addresses or
		 * setting multicast CAM filter failed, use hash
		 * table based filtering.
		 */
		mcnt = 0;
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));
			mcnt++;
		}
	}
	if_maddr_runlock(ifp);

	if (mcnt > 0)
		rxfilt |= VR_RXCFG_RX_MULTI;

	CSR_WRITE_4(sc, VR_MAR0, hashes[0]);
	CSR_WRITE_4(sc, VR_MAR1, hashes[1]);
	CSR_WRITE_1(sc, VR_RXCFG, rxfilt);
}

static void
vr_reset(const struct vr_softc *sc)
{
	int		i;

	/*VR_LOCK_ASSERT(sc);*/ /* XXX: Called during attach w/o lock. */

	CSR_WRITE_1(sc, VR_CR1, VR_CR1_RESET);
	if (sc->vr_revid < REV_ID_VT6102_A) {
		/* VT86C100A needs more delay after reset. */
		DELAY(100);
	}
	for (i = 0; i < VR_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_1(sc, VR_CR1) & VR_CR1_RESET))
			break;
	}
	if (i == VR_TIMEOUT) {
		if (sc->vr_revid < REV_ID_VT6102_A)
			device_printf(sc->vr_dev, "reset never completed!\n");
		else {
			/* Use newer force reset command. */
			device_printf(sc->vr_dev,
			    "Using force reset command.\n");
			VR_SETBIT(sc, VR_MISC_CR1, VR_MISCCR1_FORSRST);
			/*
			 * Wait a little while for the chip to get its brains
			 * in order.
			 */
			DELAY(2000);
		}
	}

}

/*
 * Probe for a VIA Rhine chip. Check the PCI vendor and device
 * IDs against our list and return a match or NULL
 */
static const struct vr_type *
vr_match(device_t dev)
{
	const struct vr_type	*t = vr_devs;

	for (t = vr_devs; t->vr_name != NULL; t++)
		if ((pci_get_vendor(dev) == t->vr_vid) &&
		    (pci_get_device(dev) == t->vr_did))
			return (t);
	return (NULL);
}

/*
 * Probe for a VIA Rhine chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
static int
vr_probe(device_t dev)
{
	const struct vr_type	*t;

	t = vr_match(dev);
	if (t != NULL) {
		device_set_desc(dev, t->vr_name);
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
vr_attach(device_t dev)
{
	struct vr_softc		*sc;
	struct ifnet		*ifp;
	const struct vr_type	*t;
	uint8_t			eaddr[ETHER_ADDR_LEN];
	int			error, rid;
	int			i, phy, pmc;

	sc = device_get_softc(dev);
	sc->vr_dev = dev;
	t = vr_match(dev);
	KASSERT(t != NULL, ("Lost if_vr device match"));
	sc->vr_quirks = t->vr_quirks;
	device_printf(dev, "Quirks: 0x%x\n", sc->vr_quirks);

	mtx_init(&sc->vr_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->vr_stat_callout, &sc->vr_mtx, 0);
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "stats", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    vr_sysctl_stats, "I", "Statistics");

	error = 0;

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);
	sc->vr_revid = pci_get_revid(dev);
	device_printf(dev, "Revision: 0x%x\n", sc->vr_revid);

	sc->vr_res_id = PCIR_BAR(0);
	sc->vr_res_type = SYS_RES_IOPORT;
	sc->vr_res = bus_alloc_resource_any(dev, sc->vr_res_type,
	    &sc->vr_res_id, RF_ACTIVE);
	if (sc->vr_res == NULL) {
		device_printf(dev, "couldn't map ports\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate interrupt. */
	rid = 0;
	sc->vr_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->vr_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/* Allocate ifnet structure. */
	ifp = sc->vr_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "couldn't allocate ifnet structure\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = vr_ioctl;
	ifp->if_start = vr_start;
	ifp->if_init = vr_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, VR_TX_RING_CNT - 1);
	ifp->if_snd.ifq_maxlen = VR_TX_RING_CNT - 1;
	IFQ_SET_READY(&ifp->if_snd);

	TASK_INIT(&sc->vr_inttask, 0, vr_int_task, sc);

	/* Configure Tx FIFO threshold. */
	sc->vr_txthresh = VR_TXTHRESH_MIN;
	if (sc->vr_revid < REV_ID_VT6105_A0) {
		/*
		 * Use store and forward mode for Rhine I/II.
		 * Otherwise they produce a lot of Tx underruns and
		 * it would take a while to get working FIFO threshold
		 * value.
		 */
		sc->vr_txthresh = VR_TXTHRESH_MAX;
	}
	if ((sc->vr_quirks & VR_Q_CSUM) != 0) {
		ifp->if_hwassist = VR_CSUM_FEATURES;
		ifp->if_capabilities |= IFCAP_HWCSUM;
		/*
		 * To update checksum field the hardware may need to
		 * store entire frames into FIFO before transmitting.
		 */
		sc->vr_txthresh = VR_TXTHRESH_MAX;
	}

	if (sc->vr_revid >= REV_ID_VT6102_A &&
	    pci_find_cap(dev, PCIY_PMG, &pmc) == 0)
		ifp->if_capabilities |= IFCAP_WOL_UCAST | IFCAP_WOL_MAGIC;

	/* Rhine supports oversized VLAN frame. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif

	/*
	 * Windows may put the chip in suspend mode when it
	 * shuts down. Be sure to kick it in the head to wake it
	 * up again.
	 */
	if (pci_find_cap(dev, PCIY_PMG, &pmc) == 0)
		VR_CLRBIT(sc, VR_STICKHW, (VR_STICKHW_DS0|VR_STICKHW_DS1));

	/*
	 * Get station address. The way the Rhine chips work,
	 * you're not allowed to directly access the EEPROM once
	 * they've been programmed a special way. Consequently,
	 * we need to read the node address from the PAR0 and PAR1
	 * registers.
	 * Reloading EEPROM also overwrites VR_CFGA, VR_CFGB,
	 * VR_CFGC and VR_CFGD such that memory mapped IO configured
	 * by driver is reset to default state.
	 */
	VR_SETBIT(sc, VR_EECSR, VR_EECSR_LOAD);
	for (i = VR_TIMEOUT; i > 0; i--) {
		DELAY(1);
		if ((CSR_READ_1(sc, VR_EECSR) & VR_EECSR_LOAD) == 0)
			break;
	}
	if (i == 0)
		device_printf(dev, "Reloading EEPROM timeout!\n");
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		eaddr[i] = CSR_READ_1(sc, VR_PAR0 + i);

	/* Reset the adapter. */
	vr_reset(sc);
	/* Ack intr & disable further interrupts. */
	CSR_WRITE_2(sc, VR_ISR, 0xFFFF);
	CSR_WRITE_2(sc, VR_IMR, 0);
	if (sc->vr_revid >= REV_ID_VT6102_A)
		CSR_WRITE_2(sc, VR_MII_IMR, 0);

	if (sc->vr_revid < REV_ID_VT6102_A) {
		pci_write_config(dev, VR_PCI_MODE2,
		    pci_read_config(dev, VR_PCI_MODE2, 1) |
		    VR_MODE2_MODE10T, 1);
	} else {
		/* Report error instead of retrying forever. */
		pci_write_config(dev, VR_PCI_MODE2,
		    pci_read_config(dev, VR_PCI_MODE2, 1) |
		    VR_MODE2_PCEROPT, 1);
        	/* Detect MII coding error. */
		pci_write_config(dev, VR_PCI_MODE3,
		    pci_read_config(dev, VR_PCI_MODE3, 1) |
		    VR_MODE3_MIION, 1);
		if (sc->vr_revid >= REV_ID_VT6105_LOM &&
		    sc->vr_revid < REV_ID_VT6105M_A0)
			pci_write_config(dev, VR_PCI_MODE2,
			    pci_read_config(dev, VR_PCI_MODE2, 1) |
			    VR_MODE2_MODE10T, 1);
		/* Enable Memory-Read-Multiple. */
		if (sc->vr_revid >= REV_ID_VT6107_A1 &&
		    sc->vr_revid < REV_ID_VT6105M_A0)
			pci_write_config(dev, VR_PCI_MODE2,
			    pci_read_config(dev, VR_PCI_MODE2, 1) |
			    VR_MODE2_MRDPL, 1);
	}
	/* Disable MII AUTOPOLL. */
	VR_CLRBIT(sc, VR_MIICMD, VR_MIICMD_AUTOPOLL);

	if (vr_dma_alloc(sc) != 0) {
		error = ENXIO;
		goto fail;
	}

	/* Do MII setup. */
	if (sc->vr_revid >= REV_ID_VT6105_A0)
		phy = 1;
	else
		phy = CSR_READ_1(sc, VR_PHYADDR) & VR_PHYADDR_MASK;
	error = mii_attach(dev, &sc->vr_miibus, ifp, vr_ifmedia_upd,
	    vr_ifmedia_sts, BMSR_DEFCAPMASK, phy, MII_OFFSET_ANY,
	    sc->vr_revid >= REV_ID_VT6102_A ? MIIF_DOPAUSE : 0);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	/* Call MI attach routine. */
	ether_ifattach(ifp, eaddr);
	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* Hook interrupt last to avoid having to lock softc. */
	error = bus_setup_intr(dev, sc->vr_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    vr_intr, NULL, sc, &sc->vr_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error)
		vr_detach(dev);

	return (error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
vr_detach(device_t dev)
{
	struct vr_softc		*sc = device_get_softc(dev);
	struct ifnet		*ifp = sc->vr_ifp;

	KASSERT(mtx_initialized(&sc->vr_mtx), ("vr mutex not initialized"));

#ifdef DEVICE_POLLING
	if (ifp != NULL && ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	/* These should only be active if attach succeeded. */
	if (device_is_attached(dev)) {
		VR_LOCK(sc);
		sc->vr_flags |= VR_F_DETACHED;
		vr_stop(sc);
		VR_UNLOCK(sc);
		callout_drain(&sc->vr_stat_callout);
		taskqueue_drain(taskqueue_fast, &sc->vr_inttask);
		ether_ifdetach(ifp);
	}
	if (sc->vr_miibus)
		device_delete_child(dev, sc->vr_miibus);
	bus_generic_detach(dev);

	if (sc->vr_intrhand)
		bus_teardown_intr(dev, sc->vr_irq, sc->vr_intrhand);
	if (sc->vr_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->vr_irq);
	if (sc->vr_res)
		bus_release_resource(dev, sc->vr_res_type, sc->vr_res_id,
		    sc->vr_res);

	if (ifp)
		if_free(ifp);

	vr_dma_free(sc);

	mtx_destroy(&sc->vr_mtx);

	return (0);
}

struct vr_dmamap_arg {
	bus_addr_t	vr_busaddr;
};

static void
vr_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct vr_dmamap_arg	*ctx;

	if (error != 0)
		return;
	ctx = arg;
	ctx->vr_busaddr = segs[0].ds_addr;
}

static int
vr_dma_alloc(struct vr_softc *sc)
{
	struct vr_dmamap_arg	ctx;
	struct vr_txdesc	*txd;
	struct vr_rxdesc	*rxd;
	bus_size_t		tx_alignment;
	int			error, i;

	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->vr_dev),	/* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vr_cdata.vr_parent_tag);
	if (error != 0) {
		device_printf(sc->vr_dev, "failed to create parent DMA tag\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(
	    sc->vr_cdata.vr_parent_tag,	/* parent */
	    VR_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    VR_TX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    VR_TX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vr_cdata.vr_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->vr_dev, "failed to create Tx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(
	    sc->vr_cdata.vr_parent_tag,	/* parent */
	    VR_RING_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    VR_RX_RING_SIZE,		/* maxsize */
	    1,				/* nsegments */
	    VR_RX_RING_SIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vr_cdata.vr_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->vr_dev, "failed to create Rx ring DMA tag\n");
		goto fail;
	}

	if ((sc->vr_quirks & VR_Q_NEEDALIGN) != 0)
		tx_alignment = sizeof(uint32_t);
	else
		tx_alignment = 1;
	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(
	    sc->vr_cdata.vr_parent_tag,	/* parent */
	    tx_alignment, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES * VR_MAXFRAGS,	/* maxsize */
	    VR_MAXFRAGS,		/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vr_cdata.vr_tx_tag);
	if (error != 0) {
		device_printf(sc->vr_dev, "failed to create Tx DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(
	    sc->vr_cdata.vr_parent_tag,	/* parent */
	    VR_RX_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->vr_cdata.vr_rx_tag);
	if (error != 0) {
		device_printf(sc->vr_dev, "failed to create Rx DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->vr_cdata.vr_tx_ring_tag,
	    (void **)&sc->vr_rdata.vr_tx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->vr_cdata.vr_tx_ring_map);
	if (error != 0) {
		device_printf(sc->vr_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.vr_busaddr = 0;
	error = bus_dmamap_load(sc->vr_cdata.vr_tx_ring_tag,
	    sc->vr_cdata.vr_tx_ring_map, sc->vr_rdata.vr_tx_ring,
	    VR_TX_RING_SIZE, vr_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.vr_busaddr == 0) {
		device_printf(sc->vr_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc->vr_rdata.vr_tx_ring_paddr = ctx.vr_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->vr_cdata.vr_rx_ring_tag,
	    (void **)&sc->vr_rdata.vr_rx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->vr_cdata.vr_rx_ring_map);
	if (error != 0) {
		device_printf(sc->vr_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.vr_busaddr = 0;
	error = bus_dmamap_load(sc->vr_cdata.vr_rx_ring_tag,
	    sc->vr_cdata.vr_rx_ring_map, sc->vr_rdata.vr_rx_ring,
	    VR_RX_RING_SIZE, vr_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.vr_busaddr == 0) {
		device_printf(sc->vr_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc->vr_rdata.vr_rx_ring_paddr = ctx.vr_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < VR_TX_RING_CNT; i++) {
		txd = &sc->vr_cdata.vr_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->vr_cdata.vr_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->vr_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->vr_cdata.vr_rx_tag, 0,
	    &sc->vr_cdata.vr_rx_sparemap)) != 0) {
		device_printf(sc->vr_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < VR_RX_RING_CNT; i++) {
		rxd = &sc->vr_cdata.vr_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->vr_cdata.vr_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->vr_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
vr_dma_free(struct vr_softc *sc)
{
	struct vr_txdesc	*txd;
	struct vr_rxdesc	*rxd;
	int			i;

	/* Tx ring. */
	if (sc->vr_cdata.vr_tx_ring_tag) {
		if (sc->vr_rdata.vr_tx_ring_paddr)
			bus_dmamap_unload(sc->vr_cdata.vr_tx_ring_tag,
			    sc->vr_cdata.vr_tx_ring_map);
		if (sc->vr_rdata.vr_tx_ring)
			bus_dmamem_free(sc->vr_cdata.vr_tx_ring_tag,
			    sc->vr_rdata.vr_tx_ring,
			    sc->vr_cdata.vr_tx_ring_map);
		sc->vr_rdata.vr_tx_ring = NULL;
		sc->vr_rdata.vr_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->vr_cdata.vr_tx_ring_tag);
		sc->vr_cdata.vr_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->vr_cdata.vr_rx_ring_tag) {
		if (sc->vr_rdata.vr_rx_ring_paddr)
			bus_dmamap_unload(sc->vr_cdata.vr_rx_ring_tag,
			    sc->vr_cdata.vr_rx_ring_map);
		if (sc->vr_rdata.vr_rx_ring)
			bus_dmamem_free(sc->vr_cdata.vr_rx_ring_tag,
			    sc->vr_rdata.vr_rx_ring,
			    sc->vr_cdata.vr_rx_ring_map);
		sc->vr_rdata.vr_rx_ring = NULL;
		sc->vr_rdata.vr_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->vr_cdata.vr_rx_ring_tag);
		sc->vr_cdata.vr_rx_ring_tag = NULL;
	}
	/* Tx buffers. */
	if (sc->vr_cdata.vr_tx_tag) {
		for (i = 0; i < VR_TX_RING_CNT; i++) {
			txd = &sc->vr_cdata.vr_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc->vr_cdata.vr_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->vr_cdata.vr_tx_tag);
		sc->vr_cdata.vr_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc->vr_cdata.vr_rx_tag) {
		for (i = 0; i < VR_RX_RING_CNT; i++) {
			rxd = &sc->vr_cdata.vr_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc->vr_cdata.vr_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->vr_cdata.vr_rx_sparemap) {
			bus_dmamap_destroy(sc->vr_cdata.vr_rx_tag,
			    sc->vr_cdata.vr_rx_sparemap);
			sc->vr_cdata.vr_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc->vr_cdata.vr_rx_tag);
		sc->vr_cdata.vr_rx_tag = NULL;
	}

	if (sc->vr_cdata.vr_parent_tag) {
		bus_dma_tag_destroy(sc->vr_cdata.vr_parent_tag);
		sc->vr_cdata.vr_parent_tag = NULL;
	}
}

/*
 * Initialize the transmit descriptors.
 */
static int
vr_tx_ring_init(struct vr_softc *sc)
{
	struct vr_ring_data	*rd;
	struct vr_txdesc	*txd;
	bus_addr_t		addr;
	int			i;

	sc->vr_cdata.vr_tx_prod = 0;
	sc->vr_cdata.vr_tx_cons = 0;
	sc->vr_cdata.vr_tx_cnt = 0;
	sc->vr_cdata.vr_tx_pkts = 0;

	rd = &sc->vr_rdata;
	bzero(rd->vr_tx_ring, VR_TX_RING_SIZE);
	for (i = 0; i < VR_TX_RING_CNT; i++) {
		if (i == VR_TX_RING_CNT - 1)
			addr = VR_TX_RING_ADDR(sc, 0);
		else
			addr = VR_TX_RING_ADDR(sc, i + 1);
		rd->vr_tx_ring[i].vr_nextphys = htole32(VR_ADDR_LO(addr));
		txd = &sc->vr_cdata.vr_txdesc[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->vr_cdata.vr_tx_ring_tag,
	    sc->vr_cdata.vr_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
vr_rx_ring_init(struct vr_softc *sc)
{
	struct vr_ring_data	*rd;
	struct vr_rxdesc	*rxd;
	bus_addr_t		addr;
	int			i;

	sc->vr_cdata.vr_rx_cons = 0;

	rd = &sc->vr_rdata;
	bzero(rd->vr_rx_ring, VR_RX_RING_SIZE);
	for (i = 0; i < VR_RX_RING_CNT; i++) {
		rxd = &sc->vr_cdata.vr_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->desc = &rd->vr_rx_ring[i];
		if (i == VR_RX_RING_CNT - 1)
			addr = VR_RX_RING_ADDR(sc, 0);
		else
			addr = VR_RX_RING_ADDR(sc, i + 1);
		rd->vr_rx_ring[i].vr_nextphys = htole32(VR_ADDR_LO(addr));
		if (vr_newbuf(sc, i) != 0)
			return (ENOBUFS);
	}

	bus_dmamap_sync(sc->vr_cdata.vr_rx_ring_tag,
	    sc->vr_cdata.vr_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static __inline void
vr_discard_rxbuf(struct vr_rxdesc *rxd)
{
	struct vr_desc	*desc;

	desc = rxd->desc;
	desc->vr_ctl = htole32(VR_RXCTL | (MCLBYTES - sizeof(uint64_t)));
	desc->vr_status = htole32(VR_RXSTAT_OWN);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 * Note: the length fields are only 11 bits wide, which means the
 * largest size we can specify is 2047. This is important because
 * MCLBYTES is 2048, so we have to subtract one otherwise we'll
 * overflow the field and make a mess.
 */
static int
vr_newbuf(struct vr_softc *sc, int idx)
{
	struct vr_desc		*desc;
	struct vr_rxdesc	*rxd;
	struct mbuf		*m;
	bus_dma_segment_t	segs[1];
	bus_dmamap_t		map;
	int			nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, sizeof(uint64_t));

	if (bus_dmamap_load_mbuf_sg(sc->vr_cdata.vr_rx_tag,
	    sc->vr_cdata.vr_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc->vr_cdata.vr_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->vr_cdata.vr_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->vr_cdata.vr_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->vr_cdata.vr_rx_sparemap;
	sc->vr_cdata.vr_rx_sparemap = map;
	bus_dmamap_sync(sc->vr_cdata.vr_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	desc = rxd->desc;
	desc->vr_data = htole32(VR_ADDR_LO(segs[0].ds_addr));
	desc->vr_ctl = htole32(VR_RXCTL | segs[0].ds_len);
	desc->vr_status = htole32(VR_RXSTAT_OWN);

	return (0);
}

#ifndef __NO_STRICT_ALIGNMENT
static __inline void
vr_fixup_rx(struct mbuf *m)
{
        uint16_t		*src, *dst;
        int			i;

	src = mtod(m, uint16_t *);
	dst = src - 1;

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;

	m->m_data -= ETHER_ALIGN;
}
#endif

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static int
vr_rxeof(struct vr_softc *sc)
{
	struct vr_rxdesc	*rxd;
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct vr_desc		*cur_rx;
	int			cons, prog, total_len, rx_npkts;
	uint32_t		rxstat, rxctl;

	VR_LOCK_ASSERT(sc);
	ifp = sc->vr_ifp;
	cons = sc->vr_cdata.vr_rx_cons;
	rx_npkts = 0;

	bus_dmamap_sync(sc->vr_cdata.vr_rx_ring_tag,
	    sc->vr_cdata.vr_rx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	for (prog = 0; prog < VR_RX_RING_CNT; VR_INC(cons, VR_RX_RING_CNT)) {
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING) {
			if (sc->rxcycles <= 0)
				break;
			sc->rxcycles--;
		}
#endif
		cur_rx = &sc->vr_rdata.vr_rx_ring[cons];
		rxstat = le32toh(cur_rx->vr_status);
		rxctl = le32toh(cur_rx->vr_ctl);
		if ((rxstat & VR_RXSTAT_OWN) == VR_RXSTAT_OWN)
			break;

		prog++;
		rxd = &sc->vr_cdata.vr_rxdesc[cons];
		m = rxd->rx_m;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
		 * comes up in the ring.
		 * We don't support SG in Rx path yet, so discard
		 * partial frame.
		 */
		if ((rxstat & VR_RXSTAT_RX_OK) == 0 ||
		    (rxstat & (VR_RXSTAT_FIRSTFRAG | VR_RXSTAT_LASTFRAG)) !=
		    (VR_RXSTAT_FIRSTFRAG | VR_RXSTAT_LASTFRAG)) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			sc->vr_stat.rx_errors++;
			if (rxstat & VR_RXSTAT_CRCERR)
				sc->vr_stat.rx_crc_errors++;
			if (rxstat & VR_RXSTAT_FRAMEALIGNERR)
				sc->vr_stat.rx_alignment++;
			if (rxstat & VR_RXSTAT_FIFOOFLOW)
				sc->vr_stat.rx_fifo_overflows++;
			if (rxstat & VR_RXSTAT_GIANT)
				sc->vr_stat.rx_giants++;
			if (rxstat & VR_RXSTAT_RUNT)
				sc->vr_stat.rx_runts++;
			if (rxstat & VR_RXSTAT_BUFFERR)
				sc->vr_stat.rx_no_buffers++;
#ifdef	VR_SHOW_ERRORS
			device_printf(sc->vr_dev, "%s: receive error = 0x%b\n",
			    __func__, rxstat & 0xff, VR_RXSTAT_ERR_BITS);
#endif
			vr_discard_rxbuf(rxd);
			continue;
		}

		if (vr_newbuf(sc, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			sc->vr_stat.rx_errors++;
			sc->vr_stat.rx_no_mbufs++;
			vr_discard_rxbuf(rxd);
			continue;
		}

		/*
		 * XXX The VIA Rhine chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off (at least, I can't find anything in
		 * the manual that explains how to do it) so we have
		 * to trim off the CRC manually.
		 */
		total_len = VR_RXBYTES(rxstat);
		total_len -= ETHER_CRC_LEN;
		m->m_pkthdr.len = m->m_len = total_len;
#ifndef	__NO_STRICT_ALIGNMENT
		/*
		 * RX buffers must be 32-bit aligned.
		 * Ignore the alignment problems on the non-strict alignment
		 * platform. The performance hit incurred due to unaligned
		 * accesses is much smaller than the hit produced by forcing
		 * buffer copies all the time.
		 */
		vr_fixup_rx(m);
#endif
		m->m_pkthdr.rcvif = ifp;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		sc->vr_stat.rx_ok++;
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0 &&
		    (rxstat & VR_RXSTAT_FRAG) == 0 &&
		    (rxctl & VR_RXCTL_IP) != 0) {
			/* Checksum is valid for non-fragmented IP packets. */
			m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			if ((rxctl & VR_RXCTL_IPOK) == VR_RXCTL_IPOK) {
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
				if (rxctl & (VR_RXCTL_TCP | VR_RXCTL_UDP)) {
					m->m_pkthdr.csum_flags |=
					    CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
					if ((rxctl & VR_RXCTL_TCPUDPOK) != 0)
						m->m_pkthdr.csum_data = 0xffff;
				}
			}
		}
		VR_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		VR_LOCK(sc);
		rx_npkts++;
	}

	if (prog > 0) {
		/*
		 * Let controller know how many number of RX buffers
		 * are posted but avoid expensive register access if
		 * TX pause capability was not negotiated with link
		 * partner.
		 */
		if ((sc->vr_flags & VR_F_TXPAUSE) != 0) {
			if (prog >= VR_RX_RING_CNT)
				prog = VR_RX_RING_CNT - 1;
			CSR_WRITE_1(sc, VR_FLOWCR0, prog);
		}
		sc->vr_cdata.vr_rx_cons = cons;
		bus_dmamap_sync(sc->vr_cdata.vr_rx_ring_tag,
		    sc->vr_cdata.vr_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
	return (rx_npkts);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
vr_txeof(struct vr_softc *sc)
{
	struct vr_txdesc	*txd;
	struct vr_desc		*cur_tx;
	struct ifnet		*ifp;
	uint32_t		txctl, txstat;
	int			cons, prod;

	VR_LOCK_ASSERT(sc);

	cons = sc->vr_cdata.vr_tx_cons;
	prod = sc->vr_cdata.vr_tx_prod;
	if (cons == prod)
		return;

	bus_dmamap_sync(sc->vr_cdata.vr_tx_ring_tag,
	    sc->vr_cdata.vr_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	ifp = sc->vr_ifp;
	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	for (; cons != prod; VR_INC(cons, VR_TX_RING_CNT)) {
		cur_tx = &sc->vr_rdata.vr_tx_ring[cons];
		txctl = le32toh(cur_tx->vr_ctl);
		txstat = le32toh(cur_tx->vr_status);
		if ((txstat & VR_TXSTAT_OWN) == VR_TXSTAT_OWN)
			break;

		sc->vr_cdata.vr_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		/* Only the first descriptor in the chain is valid. */
		if ((txctl & VR_TXCTL_FIRSTFRAG) == 0)
			continue;

		txd = &sc->vr_cdata.vr_txdesc[cons];
		KASSERT(txd->tx_m != NULL, ("%s: accessing NULL mbuf!\n",
		    __func__));

		if ((txstat & VR_TXSTAT_ERRSUM) != 0) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			sc->vr_stat.tx_errors++;
			if ((txstat & VR_TXSTAT_ABRT) != 0) {
				/* Give up and restart Tx. */
				sc->vr_stat.tx_abort++;
				bus_dmamap_sync(sc->vr_cdata.vr_tx_tag,
				    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
				bus_dmamap_unload(sc->vr_cdata.vr_tx_tag,
				    txd->tx_dmamap);
				m_freem(txd->tx_m);
				txd->tx_m = NULL;
				VR_INC(cons, VR_TX_RING_CNT);
				sc->vr_cdata.vr_tx_cons = cons;
				if (vr_tx_stop(sc) != 0) {
					device_printf(sc->vr_dev,
					    "%s: Tx shutdown error -- "
					    "resetting\n", __func__);
					sc->vr_flags |= VR_F_RESTART;
					return;
				}
				vr_tx_start(sc);
				break;
			}
			if ((sc->vr_revid < REV_ID_VT3071_A &&
			    (txstat & VR_TXSTAT_UNDERRUN)) ||
			    (txstat & (VR_TXSTAT_UDF | VR_TXSTAT_TBUFF))) {
				sc->vr_stat.tx_underrun++;
				/* Retry and restart Tx. */
				sc->vr_cdata.vr_tx_cnt++;
				sc->vr_cdata.vr_tx_cons = cons;
				cur_tx->vr_status = htole32(VR_TXSTAT_OWN);
				bus_dmamap_sync(sc->vr_cdata.vr_tx_ring_tag,
				    sc->vr_cdata.vr_tx_ring_map,
				    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
				vr_tx_underrun(sc);
				return;
			}
			if ((txstat & VR_TXSTAT_DEFER) != 0) {
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
				sc->vr_stat.tx_collisions++;
			}
			if ((txstat & VR_TXSTAT_LATECOLL) != 0) {
				if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
				sc->vr_stat.tx_late_collisions++;
			}
		} else {
			sc->vr_stat.tx_ok++;
			if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		}

		bus_dmamap_sync(sc->vr_cdata.vr_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->vr_cdata.vr_tx_tag, txd->tx_dmamap);
		if (sc->vr_revid < REV_ID_VT3071_A) {
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
			    (txstat & VR_TXSTAT_COLLCNT) >> 3);
			sc->vr_stat.tx_collisions +=
			    (txstat & VR_TXSTAT_COLLCNT) >> 3;
		} else {
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (txstat & 0x0f));
			sc->vr_stat.tx_collisions += (txstat & 0x0f);
		}
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
	}

	sc->vr_cdata.vr_tx_cons = cons;
	if (sc->vr_cdata.vr_tx_cnt == 0)
		sc->vr_watchdog_timer = 0;
}

static void
vr_tick(void *xsc)
{
	struct vr_softc		*sc;
	struct mii_data		*mii;

	sc = (struct vr_softc *)xsc;

	VR_LOCK_ASSERT(sc);

	if ((sc->vr_flags & VR_F_RESTART) != 0) {
		device_printf(sc->vr_dev, "restarting\n");
		sc->vr_stat.num_restart++;
		sc->vr_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		vr_init_locked(sc);
		sc->vr_flags &= ~VR_F_RESTART;
	}

	mii = device_get_softc(sc->vr_miibus);
	mii_tick(mii);
	if ((sc->vr_flags & VR_F_LINK) == 0)
		vr_miibus_statchg(sc->vr_dev);
	vr_watchdog(sc);
	callout_reset(&sc->vr_stat_callout, hz, vr_tick, sc);
}

#ifdef DEVICE_POLLING
static poll_handler_t vr_poll;
static poll_handler_t vr_poll_locked;

static int
vr_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct vr_softc *sc;
	int rx_npkts;

	sc = ifp->if_softc;
	rx_npkts = 0;

	VR_LOCK(sc);
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		rx_npkts = vr_poll_locked(ifp, cmd, count);
	VR_UNLOCK(sc);
	return (rx_npkts);
}

static int
vr_poll_locked(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct vr_softc *sc;
	int rx_npkts;

	sc = ifp->if_softc;

	VR_LOCK_ASSERT(sc);

	sc->rxcycles = count;
	rx_npkts = vr_rxeof(sc);
	vr_txeof(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		vr_start_locked(ifp);

	if (cmd == POLL_AND_CHECK_STATUS) {
		uint16_t status;

		/* Also check status register. */
		status = CSR_READ_2(sc, VR_ISR);
		if (status)
			CSR_WRITE_2(sc, VR_ISR, status);

		if ((status & VR_INTRS) == 0)
			return (rx_npkts);

		if ((status & (VR_ISR_BUSERR | VR_ISR_LINKSTAT2 |
		    VR_ISR_STATSOFLOW)) != 0) {
			if (vr_error(sc, status) != 0)
				return (rx_npkts);
		}
		if ((status & (VR_ISR_RX_NOBUF | VR_ISR_RX_OFLOW)) != 0) {
#ifdef	VR_SHOW_ERRORS
			device_printf(sc->vr_dev, "%s: receive error : 0x%b\n",
			    __func__, status, VR_ISR_ERR_BITS);
#endif
			vr_rx_start(sc);
		}
	}
	return (rx_npkts);
}
#endif /* DEVICE_POLLING */

/* Back off the transmit threshold. */
static void
vr_tx_underrun(struct vr_softc *sc)
{
	int	thresh;

	device_printf(sc->vr_dev, "Tx underrun -- ");
	if (sc->vr_txthresh < VR_TXTHRESH_MAX) {
		thresh = sc->vr_txthresh;
		sc->vr_txthresh++;
		if (sc->vr_txthresh >= VR_TXTHRESH_MAX) {
			sc->vr_txthresh = VR_TXTHRESH_MAX;
			printf("using store and forward mode\n");
		} else
			printf("increasing Tx threshold(%d -> %d)\n",
			    vr_tx_threshold_tables[thresh].value,
			    vr_tx_threshold_tables[thresh + 1].value);
	} else
		printf("\n");
	sc->vr_stat.tx_underrun++;
	if (vr_tx_stop(sc) != 0) {
		device_printf(sc->vr_dev, "%s: Tx shutdown error -- "
		    "resetting\n", __func__);
		sc->vr_flags |= VR_F_RESTART;
		return;
	}
	vr_tx_start(sc);
}

static int
vr_intr(void *arg)
{
	struct vr_softc		*sc;
	uint16_t		status;

	sc = (struct vr_softc *)arg;

	status = CSR_READ_2(sc, VR_ISR);
	if (status == 0 || status == 0xffff || (status & VR_INTRS) == 0)
		return (FILTER_STRAY);

	/* Disable interrupts. */
	CSR_WRITE_2(sc, VR_IMR, 0x0000);

	taskqueue_enqueue(taskqueue_fast, &sc->vr_inttask);

	return (FILTER_HANDLED);
}

static void
vr_int_task(void *arg, int npending)
{
	struct vr_softc		*sc;
	struct ifnet		*ifp;
	uint16_t		status;

	sc = (struct vr_softc *)arg;

	VR_LOCK(sc);

	if ((sc->vr_flags & VR_F_SUSPENDED) != 0)
		goto done_locked;

	status = CSR_READ_2(sc, VR_ISR);
	ifp = sc->vr_ifp;
#ifdef DEVICE_POLLING
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		goto done_locked;
#endif

	/* Suppress unwanted interrupts. */
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0 ||
	    (sc->vr_flags & VR_F_RESTART) != 0) {
		CSR_WRITE_2(sc, VR_IMR, 0);
		CSR_WRITE_2(sc, VR_ISR, status);
		goto done_locked;
	}

	for (; (status & VR_INTRS) != 0;) {
		CSR_WRITE_2(sc, VR_ISR, status);
		if ((status & (VR_ISR_BUSERR | VR_ISR_LINKSTAT2 |
		    VR_ISR_STATSOFLOW)) != 0) {
			if (vr_error(sc, status) != 0) {
				VR_UNLOCK(sc);
				return;
			}
		}
		vr_rxeof(sc);
		if ((status & (VR_ISR_RX_NOBUF | VR_ISR_RX_OFLOW)) != 0) {
#ifdef	VR_SHOW_ERRORS
			device_printf(sc->vr_dev, "%s: receive error = 0x%b\n",
			    __func__, status, VR_ISR_ERR_BITS);
#endif
			/* Restart Rx if RxDMA SM was stopped. */
			vr_rx_start(sc);
		}
		vr_txeof(sc);

		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			vr_start_locked(ifp);

		status = CSR_READ_2(sc, VR_ISR);
	}

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, VR_IMR, VR_INTRS);

done_locked:
	VR_UNLOCK(sc);
}

static int
vr_error(struct vr_softc *sc, uint16_t status)
{
	uint16_t pcis;

	status &= VR_ISR_BUSERR | VR_ISR_LINKSTAT2 | VR_ISR_STATSOFLOW;
	if ((status & VR_ISR_BUSERR) != 0) {
		status &= ~VR_ISR_BUSERR;
		sc->vr_stat.bus_errors++;
		/* Disable further interrupts. */
		CSR_WRITE_2(sc, VR_IMR, 0);
		pcis = pci_read_config(sc->vr_dev, PCIR_STATUS, 2);
		device_printf(sc->vr_dev, "PCI bus error(0x%04x) -- "
		    "resetting\n", pcis);
		pci_write_config(sc->vr_dev, PCIR_STATUS, pcis, 2);
		sc->vr_flags |= VR_F_RESTART;
		return (EAGAIN);
	}
	if ((status & VR_ISR_LINKSTAT2) != 0) {
		/* Link state change, duplex changes etc. */
		status &= ~VR_ISR_LINKSTAT2;
	}
	if ((status & VR_ISR_STATSOFLOW) != 0) {
		status &= ~VR_ISR_STATSOFLOW;
		if (sc->vr_revid >= REV_ID_VT6105M_A0) {
			/* Update MIB counters. */
		}
	}

	if (status != 0)
		device_printf(sc->vr_dev,
		    "unhandled interrupt, status = 0x%04x\n", status);
	return (0);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
vr_encap(struct vr_softc *sc, struct mbuf **m_head)
{
	struct vr_txdesc	*txd;
	struct vr_desc		*desc;
	struct mbuf		*m;
	bus_dma_segment_t	txsegs[VR_MAXFRAGS];
	uint32_t		csum_flags, txctl;
	int			error, i, nsegs, prod, si;
	int			padlen;

	VR_LOCK_ASSERT(sc);

	M_ASSERTPKTHDR((*m_head));

	/*
	 * Some VIA Rhine wants packet buffers to be longword
	 * aligned, but very often our mbufs aren't. Rather than
	 * waste time trying to decide when to copy and when not
	 * to copy, just do it all the time.
	 */
	if ((sc->vr_quirks & VR_Q_NEEDALIGN) != 0) {
		m = m_defrag(*m_head, M_NOWAIT);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
	}

	/*
	 * The Rhine chip doesn't auto-pad, so we have to make
	 * sure to pad short frames out to the minimum frame length
	 * ourselves.
	 */
	if ((*m_head)->m_pkthdr.len < VR_MIN_FRAMELEN) {
		m = *m_head;
		padlen = VR_MIN_FRAMELEN - m->m_pkthdr.len;
		if (M_WRITABLE(m) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			m_freem(*m_head);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}
		if (m->m_next != NULL || M_TRAILINGSPACE(m) < padlen) {
			m = m_defrag(m, M_NOWAIT);
			if (m == NULL) {
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

	prod = sc->vr_cdata.vr_tx_prod;
	txd = &sc->vr_cdata.vr_txdesc[prod];
	error = bus_dmamap_load_mbuf_sg(sc->vr_cdata.vr_tx_tag, txd->tx_dmamap,
	    *m_head, txsegs, &nsegs, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, VR_MAXFRAGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->vr_cdata.vr_tx_tag,
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
	if (sc->vr_cdata.vr_tx_cnt + nsegs >= (VR_TX_RING_CNT - 1)) {
		bus_dmamap_unload(sc->vr_cdata.vr_tx_tag, txd->tx_dmamap);
		return (ENOBUFS);
	}

	txd->tx_m = *m_head;
	bus_dmamap_sync(sc->vr_cdata.vr_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);

	/* Set checksum offload. */
	csum_flags = 0;
	if (((*m_head)->m_pkthdr.csum_flags & VR_CSUM_FEATURES) != 0) {
		if ((*m_head)->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= VR_TXCTL_IPCSUM;
		if ((*m_head)->m_pkthdr.csum_flags & CSUM_TCP)
			csum_flags |= VR_TXCTL_TCPCSUM;
		if ((*m_head)->m_pkthdr.csum_flags & CSUM_UDP)
			csum_flags |= VR_TXCTL_UDPCSUM;
	}

	/*
	 * Quite contrary to datasheet for VIA Rhine, VR_TXCTL_TLINK bit
	 * is required for all descriptors regardless of single or
	 * multiple buffers. Also VR_TXSTAT_OWN bit is valid only for
	 * the first descriptor for a multi-fragmented frames. Without
	 * that VIA Rhine chip generates Tx underrun interrupts and can't
	 * send any frames.
	 */
	si = prod;
	for (i = 0; i < nsegs; i++) {
		desc = &sc->vr_rdata.vr_tx_ring[prod];
		desc->vr_status = 0;
		txctl = txsegs[i].ds_len | VR_TXCTL_TLINK | csum_flags;
		if (i == 0)
			txctl |= VR_TXCTL_FIRSTFRAG;
		desc->vr_ctl = htole32(txctl);
		desc->vr_data = htole32(VR_ADDR_LO(txsegs[i].ds_addr));
		sc->vr_cdata.vr_tx_cnt++;
		VR_INC(prod, VR_TX_RING_CNT);
	}
	/* Update producer index. */
	sc->vr_cdata.vr_tx_prod = prod;

	prod = (prod + VR_TX_RING_CNT - 1) % VR_TX_RING_CNT;
	desc = &sc->vr_rdata.vr_tx_ring[prod];

	/*
	 * Set EOP on the last desciptor and reuqest Tx completion
	 * interrupt for every VR_TX_INTR_THRESH-th frames.
	 */
	VR_INC(sc->vr_cdata.vr_tx_pkts, VR_TX_INTR_THRESH);
	if (sc->vr_cdata.vr_tx_pkts == 0)
		desc->vr_ctl |= htole32(VR_TXCTL_LASTFRAG | VR_TXCTL_FINT);
	else
		desc->vr_ctl |= htole32(VR_TXCTL_LASTFRAG);

	/* Lastly turn the first descriptor ownership to hardware. */
	desc = &sc->vr_rdata.vr_tx_ring[si];
	desc->vr_status |= htole32(VR_TXSTAT_OWN);

	/* Sync descriptors. */
	bus_dmamap_sync(sc->vr_cdata.vr_tx_ring_tag,
	    sc->vr_cdata.vr_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
vr_start(struct ifnet *ifp)
{
	struct vr_softc		*sc;

	sc = ifp->if_softc;
	VR_LOCK(sc);
	vr_start_locked(ifp);
	VR_UNLOCK(sc);
}

static void
vr_start_locked(struct ifnet *ifp)
{
	struct vr_softc		*sc;
	struct mbuf		*m_head;
	int			enq;

	sc = ifp->if_softc;

	VR_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->vr_flags & VR_F_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc->vr_cdata.vr_tx_cnt < VR_TX_RING_CNT - 2; ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (vr_encap(sc, &m_head)) {
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
		/* Tell the chip to start transmitting. */
		VR_SETBIT(sc, VR_CR0, VR_CR0_TX_GO);
		/* Set a timeout in case the chip goes out to lunch. */
		sc->vr_watchdog_timer = 5;
	}
}

static void
vr_init(void *xsc)
{
	struct vr_softc		*sc;

	sc = (struct vr_softc *)xsc;
	VR_LOCK(sc);
	vr_init_locked(sc);
	VR_UNLOCK(sc);
}

static void
vr_init_locked(struct vr_softc *sc)
{
	struct ifnet		*ifp;
	struct mii_data		*mii;
	bus_addr_t		addr;
	int			i;

	VR_LOCK_ASSERT(sc);

	ifp = sc->vr_ifp;
	mii = device_get_softc(sc->vr_miibus);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/* Cancel pending I/O and free all RX/TX buffers. */
	vr_stop(sc);
	vr_reset(sc);

	/* Set our station address. */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, VR_PAR0 + i, IF_LLADDR(sc->vr_ifp)[i]);

	/* Set DMA size. */
	VR_CLRBIT(sc, VR_BCR0, VR_BCR0_DMA_LENGTH);
	VR_SETBIT(sc, VR_BCR0, VR_BCR0_DMA_STORENFWD);

	/*
	 * BCR0 and BCR1 can override the RXCFG and TXCFG registers,
	 * so we must set both.
	 */
	VR_CLRBIT(sc, VR_BCR0, VR_BCR0_RX_THRESH);
	VR_SETBIT(sc, VR_BCR0, VR_BCR0_RXTHRESH128BYTES);

	VR_CLRBIT(sc, VR_BCR1, VR_BCR1_TX_THRESH);
	VR_SETBIT(sc, VR_BCR1, vr_tx_threshold_tables[sc->vr_txthresh].bcr_cfg);

	VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_THRESH);
	VR_SETBIT(sc, VR_RXCFG, VR_RXTHRESH_128BYTES);

	VR_CLRBIT(sc, VR_TXCFG, VR_TXCFG_TX_THRESH);
	VR_SETBIT(sc, VR_TXCFG, vr_tx_threshold_tables[sc->vr_txthresh].tx_cfg);

	/* Init circular RX list. */
	if (vr_rx_ring_init(sc) != 0) {
		device_printf(sc->vr_dev,
		    "initialization failed: no memory for rx buffers\n");
		vr_stop(sc);
		return;
	}

	/* Init tx descriptors. */
	vr_tx_ring_init(sc);

	if ((sc->vr_quirks & VR_Q_CAM) != 0) {
		uint8_t vcam[2] = { 0, 0 };

		/* Disable VLAN hardware tag insertion/stripping. */
		VR_CLRBIT(sc, VR_TXCFG, VR_TXCFG_TXTAGEN | VR_TXCFG_RXTAGCTL);
		/* Disable VLAN hardware filtering. */
		VR_CLRBIT(sc, VR_BCR1, VR_BCR1_VLANFILT_ENB);
		/* Disable all CAM entries. */
		vr_cam_mask(sc, VR_MCAST_CAM, 0);
		vr_cam_mask(sc, VR_VLAN_CAM, 0);
		/* Enable the first VLAN CAM. */
		vr_cam_data(sc, VR_VLAN_CAM, 0, vcam);
		vr_cam_mask(sc, VR_VLAN_CAM, 1);
	}

	/*
	 * Set up receive filter.
	 */
	vr_set_filter(sc);

	/*
	 * Load the address of the RX ring.
	 */
	addr = VR_RX_RING_ADDR(sc, 0);
	CSR_WRITE_4(sc, VR_RXADDR, VR_ADDR_LO(addr));
	/*
	 * Load the address of the TX ring.
	 */
	addr = VR_TX_RING_ADDR(sc, 0);
	CSR_WRITE_4(sc, VR_TXADDR, VR_ADDR_LO(addr));
	/* Default : full-duplex, no Tx poll. */
	CSR_WRITE_1(sc, VR_CR1, VR_CR1_FULLDUPLEX | VR_CR1_TX_NOPOLL);

	/* Set flow-control parameters for Rhine III. */
	if (sc->vr_revid >= REV_ID_VT6105_A0) {
		/*
		 * Configure Rx buffer count available for incoming
		 * packet.
		 * Even though data sheet says almost nothing about
		 * this register, this register should be updated
		 * whenever driver adds new RX buffers to controller.
		 * Otherwise, XON frame is not sent to link partner
		 * even if controller has enough RX buffers and you
		 * would be isolated from network.
		 * The controller is not smart enough to know number
		 * of available RX buffers so driver have to let
		 * controller know how many RX buffers are posted.
		 * In other words, this register works like a residue
		 * counter for RX buffers and should be initialized
		 * to the number of total RX buffers  - 1 before
		 * enabling RX MAC.  Note, this register is 8bits so
		 * it effectively limits the maximum number of RX
		 * buffer to be configured by controller is 255.
		 */
		CSR_WRITE_1(sc, VR_FLOWCR0, VR_RX_RING_CNT - 1);
		/*
		 * Tx pause low threshold : 8 free receive buffers
		 * Tx pause XON high threshold : 24 free receive buffers
		 */
		CSR_WRITE_1(sc, VR_FLOWCR1,
		    VR_FLOWCR1_TXLO8 | VR_FLOWCR1_TXHI24 | VR_FLOWCR1_XONXOFF);
		/* Set Tx pause timer. */
		CSR_WRITE_2(sc, VR_PAUSETIMER, 0xffff);
	}

	/* Enable receiver and transmitter. */
	CSR_WRITE_1(sc, VR_CR0,
	    VR_CR0_START | VR_CR0_TX_ON | VR_CR0_RX_ON | VR_CR0_RX_GO);

	CSR_WRITE_2(sc, VR_ISR, 0xFFFF);
#ifdef DEVICE_POLLING
	/*
	 * Disable interrupts if we are polling.
	 */
	if (ifp->if_capenable & IFCAP_POLLING)
		CSR_WRITE_2(sc, VR_IMR, 0);
	else
#endif
	/*
	 * Enable interrupts and disable MII intrs.
	 */
	CSR_WRITE_2(sc, VR_IMR, VR_INTRS);
	if (sc->vr_revid > REV_ID_VT6102_A)
		CSR_WRITE_2(sc, VR_MII_IMR, 0);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc->vr_flags &= ~(VR_F_LINK | VR_F_TXPAUSE);
	mii_mediachg(mii);

	callout_reset(&sc->vr_stat_callout, hz, vr_tick, sc);
}

/*
 * Set media options.
 */
static int
vr_ifmedia_upd(struct ifnet *ifp)
{
	struct vr_softc		*sc;
	struct mii_data		*mii;
	struct mii_softc	*miisc;
	int			error;

	sc = ifp->if_softc;
	VR_LOCK(sc);
	mii = device_get_softc(sc->vr_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	sc->vr_flags &= ~(VR_F_LINK | VR_F_TXPAUSE);
	error = mii_mediachg(mii);
	VR_UNLOCK(sc);

	return (error);
}

/*
 * Report current media status.
 */
static void
vr_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct vr_softc		*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->vr_miibus);
	VR_LOCK(sc);
	if ((ifp->if_flags & IFF_UP) == 0) {
		VR_UNLOCK(sc);
		return;
	}
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	VR_UNLOCK(sc);
}

static int
vr_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct vr_softc		*sc;
	struct ifreq		*ifr;
	struct mii_data		*mii;
	int			error, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		VR_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				if ((ifp->if_flags ^ sc->vr_if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					vr_set_filter(sc);
			} else {
				if ((sc->vr_flags & VR_F_DETACHED) == 0)
					vr_init_locked(sc);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				vr_stop(sc);
		}
		sc->vr_if_flags = ifp->if_flags;
		VR_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		VR_LOCK(sc);
		vr_set_filter(sc);
		VR_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->vr_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if (mask & IFCAP_POLLING) {
			if (ifr->ifr_reqcap & IFCAP_POLLING) {
				error = ether_poll_register(vr_poll, ifp);
				if (error != 0)
					break;
				VR_LOCK(sc);
				/* Disable interrupts. */
				CSR_WRITE_2(sc, VR_IMR, 0x0000);
				ifp->if_capenable |= IFCAP_POLLING;
				VR_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				/* Enable interrupts. */
				VR_LOCK(sc);
				CSR_WRITE_2(sc, VR_IMR, VR_INTRS);
				ifp->if_capenable &= ~IFCAP_POLLING;
				VR_UNLOCK(sc);
			}
		}
#endif /* DEVICE_POLLING */
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (IFCAP_TXCSUM & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((IFCAP_TXCSUM & ifp->if_capenable) != 0)
				ifp->if_hwassist |= VR_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~VR_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (IFCAP_RXCSUM & ifp->if_capabilities) != 0)
			ifp->if_capenable ^= IFCAP_RXCSUM;
		if ((mask & IFCAP_WOL_UCAST) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_UCAST) != 0)
			ifp->if_capenable ^= IFCAP_WOL_UCAST;
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_MAGIC) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
vr_watchdog(struct vr_softc *sc)
{
	struct ifnet		*ifp;

	VR_LOCK_ASSERT(sc);

	if (sc->vr_watchdog_timer == 0 || --sc->vr_watchdog_timer)
		return;

	ifp = sc->vr_ifp;
	/*
	 * Reclaim first as we don't request interrupt for every packets.
	 */
	vr_txeof(sc);
	if (sc->vr_cdata.vr_tx_cnt == 0)
		return;

	if ((sc->vr_flags & VR_F_LINK) == 0) {
		if (bootverbose)
			if_printf(sc->vr_ifp, "watchdog timeout "
			   "(missed link)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		vr_init_locked(sc);
		return;
	}

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if_printf(ifp, "watchdog timeout\n");

	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	vr_init_locked(sc);

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		vr_start_locked(ifp);
}

static void
vr_tx_start(struct vr_softc *sc)
{
	bus_addr_t	addr;
	uint8_t		cmd;

	cmd = CSR_READ_1(sc, VR_CR0);
	if ((cmd & VR_CR0_TX_ON) == 0) {
		addr = VR_TX_RING_ADDR(sc, sc->vr_cdata.vr_tx_cons);
		CSR_WRITE_4(sc, VR_TXADDR, VR_ADDR_LO(addr));
		cmd |= VR_CR0_TX_ON;
		CSR_WRITE_1(sc, VR_CR0, cmd);
	}
	if (sc->vr_cdata.vr_tx_cnt != 0) {
		sc->vr_watchdog_timer = 5;
		VR_SETBIT(sc, VR_CR0, VR_CR0_TX_GO);
	}
}

static void
vr_rx_start(struct vr_softc *sc)
{
	bus_addr_t	addr;
	uint8_t		cmd;

	cmd = CSR_READ_1(sc, VR_CR0);
	if ((cmd & VR_CR0_RX_ON) == 0) {
		addr = VR_RX_RING_ADDR(sc, sc->vr_cdata.vr_rx_cons);
		CSR_WRITE_4(sc, VR_RXADDR, VR_ADDR_LO(addr));
		cmd |= VR_CR0_RX_ON;
		CSR_WRITE_1(sc, VR_CR0, cmd);
	}
	CSR_WRITE_1(sc, VR_CR0, cmd | VR_CR0_RX_GO);
}

static int
vr_tx_stop(struct vr_softc *sc)
{
	int		i;
	uint8_t		cmd;

	cmd = CSR_READ_1(sc, VR_CR0);
	if ((cmd & VR_CR0_TX_ON) != 0) {
		cmd &= ~VR_CR0_TX_ON;
		CSR_WRITE_1(sc, VR_CR0, cmd);
		for (i = VR_TIMEOUT; i > 0; i--) {
			DELAY(5);
			cmd = CSR_READ_1(sc, VR_CR0);
			if ((cmd & VR_CR0_TX_ON) == 0)
				break;
		}
		if (i == 0)
			return (ETIMEDOUT);
	}
	return (0);
}

static int
vr_rx_stop(struct vr_softc *sc)
{
	int		i;
	uint8_t		cmd;

	cmd = CSR_READ_1(sc, VR_CR0);
	if ((cmd & VR_CR0_RX_ON) != 0) {
		cmd &= ~VR_CR0_RX_ON;
		CSR_WRITE_1(sc, VR_CR0, cmd);
		for (i = VR_TIMEOUT; i > 0; i--) {
			DELAY(5);
			cmd = CSR_READ_1(sc, VR_CR0);
			if ((cmd & VR_CR0_RX_ON) == 0)
				break;
		}
		if (i == 0)
			return (ETIMEDOUT);
	}
	return (0);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
vr_stop(struct vr_softc *sc)
{
	struct vr_txdesc	*txd;
	struct vr_rxdesc	*rxd;
	struct ifnet		*ifp;
	int			i;

	VR_LOCK_ASSERT(sc);

	ifp = sc->vr_ifp;
	sc->vr_watchdog_timer = 0;

	callout_stop(&sc->vr_stat_callout);
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	CSR_WRITE_1(sc, VR_CR0, VR_CR0_STOP);
	if (vr_rx_stop(sc) != 0)
		device_printf(sc->vr_dev, "%s: Rx shutdown error\n", __func__);
	if (vr_tx_stop(sc) != 0)
		device_printf(sc->vr_dev, "%s: Tx shutdown error\n", __func__);
	/* Clear pending interrupts. */
	CSR_WRITE_2(sc, VR_ISR, 0xFFFF);
	CSR_WRITE_2(sc, VR_IMR, 0x0000);
	CSR_WRITE_4(sc, VR_TXADDR, 0x00000000);
	CSR_WRITE_4(sc, VR_RXADDR, 0x00000000);

	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < VR_RX_RING_CNT; i++) {
		rxd = &sc->vr_cdata.vr_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->vr_cdata.vr_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->vr_cdata.vr_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
        }
	for (i = 0; i < VR_TX_RING_CNT; i++) {
		txd = &sc->vr_cdata.vr_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->vr_cdata.vr_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->vr_cdata.vr_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
        }
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
vr_shutdown(device_t dev)
{

	return (vr_suspend(dev));
}

static int
vr_suspend(device_t dev)
{
	struct vr_softc		*sc;

	sc = device_get_softc(dev);

	VR_LOCK(sc);
	vr_stop(sc);
	vr_setwol(sc);
	sc->vr_flags |= VR_F_SUSPENDED;
	VR_UNLOCK(sc);

	return (0);
}

static int
vr_resume(device_t dev)
{
	struct vr_softc		*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);

	VR_LOCK(sc);
	ifp = sc->vr_ifp;
	vr_clrwol(sc);
	vr_reset(sc);
	if (ifp->if_flags & IFF_UP)
		vr_init_locked(sc);

	sc->vr_flags &= ~VR_F_SUSPENDED;
	VR_UNLOCK(sc);

	return (0);
}

static void
vr_setwol(struct vr_softc *sc)
{
	struct ifnet		*ifp;
	int			pmc;
	uint16_t		pmstat;
	uint8_t			v;

	VR_LOCK_ASSERT(sc);

	if (sc->vr_revid < REV_ID_VT6102_A ||
	    pci_find_cap(sc->vr_dev, PCIY_PMG, &pmc) != 0)
		return;

	ifp = sc->vr_ifp;

	/* Clear WOL configuration. */
	CSR_WRITE_1(sc, VR_WOLCR_CLR, 0xFF);
	CSR_WRITE_1(sc, VR_WOLCFG_CLR, VR_WOLCFG_SAB | VR_WOLCFG_SAM);
	CSR_WRITE_1(sc, VR_PWRCSR_CLR, 0xFF);
	CSR_WRITE_1(sc, VR_PWRCFG_CLR, VR_PWRCFG_WOLEN);
	if (sc->vr_revid > REV_ID_VT6105_B0) {
		/* Newer Rhine III supports two additional patterns. */
		CSR_WRITE_1(sc, VR_WOLCFG_CLR, VR_WOLCFG_PATTERN_PAGE);
		CSR_WRITE_1(sc, VR_TESTREG_CLR, 3);
		CSR_WRITE_1(sc, VR_PWRCSR1_CLR, 3);
	}
	if ((ifp->if_capenable & IFCAP_WOL_UCAST) != 0)
		CSR_WRITE_1(sc, VR_WOLCR_SET, VR_WOLCR_UCAST);
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		CSR_WRITE_1(sc, VR_WOLCR_SET, VR_WOLCR_MAGIC);
	/*
	 * It seems that multicast wakeup frames require programming pattern
	 * registers and valid CRC as well as pattern mask for each pattern.
	 * While it's possible to setup such a pattern it would complicate
	 * WOL configuration so ignore multicast wakeup frames.
	 */
	if ((ifp->if_capenable & IFCAP_WOL) != 0) {
		CSR_WRITE_1(sc, VR_WOLCFG_SET, VR_WOLCFG_SAB | VR_WOLCFG_SAM);
		v = CSR_READ_1(sc, VR_STICKHW);
		CSR_WRITE_1(sc, VR_STICKHW, v | VR_STICKHW_WOL_ENB);
		CSR_WRITE_1(sc, VR_PWRCFG_SET, VR_PWRCFG_WOLEN);
	}

	/* Put hardware into sleep. */
	v = CSR_READ_1(sc, VR_STICKHW);
	v |= VR_STICKHW_DS0 | VR_STICKHW_DS1;
	CSR_WRITE_1(sc, VR_STICKHW, v);

	/* Request PME if WOL is requested. */
	pmstat = pci_read_config(sc->vr_dev, pmc + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->vr_dev, pmc + PCIR_POWER_STATUS, pmstat, 2);
}

static void
vr_clrwol(struct vr_softc *sc)
{
	uint8_t			v;

	VR_LOCK_ASSERT(sc);

	if (sc->vr_revid < REV_ID_VT6102_A)
		return;

	/* Take hardware out of sleep. */
	v = CSR_READ_1(sc, VR_STICKHW);
	v &= ~(VR_STICKHW_DS0 | VR_STICKHW_DS1 | VR_STICKHW_WOL_ENB);
	CSR_WRITE_1(sc, VR_STICKHW, v);

	/* Clear WOL configuration as WOL may interfere normal operation. */
	CSR_WRITE_1(sc, VR_WOLCR_CLR, 0xFF);
	CSR_WRITE_1(sc, VR_WOLCFG_CLR,
	    VR_WOLCFG_SAB | VR_WOLCFG_SAM | VR_WOLCFG_PMEOVR);
	CSR_WRITE_1(sc, VR_PWRCSR_CLR, 0xFF);
	CSR_WRITE_1(sc, VR_PWRCFG_CLR, VR_PWRCFG_WOLEN);
	if (sc->vr_revid > REV_ID_VT6105_B0) {
		/* Newer Rhine III supports two additional patterns. */
		CSR_WRITE_1(sc, VR_WOLCFG_CLR, VR_WOLCFG_PATTERN_PAGE);
		CSR_WRITE_1(sc, VR_TESTREG_CLR, 3);
		CSR_WRITE_1(sc, VR_PWRCSR1_CLR, 3);
	}
}

static int
vr_sysctl_stats(SYSCTL_HANDLER_ARGS)
{
	struct vr_softc		*sc;
	struct vr_statistics	*stat;
	int			error;
	int			result;

	result = -1;
	error = sysctl_handle_int(oidp, &result, 0, req);

	if (error != 0 || req->newptr == NULL)
		return (error);

	if (result == 1) {
		sc = (struct vr_softc *)arg1;
		stat = &sc->vr_stat;

		printf("%s statistics:\n", device_get_nameunit(sc->vr_dev));
		printf("Outbound good frames : %ju\n",
		    (uintmax_t)stat->tx_ok);
		printf("Inbound good frames : %ju\n",
		    (uintmax_t)stat->rx_ok);
		printf("Outbound errors : %u\n", stat->tx_errors);
		printf("Inbound errors : %u\n", stat->rx_errors);
		printf("Inbound no buffers : %u\n", stat->rx_no_buffers);
		printf("Inbound no mbuf clusters: %d\n", stat->rx_no_mbufs);
		printf("Inbound FIFO overflows : %d\n",
		    stat->rx_fifo_overflows);
		printf("Inbound CRC errors : %u\n", stat->rx_crc_errors);
		printf("Inbound frame alignment errors : %u\n",
		    stat->rx_alignment);
		printf("Inbound giant frames : %u\n", stat->rx_giants);
		printf("Inbound runt frames : %u\n", stat->rx_runts);
		printf("Outbound aborted with excessive collisions : %u\n",
		    stat->tx_abort);
		printf("Outbound collisions : %u\n", stat->tx_collisions);
		printf("Outbound late collisions : %u\n",
		    stat->tx_late_collisions);
		printf("Outbound underrun : %u\n", stat->tx_underrun);
		printf("PCI bus errors : %u\n", stat->bus_errors);
		printf("driver restarted due to Rx/Tx shutdown failure : %u\n",
		    stat->num_restart);
	}

	return (error);
}
