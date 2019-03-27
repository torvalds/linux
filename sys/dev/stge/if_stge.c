/*	$NetBSD: if_stge.c,v 1.32 2005/12/11 12:22:49 christos Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Device driver for the Sundance Tech. TC9021 10/100/1000
 * Ethernet controller.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_device_polling.h"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/stge/if_stgereg.h>

#define	STGE_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)

MODULE_DEPEND(stge, pci, 1, 1, 1);
MODULE_DEPEND(stge, ether, 1, 1, 1);
MODULE_DEPEND(stge, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Devices supported by this driver.
 */
static const struct stge_product {
	uint16_t	stge_vendorid;
	uint16_t	stge_deviceid;
	const char	*stge_name;
} stge_products[] = {
	{ VENDOR_SUNDANCETI,	DEVICEID_SUNDANCETI_ST1023,
	  "Sundance ST-1023 Gigabit Ethernet" },

	{ VENDOR_SUNDANCETI,	DEVICEID_SUNDANCETI_ST2021,
	  "Sundance ST-2021 Gigabit Ethernet" },

	{ VENDOR_TAMARACK,	DEVICEID_TAMARACK_TC9021,
	  "Tamarack TC9021 Gigabit Ethernet" },

	{ VENDOR_TAMARACK,	DEVICEID_TAMARACK_TC9021_ALT,
	  "Tamarack TC9021 Gigabit Ethernet" },

	/*
	 * The Sundance sample boards use the Sundance vendor ID,
	 * but the Tamarack product ID.
	 */
	{ VENDOR_SUNDANCETI,	DEVICEID_TAMARACK_TC9021,
	  "Sundance TC9021 Gigabit Ethernet" },

	{ VENDOR_SUNDANCETI,	DEVICEID_TAMARACK_TC9021_ALT,
	  "Sundance TC9021 Gigabit Ethernet" },

	{ VENDOR_DLINK,		DEVICEID_DLINK_DL4000,
	  "D-Link DL-4000 Gigabit Ethernet" },

	{ VENDOR_ANTARES,	DEVICEID_ANTARES_TC9021,
	  "Antares Gigabit Ethernet" }
};

static int	stge_probe(device_t);
static int	stge_attach(device_t);
static int	stge_detach(device_t);
static int	stge_shutdown(device_t);
static int	stge_suspend(device_t);
static int	stge_resume(device_t);

static int	stge_encap(struct stge_softc *, struct mbuf **);
static void	stge_start(struct ifnet *);
static void	stge_start_locked(struct ifnet *);
static void	stge_watchdog(struct stge_softc *);
static int	stge_ioctl(struct ifnet *, u_long, caddr_t);
static void	stge_init(void *);
static void	stge_init_locked(struct stge_softc *);
static void	stge_vlan_setup(struct stge_softc *);
static void	stge_stop(struct stge_softc *);
static void	stge_start_tx(struct stge_softc *);
static void	stge_start_rx(struct stge_softc *);
static void	stge_stop_tx(struct stge_softc *);
static void	stge_stop_rx(struct stge_softc *);

static void	stge_reset(struct stge_softc *, uint32_t);
static int	stge_eeprom_wait(struct stge_softc *);
static void	stge_read_eeprom(struct stge_softc *, int, uint16_t *);
static void	stge_tick(void *);
static void	stge_stats_update(struct stge_softc *);
static void	stge_set_filter(struct stge_softc *);
static void	stge_set_multi(struct stge_softc *);

static void	stge_link_task(void *, int);
static void	stge_intr(void *);
static __inline int stge_tx_error(struct stge_softc *);
static void	stge_txeof(struct stge_softc *);
static int	stge_rxeof(struct stge_softc *);
static __inline void stge_discard_rxbuf(struct stge_softc *, int);
static int	stge_newbuf(struct stge_softc *, int);
#ifndef __NO_STRICT_ALIGNMENT
static __inline struct mbuf *stge_fixup_rx(struct stge_softc *, struct mbuf *);
#endif

static int	stge_miibus_readreg(device_t, int, int);
static int	stge_miibus_writereg(device_t, int, int, int);
static void	stge_miibus_statchg(device_t);
static int	stge_mediachange(struct ifnet *);
static void	stge_mediastatus(struct ifnet *, struct ifmediareq *);

static void	stge_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int	stge_dma_alloc(struct stge_softc *);
static void	stge_dma_free(struct stge_softc *);
static void	stge_dma_wait(struct stge_softc *);
static void	stge_init_tx_ring(struct stge_softc *);
static int	stge_init_rx_ring(struct stge_softc *);
#ifdef DEVICE_POLLING
static int	stge_poll(struct ifnet *, enum poll_cmd, int);
#endif

static void	stge_setwol(struct stge_softc *);
static int	sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int	sysctl_hw_stge_rxint_nframe(SYSCTL_HANDLER_ARGS);
static int	sysctl_hw_stge_rxint_dmawait(SYSCTL_HANDLER_ARGS);

/*
 * MII bit-bang glue
 */
static uint32_t stge_mii_bitbang_read(device_t);
static void	stge_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops stge_mii_bitbang_ops = {
	stge_mii_bitbang_read,
	stge_mii_bitbang_write,
	{
		PC_MgmtData,		/* MII_BIT_MDO */
		PC_MgmtData,		/* MII_BIT_MDI */
		PC_MgmtClk,		/* MII_BIT_MDC */
		PC_MgmtDir,		/* MII_BIT_DIR_HOST_PHY */
		0,			/* MII_BIT_DIR_PHY_HOST */
	}
};

static device_method_t stge_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		stge_probe),
	DEVMETHOD(device_attach,	stge_attach),
	DEVMETHOD(device_detach,	stge_detach),
	DEVMETHOD(device_shutdown,	stge_shutdown),
	DEVMETHOD(device_suspend,	stge_suspend),
	DEVMETHOD(device_resume,	stge_resume),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	stge_miibus_readreg),
	DEVMETHOD(miibus_writereg,	stge_miibus_writereg),
	DEVMETHOD(miibus_statchg,	stge_miibus_statchg),

	DEVMETHOD_END
};

static driver_t stge_driver = {
	"stge",
	stge_methods,
	sizeof(struct stge_softc)
};

static devclass_t stge_devclass;

DRIVER_MODULE(stge, pci, stge_driver, stge_devclass, 0, 0);
DRIVER_MODULE(miibus, stge, miibus_driver, miibus_devclass, 0, 0);

static struct resource_spec stge_res_spec_io[] = {
	{ SYS_RES_IOPORT,	PCIR_BAR(0),	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

static struct resource_spec stge_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	PCIR_BAR(1),	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

/*
 * stge_mii_bitbang_read: [mii bit-bang interface function]
 *
 *	Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
stge_mii_bitbang_read(device_t dev)
{
	struct stge_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = CSR_READ_1(sc, STGE_PhyCtrl);
	CSR_BARRIER(sc, STGE_PhyCtrl, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return (val);
}

/*
 * stge_mii_bitbang_write: [mii big-bang interface function]
 *
 *	Write the MII serial port for the MII bit-bang module.
 */
static void
stge_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct stge_softc *sc;

	sc = device_get_softc(dev);

	CSR_WRITE_1(sc, STGE_PhyCtrl, val);
	CSR_BARRIER(sc, STGE_PhyCtrl, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

/*
 * sc_miibus_readreg:	[mii interface function]
 *
 *	Read a PHY register on the MII of the TC9021.
 */
static int
stge_miibus_readreg(device_t dev, int phy, int reg)
{
	struct stge_softc *sc;
	int error, val;

	sc = device_get_softc(dev);

	if (reg == STGE_PhyCtrl) {
		/* XXX allow ip1000phy read STGE_PhyCtrl register. */
		STGE_MII_LOCK(sc);
		error = CSR_READ_1(sc, STGE_PhyCtrl);
		STGE_MII_UNLOCK(sc);
		return (error);
	}

	STGE_MII_LOCK(sc);
	val = mii_bitbang_readreg(dev, &stge_mii_bitbang_ops, phy, reg);
	STGE_MII_UNLOCK(sc);
	return (val);
}

/*
 * stge_miibus_writereg:	[mii interface function]
 *
 *	Write a PHY register on the MII of the TC9021.
 */
static int
stge_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct stge_softc *sc;

	sc = device_get_softc(dev);

	STGE_MII_LOCK(sc);
	mii_bitbang_writereg(dev, &stge_mii_bitbang_ops, phy, reg, val);
	STGE_MII_UNLOCK(sc);
	return (0);
}

/*
 * stge_miibus_statchg:	[mii interface function]
 *
 *	Callback from MII layer when media changes.
 */
static void
stge_miibus_statchg(device_t dev)
{
	struct stge_softc *sc;

	sc = device_get_softc(dev);
	taskqueue_enqueue(taskqueue_swi, &sc->sc_link_task);
}

/*
 * stge_mediastatus:	[ifmedia interface function]
 *
 *	Get the current interface media status.
 */
static void
stge_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct stge_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->sc_miibus);

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
}

/*
 * stge_mediachange:	[ifmedia interface function]
 *
 *	Set hardware to newly-selected media.
 */
static int
stge_mediachange(struct ifnet *ifp)
{
	struct stge_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->sc_miibus);
	mii_mediachg(mii);

	return (0);
}

static int
stge_eeprom_wait(struct stge_softc *sc)
{
	int i;

	for (i = 0; i < STGE_TIMEOUT; i++) {
		DELAY(1000);
		if ((CSR_READ_2(sc, STGE_EepromCtrl) & EC_EepromBusy) == 0)
			return (0);
	}
	return (1);
}

/*
 * stge_read_eeprom:
 *
 *	Read data from the serial EEPROM.
 */
static void
stge_read_eeprom(struct stge_softc *sc, int offset, uint16_t *data)
{

	if (stge_eeprom_wait(sc))
		device_printf(sc->sc_dev, "EEPROM failed to come ready\n");

	CSR_WRITE_2(sc, STGE_EepromCtrl,
	    EC_EepromAddress(offset) | EC_EepromOpcode(EC_OP_RR));
	if (stge_eeprom_wait(sc))
		device_printf(sc->sc_dev, "EEPROM read timed out\n");
	*data = CSR_READ_2(sc, STGE_EepromData);
}


static int
stge_probe(device_t dev)
{
	const struct stge_product *sp;
	int i;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	sp = stge_products;
	for (i = 0; i < nitems(stge_products); i++, sp++) {
		if (vendor == sp->stge_vendorid &&
		    devid == sp->stge_deviceid) {
			device_set_desc(dev, sp->stge_name);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
stge_attach(device_t dev)
{
	struct stge_softc *sc;
	struct ifnet *ifp;
	uint8_t enaddr[ETHER_ADDR_LEN];
	int error, flags, i;
	uint16_t cmd;
	uint32_t val;

	error = 0;
	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	mtx_init(&sc->sc_mii_mtx, "stge_mii_mutex", NULL, MTX_DEF);
	callout_init_mtx(&sc->sc_tick_ch, &sc->sc_mtx, 0);
	TASK_INIT(&sc->sc_link_task, 0, stge_link_task, sc);

	/*
	 * Map the device.
	 */
	pci_enable_busmaster(dev);
	cmd = pci_read_config(dev, PCIR_COMMAND, 2);
	val = pci_read_config(dev, PCIR_BAR(1), 4);
	if (PCI_BAR_IO(val))
		sc->sc_spec = stge_res_spec_mem;
	else {
		val = pci_read_config(dev, PCIR_BAR(0), 4);
		if (!PCI_BAR_IO(val)) {
			device_printf(sc->sc_dev, "couldn't locate IO BAR\n");
			error = ENXIO;
			goto fail;
		}
		sc->sc_spec = stge_res_spec_io;
	}
	error = bus_alloc_resources(dev, sc->sc_spec, sc->sc_res);
	if (error != 0) {
		device_printf(dev, "couldn't allocate %s resources\n",
		    sc->sc_spec == stge_res_spec_mem ? "memory" : "I/O");
		goto fail;
	}
	sc->sc_rev = pci_get_revid(dev);

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "rxint_nframe", CTLTYPE_INT|CTLFLAG_RW, &sc->sc_rxint_nframe, 0,
	    sysctl_hw_stge_rxint_nframe, "I", "stge rx interrupt nframe");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "rxint_dmawait", CTLTYPE_INT|CTLFLAG_RW, &sc->sc_rxint_dmawait, 0,
	    sysctl_hw_stge_rxint_dmawait, "I", "stge rx interrupt dmawait");

	/* Pull in device tunables. */
	sc->sc_rxint_nframe = STGE_RXINT_NFRAME_DEFAULT;
	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "rxint_nframe", &sc->sc_rxint_nframe);
	if (error == 0) {
		if (sc->sc_rxint_nframe < STGE_RXINT_NFRAME_MIN ||
		    sc->sc_rxint_nframe > STGE_RXINT_NFRAME_MAX) {
			device_printf(dev, "rxint_nframe value out of range; "
			    "using default: %d\n", STGE_RXINT_NFRAME_DEFAULT);
			sc->sc_rxint_nframe = STGE_RXINT_NFRAME_DEFAULT;
		}
	}

	sc->sc_rxint_dmawait = STGE_RXINT_DMAWAIT_DEFAULT;
	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "rxint_dmawait", &sc->sc_rxint_dmawait);
	if (error == 0) {
		if (sc->sc_rxint_dmawait < STGE_RXINT_DMAWAIT_MIN ||
		    sc->sc_rxint_dmawait > STGE_RXINT_DMAWAIT_MAX) {
			device_printf(dev, "rxint_dmawait value out of range; "
			    "using default: %d\n", STGE_RXINT_DMAWAIT_DEFAULT);
			sc->sc_rxint_dmawait = STGE_RXINT_DMAWAIT_DEFAULT;
		}
	}

	if ((error = stge_dma_alloc(sc)) != 0)
		goto fail;

	/*
	 * Determine if we're copper or fiber.  It affects how we
	 * reset the card.
	 */
	if (CSR_READ_4(sc, STGE_AsicCtrl) & AC_PhyMedia)
		sc->sc_usefiber = 1;
	else
		sc->sc_usefiber = 0;

	/* Load LED configuration from EEPROM. */
	stge_read_eeprom(sc, STGE_EEPROM_LEDMode, &sc->sc_led);

	/*
	 * Reset the chip to a known state.
	 */
	STGE_LOCK(sc);
	stge_reset(sc, STGE_RESET_FULL);
	STGE_UNLOCK(sc);

	/*
	 * Reading the station address from the EEPROM doesn't seem
	 * to work, at least on my sample boards.  Instead, since
	 * the reset sequence does AutoInit, read it from the station
	 * address registers. For Sundance 1023 you can only read it
	 * from EEPROM.
	 */
	if (pci_get_device(dev) != DEVICEID_SUNDANCETI_ST1023) {
		uint16_t v;

		v = CSR_READ_2(sc, STGE_StationAddress0);
		enaddr[0] = v & 0xff;
		enaddr[1] = v >> 8;
		v = CSR_READ_2(sc, STGE_StationAddress1);
		enaddr[2] = v & 0xff;
		enaddr[3] = v >> 8;
		v = CSR_READ_2(sc, STGE_StationAddress2);
		enaddr[4] = v & 0xff;
		enaddr[5] = v >> 8;
		sc->sc_stge1023 = 0;
	} else {
		uint16_t myaddr[ETHER_ADDR_LEN / 2];
		for (i = 0; i <ETHER_ADDR_LEN / 2; i++) {
			stge_read_eeprom(sc, STGE_EEPROM_StationAddress0 + i,
			    &myaddr[i]);
			myaddr[i] = le16toh(myaddr[i]);
		}
		bcopy(myaddr, enaddr, sizeof(enaddr));
		sc->sc_stge1023 = 1;
	}

	ifp = sc->sc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc->sc_dev, "failed to if_alloc()\n");
		error = ENXIO;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = stge_ioctl;
	ifp->if_start = stge_start;
	ifp->if_init = stge_init;
	ifp->if_snd.ifq_drv_maxlen = STGE_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	/* Revision B3 and earlier chips have checksum bug. */
	if (sc->sc_rev >= 0x0c) {
		ifp->if_hwassist = STGE_CSUM_FEATURES;
		ifp->if_capabilities = IFCAP_HWCSUM;
	} else {
		ifp->if_hwassist = 0;
		ifp->if_capabilities = 0;
	}
	ifp->if_capabilities |= IFCAP_WOL_MAGIC;
	ifp->if_capenable = ifp->if_capabilities;

	/*
	 * Read some important bits from the PhyCtrl register.
	 */
	sc->sc_PhyCtrl = CSR_READ_1(sc, STGE_PhyCtrl) &
	    (PC_PhyDuplexPolarity | PC_PhyLnkPolarity);

	/* Set up MII bus. */
	flags = MIIF_DOPAUSE;
	if (sc->sc_rev >= 0x40 && sc->sc_rev <= 0x4e)
		flags |= MIIF_MACPRIV0;
	error = mii_attach(sc->sc_dev, &sc->sc_miibus, ifp, stge_mediachange,
	    stge_mediastatus, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY,
	    flags);
	if (error != 0) {
		device_printf(sc->sc_dev, "attaching PHYs failed\n");
		goto fail;
	}

	ether_ifattach(ifp, enaddr);

	/* VLAN capability setup */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING;
	if (sc->sc_rev >= 0x0c)
		ifp->if_capabilities |= IFCAP_VLAN_HWCSUM;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	ifp->if_capabilities |= IFCAP_POLLING;
#endif
	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/*
	 * The manual recommends disabling early transmit, so we
	 * do.  It's disabled anyway, if using IP checksumming,
	 * since the entire packet must be in the FIFO in order
	 * for the chip to perform the checksum.
	 */
	sc->sc_txthresh = 0x0fff;

	/*
	 * Disable MWI if the PCI layer tells us to.
	 */
	sc->sc_DMACtrl = 0;
	if ((cmd & PCIM_CMD_MWRICEN) == 0)
		sc->sc_DMACtrl |= DMAC_MWIDisable;

	/*
	 * Hookup IRQ
	 */
	error = bus_setup_intr(dev, sc->sc_res[1], INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, stge_intr, sc, &sc->sc_ih);
	if (error != 0) {
		ether_ifdetach(ifp);
		device_printf(sc->sc_dev, "couldn't set up IRQ\n");
		sc->sc_ifp = NULL;
		goto fail;
	}

fail:
	if (error != 0)
		stge_detach(dev);

	return (error);
}

static int
stge_detach(device_t dev)
{
	struct stge_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	ifp = sc->sc_ifp;
#ifdef DEVICE_POLLING
	if (ifp && ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif
	if (device_is_attached(dev)) {
		STGE_LOCK(sc);
		/* XXX */
		sc->sc_detach = 1;
		stge_stop(sc);
		STGE_UNLOCK(sc);
		callout_drain(&sc->sc_tick_ch);
		taskqueue_drain(taskqueue_swi, &sc->sc_link_task);
		ether_ifdetach(ifp);
	}

	if (sc->sc_miibus != NULL) {
		device_delete_child(dev, sc->sc_miibus);
		sc->sc_miibus = NULL;
	}
	bus_generic_detach(dev);
	stge_dma_free(sc);

	if (ifp != NULL) {
		if_free(ifp);
		sc->sc_ifp = NULL;
	}

	if (sc->sc_ih) {
		bus_teardown_intr(dev, sc->sc_res[1], sc->sc_ih);
		sc->sc_ih = NULL;
	}
	bus_release_resources(dev, sc->sc_spec, sc->sc_res);

	mtx_destroy(&sc->sc_mii_mtx);
	mtx_destroy(&sc->sc_mtx);

	return (0);
}

struct stge_dmamap_arg {
	bus_addr_t	stge_busaddr;
};

static void
stge_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct stge_dmamap_arg *ctx;

	if (error != 0)
		return;

	ctx = (struct stge_dmamap_arg *)arg;
	ctx->stge_busaddr = segs[0].ds_addr;
}

static int
stge_dma_alloc(struct stge_softc *sc)
{
	struct stge_dmamap_arg ctx;
	struct stge_txdesc *txd;
	struct stge_rxdesc *rxd;
	int error, i;

	/* create parent tag. */
	error = bus_dma_tag_create(bus_get_dma_tag(sc->sc_dev),/* parent */
		    1, 0,			/* algnmnt, boundary */
		    STGE_DMA_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
		    0,				/* nsegments */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc->sc_cdata.stge_parent_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to create parent DMA tag\n");
		goto fail;
	}
	/* create tag for Tx ring. */
	error = bus_dma_tag_create(sc->sc_cdata.stge_parent_tag,/* parent */
		    STGE_RING_ALIGN, 0,		/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    STGE_TX_RING_SZ,		/* maxsize */
		    1,				/* nsegments */
		    STGE_TX_RING_SZ,		/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc->sc_cdata.stge_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "failed to allocate Tx ring DMA tag\n");
		goto fail;
	}

	/* create tag for Rx ring. */
	error = bus_dma_tag_create(sc->sc_cdata.stge_parent_tag,/* parent */
		    STGE_RING_ALIGN, 0,		/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    STGE_RX_RING_SZ,		/* maxsize */
		    1,				/* nsegments */
		    STGE_RX_RING_SZ,		/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc->sc_cdata.stge_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "failed to allocate Rx ring DMA tag\n");
		goto fail;
	}

	/* create tag for Tx buffers. */
	error = bus_dma_tag_create(sc->sc_cdata.stge_parent_tag,/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MCLBYTES * STGE_MAXTXSEGS,	/* maxsize */
		    STGE_MAXTXSEGS,		/* nsegments */
		    MCLBYTES,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc->sc_cdata.stge_tx_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to allocate Tx DMA tag\n");
		goto fail;
	}

	/* create tag for Rx buffers. */
	error = bus_dma_tag_create(sc->sc_cdata.stge_parent_tag,/* parent */
		    1, 0,			/* algnmnt, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MCLBYTES,			/* maxsize */
		    1,				/* nsegments */
		    MCLBYTES,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc->sc_cdata.stge_rx_tag);
	if (error != 0) {
		device_printf(sc->sc_dev, "failed to allocate Rx DMA tag\n");
		goto fail;
	}

	/* allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->sc_cdata.stge_tx_ring_tag,
	    (void **)&sc->sc_rdata.stge_tx_ring, BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->sc_cdata.stge_tx_ring_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.stge_busaddr = 0;
	error = bus_dmamap_load(sc->sc_cdata.stge_tx_ring_tag,
	    sc->sc_cdata.stge_tx_ring_map, sc->sc_rdata.stge_tx_ring,
	    STGE_TX_RING_SZ, stge_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0 || ctx.stge_busaddr == 0) {
		device_printf(sc->sc_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc->sc_rdata.stge_tx_ring_paddr = ctx.stge_busaddr;

	/* allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->sc_cdata.stge_rx_ring_tag,
	    (void **)&sc->sc_rdata.stge_rx_ring, BUS_DMA_NOWAIT |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc->sc_cdata.stge_rx_ring_map);
	if (error != 0) {
		device_printf(sc->sc_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.stge_busaddr = 0;
	error = bus_dmamap_load(sc->sc_cdata.stge_rx_ring_tag,
	    sc->sc_cdata.stge_rx_ring_map, sc->sc_rdata.stge_rx_ring,
	    STGE_RX_RING_SZ, stge_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0 || ctx.stge_busaddr == 0) {
		device_printf(sc->sc_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc->sc_rdata.stge_rx_ring_paddr = ctx.stge_busaddr;

	/* create DMA maps for Tx buffers. */
	for (i = 0; i < STGE_TX_RING_CNT; i++) {
		txd = &sc->sc_cdata.stge_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = 0;
		error = bus_dmamap_create(sc->sc_cdata.stge_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->sc_cdata.stge_rx_tag, 0,
	    &sc->sc_cdata.stge_rx_sparemap)) != 0) {
		device_printf(sc->sc_dev, "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < STGE_RX_RING_CNT; i++) {
		rxd = &sc->sc_cdata.stge_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = 0;
		error = bus_dmamap_create(sc->sc_cdata.stge_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->sc_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
stge_dma_free(struct stge_softc *sc)
{
	struct stge_txdesc *txd;
	struct stge_rxdesc *rxd;
	int i;

	/* Tx ring */
	if (sc->sc_cdata.stge_tx_ring_tag) {
		if (sc->sc_rdata.stge_tx_ring_paddr)
			bus_dmamap_unload(sc->sc_cdata.stge_tx_ring_tag,
			    sc->sc_cdata.stge_tx_ring_map);
		if (sc->sc_rdata.stge_tx_ring)
			bus_dmamem_free(sc->sc_cdata.stge_tx_ring_tag,
			    sc->sc_rdata.stge_tx_ring,
			    sc->sc_cdata.stge_tx_ring_map);
		sc->sc_rdata.stge_tx_ring = NULL;
		sc->sc_rdata.stge_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->sc_cdata.stge_tx_ring_tag);
		sc->sc_cdata.stge_tx_ring_tag = NULL;
	}
	/* Rx ring */
	if (sc->sc_cdata.stge_rx_ring_tag) {
		if (sc->sc_rdata.stge_rx_ring_paddr)
			bus_dmamap_unload(sc->sc_cdata.stge_rx_ring_tag,
			    sc->sc_cdata.stge_rx_ring_map);
		if (sc->sc_rdata.stge_rx_ring)
			bus_dmamem_free(sc->sc_cdata.stge_rx_ring_tag,
			    sc->sc_rdata.stge_rx_ring,
			    sc->sc_cdata.stge_rx_ring_map);
		sc->sc_rdata.stge_rx_ring = NULL;
		sc->sc_rdata.stge_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc->sc_cdata.stge_rx_ring_tag);
		sc->sc_cdata.stge_rx_ring_tag = NULL;
	}
	/* Tx buffers */
	if (sc->sc_cdata.stge_tx_tag) {
		for (i = 0; i < STGE_TX_RING_CNT; i++) {
			txd = &sc->sc_cdata.stge_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc->sc_cdata.stge_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = 0;
			}
		}
		bus_dma_tag_destroy(sc->sc_cdata.stge_tx_tag);
		sc->sc_cdata.stge_tx_tag = NULL;
	}
	/* Rx buffers */
	if (sc->sc_cdata.stge_rx_tag) {
		for (i = 0; i < STGE_RX_RING_CNT; i++) {
			rxd = &sc->sc_cdata.stge_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc->sc_cdata.stge_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = 0;
			}
		}
		if (sc->sc_cdata.stge_rx_sparemap) {
			bus_dmamap_destroy(sc->sc_cdata.stge_rx_tag,
			    sc->sc_cdata.stge_rx_sparemap);
			sc->sc_cdata.stge_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc->sc_cdata.stge_rx_tag);
		sc->sc_cdata.stge_rx_tag = NULL;
	}

	if (sc->sc_cdata.stge_parent_tag) {
		bus_dma_tag_destroy(sc->sc_cdata.stge_parent_tag);
		sc->sc_cdata.stge_parent_tag = NULL;
	}
}

/*
 * stge_shutdown:
 *
 *	Make sure the interface is stopped at reboot time.
 */
static int
stge_shutdown(device_t dev)
{

	return (stge_suspend(dev));
}

static void
stge_setwol(struct stge_softc *sc)
{
	struct ifnet *ifp;
	uint8_t v;

	STGE_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;
	v = CSR_READ_1(sc, STGE_WakeEvent);
	/* Disable all WOL bits. */
	v &= ~(WE_WakePktEnable | WE_MagicPktEnable | WE_LinkEventEnable |
	    WE_WakeOnLanEnable);
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		v |= WE_MagicPktEnable | WE_WakeOnLanEnable;
	CSR_WRITE_1(sc, STGE_WakeEvent, v);
	/* Reset Tx and prevent transmission. */
	CSR_WRITE_4(sc, STGE_AsicCtrl,
	    CSR_READ_4(sc, STGE_AsicCtrl) | AC_TxReset);
	/*
	 * TC9021 automatically reset link speed to 100Mbps when it's put
	 * into sleep so there is no need to try to resetting link speed.
	 */
}

static int
stge_suspend(device_t dev)
{
	struct stge_softc *sc;

	sc = device_get_softc(dev);

	STGE_LOCK(sc);
	stge_stop(sc);
	sc->sc_suspended = 1;
	stge_setwol(sc);
	STGE_UNLOCK(sc);

	return (0);
}

static int
stge_resume(device_t dev)
{
	struct stge_softc *sc;
	struct ifnet *ifp;
	uint8_t v;

	sc = device_get_softc(dev);

	STGE_LOCK(sc);
	/*
	 * Clear WOL bits, so special frames wouldn't interfere
	 * normal Rx operation anymore.
	 */
	v = CSR_READ_1(sc, STGE_WakeEvent);
	v &= ~(WE_WakePktEnable | WE_MagicPktEnable | WE_LinkEventEnable |
	    WE_WakeOnLanEnable);
	CSR_WRITE_1(sc, STGE_WakeEvent, v);
	ifp = sc->sc_ifp;
	if (ifp->if_flags & IFF_UP)
		stge_init_locked(sc);

	sc->sc_suspended = 0;
	STGE_UNLOCK(sc);

	return (0);
}

static void
stge_dma_wait(struct stge_softc *sc)
{
	int i;

	for (i = 0; i < STGE_TIMEOUT; i++) {
		DELAY(2);
		if ((CSR_READ_4(sc, STGE_DMACtrl) & DMAC_TxDMAInProg) == 0)
			break;
	}

	if (i == STGE_TIMEOUT)
		device_printf(sc->sc_dev, "DMA wait timed out\n");
}

static int
stge_encap(struct stge_softc *sc, struct mbuf **m_head)
{
	struct stge_txdesc *txd;
	struct stge_tfd *tfd;
	struct mbuf *m;
	bus_dma_segment_t txsegs[STGE_MAXTXSEGS];
	int error, i, nsegs, si;
	uint64_t csum_flags, tfc;

	STGE_LOCK_ASSERT(sc);

	if ((txd = STAILQ_FIRST(&sc->sc_cdata.stge_txfreeq)) == NULL)
		return (ENOBUFS);

	error =  bus_dmamap_load_mbuf_sg(sc->sc_cdata.stge_tx_tag,
	    txd->tx_dmamap, *m_head, txsegs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, STGE_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->sc_cdata.stge_tx_tag,
		    txd->tx_dmamap, *m_head, txsegs, &nsegs, 0);
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

	m = *m_head;
	csum_flags = 0;
	if ((m->m_pkthdr.csum_flags & STGE_CSUM_FEATURES) != 0) {
		if (m->m_pkthdr.csum_flags & CSUM_IP)
			csum_flags |= TFD_IPChecksumEnable;
		if (m->m_pkthdr.csum_flags & CSUM_TCP)
			csum_flags |= TFD_TCPChecksumEnable;
		else if (m->m_pkthdr.csum_flags & CSUM_UDP)
			csum_flags |= TFD_UDPChecksumEnable;
	}

	si = sc->sc_cdata.stge_tx_prod;
	tfd = &sc->sc_rdata.stge_tx_ring[si];
	for (i = 0; i < nsegs; i++)
		tfd->tfd_frags[i].frag_word0 =
		    htole64(FRAG_ADDR(txsegs[i].ds_addr) |
		    FRAG_LEN(txsegs[i].ds_len));
	sc->sc_cdata.stge_tx_cnt++;

	tfc = TFD_FrameId(si) | TFD_WordAlign(TFD_WordAlign_disable) |
	    TFD_FragCount(nsegs) | csum_flags;
	if (sc->sc_cdata.stge_tx_cnt >= STGE_TX_HIWAT)
		tfc |= TFD_TxDMAIndicate;

	/* Update producer index. */
	sc->sc_cdata.stge_tx_prod = (si + 1) % STGE_TX_RING_CNT;

	/* Check if we have a VLAN tag to insert. */
	if (m->m_flags & M_VLANTAG)
		tfc |= (TFD_VLANTagInsert | TFD_VID(m->m_pkthdr.ether_vtag));
	tfd->tfd_control = htole64(tfc);

	/* Update Tx Queue. */
	STAILQ_REMOVE_HEAD(&sc->sc_cdata.stge_txfreeq, tx_q);
	STAILQ_INSERT_TAIL(&sc->sc_cdata.stge_txbusyq, txd, tx_q);
	txd->tx_m = m;

	/* Sync descriptors. */
	bus_dmamap_sync(sc->sc_cdata.stge_tx_tag, txd->tx_dmamap,
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc->sc_cdata.stge_tx_ring_tag,
	    sc->sc_cdata.stge_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * stge_start:		[ifnet interface function]
 *
 *	Start packet transmission on the interface.
 */
static void
stge_start(struct ifnet *ifp)
{
	struct stge_softc *sc;

	sc = ifp->if_softc;
	STGE_LOCK(sc);
	stge_start_locked(ifp);
	STGE_UNLOCK(sc);
}

static void
stge_start_locked(struct ifnet *ifp)
{
        struct stge_softc *sc;
        struct mbuf *m_head;
	int enq;

	sc = ifp->if_softc;

	STGE_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING|IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || sc->sc_link == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		if (sc->sc_cdata.stge_tx_cnt >= STGE_TX_HIWAT) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			break;
		}

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (stge_encap(sc, &m_head)) {
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
		/* Transmit */
		CSR_WRITE_4(sc, STGE_DMACtrl, DMAC_TxDMAPollNow);

		/* Set a timeout in case the chip goes out to lunch. */
		sc->sc_watchdog_timer = 5;
	}
}

/*
 * stge_watchdog:
 *
 *	Watchdog timer handler.
 */
static void
stge_watchdog(struct stge_softc *sc)
{
	struct ifnet *ifp;

	STGE_LOCK_ASSERT(sc);

	if (sc->sc_watchdog_timer == 0 || --sc->sc_watchdog_timer)
		return;

	ifp = sc->sc_ifp;
	if_printf(sc->sc_ifp, "device timeout\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	stge_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		stge_start_locked(ifp);
}

/*
 * stge_ioctl:		[ifnet interface function]
 *
 *	Handle control requests from the operator.
 */
static int
stge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct stge_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	int error, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;
	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > STGE_JUMBO_MTU)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
			STGE_LOCK(sc);
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				stge_init_locked(sc);
			}
			STGE_UNLOCK(sc);
		}
		break;
	case SIOCSIFFLAGS:
		STGE_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if (((ifp->if_flags ^ sc->sc_if_flags)
				    & IFF_PROMISC) != 0)
					stge_set_filter(sc);
			} else {
				if (sc->sc_detach == 0)
					stge_init_locked(sc);
			}
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				stge_stop(sc);
		}
		sc->sc_if_flags = ifp->if_flags;
		STGE_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		STGE_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			stge_set_multi(sc);
		STGE_UNLOCK(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->sc_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	case SIOCSIFCAP:
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0) {
			if ((ifr->ifr_reqcap & IFCAP_POLLING) != 0) {
				error = ether_poll_register(stge_poll, ifp);
				if (error != 0)
					break;
				STGE_LOCK(sc);
				CSR_WRITE_2(sc, STGE_IntEnable, 0);
				ifp->if_capenable |= IFCAP_POLLING;
				STGE_UNLOCK(sc);
			} else {
				error = ether_poll_deregister(ifp);
				if (error != 0)
					break;
				STGE_LOCK(sc);
				CSR_WRITE_2(sc, STGE_IntEnable,
				    sc->sc_IntEnable);
				ifp->if_capenable &= ~IFCAP_POLLING;
				STGE_UNLOCK(sc);
			}
		}
#endif
		if ((mask & IFCAP_HWCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_HWCSUM;
			if ((IFCAP_HWCSUM & ifp->if_capenable) != 0 &&
			    (IFCAP_HWCSUM & ifp->if_capabilities) != 0)
				ifp->if_hwassist = STGE_CSUM_FEATURES;
			else
				ifp->if_hwassist = 0;
		}
		if ((mask & IFCAP_WOL) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL) != 0) {
			if ((mask & IFCAP_WOL_MAGIC) != 0)
				ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		}
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				STGE_LOCK(sc);
				stge_vlan_setup(sc);
				STGE_UNLOCK(sc);
			}
		}
		VLAN_CAPABILITIES(ifp);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
stge_link_task(void *arg, int pending)
{
	struct stge_softc *sc;
	struct mii_data *mii;
	uint32_t v, ac;
	int i;

	sc = (struct stge_softc *)arg;
	STGE_LOCK(sc);

	mii = device_get_softc(sc->sc_miibus);
	if (mii->mii_media_status & IFM_ACTIVE) {
		if (IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE)
			sc->sc_link = 1;
	} else
		sc->sc_link = 0;

	sc->sc_MACCtrl = 0;
	if (((mii->mii_media_active & IFM_GMASK) & IFM_FDX) != 0)
		sc->sc_MACCtrl |= MC_DuplexSelect;
	if (((mii->mii_media_active & IFM_GMASK) & IFM_ETH_RXPAUSE) != 0)
		sc->sc_MACCtrl |= MC_RxFlowControlEnable;
	if (((mii->mii_media_active & IFM_GMASK) & IFM_ETH_TXPAUSE) != 0)
		sc->sc_MACCtrl |= MC_TxFlowControlEnable;
	/*
	 * Update STGE_MACCtrl register depending on link status.
	 * (duplex, flow control etc)
	 */
	v = ac = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	v &= ~(MC_DuplexSelect|MC_RxFlowControlEnable|MC_TxFlowControlEnable);
	v |= sc->sc_MACCtrl;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	if (((ac ^ sc->sc_MACCtrl) & MC_DuplexSelect) != 0) {
		/* Duplex setting changed, reset Tx/Rx functions. */
		ac = CSR_READ_4(sc, STGE_AsicCtrl);
		ac |= AC_TxReset | AC_RxReset;
		CSR_WRITE_4(sc, STGE_AsicCtrl, ac);
		for (i = 0; i < STGE_TIMEOUT; i++) {
			DELAY(100);
			if ((CSR_READ_4(sc, STGE_AsicCtrl) & AC_ResetBusy) == 0)
				break;
		}
		if (i == STGE_TIMEOUT)
			device_printf(sc->sc_dev, "reset failed to complete\n");
	}
	STGE_UNLOCK(sc);
}

static __inline int
stge_tx_error(struct stge_softc *sc)
{
	uint32_t txstat;
	int error;

	for (error = 0;;) {
		txstat = CSR_READ_4(sc, STGE_TxStatus);
		if ((txstat & TS_TxComplete) == 0)
			break;
		/* Tx underrun */
		if ((txstat & TS_TxUnderrun) != 0) {
			/*
			 * XXX
			 * There should be a more better way to recover
			 * from Tx underrun instead of a full reset.
			 */
			if (sc->sc_nerr++ < STGE_MAXERR)
				device_printf(sc->sc_dev, "Tx underrun, "
				    "resetting...\n");
			if (sc->sc_nerr == STGE_MAXERR)
				device_printf(sc->sc_dev, "too many errors; "
				    "not reporting any more\n");
			error = -1;
			break;
		}
		/* Maximum/Late collisions, Re-enable Tx MAC. */
		if ((txstat & (TS_MaxCollisions|TS_LateCollision)) != 0)
			CSR_WRITE_4(sc, STGE_MACCtrl,
			    (CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK) |
			    MC_TxEnable);
	}

	return (error);
}

/*
 * stge_intr:
 *
 *	Interrupt service routine.
 */
static void
stge_intr(void *arg)
{
	struct stge_softc *sc;
	struct ifnet *ifp;
	int reinit;
	uint16_t status;

	sc = (struct stge_softc *)arg;
	ifp = sc->sc_ifp;

	STGE_LOCK(sc);

#ifdef DEVICE_POLLING
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		goto done_locked;
#endif
	status = CSR_READ_2(sc, STGE_IntStatus);
	if (sc->sc_suspended || (status & IS_InterruptStatus) == 0)
		goto done_locked;

	/* Disable interrupts. */
	for (reinit = 0;;) {
		status = CSR_READ_2(sc, STGE_IntStatusAck);
		status &= sc->sc_IntEnable;
		if (status == 0)
			break;
		/* Host interface errors. */
		if ((status & IS_HostError) != 0) {
			device_printf(sc->sc_dev,
			    "Host interface error, resetting...\n");
			reinit = 1;
			goto force_init;
		}

		/* Receive interrupts. */
		if ((status & IS_RxDMAComplete) != 0) {
			stge_rxeof(sc);
			if ((status & IS_RFDListEnd) != 0)
				CSR_WRITE_4(sc, STGE_DMACtrl,
				    DMAC_RxDMAPollNow);
		}

		/* Transmit interrupts. */
		if ((status & (IS_TxDMAComplete | IS_TxComplete)) != 0)
			stge_txeof(sc);

		/* Transmission errors.*/
		if ((status & IS_TxComplete) != 0) {
			if ((reinit = stge_tx_error(sc)) != 0)
				break;
		}
	}

force_init:
	if (reinit != 0) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		stge_init_locked(sc);
	}

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, STGE_IntEnable, sc->sc_IntEnable);

	/* Try to get more packets going. */
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		stge_start_locked(ifp);

done_locked:
	STGE_UNLOCK(sc);
}

/*
 * stge_txeof:
 *
 *	Helper; handle transmit interrupts.
 */
static void
stge_txeof(struct stge_softc *sc)
{
	struct ifnet *ifp;
	struct stge_txdesc *txd;
	uint64_t control;
	int cons;

	STGE_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;

	txd = STAILQ_FIRST(&sc->sc_cdata.stge_txbusyq);
	if (txd == NULL)
		return;
	bus_dmamap_sync(sc->sc_cdata.stge_tx_ring_tag,
	    sc->sc_cdata.stge_tx_ring_map, BUS_DMASYNC_POSTREAD);

	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (cons = sc->sc_cdata.stge_tx_cons;;
	    cons = (cons + 1) % STGE_TX_RING_CNT) {
		if (sc->sc_cdata.stge_tx_cnt <= 0)
			break;
		control = le64toh(sc->sc_rdata.stge_tx_ring[cons].tfd_control);
		if ((control & TFD_TFDDone) == 0)
			break;
		sc->sc_cdata.stge_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

		bus_dmamap_sync(sc->sc_cdata.stge_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_cdata.stge_tx_tag, txd->tx_dmamap);

		/* Output counter is updated with statistics register */
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
		STAILQ_REMOVE_HEAD(&sc->sc_cdata.stge_txbusyq, tx_q);
		STAILQ_INSERT_TAIL(&sc->sc_cdata.stge_txfreeq, txd, tx_q);
		txd = STAILQ_FIRST(&sc->sc_cdata.stge_txbusyq);
	}
	sc->sc_cdata.stge_tx_cons = cons;
	if (sc->sc_cdata.stge_tx_cnt == 0)
		sc->sc_watchdog_timer = 0;

        bus_dmamap_sync(sc->sc_cdata.stge_tx_ring_tag,
	    sc->sc_cdata.stge_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static __inline void
stge_discard_rxbuf(struct stge_softc *sc, int idx)
{
	struct stge_rfd *rfd;

	rfd = &sc->sc_rdata.stge_rx_ring[idx];
	rfd->rfd_status = 0;
}

#ifndef __NO_STRICT_ALIGNMENT
/*
 * It seems that TC9021's DMA engine has alignment restrictions in
 * DMA scatter operations. The first DMA segment has no address
 * alignment restrictins but the rest should be aligned on 4(?) bytes
 * boundary. Otherwise it would corrupt random memory. Since we don't
 * know which one is used for the first segment in advance we simply
 * don't align at all.
 * To avoid copying over an entire frame to align, we allocate a new
 * mbuf and copy ethernet header to the new mbuf. The new mbuf is
 * prepended into the existing mbuf chain.
 */
static __inline struct mbuf *
stge_fixup_rx(struct stge_softc *sc, struct mbuf *m)
{
	struct mbuf *n;

	n = NULL;
	if (m->m_len <= (MCLBYTES - ETHER_HDR_LEN)) {
		bcopy(m->m_data, m->m_data + ETHER_HDR_LEN, m->m_len);
		m->m_data += ETHER_HDR_LEN;
		n = m;
	} else {
		MGETHDR(n, M_NOWAIT, MT_DATA);
		if (n != NULL) {
			bcopy(m->m_data, n->m_data, ETHER_HDR_LEN);
			m->m_data += ETHER_HDR_LEN;
			m->m_len -= ETHER_HDR_LEN;
			n->m_len = ETHER_HDR_LEN;
			M_MOVE_PKTHDR(n, m);
			n->m_next = m;
		} else
			m_freem(m);
	}

	return (n);
}
#endif

/*
 * stge_rxeof:
 *
 *	Helper; handle receive interrupts.
 */
static int
stge_rxeof(struct stge_softc *sc)
{
	struct ifnet *ifp;
	struct stge_rxdesc *rxd;
	struct mbuf *mp, *m;
	uint64_t status64;
	uint32_t status;
	int cons, prog, rx_npkts;

	STGE_LOCK_ASSERT(sc);

	rx_npkts = 0;
	ifp = sc->sc_ifp;

	bus_dmamap_sync(sc->sc_cdata.stge_rx_ring_tag,
	    sc->sc_cdata.stge_rx_ring_map, BUS_DMASYNC_POSTREAD);

	prog = 0;
	for (cons = sc->sc_cdata.stge_rx_cons; prog < STGE_RX_RING_CNT;
	    prog++, cons = (cons + 1) % STGE_RX_RING_CNT) {
		status64 = le64toh(sc->sc_rdata.stge_rx_ring[cons].rfd_status);
		status = RFD_RxStatus(status64);
		if ((status & RFD_RFDDone) == 0)
			break;
#ifdef DEVICE_POLLING
		if (ifp->if_capenable & IFCAP_POLLING) {
			if (sc->sc_cdata.stge_rxcycles <= 0)
				break;
			sc->sc_cdata.stge_rxcycles--;
		}
#endif
		prog++;
		rxd = &sc->sc_cdata.stge_rxdesc[cons];
		mp = rxd->rx_m;

		/*
		 * If the packet had an error, drop it.  Note we count
		 * the error later in the periodic stats update.
		 */
		if ((status & RFD_FrameEnd) != 0 && (status &
		    (RFD_RxFIFOOverrun | RFD_RxRuntFrame |
		    RFD_RxAlignmentError | RFD_RxFCSError |
		    RFD_RxLengthError)) != 0) {
			stge_discard_rxbuf(sc, cons);
			if (sc->sc_cdata.stge_rxhead != NULL) {
				m_freem(sc->sc_cdata.stge_rxhead);
				STGE_RXCHAIN_RESET(sc);
			}
			continue;
		}
		/*
		 * Add a new receive buffer to the ring.
		 */
		if (stge_newbuf(sc, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			stge_discard_rxbuf(sc, cons);
			if (sc->sc_cdata.stge_rxhead != NULL) {
				m_freem(sc->sc_cdata.stge_rxhead);
				STGE_RXCHAIN_RESET(sc);
			}
			continue;
		}

		if ((status & RFD_FrameEnd) != 0)
			mp->m_len = RFD_RxDMAFrameLen(status) -
			    sc->sc_cdata.stge_rxlen;
		sc->sc_cdata.stge_rxlen += mp->m_len;

		/* Chain mbufs. */
		if (sc->sc_cdata.stge_rxhead == NULL) {
			sc->sc_cdata.stge_rxhead = mp;
			sc->sc_cdata.stge_rxtail = mp;
		} else {
			mp->m_flags &= ~M_PKTHDR;
			sc->sc_cdata.stge_rxtail->m_next = mp;
			sc->sc_cdata.stge_rxtail = mp;
		}

		if ((status & RFD_FrameEnd) != 0) {
			m = sc->sc_cdata.stge_rxhead;
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = sc->sc_cdata.stge_rxlen;

			if (m->m_pkthdr.len > sc->sc_if_framesize) {
				m_freem(m);
				STGE_RXCHAIN_RESET(sc);
				continue;
			}
			/*
			 * Set the incoming checksum information for
			 * the packet.
			 */
			if ((ifp->if_capenable & IFCAP_RXCSUM) != 0) {
				if ((status & RFD_IPDetected) != 0) {
					m->m_pkthdr.csum_flags |=
						CSUM_IP_CHECKED;
					if ((status & RFD_IPError) == 0)
						m->m_pkthdr.csum_flags |=
						    CSUM_IP_VALID;
				}
				if (((status & RFD_TCPDetected) != 0 &&
				    (status & RFD_TCPError) == 0) ||
				    ((status & RFD_UDPDetected) != 0 &&
				    (status & RFD_UDPError) == 0)) {
					m->m_pkthdr.csum_flags |=
					    (CSUM_DATA_VALID | CSUM_PSEUDO_HDR);
					m->m_pkthdr.csum_data = 0xffff;
				}
			}

#ifndef __NO_STRICT_ALIGNMENT
			if (sc->sc_if_framesize > (MCLBYTES - ETHER_ALIGN)) {
				if ((m = stge_fixup_rx(sc, m)) == NULL) {
					STGE_RXCHAIN_RESET(sc);
					continue;
				}
			}
#endif
			/* Check for VLAN tagged packets. */
			if ((status & RFD_VLANDetected) != 0 &&
			    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0) {
				m->m_pkthdr.ether_vtag = RFD_TCI(status64);
				m->m_flags |= M_VLANTAG;
			}

			STGE_UNLOCK(sc);
			/* Pass it on. */
			(*ifp->if_input)(ifp, m);
			STGE_LOCK(sc);
			rx_npkts++;

			STGE_RXCHAIN_RESET(sc);
		}
	}

	if (prog > 0) {
		/* Update the consumer index. */
		sc->sc_cdata.stge_rx_cons = cons;
		bus_dmamap_sync(sc->sc_cdata.stge_rx_ring_tag,
		    sc->sc_cdata.stge_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
	return (rx_npkts);
}

#ifdef DEVICE_POLLING
static int
stge_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct stge_softc *sc;
	uint16_t status;
	int rx_npkts;

	rx_npkts = 0;
	sc = ifp->if_softc;
	STGE_LOCK(sc);
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		STGE_UNLOCK(sc);
		return (rx_npkts);
	}

	sc->sc_cdata.stge_rxcycles = count;
	rx_npkts = stge_rxeof(sc);
	stge_txeof(sc);

	if (cmd == POLL_AND_CHECK_STATUS) {
		status = CSR_READ_2(sc, STGE_IntStatus);
		status &= sc->sc_IntEnable;
		if (status != 0) {
			if ((status & IS_HostError) != 0) {
				device_printf(sc->sc_dev,
				    "Host interface error, resetting...\n");
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				stge_init_locked(sc);
			}
			if ((status & IS_TxComplete) != 0) {
				if (stge_tx_error(sc) != 0) {
					ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
					stge_init_locked(sc);
				}
			}
		}

	}

	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		stge_start_locked(ifp);

	STGE_UNLOCK(sc);
	return (rx_npkts);
}
#endif	/* DEVICE_POLLING */

/*
 * stge_tick:
 *
 *	One second timer, used to tick the MII.
 */
static void
stge_tick(void *arg)
{
	struct stge_softc *sc;
	struct mii_data *mii;

	sc = (struct stge_softc *)arg;

	STGE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->sc_miibus);
	mii_tick(mii);

	/* Update statistics counters. */
	stge_stats_update(sc);

	/*
	 * Relcaim any pending Tx descriptors to release mbufs in a
	 * timely manner as we don't generate Tx completion interrupts
	 * for every frame. This limits the delay to a maximum of one
	 * second.
	 */
	if (sc->sc_cdata.stge_tx_cnt != 0)
		stge_txeof(sc);

	stge_watchdog(sc);

	callout_reset(&sc->sc_tick_ch, hz, stge_tick, sc);
}

/*
 * stge_stats_update:
 *
 *	Read the TC9021 statistics counters.
 */
static void
stge_stats_update(struct stge_softc *sc)
{
	struct ifnet *ifp;

	STGE_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;

	CSR_READ_4(sc,STGE_OctetRcvOk);

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, CSR_READ_4(sc, STGE_FramesRcvdOk));

	if_inc_counter(ifp, IFCOUNTER_IERRORS, CSR_READ_2(sc, STGE_FramesLostRxErrors));

	CSR_READ_4(sc, STGE_OctetXmtdOk);

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, CSR_READ_4(sc, STGE_FramesXmtdOk));

	if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
	    CSR_READ_4(sc, STGE_LateCollisions) +
	    CSR_READ_4(sc, STGE_MultiColFrames) +
	    CSR_READ_4(sc, STGE_SingleColFrames));

	if_inc_counter(ifp, IFCOUNTER_OERRORS,
	    CSR_READ_2(sc, STGE_FramesAbortXSColls) +
	    CSR_READ_2(sc, STGE_FramesWEXDeferal));
}

/*
 * stge_reset:
 *
 *	Perform a soft reset on the TC9021.
 */
static void
stge_reset(struct stge_softc *sc, uint32_t how)
{
	uint32_t ac;
	uint8_t v;
	int i, dv;

	STGE_LOCK_ASSERT(sc);

	dv = 5000;
	ac = CSR_READ_4(sc, STGE_AsicCtrl);
	switch (how) {
	case STGE_RESET_TX:
		ac |= AC_TxReset | AC_FIFO;
		dv = 100;
		break;
	case STGE_RESET_RX:
		ac |= AC_RxReset | AC_FIFO;
		dv = 100;
		break;
	case STGE_RESET_FULL:
	default:
		/*
		 * Only assert RstOut if we're fiber.  We need GMII clocks
		 * to be present in order for the reset to complete on fiber
		 * cards.
		 */
		ac |= AC_GlobalReset | AC_RxReset | AC_TxReset |
		    AC_DMA | AC_FIFO | AC_Network | AC_Host | AC_AutoInit |
		    (sc->sc_usefiber ? AC_RstOut : 0);
		break;
	}

	CSR_WRITE_4(sc, STGE_AsicCtrl, ac);

	/* Account for reset problem at 10Mbps. */
	DELAY(dv);

	for (i = 0; i < STGE_TIMEOUT; i++) {
		if ((CSR_READ_4(sc, STGE_AsicCtrl) & AC_ResetBusy) == 0)
			break;
		DELAY(dv);
	}

	if (i == STGE_TIMEOUT)
		device_printf(sc->sc_dev, "reset failed to complete\n");

	/* Set LED, from Linux IPG driver. */
	ac = CSR_READ_4(sc, STGE_AsicCtrl);
	ac &= ~(AC_LEDMode | AC_LEDSpeed | AC_LEDModeBit1);
	if ((sc->sc_led & 0x01) != 0)
		ac |= AC_LEDMode;
	if ((sc->sc_led & 0x03) != 0)
		ac |= AC_LEDModeBit1;
	if ((sc->sc_led & 0x08) != 0)
		ac |= AC_LEDSpeed;
	CSR_WRITE_4(sc, STGE_AsicCtrl, ac);

	/* Set PHY, from Linux IPG driver */
	v = CSR_READ_1(sc, STGE_PhySet);
	v &= ~(PS_MemLenb9b | PS_MemLen | PS_NonCompdet);
	v |= ((sc->sc_led & 0x70) >> 4);
	CSR_WRITE_1(sc, STGE_PhySet, v);
}

/*
 * stge_init:		[ ifnet interface function ]
 *
 *	Initialize the interface.
 */
static void
stge_init(void *xsc)
{
	struct stge_softc *sc;

	sc = (struct stge_softc *)xsc;
	STGE_LOCK(sc);
	stge_init_locked(sc);
	STGE_UNLOCK(sc);
}

static void
stge_init_locked(struct stge_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint16_t eaddr[3];
	uint32_t v;
	int error;

	STGE_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;
	mii = device_get_softc(sc->sc_miibus);

	/*
	 * Cancel any pending I/O.
	 */
	stge_stop(sc);

	/*
	 * Reset the chip to a known state.
	 */
	stge_reset(sc, STGE_RESET_FULL);

	/* Init descriptors. */
	error = stge_init_rx_ring(sc);
        if (error != 0) {
                device_printf(sc->sc_dev,
                    "initialization failed: no memory for rx buffers\n");
                stge_stop(sc);
		goto out;
        }
	stge_init_tx_ring(sc);

	/* Set the station address. */
	bcopy(IF_LLADDR(ifp), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_2(sc, STGE_StationAddress0, htole16(eaddr[0]));
	CSR_WRITE_2(sc, STGE_StationAddress1, htole16(eaddr[1]));
	CSR_WRITE_2(sc, STGE_StationAddress2, htole16(eaddr[2]));

	/*
	 * Set the statistics masks.  Disable all the RMON stats,
	 * and disable selected stats in the non-RMON stats registers.
	 */
	CSR_WRITE_4(sc, STGE_RMONStatisticsMask, 0xffffffff);
	CSR_WRITE_4(sc, STGE_StatisticsMask,
	    (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4) | (1U << 5) |
	    (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9) | (1U << 10) |
	    (1U << 13) | (1U << 14) | (1U << 15) | (1U << 19) | (1U << 20) |
	    (1U << 21));

	/* Set up the receive filter. */
	stge_set_filter(sc);
	/* Program multicast filter. */
	stge_set_multi(sc);

	/*
	 * Give the transmit and receive ring to the chip.
	 */
	CSR_WRITE_4(sc, STGE_TFDListPtrHi,
	    STGE_ADDR_HI(STGE_TX_RING_ADDR(sc, 0)));
	CSR_WRITE_4(sc, STGE_TFDListPtrLo,
	    STGE_ADDR_LO(STGE_TX_RING_ADDR(sc, 0)));

	CSR_WRITE_4(sc, STGE_RFDListPtrHi,
	    STGE_ADDR_HI(STGE_RX_RING_ADDR(sc, 0)));
	CSR_WRITE_4(sc, STGE_RFDListPtrLo,
	    STGE_ADDR_LO(STGE_RX_RING_ADDR(sc, 0)));

	/*
	 * Initialize the Tx auto-poll period.  It's OK to make this number
	 * large (255 is the max, but we use 127) -- we explicitly kick the
	 * transmit engine when there's actually a packet.
	 */
	CSR_WRITE_1(sc, STGE_TxDMAPollPeriod, 127);

	/* ..and the Rx auto-poll period. */
	CSR_WRITE_1(sc, STGE_RxDMAPollPeriod, 1);

	/* Initialize the Tx start threshold. */
	CSR_WRITE_2(sc, STGE_TxStartThresh, sc->sc_txthresh);

	/* Rx DMA thresholds, from Linux */
	CSR_WRITE_1(sc, STGE_RxDMABurstThresh, 0x30);
	CSR_WRITE_1(sc, STGE_RxDMAUrgentThresh, 0x30);

	/* Rx early threhold, from Linux */
	CSR_WRITE_2(sc, STGE_RxEarlyThresh, 0x7ff);

	/* Tx DMA thresholds, from Linux */
	CSR_WRITE_1(sc, STGE_TxDMABurstThresh, 0x30);
	CSR_WRITE_1(sc, STGE_TxDMAUrgentThresh, 0x04);

	/*
	 * Initialize the Rx DMA interrupt control register.  We
	 * request an interrupt after every incoming packet, but
	 * defer it for sc_rxint_dmawait us. When the number of
	 * interrupts pending reaches STGE_RXINT_NFRAME, we stop
	 * deferring the interrupt, and signal it immediately.
	 */
	CSR_WRITE_4(sc, STGE_RxDMAIntCtrl,
	    RDIC_RxFrameCount(sc->sc_rxint_nframe) |
	    RDIC_RxDMAWaitTime(STGE_RXINT_USECS2TICK(sc->sc_rxint_dmawait)));

	/*
	 * Initialize the interrupt mask.
	 */
	sc->sc_IntEnable = IS_HostError | IS_TxComplete |
	    IS_TxDMAComplete | IS_RxDMAComplete | IS_RFDListEnd;
#ifdef DEVICE_POLLING
	/* Disable interrupts if we are polling. */
	if ((ifp->if_capenable & IFCAP_POLLING) != 0)
		CSR_WRITE_2(sc, STGE_IntEnable, 0);
	else
#endif
	CSR_WRITE_2(sc, STGE_IntEnable, sc->sc_IntEnable);

	/*
	 * Configure the DMA engine.
	 * XXX Should auto-tune TxBurstLimit.
	 */
	CSR_WRITE_4(sc, STGE_DMACtrl, sc->sc_DMACtrl | DMAC_TxBurstLimit(3));

	/*
	 * Send a PAUSE frame when we reach 29,696 bytes in the Rx
	 * FIFO, and send an un-PAUSE frame when we reach 3056 bytes
	 * in the Rx FIFO.
	 */
	CSR_WRITE_2(sc, STGE_FlowOnTresh, 29696 / 16);
	CSR_WRITE_2(sc, STGE_FlowOffThresh, 3056 / 16);

	/*
	 * Set the maximum frame size.
	 */
	sc->sc_if_framesize = ifp->if_mtu + ETHER_HDR_LEN + ETHER_CRC_LEN;
	CSR_WRITE_2(sc, STGE_MaxFrameSize, sc->sc_if_framesize);

	/*
	 * Initialize MacCtrl -- do it before setting the media,
	 * as setting the media will actually program the register.
	 *
	 * Note: We have to poke the IFS value before poking
	 * anything else.
	 */
	/* Tx/Rx MAC should be disabled before programming IFS.*/
	CSR_WRITE_4(sc, STGE_MACCtrl, MC_IFSSelect(MC_IFS96bit));

	stge_vlan_setup(sc);

	if (sc->sc_rev >= 6) {		/* >= B.2 */
		/* Multi-frag frame bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0200);

		/* Tx Poll Now bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0010);
		/* Tx Poll Now bug work-around. */
		CSR_WRITE_2(sc, STGE_DebugCtrl,
		    CSR_READ_2(sc, STGE_DebugCtrl) | 0x0020);
	}

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	v |= MC_StatisticsEnable | MC_TxEnable | MC_RxEnable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	/*
	 * It seems that transmitting frames without checking the state of
	 * Rx/Tx MAC wedge the hardware.
	 */
	stge_start_tx(sc);
	stge_start_rx(sc);

	sc->sc_link = 0;
	/*
	 * Set the current media.
	 */
	mii_mediachg(mii);

	/*
	 * Start the one second MII clock.
	 */
	callout_reset(&sc->sc_tick_ch, hz, stge_tick, sc);

	/*
	 * ...all done!
	 */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

 out:
	if (error != 0)
		device_printf(sc->sc_dev, "interface not running\n");
}

static void
stge_vlan_setup(struct stge_softc *sc)
{
	struct ifnet *ifp;
	uint32_t v;

	ifp = sc->sc_ifp;
	/*
	 * The NIC always copy a VLAN tag regardless of STGE_MACCtrl
	 * MC_AutoVLANuntagging bit.
	 * MC_AutoVLANtagging bit selects which VLAN source to use
	 * between STGE_VLANTag and TFC. However TFC TFD_VLANTagInsert
	 * bit has priority over MC_AutoVLANtagging bit. So we always
	 * use TFC instead of STGE_VLANTag register.
	 */
	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		v |= MC_AutoVLANuntagging;
	else
		v &= ~MC_AutoVLANuntagging;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
}

/*
 *	Stop transmission on the interface.
 */
static void
stge_stop(struct stge_softc *sc)
{
	struct ifnet *ifp;
	struct stge_txdesc *txd;
	struct stge_rxdesc *rxd;
	uint32_t v;
	int i;

	STGE_LOCK_ASSERT(sc);
	/*
	 * Stop the one second clock.
	 */
	callout_stop(&sc->sc_tick_ch);
	sc->sc_watchdog_timer = 0;

	/*
	 * Disable interrupts.
	 */
	CSR_WRITE_2(sc, STGE_IntEnable, 0);

	/*
	 * Stop receiver, transmitter, and stats update.
	 */
	stge_stop_rx(sc);
	stge_stop_tx(sc);
	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	v |= MC_StatisticsDisable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);

	/*
	 * Stop the transmit and receive DMA.
	 */
	stge_dma_wait(sc);
	CSR_WRITE_4(sc, STGE_TFDListPtrHi, 0);
	CSR_WRITE_4(sc, STGE_TFDListPtrLo, 0);
	CSR_WRITE_4(sc, STGE_RFDListPtrHi, 0);
	CSR_WRITE_4(sc, STGE_RFDListPtrLo, 0);

	/*
	 * Free RX and TX mbufs still in the queues.
	 */
	for (i = 0; i < STGE_RX_RING_CNT; i++) {
		rxd = &sc->sc_cdata.stge_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->sc_cdata.stge_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_cdata.stge_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
        }
	for (i = 0; i < STGE_TX_RING_CNT; i++) {
		txd = &sc->sc_cdata.stge_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->sc_cdata.stge_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_cdata.stge_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
        }

	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp = sc->sc_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->sc_link = 0;
}

static void
stge_start_tx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_TxEnabled) != 0)
		return;
	v |= MC_TxEnable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	CSR_WRITE_1(sc, STGE_TxDMAPollPeriod, 127);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_TxEnabled) != 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Starting Tx MAC timed out\n");
}

static void
stge_start_rx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_RxEnabled) != 0)
		return;
	v |= MC_RxEnable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	CSR_WRITE_1(sc, STGE_RxDMAPollPeriod, 1);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_RxEnabled) != 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Starting Rx MAC timed out\n");
}

static void
stge_stop_tx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_TxEnabled) == 0)
		return;
	v |= MC_TxDisable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_TxEnabled) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Stopping Tx MAC timed out\n");
}

static void
stge_stop_rx(struct stge_softc *sc)
{
	uint32_t v;
	int i;

	v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
	if ((v & MC_RxEnabled) == 0)
		return;
	v |= MC_RxDisable;
	CSR_WRITE_4(sc, STGE_MACCtrl, v);
	for (i = STGE_TIMEOUT; i > 0; i--) {
		DELAY(10);
		v = CSR_READ_4(sc, STGE_MACCtrl) & MC_MASK;
		if ((v & MC_RxEnabled) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->sc_dev, "Stopping Rx MAC timed out\n");
}

static void
stge_init_tx_ring(struct stge_softc *sc)
{
	struct stge_ring_data *rd;
	struct stge_txdesc *txd;
	bus_addr_t addr;
	int i;

	STAILQ_INIT(&sc->sc_cdata.stge_txfreeq);
	STAILQ_INIT(&sc->sc_cdata.stge_txbusyq);

	sc->sc_cdata.stge_tx_prod = 0;
	sc->sc_cdata.stge_tx_cons = 0;
	sc->sc_cdata.stge_tx_cnt = 0;

	rd = &sc->sc_rdata;
	bzero(rd->stge_tx_ring, STGE_TX_RING_SZ);
	for (i = 0; i < STGE_TX_RING_CNT; i++) {
		if (i == (STGE_TX_RING_CNT - 1))
			addr = STGE_TX_RING_ADDR(sc, 0);
		else
			addr = STGE_TX_RING_ADDR(sc, i + 1);
		rd->stge_tx_ring[i].tfd_next = htole64(addr);
		rd->stge_tx_ring[i].tfd_control = htole64(TFD_TFDDone);
		txd = &sc->sc_cdata.stge_txdesc[i];
		STAILQ_INSERT_TAIL(&sc->sc_cdata.stge_txfreeq, txd, tx_q);
	}

	bus_dmamap_sync(sc->sc_cdata.stge_tx_ring_tag,
	    sc->sc_cdata.stge_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

}

static int
stge_init_rx_ring(struct stge_softc *sc)
{
	struct stge_ring_data *rd;
	bus_addr_t addr;
	int i;

	sc->sc_cdata.stge_rx_cons = 0;
	STGE_RXCHAIN_RESET(sc);

	rd = &sc->sc_rdata;
	bzero(rd->stge_rx_ring, STGE_RX_RING_SZ);
	for (i = 0; i < STGE_RX_RING_CNT; i++) {
		if (stge_newbuf(sc, i) != 0)
			return (ENOBUFS);
		if (i == (STGE_RX_RING_CNT - 1))
			addr = STGE_RX_RING_ADDR(sc, 0);
		else
			addr = STGE_RX_RING_ADDR(sc, i + 1);
		rd->stge_rx_ring[i].rfd_next = htole64(addr);
		rd->stge_rx_ring[i].rfd_status = 0;
	}

	bus_dmamap_sync(sc->sc_cdata.stge_rx_ring_tag,
	    sc->sc_cdata.stge_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * stge_newbuf:
 *
 *	Add a receive buffer to the indicated descriptor.
 */
static int
stge_newbuf(struct stge_softc *sc, int idx)
{
	struct stge_rxdesc *rxd;
	struct stge_rfd *rfd;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	/*
	 * The hardware requires 4bytes aligned DMA address when JUMBO
	 * frame is used.
	 */
	if (sc->sc_if_framesize <= (MCLBYTES - ETHER_ALIGN))
		m_adj(m, ETHER_ALIGN);

	if (bus_dmamap_load_mbuf_sg(sc->sc_cdata.stge_rx_tag,
	    sc->sc_cdata.stge_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc->sc_cdata.stge_rxdesc[idx];
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->sc_cdata.stge_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_cdata.stge_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->sc_cdata.stge_rx_sparemap;
	sc->sc_cdata.stge_rx_sparemap = map;
	bus_dmamap_sync(sc->sc_cdata.stge_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;

	rfd = &sc->sc_rdata.stge_rx_ring[idx];
	rfd->rfd_frag.frag_word0 =
	    htole64(FRAG_ADDR(segs[0].ds_addr) | FRAG_LEN(segs[0].ds_len));
	rfd->rfd_status = 0;

	return (0);
}

/*
 * stge_set_filter:
 *
 *	Set up the receive filter.
 */
static void
stge_set_filter(struct stge_softc *sc)
{
	struct ifnet *ifp;
	uint16_t mode;

	STGE_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;

	mode = CSR_READ_2(sc, STGE_ReceiveMode);
	mode |= RM_ReceiveUnicast;
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		mode |= RM_ReceiveBroadcast;
	else
		mode &= ~RM_ReceiveBroadcast;
	if ((ifp->if_flags & IFF_PROMISC) != 0)
		mode |= RM_ReceiveAllFrames;
	else
		mode &= ~RM_ReceiveAllFrames;

	CSR_WRITE_2(sc, STGE_ReceiveMode, mode);
}

static void
stge_set_multi(struct stge_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t crc;
	uint32_t mchash[2];
	uint16_t mode;
	int count;

	STGE_LOCK_ASSERT(sc);

	ifp = sc->sc_ifp;

	mode = CSR_READ_2(sc, STGE_ReceiveMode);
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			mode |= RM_ReceiveAllFrames;
		else if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			mode |= RM_ReceiveMulticast;
		CSR_WRITE_2(sc, STGE_ReceiveMode, mode);
		return;
	}

	/* clear existing filters. */
	CSR_WRITE_4(sc, STGE_HashTable0, 0);
	CSR_WRITE_4(sc, STGE_HashTable1, 0);

	/*
	 * Set up the multicast address filter by passing all multicast
	 * addresses through a CRC generator, and then using the low-order
	 * 6 bits as an index into the 64 bit multicast hash table.  The
	 * high order bits select the register, while the rest of the bits
	 * select the bit within the register.
	 */

	bzero(mchash, sizeof(mchash));

	count = 0;
	if_maddr_rlock(sc->sc_ifp);
	CK_STAILQ_FOREACH(ifma, &sc->sc_ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);

		/* Just want the 6 least significant bits. */
		crc &= 0x3f;

		/* Set the corresponding bit in the hash table. */
		mchash[crc >> 5] |= 1 << (crc & 0x1f);
		count++;
	}
	if_maddr_runlock(ifp);

	mode &= ~(RM_ReceiveMulticast | RM_ReceiveAllFrames);
	if (count > 0)
		mode |= RM_ReceiveMulticastHash;
	else
		mode &= ~RM_ReceiveMulticastHash;

	CSR_WRITE_4(sc, STGE_HashTable0, mchash[0]);
	CSR_WRITE_4(sc, STGE_HashTable1, mchash[1]);
	CSR_WRITE_2(sc, STGE_ReceiveMode, mode);
}

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (!arg1)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || !req->newptr)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
        *(int *)arg1 = value;

        return (0);
}

static int
sysctl_hw_stge_rxint_nframe(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    STGE_RXINT_NFRAME_MIN, STGE_RXINT_NFRAME_MAX));
}

static int
sysctl_hw_stge_rxint_dmawait(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    STGE_RXINT_DMAWAIT_MIN, STGE_RXINT_DMAWAIT_MAX));
}
