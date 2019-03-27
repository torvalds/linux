/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009, Pyun YongHyeon <yongari@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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

/* Driver for Atheros AR813x/AR815x PCIe Ethernet. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/taskqueue.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_llc.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/netdump/netdump.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>
#include <machine/in_cksum.h>

#include <dev/alc/if_alcreg.h>
#include <dev/alc/if_alcvar.h>

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"
#undef ALC_USE_CUSTOM_CSUM

#ifdef ALC_USE_CUSTOM_CSUM
#define	ALC_CSUM_FEATURES	(CSUM_TCP | CSUM_UDP)
#else
#define	ALC_CSUM_FEATURES	(CSUM_IP | CSUM_TCP | CSUM_UDP)
#endif

MODULE_DEPEND(alc, pci, 1, 1, 1);
MODULE_DEPEND(alc, ether, 1, 1, 1);
MODULE_DEPEND(alc, miibus, 1, 1, 1);

/* Tunables. */
static int msi_disable = 0;
static int msix_disable = 0;
TUNABLE_INT("hw.alc.msi_disable", &msi_disable);
TUNABLE_INT("hw.alc.msix_disable", &msix_disable);

/*
 * Devices supported by this driver.
 */
static struct alc_ident alc_ident_table[] = {
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8131, 9 * 1024,
		"Atheros AR8131 PCIe Gigabit Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8132, 9 * 1024,
		"Atheros AR8132 PCIe Fast Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8151, 6 * 1024,
		"Atheros AR8151 v1.0 PCIe Gigabit Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8151_V2, 6 * 1024,
		"Atheros AR8151 v2.0 PCIe Gigabit Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8152_B, 6 * 1024,
		"Atheros AR8152 v1.1 PCIe Fast Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8152_B2, 6 * 1024,
		"Atheros AR8152 v2.0 PCIe Fast Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8161, 9 * 1024,
		"Atheros AR8161 PCIe Gigabit Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8162, 9 * 1024,
		"Atheros AR8162 PCIe Fast Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8171, 9 * 1024,
		"Atheros AR8171 PCIe Gigabit Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_AR8172, 9 * 1024,
		"Atheros AR8172 PCIe Fast Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_E2200, 9 * 1024,
		"Killer E2200 Gigabit Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_E2400, 9 * 1024,
		"Killer E2400 Gigabit Ethernet" },
	{ VENDORID_ATHEROS, DEVICEID_ATHEROS_E2500, 9 * 1024,
		"Killer E2500 Gigabit Ethernet" },
	{ 0, 0, 0, NULL}
};

static void	alc_aspm(struct alc_softc *, int, int);
static void	alc_aspm_813x(struct alc_softc *, int);
static void	alc_aspm_816x(struct alc_softc *, int);
static int	alc_attach(device_t);
static int	alc_check_boundary(struct alc_softc *);
static void	alc_config_msi(struct alc_softc *);
static int	alc_detach(device_t);
static void	alc_disable_l0s_l1(struct alc_softc *);
static int	alc_dma_alloc(struct alc_softc *);
static void	alc_dma_free(struct alc_softc *);
static void	alc_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static void	alc_dsp_fixup(struct alc_softc *, int);
static int	alc_encap(struct alc_softc *, struct mbuf **);
static struct alc_ident *
		alc_find_ident(device_t);
#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf *
		alc_fixup_rx(struct ifnet *, struct mbuf *);
#endif
static void	alc_get_macaddr(struct alc_softc *);
static void	alc_get_macaddr_813x(struct alc_softc *);
static void	alc_get_macaddr_816x(struct alc_softc *);
static void	alc_get_macaddr_par(struct alc_softc *);
static void	alc_init(void *);
static void	alc_init_cmb(struct alc_softc *);
static void	alc_init_locked(struct alc_softc *);
static void	alc_init_rr_ring(struct alc_softc *);
static int	alc_init_rx_ring(struct alc_softc *);
static void	alc_init_smb(struct alc_softc *);
static void	alc_init_tx_ring(struct alc_softc *);
static void	alc_int_task(void *, int);
static int	alc_intr(void *);
static int	alc_ioctl(struct ifnet *, u_long, caddr_t);
static void	alc_mac_config(struct alc_softc *);
static uint32_t	alc_mii_readreg_813x(struct alc_softc *, int, int);
static uint32_t	alc_mii_readreg_816x(struct alc_softc *, int, int);
static uint32_t	alc_mii_writereg_813x(struct alc_softc *, int, int, int);
static uint32_t	alc_mii_writereg_816x(struct alc_softc *, int, int, int);
static int	alc_miibus_readreg(device_t, int, int);
static void	alc_miibus_statchg(device_t);
static int	alc_miibus_writereg(device_t, int, int, int);
static uint32_t	alc_miidbg_readreg(struct alc_softc *, int);
static uint32_t	alc_miidbg_writereg(struct alc_softc *, int, int);
static uint32_t	alc_miiext_readreg(struct alc_softc *, int, int);
static uint32_t	alc_miiext_writereg(struct alc_softc *, int, int, int);
static int	alc_mediachange(struct ifnet *);
static int	alc_mediachange_locked(struct alc_softc *);
static void	alc_mediastatus(struct ifnet *, struct ifmediareq *);
static int	alc_newbuf(struct alc_softc *, struct alc_rxdesc *);
static void	alc_osc_reset(struct alc_softc *);
static void	alc_phy_down(struct alc_softc *);
static void	alc_phy_reset(struct alc_softc *);
static void	alc_phy_reset_813x(struct alc_softc *);
static void	alc_phy_reset_816x(struct alc_softc *);
static int	alc_probe(device_t);
static void	alc_reset(struct alc_softc *);
static int	alc_resume(device_t);
static void	alc_rxeof(struct alc_softc *, struct rx_rdesc *);
static int	alc_rxintr(struct alc_softc *, int);
static void	alc_rxfilter(struct alc_softc *);
static void	alc_rxvlan(struct alc_softc *);
static void	alc_setlinkspeed(struct alc_softc *);
static void	alc_setwol(struct alc_softc *);
static void	alc_setwol_813x(struct alc_softc *);
static void	alc_setwol_816x(struct alc_softc *);
static int	alc_shutdown(device_t);
static void	alc_start(struct ifnet *);
static void	alc_start_locked(struct ifnet *);
static void	alc_start_queue(struct alc_softc *);
static void	alc_start_tx(struct alc_softc *);
static void	alc_stats_clear(struct alc_softc *);
static void	alc_stats_update(struct alc_softc *);
static void	alc_stop(struct alc_softc *);
static void	alc_stop_mac(struct alc_softc *);
static void	alc_stop_queue(struct alc_softc *);
static int	alc_suspend(device_t);
static void	alc_sysctl_node(struct alc_softc *);
static void	alc_tick(void *);
static void	alc_txeof(struct alc_softc *);
static void	alc_watchdog(struct alc_softc *);
static int	sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int	sysctl_hw_alc_proc_limit(SYSCTL_HANDLER_ARGS);
static int	sysctl_hw_alc_int_mod(SYSCTL_HANDLER_ARGS);

NETDUMP_DEFINE(alc);

static device_method_t alc_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		alc_probe),
	DEVMETHOD(device_attach,	alc_attach),
	DEVMETHOD(device_detach,	alc_detach),
	DEVMETHOD(device_shutdown,	alc_shutdown),
	DEVMETHOD(device_suspend,	alc_suspend),
	DEVMETHOD(device_resume,	alc_resume),

	/* MII interface. */
	DEVMETHOD(miibus_readreg,	alc_miibus_readreg),
	DEVMETHOD(miibus_writereg,	alc_miibus_writereg),
	DEVMETHOD(miibus_statchg,	alc_miibus_statchg),

	DEVMETHOD_END
};

static driver_t alc_driver = {
	"alc",
	alc_methods,
	sizeof(struct alc_softc)
};

static devclass_t alc_devclass;

DRIVER_MODULE(alc, pci, alc_driver, alc_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device", pci, alc, alc_ident_table,
    nitems(alc_ident_table) - 1);
DRIVER_MODULE(miibus, alc, miibus_driver, miibus_devclass, 0, 0);

static struct resource_spec alc_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	PCIR_BAR(0),	RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec alc_irq_spec_legacy[] = {
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

static struct resource_spec alc_irq_spec_msi[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec alc_irq_spec_msix[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

static uint32_t alc_dma_burst[] = { 128, 256, 512, 1024, 2048, 4096, 0, 0 };

static int
alc_miibus_readreg(device_t dev, int phy, int reg)
{
	struct alc_softc *sc;
	int v;

	sc = device_get_softc(dev);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		v = alc_mii_readreg_816x(sc, phy, reg);
	else
		v = alc_mii_readreg_813x(sc, phy, reg);
	return (v);
}

static uint32_t
alc_mii_readreg_813x(struct alc_softc *sc, int phy, int reg)
{
	uint32_t v;
	int i;

	/*
	 * For AR8132 fast ethernet controller, do not report 1000baseT
	 * capability to mii(4). Even though AR8132 uses the same
	 * model/revision number of F1 gigabit PHY, the PHY has no
	 * ability to establish 1000baseT link.
	 */
	if ((sc->alc_flags & ALC_FLAG_FASTETHER) != 0 &&
	    reg == MII_EXTSR)
		return (0);

	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0) {
		device_printf(sc->alc_dev, "phy read timeout : %d\n", reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

static uint32_t
alc_mii_readreg_816x(struct alc_softc *sc, int phy, int reg)
{
	uint32_t clk, v;
	int i;

	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | clk | MDIO_REG_ADDR(reg));
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0) {
		device_printf(sc->alc_dev, "phy read timeout : %d\n", reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

static int
alc_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct alc_softc *sc;
	int v;

	sc = device_get_softc(dev);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		v = alc_mii_writereg_816x(sc, phy, reg, val);
	else
		v = alc_mii_writereg_813x(sc, phy, reg, val);
	return (v);
}

static uint32_t
alc_mii_writereg_813x(struct alc_softc *sc, int phy, int reg, int val)
{
	uint32_t v;
	int i;

	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    (val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT |
	    MDIO_SUP_PREAMBLE | MDIO_CLK_25_4 | MDIO_REG_ADDR(reg));
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & (MDIO_OP_EXECUTE | MDIO_OP_BUSY)) == 0)
			break;
	}

	if (i == 0)
		device_printf(sc->alc_dev, "phy write timeout : %d\n", reg);

	return (0);
}

static uint32_t
alc_mii_writereg_816x(struct alc_softc *sc, int phy, int reg, int val)
{
	uint32_t clk, v;
	int i;

	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    ((val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT) | MDIO_REG_ADDR(reg) |
	    MDIO_SUP_PREAMBLE | clk);
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0)
		device_printf(sc->alc_dev, "phy write timeout : %d\n", reg);

	return (0);
}

static void
alc_miibus_statchg(device_t dev)
{
	struct alc_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t reg;

	sc = device_get_softc(dev);

	mii = device_get_softc(sc->alc_miibus);
	ifp = sc->alc_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->alc_flags &= ~ALC_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->alc_flags |= ALC_FLAG_LINK;
			break;
		case IFM_1000_T:
			if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0)
				sc->alc_flags |= ALC_FLAG_LINK;
			break;
		default:
			break;
		}
	}
	/* Stop Rx/Tx MACs. */
	alc_stop_mac(sc);

	/* Program MACs with resolved speed/duplex/flow-control. */
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		alc_start_queue(sc);
		alc_mac_config(sc);
		/* Re-enable Tx/Rx MACs. */
		reg = CSR_READ_4(sc, ALC_MAC_CFG);
		reg |= MAC_CFG_TX_ENB | MAC_CFG_RX_ENB;
		CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
	}
	alc_aspm(sc, 0, IFM_SUBTYPE(mii->mii_media_active));
	alc_dsp_fixup(sc, IFM_SUBTYPE(mii->mii_media_active));
}

static uint32_t
alc_miidbg_readreg(struct alc_softc *sc, int reg)
{

	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    reg);
	return (alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA));
}

static uint32_t
alc_miidbg_writereg(struct alc_softc *sc, int reg, int val)
{

	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    reg);
	return (alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, val));
}

static uint32_t
alc_miiext_readreg(struct alc_softc *sc, int devaddr, int reg)
{
	uint32_t clk, v;
	int i;

	CSR_WRITE_4(sc, ALC_EXT_MDIO, EXT_MDIO_REG(reg) |
	    EXT_MDIO_DEVADDR(devaddr));
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_READ |
	    MDIO_SUP_PREAMBLE | clk | MDIO_MODE_EXT);
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0) {
		device_printf(sc->alc_dev, "phy ext read timeout : %d, %d\n",
		    devaddr, reg);
		return (0);
	}

	return ((v & MDIO_DATA_MASK) >> MDIO_DATA_SHIFT);
}

static uint32_t
alc_miiext_writereg(struct alc_softc *sc, int devaddr, int reg, int val)
{
	uint32_t clk, v;
	int i;

	CSR_WRITE_4(sc, ALC_EXT_MDIO, EXT_MDIO_REG(reg) |
	    EXT_MDIO_DEVADDR(devaddr));
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0)
		clk = MDIO_CLK_25_128;
	else
		clk = MDIO_CLK_25_4;
	CSR_WRITE_4(sc, ALC_MDIO, MDIO_OP_EXECUTE | MDIO_OP_WRITE |
	    ((val & MDIO_DATA_MASK) << MDIO_DATA_SHIFT) |
	    MDIO_SUP_PREAMBLE | clk | MDIO_MODE_EXT);
	for (i = ALC_PHY_TIMEOUT; i > 0; i--) {
		DELAY(5);
		v = CSR_READ_4(sc, ALC_MDIO);
		if ((v & MDIO_OP_BUSY) == 0)
			break;
	}

	if (i == 0)
		device_printf(sc->alc_dev, "phy ext write timeout : %d, %d\n",
		    devaddr, reg);

	return (0);
}

static void
alc_dsp_fixup(struct alc_softc *sc, int media)
{
	uint16_t agc, len, val;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		return;
	if (AR816X_REV(sc->alc_rev) >= AR816X_REV_C0)
		return;

	/*
	 * Vendor PHY magic.
	 * 1000BT/AZ, wrong cable length
	 */
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		len = alc_miiext_readreg(sc, MII_EXT_PCS, MII_EXT_CLDCTL6);
		len = (len >> EXT_CLDCTL6_CAB_LEN_SHIFT) &
		    EXT_CLDCTL6_CAB_LEN_MASK;
		agc = alc_miidbg_readreg(sc, MII_DBG_AGC);
		agc = (agc >> DBG_AGC_2_VGA_SHIFT) & DBG_AGC_2_VGA_MASK;
		if ((media == IFM_1000_T && len > EXT_CLDCTL6_CAB_LEN_SHORT1G &&
		    agc > DBG_AGC_LONG1G_LIMT) ||
		    (media == IFM_100_TX && len > DBG_AGC_LONG100M_LIMT &&
		    agc > DBG_AGC_LONG1G_LIMT)) {
			alc_miidbg_writereg(sc, MII_DBG_AZ_ANADECT,
			    DBG_AZ_ANADECT_LONG);
			val = alc_miiext_readreg(sc, MII_EXT_ANEG,
			    MII_EXT_ANEG_AFE);
			val |= ANEG_AFEE_10BT_100M_TH;
			alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_AFE,
			    val);
		} else {
			alc_miidbg_writereg(sc, MII_DBG_AZ_ANADECT,
			    DBG_AZ_ANADECT_DEFAULT);
			val = alc_miiext_readreg(sc, MII_EXT_ANEG,
			    MII_EXT_ANEG_AFE);
			val &= ~ANEG_AFEE_10BT_100M_TH;
			alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_AFE,
			    val);
		}
		if ((sc->alc_flags & ALC_FLAG_LINK_WAR) != 0 &&
		    AR816X_REV(sc->alc_rev) == AR816X_REV_B0) {
			if (media == IFM_1000_T) {
				/*
				 * Giga link threshold, raise the tolerance of
				 * noise 50%.
				 */
				val = alc_miidbg_readreg(sc, MII_DBG_MSE20DB);
				val &= ~DBG_MSE20DB_TH_MASK;
				val |= (DBG_MSE20DB_TH_HI <<
				    DBG_MSE20DB_TH_SHIFT);
				alc_miidbg_writereg(sc, MII_DBG_MSE20DB, val);
			} else if (media == IFM_100_TX)
				alc_miidbg_writereg(sc, MII_DBG_MSE16DB,
				    DBG_MSE16DB_UP);
		}
	} else {
		val = alc_miiext_readreg(sc, MII_EXT_ANEG, MII_EXT_ANEG_AFE);
		val &= ~ANEG_AFEE_10BT_100M_TH;
		alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_AFE, val);
		if ((sc->alc_flags & ALC_FLAG_LINK_WAR) != 0 &&
		    AR816X_REV(sc->alc_rev) == AR816X_REV_B0) {
			alc_miidbg_writereg(sc, MII_DBG_MSE16DB,
			    DBG_MSE16DB_DOWN);
			val = alc_miidbg_readreg(sc, MII_DBG_MSE20DB);
			val &= ~DBG_MSE20DB_TH_MASK;
			val |= (DBG_MSE20DB_TH_DEFAULT << DBG_MSE20DB_TH_SHIFT);
			alc_miidbg_writereg(sc, MII_DBG_MSE20DB, val);
		}
	}
}

static void
alc_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct alc_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	ALC_LOCK(sc);
	if ((ifp->if_flags & IFF_UP) == 0) {
		ALC_UNLOCK(sc);
		return;
	}
	mii = device_get_softc(sc->alc_miibus);

	mii_pollstat(mii);
	ifmr->ifm_status = mii->mii_media_status;
	ifmr->ifm_active = mii->mii_media_active;
	ALC_UNLOCK(sc);
}

static int
alc_mediachange(struct ifnet *ifp)
{
	struct alc_softc *sc;
	int error;

	sc = ifp->if_softc;
	ALC_LOCK(sc);
	error = alc_mediachange_locked(sc);
	ALC_UNLOCK(sc);

	return (error);
}

static int
alc_mediachange_locked(struct alc_softc *sc)
{
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error;

	ALC_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->alc_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);

	return (error);
}

static struct alc_ident *
alc_find_ident(device_t dev)
{
	struct alc_ident *ident;
	uint16_t vendor, devid;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	for (ident = alc_ident_table; ident->name != NULL; ident++) {
		if (vendor == ident->vendorid && devid == ident->deviceid)
			return (ident);
	}

	return (NULL);
}

static int
alc_probe(device_t dev)
{
	struct alc_ident *ident;

	ident = alc_find_ident(dev);
	if (ident != NULL) {
		device_set_desc(dev, ident->name);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static void
alc_get_macaddr(struct alc_softc *sc)
{

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_get_macaddr_816x(sc);
	else
		alc_get_macaddr_813x(sc);
}

static void
alc_get_macaddr_813x(struct alc_softc *sc)
{
	uint32_t opt;
	uint16_t val;
	int eeprom, i;

	eeprom = 0;
	opt = CSR_READ_4(sc, ALC_OPT_CFG);
	if ((CSR_READ_4(sc, ALC_MASTER_CFG) & MASTER_OTP_SEL) != 0 &&
	    (CSR_READ_4(sc, ALC_TWSI_DEBUG) & TWSI_DEBUG_DEV_EXIST) != 0) {
		/*
		 * EEPROM found, let TWSI reload EEPROM configuration.
		 * This will set ethernet address of controller.
		 */
		eeprom++;
		switch (sc->alc_ident->deviceid) {
		case DEVICEID_ATHEROS_AR8131:
		case DEVICEID_ATHEROS_AR8132:
			if ((opt & OPT_CFG_CLK_ENB) == 0) {
				opt |= OPT_CFG_CLK_ENB;
				CSR_WRITE_4(sc, ALC_OPT_CFG, opt);
				CSR_READ_4(sc, ALC_OPT_CFG);
				DELAY(1000);
			}
			break;
		case DEVICEID_ATHEROS_AR8151:
		case DEVICEID_ATHEROS_AR8151_V2:
		case DEVICEID_ATHEROS_AR8152_B:
		case DEVICEID_ATHEROS_AR8152_B2:
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x00);
			val = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val & 0xFF7F);
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x3B);
			val = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val | 0x0008);
			DELAY(20);
			break;
		}

		CSR_WRITE_4(sc, ALC_LTSSM_ID_CFG,
		    CSR_READ_4(sc, ALC_LTSSM_ID_CFG) & ~LTSSM_ID_WRO_ENB);
		CSR_WRITE_4(sc, ALC_WOL_CFG, 0);
		CSR_READ_4(sc, ALC_WOL_CFG);

		CSR_WRITE_4(sc, ALC_TWSI_CFG, CSR_READ_4(sc, ALC_TWSI_CFG) |
		    TWSI_CFG_SW_LD_START);
		for (i = 100; i > 0; i--) {
			DELAY(1000);
			if ((CSR_READ_4(sc, ALC_TWSI_CFG) &
			    TWSI_CFG_SW_LD_START) == 0)
				break;
		}
		if (i == 0)
			device_printf(sc->alc_dev,
			    "reloading EEPROM timeout!\n");
	} else {
		if (bootverbose)
			device_printf(sc->alc_dev, "EEPROM not found!\n");
	}
	if (eeprom != 0) {
		switch (sc->alc_ident->deviceid) {
		case DEVICEID_ATHEROS_AR8131:
		case DEVICEID_ATHEROS_AR8132:
			if ((opt & OPT_CFG_CLK_ENB) != 0) {
				opt &= ~OPT_CFG_CLK_ENB;
				CSR_WRITE_4(sc, ALC_OPT_CFG, opt);
				CSR_READ_4(sc, ALC_OPT_CFG);
				DELAY(1000);
			}
			break;
		case DEVICEID_ATHEROS_AR8151:
		case DEVICEID_ATHEROS_AR8151_V2:
		case DEVICEID_ATHEROS_AR8152_B:
		case DEVICEID_ATHEROS_AR8152_B2:
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x00);
			val = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val | 0x0080);
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_ADDR, 0x3B);
			val = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA);
			alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
			    ALC_MII_DBG_DATA, val & 0xFFF7);
			DELAY(20);
			break;
		}
	}

	alc_get_macaddr_par(sc);
}

static void
alc_get_macaddr_816x(struct alc_softc *sc)
{
	uint32_t reg;
	int i, reloaded;

	reloaded = 0;
	/* Try to reload station address via TWSI. */
	for (i = 100; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_SLD);
		if ((reg & (SLD_PROGRESS | SLD_START)) == 0)
			break;
		DELAY(1000);
	}
	if (i != 0) {
		CSR_WRITE_4(sc, ALC_SLD, reg | SLD_START);
		for (i = 100; i > 0; i--) {
			DELAY(1000);
			reg = CSR_READ_4(sc, ALC_SLD);
			if ((reg & SLD_START) == 0)
				break;
		}
		if (i != 0)
			reloaded++;
		else if (bootverbose)
			device_printf(sc->alc_dev,
			    "reloading station address via TWSI timed out!\n");
	}

	/* Try to reload station address from EEPROM or FLASH. */
	if (reloaded == 0) {
		reg = CSR_READ_4(sc, ALC_EEPROM_LD);
		if ((reg & (EEPROM_LD_EEPROM_EXIST |
		    EEPROM_LD_FLASH_EXIST)) != 0) {
			for (i = 100; i > 0; i--) {
				reg = CSR_READ_4(sc, ALC_EEPROM_LD);
				if ((reg & (EEPROM_LD_PROGRESS |
				    EEPROM_LD_START)) == 0)
					break;
				DELAY(1000);
			}
			if (i != 0) {
				CSR_WRITE_4(sc, ALC_EEPROM_LD, reg |
				    EEPROM_LD_START);
				for (i = 100; i > 0; i--) {
					DELAY(1000);
					reg = CSR_READ_4(sc, ALC_EEPROM_LD);
					if ((reg & EEPROM_LD_START) == 0)
						break;
				}
			} else if (bootverbose)
				device_printf(sc->alc_dev,
				    "reloading EEPROM/FLASH timed out!\n");
		}
	}

	alc_get_macaddr_par(sc);
}

static void
alc_get_macaddr_par(struct alc_softc *sc)
{
	uint32_t ea[2];

	ea[0] = CSR_READ_4(sc, ALC_PAR0);
	ea[1] = CSR_READ_4(sc, ALC_PAR1);
	sc->alc_eaddr[0] = (ea[1] >> 8) & 0xFF;
	sc->alc_eaddr[1] = (ea[1] >> 0) & 0xFF;
	sc->alc_eaddr[2] = (ea[0] >> 24) & 0xFF;
	sc->alc_eaddr[3] = (ea[0] >> 16) & 0xFF;
	sc->alc_eaddr[4] = (ea[0] >> 8) & 0xFF;
	sc->alc_eaddr[5] = (ea[0] >> 0) & 0xFF;
}

static void
alc_disable_l0s_l1(struct alc_softc *sc)
{
	uint32_t pmcfg;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* Another magic from vendor. */
		pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
		pmcfg &= ~(PM_CFG_L1_ENTRY_TIMER_MASK | PM_CFG_CLK_SWH_L1 |
		    PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB |
		    PM_CFG_MAC_ASPM_CHK | PM_CFG_SERDES_PD_EX_L1);
		pmcfg |= PM_CFG_SERDES_BUDS_RX_L1_ENB |
		    PM_CFG_SERDES_PLL_L1_ENB | PM_CFG_SERDES_L1_ENB;
		CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
	}
}

static void
alc_phy_reset(struct alc_softc *sc)
{

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_phy_reset_816x(sc);
	else
		alc_phy_reset_813x(sc);
}

static void
alc_phy_reset_813x(struct alc_softc *sc)
{
	uint16_t data;

	/* Reset magic from Linux. */
	CSR_WRITE_2(sc, ALC_GPHY_CFG, GPHY_CFG_SEL_ANA_RESET);
	CSR_READ_2(sc, ALC_GPHY_CFG);
	DELAY(10 * 1000);

	CSR_WRITE_2(sc, ALC_GPHY_CFG, GPHY_CFG_EXT_RESET |
	    GPHY_CFG_SEL_ANA_RESET);
	CSR_READ_2(sc, ALC_GPHY_CFG);
	DELAY(10 * 1000);

	/* DSP fixup, Vendor magic. */
	if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B) {
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x000A);
		data = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA);
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, data & 0xDFFF);
	}
	if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151_V2 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B2) {
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x003B);
		data = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA);
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, data & 0xFFF7);
		DELAY(20 * 1000);
	}
	if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151) {
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x0029);
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, 0x929D);
	}
	if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8131 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8132 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151_V2 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B2) {
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_ADDR, 0x0029);
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    ALC_MII_DBG_DATA, 0xB6DD);
	}

	/* Load DSP codes, vendor magic. */
	data = ANA_LOOP_SEL_10BT | ANA_EN_MASK_TB | ANA_EN_10BT_IDLE |
	    ((1 << ANA_INTERVAL_SEL_TIMER_SHIFT) & ANA_INTERVAL_SEL_TIMER_MASK);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG18);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((2 << ANA_SERDES_CDR_BW_SHIFT) & ANA_SERDES_CDR_BW_MASK) |
	    ANA_SERDES_EN_DEEM | ANA_SERDES_SEL_HSP | ANA_SERDES_EN_PLL |
	    ANA_SERDES_EN_LCKDT;
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG5);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((44 << ANA_LONG_CABLE_TH_100_SHIFT) &
	    ANA_LONG_CABLE_TH_100_MASK) |
	    ((33 << ANA_SHORT_CABLE_TH_100_SHIFT) &
	    ANA_SHORT_CABLE_TH_100_SHIFT) |
	    ANA_BP_BAD_LINK_ACCUM | ANA_BP_SMALL_BW;
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG54);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((11 << ANA_IECHO_ADJ_3_SHIFT) & ANA_IECHO_ADJ_3_MASK) |
	    ((11 << ANA_IECHO_ADJ_2_SHIFT) & ANA_IECHO_ADJ_2_MASK) |
	    ((8 << ANA_IECHO_ADJ_1_SHIFT) & ANA_IECHO_ADJ_1_MASK) |
	    ((8 << ANA_IECHO_ADJ_0_SHIFT) & ANA_IECHO_ADJ_0_MASK);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG4);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);

	data = ((7 & ANA_MANUL_SWICH_ON_SHIFT) & ANA_MANUL_SWICH_ON_MASK) |
	    ANA_RESTART_CAL | ANA_MAN_ENABLE | ANA_SEL_HSP | ANA_EN_HB |
	    ANA_OEN_125M;
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_ADDR, MII_ANA_CFG0);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA, data);
	DELAY(1000);

	/* Disable hibernation. */
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    0x0029);
	data = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA);
	data &= ~0x8000;
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr, ALC_MII_DBG_DATA,
	    data);

	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr, ALC_MII_DBG_ADDR,
	    0x000B);
	data = alc_miibus_readreg(sc->alc_dev, sc->alc_phyaddr,
	    ALC_MII_DBG_DATA);
	data &= ~0x8000;
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr, ALC_MII_DBG_DATA,
	    data);
}

static void
alc_phy_reset_816x(struct alc_softc *sc)
{
	uint32_t val;

	val = CSR_READ_4(sc, ALC_GPHY_CFG);
	val &= ~(GPHY_CFG_EXT_RESET | GPHY_CFG_LED_MODE |
	    GPHY_CFG_GATE_25M_ENB | GPHY_CFG_PHY_IDDQ | GPHY_CFG_PHY_PLL_ON |
	    GPHY_CFG_PWDOWN_HW | GPHY_CFG_100AB_ENB);
	val |= GPHY_CFG_SEL_ANA_RESET;
#ifdef notyet
	val |= GPHY_CFG_HIB_PULSE | GPHY_CFG_HIB_EN | GPHY_CFG_SEL_ANA_RESET;
#else
	/* Disable PHY hibernation. */
	val &= ~(GPHY_CFG_HIB_PULSE | GPHY_CFG_HIB_EN);
#endif
	CSR_WRITE_4(sc, ALC_GPHY_CFG, val);
	DELAY(10);
	CSR_WRITE_4(sc, ALC_GPHY_CFG, val | GPHY_CFG_EXT_RESET);
	DELAY(800);

	/* Vendor PHY magic. */
#ifdef notyet
	alc_miidbg_writereg(sc, MII_DBG_LEGCYPS, DBG_LEGCYPS_DEFAULT);
	alc_miidbg_writereg(sc, MII_DBG_SYSMODCTL, DBG_SYSMODCTL_DEFAULT);
	alc_miiext_writereg(sc, MII_EXT_PCS, MII_EXT_VDRVBIAS,
	    EXT_VDRVBIAS_DEFAULT);
#else
	/* Disable PHY hibernation. */
	alc_miidbg_writereg(sc, MII_DBG_LEGCYPS,
	    DBG_LEGCYPS_DEFAULT & ~DBG_LEGCYPS_ENB);
	alc_miidbg_writereg(sc, MII_DBG_HIBNEG,
	    DBG_HIBNEG_DEFAULT & ~(DBG_HIBNEG_PSHIB_EN | DBG_HIBNEG_HIB_PULSE));
	alc_miidbg_writereg(sc, MII_DBG_GREENCFG, DBG_GREENCFG_DEFAULT);
#endif

	/* XXX Disable EEE. */
	val = CSR_READ_4(sc, ALC_LPI_CTL);
	val &= ~LPI_CTL_ENB;
	CSR_WRITE_4(sc, ALC_LPI_CTL, val);
	alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_LOCAL_EEEADV, 0);

	/* PHY power saving. */
	alc_miidbg_writereg(sc, MII_DBG_TST10BTCFG, DBG_TST10BTCFG_DEFAULT);
	alc_miidbg_writereg(sc, MII_DBG_SRDSYSMOD, DBG_SRDSYSMOD_DEFAULT);
	alc_miidbg_writereg(sc, MII_DBG_TST100BTCFG, DBG_TST100BTCFG_DEFAULT);
	alc_miidbg_writereg(sc, MII_DBG_ANACTL, DBG_ANACTL_DEFAULT);
	val = alc_miidbg_readreg(sc, MII_DBG_GREENCFG2);
	val &= ~DBG_GREENCFG2_GATE_DFSE_EN;
	alc_miidbg_writereg(sc, MII_DBG_GREENCFG2, val);

	/* RTL8139C, 120m issue. */
	alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_NLP78,
	    ANEG_NLP78_120M_DEFAULT);
	alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_S3DIG10,
	    ANEG_S3DIG10_DEFAULT);

	if ((sc->alc_flags & ALC_FLAG_LINK_WAR) != 0) {
		/* Turn off half amplitude. */
		val = alc_miiext_readreg(sc, MII_EXT_PCS, MII_EXT_CLDCTL3);
		val |= EXT_CLDCTL3_BP_CABLE1TH_DET_GT;
		alc_miiext_writereg(sc, MII_EXT_PCS, MII_EXT_CLDCTL3, val);
		/* Turn off Green feature. */
		val = alc_miidbg_readreg(sc, MII_DBG_GREENCFG2);
		val |= DBG_GREENCFG2_BP_GREEN;
		alc_miidbg_writereg(sc, MII_DBG_GREENCFG2, val);
		/* Turn off half bias. */
		val = alc_miiext_readreg(sc, MII_EXT_PCS, MII_EXT_CLDCTL5);
		val |= EXT_CLDCTL5_BP_VD_HLFBIAS;
		alc_miiext_writereg(sc, MII_EXT_PCS, MII_EXT_CLDCTL5, val);
	}
}

static void
alc_phy_down(struct alc_softc *sc)
{
	uint32_t gphy;

	switch (sc->alc_ident->deviceid) {
	case DEVICEID_ATHEROS_AR8161:
	case DEVICEID_ATHEROS_E2200:
	case DEVICEID_ATHEROS_E2400:
	case DEVICEID_ATHEROS_E2500:
	case DEVICEID_ATHEROS_AR8162:
	case DEVICEID_ATHEROS_AR8171:
	case DEVICEID_ATHEROS_AR8172:
		gphy = CSR_READ_4(sc, ALC_GPHY_CFG);
		gphy &= ~(GPHY_CFG_EXT_RESET | GPHY_CFG_LED_MODE |
		    GPHY_CFG_100AB_ENB | GPHY_CFG_PHY_PLL_ON);
		gphy |= GPHY_CFG_HIB_EN | GPHY_CFG_HIB_PULSE |
		    GPHY_CFG_SEL_ANA_RESET;
		gphy |= GPHY_CFG_PHY_IDDQ | GPHY_CFG_PWDOWN_HW;
		CSR_WRITE_4(sc, ALC_GPHY_CFG, gphy);
		break;
	case DEVICEID_ATHEROS_AR8151:
	case DEVICEID_ATHEROS_AR8151_V2:
	case DEVICEID_ATHEROS_AR8152_B:
	case DEVICEID_ATHEROS_AR8152_B2:
		/*
		 * GPHY power down caused more problems on AR8151 v2.0.
		 * When driver is reloaded after GPHY power down,
		 * accesses to PHY/MAC registers hung the system. Only
		 * cold boot recovered from it.  I'm not sure whether
		 * AR8151 v1.0 also requires this one though.  I don't
		 * have AR8151 v1.0 controller in hand.
		 * The only option left is to isolate the PHY and
		 * initiates power down the PHY which in turn saves
		 * more power when driver is unloaded.
		 */
		alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
		    MII_BMCR, BMCR_ISO | BMCR_PDOWN);
		break;
	default:
		/* Force PHY down. */
		CSR_WRITE_2(sc, ALC_GPHY_CFG, GPHY_CFG_EXT_RESET |
		    GPHY_CFG_SEL_ANA_RESET | GPHY_CFG_PHY_IDDQ |
		    GPHY_CFG_PWDOWN_HW);
		DELAY(1000);
		break;
	}
}

static void
alc_aspm(struct alc_softc *sc, int init, int media)
{

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_aspm_816x(sc, init);
	else
		alc_aspm_813x(sc, media);
}

static void
alc_aspm_813x(struct alc_softc *sc, int media)
{
	uint32_t pmcfg;
	uint16_t linkcfg;

	if ((sc->alc_flags & ALC_FLAG_LINK) == 0)
		return;

	pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
	if ((sc->alc_flags & (ALC_FLAG_APS | ALC_FLAG_PCIE)) ==
	    (ALC_FLAG_APS | ALC_FLAG_PCIE))
		linkcfg = CSR_READ_2(sc, sc->alc_expcap +
		    PCIER_LINK_CTL);
	else
		linkcfg = 0;
	pmcfg &= ~PM_CFG_SERDES_PD_EX_L1;
	pmcfg &= ~(PM_CFG_L1_ENTRY_TIMER_MASK | PM_CFG_LCKDET_TIMER_MASK);
	pmcfg |= PM_CFG_MAC_ASPM_CHK;
	pmcfg |= (PM_CFG_LCKDET_TIMER_DEFAULT << PM_CFG_LCKDET_TIMER_SHIFT);
	pmcfg &= ~(PM_CFG_ASPM_L1_ENB | PM_CFG_ASPM_L0S_ENB);

	if ((sc->alc_flags & ALC_FLAG_APS) != 0) {
		/* Disable extended sync except AR8152 B v1.0 */
		linkcfg &= ~PCIEM_LINK_CTL_EXTENDED_SYNC;
		if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B &&
		    sc->alc_rev == ATHEROS_AR8152_B_V10)
			linkcfg |= PCIEM_LINK_CTL_EXTENDED_SYNC;
		CSR_WRITE_2(sc, sc->alc_expcap + PCIER_LINK_CTL,
		    linkcfg);
		pmcfg &= ~(PM_CFG_EN_BUFS_RX_L0S | PM_CFG_SA_DLY_ENB |
		    PM_CFG_HOTRST);
		pmcfg |= (PM_CFG_L1_ENTRY_TIMER_DEFAULT <<
		    PM_CFG_L1_ENTRY_TIMER_SHIFT);
		pmcfg &= ~PM_CFG_PM_REQ_TIMER_MASK;
		pmcfg |= (PM_CFG_PM_REQ_TIMER_DEFAULT <<
		    PM_CFG_PM_REQ_TIMER_SHIFT);
		pmcfg |= PM_CFG_SERDES_PD_EX_L1 | PM_CFG_PCIE_RECV;
	}

	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		if ((sc->alc_flags & ALC_FLAG_L0S) != 0)
			pmcfg |= PM_CFG_ASPM_L0S_ENB;
		if ((sc->alc_flags & ALC_FLAG_L1S) != 0)
			pmcfg |= PM_CFG_ASPM_L1_ENB;
		if ((sc->alc_flags & ALC_FLAG_APS) != 0) {
			if (sc->alc_ident->deviceid ==
			    DEVICEID_ATHEROS_AR8152_B)
				pmcfg &= ~PM_CFG_ASPM_L0S_ENB;
			pmcfg &= ~(PM_CFG_SERDES_L1_ENB |
			    PM_CFG_SERDES_PLL_L1_ENB |
			    PM_CFG_SERDES_BUDS_RX_L1_ENB);
			pmcfg |= PM_CFG_CLK_SWH_L1;
			if (media == IFM_100_TX || media == IFM_1000_T) {
				pmcfg &= ~PM_CFG_L1_ENTRY_TIMER_MASK;
				switch (sc->alc_ident->deviceid) {
				case DEVICEID_ATHEROS_AR8152_B:
					pmcfg |= (7 <<
					    PM_CFG_L1_ENTRY_TIMER_SHIFT);
					break;
				case DEVICEID_ATHEROS_AR8152_B2:
				case DEVICEID_ATHEROS_AR8151_V2:
					pmcfg |= (4 <<
					    PM_CFG_L1_ENTRY_TIMER_SHIFT);
					break;
				default:
					pmcfg |= (15 <<
					    PM_CFG_L1_ENTRY_TIMER_SHIFT);
					break;
				}
			}
		} else {
			pmcfg |= PM_CFG_SERDES_L1_ENB |
			    PM_CFG_SERDES_PLL_L1_ENB |
			    PM_CFG_SERDES_BUDS_RX_L1_ENB;
			pmcfg &= ~(PM_CFG_CLK_SWH_L1 |
			    PM_CFG_ASPM_L1_ENB | PM_CFG_ASPM_L0S_ENB);
		}
	} else {
		pmcfg &= ~(PM_CFG_SERDES_BUDS_RX_L1_ENB | PM_CFG_SERDES_L1_ENB |
		    PM_CFG_SERDES_PLL_L1_ENB);
		pmcfg |= PM_CFG_CLK_SWH_L1;
		if ((sc->alc_flags & ALC_FLAG_L1S) != 0)
			pmcfg |= PM_CFG_ASPM_L1_ENB;
	}
	CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
}

static void
alc_aspm_816x(struct alc_softc *sc, int init)
{
	uint32_t pmcfg;

	pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
	pmcfg &= ~PM_CFG_L1_ENTRY_TIMER_816X_MASK;
	pmcfg |= PM_CFG_L1_ENTRY_TIMER_816X_DEFAULT;
	pmcfg &= ~PM_CFG_PM_REQ_TIMER_MASK;
	pmcfg |= PM_CFG_PM_REQ_TIMER_816X_DEFAULT;
	pmcfg &= ~PM_CFG_LCKDET_TIMER_MASK;
	pmcfg |= PM_CFG_LCKDET_TIMER_DEFAULT;
	pmcfg |= PM_CFG_SERDES_PD_EX_L1 | PM_CFG_CLK_SWH_L1 | PM_CFG_PCIE_RECV;
	pmcfg &= ~(PM_CFG_RX_L1_AFTER_L0S | PM_CFG_TX_L1_AFTER_L0S |
	    PM_CFG_ASPM_L1_ENB | PM_CFG_ASPM_L0S_ENB |
	    PM_CFG_SERDES_L1_ENB | PM_CFG_SERDES_PLL_L1_ENB |
	    PM_CFG_SERDES_BUDS_RX_L1_ENB | PM_CFG_SA_DLY_ENB |
	    PM_CFG_MAC_ASPM_CHK | PM_CFG_HOTRST);
	if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
	    (sc->alc_rev & 0x01) != 0)
		pmcfg |= PM_CFG_SERDES_L1_ENB | PM_CFG_SERDES_PLL_L1_ENB;
	if ((sc->alc_flags & ALC_FLAG_LINK) != 0) {
		/* Link up, enable both L0s, L1s. */
		pmcfg |= PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB |
		    PM_CFG_MAC_ASPM_CHK;
	} else {
		if (init != 0)
			pmcfg |= PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB |
			    PM_CFG_MAC_ASPM_CHK;
		else if ((sc->alc_ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			pmcfg |= PM_CFG_ASPM_L1_ENB | PM_CFG_MAC_ASPM_CHK;
	}
	CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
}

static void
alc_init_pcie(struct alc_softc *sc)
{
	const char *aspm_state[] = { "L0s/L1", "L0s", "L1", "L0s/L1" };
	uint32_t cap, ctl, val;
	int state;

	/* Clear data link and flow-control protocol error. */
	val = CSR_READ_4(sc, ALC_PEX_UNC_ERR_SEV);
	val &= ~(PEX_UNC_ERR_SEV_DLP | PEX_UNC_ERR_SEV_FCP);
	CSR_WRITE_4(sc, ALC_PEX_UNC_ERR_SEV, val);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		CSR_WRITE_4(sc, ALC_LTSSM_ID_CFG,
		    CSR_READ_4(sc, ALC_LTSSM_ID_CFG) & ~LTSSM_ID_WRO_ENB);
		CSR_WRITE_4(sc, ALC_PCIE_PHYMISC,
		    CSR_READ_4(sc, ALC_PCIE_PHYMISC) |
		    PCIE_PHYMISC_FORCE_RCV_DET);
		if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B &&
		    sc->alc_rev == ATHEROS_AR8152_B_V10) {
			val = CSR_READ_4(sc, ALC_PCIE_PHYMISC2);
			val &= ~(PCIE_PHYMISC2_SERDES_CDR_MASK |
			    PCIE_PHYMISC2_SERDES_TH_MASK);
			val |= 3 << PCIE_PHYMISC2_SERDES_CDR_SHIFT;
			val |= 3 << PCIE_PHYMISC2_SERDES_TH_SHIFT;
			CSR_WRITE_4(sc, ALC_PCIE_PHYMISC2, val);
		}
		/* Disable ASPM L0S and L1. */
		cap = CSR_READ_2(sc, sc->alc_expcap + PCIER_LINK_CAP);
		if ((cap & PCIEM_LINK_CAP_ASPM) != 0) {
			ctl = CSR_READ_2(sc, sc->alc_expcap + PCIER_LINK_CTL);
			if ((ctl & PCIEM_LINK_CTL_RCB) != 0)
				sc->alc_rcb = DMA_CFG_RCB_128;
			if (bootverbose)
				device_printf(sc->alc_dev, "RCB %u bytes\n",
				    sc->alc_rcb == DMA_CFG_RCB_64 ? 64 : 128);
			state = ctl & PCIEM_LINK_CTL_ASPMC;
			if (state & PCIEM_LINK_CTL_ASPMC_L0S)
				sc->alc_flags |= ALC_FLAG_L0S;
			if (state & PCIEM_LINK_CTL_ASPMC_L1)
				sc->alc_flags |= ALC_FLAG_L1S;
			if (bootverbose)
				device_printf(sc->alc_dev, "ASPM %s %s\n",
				    aspm_state[state],
				    state == 0 ? "disabled" : "enabled");
			alc_disable_l0s_l1(sc);
		} else {
			if (bootverbose)
				device_printf(sc->alc_dev,
				    "no ASPM support\n");
		}
	} else {
		val = CSR_READ_4(sc, ALC_PDLL_TRNS1);
		val &= ~PDLL_TRNS1_D3PLLOFF_ENB;
		CSR_WRITE_4(sc, ALC_PDLL_TRNS1, val);
		val = CSR_READ_4(sc, ALC_MASTER_CFG);
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
		    (sc->alc_rev & 0x01) != 0) {
			if ((val & MASTER_WAKEN_25M) == 0 ||
			    (val & MASTER_CLK_SEL_DIS) == 0) {
				val |= MASTER_WAKEN_25M | MASTER_CLK_SEL_DIS;
				CSR_WRITE_4(sc, ALC_MASTER_CFG, val);
			}
		} else {
			if ((val & MASTER_WAKEN_25M) == 0 ||
			    (val & MASTER_CLK_SEL_DIS) != 0) {
				val |= MASTER_WAKEN_25M;
				val &= ~MASTER_CLK_SEL_DIS;
				CSR_WRITE_4(sc, ALC_MASTER_CFG, val);
			}
		}
	}
	alc_aspm(sc, 1, IFM_UNKNOWN);
}

static void
alc_config_msi(struct alc_softc *sc)
{
	uint32_t ctl, mod;

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		/*
		 * It seems interrupt moderation is controlled by
		 * ALC_MSI_RETRANS_TIMER register if MSI/MSIX is active.
		 * Driver uses RX interrupt moderation parameter to
		 * program ALC_MSI_RETRANS_TIMER register.
		 */
		ctl = CSR_READ_4(sc, ALC_MSI_RETRANS_TIMER);
		ctl &= ~MSI_RETRANS_TIMER_MASK;
		ctl &= ~MSI_RETRANS_MASK_SEL_LINE;
		mod = ALC_USECS(sc->alc_int_rx_mod);
		if (mod == 0)
			mod = 1;
		ctl |= mod;
		if ((sc->alc_flags & ALC_FLAG_MSIX) != 0)
			CSR_WRITE_4(sc, ALC_MSI_RETRANS_TIMER, ctl |
			    MSI_RETRANS_MASK_SEL_STD);
		else if ((sc->alc_flags & ALC_FLAG_MSI) != 0)
			CSR_WRITE_4(sc, ALC_MSI_RETRANS_TIMER, ctl |
			    MSI_RETRANS_MASK_SEL_LINE);
		else
			CSR_WRITE_4(sc, ALC_MSI_RETRANS_TIMER, 0);
	}
}

static int
alc_attach(device_t dev)
{
	struct alc_softc *sc;
	struct ifnet *ifp;
	int base, error, i, msic, msixc;
	uint16_t burst;

	error = 0;
	sc = device_get_softc(dev);
	sc->alc_dev = dev;
	sc->alc_rev = pci_get_revid(dev);

	mtx_init(&sc->alc_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->alc_tick_ch, &sc->alc_mtx, 0);
	TASK_INIT(&sc->alc_int_task, 0, alc_int_task, sc);
	sc->alc_ident = alc_find_ident(dev);

	/* Map the device. */
	pci_enable_busmaster(dev);
	sc->alc_res_spec = alc_res_spec_mem;
	sc->alc_irq_spec = alc_irq_spec_legacy;
	error = bus_alloc_resources(dev, sc->alc_res_spec, sc->alc_res);
	if (error != 0) {
		device_printf(dev, "cannot allocate memory resources.\n");
		goto fail;
	}

	/* Set PHY address. */
	sc->alc_phyaddr = ALC_PHY_ADDR;

	/*
	 * One odd thing is AR8132 uses the same PHY hardware(F1
	 * gigabit PHY) of AR8131. So atphy(4) of AR8132 reports
	 * the PHY supports 1000Mbps but that's not true. The PHY
	 * used in AR8132 can't establish gigabit link even if it
	 * shows the same PHY model/revision number of AR8131.
	 */
	switch (sc->alc_ident->deviceid) {
	case DEVICEID_ATHEROS_E2200:
	case DEVICEID_ATHEROS_E2400:
	case DEVICEID_ATHEROS_E2500:
		sc->alc_flags |= ALC_FLAG_E2X00;
		/* FALLTHROUGH */
	case DEVICEID_ATHEROS_AR8161:
		if (pci_get_subvendor(dev) == VENDORID_ATHEROS &&
		    pci_get_subdevice(dev) == 0x0091 && sc->alc_rev == 0)
			sc->alc_flags |= ALC_FLAG_LINK_WAR;
		/* FALLTHROUGH */
	case DEVICEID_ATHEROS_AR8171:
		sc->alc_flags |= ALC_FLAG_AR816X_FAMILY;
		break;
	case DEVICEID_ATHEROS_AR8162:
	case DEVICEID_ATHEROS_AR8172:
		sc->alc_flags |= ALC_FLAG_FASTETHER | ALC_FLAG_AR816X_FAMILY;
		break;
	case DEVICEID_ATHEROS_AR8152_B:
	case DEVICEID_ATHEROS_AR8152_B2:
		sc->alc_flags |= ALC_FLAG_APS;
		/* FALLTHROUGH */
	case DEVICEID_ATHEROS_AR8132:
		sc->alc_flags |= ALC_FLAG_FASTETHER;
		break;
	case DEVICEID_ATHEROS_AR8151:
	case DEVICEID_ATHEROS_AR8151_V2:
		sc->alc_flags |= ALC_FLAG_APS;
		/* FALLTHROUGH */
	default:
		break;
	}
	sc->alc_flags |= ALC_FLAG_JUMBO;

	/*
	 * It seems that AR813x/AR815x has silicon bug for SMB. In
	 * addition, Atheros said that enabling SMB wouldn't improve
	 * performance. However I think it's bad to access lots of
	 * registers to extract MAC statistics.
	 */
	sc->alc_flags |= ALC_FLAG_SMB_BUG;
	/*
	 * Don't use Tx CMB. It is known to have silicon bug.
	 */
	sc->alc_flags |= ALC_FLAG_CMB_BUG;
	sc->alc_chip_rev = CSR_READ_4(sc, ALC_MASTER_CFG) >>
	    MASTER_CHIP_REV_SHIFT;
	if (bootverbose) {
		device_printf(dev, "PCI device revision : 0x%04x\n",
		    sc->alc_rev);
		device_printf(dev, "Chip id/revision : 0x%04x\n",
		    sc->alc_chip_rev);
		if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
			device_printf(dev, "AR816x revision : 0x%x\n",
			    AR816X_REV(sc->alc_rev));
	}
	device_printf(dev, "%u Tx FIFO, %u Rx FIFO\n",
	    CSR_READ_4(sc, ALC_SRAM_TX_FIFO_LEN) * 8,
	    CSR_READ_4(sc, ALC_SRAM_RX_FIFO_LEN) * 8);

	/* Initialize DMA parameters. */
	sc->alc_dma_rd_burst = 0;
	sc->alc_dma_wr_burst = 0;
	sc->alc_rcb = DMA_CFG_RCB_64;
	if (pci_find_cap(dev, PCIY_EXPRESS, &base) == 0) {
		sc->alc_flags |= ALC_FLAG_PCIE;
		sc->alc_expcap = base;
		burst = CSR_READ_2(sc, base + PCIER_DEVICE_CTL);
		sc->alc_dma_rd_burst =
		    (burst & PCIEM_CTL_MAX_READ_REQUEST) >> 12;
		sc->alc_dma_wr_burst = (burst & PCIEM_CTL_MAX_PAYLOAD) >> 5;
		if (bootverbose) {
			device_printf(dev, "Read request size : %u bytes.\n",
			    alc_dma_burst[sc->alc_dma_rd_burst]);
			device_printf(dev, "TLP payload size : %u bytes.\n",
			    alc_dma_burst[sc->alc_dma_wr_burst]);
		}
		if (alc_dma_burst[sc->alc_dma_rd_burst] > 1024)
			sc->alc_dma_rd_burst = 3;
		if (alc_dma_burst[sc->alc_dma_wr_burst] > 1024)
			sc->alc_dma_wr_burst = 3;
		/*
		 * Force maximum payload size to 128 bytes for
		 * E2200/E2400/E2500.
		 * Otherwise it triggers DMA write error.
		 */
		if ((sc->alc_flags & ALC_FLAG_E2X00) != 0)
			sc->alc_dma_wr_burst = 0;
		alc_init_pcie(sc);
	}

	/* Reset PHY. */
	alc_phy_reset(sc);

	/* Reset the ethernet controller. */
	alc_stop_mac(sc);
	alc_reset(sc);

	/* Allocate IRQ resources. */
	msixc = pci_msix_count(dev);
	msic = pci_msi_count(dev);
	if (bootverbose) {
		device_printf(dev, "MSIX count : %d\n", msixc);
		device_printf(dev, "MSI count : %d\n", msic);
	}
	if (msixc > 1)
		msixc = 1;
	if (msic > 1)
		msic = 1;
	/*
	 * Prefer MSIX over MSI.
	 * AR816x controller has a silicon bug that MSI interrupt
	 * does not assert if PCIM_CMD_INTxDIS bit of command
	 * register is set.  pci(4) was taught to handle that case.
	 */
	if (msix_disable == 0 || msi_disable == 0) {
		if (msix_disable == 0 && msixc > 0 &&
		    pci_alloc_msix(dev, &msixc) == 0) {
			if (msic == 1) {
				device_printf(dev,
				    "Using %d MSIX message(s).\n", msixc);
				sc->alc_flags |= ALC_FLAG_MSIX;
				sc->alc_irq_spec = alc_irq_spec_msix;
			} else
				pci_release_msi(dev);
		}
		if (msi_disable == 0 && (sc->alc_flags & ALC_FLAG_MSIX) == 0 &&
		    msic > 0 && pci_alloc_msi(dev, &msic) == 0) {
			if (msic == 1) {
				device_printf(dev,
				    "Using %d MSI message(s).\n", msic);
				sc->alc_flags |= ALC_FLAG_MSI;
				sc->alc_irq_spec = alc_irq_spec_msi;
			} else
				pci_release_msi(dev);
		}
	}

	error = bus_alloc_resources(dev, sc->alc_irq_spec, sc->alc_irq);
	if (error != 0) {
		device_printf(dev, "cannot allocate IRQ resources.\n");
		goto fail;
	}

	/* Create device sysctl node. */
	alc_sysctl_node(sc);

	if ((error = alc_dma_alloc(sc)) != 0)
		goto fail;

	/* Load station address. */
	alc_get_macaddr(sc);

	ifp = sc->alc_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "cannot allocate ifnet structure.\n");
		error = ENXIO;
		goto fail;
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = alc_ioctl;
	ifp->if_start = alc_start;
	ifp->if_init = alc_init;
	ifp->if_snd.ifq_drv_maxlen = ALC_TX_RING_CNT - 1;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifp->if_snd.ifq_drv_maxlen);
	IFQ_SET_READY(&ifp->if_snd);
	ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_TSO4;
	ifp->if_hwassist = ALC_CSUM_FEATURES | CSUM_TSO;
	if (pci_find_cap(dev, PCIY_PMG, &base) == 0) {
		ifp->if_capabilities |= IFCAP_WOL_MAGIC | IFCAP_WOL_MCAST;
		sc->alc_flags |= ALC_FLAG_PM;
		sc->alc_pmcap = base;
	}
	ifp->if_capenable = ifp->if_capabilities;

	/* Set up MII bus. */
	error = mii_attach(dev, &sc->alc_miibus, ifp, alc_mediachange,
	    alc_mediastatus, BMSR_DEFCAPMASK, sc->alc_phyaddr, MII_OFFSET_ANY,
	    MIIF_DOPAUSE);
	if (error != 0) {
		device_printf(dev, "attaching PHYs failed\n");
		goto fail;
	}

	ether_ifattach(ifp, sc->alc_eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING |
	    IFCAP_VLAN_HWCSUM | IFCAP_VLAN_HWTSO;
	ifp->if_capenable = ifp->if_capabilities;
	/*
	 * XXX
	 * It seems enabling Tx checksum offloading makes more trouble.
	 * Sometimes the controller does not receive any frames when
	 * Tx checksum offloading is enabled. I'm not sure whether this
	 * is a bug in Tx checksum offloading logic or I got broken
	 * sample boards. To safety, don't enable Tx checksum offloading
	 * by default but give chance to users to toggle it if they know
	 * their controllers work without problems.
	 * Fortunately, Tx checksum offloading for AR816x family
	 * seems to work.
	 */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		ifp->if_capenable &= ~IFCAP_TXCSUM;
		ifp->if_hwassist &= ~ALC_CSUM_FEATURES;
	}

	/* Tell the upper layer(s) we support long frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/* Create local taskq. */
	sc->alc_tq = taskqueue_create_fast("alc_taskq", M_WAITOK,
	    taskqueue_thread_enqueue, &sc->alc_tq);
	if (sc->alc_tq == NULL) {
		device_printf(dev, "could not create taskqueue.\n");
		ether_ifdetach(ifp);
		error = ENXIO;
		goto fail;
	}
	taskqueue_start_threads(&sc->alc_tq, 1, PI_NET, "%s taskq",
	    device_get_nameunit(sc->alc_dev));

	alc_config_msi(sc);
	if ((sc->alc_flags & ALC_FLAG_MSIX) != 0)
		msic = ALC_MSIX_MESSAGES;
	else if ((sc->alc_flags & ALC_FLAG_MSI) != 0)
		msic = ALC_MSI_MESSAGES;
	else
		msic = 1;
	for (i = 0; i < msic; i++) {
		error = bus_setup_intr(dev, sc->alc_irq[i],
		    INTR_TYPE_NET | INTR_MPSAFE, alc_intr, NULL, sc,
		    &sc->alc_intrhand[i]);
		if (error != 0)
			break;
	}
	if (error != 0) {
		device_printf(dev, "could not set up interrupt handler.\n");
		taskqueue_free(sc->alc_tq);
		sc->alc_tq = NULL;
		ether_ifdetach(ifp);
		goto fail;
	}

	/* Attach driver netdump methods. */
	NETDUMP_SET(ifp, alc);

fail:
	if (error != 0)
		alc_detach(dev);

	return (error);
}

static int
alc_detach(device_t dev)
{
	struct alc_softc *sc;
	struct ifnet *ifp;
	int i, msic;

	sc = device_get_softc(dev);

	ifp = sc->alc_ifp;
	if (device_is_attached(dev)) {
		ether_ifdetach(ifp);
		ALC_LOCK(sc);
		alc_stop(sc);
		ALC_UNLOCK(sc);
		callout_drain(&sc->alc_tick_ch);
		taskqueue_drain(sc->alc_tq, &sc->alc_int_task);
	}

	if (sc->alc_tq != NULL) {
		taskqueue_drain(sc->alc_tq, &sc->alc_int_task);
		taskqueue_free(sc->alc_tq);
		sc->alc_tq = NULL;
	}

	if (sc->alc_miibus != NULL) {
		device_delete_child(dev, sc->alc_miibus);
		sc->alc_miibus = NULL;
	}
	bus_generic_detach(dev);
	alc_dma_free(sc);

	if (ifp != NULL) {
		if_free(ifp);
		sc->alc_ifp = NULL;
	}

	if ((sc->alc_flags & ALC_FLAG_MSIX) != 0)
		msic = ALC_MSIX_MESSAGES;
	else if ((sc->alc_flags & ALC_FLAG_MSI) != 0)
		msic = ALC_MSI_MESSAGES;
	else
		msic = 1;
	for (i = 0; i < msic; i++) {
		if (sc->alc_intrhand[i] != NULL) {
			bus_teardown_intr(dev, sc->alc_irq[i],
			    sc->alc_intrhand[i]);
			sc->alc_intrhand[i] = NULL;
		}
	}
	if (sc->alc_res[0] != NULL)
		alc_phy_down(sc);
	bus_release_resources(dev, sc->alc_irq_spec, sc->alc_irq);
	if ((sc->alc_flags & (ALC_FLAG_MSI | ALC_FLAG_MSIX)) != 0)
		pci_release_msi(dev);
	bus_release_resources(dev, sc->alc_res_spec, sc->alc_res);
	mtx_destroy(&sc->alc_mtx);

	return (0);
}

#define	ALC_SYSCTL_STAT_ADD32(c, h, n, p, d)	\
	    SYSCTL_ADD_UINT(c, h, OID_AUTO, n, CTLFLAG_RD, p, 0, d)
#define	ALC_SYSCTL_STAT_ADD64(c, h, n, p, d)	\
	    SYSCTL_ADD_UQUAD(c, h, OID_AUTO, n, CTLFLAG_RD, p, d)

static void
alc_sysctl_node(struct alc_softc *sc)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *parent;
	struct sysctl_oid *tree;
	struct alc_hw_stats *stats;
	int error;

	stats = &sc->alc_stats;
	ctx = device_get_sysctl_ctx(sc->alc_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc->alc_dev));

	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "int_rx_mod",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->alc_int_rx_mod, 0,
	    sysctl_hw_alc_int_mod, "I", "alc Rx interrupt moderation");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "int_tx_mod",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->alc_int_tx_mod, 0,
	    sysctl_hw_alc_int_mod, "I", "alc Tx interrupt moderation");
	/* Pull in device tunables. */
	sc->alc_int_rx_mod = ALC_IM_RX_TIMER_DEFAULT;
	error = resource_int_value(device_get_name(sc->alc_dev),
	    device_get_unit(sc->alc_dev), "int_rx_mod", &sc->alc_int_rx_mod);
	if (error == 0) {
		if (sc->alc_int_rx_mod < ALC_IM_TIMER_MIN ||
		    sc->alc_int_rx_mod > ALC_IM_TIMER_MAX) {
			device_printf(sc->alc_dev, "int_rx_mod value out of "
			    "range; using default: %d\n",
			    ALC_IM_RX_TIMER_DEFAULT);
			sc->alc_int_rx_mod = ALC_IM_RX_TIMER_DEFAULT;
		}
	}
	sc->alc_int_tx_mod = ALC_IM_TX_TIMER_DEFAULT;
	error = resource_int_value(device_get_name(sc->alc_dev),
	    device_get_unit(sc->alc_dev), "int_tx_mod", &sc->alc_int_tx_mod);
	if (error == 0) {
		if (sc->alc_int_tx_mod < ALC_IM_TIMER_MIN ||
		    sc->alc_int_tx_mod > ALC_IM_TIMER_MAX) {
			device_printf(sc->alc_dev, "int_tx_mod value out of "
			    "range; using default: %d\n",
			    ALC_IM_TX_TIMER_DEFAULT);
			sc->alc_int_tx_mod = ALC_IM_TX_TIMER_DEFAULT;
		}
	}
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "process_limit",
	    CTLTYPE_INT | CTLFLAG_RW, &sc->alc_process_limit, 0,
	    sysctl_hw_alc_proc_limit, "I",
	    "max number of Rx events to process");
	/* Pull in device tunables. */
	sc->alc_process_limit = ALC_PROC_DEFAULT;
	error = resource_int_value(device_get_name(sc->alc_dev),
	    device_get_unit(sc->alc_dev), "process_limit",
	    &sc->alc_process_limit);
	if (error == 0) {
		if (sc->alc_process_limit < ALC_PROC_MIN ||
		    sc->alc_process_limit > ALC_PROC_MAX) {
			device_printf(sc->alc_dev,
			    "process_limit value out of range; "
			    "using default: %d\n", ALC_PROC_DEFAULT);
			sc->alc_process_limit = ALC_PROC_DEFAULT;
		}
	}

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "ALC statistics");
	parent = SYSCTL_CHILDREN(tree);

	/* Rx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "Rx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	ALC_SYSCTL_STAT_ADD32(ctx, child, "good_frames",
	    &stats->rx_frames, "Good frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "good_bcast_frames",
	    &stats->rx_bcast_frames, "Good broadcast frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "good_mcast_frames",
	    &stats->rx_mcast_frames, "Good multicast frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "pause_frames",
	    &stats->rx_pause_frames, "Pause control frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "control_frames",
	    &stats->rx_control_frames, "Control frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "crc_errs",
	    &stats->rx_crcerrs, "CRC errors");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "len_errs",
	    &stats->rx_lenerrs, "Frames with length mismatched");
	ALC_SYSCTL_STAT_ADD64(ctx, child, "good_octets",
	    &stats->rx_bytes, "Good octets");
	ALC_SYSCTL_STAT_ADD64(ctx, child, "good_bcast_octets",
	    &stats->rx_bcast_bytes, "Good broadcast octets");
	ALC_SYSCTL_STAT_ADD64(ctx, child, "good_mcast_octets",
	    &stats->rx_mcast_bytes, "Good multicast octets");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "runts",
	    &stats->rx_runts, "Too short frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "fragments",
	    &stats->rx_fragments, "Fragmented frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_64",
	    &stats->rx_pkts_64, "64 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_65_127",
	    &stats->rx_pkts_65_127, "65 to 127 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_128_255",
	    &stats->rx_pkts_128_255, "128 to 255 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_256_511",
	    &stats->rx_pkts_256_511, "256 to 511 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_512_1023",
	    &stats->rx_pkts_512_1023, "512 to 1023 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_1024_1518",
	    &stats->rx_pkts_1024_1518, "1024 to 1518 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_1519_max",
	    &stats->rx_pkts_1519_max, "1519 to max frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "trunc_errs",
	    &stats->rx_pkts_truncated, "Truncated frames due to MTU size");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "fifo_oflows",
	    &stats->rx_fifo_oflows, "FIFO overflows");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "rrs_errs",
	    &stats->rx_rrs_errs, "Return status write-back errors");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "align_errs",
	    &stats->rx_alignerrs, "Alignment errors");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "filtered",
	    &stats->rx_pkts_filtered,
	    "Frames dropped due to address filtering");

	/* Tx statistics. */
	tree = SYSCTL_ADD_NODE(ctx, parent, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "Tx MAC statistics");
	child = SYSCTL_CHILDREN(tree);
	ALC_SYSCTL_STAT_ADD32(ctx, child, "good_frames",
	    &stats->tx_frames, "Good frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "good_bcast_frames",
	    &stats->tx_bcast_frames, "Good broadcast frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "good_mcast_frames",
	    &stats->tx_mcast_frames, "Good multicast frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "pause_frames",
	    &stats->tx_pause_frames, "Pause control frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "control_frames",
	    &stats->tx_control_frames, "Control frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "excess_defers",
	    &stats->tx_excess_defer, "Frames with excessive derferrals");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "defers",
	    &stats->tx_excess_defer, "Frames with derferrals");
	ALC_SYSCTL_STAT_ADD64(ctx, child, "good_octets",
	    &stats->tx_bytes, "Good octets");
	ALC_SYSCTL_STAT_ADD64(ctx, child, "good_bcast_octets",
	    &stats->tx_bcast_bytes, "Good broadcast octets");
	ALC_SYSCTL_STAT_ADD64(ctx, child, "good_mcast_octets",
	    &stats->tx_mcast_bytes, "Good multicast octets");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_64",
	    &stats->tx_pkts_64, "64 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_65_127",
	    &stats->tx_pkts_65_127, "65 to 127 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_128_255",
	    &stats->tx_pkts_128_255, "128 to 255 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_256_511",
	    &stats->tx_pkts_256_511, "256 to 511 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_512_1023",
	    &stats->tx_pkts_512_1023, "512 to 1023 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_1024_1518",
	    &stats->tx_pkts_1024_1518, "1024 to 1518 bytes frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "frames_1519_max",
	    &stats->tx_pkts_1519_max, "1519 to max frames");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "single_colls",
	    &stats->tx_single_colls, "Single collisions");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "multi_colls",
	    &stats->tx_multi_colls, "Multiple collisions");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "late_colls",
	    &stats->tx_late_colls, "Late collisions");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "excess_colls",
	    &stats->tx_excess_colls, "Excessive collisions");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "underruns",
	    &stats->tx_underrun, "FIFO underruns");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "desc_underruns",
	    &stats->tx_desc_underrun, "Descriptor write-back errors");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "len_errs",
	    &stats->tx_lenerrs, "Frames with length mismatched");
	ALC_SYSCTL_STAT_ADD32(ctx, child, "trunc_errs",
	    &stats->tx_pkts_truncated, "Truncated frames due to MTU size");
}

#undef ALC_SYSCTL_STAT_ADD32
#undef ALC_SYSCTL_STAT_ADD64

struct alc_dmamap_arg {
	bus_addr_t	alc_busaddr;
};

static void
alc_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
	struct alc_dmamap_arg *ctx;

	if (error != 0)
		return;

	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	ctx = (struct alc_dmamap_arg *)arg;
	ctx->alc_busaddr = segs[0].ds_addr;
}

/*
 * Normal and high Tx descriptors shares single Tx high address.
 * Four Rx descriptor/return rings and CMB shares the same Rx
 * high address.
 */
static int
alc_check_boundary(struct alc_softc *sc)
{
	bus_addr_t cmb_end, rx_ring_end, rr_ring_end, tx_ring_end;

	rx_ring_end = sc->alc_rdata.alc_rx_ring_paddr + ALC_RX_RING_SZ;
	rr_ring_end = sc->alc_rdata.alc_rr_ring_paddr + ALC_RR_RING_SZ;
	cmb_end = sc->alc_rdata.alc_cmb_paddr + ALC_CMB_SZ;
	tx_ring_end = sc->alc_rdata.alc_tx_ring_paddr + ALC_TX_RING_SZ;

	/* 4GB boundary crossing is not allowed. */
	if ((ALC_ADDR_HI(rx_ring_end) !=
	    ALC_ADDR_HI(sc->alc_rdata.alc_rx_ring_paddr)) ||
	    (ALC_ADDR_HI(rr_ring_end) !=
	    ALC_ADDR_HI(sc->alc_rdata.alc_rr_ring_paddr)) ||
	    (ALC_ADDR_HI(cmb_end) !=
	    ALC_ADDR_HI(sc->alc_rdata.alc_cmb_paddr)) ||
	    (ALC_ADDR_HI(tx_ring_end) !=
	    ALC_ADDR_HI(sc->alc_rdata.alc_tx_ring_paddr)))
		return (EFBIG);
	/*
	 * Make sure Rx return descriptor/Rx descriptor/CMB use
	 * the same high address.
	 */
	if ((ALC_ADDR_HI(rx_ring_end) != ALC_ADDR_HI(rr_ring_end)) ||
	    (ALC_ADDR_HI(rx_ring_end) != ALC_ADDR_HI(cmb_end)))
		return (EFBIG);

	return (0);
}

static int
alc_dma_alloc(struct alc_softc *sc)
{
	struct alc_txdesc *txd;
	struct alc_rxdesc *rxd;
	bus_addr_t lowaddr;
	struct alc_dmamap_arg ctx;
	int error, i;

	lowaddr = BUS_SPACE_MAXADDR;
again:
	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->alc_dev), /* parent */
	    1, 0,			/* alignment, boundary */
	    lowaddr,			/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_parent_tag);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not create parent DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for Tx descriptor ring. */
	error = bus_dma_tag_create(
	    sc->alc_cdata.alc_parent_tag, /* parent */
	    ALC_TX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ALC_TX_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    ALC_TX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_tx_ring_tag);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not create Tx ring DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for Rx free descriptor ring. */
	error = bus_dma_tag_create(
	    sc->alc_cdata.alc_parent_tag, /* parent */
	    ALC_RX_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ALC_RX_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    ALC_RX_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_rx_ring_tag);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not create Rx ring DMA tag.\n");
		goto fail;
	}
	/* Create DMA tag for Rx return descriptor ring. */
	error = bus_dma_tag_create(
	    sc->alc_cdata.alc_parent_tag, /* parent */
	    ALC_RR_RING_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ALC_RR_RING_SZ,		/* maxsize */
	    1,				/* nsegments */
	    ALC_RR_RING_SZ,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_rr_ring_tag);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not create Rx return ring DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for coalescing message block. */
	error = bus_dma_tag_create(
	    sc->alc_cdata.alc_parent_tag, /* parent */
	    ALC_CMB_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ALC_CMB_SZ,			/* maxsize */
	    1,				/* nsegments */
	    ALC_CMB_SZ,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_cmb_tag);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not create CMB DMA tag.\n");
		goto fail;
	}
	/* Create DMA tag for status message block. */
	error = bus_dma_tag_create(
	    sc->alc_cdata.alc_parent_tag, /* parent */
	    ALC_SMB_ALIGN, 0,		/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ALC_SMB_SZ,			/* maxsize */
	    1,				/* nsegments */
	    ALC_SMB_SZ,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_smb_tag);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not create SMB DMA tag.\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc->alc_cdata.alc_tx_ring_tag,
	    (void **)&sc->alc_rdata.alc_tx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->alc_cdata.alc_tx_ring_map);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not allocate DMA'able memory for Tx ring.\n");
		goto fail;
	}
	ctx.alc_busaddr = 0;
	error = bus_dmamap_load(sc->alc_cdata.alc_tx_ring_tag,
	    sc->alc_cdata.alc_tx_ring_map, sc->alc_rdata.alc_tx_ring,
	    ALC_TX_RING_SZ, alc_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.alc_busaddr == 0) {
		device_printf(sc->alc_dev,
		    "could not load DMA'able memory for Tx ring.\n");
		goto fail;
	}
	sc->alc_rdata.alc_tx_ring_paddr = ctx.alc_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc->alc_cdata.alc_rx_ring_tag,
	    (void **)&sc->alc_rdata.alc_rx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->alc_cdata.alc_rx_ring_map);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not allocate DMA'able memory for Rx ring.\n");
		goto fail;
	}
	ctx.alc_busaddr = 0;
	error = bus_dmamap_load(sc->alc_cdata.alc_rx_ring_tag,
	    sc->alc_cdata.alc_rx_ring_map, sc->alc_rdata.alc_rx_ring,
	    ALC_RX_RING_SZ, alc_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.alc_busaddr == 0) {
		device_printf(sc->alc_dev,
		    "could not load DMA'able memory for Rx ring.\n");
		goto fail;
	}
	sc->alc_rdata.alc_rx_ring_paddr = ctx.alc_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx return ring. */
	error = bus_dmamem_alloc(sc->alc_cdata.alc_rr_ring_tag,
	    (void **)&sc->alc_rdata.alc_rr_ring,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->alc_cdata.alc_rr_ring_map);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not allocate DMA'able memory for Rx return ring.\n");
		goto fail;
	}
	ctx.alc_busaddr = 0;
	error = bus_dmamap_load(sc->alc_cdata.alc_rr_ring_tag,
	    sc->alc_cdata.alc_rr_ring_map, sc->alc_rdata.alc_rr_ring,
	    ALC_RR_RING_SZ, alc_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.alc_busaddr == 0) {
		device_printf(sc->alc_dev,
		    "could not load DMA'able memory for Tx ring.\n");
		goto fail;
	}
	sc->alc_rdata.alc_rr_ring_paddr = ctx.alc_busaddr;

	/* Allocate DMA'able memory and load the DMA map for CMB. */
	error = bus_dmamem_alloc(sc->alc_cdata.alc_cmb_tag,
	    (void **)&sc->alc_rdata.alc_cmb,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->alc_cdata.alc_cmb_map);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not allocate DMA'able memory for CMB.\n");
		goto fail;
	}
	ctx.alc_busaddr = 0;
	error = bus_dmamap_load(sc->alc_cdata.alc_cmb_tag,
	    sc->alc_cdata.alc_cmb_map, sc->alc_rdata.alc_cmb,
	    ALC_CMB_SZ, alc_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.alc_busaddr == 0) {
		device_printf(sc->alc_dev,
		    "could not load DMA'able memory for CMB.\n");
		goto fail;
	}
	sc->alc_rdata.alc_cmb_paddr = ctx.alc_busaddr;

	/* Allocate DMA'able memory and load the DMA map for SMB. */
	error = bus_dmamem_alloc(sc->alc_cdata.alc_smb_tag,
	    (void **)&sc->alc_rdata.alc_smb,
	    BUS_DMA_WAITOK | BUS_DMA_ZERO | BUS_DMA_COHERENT,
	    &sc->alc_cdata.alc_smb_map);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not allocate DMA'able memory for SMB.\n");
		goto fail;
	}
	ctx.alc_busaddr = 0;
	error = bus_dmamap_load(sc->alc_cdata.alc_smb_tag,
	    sc->alc_cdata.alc_smb_map, sc->alc_rdata.alc_smb,
	    ALC_SMB_SZ, alc_dmamap_cb, &ctx, 0);
	if (error != 0 || ctx.alc_busaddr == 0) {
		device_printf(sc->alc_dev,
		    "could not load DMA'able memory for CMB.\n");
		goto fail;
	}
	sc->alc_rdata.alc_smb_paddr = ctx.alc_busaddr;

	/* Make sure we've not crossed 4GB boundary. */
	if (lowaddr != BUS_SPACE_MAXADDR_32BIT &&
	    (error = alc_check_boundary(sc)) != 0) {
		device_printf(sc->alc_dev, "4GB boundary crossed, "
		    "switching to 32bit DMA addressing mode.\n");
		alc_dma_free(sc);
		/*
		 * Limit max allowable DMA address space to 32bit
		 * and try again.
		 */
		lowaddr = BUS_SPACE_MAXADDR_32BIT;
		goto again;
	}

	/*
	 * Create Tx buffer parent tag.
	 * AR81[3567]x allows 64bit DMA addressing of Tx/Rx buffers
	 * so it needs separate parent DMA tag as parent DMA address
	 * space could be restricted to be within 32bit address space
	 * by 4GB boundary crossing.
	 */
	error = bus_dma_tag_create(
	    bus_get_dma_tag(sc->alc_dev), /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
	    0,				/* nsegments */
	    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_buffer_tag);
	if (error != 0) {
		device_printf(sc->alc_dev,
		    "could not create parent buffer DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for Tx buffers. */
	error = bus_dma_tag_create(
	    sc->alc_cdata.alc_buffer_tag, /* parent */
	    1, 0,			/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    ALC_TSO_MAXSIZE,		/* maxsize */
	    ALC_MAXTXSEGS,		/* nsegments */
	    ALC_TSO_MAXSEGSIZE,		/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_tx_tag);
	if (error != 0) {
		device_printf(sc->alc_dev, "could not create Tx DMA tag.\n");
		goto fail;
	}

	/* Create DMA tag for Rx buffers. */
	error = bus_dma_tag_create(
	    sc->alc_cdata.alc_buffer_tag, /* parent */
	    ALC_RX_BUF_ALIGN, 0,	/* alignment, boundary */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    MCLBYTES,			/* maxsize */
	    1,				/* nsegments */
	    MCLBYTES,			/* maxsegsize */
	    0,				/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->alc_cdata.alc_rx_tag);
	if (error != 0) {
		device_printf(sc->alc_dev, "could not create Rx DMA tag.\n");
		goto fail;
	}
	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < ALC_TX_RING_CNT; i++) {
		txd = &sc->alc_cdata.alc_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc->alc_cdata.alc_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc->alc_dev,
			    "could not create Tx dmamap.\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc->alc_cdata.alc_rx_tag, 0,
	    &sc->alc_cdata.alc_rx_sparemap)) != 0) {
		device_printf(sc->alc_dev,
		    "could not create spare Rx dmamap.\n");
		goto fail;
	}
	for (i = 0; i < ALC_RX_RING_CNT; i++) {
		rxd = &sc->alc_cdata.alc_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc->alc_cdata.alc_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc->alc_dev,
			    "could not create Rx dmamap.\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static void
alc_dma_free(struct alc_softc *sc)
{
	struct alc_txdesc *txd;
	struct alc_rxdesc *rxd;
	int i;

	/* Tx buffers. */
	if (sc->alc_cdata.alc_tx_tag != NULL) {
		for (i = 0; i < ALC_TX_RING_CNT; i++) {
			txd = &sc->alc_cdata.alc_txdesc[i];
			if (txd->tx_dmamap != NULL) {
				bus_dmamap_destroy(sc->alc_cdata.alc_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc->alc_cdata.alc_tx_tag);
		sc->alc_cdata.alc_tx_tag = NULL;
	}
	/* Rx buffers */
	if (sc->alc_cdata.alc_rx_tag != NULL) {
		for (i = 0; i < ALC_RX_RING_CNT; i++) {
			rxd = &sc->alc_cdata.alc_rxdesc[i];
			if (rxd->rx_dmamap != NULL) {
				bus_dmamap_destroy(sc->alc_cdata.alc_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc->alc_cdata.alc_rx_sparemap != NULL) {
			bus_dmamap_destroy(sc->alc_cdata.alc_rx_tag,
			    sc->alc_cdata.alc_rx_sparemap);
			sc->alc_cdata.alc_rx_sparemap = NULL;
		}
		bus_dma_tag_destroy(sc->alc_cdata.alc_rx_tag);
		sc->alc_cdata.alc_rx_tag = NULL;
	}
	/* Tx descriptor ring. */
	if (sc->alc_cdata.alc_tx_ring_tag != NULL) {
		if (sc->alc_rdata.alc_tx_ring_paddr != 0)
			bus_dmamap_unload(sc->alc_cdata.alc_tx_ring_tag,
			    sc->alc_cdata.alc_tx_ring_map);
		if (sc->alc_rdata.alc_tx_ring != NULL)
			bus_dmamem_free(sc->alc_cdata.alc_tx_ring_tag,
			    sc->alc_rdata.alc_tx_ring,
			    sc->alc_cdata.alc_tx_ring_map);
		sc->alc_rdata.alc_tx_ring_paddr = 0;
		sc->alc_rdata.alc_tx_ring = NULL;
		bus_dma_tag_destroy(sc->alc_cdata.alc_tx_ring_tag);
		sc->alc_cdata.alc_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc->alc_cdata.alc_rx_ring_tag != NULL) {
		if (sc->alc_rdata.alc_rx_ring_paddr != 0)
			bus_dmamap_unload(sc->alc_cdata.alc_rx_ring_tag,
			    sc->alc_cdata.alc_rx_ring_map);
		if (sc->alc_rdata.alc_rx_ring != NULL)
			bus_dmamem_free(sc->alc_cdata.alc_rx_ring_tag,
			    sc->alc_rdata.alc_rx_ring,
			    sc->alc_cdata.alc_rx_ring_map);
		sc->alc_rdata.alc_rx_ring_paddr = 0;
		sc->alc_rdata.alc_rx_ring = NULL;
		bus_dma_tag_destroy(sc->alc_cdata.alc_rx_ring_tag);
		sc->alc_cdata.alc_rx_ring_tag = NULL;
	}
	/* Rx return ring. */
	if (sc->alc_cdata.alc_rr_ring_tag != NULL) {
		if (sc->alc_rdata.alc_rr_ring_paddr != 0)
			bus_dmamap_unload(sc->alc_cdata.alc_rr_ring_tag,
			    sc->alc_cdata.alc_rr_ring_map);
		if (sc->alc_rdata.alc_rr_ring != NULL)
			bus_dmamem_free(sc->alc_cdata.alc_rr_ring_tag,
			    sc->alc_rdata.alc_rr_ring,
			    sc->alc_cdata.alc_rr_ring_map);
		sc->alc_rdata.alc_rr_ring_paddr = 0;
		sc->alc_rdata.alc_rr_ring = NULL;
		bus_dma_tag_destroy(sc->alc_cdata.alc_rr_ring_tag);
		sc->alc_cdata.alc_rr_ring_tag = NULL;
	}
	/* CMB block */
	if (sc->alc_cdata.alc_cmb_tag != NULL) {
		if (sc->alc_rdata.alc_cmb_paddr != 0)
			bus_dmamap_unload(sc->alc_cdata.alc_cmb_tag,
			    sc->alc_cdata.alc_cmb_map);
		if (sc->alc_rdata.alc_cmb != NULL)
			bus_dmamem_free(sc->alc_cdata.alc_cmb_tag,
			    sc->alc_rdata.alc_cmb,
			    sc->alc_cdata.alc_cmb_map);		
		sc->alc_rdata.alc_cmb_paddr = 0;
		sc->alc_rdata.alc_cmb = NULL;
		bus_dma_tag_destroy(sc->alc_cdata.alc_cmb_tag);
		sc->alc_cdata.alc_cmb_tag = NULL;
	}
	/* SMB block */
	if (sc->alc_cdata.alc_smb_tag != NULL) {
		if (sc->alc_rdata.alc_smb_paddr != 0)
			bus_dmamap_unload(sc->alc_cdata.alc_smb_tag,
			    sc->alc_cdata.alc_smb_map);
		if (sc->alc_rdata.alc_smb != NULL)
			bus_dmamem_free(sc->alc_cdata.alc_smb_tag,
			    sc->alc_rdata.alc_smb,
			    sc->alc_cdata.alc_smb_map);
		sc->alc_rdata.alc_smb_paddr = 0;
		sc->alc_rdata.alc_smb = NULL;
		bus_dma_tag_destroy(sc->alc_cdata.alc_smb_tag);
		sc->alc_cdata.alc_smb_tag = NULL;
	}
	if (sc->alc_cdata.alc_buffer_tag != NULL) {
		bus_dma_tag_destroy(sc->alc_cdata.alc_buffer_tag);
		sc->alc_cdata.alc_buffer_tag = NULL;
	}
	if (sc->alc_cdata.alc_parent_tag != NULL) {
		bus_dma_tag_destroy(sc->alc_cdata.alc_parent_tag);
		sc->alc_cdata.alc_parent_tag = NULL;
	}
}

static int
alc_shutdown(device_t dev)
{

	return (alc_suspend(dev));
}

/*
 * Note, this driver resets the link speed to 10/100Mbps by
 * restarting auto-negotiation in suspend/shutdown phase but we
 * don't know whether that auto-negotiation would succeed or not
 * as driver has no control after powering off/suspend operation.
 * If the renegotiation fail WOL may not work. Running at 1Gbps
 * will draw more power than 375mA at 3.3V which is specified in
 * PCI specification and that would result in complete
 * shutdowning power to ethernet controller.
 *
 * TODO
 * Save current negotiated media speed/duplex/flow-control to
 * softc and restore the same link again after resuming. PHY
 * handling such as power down/resetting to 100Mbps may be better
 * handled in suspend method in phy driver.
 */
static void
alc_setlinkspeed(struct alc_softc *sc)
{
	struct mii_data *mii;
	int aneg, i;

	mii = device_get_softc(sc->alc_miibus);
	mii_pollstat(mii);
	aneg = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch IFM_SUBTYPE(mii->mii_media_active) {
		case IFM_10_T:
		case IFM_100_TX:
			return;
		case IFM_1000_T:
			aneg++;
			break;
		default:
			break;
		}
	}
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr, MII_100T2CR, 0);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    MII_ANAR, ANAR_TX_FD | ANAR_TX | ANAR_10_FD | ANAR_10 | ANAR_CSMA);
	alc_miibus_writereg(sc->alc_dev, sc->alc_phyaddr,
	    MII_BMCR, BMCR_RESET | BMCR_AUTOEN | BMCR_STARTNEG);
	DELAY(1000);
	if (aneg != 0) {
		/*
		 * Poll link state until alc(4) get a 10/100Mbps link.
		 */
		for (i = 0; i < MII_ANEGTICKS_GIGE; i++) {
			mii_pollstat(mii);
			if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID))
			    == (IFM_ACTIVE | IFM_AVALID)) {
				switch (IFM_SUBTYPE(
				    mii->mii_media_active)) {
				case IFM_10_T:
				case IFM_100_TX:
					alc_mac_config(sc);
					return;
				default:
					break;
				}
			}
			ALC_UNLOCK(sc);
			pause("alclnk", hz);
			ALC_LOCK(sc);
		}
		if (i == MII_ANEGTICKS_GIGE)
			device_printf(sc->alc_dev,
			    "establishing a link failed, WOL may not work!");
	}
	/*
	 * No link, force MAC to have 100Mbps, full-duplex link.
	 * This is the last resort and may/may not work.
	 */
	mii->mii_media_status = IFM_AVALID | IFM_ACTIVE;
	mii->mii_media_active = IFM_ETHER | IFM_100_TX | IFM_FDX;
	alc_mac_config(sc);
}

static void
alc_setwol(struct alc_softc *sc)
{

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		alc_setwol_816x(sc);
	else
		alc_setwol_813x(sc);
}

static void
alc_setwol_813x(struct alc_softc *sc)
{
	struct ifnet *ifp;
	uint32_t reg, pmcs;
	uint16_t pmstat;

	ALC_LOCK_ASSERT(sc);

	alc_disable_l0s_l1(sc);
	ifp = sc->alc_ifp;
	if ((sc->alc_flags & ALC_FLAG_PM) == 0) {
		/* Disable WOL. */
		CSR_WRITE_4(sc, ALC_WOL_CFG, 0);
		reg = CSR_READ_4(sc, ALC_PCIE_PHYMISC);
		reg |= PCIE_PHYMISC_FORCE_RCV_DET;
		CSR_WRITE_4(sc, ALC_PCIE_PHYMISC, reg);
		/* Force PHY power down. */
		alc_phy_down(sc);
		CSR_WRITE_4(sc, ALC_MASTER_CFG,
		    CSR_READ_4(sc, ALC_MASTER_CFG) | MASTER_CLK_SEL_DIS);
		return;
	}

	if ((ifp->if_capenable & IFCAP_WOL) != 0) {
		if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0)
			alc_setlinkspeed(sc);
		CSR_WRITE_4(sc, ALC_MASTER_CFG,
		    CSR_READ_4(sc, ALC_MASTER_CFG) & ~MASTER_CLK_SEL_DIS);
	}

	pmcs = 0;
	if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
		pmcs |= WOL_CFG_MAGIC | WOL_CFG_MAGIC_ENB;
	CSR_WRITE_4(sc, ALC_WOL_CFG, pmcs);
	reg = CSR_READ_4(sc, ALC_MAC_CFG);
	reg &= ~(MAC_CFG_DBG | MAC_CFG_PROMISC | MAC_CFG_ALLMULTI |
	    MAC_CFG_BCAST);
	if ((ifp->if_capenable & IFCAP_WOL_MCAST) != 0)
		reg |= MAC_CFG_ALLMULTI | MAC_CFG_BCAST;
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		reg |= MAC_CFG_RX_ENB;
	CSR_WRITE_4(sc, ALC_MAC_CFG, reg);

	reg = CSR_READ_4(sc, ALC_PCIE_PHYMISC);
	reg |= PCIE_PHYMISC_FORCE_RCV_DET;
	CSR_WRITE_4(sc, ALC_PCIE_PHYMISC, reg);
	if ((ifp->if_capenable & IFCAP_WOL) == 0) {
		/* WOL disabled, PHY power down. */
		alc_phy_down(sc);
		CSR_WRITE_4(sc, ALC_MASTER_CFG,
		    CSR_READ_4(sc, ALC_MASTER_CFG) | MASTER_CLK_SEL_DIS);
	}
	/* Request PME. */
	pmstat = pci_read_config(sc->alc_dev,
	    sc->alc_pmcap + PCIR_POWER_STATUS, 2);
	pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
	if ((ifp->if_capenable & IFCAP_WOL) != 0)
		pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
	pci_write_config(sc->alc_dev,
	    sc->alc_pmcap + PCIR_POWER_STATUS, pmstat, 2);
}

static void
alc_setwol_816x(struct alc_softc *sc)
{
	struct ifnet *ifp;
	uint32_t gphy, mac, master, pmcs, reg;
	uint16_t pmstat;

	ALC_LOCK_ASSERT(sc);

	ifp = sc->alc_ifp;
	master = CSR_READ_4(sc, ALC_MASTER_CFG);
	master &= ~MASTER_CLK_SEL_DIS;
	gphy = CSR_READ_4(sc, ALC_GPHY_CFG);
	gphy &= ~(GPHY_CFG_EXT_RESET | GPHY_CFG_LED_MODE | GPHY_CFG_100AB_ENB |
	    GPHY_CFG_PHY_PLL_ON);
	gphy |= GPHY_CFG_HIB_EN | GPHY_CFG_HIB_PULSE | GPHY_CFG_SEL_ANA_RESET;
	if ((sc->alc_flags & ALC_FLAG_PM) == 0) {
		CSR_WRITE_4(sc, ALC_WOL_CFG, 0);
		gphy |= GPHY_CFG_PHY_IDDQ | GPHY_CFG_PWDOWN_HW;
		mac = CSR_READ_4(sc, ALC_MAC_CFG);
	} else {
		if ((ifp->if_capenable & IFCAP_WOL) != 0) {
			gphy |= GPHY_CFG_EXT_RESET;
			if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0)
				alc_setlinkspeed(sc);
		}
		pmcs = 0;
		if ((ifp->if_capenable & IFCAP_WOL_MAGIC) != 0)
			pmcs |= WOL_CFG_MAGIC | WOL_CFG_MAGIC_ENB;
		CSR_WRITE_4(sc, ALC_WOL_CFG, pmcs);
		mac = CSR_READ_4(sc, ALC_MAC_CFG);
		mac &= ~(MAC_CFG_DBG | MAC_CFG_PROMISC | MAC_CFG_ALLMULTI |
		    MAC_CFG_BCAST);
		if ((ifp->if_capenable & IFCAP_WOL_MCAST) != 0)
			mac |= MAC_CFG_ALLMULTI | MAC_CFG_BCAST;
		if ((ifp->if_capenable & IFCAP_WOL) != 0)
			mac |= MAC_CFG_RX_ENB;
		alc_miiext_writereg(sc, MII_EXT_ANEG, MII_EXT_ANEG_S3DIG10,
		    ANEG_S3DIG10_SL);
	}

	/* Enable OSC. */
	reg = CSR_READ_4(sc, ALC_MISC);
	reg &= ~MISC_INTNLOSC_OPEN;
	CSR_WRITE_4(sc, ALC_MISC, reg);
	reg |= MISC_INTNLOSC_OPEN;
	CSR_WRITE_4(sc, ALC_MISC, reg);
	CSR_WRITE_4(sc, ALC_MASTER_CFG, master);
	CSR_WRITE_4(sc, ALC_MAC_CFG, mac);
	CSR_WRITE_4(sc, ALC_GPHY_CFG, gphy);
	reg = CSR_READ_4(sc, ALC_PDLL_TRNS1);
	reg |= PDLL_TRNS1_D3PLLOFF_ENB;
	CSR_WRITE_4(sc, ALC_PDLL_TRNS1, reg);

	if ((sc->alc_flags & ALC_FLAG_PM) != 0) {
		/* Request PME. */
		pmstat = pci_read_config(sc->alc_dev,
		    sc->alc_pmcap + PCIR_POWER_STATUS, 2);
		pmstat &= ~(PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE);
		if ((ifp->if_capenable & IFCAP_WOL) != 0)
			pmstat |= PCIM_PSTAT_PME | PCIM_PSTAT_PMEENABLE;
		pci_write_config(sc->alc_dev,
		    sc->alc_pmcap + PCIR_POWER_STATUS, pmstat, 2);
	}
}

static int
alc_suspend(device_t dev)
{
	struct alc_softc *sc;

	sc = device_get_softc(dev);

	ALC_LOCK(sc);
	alc_stop(sc);
	alc_setwol(sc);
	ALC_UNLOCK(sc);

	return (0);
}

static int
alc_resume(device_t dev)
{
	struct alc_softc *sc;
	struct ifnet *ifp;
	uint16_t pmstat;

	sc = device_get_softc(dev);

	ALC_LOCK(sc);
	if ((sc->alc_flags & ALC_FLAG_PM) != 0) {
		/* Disable PME and clear PME status. */
		pmstat = pci_read_config(sc->alc_dev,
		    sc->alc_pmcap + PCIR_POWER_STATUS, 2);
		if ((pmstat & PCIM_PSTAT_PMEENABLE) != 0) {
			pmstat &= ~PCIM_PSTAT_PMEENABLE;
			pci_write_config(sc->alc_dev,
			    sc->alc_pmcap + PCIR_POWER_STATUS, pmstat, 2);
		}
	}
	/* Reset PHY. */
	alc_phy_reset(sc);
	ifp = sc->alc_ifp;
	if ((ifp->if_flags & IFF_UP) != 0) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		alc_init_locked(sc);
	}
	ALC_UNLOCK(sc);

	return (0);
}

static int
alc_encap(struct alc_softc *sc, struct mbuf **m_head)
{
	struct alc_txdesc *txd, *txd_last;
	struct tx_desc *desc;
	struct mbuf *m;
	struct ip *ip;
	struct tcphdr *tcp;
	bus_dma_segment_t txsegs[ALC_MAXTXSEGS];
	bus_dmamap_t map;
	uint32_t cflags, hdrlen, ip_off, poff, vtag;
	int error, idx, nsegs, prod;

	ALC_LOCK_ASSERT(sc);

	M_ASSERTPKTHDR((*m_head));

	m = *m_head;
	ip = NULL;
	tcp = NULL;
	ip_off = poff = 0;
	if ((m->m_pkthdr.csum_flags & (ALC_CSUM_FEATURES | CSUM_TSO)) != 0) {
		/*
		 * AR81[3567]x requires offset of TCP/UDP header in its
		 * Tx descriptor to perform Tx checksum offloading. TSO
		 * also requires TCP header offset and modification of
		 * IP/TCP header. This kind of operation takes many CPU
		 * cycles on FreeBSD so fast host CPU is required to get
		 * smooth TSO performance.
		 */
		struct ether_header *eh;

		if (M_WRITABLE(m) == 0) {
			/* Get a writable copy. */
			m = m_dup(*m_head, M_NOWAIT);
			/* Release original mbufs. */
			m_freem(*m_head);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*m_head = m;
		}

		ip_off = sizeof(struct ether_header);
		m = m_pullup(m, ip_off);
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		eh = mtod(m, struct ether_header *);
		/*
		 * Check if hardware VLAN insertion is off.
		 * Additional check for LLC/SNAP frame?
		 */
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
		if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
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
			 * Due to strict adherence of Microsoft NDIS
			 * Large Send specification, hardware expects
			 * a pseudo TCP checksum inserted by upper
			 * stack. Unfortunately the pseudo TCP
			 * checksum that NDIS refers to does not include
			 * TCP payload length so driver should recompute
			 * the pseudo checksum here. Hopefully this
			 * wouldn't be much burden on modern CPUs.
			 *
			 * Reset IP checksum and recompute TCP pseudo
			 * checksum as NDIS specification said.
			 */
			ip = (struct ip *)(mtod(m, char *) + ip_off);
			tcp = (struct tcphdr *)(mtod(m, char *) + poff);
			ip->ip_sum = 0;
			tcp->th_sum = in_pseudo(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, htons(IPPROTO_TCP));
		}
		*m_head = m;
	}

	prod = sc->alc_cdata.alc_tx_prod;
	txd = &sc->alc_cdata.alc_txdesc[prod];
	txd_last = txd;
	map = txd->tx_dmamap;

	error = bus_dmamap_load_mbuf_sg(sc->alc_cdata.alc_tx_tag, map,
	    *m_head, txsegs, &nsegs, 0);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, ALC_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOMEM);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc->alc_cdata.alc_tx_tag, map,
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

	/* Check descriptor overrun. */
	if (sc->alc_cdata.alc_tx_cnt + nsegs >= ALC_TX_RING_CNT - 3) {
		bus_dmamap_unload(sc->alc_cdata.alc_tx_tag, map);
		return (ENOBUFS);
	}
	bus_dmamap_sync(sc->alc_cdata.alc_tx_tag, map, BUS_DMASYNC_PREWRITE);

	m = *m_head;
	cflags = TD_ETHERNET;
	vtag = 0;
	desc = NULL;
	idx = 0;
	/* Configure VLAN hardware tag insertion. */
	if ((m->m_flags & M_VLANTAG) != 0) {
		vtag = htons(m->m_pkthdr.ether_vtag);
		vtag = (vtag << TD_VLAN_SHIFT) & TD_VLAN_MASK;
		cflags |= TD_INS_VLAN_TAG;
	}
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		/* Request TSO and set MSS. */
		cflags |= TD_TSO | TD_TSO_DESCV1;
		cflags |= ((uint32_t)m->m_pkthdr.tso_segsz << TD_MSS_SHIFT) &
		    TD_MSS_MASK;
		/* Set TCP header offset. */
		cflags |= (poff << TD_TCPHDR_OFFSET_SHIFT) &
		    TD_TCPHDR_OFFSET_MASK;
		/*
		 * AR81[3567]x requires the first buffer should
		 * only hold IP/TCP header data. Payload should
		 * be handled in other descriptors.
		 */
		hdrlen = poff + (tcp->th_off << 2);
		desc = &sc->alc_rdata.alc_tx_ring[prod];
		desc->len = htole32(TX_BYTES(hdrlen | vtag));
		desc->flags = htole32(cflags);
		desc->addr = htole64(txsegs[0].ds_addr);
		sc->alc_cdata.alc_tx_cnt++;
		ALC_DESC_INC(prod, ALC_TX_RING_CNT);
		if (m->m_len - hdrlen > 0) {
			/* Handle remaining payload of the first fragment. */
			desc = &sc->alc_rdata.alc_tx_ring[prod];
			desc->len = htole32(TX_BYTES((m->m_len - hdrlen) |
			    vtag));
			desc->flags = htole32(cflags);
			desc->addr = htole64(txsegs[0].ds_addr + hdrlen);
			sc->alc_cdata.alc_tx_cnt++;
			ALC_DESC_INC(prod, ALC_TX_RING_CNT);
		}
		/* Handle remaining fragments. */
		idx = 1;
	} else if ((m->m_pkthdr.csum_flags & ALC_CSUM_FEATURES) != 0) {
		/* Configure Tx checksum offload. */
#ifdef ALC_USE_CUSTOM_CSUM
		cflags |= TD_CUSTOM_CSUM;
		/* Set checksum start offset. */
		cflags |= ((poff >> 1) << TD_PLOAD_OFFSET_SHIFT) &
		    TD_PLOAD_OFFSET_MASK;
		/* Set checksum insertion position of TCP/UDP. */
		cflags |= (((poff + m->m_pkthdr.csum_data) >> 1) <<
		    TD_CUSTOM_CSUM_OFFSET_SHIFT) & TD_CUSTOM_CSUM_OFFSET_MASK;
#else
		if ((m->m_pkthdr.csum_flags & CSUM_IP) != 0)
			cflags |= TD_IPCSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_TCP) != 0)
			cflags |= TD_TCPCSUM;
		if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
			cflags |= TD_UDPCSUM;
		/* Set TCP/UDP header offset. */
		cflags |= (poff << TD_L4HDR_OFFSET_SHIFT) &
		    TD_L4HDR_OFFSET_MASK;
#endif
	}
	for (; idx < nsegs; idx++) {
		desc = &sc->alc_rdata.alc_tx_ring[prod];
		desc->len = htole32(TX_BYTES(txsegs[idx].ds_len) | vtag);
		desc->flags = htole32(cflags);
		desc->addr = htole64(txsegs[idx].ds_addr);
		sc->alc_cdata.alc_tx_cnt++;
		ALC_DESC_INC(prod, ALC_TX_RING_CNT);
	}
	/* Update producer index. */
	sc->alc_cdata.alc_tx_prod = prod;

	/* Finally set EOP on the last descriptor. */
	prod = (prod + ALC_TX_RING_CNT - 1) % ALC_TX_RING_CNT;
	desc = &sc->alc_rdata.alc_tx_ring[prod];
	desc->flags |= htole32(TD_EOP);

	/* Swap dmamap of the first and the last. */
	txd = &sc->alc_cdata.alc_txdesc[prod];
	map = txd_last->tx_dmamap;
	txd_last->tx_dmamap = txd->tx_dmamap;
	txd->tx_dmamap = map;
	txd->tx_m = m;

	return (0);
}

static void
alc_start(struct ifnet *ifp)
{
	struct alc_softc *sc;

	sc = ifp->if_softc;
	ALC_LOCK(sc);
	alc_start_locked(ifp);
	ALC_UNLOCK(sc);
}

static void
alc_start_locked(struct ifnet *ifp)
{
	struct alc_softc *sc;
	struct mbuf *m_head;
	int enq;

	sc = ifp->if_softc;

	ALC_LOCK_ASSERT(sc);

	/* Reclaim transmitted frames. */
	if (sc->alc_cdata.alc_tx_cnt >= ALC_TX_DESC_HIWAT)
		alc_txeof(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->alc_flags & ALC_FLAG_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd); ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (alc_encap(sc, &m_head)) {
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

	if (enq > 0)
		alc_start_tx(sc);
}

static void
alc_start_tx(struct alc_softc *sc)
{

	/* Sync descriptors. */
	bus_dmamap_sync(sc->alc_cdata.alc_tx_ring_tag,
	    sc->alc_cdata.alc_tx_ring_map, BUS_DMASYNC_PREWRITE);
	/* Kick. Assume we're using normal Tx priority queue. */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		CSR_WRITE_2(sc, ALC_MBOX_TD_PRI0_PROD_IDX,
		    (uint16_t)sc->alc_cdata.alc_tx_prod);
	else
		CSR_WRITE_4(sc, ALC_MBOX_TD_PROD_IDX,
		    (sc->alc_cdata.alc_tx_prod <<
		    MBOX_TD_PROD_LO_IDX_SHIFT) &
		    MBOX_TD_PROD_LO_IDX_MASK);
	/* Set a timeout in case the chip goes out to lunch. */
	sc->alc_watchdog_timer = ALC_TX_TIMEOUT;
}

static void
alc_watchdog(struct alc_softc *sc)
{
	struct ifnet *ifp;

	ALC_LOCK_ASSERT(sc);

	if (sc->alc_watchdog_timer == 0 || --sc->alc_watchdog_timer)
		return;

	ifp = sc->alc_ifp;
	if ((sc->alc_flags & ALC_FLAG_LINK) == 0) {
		if_printf(sc->alc_ifp, "watchdog timeout (lost link)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		alc_init_locked(sc);
		return;
	}
	if_printf(sc->alc_ifp, "watchdog timeout -- resetting\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	alc_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		alc_start_locked(ifp);
}

static int
alc_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct alc_softc *sc;
	struct ifreq *ifr;
	struct mii_data *mii;
	int error, mask;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;
	switch (cmd) {
	case SIOCSIFMTU:
		if (ifr->ifr_mtu < ETHERMIN ||
		    ifr->ifr_mtu > (sc->alc_ident->max_framelen -
		    sizeof(struct ether_vlan_header) - ETHER_CRC_LEN) ||
		    ((sc->alc_flags & ALC_FLAG_JUMBO) == 0 &&
		    ifr->ifr_mtu > ETHERMTU))
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			ALC_LOCK(sc);
			ifp->if_mtu = ifr->ifr_mtu;
			/* AR81[3567]x has 13 bits MSS field. */
			if (ifp->if_mtu > ALC_TSO_MTU &&
			    (ifp->if_capenable & IFCAP_TSO4) != 0) {
				ifp->if_capenable &= ~IFCAP_TSO4;
				ifp->if_hwassist &= ~CSUM_TSO;
				VLAN_CAPABILITIES(ifp);
			}
			ALC_UNLOCK(sc);
		}
		break;
	case SIOCSIFFLAGS:
		ALC_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc->alc_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) != 0)
				alc_rxfilter(sc);
			else
				alc_init_locked(sc);
		} else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			alc_stop(sc);
		sc->alc_if_flags = ifp->if_flags;
		ALC_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		ALC_LOCK(sc);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			alc_rxfilter(sc);
		ALC_UNLOCK(sc);
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->alc_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;
	case SIOCSIFCAP:
		ALC_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_TXCSUM) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((ifp->if_capenable & IFCAP_TXCSUM) != 0)
				ifp->if_hwassist |= ALC_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~ALC_CSUM_FEATURES;
		}
		if ((mask & IFCAP_TSO4) != 0 &&
		    (ifp->if_capabilities & IFCAP_TSO4) != 0) {
			ifp->if_capenable ^= IFCAP_TSO4;
			if ((ifp->if_capenable & IFCAP_TSO4) != 0) {
				/* AR81[3567]x has 13 bits MSS field. */
				if (ifp->if_mtu > ALC_TSO_MTU) {
					ifp->if_capenable &= ~IFCAP_TSO4;
					ifp->if_hwassist &= ~CSUM_TSO;
				} else
					ifp->if_hwassist |= CSUM_TSO;
			} else
				ifp->if_hwassist &= ~CSUM_TSO;
		}
		if ((mask & IFCAP_WOL_MCAST) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_MCAST) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MCAST;
		if ((mask & IFCAP_WOL_MAGIC) != 0 &&
		    (ifp->if_capabilities & IFCAP_WOL_MAGIC) != 0)
			ifp->if_capenable ^= IFCAP_WOL_MAGIC;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			alc_rxvlan(sc);
		}
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWCSUM) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (ifp->if_capabilities & IFCAP_VLAN_HWTSO) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) == 0)
			ifp->if_capenable &=
			    ~(IFCAP_VLAN_HWTSO | IFCAP_VLAN_HWCSUM);
		ALC_UNLOCK(sc);
		VLAN_CAPABILITIES(ifp);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
alc_mac_config(struct alc_softc *sc)
{
	struct mii_data *mii;
	uint32_t reg;

	ALC_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->alc_miibus);
	reg = CSR_READ_4(sc, ALC_MAC_CFG);
	reg &= ~(MAC_CFG_FULL_DUPLEX | MAC_CFG_TX_FC | MAC_CFG_RX_FC |
	    MAC_CFG_SPEED_MASK);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151_V2 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B2)
		reg |= MAC_CFG_HASH_ALG_CRC32 | MAC_CFG_SPEED_MODE_SW;
	/* Reprogram MAC with resolved speed/duplex. */
	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_10_T:
	case IFM_100_TX:
		reg |= MAC_CFG_SPEED_10_100;
		break;
	case IFM_1000_T:
		reg |= MAC_CFG_SPEED_1000;
		break;
	}
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		reg |= MAC_CFG_FULL_DUPLEX;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_TXPAUSE) != 0)
			reg |= MAC_CFG_TX_FC;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_ETH_RXPAUSE) != 0)
			reg |= MAC_CFG_RX_FC;
	}
	CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
}

static void
alc_stats_clear(struct alc_softc *sc)
{
	struct smb sb, *smb;
	uint32_t *reg;
	int i;

	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0) {
		bus_dmamap_sync(sc->alc_cdata.alc_smb_tag,
		    sc->alc_cdata.alc_smb_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		smb = sc->alc_rdata.alc_smb;
		/* Update done, clear. */
		smb->updated = 0;
		bus_dmamap_sync(sc->alc_cdata.alc_smb_tag,
		    sc->alc_cdata.alc_smb_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	} else {
		for (reg = &sb.rx_frames, i = 0; reg <= &sb.rx_pkts_filtered;
		    reg++) {
			CSR_READ_4(sc, ALC_RX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
		/* Read Tx statistics. */
		for (reg = &sb.tx_frames, i = 0; reg <= &sb.tx_mcast_bytes;
		    reg++) {
			CSR_READ_4(sc, ALC_TX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
	}
}

static void
alc_stats_update(struct alc_softc *sc)
{
	struct alc_hw_stats *stat;
	struct smb sb, *smb;
	struct ifnet *ifp;
	uint32_t *reg;
	int i;

	ALC_LOCK_ASSERT(sc);

	ifp = sc->alc_ifp;
	stat = &sc->alc_stats;
	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0) {
		bus_dmamap_sync(sc->alc_cdata.alc_smb_tag,
		    sc->alc_cdata.alc_smb_map,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		smb = sc->alc_rdata.alc_smb;
		if (smb->updated == 0)
			return;
	} else {
		smb = &sb;
		/* Read Rx statistics. */
		for (reg = &sb.rx_frames, i = 0; reg <= &sb.rx_pkts_filtered;
		    reg++) {
			*reg = CSR_READ_4(sc, ALC_RX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
		/* Read Tx statistics. */
		for (reg = &sb.tx_frames, i = 0; reg <= &sb.tx_mcast_bytes;
		    reg++) {
			*reg = CSR_READ_4(sc, ALC_TX_MIB_BASE + i);
			i += sizeof(uint32_t);
		}
	}

	/* Rx stats. */
	stat->rx_frames += smb->rx_frames;
	stat->rx_bcast_frames += smb->rx_bcast_frames;
	stat->rx_mcast_frames += smb->rx_mcast_frames;
	stat->rx_pause_frames += smb->rx_pause_frames;
	stat->rx_control_frames += smb->rx_control_frames;
	stat->rx_crcerrs += smb->rx_crcerrs;
	stat->rx_lenerrs += smb->rx_lenerrs;
	stat->rx_bytes += smb->rx_bytes;
	stat->rx_runts += smb->rx_runts;
	stat->rx_fragments += smb->rx_fragments;
	stat->rx_pkts_64 += smb->rx_pkts_64;
	stat->rx_pkts_65_127 += smb->rx_pkts_65_127;
	stat->rx_pkts_128_255 += smb->rx_pkts_128_255;
	stat->rx_pkts_256_511 += smb->rx_pkts_256_511;
	stat->rx_pkts_512_1023 += smb->rx_pkts_512_1023;
	stat->rx_pkts_1024_1518 += smb->rx_pkts_1024_1518;
	stat->rx_pkts_1519_max += smb->rx_pkts_1519_max;
	stat->rx_pkts_truncated += smb->rx_pkts_truncated;
	stat->rx_fifo_oflows += smb->rx_fifo_oflows;
	stat->rx_rrs_errs += smb->rx_rrs_errs;
	stat->rx_alignerrs += smb->rx_alignerrs;
	stat->rx_bcast_bytes += smb->rx_bcast_bytes;
	stat->rx_mcast_bytes += smb->rx_mcast_bytes;
	stat->rx_pkts_filtered += smb->rx_pkts_filtered;

	/* Tx stats. */
	stat->tx_frames += smb->tx_frames;
	stat->tx_bcast_frames += smb->tx_bcast_frames;
	stat->tx_mcast_frames += smb->tx_mcast_frames;
	stat->tx_pause_frames += smb->tx_pause_frames;
	stat->tx_excess_defer += smb->tx_excess_defer;
	stat->tx_control_frames += smb->tx_control_frames;
	stat->tx_deferred += smb->tx_deferred;
	stat->tx_bytes += smb->tx_bytes;
	stat->tx_pkts_64 += smb->tx_pkts_64;
	stat->tx_pkts_65_127 += smb->tx_pkts_65_127;
	stat->tx_pkts_128_255 += smb->tx_pkts_128_255;
	stat->tx_pkts_256_511 += smb->tx_pkts_256_511;
	stat->tx_pkts_512_1023 += smb->tx_pkts_512_1023;
	stat->tx_pkts_1024_1518 += smb->tx_pkts_1024_1518;
	stat->tx_pkts_1519_max += smb->tx_pkts_1519_max;
	stat->tx_single_colls += smb->tx_single_colls;
	stat->tx_multi_colls += smb->tx_multi_colls;
	stat->tx_late_colls += smb->tx_late_colls;
	stat->tx_excess_colls += smb->tx_excess_colls;
	stat->tx_underrun += smb->tx_underrun;
	stat->tx_desc_underrun += smb->tx_desc_underrun;
	stat->tx_lenerrs += smb->tx_lenerrs;
	stat->tx_pkts_truncated += smb->tx_pkts_truncated;
	stat->tx_bcast_bytes += smb->tx_bcast_bytes;
	stat->tx_mcast_bytes += smb->tx_mcast_bytes;

	/* Update counters in ifnet. */
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, smb->tx_frames);

	if_inc_counter(ifp, IFCOUNTER_COLLISIONS, smb->tx_single_colls +
	    smb->tx_multi_colls * 2 + smb->tx_late_colls +
	    smb->tx_excess_colls * HDPX_CFG_RETRY_DEFAULT);

	if_inc_counter(ifp, IFCOUNTER_OERRORS, smb->tx_late_colls +
	    smb->tx_excess_colls + smb->tx_underrun + smb->tx_pkts_truncated);

	if_inc_counter(ifp, IFCOUNTER_IPACKETS, smb->rx_frames);

	if_inc_counter(ifp, IFCOUNTER_IERRORS,
	    smb->rx_crcerrs + smb->rx_lenerrs +
	    smb->rx_runts + smb->rx_pkts_truncated +
	    smb->rx_fifo_oflows + smb->rx_rrs_errs +
	    smb->rx_alignerrs);

	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0) {
		/* Update done, clear. */
		smb->updated = 0;
		bus_dmamap_sync(sc->alc_cdata.alc_smb_tag,
		    sc->alc_cdata.alc_smb_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}
}

static int
alc_intr(void *arg)
{
	struct alc_softc *sc;
	uint32_t status;

	sc = (struct alc_softc *)arg;

	status = CSR_READ_4(sc, ALC_INTR_STATUS);
	if ((status & ALC_INTRS) == 0)
		return (FILTER_STRAY);
	/* Disable interrupts. */
	CSR_WRITE_4(sc, ALC_INTR_STATUS, INTR_DIS_INT);
	taskqueue_enqueue(sc->alc_tq, &sc->alc_int_task);

	return (FILTER_HANDLED);
}

static void
alc_int_task(void *arg, int pending)
{
	struct alc_softc *sc;
	struct ifnet *ifp;
	uint32_t status;
	int more;

	sc = (struct alc_softc *)arg;
	ifp = sc->alc_ifp;

	status = CSR_READ_4(sc, ALC_INTR_STATUS);
	ALC_LOCK(sc);
	if (sc->alc_morework != 0) {
		sc->alc_morework = 0;
		status |= INTR_RX_PKT;
	}
	if ((status & ALC_INTRS) == 0)
		goto done;

	/* Acknowledge interrupts but still disable interrupts. */
	CSR_WRITE_4(sc, ALC_INTR_STATUS, status | INTR_DIS_INT);

	more = 0;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		if ((status & INTR_RX_PKT) != 0) {
			more = alc_rxintr(sc, sc->alc_process_limit);
			if (more == EAGAIN)
				sc->alc_morework = 1;
			else if (more == EIO) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				alc_init_locked(sc);
				ALC_UNLOCK(sc);
				return;
			}
		}
		if ((status & (INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST |
		    INTR_TXQ_TO_RST)) != 0) {
			if ((status & INTR_DMA_RD_TO_RST) != 0)
				device_printf(sc->alc_dev,
				    "DMA read error! -- resetting\n");
			if ((status & INTR_DMA_WR_TO_RST) != 0)
				device_printf(sc->alc_dev,
				    "DMA write error! -- resetting\n");
			if ((status & INTR_TXQ_TO_RST) != 0)
				device_printf(sc->alc_dev,
				    "TxQ reset! -- resetting\n");
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			alc_init_locked(sc);
			ALC_UNLOCK(sc);
			return;
		}
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
		    !IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			alc_start_locked(ifp);
	}

	if (more == EAGAIN ||
	    (CSR_READ_4(sc, ALC_INTR_STATUS) & ALC_INTRS) != 0) {
		ALC_UNLOCK(sc);
		taskqueue_enqueue(sc->alc_tq, &sc->alc_int_task);
		return;
	}

done:
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		/* Re-enable interrupts if we're running. */
		CSR_WRITE_4(sc, ALC_INTR_STATUS, 0x7FFFFFFF);
	}
	ALC_UNLOCK(sc);
}

static void
alc_txeof(struct alc_softc *sc)
{
	struct ifnet *ifp;
	struct alc_txdesc *txd;
	uint32_t cons, prod;
	int prog;

	ALC_LOCK_ASSERT(sc);

	ifp = sc->alc_ifp;

	if (sc->alc_cdata.alc_tx_cnt == 0)
		return;
	bus_dmamap_sync(sc->alc_cdata.alc_tx_ring_tag,
	    sc->alc_cdata.alc_tx_ring_map, BUS_DMASYNC_POSTWRITE);
	if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0) {
		bus_dmamap_sync(sc->alc_cdata.alc_cmb_tag,
		    sc->alc_cdata.alc_cmb_map, BUS_DMASYNC_POSTREAD);
		prod = sc->alc_rdata.alc_cmb->cons;
	} else {
		if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
			prod = CSR_READ_2(sc, ALC_MBOX_TD_PRI0_CONS_IDX);
		else {
			prod = CSR_READ_4(sc, ALC_MBOX_TD_CONS_IDX);
			/* Assume we're using normal Tx priority queue. */
			prod = (prod & MBOX_TD_CONS_LO_IDX_MASK) >>
			    MBOX_TD_CONS_LO_IDX_SHIFT;
		}
	}
	cons = sc->alc_cdata.alc_tx_cons;
	/*
	 * Go through our Tx list and free mbufs for those
	 * frames which have been transmitted.
	 */
	for (prog = 0; cons != prod; prog++,
	    ALC_DESC_INC(cons, ALC_TX_RING_CNT)) {
		if (sc->alc_cdata.alc_tx_cnt <= 0)
			break;
		prog++;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		sc->alc_cdata.alc_tx_cnt--;
		txd = &sc->alc_cdata.alc_txdesc[cons];
		if (txd->tx_m != NULL) {
			/* Reclaim transmitted mbufs. */
			bus_dmamap_sync(sc->alc_cdata.alc_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->alc_cdata.alc_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}

	if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0)
		bus_dmamap_sync(sc->alc_cdata.alc_cmb_tag,
		    sc->alc_cdata.alc_cmb_map, BUS_DMASYNC_PREREAD);
	sc->alc_cdata.alc_tx_cons = cons;
	/*
	 * Unarm watchdog timer only when there is no pending
	 * frames in Tx queue.
	 */
	if (sc->alc_cdata.alc_tx_cnt == 0)
		sc->alc_watchdog_timer = 0;
}

static int
alc_newbuf(struct alc_softc *sc, struct alc_rxdesc *rxd)
{
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = RX_BUF_SIZE_MAX;
#ifndef __NO_STRICT_ALIGNMENT
	m_adj(m, sizeof(uint64_t));
#endif

	if (bus_dmamap_load_mbuf_sg(sc->alc_cdata.alc_rx_tag,
	    sc->alc_cdata.alc_rx_sparemap, m, segs, &nsegs, 0) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc->alc_cdata.alc_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->alc_cdata.alc_rx_tag, rxd->rx_dmamap);
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc->alc_cdata.alc_rx_sparemap;
	sc->alc_cdata.alc_rx_sparemap = map;
	bus_dmamap_sync(sc->alc_cdata.alc_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	rxd->rx_desc->addr = htole64(segs[0].ds_addr);
	return (0);
}

static int
alc_rxintr(struct alc_softc *sc, int count)
{
	struct ifnet *ifp;
	struct rx_rdesc *rrd;
	uint32_t nsegs, status;
	int rr_cons, prog;

	bus_dmamap_sync(sc->alc_cdata.alc_rr_ring_tag,
	    sc->alc_cdata.alc_rr_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(sc->alc_cdata.alc_rx_ring_tag,
	    sc->alc_cdata.alc_rx_ring_map, BUS_DMASYNC_POSTWRITE);
	rr_cons = sc->alc_cdata.alc_rr_cons;
	ifp = sc->alc_ifp;
	for (prog = 0; (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0;) {
		if (count-- <= 0)
			break;
		rrd = &sc->alc_rdata.alc_rr_ring[rr_cons];
		status = le32toh(rrd->status);
		if ((status & RRD_VALID) == 0)
			break;
		nsegs = RRD_RD_CNT(le32toh(rrd->rdinfo));
		if (nsegs == 0) {
			/* This should not happen! */
			device_printf(sc->alc_dev,
			    "unexpected segment count -- resetting\n");
			return (EIO);
		}
		alc_rxeof(sc, rrd);
		/* Clear Rx return status. */
		rrd->status = 0;
		ALC_DESC_INC(rr_cons, ALC_RR_RING_CNT);
		sc->alc_cdata.alc_rx_cons += nsegs;
		sc->alc_cdata.alc_rx_cons %= ALC_RR_RING_CNT;
		prog += nsegs;
	}

	if (prog > 0) {
		/* Update the consumer index. */
		sc->alc_cdata.alc_rr_cons = rr_cons;
		/* Sync Rx return descriptors. */
		bus_dmamap_sync(sc->alc_cdata.alc_rr_ring_tag,
		    sc->alc_cdata.alc_rr_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/*
		 * Sync updated Rx descriptors such that controller see
		 * modified buffer addresses.
		 */
		bus_dmamap_sync(sc->alc_cdata.alc_rx_ring_tag,
		    sc->alc_cdata.alc_rx_ring_map, BUS_DMASYNC_PREWRITE);
		/*
		 * Let controller know availability of new Rx buffers.
		 * Since alc(4) use RXQ_CFG_RD_BURST_DEFAULT descriptors
		 * it may be possible to update ALC_MBOX_RD0_PROD_IDX
		 * only when Rx buffer pre-fetching is required. In
		 * addition we already set ALC_RX_RD_FREE_THRESH to
		 * RX_RD_FREE_THRESH_LO_DEFAULT descriptors. However
		 * it still seems that pre-fetching needs more
		 * experimentation.
		 */
		if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
			CSR_WRITE_2(sc, ALC_MBOX_RD0_PROD_IDX,
			    (uint16_t)sc->alc_cdata.alc_rx_cons);
		else
			CSR_WRITE_4(sc, ALC_MBOX_RD0_PROD_IDX,
			    sc->alc_cdata.alc_rx_cons);
	}

	return (count > 0 ? 0 : EAGAIN);
}

#ifndef __NO_STRICT_ALIGNMENT
static struct mbuf *
alc_fixup_rx(struct ifnet *ifp, struct mbuf *m)
{
	struct mbuf *n;
        int i;
        uint16_t *src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - 3;

	if (m->m_next == NULL) {
		for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
			*dst++ = *src++;
		m->m_data -= 6;
		return (m);
	}
	/*
	 * Append a new mbuf to received mbuf chain and copy ethernet
	 * header from the mbuf chain. This can save lots of CPU
	 * cycles for jumbo frame.
	 */
	MGETHDR(n, M_NOWAIT, MT_DATA);
	if (n == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
		m_freem(m);
		return (NULL);
	}
	bcopy(m->m_data, n->m_data, ETHER_HDR_LEN);
	m->m_data += ETHER_HDR_LEN;
	m->m_len -= ETHER_HDR_LEN;
	n->m_len = ETHER_HDR_LEN;
	M_MOVE_PKTHDR(n, m);
	n->m_next = m;
	return (n);
}
#endif

/* Receive a frame. */
static void
alc_rxeof(struct alc_softc *sc, struct rx_rdesc *rrd)
{
	struct alc_rxdesc *rxd;
	struct ifnet *ifp;
	struct mbuf *mp, *m;
	uint32_t rdinfo, status, vtag;
	int count, nsegs, rx_cons;

	ifp = sc->alc_ifp;
	status = le32toh(rrd->status);
	rdinfo = le32toh(rrd->rdinfo);
	rx_cons = RRD_RD_IDX(rdinfo);
	nsegs = RRD_RD_CNT(rdinfo);

	sc->alc_cdata.alc_rxlen = RRD_BYTES(status);
	if ((status & (RRD_ERR_SUM | RRD_ERR_LENGTH)) != 0) {
		/*
		 * We want to pass the following frames to upper
		 * layer regardless of error status of Rx return
		 * ring.
		 *
		 *  o IP/TCP/UDP checksum is bad.
		 *  o frame length and protocol specific length
		 *     does not match.
		 *
		 *  Force network stack compute checksum for
		 *  errored frames.
		 */
		status |= RRD_TCP_UDPCSUM_NOK | RRD_IPCSUM_NOK;
		if ((status & (RRD_ERR_CRC | RRD_ERR_ALIGN |
		    RRD_ERR_TRUNC | RRD_ERR_RUNT)) != 0)
			return;
	}

	for (count = 0; count < nsegs; count++,
	    ALC_DESC_INC(rx_cons, ALC_RX_RING_CNT)) {
		rxd = &sc->alc_cdata.alc_rxdesc[rx_cons];
		mp = rxd->rx_m;
		/* Add a new receive buffer to the ring. */
		if (alc_newbuf(sc, rxd) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/* Reuse Rx buffers. */
			if (sc->alc_cdata.alc_rxhead != NULL)
				m_freem(sc->alc_cdata.alc_rxhead);
			break;
		}

		/*
		 * Assume we've received a full sized frame.
		 * Actual size is fixed when we encounter the end of
		 * multi-segmented frame.
		 */
		mp->m_len = sc->alc_buf_size;

		/* Chain received mbufs. */
		if (sc->alc_cdata.alc_rxhead == NULL) {
			sc->alc_cdata.alc_rxhead = mp;
			sc->alc_cdata.alc_rxtail = mp;
		} else {
			mp->m_flags &= ~M_PKTHDR;
			sc->alc_cdata.alc_rxprev_tail =
			    sc->alc_cdata.alc_rxtail;
			sc->alc_cdata.alc_rxtail->m_next = mp;
			sc->alc_cdata.alc_rxtail = mp;
		}

		if (count == nsegs - 1) {
			/* Last desc. for this frame. */
			m = sc->alc_cdata.alc_rxhead;
			m->m_flags |= M_PKTHDR;
			/*
			 * It seems that L1C/L2C controller has no way
			 * to tell hardware to strip CRC bytes.
			 */
			m->m_pkthdr.len =
			    sc->alc_cdata.alc_rxlen - ETHER_CRC_LEN;
			if (nsegs > 1) {
				/* Set last mbuf size. */
				mp->m_len = sc->alc_cdata.alc_rxlen -
				    (nsegs - 1) * sc->alc_buf_size;
				/* Remove the CRC bytes in chained mbufs. */
				if (mp->m_len <= ETHER_CRC_LEN) {
					sc->alc_cdata.alc_rxtail =
					    sc->alc_cdata.alc_rxprev_tail;
					sc->alc_cdata.alc_rxtail->m_len -=
					    (ETHER_CRC_LEN - mp->m_len);
					sc->alc_cdata.alc_rxtail->m_next = NULL;
					m_freem(mp);
				} else {
					mp->m_len -= ETHER_CRC_LEN;
				}
			} else
				m->m_len = m->m_pkthdr.len;
			m->m_pkthdr.rcvif = ifp;
			/*
			 * Due to hardware bugs, Rx checksum offloading
			 * was intentionally disabled.
			 */
			if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0 &&
			    (status & RRD_VLAN_TAG) != 0) {
				vtag = RRD_VLAN(le32toh(rrd->vtag));
				m->m_pkthdr.ether_vtag = ntohs(vtag);
				m->m_flags |= M_VLANTAG;
			}
#ifndef __NO_STRICT_ALIGNMENT
			m = alc_fixup_rx(ifp, m);
			if (m != NULL)
#endif
			{
			/* Pass it on. */
			ALC_UNLOCK(sc);
			(*ifp->if_input)(ifp, m);
			ALC_LOCK(sc);
			}
		}
	}
	/* Reset mbuf chains. */
	ALC_RXCHAIN_RESET(sc);
}

static void
alc_tick(void *arg)
{
	struct alc_softc *sc;
	struct mii_data *mii;

	sc = (struct alc_softc *)arg;

	ALC_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->alc_miibus);
	mii_tick(mii);
	alc_stats_update(sc);
	/*
	 * alc(4) does not rely on Tx completion interrupts to reclaim
	 * transferred buffers. Instead Tx completion interrupts are
	 * used to hint for scheduling Tx task. So it's necessary to
	 * release transmitted buffers by kicking Tx completion
	 * handler. This limits the maximum reclamation delay to a hz.
	 */
	alc_txeof(sc);
	alc_watchdog(sc);
	callout_reset(&sc->alc_tick_ch, hz, alc_tick, sc);
}

static void
alc_osc_reset(struct alc_softc *sc)
{
	uint32_t reg;

	reg = CSR_READ_4(sc, ALC_MISC3);
	reg &= ~MISC3_25M_BY_SW;
	reg |= MISC3_25M_NOTO_INTNL;
	CSR_WRITE_4(sc, ALC_MISC3, reg);

	reg = CSR_READ_4(sc, ALC_MISC);
	if (AR816X_REV(sc->alc_rev) >= AR816X_REV_B0) {
		/*
		 * Restore over-current protection default value.
		 * This value could be reset by MAC reset.
		 */
		reg &= ~MISC_PSW_OCP_MASK;
		reg |= (MISC_PSW_OCP_DEFAULT << MISC_PSW_OCP_SHIFT);
		reg &= ~MISC_INTNLOSC_OPEN;
		CSR_WRITE_4(sc, ALC_MISC, reg);
		CSR_WRITE_4(sc, ALC_MISC, reg | MISC_INTNLOSC_OPEN);
		reg = CSR_READ_4(sc, ALC_MISC2);
		reg &= ~MISC2_CALB_START;
		CSR_WRITE_4(sc, ALC_MISC2, reg);
		CSR_WRITE_4(sc, ALC_MISC2, reg | MISC2_CALB_START);

	} else {
		reg &= ~MISC_INTNLOSC_OPEN;
		/* Disable isolate for revision A devices. */
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1)
			reg &= ~MISC_ISO_ENB;
		CSR_WRITE_4(sc, ALC_MISC, reg | MISC_INTNLOSC_OPEN);
		CSR_WRITE_4(sc, ALC_MISC, reg);
	}

	DELAY(20);
}

static void
alc_reset(struct alc_softc *sc)
{
	uint32_t pmcfg, reg;
	int i;

	pmcfg = 0;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		/* Reset workaround. */
		CSR_WRITE_4(sc, ALC_MBOX_RD0_PROD_IDX, 1);
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
		    (sc->alc_rev & 0x01) != 0) {
			/* Disable L0s/L1s before reset. */
			pmcfg = CSR_READ_4(sc, ALC_PM_CFG);
			if ((pmcfg & (PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB))
			    != 0) {
				pmcfg &= ~(PM_CFG_ASPM_L0S_ENB |
				    PM_CFG_ASPM_L1_ENB);
				CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
			}
		}
	}
	reg = CSR_READ_4(sc, ALC_MASTER_CFG);
	reg |= MASTER_OOB_DIS_OFF | MASTER_RESET;
	CSR_WRITE_4(sc, ALC_MASTER_CFG, reg);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		for (i = ALC_RESET_TIMEOUT; i > 0; i--) {
			DELAY(10);
			if (CSR_READ_4(sc, ALC_MBOX_RD0_PROD_IDX) == 0)
				break;
		}
		if (i == 0)
			device_printf(sc->alc_dev, "MAC reset timeout!\n");
	}
	for (i = ALC_RESET_TIMEOUT; i > 0; i--) {
		DELAY(10);
		if ((CSR_READ_4(sc, ALC_MASTER_CFG) & MASTER_RESET) == 0)
			break;
	}
	if (i == 0)
		device_printf(sc->alc_dev, "master reset timeout!\n");

	for (i = ALC_RESET_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_IDLE_STATUS);
		if ((reg & (IDLE_STATUS_RXMAC | IDLE_STATUS_TXMAC |
		    IDLE_STATUS_RXQ | IDLE_STATUS_TXQ)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		device_printf(sc->alc_dev, "reset timeout(0x%08x)!\n", reg);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1 &&
		    (sc->alc_rev & 0x01) != 0) {
			reg = CSR_READ_4(sc, ALC_MASTER_CFG);
			reg |= MASTER_CLK_SEL_DIS;
			CSR_WRITE_4(sc, ALC_MASTER_CFG, reg);
			/* Restore L0s/L1s config. */
			if ((pmcfg & (PM_CFG_ASPM_L0S_ENB | PM_CFG_ASPM_L1_ENB))
			    != 0)
				CSR_WRITE_4(sc, ALC_PM_CFG, pmcfg);
		}

		alc_osc_reset(sc);
		reg = CSR_READ_4(sc, ALC_MISC3);
		reg &= ~MISC3_25M_BY_SW;
		reg |= MISC3_25M_NOTO_INTNL;
		CSR_WRITE_4(sc, ALC_MISC3, reg);
		reg = CSR_READ_4(sc, ALC_MISC);
		reg &= ~MISC_INTNLOSC_OPEN;
		if (AR816X_REV(sc->alc_rev) <= AR816X_REV_A1)
			reg &= ~MISC_ISO_ENB;
		CSR_WRITE_4(sc, ALC_MISC, reg);
		DELAY(20);
	}
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151_V2)
		CSR_WRITE_4(sc, ALC_SERDES_LOCK,
		    CSR_READ_4(sc, ALC_SERDES_LOCK) | SERDES_MAC_CLK_SLOWDOWN |
		    SERDES_PHY_CLK_SLOWDOWN);
}

static void
alc_init(void *xsc)
{
	struct alc_softc *sc;

	sc = (struct alc_softc *)xsc;
	ALC_LOCK(sc);
	alc_init_locked(sc);
	ALC_UNLOCK(sc);
}

static void
alc_init_locked(struct alc_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint8_t eaddr[ETHER_ADDR_LEN];
	bus_addr_t paddr;
	uint32_t reg, rxf_hi, rxf_lo;

	ALC_LOCK_ASSERT(sc);

	ifp = sc->alc_ifp;
	mii = device_get_softc(sc->alc_miibus);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;
	/*
	 * Cancel any pending I/O.
	 */
	alc_stop(sc);
	/*
	 * Reset the chip to a known state.
	 */
	alc_reset(sc);

	/* Initialize Rx descriptors. */
	if (alc_init_rx_ring(sc) != 0) {
		device_printf(sc->alc_dev, "no memory for Rx buffers.\n");
		alc_stop(sc);
		return;
	}
	alc_init_rr_ring(sc);
	alc_init_tx_ring(sc);
	alc_init_cmb(sc);
	alc_init_smb(sc);

	/* Enable all clocks. */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		CSR_WRITE_4(sc, ALC_CLK_GATING_CFG, CLK_GATING_DMAW_ENB |
		    CLK_GATING_DMAR_ENB | CLK_GATING_TXQ_ENB |
		    CLK_GATING_RXQ_ENB | CLK_GATING_TXMAC_ENB |
		    CLK_GATING_RXMAC_ENB);
		if (AR816X_REV(sc->alc_rev) >= AR816X_REV_B0)
			CSR_WRITE_4(sc, ALC_IDLE_DECISN_TIMER,
			    IDLE_DECISN_TIMER_DEFAULT_1MS);
	} else
		CSR_WRITE_4(sc, ALC_CLK_GATING_CFG, 0);

	/* Reprogram the station address. */
	bcopy(IF_LLADDR(ifp), eaddr, ETHER_ADDR_LEN);
	CSR_WRITE_4(sc, ALC_PAR0,
	    eaddr[2] << 24 | eaddr[3] << 16 | eaddr[4] << 8 | eaddr[5]);
	CSR_WRITE_4(sc, ALC_PAR1, eaddr[0] << 8 | eaddr[1]);
	/*
	 * Clear WOL status and disable all WOL feature as WOL
	 * would interfere Rx operation under normal environments.
	 */
	CSR_READ_4(sc, ALC_WOL_CFG);
	CSR_WRITE_4(sc, ALC_WOL_CFG, 0);
	/* Set Tx descriptor base addresses. */
	paddr = sc->alc_rdata.alc_tx_ring_paddr;
	CSR_WRITE_4(sc, ALC_TX_BASE_ADDR_HI, ALC_ADDR_HI(paddr));
	CSR_WRITE_4(sc, ALC_TDL_HEAD_ADDR_LO, ALC_ADDR_LO(paddr));
	/* We don't use high priority ring. */
	CSR_WRITE_4(sc, ALC_TDH_HEAD_ADDR_LO, 0);
	/* Set Tx descriptor counter. */
	CSR_WRITE_4(sc, ALC_TD_RING_CNT,
	    (ALC_TX_RING_CNT << TD_RING_CNT_SHIFT) & TD_RING_CNT_MASK);
	/* Set Rx descriptor base addresses. */
	paddr = sc->alc_rdata.alc_rx_ring_paddr;
	CSR_WRITE_4(sc, ALC_RX_BASE_ADDR_HI, ALC_ADDR_HI(paddr));
	CSR_WRITE_4(sc, ALC_RD0_HEAD_ADDR_LO, ALC_ADDR_LO(paddr));
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* We use one Rx ring. */
		CSR_WRITE_4(sc, ALC_RD1_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RD2_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RD3_HEAD_ADDR_LO, 0);
	}
	/* Set Rx descriptor counter. */
	CSR_WRITE_4(sc, ALC_RD_RING_CNT,
	    (ALC_RX_RING_CNT << RD_RING_CNT_SHIFT) & RD_RING_CNT_MASK);

	/*
	 * Let hardware split jumbo frames into alc_max_buf_sized chunks.
	 * if it do not fit the buffer size. Rx return descriptor holds
	 * a counter that indicates how many fragments were made by the
	 * hardware. The buffer size should be multiple of 8 bytes.
	 * Since hardware has limit on the size of buffer size, always
	 * use the maximum value.
	 * For strict-alignment architectures make sure to reduce buffer
	 * size by 8 bytes to make room for alignment fixup.
	 */
#ifndef __NO_STRICT_ALIGNMENT
	sc->alc_buf_size = RX_BUF_SIZE_MAX - sizeof(uint64_t);
#else
	sc->alc_buf_size = RX_BUF_SIZE_MAX;
#endif
	CSR_WRITE_4(sc, ALC_RX_BUF_SIZE, sc->alc_buf_size);

	paddr = sc->alc_rdata.alc_rr_ring_paddr;
	/* Set Rx return descriptor base addresses. */
	CSR_WRITE_4(sc, ALC_RRD0_HEAD_ADDR_LO, ALC_ADDR_LO(paddr));
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* We use one Rx return ring. */
		CSR_WRITE_4(sc, ALC_RRD1_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RRD2_HEAD_ADDR_LO, 0);
		CSR_WRITE_4(sc, ALC_RRD3_HEAD_ADDR_LO, 0);
	}
	/* Set Rx return descriptor counter. */
	CSR_WRITE_4(sc, ALC_RRD_RING_CNT,
	    (ALC_RR_RING_CNT << RRD_RING_CNT_SHIFT) & RRD_RING_CNT_MASK);
	paddr = sc->alc_rdata.alc_cmb_paddr;
	CSR_WRITE_4(sc, ALC_CMB_BASE_ADDR_LO, ALC_ADDR_LO(paddr));
	paddr = sc->alc_rdata.alc_smb_paddr;
	CSR_WRITE_4(sc, ALC_SMB_BASE_ADDR_HI, ALC_ADDR_HI(paddr));
	CSR_WRITE_4(sc, ALC_SMB_BASE_ADDR_LO, ALC_ADDR_LO(paddr));

	if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B) {
		/* Reconfigure SRAM - Vendor magic. */
		CSR_WRITE_4(sc, ALC_SRAM_RX_FIFO_LEN, 0x000002A0);
		CSR_WRITE_4(sc, ALC_SRAM_TX_FIFO_LEN, 0x00000100);
		CSR_WRITE_4(sc, ALC_SRAM_RX_FIFO_ADDR, 0x029F0000);
		CSR_WRITE_4(sc, ALC_SRAM_RD0_ADDR, 0x02BF02A0);
		CSR_WRITE_4(sc, ALC_SRAM_TX_FIFO_ADDR, 0x03BF02C0);
		CSR_WRITE_4(sc, ALC_SRAM_TD_ADDR, 0x03DF03C0);
		CSR_WRITE_4(sc, ALC_TXF_WATER_MARK, 0x00000000);
		CSR_WRITE_4(sc, ALC_RD_DMA_CFG, 0x00000000);
	}

	/* Tell hardware that we're ready to load DMA blocks. */
	CSR_WRITE_4(sc, ALC_DMA_BLOCK, DMA_BLOCK_LOAD);

	/* Configure interrupt moderation timer. */
	reg = ALC_USECS(sc->alc_int_rx_mod) << IM_TIMER_RX_SHIFT;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0)
		reg |= ALC_USECS(sc->alc_int_tx_mod) << IM_TIMER_TX_SHIFT;
	CSR_WRITE_4(sc, ALC_IM_TIMER, reg);
	/*
	 * We don't want to automatic interrupt clear as task queue
	 * for the interrupt should know interrupt status.
	 */
	reg = CSR_READ_4(sc, ALC_MASTER_CFG);
	reg &= ~(MASTER_IM_RX_TIMER_ENB | MASTER_IM_TX_TIMER_ENB);
	reg |= MASTER_SA_TIMER_ENB;
	if (ALC_USECS(sc->alc_int_rx_mod) != 0)
		reg |= MASTER_IM_RX_TIMER_ENB;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0 &&
	    ALC_USECS(sc->alc_int_tx_mod) != 0)
		reg |= MASTER_IM_TX_TIMER_ENB;
	CSR_WRITE_4(sc, ALC_MASTER_CFG, reg);
	/*
	 * Disable interrupt re-trigger timer. We don't want automatic
	 * re-triggering of un-ACKed interrupts.
	 */
	CSR_WRITE_4(sc, ALC_INTR_RETRIG_TIMER, ALC_USECS(0));
	/* Configure CMB. */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		CSR_WRITE_4(sc, ALC_CMB_TD_THRESH, ALC_TX_RING_CNT / 3);
		CSR_WRITE_4(sc, ALC_CMB_TX_TIMER,
		    ALC_USECS(sc->alc_int_tx_mod));
	} else {
		if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0) {
			CSR_WRITE_4(sc, ALC_CMB_TD_THRESH, 4);
			CSR_WRITE_4(sc, ALC_CMB_TX_TIMER, ALC_USECS(5000));
		} else
			CSR_WRITE_4(sc, ALC_CMB_TX_TIMER, ALC_USECS(0));
	}
	/*
	 * Hardware can be configured to issue SMB interrupt based
	 * on programmed interval. Since there is a callout that is
	 * invoked for every hz in driver we use that instead of
	 * relying on periodic SMB interrupt.
	 */
	CSR_WRITE_4(sc, ALC_SMB_STAT_TIMER, ALC_USECS(0));
	/* Clear MAC statistics. */
	alc_stats_clear(sc);

	/*
	 * Always use maximum frame size that controller can support.
	 * Otherwise received frames that has larger frame length
	 * than alc(4) MTU would be silently dropped in hardware. This
	 * would make path-MTU discovery hard as sender wouldn't get
	 * any responses from receiver. alc(4) supports
	 * multi-fragmented frames on Rx path so it has no issue on
	 * assembling fragmented frames. Using maximum frame size also
	 * removes the need to reinitialize hardware when interface
	 * MTU configuration was changed.
	 *
	 * Be conservative in what you do, be liberal in what you
	 * accept from others - RFC 793.
	 */
	CSR_WRITE_4(sc, ALC_FRAME_SIZE, sc->alc_ident->max_framelen);

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* Disable header split(?) */
		CSR_WRITE_4(sc, ALC_HDS_CFG, 0);

		/* Configure IPG/IFG parameters. */
		CSR_WRITE_4(sc, ALC_IPG_IFG_CFG,
		    ((IPG_IFG_IPGT_DEFAULT << IPG_IFG_IPGT_SHIFT) &
		    IPG_IFG_IPGT_MASK) |
		    ((IPG_IFG_MIFG_DEFAULT << IPG_IFG_MIFG_SHIFT) &
		    IPG_IFG_MIFG_MASK) |
		    ((IPG_IFG_IPG1_DEFAULT << IPG_IFG_IPG1_SHIFT) &
		    IPG_IFG_IPG1_MASK) |
		    ((IPG_IFG_IPG2_DEFAULT << IPG_IFG_IPG2_SHIFT) &
		    IPG_IFG_IPG2_MASK));
		/* Set parameters for half-duplex media. */
		CSR_WRITE_4(sc, ALC_HDPX_CFG,
		    ((HDPX_CFG_LCOL_DEFAULT << HDPX_CFG_LCOL_SHIFT) &
		    HDPX_CFG_LCOL_MASK) |
		    ((HDPX_CFG_RETRY_DEFAULT << HDPX_CFG_RETRY_SHIFT) &
		    HDPX_CFG_RETRY_MASK) | HDPX_CFG_EXC_DEF_EN |
		    ((HDPX_CFG_ABEBT_DEFAULT << HDPX_CFG_ABEBT_SHIFT) &
		    HDPX_CFG_ABEBT_MASK) |
		    ((HDPX_CFG_JAMIPG_DEFAULT << HDPX_CFG_JAMIPG_SHIFT) &
		    HDPX_CFG_JAMIPG_MASK));
	}

	/*
	 * Set TSO/checksum offload threshold. For frames that is
	 * larger than this threshold, hardware wouldn't do
	 * TSO/checksum offloading.
	 */
	reg = (sc->alc_ident->max_framelen >> TSO_OFFLOAD_THRESH_UNIT_SHIFT) &
	    TSO_OFFLOAD_THRESH_MASK;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0)
		reg |= TSO_OFFLOAD_ERRLGPKT_DROP_ENB;
	CSR_WRITE_4(sc, ALC_TSO_OFFLOAD_THRESH, reg);
	/* Configure TxQ. */
	reg = (alc_dma_burst[sc->alc_dma_rd_burst] <<
	    TXQ_CFG_TX_FIFO_BURST_SHIFT) & TXQ_CFG_TX_FIFO_BURST_MASK;
	if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B2)
		reg >>= 1;
	reg |= (TXQ_CFG_TD_BURST_DEFAULT << TXQ_CFG_TD_BURST_SHIFT) &
	    TXQ_CFG_TD_BURST_MASK;
	reg |= TXQ_CFG_IP_OPTION_ENB | TXQ_CFG_8023_ENB;
	CSR_WRITE_4(sc, ALC_TXQ_CFG, reg | TXQ_CFG_ENHANCED_MODE);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		reg = (TXQ_CFG_TD_BURST_DEFAULT << HQTD_CFG_Q1_BURST_SHIFT |
		    TXQ_CFG_TD_BURST_DEFAULT << HQTD_CFG_Q2_BURST_SHIFT |
		    TXQ_CFG_TD_BURST_DEFAULT << HQTD_CFG_Q3_BURST_SHIFT |
		    HQTD_CFG_BURST_ENB);
		CSR_WRITE_4(sc, ALC_HQTD_CFG, reg);
		reg = WRR_PRI_RESTRICT_NONE;
		reg |= (WRR_PRI_DEFAULT << WRR_PRI0_SHIFT |
		    WRR_PRI_DEFAULT << WRR_PRI1_SHIFT |
		    WRR_PRI_DEFAULT << WRR_PRI2_SHIFT |
		    WRR_PRI_DEFAULT << WRR_PRI3_SHIFT);
		CSR_WRITE_4(sc, ALC_WRR, reg);
	} else {
		/* Configure Rx free descriptor pre-fetching. */
		CSR_WRITE_4(sc, ALC_RX_RD_FREE_THRESH,
		    ((RX_RD_FREE_THRESH_HI_DEFAULT <<
		    RX_RD_FREE_THRESH_HI_SHIFT) & RX_RD_FREE_THRESH_HI_MASK) |
		    ((RX_RD_FREE_THRESH_LO_DEFAULT <<
		    RX_RD_FREE_THRESH_LO_SHIFT) & RX_RD_FREE_THRESH_LO_MASK));
	}

	/*
	 * Configure flow control parameters.
	 * XON  : 80% of Rx FIFO
	 * XOFF : 30% of Rx FIFO
	 */
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		reg = CSR_READ_4(sc, ALC_SRAM_RX_FIFO_LEN);
		reg &= SRAM_RX_FIFO_LEN_MASK;
		reg *= 8;
		if (reg > 8 * 1024)
			reg -= RX_FIFO_PAUSE_816X_RSVD;
		else
			reg -= RX_BUF_SIZE_MAX;
		reg /= 8;
		CSR_WRITE_4(sc, ALC_RX_FIFO_PAUSE_THRESH,
		    ((reg << RX_FIFO_PAUSE_THRESH_LO_SHIFT) &
		    RX_FIFO_PAUSE_THRESH_LO_MASK) |
		    (((RX_FIFO_PAUSE_816X_RSVD / 8) <<
		    RX_FIFO_PAUSE_THRESH_HI_SHIFT) &
		    RX_FIFO_PAUSE_THRESH_HI_MASK));
	} else if (sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8131 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8132) {
		reg = CSR_READ_4(sc, ALC_SRAM_RX_FIFO_LEN);
		rxf_hi = (reg * 8) / 10;
		rxf_lo = (reg * 3) / 10;
		CSR_WRITE_4(sc, ALC_RX_FIFO_PAUSE_THRESH,
		    ((rxf_lo << RX_FIFO_PAUSE_THRESH_LO_SHIFT) &
		     RX_FIFO_PAUSE_THRESH_LO_MASK) |
		    ((rxf_hi << RX_FIFO_PAUSE_THRESH_HI_SHIFT) &
		     RX_FIFO_PAUSE_THRESH_HI_MASK));
	}

	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		/* Disable RSS until I understand L1C/L2C's RSS logic. */
		CSR_WRITE_4(sc, ALC_RSS_IDT_TABLE0, 0);
		CSR_WRITE_4(sc, ALC_RSS_CPU, 0);
	}

	/* Configure RxQ. */
	reg = (RXQ_CFG_RD_BURST_DEFAULT << RXQ_CFG_RD_BURST_SHIFT) &
	    RXQ_CFG_RD_BURST_MASK;
	reg |= RXQ_CFG_RSS_MODE_DIS;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		reg |= (RXQ_CFG_816X_IDT_TBL_SIZE_DEFAULT <<
		    RXQ_CFG_816X_IDT_TBL_SIZE_SHIFT) &
		    RXQ_CFG_816X_IDT_TBL_SIZE_MASK;
		if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0)
			reg |= RXQ_CFG_ASPM_THROUGHPUT_LIMIT_100M;
	} else {
		if ((sc->alc_flags & ALC_FLAG_FASTETHER) == 0 &&
		    sc->alc_ident->deviceid != DEVICEID_ATHEROS_AR8151_V2)
			reg |= RXQ_CFG_ASPM_THROUGHPUT_LIMIT_100M;
	}
	CSR_WRITE_4(sc, ALC_RXQ_CFG, reg);

	/* Configure DMA parameters. */
	reg = DMA_CFG_OUT_ORDER | DMA_CFG_RD_REQ_PRI;
	reg |= sc->alc_rcb;
	if ((sc->alc_flags & ALC_FLAG_CMB_BUG) == 0)
		reg |= DMA_CFG_CMB_ENB;
	if ((sc->alc_flags & ALC_FLAG_SMB_BUG) == 0)
		reg |= DMA_CFG_SMB_ENB;
	else
		reg |= DMA_CFG_SMB_DIS;
	reg |= (sc->alc_dma_rd_burst & DMA_CFG_RD_BURST_MASK) <<
	    DMA_CFG_RD_BURST_SHIFT;
	reg |= (sc->alc_dma_wr_burst & DMA_CFG_WR_BURST_MASK) <<
	    DMA_CFG_WR_BURST_SHIFT;
	reg |= (DMA_CFG_RD_DELAY_CNT_DEFAULT << DMA_CFG_RD_DELAY_CNT_SHIFT) &
	    DMA_CFG_RD_DELAY_CNT_MASK;
	reg |= (DMA_CFG_WR_DELAY_CNT_DEFAULT << DMA_CFG_WR_DELAY_CNT_SHIFT) &
	    DMA_CFG_WR_DELAY_CNT_MASK;
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0) {
		switch (AR816X_REV(sc->alc_rev)) {
		case AR816X_REV_A0:
		case AR816X_REV_A1:
			reg |= DMA_CFG_RD_CHNL_SEL_2;
			break;
		case AR816X_REV_B0:
			/* FALLTHROUGH */
		default:
			reg |= DMA_CFG_RD_CHNL_SEL_4;
			break;
		}
	}
	CSR_WRITE_4(sc, ALC_DMA_CFG, reg);

	/*
	 * Configure Tx/Rx MACs.
	 *  - Auto-padding for short frames.
	 *  - Enable CRC generation.
	 *  Actual reconfiguration of MAC for resolved speed/duplex
	 *  is followed after detection of link establishment.
	 *  AR813x/AR815x always does checksum computation regardless
	 *  of MAC_CFG_RXCSUM_ENB bit. Also the controller is known to
	 *  have bug in protocol field in Rx return structure so
	 *  these controllers can't handle fragmented frames. Disable
	 *  Rx checksum offloading until there is a newer controller
	 *  that has sane implementation.
	 */
	reg = MAC_CFG_TX_CRC_ENB | MAC_CFG_TX_AUTO_PAD | MAC_CFG_FULL_DUPLEX |
	    ((MAC_CFG_PREAMBLE_DEFAULT << MAC_CFG_PREAMBLE_SHIFT) &
	    MAC_CFG_PREAMBLE_MASK);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) != 0 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8151_V2 ||
	    sc->alc_ident->deviceid == DEVICEID_ATHEROS_AR8152_B2)
		reg |= MAC_CFG_HASH_ALG_CRC32 | MAC_CFG_SPEED_MODE_SW;
	if ((sc->alc_flags & ALC_FLAG_FASTETHER) != 0)
		reg |= MAC_CFG_SPEED_10_100;
	else
		reg |= MAC_CFG_SPEED_1000;
	CSR_WRITE_4(sc, ALC_MAC_CFG, reg);

	/* Set up the receive filter. */
	alc_rxfilter(sc);
	alc_rxvlan(sc);

	/* Acknowledge all pending interrupts and clear it. */
	CSR_WRITE_4(sc, ALC_INTR_MASK, ALC_INTRS);
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0xFFFFFFFF);
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc->alc_flags &= ~ALC_FLAG_LINK;
	/* Switch to the current media. */
	alc_mediachange_locked(sc);

	callout_reset(&sc->alc_tick_ch, hz, alc_tick, sc);
}

static void
alc_stop(struct alc_softc *sc)
{
	struct ifnet *ifp;
	struct alc_txdesc *txd;
	struct alc_rxdesc *rxd;
	uint32_t reg;
	int i;

	ALC_LOCK_ASSERT(sc);
	/*
	 * Mark the interface down and cancel the watchdog timer.
	 */
	ifp = sc->alc_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->alc_flags &= ~ALC_FLAG_LINK;
	callout_stop(&sc->alc_tick_ch);
	sc->alc_watchdog_timer = 0;
	alc_stats_update(sc);
	/* Disable interrupts. */
	CSR_WRITE_4(sc, ALC_INTR_MASK, 0);
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0xFFFFFFFF);
	/* Disable DMA. */
	reg = CSR_READ_4(sc, ALC_DMA_CFG);
	reg &= ~(DMA_CFG_CMB_ENB | DMA_CFG_SMB_ENB);
	reg |= DMA_CFG_SMB_DIS;
	CSR_WRITE_4(sc, ALC_DMA_CFG, reg);
	DELAY(1000);
	/* Stop Rx/Tx MACs. */
	alc_stop_mac(sc);
	/* Disable interrupts which might be touched in taskq handler. */
	CSR_WRITE_4(sc, ALC_INTR_STATUS, 0xFFFFFFFF);
	/* Disable L0s/L1s */
	alc_aspm(sc, 0, IFM_UNKNOWN);
	/* Reclaim Rx buffers that have been processed. */
	if (sc->alc_cdata.alc_rxhead != NULL)
		m_freem(sc->alc_cdata.alc_rxhead);
	ALC_RXCHAIN_RESET(sc);
	/*
	 * Free Tx/Rx mbufs still in the queues.
	 */
	for (i = 0; i < ALC_RX_RING_CNT; i++) {
		rxd = &sc->alc_cdata.alc_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc->alc_cdata.alc_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->alc_cdata.alc_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < ALC_TX_RING_CNT; i++) {
		txd = &sc->alc_cdata.alc_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc->alc_cdata.alc_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->alc_cdata.alc_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}
}

static void
alc_stop_mac(struct alc_softc *sc)
{
	uint32_t reg;
	int i;

	alc_stop_queue(sc);
	/* Disable Rx/Tx MAC. */
	reg = CSR_READ_4(sc, ALC_MAC_CFG);
	if ((reg & (MAC_CFG_TX_ENB | MAC_CFG_RX_ENB)) != 0) {
		reg &= ~(MAC_CFG_TX_ENB | MAC_CFG_RX_ENB);
		CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
	}
	for (i = ALC_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_IDLE_STATUS);
		if ((reg & (IDLE_STATUS_RXMAC | IDLE_STATUS_TXMAC)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		device_printf(sc->alc_dev,
		    "could not disable Rx/Tx MAC(0x%08x)!\n", reg);
}

static void
alc_start_queue(struct alc_softc *sc)
{
	uint32_t qcfg[] = {
		0,
		RXQ_CFG_QUEUE0_ENB,
		RXQ_CFG_QUEUE0_ENB | RXQ_CFG_QUEUE1_ENB,
		RXQ_CFG_QUEUE0_ENB | RXQ_CFG_QUEUE1_ENB | RXQ_CFG_QUEUE2_ENB,
		RXQ_CFG_ENB
	};
	uint32_t cfg;

	ALC_LOCK_ASSERT(sc);

	/* Enable RxQ. */
	cfg = CSR_READ_4(sc, ALC_RXQ_CFG);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		cfg &= ~RXQ_CFG_ENB;
		cfg |= qcfg[1];
	} else
		cfg |= RXQ_CFG_QUEUE0_ENB;
	CSR_WRITE_4(sc, ALC_RXQ_CFG, cfg);
	/* Enable TxQ. */
	cfg = CSR_READ_4(sc, ALC_TXQ_CFG);
	cfg |= TXQ_CFG_ENB;
	CSR_WRITE_4(sc, ALC_TXQ_CFG, cfg);
}

static void
alc_stop_queue(struct alc_softc *sc)
{
	uint32_t reg;
	int i;

	/* Disable RxQ. */
	reg = CSR_READ_4(sc, ALC_RXQ_CFG);
	if ((sc->alc_flags & ALC_FLAG_AR816X_FAMILY) == 0) {
		if ((reg & RXQ_CFG_ENB) != 0) {
			reg &= ~RXQ_CFG_ENB;
			CSR_WRITE_4(sc, ALC_RXQ_CFG, reg);
		}
	} else {
		if ((reg & RXQ_CFG_QUEUE0_ENB) != 0) {
			reg &= ~RXQ_CFG_QUEUE0_ENB;
			CSR_WRITE_4(sc, ALC_RXQ_CFG, reg);
		}
	}
	/* Disable TxQ. */
	reg = CSR_READ_4(sc, ALC_TXQ_CFG);
	if ((reg & TXQ_CFG_ENB) != 0) {
		reg &= ~TXQ_CFG_ENB;
		CSR_WRITE_4(sc, ALC_TXQ_CFG, reg);
	}
	DELAY(40);
	for (i = ALC_TIMEOUT; i > 0; i--) {
		reg = CSR_READ_4(sc, ALC_IDLE_STATUS);
		if ((reg & (IDLE_STATUS_RXQ | IDLE_STATUS_TXQ)) == 0)
			break;
		DELAY(10);
	}
	if (i == 0)
		device_printf(sc->alc_dev,
		    "could not disable RxQ/TxQ (0x%08x)!\n", reg);
}

static void
alc_init_tx_ring(struct alc_softc *sc)
{
	struct alc_ring_data *rd;
	struct alc_txdesc *txd;
	int i;

	ALC_LOCK_ASSERT(sc);

	sc->alc_cdata.alc_tx_prod = 0;
	sc->alc_cdata.alc_tx_cons = 0;
	sc->alc_cdata.alc_tx_cnt = 0;

	rd = &sc->alc_rdata;
	bzero(rd->alc_tx_ring, ALC_TX_RING_SZ);
	for (i = 0; i < ALC_TX_RING_CNT; i++) {
		txd = &sc->alc_cdata.alc_txdesc[i];
		txd->tx_m = NULL;
	}

	bus_dmamap_sync(sc->alc_cdata.alc_tx_ring_tag,
	    sc->alc_cdata.alc_tx_ring_map, BUS_DMASYNC_PREWRITE);
}

static int
alc_init_rx_ring(struct alc_softc *sc)
{
	struct alc_ring_data *rd;
	struct alc_rxdesc *rxd;
	int i;

	ALC_LOCK_ASSERT(sc);

	sc->alc_cdata.alc_rx_cons = ALC_RX_RING_CNT - 1;
	sc->alc_morework = 0;
	rd = &sc->alc_rdata;
	bzero(rd->alc_rx_ring, ALC_RX_RING_SZ);
	for (i = 0; i < ALC_RX_RING_CNT; i++) {
		rxd = &sc->alc_cdata.alc_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_desc = &rd->alc_rx_ring[i];
		if (alc_newbuf(sc, rxd) != 0)
			return (ENOBUFS);
	}

	/*
	 * Since controller does not update Rx descriptors, driver
	 * does have to read Rx descriptors back so BUS_DMASYNC_PREWRITE
	 * is enough to ensure coherence.
	 */
	bus_dmamap_sync(sc->alc_cdata.alc_rx_ring_tag,
	    sc->alc_cdata.alc_rx_ring_map, BUS_DMASYNC_PREWRITE);
	/* Let controller know availability of new Rx buffers. */
	CSR_WRITE_4(sc, ALC_MBOX_RD0_PROD_IDX, sc->alc_cdata.alc_rx_cons);

	return (0);
}

static void
alc_init_rr_ring(struct alc_softc *sc)
{
	struct alc_ring_data *rd;

	ALC_LOCK_ASSERT(sc);

	sc->alc_cdata.alc_rr_cons = 0;
	ALC_RXCHAIN_RESET(sc);

	rd = &sc->alc_rdata;
	bzero(rd->alc_rr_ring, ALC_RR_RING_SZ);
	bus_dmamap_sync(sc->alc_cdata.alc_rr_ring_tag,
	    sc->alc_cdata.alc_rr_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
alc_init_cmb(struct alc_softc *sc)
{
	struct alc_ring_data *rd;

	ALC_LOCK_ASSERT(sc);

	rd = &sc->alc_rdata;
	bzero(rd->alc_cmb, ALC_CMB_SZ);
	bus_dmamap_sync(sc->alc_cdata.alc_cmb_tag, sc->alc_cdata.alc_cmb_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
alc_init_smb(struct alc_softc *sc)
{
	struct alc_ring_data *rd;

	ALC_LOCK_ASSERT(sc);

	rd = &sc->alc_rdata;
	bzero(rd->alc_smb, ALC_SMB_SZ);
	bus_dmamap_sync(sc->alc_cdata.alc_smb_tag, sc->alc_cdata.alc_smb_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static void
alc_rxvlan(struct alc_softc *sc)
{
	struct ifnet *ifp;
	uint32_t reg;

	ALC_LOCK_ASSERT(sc);

	ifp = sc->alc_ifp;
	reg = CSR_READ_4(sc, ALC_MAC_CFG);
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
		reg |= MAC_CFG_VLAN_TAG_STRIP;
	else
		reg &= ~MAC_CFG_VLAN_TAG_STRIP;
	CSR_WRITE_4(sc, ALC_MAC_CFG, reg);
}

static void
alc_rxfilter(struct alc_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t crc;
	uint32_t mchash[2];
	uint32_t rxcfg;

	ALC_LOCK_ASSERT(sc);

	ifp = sc->alc_ifp;

	bzero(mchash, sizeof(mchash));
	rxcfg = CSR_READ_4(sc, ALC_MAC_CFG);
	rxcfg &= ~(MAC_CFG_ALLMULTI | MAC_CFG_BCAST | MAC_CFG_PROMISC);
	if ((ifp->if_flags & IFF_BROADCAST) != 0)
		rxcfg |= MAC_CFG_BCAST;
	if ((ifp->if_flags & (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
		if ((ifp->if_flags & IFF_PROMISC) != 0)
			rxcfg |= MAC_CFG_PROMISC;
		if ((ifp->if_flags & IFF_ALLMULTI) != 0)
			rxcfg |= MAC_CFG_ALLMULTI;
		mchash[0] = 0xFFFFFFFF;
		mchash[1] = 0xFFFFFFFF;
		goto chipit;
	}

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &sc->alc_ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN);
		mchash[crc >> 31] |= 1 << ((crc >> 26) & 0x1f);
	}
	if_maddr_runlock(ifp);

chipit:
	CSR_WRITE_4(sc, ALC_MAR0, mchash[0]);
	CSR_WRITE_4(sc, ALC_MAR1, mchash[1]);
	CSR_WRITE_4(sc, ALC_MAC_CFG, rxcfg);
}

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (arg1 == NULL)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;

	return (0);
}

static int
sysctl_hw_alc_proc_limit(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_int_range(oidp, arg1, arg2, req,
	    ALC_PROC_MIN, ALC_PROC_MAX));
}

static int
sysctl_hw_alc_int_mod(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req,
	    ALC_IM_TIMER_MIN, ALC_IM_TIMER_MAX));
}

#ifdef NETDUMP
static void
alc_netdump_init(struct ifnet *ifp, int *nrxr, int *ncl, int *clsize)
{
	struct alc_softc *sc;

	sc = if_getsoftc(ifp);
	KASSERT(sc->alc_buf_size <= MCLBYTES, ("incorrect cluster size"));

	*nrxr = ALC_RX_RING_CNT;
	*ncl = NETDUMP_MAX_IN_FLIGHT;
	*clsize = MCLBYTES;
}

static void
alc_netdump_event(struct ifnet *ifp __unused, enum netdump_ev event __unused)
{
}

static int
alc_netdump_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct alc_softc *sc;
	int error;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (EBUSY);

	error = alc_encap(sc, &m);
	if (error == 0)
		alc_start_tx(sc);
	return (error);
}

static int
alc_netdump_poll(struct ifnet *ifp, int count)
{
	struct alc_softc *sc;

	sc = if_getsoftc(ifp);
	if ((if_getdrvflags(ifp) & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING)
		return (EBUSY);

	alc_txeof(sc);
	return (alc_rxintr(sc, count));
}
#endif /* NETDUMP */
