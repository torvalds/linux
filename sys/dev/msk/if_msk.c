/******************************************************************************
 *
 * Name   : sky2.c
 * Project: Gigabit Ethernet Driver for FreeBSD 5.x/6.x
 * Version: $Revision: 1.23 $
 * Date   : $Date: 2005/12/22 09:04:11 $
 * Purpose: Main driver source file
 *
 *****************************************************************************/

/******************************************************************************
 *
 *	LICENSE:
 *	Copyright (C) Marvell International Ltd. and/or its affiliates
 *
 *	The computer program files contained in this folder ("Files")
 *	are provided to you under the BSD-type license terms provided
 *	below, and any use of such Files and any derivative works
 *	thereof created by you shall be governed by the following terms
 *	and conditions:
 *
 *	- Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials provided
 *	  with the distribution.
 *	- Neither the name of Marvell nor the names of its contributors
 *	  may be used to endorse or promote products derived from this
 *	  software without specific prior written permission.
 *
 *	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *	"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *	FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *	COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *	BUT NOT LIMITED TO, PROCUREMENT OF  SUBSTITUTE GOODS OR SERVICES;
 *	LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *	HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 *	STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 *	OF THE POSSIBILITY OF SUCH DAMAGE.
 *	/LICENSE
 *
 *****************************************************************************/

/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-3-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000
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
/*-
 * Copyright (c) 2003 Nathan L. Binkert <binkertn@umich.edu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Device driver for the Marvell Yukon II Ethernet controller.
 * Due to lack of documentation, this driver is based on the code from
 * sk(4) and Marvell's myk(4) driver for FreeBSD 5.x.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

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
#include <machine/in_cksum.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/msk/if_mskreg.h>

MODULE_DEPEND(msk, pci, 1, 1, 1);
MODULE_DEPEND(msk, ether, 1, 1, 1);
MODULE_DEPEND(msk, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/* Tunables. */
static int msi_disable = 0;
TUNABLE_INT("hw.msk.msi_disable", &msi_disable);
static int legacy_intr = 0;
TUNABLE_INT("hw.msk.legacy_intr", &legacy_intr);
static int jumbo_disable = 0;
TUNABLE_INT("hw.msk.jumbo_disable", &jumbo_disable);

#define MSK_CSUM_FEATURES	(CSUM_TCP | CSUM_UDP)

/*
 * Devices supported by this driver.
 */
static const struct msk_product {
	uint16_t	msk_vendorid;
	uint16_t	msk_deviceid;
	const char	*msk_name;
} msk_products[] = {
	{ VENDORID_SK, DEVICEID_SK_YUKON2,
	    "SK-9Sxx Gigabit Ethernet" },
	{ VENDORID_SK, DEVICEID_SK_YUKON2_EXPR,
	    "SK-9Exx Gigabit Ethernet"},
	{ VENDORID_MARVELL, DEVICEID_MRVL_8021CU,
	    "Marvell Yukon 88E8021CU Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8021X,
	    "Marvell Yukon 88E8021 SX/LX Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8022CU,
	    "Marvell Yukon 88E8022CU Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8022X,
	    "Marvell Yukon 88E8022 SX/LX Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8061CU,
	    "Marvell Yukon 88E8061CU Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8061X,
	    "Marvell Yukon 88E8061 SX/LX Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8062CU,
	    "Marvell Yukon 88E8062CU Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8062X,
	    "Marvell Yukon 88E8062 SX/LX Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8035,
	    "Marvell Yukon 88E8035 Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8036,
	    "Marvell Yukon 88E8036 Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8038,
	    "Marvell Yukon 88E8038 Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8039,
	    "Marvell Yukon 88E8039 Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8040,
	    "Marvell Yukon 88E8040 Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8040T,
	    "Marvell Yukon 88E8040T Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8042,
	    "Marvell Yukon 88E8042 Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_8048,
	    "Marvell Yukon 88E8048 Fast Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4361,
	    "Marvell Yukon 88E8050 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4360,
	    "Marvell Yukon 88E8052 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4362,
	    "Marvell Yukon 88E8053 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4363,
	    "Marvell Yukon 88E8055 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4364,
	    "Marvell Yukon 88E8056 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4365,
	    "Marvell Yukon 88E8070 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_436A,
	    "Marvell Yukon 88E8058 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_436B,
	    "Marvell Yukon 88E8071 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_436C,
	    "Marvell Yukon 88E8072 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_436D,
	    "Marvell Yukon 88E8055 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4370,
	    "Marvell Yukon 88E8075 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4380,
	    "Marvell Yukon 88E8057 Gigabit Ethernet" },
	{ VENDORID_MARVELL, DEVICEID_MRVL_4381,
	    "Marvell Yukon 88E8059 Gigabit Ethernet" },
	{ VENDORID_DLINK, DEVICEID_DLINK_DGE550SX,
	    "D-Link 550SX Gigabit Ethernet" },
	{ VENDORID_DLINK, DEVICEID_DLINK_DGE560SX,
	    "D-Link 560SX Gigabit Ethernet" },
	{ VENDORID_DLINK, DEVICEID_DLINK_DGE560T,
	    "D-Link 560T Gigabit Ethernet" }
};

static const char *model_name[] = {
	"Yukon XL",
        "Yukon EC Ultra",
        "Yukon EX",
        "Yukon EC",
        "Yukon FE",
        "Yukon FE+",
        "Yukon Supreme",
        "Yukon Ultra 2",
        "Yukon Unknown",
        "Yukon Optima",
};

static int mskc_probe(device_t);
static int mskc_attach(device_t);
static int mskc_detach(device_t);
static int mskc_shutdown(device_t);
static int mskc_setup_rambuffer(struct msk_softc *);
static int mskc_suspend(device_t);
static int mskc_resume(device_t);
static bus_dma_tag_t mskc_get_dma_tag(device_t, device_t);
static void mskc_reset(struct msk_softc *);

static int msk_probe(device_t);
static int msk_attach(device_t);
static int msk_detach(device_t);

static void msk_tick(void *);
static void msk_intr(void *);
static void msk_intr_phy(struct msk_if_softc *);
static void msk_intr_gmac(struct msk_if_softc *);
static __inline void msk_rxput(struct msk_if_softc *);
static int msk_handle_events(struct msk_softc *);
static void msk_handle_hwerr(struct msk_if_softc *, uint32_t);
static void msk_intr_hwerr(struct msk_softc *);
#ifndef __NO_STRICT_ALIGNMENT
static __inline void msk_fixup_rx(struct mbuf *);
#endif
static __inline void msk_rxcsum(struct msk_if_softc *, uint32_t, struct mbuf *);
static void msk_rxeof(struct msk_if_softc *, uint32_t, uint32_t, int);
static void msk_jumbo_rxeof(struct msk_if_softc *, uint32_t, uint32_t, int);
static void msk_txeof(struct msk_if_softc *, int);
static int msk_encap(struct msk_if_softc *, struct mbuf **);
static void msk_start(struct ifnet *);
static void msk_start_locked(struct ifnet *);
static int msk_ioctl(struct ifnet *, u_long, caddr_t);
static void msk_set_prefetch(struct msk_softc *, int, bus_addr_t, uint32_t);
static void msk_set_rambuffer(struct msk_if_softc *);
static void msk_set_tx_stfwd(struct msk_if_softc *);
static void msk_init(void *);
static void msk_init_locked(struct msk_if_softc *);
static void msk_stop(struct msk_if_softc *);
static void msk_watchdog(struct msk_if_softc *);
static int msk_mediachange(struct ifnet *);
static void msk_mediastatus(struct ifnet *, struct ifmediareq *);
static void msk_phy_power(struct msk_softc *, int);
static void msk_dmamap_cb(void *, bus_dma_segment_t *, int, int);
static int msk_status_dma_alloc(struct msk_softc *);
static void msk_status_dma_free(struct msk_softc *);
static int msk_txrx_dma_alloc(struct msk_if_softc *);
static int msk_rx_dma_jalloc(struct msk_if_softc *);
static void msk_txrx_dma_free(struct msk_if_softc *);
static void msk_rx_dma_jfree(struct msk_if_softc *);
static int msk_rx_fill(struct msk_if_softc *, int);
static int msk_init_rx_ring(struct msk_if_softc *);
static int msk_init_jumbo_rx_ring(struct msk_if_softc *);
static void msk_init_tx_ring(struct msk_if_softc *);
static __inline void msk_discard_rxbuf(struct msk_if_softc *, int);
static __inline void msk_discard_jumbo_rxbuf(struct msk_if_softc *, int);
static int msk_newbuf(struct msk_if_softc *, int);
static int msk_jumbo_newbuf(struct msk_if_softc *, int);

static int msk_phy_readreg(struct msk_if_softc *, int, int);
static int msk_phy_writereg(struct msk_if_softc *, int, int, int);
static int msk_miibus_readreg(device_t, int, int);
static int msk_miibus_writereg(device_t, int, int, int);
static void msk_miibus_statchg(device_t);

static void msk_rxfilter(struct msk_if_softc *);
static void msk_setvlan(struct msk_if_softc *, struct ifnet *);

static void msk_stats_clear(struct msk_if_softc *);
static void msk_stats_update(struct msk_if_softc *);
static int msk_sysctl_stat32(SYSCTL_HANDLER_ARGS);
static int msk_sysctl_stat64(SYSCTL_HANDLER_ARGS);
static void msk_sysctl_node(struct msk_if_softc *);
static int sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int sysctl_hw_msk_proc_limit(SYSCTL_HANDLER_ARGS);

static device_method_t mskc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mskc_probe),
	DEVMETHOD(device_attach,	mskc_attach),
	DEVMETHOD(device_detach,	mskc_detach),
	DEVMETHOD(device_suspend,	mskc_suspend),
	DEVMETHOD(device_resume,	mskc_resume),
	DEVMETHOD(device_shutdown,	mskc_shutdown),

	DEVMETHOD(bus_get_dma_tag,	mskc_get_dma_tag),

	DEVMETHOD_END
};

static driver_t mskc_driver = {
	"mskc",
	mskc_methods,
	sizeof(struct msk_softc)
};

static devclass_t mskc_devclass;

static device_method_t msk_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		msk_probe),
	DEVMETHOD(device_attach,	msk_attach),
	DEVMETHOD(device_detach,	msk_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	msk_miibus_readreg),
	DEVMETHOD(miibus_writereg,	msk_miibus_writereg),
	DEVMETHOD(miibus_statchg,	msk_miibus_statchg),

	DEVMETHOD_END
};

static driver_t msk_driver = {
	"msk",
	msk_methods,
	sizeof(struct msk_if_softc)
};

static devclass_t msk_devclass;

DRIVER_MODULE(mskc, pci, mskc_driver, mskc_devclass, NULL, NULL);
DRIVER_MODULE(msk, mskc, msk_driver, msk_devclass, NULL, NULL);
DRIVER_MODULE(miibus, msk, miibus_driver, miibus_devclass, NULL, NULL);

static struct resource_spec msk_res_spec_io[] = {
	{ SYS_RES_IOPORT,	PCIR_BAR(1),	RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec msk_res_spec_mem[] = {
	{ SYS_RES_MEMORY,	PCIR_BAR(0),	RF_ACTIVE },
	{ -1,			0,		0 }
};

static struct resource_spec msk_irq_spec_legacy[] = {
	{ SYS_RES_IRQ,		0,		RF_ACTIVE | RF_SHAREABLE },
	{ -1,			0,		0 }
};

static struct resource_spec msk_irq_spec_msi[] = {
	{ SYS_RES_IRQ,		1,		RF_ACTIVE },
	{ -1,			0,		0 }
};

static int
msk_miibus_readreg(device_t dev, int phy, int reg)
{
	struct msk_if_softc *sc_if;

	sc_if = device_get_softc(dev);

	return (msk_phy_readreg(sc_if, phy, reg));
}

static int
msk_phy_readreg(struct msk_if_softc *sc_if, int phy, int reg)
{
	struct msk_softc *sc;
	int i, val;

	sc = sc_if->msk_softc;

        GMAC_WRITE_2(sc, sc_if->msk_port, GM_SMI_CTRL,
	    GM_SMI_CT_PHY_AD(phy) | GM_SMI_CT_REG_AD(reg) | GM_SMI_CT_OP_RD);

	for (i = 0; i < MSK_TIMEOUT; i++) {
		DELAY(1);
		val = GMAC_READ_2(sc, sc_if->msk_port, GM_SMI_CTRL);
		if ((val & GM_SMI_CT_RD_VAL) != 0) {
			val = GMAC_READ_2(sc, sc_if->msk_port, GM_SMI_DATA);
			break;
		}
	}

	if (i == MSK_TIMEOUT) {
		if_printf(sc_if->msk_ifp, "phy failed to come ready\n");
		val = 0;
	}

	return (val);
}

static int
msk_miibus_writereg(device_t dev, int phy, int reg, int val)
{
	struct msk_if_softc *sc_if;

	sc_if = device_get_softc(dev);

	return (msk_phy_writereg(sc_if, phy, reg, val));
}

static int
msk_phy_writereg(struct msk_if_softc *sc_if, int phy, int reg, int val)
{
	struct msk_softc *sc;
	int i;

	sc = sc_if->msk_softc;

	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SMI_DATA, val);
        GMAC_WRITE_2(sc, sc_if->msk_port, GM_SMI_CTRL,
	    GM_SMI_CT_PHY_AD(phy) | GM_SMI_CT_REG_AD(reg));
	for (i = 0; i < MSK_TIMEOUT; i++) {
		DELAY(1);
		if ((GMAC_READ_2(sc, sc_if->msk_port, GM_SMI_CTRL) &
		    GM_SMI_CT_BUSY) == 0)
			break;
	}
	if (i == MSK_TIMEOUT)
		if_printf(sc_if->msk_ifp, "phy write timeout\n");

	return (0);
}

static void
msk_miibus_statchg(device_t dev)
{
	struct msk_softc *sc;
	struct msk_if_softc *sc_if;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t gmac;

	sc_if = device_get_softc(dev);
	sc = sc_if->msk_softc;

	MSK_IF_LOCK_ASSERT(sc_if);

	mii = device_get_softc(sc_if->msk_miibus);
	ifp = sc_if->msk_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc_if->msk_flags &= ~MSK_FLAG_LINK;
	if ((mii->mii_media_status & (IFM_AVALID | IFM_ACTIVE)) ==
	    (IFM_AVALID | IFM_ACTIVE)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc_if->msk_flags |= MSK_FLAG_LINK;
			break;
		case IFM_1000_T:
		case IFM_1000_SX:
		case IFM_1000_LX:
		case IFM_1000_CX:
			if ((sc_if->msk_flags & MSK_FLAG_FASTETHER) == 0)
				sc_if->msk_flags |= MSK_FLAG_LINK;
			break;
		default:
			break;
		}
	}

	if ((sc_if->msk_flags & MSK_FLAG_LINK) != 0) {
		/* Enable Tx FIFO Underrun. */
		CSR_WRITE_1(sc, MR_ADDR(sc_if->msk_port, GMAC_IRQ_MSK),
		    GM_IS_TX_FF_UR | GM_IS_RX_FF_OR);
		/*
		 * Because mii(4) notify msk(4) that it detected link status
		 * change, there is no need to enable automatic
		 * speed/flow-control/duplex updates.
		 */
		gmac = GM_GPCR_AU_ALL_DIS;
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_1000_SX:
		case IFM_1000_T:
			gmac |= GM_GPCR_SPEED_1000;
			break;
		case IFM_100_TX:
			gmac |= GM_GPCR_SPEED_100;
			break;
		case IFM_10_T:
			break;
		}

		if ((IFM_OPTIONS(mii->mii_media_active) &
		    IFM_ETH_RXPAUSE) == 0)
			gmac |= GM_GPCR_FC_RX_DIS;
		if ((IFM_OPTIONS(mii->mii_media_active) &
		     IFM_ETH_TXPAUSE) == 0)
			gmac |= GM_GPCR_FC_TX_DIS;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0)
			gmac |= GM_GPCR_DUP_FULL;
		else
			gmac |= GM_GPCR_FC_RX_DIS | GM_GPCR_FC_TX_DIS;
		gmac |= GM_GPCR_RX_ENA | GM_GPCR_TX_ENA;
		GMAC_WRITE_2(sc, sc_if->msk_port, GM_GP_CTRL, gmac);
		/* Read again to ensure writing. */
		GMAC_READ_2(sc, sc_if->msk_port, GM_GP_CTRL);
		gmac = GMC_PAUSE_OFF;
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
			if ((IFM_OPTIONS(mii->mii_media_active) &
			    IFM_ETH_RXPAUSE) != 0)
				gmac = GMC_PAUSE_ON;
		}
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, GMAC_CTRL), gmac);

		/* Enable PHY interrupt for FIFO underrun/overflow. */
		msk_phy_writereg(sc_if, PHY_ADDR_MARV,
		    PHY_MARV_INT_MASK, PHY_M_IS_FIFO_ERROR);
	} else {
		/*
		 * Link state changed to down.
		 * Disable PHY interrupts.
		 */
		msk_phy_writereg(sc_if, PHY_ADDR_MARV, PHY_MARV_INT_MASK, 0);
		/* Disable Rx/Tx MAC. */
		gmac = GMAC_READ_2(sc, sc_if->msk_port, GM_GP_CTRL);
		if ((gmac & (GM_GPCR_RX_ENA | GM_GPCR_TX_ENA)) != 0) {
			gmac &= ~(GM_GPCR_RX_ENA | GM_GPCR_TX_ENA);
			GMAC_WRITE_2(sc, sc_if->msk_port, GM_GP_CTRL, gmac);
			/* Read again to ensure writing. */
			GMAC_READ_2(sc, sc_if->msk_port, GM_GP_CTRL);
		}
	}
}

static void
msk_rxfilter(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t mchash[2];
	uint32_t crc;
	uint16_t mode;

	sc = sc_if->msk_softc;

	MSK_IF_LOCK_ASSERT(sc_if);

	ifp = sc_if->msk_ifp;

	bzero(mchash, sizeof(mchash));
	mode = GMAC_READ_2(sc, sc_if->msk_port, GM_RX_CTRL);
	if ((ifp->if_flags & IFF_PROMISC) != 0)
		mode &= ~(GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
	else if ((ifp->if_flags & IFF_ALLMULTI) != 0) {
		mode |= GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA;
		mchash[0] = 0xffff;
		mchash[1] = 0xffff;
	} else {
		mode |= GM_RXCR_UCF_ENA;
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			crc = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN);
			/* Just want the 6 least significant bits. */
			crc &= 0x3f;
			/* Set the corresponding bit in the hash table. */
			mchash[crc >> 5] |= 1 << (crc & 0x1f);
		}
		if_maddr_runlock(ifp);
		if (mchash[0] != 0 || mchash[1] != 0)
			mode |= GM_RXCR_MCF_ENA;
	}

	GMAC_WRITE_2(sc, sc_if->msk_port, GM_MC_ADDR_H1,
	    mchash[0] & 0xffff);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_MC_ADDR_H2,
	    (mchash[0] >> 16) & 0xffff);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_MC_ADDR_H3,
	    mchash[1] & 0xffff);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_MC_ADDR_H4,
	    (mchash[1] >> 16) & 0xffff);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_RX_CTRL, mode);
}

static void
msk_setvlan(struct msk_if_softc *sc_if, struct ifnet *ifp)
{
	struct msk_softc *sc;

	sc = sc_if->msk_softc;
	if ((ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0) {
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T),
		    RX_VLAN_STRIP_ON);
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T),
		    TX_VLAN_TAG_ON);
	} else {
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T),
		    RX_VLAN_STRIP_OFF);
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T),
		    TX_VLAN_TAG_OFF);
	}
}

static int
msk_rx_fill(struct msk_if_softc *sc_if, int jumbo)
{
	uint16_t idx;
	int i;

	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0 &&
	    (sc_if->msk_ifp->if_capenable & IFCAP_RXCSUM) != 0) {
		/* Wait until controller executes OP_TCPSTART command. */
		for (i = 100; i > 0; i--) {
			DELAY(100);
			idx = CSR_READ_2(sc_if->msk_softc,
			    Y2_PREF_Q_ADDR(sc_if->msk_rxq,
			    PREF_UNIT_GET_IDX_REG));
			if (idx != 0)
				break;
		}
		if (i == 0) {
			device_printf(sc_if->msk_if_dev,
			    "prefetch unit stuck?\n");
			return (ETIMEDOUT);
		}
		/*
		 * Fill consumed LE with free buffer. This can be done
		 * in Rx handler but we don't want to add special code
		 * in fast handler.
		 */
		if (jumbo > 0) {
			if (msk_jumbo_newbuf(sc_if, 0) != 0)
				return (ENOBUFS);
			bus_dmamap_sync(sc_if->msk_cdata.msk_jumbo_rx_ring_tag,
			    sc_if->msk_cdata.msk_jumbo_rx_ring_map,
			    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		} else {
			if (msk_newbuf(sc_if, 0) != 0)
				return (ENOBUFS);
			bus_dmamap_sync(sc_if->msk_cdata.msk_rx_ring_tag,
			    sc_if->msk_cdata.msk_rx_ring_map,
			    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		}
		sc_if->msk_cdata.msk_rx_prod = 0;
		CSR_WRITE_2(sc_if->msk_softc,
		    Y2_PREF_Q_ADDR(sc_if->msk_rxq, PREF_UNIT_PUT_IDX_REG),
		    sc_if->msk_cdata.msk_rx_prod);
	}
	return (0);
}

static int
msk_init_rx_ring(struct msk_if_softc *sc_if)
{
	struct msk_ring_data *rd;
	struct msk_rxdesc *rxd;
	int i, nbuf, prod;

	MSK_IF_LOCK_ASSERT(sc_if);

	sc_if->msk_cdata.msk_rx_cons = 0;
	sc_if->msk_cdata.msk_rx_prod = 0;
	sc_if->msk_cdata.msk_rx_putwm = MSK_PUT_WM;

	rd = &sc_if->msk_rdata;
	bzero(rd->msk_rx_ring, sizeof(struct msk_rx_desc) * MSK_RX_RING_CNT);
	for (i = prod = 0; i < MSK_RX_RING_CNT; i++) {
		rxd = &sc_if->msk_cdata.msk_rxdesc[prod];
		rxd->rx_m = NULL;
		rxd->rx_le = &rd->msk_rx_ring[prod];
		MSK_INC(prod, MSK_RX_RING_CNT);
	}
	nbuf = MSK_RX_BUF_CNT;
	prod = 0;
	/* Have controller know how to compute Rx checksum. */
	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0 &&
	    (sc_if->msk_ifp->if_capenable & IFCAP_RXCSUM) != 0) {
#ifdef MSK_64BIT_DMA
		rxd = &sc_if->msk_cdata.msk_rxdesc[prod];
		rxd->rx_m = NULL;
		rxd->rx_le = &rd->msk_rx_ring[prod];
		rxd->rx_le->msk_addr = htole32(ETHER_HDR_LEN << 16 |
		    ETHER_HDR_LEN);
		rxd->rx_le->msk_control = htole32(OP_TCPSTART | HW_OWNER);
		MSK_INC(prod, MSK_RX_RING_CNT);
		MSK_INC(sc_if->msk_cdata.msk_rx_cons, MSK_RX_RING_CNT);
#endif
		rxd = &sc_if->msk_cdata.msk_rxdesc[prod];
		rxd->rx_m = NULL;
		rxd->rx_le = &rd->msk_rx_ring[prod];
		rxd->rx_le->msk_addr = htole32(ETHER_HDR_LEN << 16 |
		    ETHER_HDR_LEN);
		rxd->rx_le->msk_control = htole32(OP_TCPSTART | HW_OWNER);
		MSK_INC(prod, MSK_RX_RING_CNT);
		MSK_INC(sc_if->msk_cdata.msk_rx_cons, MSK_RX_RING_CNT);
		nbuf--;
	}
	for (i = 0; i < nbuf; i++) {
		if (msk_newbuf(sc_if, prod) != 0)
			return (ENOBUFS);
		MSK_RX_INC(prod, MSK_RX_RING_CNT);
	}

	bus_dmamap_sync(sc_if->msk_cdata.msk_rx_ring_tag,
	    sc_if->msk_cdata.msk_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Update prefetch unit. */
	sc_if->msk_cdata.msk_rx_prod = prod;
	CSR_WRITE_2(sc_if->msk_softc,
	    Y2_PREF_Q_ADDR(sc_if->msk_rxq, PREF_UNIT_PUT_IDX_REG),
	    (sc_if->msk_cdata.msk_rx_prod + MSK_RX_RING_CNT - 1) %
	    MSK_RX_RING_CNT);
	if (msk_rx_fill(sc_if, 0) != 0)
		return (ENOBUFS);
	return (0);
}

static int
msk_init_jumbo_rx_ring(struct msk_if_softc *sc_if)
{
	struct msk_ring_data *rd;
	struct msk_rxdesc *rxd;
	int i, nbuf, prod;

	MSK_IF_LOCK_ASSERT(sc_if);

	sc_if->msk_cdata.msk_rx_cons = 0;
	sc_if->msk_cdata.msk_rx_prod = 0;
	sc_if->msk_cdata.msk_rx_putwm = MSK_PUT_WM;

	rd = &sc_if->msk_rdata;
	bzero(rd->msk_jumbo_rx_ring,
	    sizeof(struct msk_rx_desc) * MSK_JUMBO_RX_RING_CNT);
	for (i = prod = 0; i < MSK_JUMBO_RX_RING_CNT; i++) {
		rxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[prod];
		rxd->rx_m = NULL;
		rxd->rx_le = &rd->msk_jumbo_rx_ring[prod];
		MSK_INC(prod, MSK_JUMBO_RX_RING_CNT);
	}
	nbuf = MSK_RX_BUF_CNT;
	prod = 0;
	/* Have controller know how to compute Rx checksum. */
	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0 &&
	    (sc_if->msk_ifp->if_capenable & IFCAP_RXCSUM) != 0) {
#ifdef MSK_64BIT_DMA
		rxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[prod];
		rxd->rx_m = NULL;
		rxd->rx_le = &rd->msk_jumbo_rx_ring[prod];
		rxd->rx_le->msk_addr = htole32(ETHER_HDR_LEN << 16 |
		    ETHER_HDR_LEN);
		rxd->rx_le->msk_control = htole32(OP_TCPSTART | HW_OWNER);
		MSK_INC(prod, MSK_JUMBO_RX_RING_CNT);
		MSK_INC(sc_if->msk_cdata.msk_rx_cons, MSK_JUMBO_RX_RING_CNT);
#endif
		rxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[prod];
		rxd->rx_m = NULL;
		rxd->rx_le = &rd->msk_jumbo_rx_ring[prod];
		rxd->rx_le->msk_addr = htole32(ETHER_HDR_LEN << 16 |
		    ETHER_HDR_LEN);
		rxd->rx_le->msk_control = htole32(OP_TCPSTART | HW_OWNER);
		MSK_INC(prod, MSK_JUMBO_RX_RING_CNT);
		MSK_INC(sc_if->msk_cdata.msk_rx_cons, MSK_JUMBO_RX_RING_CNT);
		nbuf--;
	}
	for (i = 0; i < nbuf; i++) {
		if (msk_jumbo_newbuf(sc_if, prod) != 0)
			return (ENOBUFS);
		MSK_RX_INC(prod, MSK_JUMBO_RX_RING_CNT);
	}

	bus_dmamap_sync(sc_if->msk_cdata.msk_jumbo_rx_ring_tag,
	    sc_if->msk_cdata.msk_jumbo_rx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	/* Update prefetch unit. */
	sc_if->msk_cdata.msk_rx_prod = prod;
	CSR_WRITE_2(sc_if->msk_softc,
	    Y2_PREF_Q_ADDR(sc_if->msk_rxq, PREF_UNIT_PUT_IDX_REG),
	    (sc_if->msk_cdata.msk_rx_prod + MSK_JUMBO_RX_RING_CNT - 1) %
	    MSK_JUMBO_RX_RING_CNT);
	if (msk_rx_fill(sc_if, 1) != 0)
		return (ENOBUFS);
	return (0);
}

static void
msk_init_tx_ring(struct msk_if_softc *sc_if)
{
	struct msk_ring_data *rd;
	struct msk_txdesc *txd;
	int i;

	sc_if->msk_cdata.msk_tso_mtu = 0;
	sc_if->msk_cdata.msk_last_csum = 0;
	sc_if->msk_cdata.msk_tx_prod = 0;
	sc_if->msk_cdata.msk_tx_cons = 0;
	sc_if->msk_cdata.msk_tx_cnt = 0;
	sc_if->msk_cdata.msk_tx_high_addr = 0;

	rd = &sc_if->msk_rdata;
	bzero(rd->msk_tx_ring, sizeof(struct msk_tx_desc) * MSK_TX_RING_CNT);
	for (i = 0; i < MSK_TX_RING_CNT; i++) {
		txd = &sc_if->msk_cdata.msk_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_le = &rd->msk_tx_ring[i];
	}

	bus_dmamap_sync(sc_if->msk_cdata.msk_tx_ring_tag,
	    sc_if->msk_cdata.msk_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

static __inline void
msk_discard_rxbuf(struct msk_if_softc *sc_if, int idx)
{
	struct msk_rx_desc *rx_le;
	struct msk_rxdesc *rxd;
	struct mbuf *m;

#ifdef MSK_64BIT_DMA
	rxd = &sc_if->msk_cdata.msk_rxdesc[idx];
	rx_le = rxd->rx_le;
	rx_le->msk_control = htole32(OP_ADDR64 | HW_OWNER);
	MSK_INC(idx, MSK_RX_RING_CNT);
#endif
	rxd = &sc_if->msk_cdata.msk_rxdesc[idx];
	m = rxd->rx_m;
	rx_le = rxd->rx_le;
	rx_le->msk_control = htole32(m->m_len | OP_PACKET | HW_OWNER);
}

static __inline void
msk_discard_jumbo_rxbuf(struct msk_if_softc *sc_if, int	idx)
{
	struct msk_rx_desc *rx_le;
	struct msk_rxdesc *rxd;
	struct mbuf *m;

#ifdef MSK_64BIT_DMA
	rxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[idx];
	rx_le = rxd->rx_le;
	rx_le->msk_control = htole32(OP_ADDR64 | HW_OWNER);
	MSK_INC(idx, MSK_JUMBO_RX_RING_CNT);
#endif
	rxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[idx];
	m = rxd->rx_m;
	rx_le = rxd->rx_le;
	rx_le->msk_control = htole32(m->m_len | OP_PACKET | HW_OWNER);
}

static int
msk_newbuf(struct msk_if_softc *sc_if, int idx)
{
	struct msk_rx_desc *rx_le;
	struct msk_rxdesc *rxd;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m == NULL)
		return (ENOBUFS);

	m->m_len = m->m_pkthdr.len = MCLBYTES;
	if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) == 0)
		m_adj(m, ETHER_ALIGN);
#ifndef __NO_STRICT_ALIGNMENT
	else
		m_adj(m, MSK_RX_BUF_ALIGN);
#endif

	if (bus_dmamap_load_mbuf_sg(sc_if->msk_cdata.msk_rx_tag,
	    sc_if->msk_cdata.msk_rx_sparemap, m, segs, &nsegs,
	    BUS_DMA_NOWAIT) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc_if->msk_cdata.msk_rxdesc[idx];
#ifdef MSK_64BIT_DMA
	rx_le = rxd->rx_le;
	rx_le->msk_addr = htole32(MSK_ADDR_HI(segs[0].ds_addr));
	rx_le->msk_control = htole32(OP_ADDR64 | HW_OWNER);
	MSK_INC(idx, MSK_RX_RING_CNT);
	rxd = &sc_if->msk_cdata.msk_rxdesc[idx];
#endif
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc_if->msk_cdata.msk_rx_tag, rxd->rx_dmamap,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc_if->msk_cdata.msk_rx_tag, rxd->rx_dmamap);
		rxd->rx_m = NULL;
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc_if->msk_cdata.msk_rx_sparemap;
	sc_if->msk_cdata.msk_rx_sparemap = map;
	bus_dmamap_sync(sc_if->msk_cdata.msk_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	rx_le = rxd->rx_le;
	rx_le->msk_addr = htole32(MSK_ADDR_LO(segs[0].ds_addr));
	rx_le->msk_control =
	    htole32(segs[0].ds_len | OP_PACKET | HW_OWNER);

	return (0);
}

static int
msk_jumbo_newbuf(struct msk_if_softc *sc_if, int idx)
{
	struct msk_rx_desc *rx_le;
	struct msk_rxdesc *rxd;
	struct mbuf *m;
	bus_dma_segment_t segs[1];
	bus_dmamap_t map;
	int nsegs;

	m = m_getjcl(M_NOWAIT, MT_DATA, M_PKTHDR, MJUM9BYTES);
	if (m == NULL)
		return (ENOBUFS);
	m->m_len = m->m_pkthdr.len = MJUM9BYTES;
	if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) == 0)
		m_adj(m, ETHER_ALIGN);
#ifndef __NO_STRICT_ALIGNMENT
	else
		m_adj(m, MSK_RX_BUF_ALIGN);
#endif

	if (bus_dmamap_load_mbuf_sg(sc_if->msk_cdata.msk_jumbo_rx_tag,
	    sc_if->msk_cdata.msk_jumbo_rx_sparemap, m, segs, &nsegs,
	    BUS_DMA_NOWAIT) != 0) {
		m_freem(m);
		return (ENOBUFS);
	}
	KASSERT(nsegs == 1, ("%s: %d segments returned!", __func__, nsegs));

	rxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[idx];
#ifdef MSK_64BIT_DMA
	rx_le = rxd->rx_le;
	rx_le->msk_addr = htole32(MSK_ADDR_HI(segs[0].ds_addr));
	rx_le->msk_control = htole32(OP_ADDR64 | HW_OWNER);
	MSK_INC(idx, MSK_JUMBO_RX_RING_CNT);
	rxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[idx];
#endif
	if (rxd->rx_m != NULL) {
		bus_dmamap_sync(sc_if->msk_cdata.msk_jumbo_rx_tag,
		    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc_if->msk_cdata.msk_jumbo_rx_tag,
		    rxd->rx_dmamap);
		rxd->rx_m = NULL;
	}
	map = rxd->rx_dmamap;
	rxd->rx_dmamap = sc_if->msk_cdata.msk_jumbo_rx_sparemap;
	sc_if->msk_cdata.msk_jumbo_rx_sparemap = map;
	bus_dmamap_sync(sc_if->msk_cdata.msk_jumbo_rx_tag, rxd->rx_dmamap,
	    BUS_DMASYNC_PREREAD);
	rxd->rx_m = m;
	rx_le = rxd->rx_le;
	rx_le->msk_addr = htole32(MSK_ADDR_LO(segs[0].ds_addr));
	rx_le->msk_control =
	    htole32(segs[0].ds_len | OP_PACKET | HW_OWNER);

	return (0);
}

/*
 * Set media options.
 */
static int
msk_mediachange(struct ifnet *ifp)
{
	struct msk_if_softc *sc_if;
	struct mii_data	*mii;
	int error;

	sc_if = ifp->if_softc;

	MSK_IF_LOCK(sc_if);
	mii = device_get_softc(sc_if->msk_miibus);
	error = mii_mediachg(mii);
	MSK_IF_UNLOCK(sc_if);

	return (error);
}

/*
 * Report current media status.
 */
static void
msk_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct msk_if_softc *sc_if;
	struct mii_data	*mii;

	sc_if = ifp->if_softc;
	MSK_IF_LOCK(sc_if);
	if ((ifp->if_flags & IFF_UP) == 0) {
		MSK_IF_UNLOCK(sc_if);
		return;
	}
	mii = device_get_softc(sc_if->msk_miibus);

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	MSK_IF_UNLOCK(sc_if);
}

static int
msk_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct msk_if_softc *sc_if;
	struct ifreq *ifr;
	struct mii_data	*mii;
	int error, mask, reinit;

	sc_if = ifp->if_softc;
	ifr = (struct ifreq *)data;
	error = 0;

	switch(command) {
	case SIOCSIFMTU:
		MSK_IF_LOCK(sc_if);
		if (ifr->ifr_mtu > MSK_JUMBO_MTU || ifr->ifr_mtu < ETHERMIN)
			error = EINVAL;
		else if (ifp->if_mtu != ifr->ifr_mtu) {
			if (ifr->ifr_mtu > ETHERMTU) {
				if ((sc_if->msk_flags & MSK_FLAG_JUMBO) == 0) {
					error = EINVAL;
					MSK_IF_UNLOCK(sc_if);
					break;
				}
				if ((sc_if->msk_flags &
				    MSK_FLAG_JUMBO_NOCSUM) != 0) {
					ifp->if_hwassist &=
					    ~(MSK_CSUM_FEATURES | CSUM_TSO);
					ifp->if_capenable &=
					    ~(IFCAP_TSO4 | IFCAP_TXCSUM);
					VLAN_CAPABILITIES(ifp);
				}
			}
			ifp->if_mtu = ifr->ifr_mtu;
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
				msk_init_locked(sc_if);
			}
		}
		MSK_IF_UNLOCK(sc_if);
		break;
	case SIOCSIFFLAGS:
		MSK_IF_LOCK(sc_if);
		if ((ifp->if_flags & IFF_UP) != 0) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc_if->msk_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) != 0)
				msk_rxfilter(sc_if);
			else if ((sc_if->msk_flags & MSK_FLAG_DETACH) == 0)
				msk_init_locked(sc_if);
		} else if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			msk_stop(sc_if);
		sc_if->msk_if_flags = ifp->if_flags;
		MSK_IF_UNLOCK(sc_if);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		MSK_IF_LOCK(sc_if);
		if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
			msk_rxfilter(sc_if);
		MSK_IF_UNLOCK(sc_if);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc_if->msk_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	case SIOCSIFCAP:
		reinit = 0;
		MSK_IF_LOCK(sc_if);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		if ((mask & IFCAP_TXCSUM) != 0 &&
		    (IFCAP_TXCSUM & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_TXCSUM;
			if ((IFCAP_TXCSUM & ifp->if_capenable) != 0)
				ifp->if_hwassist |= MSK_CSUM_FEATURES;
			else
				ifp->if_hwassist &= ~MSK_CSUM_FEATURES;
		}
		if ((mask & IFCAP_RXCSUM) != 0 &&
		    (IFCAP_RXCSUM & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_RXCSUM;
			if ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0)
				reinit = 1;
		}
		if ((mask & IFCAP_VLAN_HWCSUM) != 0 &&
		    (IFCAP_VLAN_HWCSUM & ifp->if_capabilities) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWCSUM;
		if ((mask & IFCAP_TSO4) != 0 &&
		    (IFCAP_TSO4 & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_TSO4;
			if ((IFCAP_TSO4 & ifp->if_capenable) != 0)
				ifp->if_hwassist |= CSUM_TSO;
			else
				ifp->if_hwassist &= ~CSUM_TSO;
		}
		if ((mask & IFCAP_VLAN_HWTSO) != 0 &&
		    (IFCAP_VLAN_HWTSO & ifp->if_capabilities) != 0)
			ifp->if_capenable ^= IFCAP_VLAN_HWTSO;
		if ((mask & IFCAP_VLAN_HWTAGGING) != 0 &&
		    (IFCAP_VLAN_HWTAGGING & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_VLAN_HWTAGGING;
			if ((IFCAP_VLAN_HWTAGGING & ifp->if_capenable) == 0)
				ifp->if_capenable &=
				    ~(IFCAP_VLAN_HWTSO | IFCAP_VLAN_HWCSUM);
			msk_setvlan(sc_if, ifp);
		}
		if (ifp->if_mtu > ETHERMTU &&
		    (sc_if->msk_flags & MSK_FLAG_JUMBO_NOCSUM) != 0) {
			ifp->if_hwassist &= ~(MSK_CSUM_FEATURES | CSUM_TSO);
			ifp->if_capenable &= ~(IFCAP_TSO4 | IFCAP_TXCSUM);
		}
		VLAN_CAPABILITIES(ifp);
		if (reinit > 0 && (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			msk_init_locked(sc_if);
		}
		MSK_IF_UNLOCK(sc_if);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static int
mskc_probe(device_t dev)
{
	const struct msk_product *mp;
	uint16_t vendor, devid;
	int i;

	vendor = pci_get_vendor(dev);
	devid = pci_get_device(dev);
	mp = msk_products;
	for (i = 0; i < nitems(msk_products); i++, mp++) {
		if (vendor == mp->msk_vendorid && devid == mp->msk_deviceid) {
			device_set_desc(dev, mp->msk_name);
			return (BUS_PROBE_DEFAULT);
		}
	}

	return (ENXIO);
}

static int
mskc_setup_rambuffer(struct msk_softc *sc)
{
	int next;
	int i;

	/* Get adapter SRAM size. */
	sc->msk_ramsize = CSR_READ_1(sc, B2_E_0) * 4;
	if (bootverbose)
		device_printf(sc->msk_dev,
		    "RAM buffer size : %dKB\n", sc->msk_ramsize);
	if (sc->msk_ramsize == 0)
		return (0);

	sc->msk_pflags |= MSK_FLAG_RAMBUF;
	/*
	 * Give receiver 2/3 of memory and round down to the multiple
	 * of 1024. Tx/Rx RAM buffer size of Yukon II should be multiple
	 * of 1024.
	 */
	sc->msk_rxqsize = rounddown((sc->msk_ramsize * 1024 * 2) / 3, 1024);
	sc->msk_txqsize = (sc->msk_ramsize * 1024) - sc->msk_rxqsize;
	for (i = 0, next = 0; i < sc->msk_num_port; i++) {
		sc->msk_rxqstart[i] = next;
		sc->msk_rxqend[i] = next + sc->msk_rxqsize - 1;
		next = sc->msk_rxqend[i] + 1;
		sc->msk_txqstart[i] = next;
		sc->msk_txqend[i] = next + sc->msk_txqsize - 1;
		next = sc->msk_txqend[i] + 1;
		if (bootverbose) {
			device_printf(sc->msk_dev,
			    "Port %d : Rx Queue %dKB(0x%08x:0x%08x)\n", i,
			    sc->msk_rxqsize / 1024, sc->msk_rxqstart[i],
			    sc->msk_rxqend[i]);
			device_printf(sc->msk_dev,
			    "Port %d : Tx Queue %dKB(0x%08x:0x%08x)\n", i,
			    sc->msk_txqsize / 1024, sc->msk_txqstart[i],
			    sc->msk_txqend[i]);
		}
	}

	return (0);
}

static void
msk_phy_power(struct msk_softc *sc, int mode)
{
	uint32_t our, val;
	int i;

	switch (mode) {
	case MSK_PHY_POWERUP:
		/* Switch power to VCC (WA for VAUX problem). */
		CSR_WRITE_1(sc, B0_POWER_CTRL,
		    PC_VAUX_ENA | PC_VCC_ENA | PC_VAUX_OFF | PC_VCC_ON);
		/* Disable Core Clock Division, set Clock Select to 0. */
		CSR_WRITE_4(sc, B2_Y2_CLK_CTRL, Y2_CLK_DIV_DIS);

		val = 0;
		if (sc->msk_hw_id == CHIP_ID_YUKON_XL &&
		    sc->msk_hw_rev > CHIP_REV_YU_XL_A1) {
			/* Enable bits are inverted. */
			val = Y2_PCI_CLK_LNK1_DIS | Y2_COR_CLK_LNK1_DIS |
			      Y2_CLK_GAT_LNK1_DIS | Y2_PCI_CLK_LNK2_DIS |
			      Y2_COR_CLK_LNK2_DIS | Y2_CLK_GAT_LNK2_DIS;
		}
		/*
		 * Enable PCI & Core Clock, enable clock gating for both Links.
		 */
		CSR_WRITE_1(sc, B2_Y2_CLK_GATE, val);

		our = CSR_PCI_READ_4(sc, PCI_OUR_REG_1);
		our &= ~(PCI_Y2_PHY1_POWD | PCI_Y2_PHY2_POWD);
		if (sc->msk_hw_id == CHIP_ID_YUKON_XL) {
			if (sc->msk_hw_rev > CHIP_REV_YU_XL_A1) {
				/* Deassert Low Power for 1st PHY. */
				our |= PCI_Y2_PHY1_COMA;
				if (sc->msk_num_port > 1)
					our |= PCI_Y2_PHY2_COMA;
			}
		}
		if (sc->msk_hw_id == CHIP_ID_YUKON_EC_U ||
		    sc->msk_hw_id == CHIP_ID_YUKON_EX ||
		    sc->msk_hw_id >= CHIP_ID_YUKON_FE_P) {
			val = CSR_PCI_READ_4(sc, PCI_OUR_REG_4);
			val &= (PCI_FORCE_ASPM_REQUEST |
			    PCI_ASPM_GPHY_LINK_DOWN | PCI_ASPM_INT_FIFO_EMPTY |
			    PCI_ASPM_CLKRUN_REQUEST);
			/* Set all bits to 0 except bits 15..12. */
			CSR_PCI_WRITE_4(sc, PCI_OUR_REG_4, val);
			val = CSR_PCI_READ_4(sc, PCI_OUR_REG_5);
			val &= PCI_CTL_TIM_VMAIN_AV_MSK;
			CSR_PCI_WRITE_4(sc, PCI_OUR_REG_5, val);
			CSR_PCI_WRITE_4(sc, PCI_CFG_REG_1, 0);
			CSR_WRITE_2(sc, B0_CTST, Y2_HW_WOL_ON);
			/*
			 * Disable status race, workaround for
			 * Yukon EC Ultra & Yukon EX.
			 */
			val = CSR_READ_4(sc, B2_GP_IO);
			val |= GLB_GPIO_STAT_RACE_DIS;
			CSR_WRITE_4(sc, B2_GP_IO, val);
			CSR_READ_4(sc, B2_GP_IO);
		}
		/* Release PHY from PowerDown/COMA mode. */
		CSR_PCI_WRITE_4(sc, PCI_OUR_REG_1, our);

		for (i = 0; i < sc->msk_num_port; i++) {
			CSR_WRITE_2(sc, MR_ADDR(i, GMAC_LINK_CTRL),
			    GMLC_RST_SET);
			CSR_WRITE_2(sc, MR_ADDR(i, GMAC_LINK_CTRL),
			    GMLC_RST_CLR);
		}
		break;
	case MSK_PHY_POWERDOWN:
		val = CSR_PCI_READ_4(sc, PCI_OUR_REG_1);
		val |= PCI_Y2_PHY1_POWD | PCI_Y2_PHY2_POWD;
		if (sc->msk_hw_id == CHIP_ID_YUKON_XL &&
		    sc->msk_hw_rev > CHIP_REV_YU_XL_A1) {
			val &= ~PCI_Y2_PHY1_COMA;
			if (sc->msk_num_port > 1)
				val &= ~PCI_Y2_PHY2_COMA;
		}
		CSR_PCI_WRITE_4(sc, PCI_OUR_REG_1, val);

		val = Y2_PCI_CLK_LNK1_DIS | Y2_COR_CLK_LNK1_DIS |
		      Y2_CLK_GAT_LNK1_DIS | Y2_PCI_CLK_LNK2_DIS |
		      Y2_COR_CLK_LNK2_DIS | Y2_CLK_GAT_LNK2_DIS;
		if (sc->msk_hw_id == CHIP_ID_YUKON_XL &&
		    sc->msk_hw_rev > CHIP_REV_YU_XL_A1) {
			/* Enable bits are inverted. */
			val = 0;
		}
		/*
		 * Disable PCI & Core Clock, disable clock gating for
		 * both Links.
		 */
		CSR_WRITE_1(sc, B2_Y2_CLK_GATE, val);
		CSR_WRITE_1(sc, B0_POWER_CTRL,
		    PC_VAUX_ENA | PC_VCC_ENA | PC_VAUX_ON | PC_VCC_OFF);
		break;
	default:
		break;
	}
}

static void
mskc_reset(struct msk_softc *sc)
{
	bus_addr_t addr;
	uint16_t status;
	uint32_t val;
	int i, initram;

	/* Disable ASF. */
	if (sc->msk_hw_id >= CHIP_ID_YUKON_XL &&
	    sc->msk_hw_id <= CHIP_ID_YUKON_SUPR) {
		if (sc->msk_hw_id == CHIP_ID_YUKON_EX ||
		    sc->msk_hw_id == CHIP_ID_YUKON_SUPR) {
			CSR_WRITE_4(sc, B28_Y2_CPU_WDOG, 0);
			status = CSR_READ_2(sc, B28_Y2_ASF_HCU_CCSR);
			/* Clear AHB bridge & microcontroller reset. */
			status &= ~(Y2_ASF_HCU_CCSR_AHB_RST |
			    Y2_ASF_HCU_CCSR_CPU_RST_MODE);
			/* Clear ASF microcontroller state. */
			status &= ~Y2_ASF_HCU_CCSR_UC_STATE_MSK;
			status &= ~Y2_ASF_HCU_CCSR_CPU_CLK_DIVIDE_MSK;
			CSR_WRITE_2(sc, B28_Y2_ASF_HCU_CCSR, status);
			CSR_WRITE_4(sc, B28_Y2_CPU_WDOG, 0);
		} else
			CSR_WRITE_1(sc, B28_Y2_ASF_STAT_CMD, Y2_ASF_RESET);
		CSR_WRITE_2(sc, B0_CTST, Y2_ASF_DISABLE);
		/*
		 * Since we disabled ASF, S/W reset is required for
		 * Power Management.
		 */
		CSR_WRITE_2(sc, B0_CTST, CS_RST_SET);
		CSR_WRITE_2(sc, B0_CTST, CS_RST_CLR);
	}

	/* Clear all error bits in the PCI status register. */
	status = pci_read_config(sc->msk_dev, PCIR_STATUS, 2);
	CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_ON);

	pci_write_config(sc->msk_dev, PCIR_STATUS, status |
	    PCIM_STATUS_PERR | PCIM_STATUS_SERR | PCIM_STATUS_RMABORT |
	    PCIM_STATUS_RTABORT | PCIM_STATUS_MDPERR, 2);
	CSR_WRITE_2(sc, B0_CTST, CS_MRST_CLR);

	switch (sc->msk_bustype) {
	case MSK_PEX_BUS:
		/* Clear all PEX errors. */
		CSR_PCI_WRITE_4(sc, PEX_UNC_ERR_STAT, 0xffffffff);
		val = CSR_PCI_READ_4(sc, PEX_UNC_ERR_STAT);
		if ((val & PEX_RX_OV) != 0) {
			sc->msk_intrmask &= ~Y2_IS_HW_ERR;
			sc->msk_intrhwemask &= ~Y2_IS_PCI_EXP;
		}
		break;
	case MSK_PCI_BUS:
	case MSK_PCIX_BUS:
		/* Set Cache Line Size to 2(8bytes) if configured to 0. */
		val = pci_read_config(sc->msk_dev, PCIR_CACHELNSZ, 1);
		if (val == 0)
			pci_write_config(sc->msk_dev, PCIR_CACHELNSZ, 2, 1);
		if (sc->msk_bustype == MSK_PCIX_BUS) {
			/* Set Cache Line Size opt. */
			val = pci_read_config(sc->msk_dev, PCI_OUR_REG_1, 4);
			val |= PCI_CLS_OPT;
			pci_write_config(sc->msk_dev, PCI_OUR_REG_1, val, 4);
		}
		break;
	}
	/* Set PHY power state. */
	msk_phy_power(sc, MSK_PHY_POWERUP);

	/* Reset GPHY/GMAC Control */
	for (i = 0; i < sc->msk_num_port; i++) {
		/* GPHY Control reset. */
		CSR_WRITE_1(sc, MR_ADDR(i, GPHY_CTRL), GPC_RST_SET);
		CSR_WRITE_1(sc, MR_ADDR(i, GPHY_CTRL), GPC_RST_CLR);
		/* GMAC Control reset. */
		CSR_WRITE_4(sc, MR_ADDR(i, GMAC_CTRL), GMC_RST_SET);
		CSR_WRITE_4(sc, MR_ADDR(i, GMAC_CTRL), GMC_RST_CLR);
		CSR_WRITE_4(sc, MR_ADDR(i, GMAC_CTRL), GMC_F_LOOPB_OFF);
		if (sc->msk_hw_id == CHIP_ID_YUKON_EX ||
		    sc->msk_hw_id == CHIP_ID_YUKON_SUPR)
			CSR_WRITE_4(sc, MR_ADDR(i, GMAC_CTRL),
			    GMC_BYP_MACSECRX_ON | GMC_BYP_MACSECTX_ON |
			    GMC_BYP_RETR_ON);
	}

	if (sc->msk_hw_id == CHIP_ID_YUKON_SUPR &&
	    sc->msk_hw_rev > CHIP_REV_YU_SU_B0)
		CSR_PCI_WRITE_4(sc, PCI_OUR_REG_3, PCI_CLK_MACSEC_DIS);
	if (sc->msk_hw_id == CHIP_ID_YUKON_OPT && sc->msk_hw_rev == 0) {
		/* Disable PCIe PHY powerdown(reg 0x80, bit7). */
		CSR_WRITE_4(sc, Y2_PEX_PHY_DATA, (0x0080 << 16) | 0x0080);
	}
	CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

	/* LED On. */
	CSR_WRITE_2(sc, B0_CTST, Y2_LED_STAT_ON);

	/* Clear TWSI IRQ. */
	CSR_WRITE_4(sc, B2_I2C_IRQ, I2C_CLR_IRQ);

	/* Turn off hardware timer. */
	CSR_WRITE_1(sc, B2_TI_CTRL, TIM_STOP);
	CSR_WRITE_1(sc, B2_TI_CTRL, TIM_CLR_IRQ);

	/* Turn off descriptor polling. */
	CSR_WRITE_1(sc, B28_DPT_CTRL, DPT_STOP);

	/* Turn off time stamps. */
	CSR_WRITE_1(sc, GMAC_TI_ST_CTRL, GMT_ST_STOP);
	CSR_WRITE_1(sc, GMAC_TI_ST_CTRL, GMT_ST_CLR_IRQ);

	initram = 0;
	if (sc->msk_hw_id == CHIP_ID_YUKON_XL ||
	    sc->msk_hw_id == CHIP_ID_YUKON_EC ||
	    sc->msk_hw_id == CHIP_ID_YUKON_FE)
		initram++;

	/* Configure timeout values. */
	for (i = 0; initram > 0 && i < sc->msk_num_port; i++) {
		CSR_WRITE_2(sc, SELECT_RAM_BUFFER(i, B3_RI_CTRL), RI_RST_SET);
		CSR_WRITE_2(sc, SELECT_RAM_BUFFER(i, B3_RI_CTRL), RI_RST_CLR);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_WTO_R1),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_WTO_XA1),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_WTO_XS1),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_RTO_R1),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_RTO_XA1),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_RTO_XS1),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_WTO_R2),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_WTO_XA2),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_WTO_XS2),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_RTO_R2),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_RTO_XA2),
		    MSK_RI_TO_53);
		CSR_WRITE_1(sc, SELECT_RAM_BUFFER(i, B3_RI_RTO_XS2),
		    MSK_RI_TO_53);
	}

	/* Disable all interrupts. */
	CSR_WRITE_4(sc, B0_HWE_IMSK, 0);
	CSR_READ_4(sc, B0_HWE_IMSK);
	CSR_WRITE_4(sc, B0_IMSK, 0);
	CSR_READ_4(sc, B0_IMSK);

        /*
         * On dual port PCI-X card, there is an problem where status
         * can be received out of order due to split transactions.
         */
	if (sc->msk_pcixcap != 0 && sc->msk_num_port > 1) {
		uint16_t pcix_cmd;

		pcix_cmd = pci_read_config(sc->msk_dev,
		    sc->msk_pcixcap + PCIXR_COMMAND, 2);
		/* Clear Max Outstanding Split Transactions. */
		pcix_cmd &= ~PCIXM_COMMAND_MAX_SPLITS;
		CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		pci_write_config(sc->msk_dev,
		    sc->msk_pcixcap + PCIXR_COMMAND, pcix_cmd, 2);
		CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
        }
	if (sc->msk_expcap != 0) {
		/* Change Max. Read Request Size to 2048 bytes. */
		if (pci_get_max_read_req(sc->msk_dev) == 512)
			pci_set_max_read_req(sc->msk_dev, 2048);
	}

	/* Clear status list. */
	bzero(sc->msk_stat_ring,
	    sizeof(struct msk_stat_desc) * sc->msk_stat_count);
	sc->msk_stat_cons = 0;
	bus_dmamap_sync(sc->msk_stat_tag, sc->msk_stat_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_4(sc, STAT_CTRL, SC_STAT_RST_SET);
	CSR_WRITE_4(sc, STAT_CTRL, SC_STAT_RST_CLR);
	/* Set the status list base address. */
	addr = sc->msk_stat_ring_paddr;
	CSR_WRITE_4(sc, STAT_LIST_ADDR_LO, MSK_ADDR_LO(addr));
	CSR_WRITE_4(sc, STAT_LIST_ADDR_HI, MSK_ADDR_HI(addr));
	/* Set the status list last index. */
	CSR_WRITE_2(sc, STAT_LAST_IDX, sc->msk_stat_count - 1);
	if (sc->msk_hw_id == CHIP_ID_YUKON_EC &&
	    sc->msk_hw_rev == CHIP_REV_YU_EC_A1) {
		/* WA for dev. #4.3 */
		CSR_WRITE_2(sc, STAT_TX_IDX_TH, ST_TXTH_IDX_MASK);
		/* WA for dev. #4.18 */
		CSR_WRITE_1(sc, STAT_FIFO_WM, 0x21);
		CSR_WRITE_1(sc, STAT_FIFO_ISR_WM, 0x07);
	} else {
		CSR_WRITE_2(sc, STAT_TX_IDX_TH, 0x0a);
		CSR_WRITE_1(sc, STAT_FIFO_WM, 0x10);
		if (sc->msk_hw_id == CHIP_ID_YUKON_XL &&
		    sc->msk_hw_rev == CHIP_REV_YU_XL_A0)
			CSR_WRITE_1(sc, STAT_FIFO_ISR_WM, 0x04);
		else
			CSR_WRITE_1(sc, STAT_FIFO_ISR_WM, 0x10);
		CSR_WRITE_4(sc, STAT_ISR_TIMER_INI, 0x0190);
	}
	/*
	 * Use default value for STAT_ISR_TIMER_INI, STAT_LEV_TIMER_INI.
	 */
	CSR_WRITE_4(sc, STAT_TX_TIMER_INI, MSK_USECS(sc, 1000));

	/* Enable status unit. */
	CSR_WRITE_4(sc, STAT_CTRL, SC_STAT_OP_ON);

	CSR_WRITE_1(sc, STAT_TX_TIMER_CTRL, TIM_START);
	CSR_WRITE_1(sc, STAT_LEV_TIMER_CTRL, TIM_START);
	CSR_WRITE_1(sc, STAT_ISR_TIMER_CTRL, TIM_START);
}

static int
msk_probe(device_t dev)
{
	struct msk_softc *sc;
	char desc[100];

	sc = device_get_softc(device_get_parent(dev));
	/*
	 * Not much to do here. We always know there will be
	 * at least one GMAC present, and if there are two,
	 * mskc_attach() will create a second device instance
	 * for us.
	 */
	snprintf(desc, sizeof(desc),
	    "Marvell Technology Group Ltd. %s Id 0x%02x Rev 0x%02x",
	    model_name[sc->msk_hw_id - CHIP_ID_YUKON_XL], sc->msk_hw_id,
	    sc->msk_hw_rev);
	device_set_desc_copy(dev, desc);

	return (BUS_PROBE_DEFAULT);
}

static int
msk_attach(device_t dev)
{
	struct msk_softc *sc;
	struct msk_if_softc *sc_if;
	struct ifnet *ifp;
	struct msk_mii_data *mmd;
	int i, port, error;
	uint8_t eaddr[6];

	if (dev == NULL)
		return (EINVAL);

	error = 0;
	sc_if = device_get_softc(dev);
	sc = device_get_softc(device_get_parent(dev));
	mmd = device_get_ivars(dev);
	port = mmd->port;

	sc_if->msk_if_dev = dev;
	sc_if->msk_port = port;
	sc_if->msk_softc = sc;
	sc_if->msk_flags = sc->msk_pflags;
	sc->msk_if[port] = sc_if;
	/* Setup Tx/Rx queue register offsets. */
	if (port == MSK_PORT_A) {
		sc_if->msk_txq = Q_XA1;
		sc_if->msk_txsq = Q_XS1;
		sc_if->msk_rxq = Q_R1;
	} else {
		sc_if->msk_txq = Q_XA2;
		sc_if->msk_txsq = Q_XS2;
		sc_if->msk_rxq = Q_R2;
	}

	callout_init_mtx(&sc_if->msk_tick_ch, &sc_if->msk_softc->msk_mtx, 0);
	msk_sysctl_node(sc_if);

	if ((error = msk_txrx_dma_alloc(sc_if)) != 0)
		goto fail;
	msk_rx_dma_jalloc(sc_if);

	ifp = sc_if->msk_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc_if->msk_if_dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc_if;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_capabilities = IFCAP_TXCSUM | IFCAP_TSO4;
	/*
	 * Enable Rx checksum offloading if controller supports
	 * new descriptor formant and controller is not Yukon XL.
	 */
	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0 &&
	    sc->msk_hw_id != CHIP_ID_YUKON_XL)
		ifp->if_capabilities |= IFCAP_RXCSUM;
	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) != 0 &&
	    (sc_if->msk_flags & MSK_FLAG_NORX_CSUM) == 0)
		ifp->if_capabilities |= IFCAP_RXCSUM;
	ifp->if_hwassist = MSK_CSUM_FEATURES | CSUM_TSO;
	ifp->if_capenable = ifp->if_capabilities;
	ifp->if_ioctl = msk_ioctl;
	ifp->if_start = msk_start;
	ifp->if_init = msk_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, MSK_TX_RING_CNT - 1);
	ifp->if_snd.ifq_drv_maxlen = MSK_TX_RING_CNT - 1;
	IFQ_SET_READY(&ifp->if_snd);
	/*
	 * Get station address for this interface. Note that
	 * dual port cards actually come with three station
	 * addresses: one for each port, plus an extra. The
	 * extra one is used by the SysKonnect driver software
	 * as a 'virtual' station address for when both ports
	 * are operating in failover mode. Currently we don't
	 * use this extra address.
	 */
	MSK_IF_LOCK(sc_if);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		eaddr[i] = CSR_READ_1(sc, B2_MAC_1 + (port * 8) + i);

	/*
	 * Call MI attach routine.  Can't hold locks when calling into ether_*.
	 */
	MSK_IF_UNLOCK(sc_if);
	ether_ifattach(ifp, eaddr);
	MSK_IF_LOCK(sc_if);

	/* VLAN capability setup */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	if ((sc_if->msk_flags & MSK_FLAG_NOHWVLAN) == 0) {
		/*
		 * Due to Tx checksum offload hardware bugs, msk(4) manually
		 * computes checksum for short frames. For VLAN tagged frames
		 * this workaround does not work so disable checksum offload
		 * for VLAN interface.
		 */
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING | IFCAP_VLAN_HWTSO;
		/*
		 * Enable Rx checksum offloading for VLAN tagged frames
		 * if controller support new descriptor format.
		 */
		if ((sc_if->msk_flags & MSK_FLAG_DESCV2) != 0 &&
		    (sc_if->msk_flags & MSK_FLAG_NORX_CSUM) == 0)
			ifp->if_capabilities |= IFCAP_VLAN_HWCSUM;
	}
	ifp->if_capenable = ifp->if_capabilities;
	/*
	 * Disable RX checksum offloading on controllers that don't use
	 * new descriptor format but give chance to enable it.
	 */
	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0)
		ifp->if_capenable &= ~IFCAP_RXCSUM;

	/*
	 * Tell the upper layer(s) we support long frames.
	 * Must appear after the call to ether_ifattach() because
	 * ether_ifattach() sets ifi_hdrlen to the default value.
	 */
        ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	/*
	 * Do miibus setup.
	 */
	MSK_IF_UNLOCK(sc_if);
	error = mii_attach(dev, &sc_if->msk_miibus, ifp, msk_mediachange,
	    msk_mediastatus, BMSR_DEFCAPMASK, PHY_ADDR_MARV, MII_OFFSET_ANY,
	    mmd->mii_flags);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev, "attaching PHYs failed\n");
		ether_ifdetach(ifp);
		error = ENXIO;
		goto fail;
	}

fail:
	if (error != 0) {
		/* Access should be ok even though lock has been dropped */
		sc->msk_if[port] = NULL;
		msk_detach(dev);
	}

	return (error);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
static int
mskc_attach(device_t dev)
{
	struct msk_softc *sc;
	struct msk_mii_data *mmd;
	int error, msic, msir, reg;

	sc = device_get_softc(dev);
	sc->msk_dev = dev;
	mtx_init(&sc->msk_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	/* Allocate I/O resource */
#ifdef MSK_USEIOSPACE
	sc->msk_res_spec = msk_res_spec_io;
#else
	sc->msk_res_spec = msk_res_spec_mem;
#endif
	sc->msk_irq_spec = msk_irq_spec_legacy;
	error = bus_alloc_resources(dev, sc->msk_res_spec, sc->msk_res);
	if (error) {
		if (sc->msk_res_spec == msk_res_spec_mem)
			sc->msk_res_spec = msk_res_spec_io;
		else
			sc->msk_res_spec = msk_res_spec_mem;
		error = bus_alloc_resources(dev, sc->msk_res_spec, sc->msk_res);
		if (error) {
			device_printf(dev, "couldn't allocate %s resources\n",
			    sc->msk_res_spec == msk_res_spec_mem ? "memory" :
			    "I/O");
			mtx_destroy(&sc->msk_mtx);
			return (ENXIO);
		}
	}

	/* Enable all clocks before accessing any registers. */
	CSR_PCI_WRITE_4(sc, PCI_OUR_REG_3, 0);

	CSR_WRITE_2(sc, B0_CTST, CS_RST_CLR);
	sc->msk_hw_id = CSR_READ_1(sc, B2_CHIP_ID);
	sc->msk_hw_rev = (CSR_READ_1(sc, B2_MAC_CFG) >> 4) & 0x0f;
	/* Bail out if chip is not recognized. */
	if (sc->msk_hw_id < CHIP_ID_YUKON_XL ||
	    sc->msk_hw_id > CHIP_ID_YUKON_OPT ||
	    sc->msk_hw_id == CHIP_ID_YUKON_UNKNOWN) {
		device_printf(dev, "unknown device: id=0x%02x, rev=0x%02x\n",
		    sc->msk_hw_id, sc->msk_hw_rev);
		mtx_destroy(&sc->msk_mtx);
		return (ENXIO);
	}

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "process_limit", CTLTYPE_INT | CTLFLAG_RW,
	    &sc->msk_process_limit, 0, sysctl_hw_msk_proc_limit, "I",
	    "max number of Rx events to process");

	sc->msk_process_limit = MSK_PROC_DEFAULT;
	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "process_limit", &sc->msk_process_limit);
	if (error == 0) {
		if (sc->msk_process_limit < MSK_PROC_MIN ||
		    sc->msk_process_limit > MSK_PROC_MAX) {
			device_printf(dev, "process_limit value out of range; "
			    "using default: %d\n", MSK_PROC_DEFAULT);
			sc->msk_process_limit = MSK_PROC_DEFAULT;
		}
	}

	sc->msk_int_holdoff = MSK_INT_HOLDOFF_DEFAULT;
	SYSCTL_ADD_INT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)), OID_AUTO,
	    "int_holdoff", CTLFLAG_RW, &sc->msk_int_holdoff, 0,
	    "Maximum number of time to delay interrupts");
	resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "int_holdoff", &sc->msk_int_holdoff);

	sc->msk_pmd = CSR_READ_1(sc, B2_PMD_TYP);
	/* Check number of MACs. */
	sc->msk_num_port = 1;
	if ((CSR_READ_1(sc, B2_Y2_HW_RES) & CFG_DUAL_MAC_MSK) ==
	    CFG_DUAL_MAC_MSK) {
		if (!(CSR_READ_1(sc, B2_Y2_CLK_GATE) & Y2_STATUS_LNK2_INAC))
			sc->msk_num_port++;
	}

	/* Check bus type. */
	if (pci_find_cap(sc->msk_dev, PCIY_EXPRESS, &reg) == 0) {
		sc->msk_bustype = MSK_PEX_BUS;
		sc->msk_expcap = reg;
	} else if (pci_find_cap(sc->msk_dev, PCIY_PCIX, &reg) == 0) {
		sc->msk_bustype = MSK_PCIX_BUS;
		sc->msk_pcixcap = reg;
	} else
		sc->msk_bustype = MSK_PCI_BUS;

	switch (sc->msk_hw_id) {
	case CHIP_ID_YUKON_EC:
		sc->msk_clock = 125;	/* 125 MHz */
		sc->msk_pflags |= MSK_FLAG_JUMBO;
		break;
	case CHIP_ID_YUKON_EC_U:
		sc->msk_clock = 125;	/* 125 MHz */
		sc->msk_pflags |= MSK_FLAG_JUMBO | MSK_FLAG_JUMBO_NOCSUM;
		break;
	case CHIP_ID_YUKON_EX:
		sc->msk_clock = 125;	/* 125 MHz */
		sc->msk_pflags |= MSK_FLAG_JUMBO | MSK_FLAG_DESCV2 |
		    MSK_FLAG_AUTOTX_CSUM;
		/*
		 * Yukon Extreme seems to have silicon bug for
		 * automatic Tx checksum calculation capability.
		 */
		if (sc->msk_hw_rev == CHIP_REV_YU_EX_B0)
			sc->msk_pflags &= ~MSK_FLAG_AUTOTX_CSUM;
		/*
		 * Yukon Extreme A0 could not use store-and-forward
		 * for jumbo frames, so disable Tx checksum
		 * offloading for jumbo frames.
		 */
		if (sc->msk_hw_rev == CHIP_REV_YU_EX_A0)
			sc->msk_pflags |= MSK_FLAG_JUMBO_NOCSUM;
		break;
	case CHIP_ID_YUKON_FE:
		sc->msk_clock = 100;	/* 100 MHz */
		sc->msk_pflags |= MSK_FLAG_FASTETHER;
		break;
	case CHIP_ID_YUKON_FE_P:
		sc->msk_clock = 50;	/* 50 MHz */
		sc->msk_pflags |= MSK_FLAG_FASTETHER | MSK_FLAG_DESCV2 |
		    MSK_FLAG_AUTOTX_CSUM;
		if (sc->msk_hw_rev == CHIP_REV_YU_FE_P_A0) {
			/*
			 * XXX
			 * FE+ A0 has status LE writeback bug so msk(4)
			 * does not rely on status word of received frame
			 * in msk_rxeof() which in turn disables all
			 * hardware assistance bits reported by the status
			 * word as well as validity of the received frame.
			 * Just pass received frames to upper stack with
			 * minimal test and let upper stack handle them.
			 */
			sc->msk_pflags |= MSK_FLAG_NOHWVLAN |
			    MSK_FLAG_NORXCHK | MSK_FLAG_NORX_CSUM;
		}
		break;
	case CHIP_ID_YUKON_XL:
		sc->msk_clock = 156;	/* 156 MHz */
		sc->msk_pflags |= MSK_FLAG_JUMBO;
		break;
	case CHIP_ID_YUKON_SUPR:
		sc->msk_clock = 125;	/* 125 MHz */
		sc->msk_pflags |= MSK_FLAG_JUMBO | MSK_FLAG_DESCV2 |
		    MSK_FLAG_AUTOTX_CSUM;
		break;
	case CHIP_ID_YUKON_UL_2:
		sc->msk_clock = 125;	/* 125 MHz */
		sc->msk_pflags |= MSK_FLAG_JUMBO;
		break;
	case CHIP_ID_YUKON_OPT:
		sc->msk_clock = 125;	/* 125 MHz */
		sc->msk_pflags |= MSK_FLAG_JUMBO | MSK_FLAG_DESCV2;
		break;
	default:
		sc->msk_clock = 156;	/* 156 MHz */
		break;
	}

	/* Allocate IRQ resources. */
	msic = pci_msi_count(dev);
	if (bootverbose)
		device_printf(dev, "MSI count : %d\n", msic);
	if (legacy_intr != 0)
		msi_disable = 1;
	if (msi_disable == 0 && msic > 0) {
		msir = 1;
		if (pci_alloc_msi(dev, &msir) == 0) {
			if (msir == 1) {
				sc->msk_pflags |= MSK_FLAG_MSI;
				sc->msk_irq_spec = msk_irq_spec_msi;
			} else
				pci_release_msi(dev);
		}
	}

	error = bus_alloc_resources(dev, sc->msk_irq_spec, sc->msk_irq);
	if (error) {
		device_printf(dev, "couldn't allocate IRQ resources\n");
		goto fail;
	}

	if ((error = msk_status_dma_alloc(sc)) != 0)
		goto fail;

	/* Set base interrupt mask. */
	sc->msk_intrmask = Y2_IS_HW_ERR | Y2_IS_STAT_BMU;
	sc->msk_intrhwemask = Y2_IS_TIST_OV | Y2_IS_MST_ERR |
	    Y2_IS_IRQ_STAT | Y2_IS_PCI_EXP | Y2_IS_PCI_NEXP;

	/* Reset the adapter. */
	mskc_reset(sc);

	if ((error = mskc_setup_rambuffer(sc)) != 0)
		goto fail;

	sc->msk_devs[MSK_PORT_A] = device_add_child(dev, "msk", -1);
	if (sc->msk_devs[MSK_PORT_A] == NULL) {
		device_printf(dev, "failed to add child for PORT_A\n");
		error = ENXIO;
		goto fail;
	}
	mmd = malloc(sizeof(struct msk_mii_data), M_DEVBUF, M_WAITOK | M_ZERO);
	mmd->port = MSK_PORT_A;
	mmd->pmd = sc->msk_pmd;
	mmd->mii_flags |= MIIF_DOPAUSE;
	if (sc->msk_pmd == 'L' || sc->msk_pmd == 'S')
		mmd->mii_flags |= MIIF_HAVEFIBER;
	if (sc->msk_pmd == 'P')
		mmd->mii_flags |= MIIF_HAVEFIBER | MIIF_MACPRIV0;
	device_set_ivars(sc->msk_devs[MSK_PORT_A], mmd);

	if (sc->msk_num_port > 1) {
		sc->msk_devs[MSK_PORT_B] = device_add_child(dev, "msk", -1);
		if (sc->msk_devs[MSK_PORT_B] == NULL) {
			device_printf(dev, "failed to add child for PORT_B\n");
			error = ENXIO;
			goto fail;
		}
		mmd = malloc(sizeof(struct msk_mii_data), M_DEVBUF, M_WAITOK |
		    M_ZERO);
		mmd->port = MSK_PORT_B;
		mmd->pmd = sc->msk_pmd;
		if (sc->msk_pmd == 'L' || sc->msk_pmd == 'S')
			mmd->mii_flags |= MIIF_HAVEFIBER;
		if (sc->msk_pmd == 'P')
			mmd->mii_flags |= MIIF_HAVEFIBER | MIIF_MACPRIV0;
		device_set_ivars(sc->msk_devs[MSK_PORT_B], mmd);
	}

	error = bus_generic_attach(dev);
	if (error) {
		device_printf(dev, "failed to attach port(s)\n");
		goto fail;
	}

	/* Hook interrupt last to avoid having to lock softc. */
	error = bus_setup_intr(dev, sc->msk_irq[0], INTR_TYPE_NET |
	    INTR_MPSAFE, NULL, msk_intr, sc, &sc->msk_intrhand);
	if (error != 0) {
		device_printf(dev, "couldn't set up interrupt handler\n");
		goto fail;
	}
fail:
	if (error != 0)
		mskc_detach(dev);

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
msk_detach(device_t dev)
{
	struct msk_softc *sc;
	struct msk_if_softc *sc_if;
	struct ifnet *ifp;

	sc_if = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc_if->msk_softc->msk_mtx),
	    ("msk mutex not initialized in msk_detach"));
	MSK_IF_LOCK(sc_if);

	ifp = sc_if->msk_ifp;
	if (device_is_attached(dev)) {
		/* XXX */
		sc_if->msk_flags |= MSK_FLAG_DETACH;
		msk_stop(sc_if);
		/* Can't hold locks while calling detach. */
		MSK_IF_UNLOCK(sc_if);
		callout_drain(&sc_if->msk_tick_ch);
		if (ifp)
			ether_ifdetach(ifp);
		MSK_IF_LOCK(sc_if);
	}

	/*
	 * We're generally called from mskc_detach() which is using
	 * device_delete_child() to get to here. It's already trashed
	 * miibus for us, so don't do it here or we'll panic.
	 *
	 * if (sc_if->msk_miibus != NULL) {
	 * 	device_delete_child(dev, sc_if->msk_miibus);
	 * 	sc_if->msk_miibus = NULL;
	 * }
	 */

	msk_rx_dma_jfree(sc_if);
	msk_txrx_dma_free(sc_if);
	bus_generic_detach(dev);

	sc = sc_if->msk_softc;
	sc->msk_if[sc_if->msk_port] = NULL;
	MSK_IF_UNLOCK(sc_if);
	if (ifp)
		if_free(ifp);

	return (0);
}

static int
mskc_detach(device_t dev)
{
	struct msk_softc *sc;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->msk_mtx), ("msk mutex not initialized"));

	if (device_is_alive(dev)) {
		if (sc->msk_devs[MSK_PORT_A] != NULL) {
			free(device_get_ivars(sc->msk_devs[MSK_PORT_A]),
			    M_DEVBUF);
			device_delete_child(dev, sc->msk_devs[MSK_PORT_A]);
		}
		if (sc->msk_devs[MSK_PORT_B] != NULL) {
			free(device_get_ivars(sc->msk_devs[MSK_PORT_B]),
			    M_DEVBUF);
			device_delete_child(dev, sc->msk_devs[MSK_PORT_B]);
		}
		bus_generic_detach(dev);
	}

	/* Disable all interrupts. */
	CSR_WRITE_4(sc, B0_IMSK, 0);
	CSR_READ_4(sc, B0_IMSK);
	CSR_WRITE_4(sc, B0_HWE_IMSK, 0);
	CSR_READ_4(sc, B0_HWE_IMSK);

	/* LED Off. */
	CSR_WRITE_2(sc, B0_CTST, Y2_LED_STAT_OFF);

	/* Put hardware reset. */
	CSR_WRITE_2(sc, B0_CTST, CS_RST_SET);

	msk_status_dma_free(sc);

	if (sc->msk_intrhand) {
		bus_teardown_intr(dev, sc->msk_irq[0], sc->msk_intrhand);
		sc->msk_intrhand = NULL;
	}
	bus_release_resources(dev, sc->msk_irq_spec, sc->msk_irq);
	if ((sc->msk_pflags & MSK_FLAG_MSI) != 0)
		pci_release_msi(dev);
	bus_release_resources(dev, sc->msk_res_spec, sc->msk_res);
	mtx_destroy(&sc->msk_mtx);

	return (0);
}

static bus_dma_tag_t
mskc_get_dma_tag(device_t bus, device_t child __unused)
{

	return (bus_get_dma_tag(bus));
}

struct msk_dmamap_arg {
	bus_addr_t	msk_busaddr;
};

static void
msk_dmamap_cb(void *arg, bus_dma_segment_t *segs, int nseg, int error)
{
	struct msk_dmamap_arg *ctx;

	if (error != 0)
		return;
	ctx = arg;
	ctx->msk_busaddr = segs[0].ds_addr;
}

/* Create status DMA region. */
static int
msk_status_dma_alloc(struct msk_softc *sc)
{
	struct msk_dmamap_arg ctx;
	bus_size_t stat_sz;
	int count, error;

	/*
	 * It seems controller requires number of status LE entries
	 * is power of 2 and the maximum number of status LE entries
	 * is 4096.  For dual-port controllers, the number of status
	 * LE entries should be large enough to hold both port's
	 * status updates.
	 */
	count = 3 * MSK_RX_RING_CNT + MSK_TX_RING_CNT;
	count = imin(4096, roundup2(count, 1024));
	sc->msk_stat_count = count;
	stat_sz = count * sizeof(struct msk_stat_desc);
	error = bus_dma_tag_create(
		    bus_get_dma_tag(sc->msk_dev),	/* parent */
		    MSK_STAT_ALIGN, 0,		/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    stat_sz,			/* maxsize */
		    1,				/* nsegments */
		    stat_sz,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc->msk_stat_tag);
	if (error != 0) {
		device_printf(sc->msk_dev,
		    "failed to create status DMA tag\n");
		return (error);
	}

	/* Allocate DMA'able memory and load the DMA map for status ring. */
	error = bus_dmamem_alloc(sc->msk_stat_tag,
	    (void **)&sc->msk_stat_ring, BUS_DMA_WAITOK | BUS_DMA_COHERENT |
	    BUS_DMA_ZERO, &sc->msk_stat_map);
	if (error != 0) {
		device_printf(sc->msk_dev,
		    "failed to allocate DMA'able memory for status ring\n");
		return (error);
	}

	ctx.msk_busaddr = 0;
	error = bus_dmamap_load(sc->msk_stat_tag, sc->msk_stat_map,
	    sc->msk_stat_ring, stat_sz, msk_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc->msk_dev,
		    "failed to load DMA'able memory for status ring\n");
		return (error);
	}
	sc->msk_stat_ring_paddr = ctx.msk_busaddr;

	return (0);
}

static void
msk_status_dma_free(struct msk_softc *sc)
{

	/* Destroy status block. */
	if (sc->msk_stat_tag) {
		if (sc->msk_stat_ring_paddr) {
			bus_dmamap_unload(sc->msk_stat_tag, sc->msk_stat_map);
			sc->msk_stat_ring_paddr = 0;
		}
		if (sc->msk_stat_ring) {
			bus_dmamem_free(sc->msk_stat_tag,
			    sc->msk_stat_ring, sc->msk_stat_map);
			sc->msk_stat_ring = NULL;
		}
		bus_dma_tag_destroy(sc->msk_stat_tag);
		sc->msk_stat_tag = NULL;
	}
}

static int
msk_txrx_dma_alloc(struct msk_if_softc *sc_if)
{
	struct msk_dmamap_arg ctx;
	struct msk_txdesc *txd;
	struct msk_rxdesc *rxd;
	bus_size_t rxalign;
	int error, i;

	/* Create parent DMA tag. */
	error = bus_dma_tag_create(
		    bus_get_dma_tag(sc_if->msk_if_dev),	/* parent */
		    1, 0,			/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsize */
		    0,				/* nsegments */
		    BUS_SPACE_MAXSIZE_32BIT,	/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->msk_cdata.msk_parent_tag);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create parent DMA tag\n");
		goto fail;
	}
	/* Create tag for Tx ring. */
	error = bus_dma_tag_create(sc_if->msk_cdata.msk_parent_tag,/* parent */
		    MSK_RING_ALIGN, 0,		/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MSK_TX_RING_SZ,		/* maxsize */
		    1,				/* nsegments */
		    MSK_TX_RING_SZ,		/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->msk_cdata.msk_tx_ring_tag);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create Tx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Rx ring. */
	error = bus_dma_tag_create(sc_if->msk_cdata.msk_parent_tag,/* parent */
		    MSK_RING_ALIGN, 0,		/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MSK_RX_RING_SZ,		/* maxsize */
		    1,				/* nsegments */
		    MSK_RX_RING_SZ,		/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->msk_cdata.msk_rx_ring_tag);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create Rx ring DMA tag\n");
		goto fail;
	}

	/* Create tag for Tx buffers. */
	error = bus_dma_tag_create(sc_if->msk_cdata.msk_parent_tag,/* parent */
		    1, 0,			/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MSK_TSO_MAXSIZE,		/* maxsize */
		    MSK_MAXTXSEGS,		/* nsegments */
		    MSK_TSO_MAXSGSIZE,		/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->msk_cdata.msk_tx_tag);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create Tx DMA tag\n");
		goto fail;
	}

	rxalign = 1;
	/*
	 * Workaround hardware hang which seems to happen when Rx buffer
	 * is not aligned on multiple of FIFO word(8 bytes).
	 */
	if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) != 0)
		rxalign = MSK_RX_BUF_ALIGN;
	/* Create tag for Rx buffers. */
	error = bus_dma_tag_create(sc_if->msk_cdata.msk_parent_tag,/* parent */
		    rxalign, 0,			/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MCLBYTES,			/* maxsize */
		    1,				/* nsegments */
		    MCLBYTES,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->msk_cdata.msk_rx_tag);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create Rx DMA tag\n");
		goto fail;
	}

	/* Allocate DMA'able memory and load the DMA map for Tx ring. */
	error = bus_dmamem_alloc(sc_if->msk_cdata.msk_tx_ring_tag,
	    (void **)&sc_if->msk_rdata.msk_tx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc_if->msk_cdata.msk_tx_ring_map);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to allocate DMA'able memory for Tx ring\n");
		goto fail;
	}

	ctx.msk_busaddr = 0;
	error = bus_dmamap_load(sc_if->msk_cdata.msk_tx_ring_tag,
	    sc_if->msk_cdata.msk_tx_ring_map, sc_if->msk_rdata.msk_tx_ring,
	    MSK_TX_RING_SZ, msk_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to load DMA'able memory for Tx ring\n");
		goto fail;
	}
	sc_if->msk_rdata.msk_tx_ring_paddr = ctx.msk_busaddr;

	/* Allocate DMA'able memory and load the DMA map for Rx ring. */
	error = bus_dmamem_alloc(sc_if->msk_cdata.msk_rx_ring_tag,
	    (void **)&sc_if->msk_rdata.msk_rx_ring, BUS_DMA_WAITOK |
	    BUS_DMA_COHERENT | BUS_DMA_ZERO, &sc_if->msk_cdata.msk_rx_ring_map);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to allocate DMA'able memory for Rx ring\n");
		goto fail;
	}

	ctx.msk_busaddr = 0;
	error = bus_dmamap_load(sc_if->msk_cdata.msk_rx_ring_tag,
	    sc_if->msk_cdata.msk_rx_ring_map, sc_if->msk_rdata.msk_rx_ring,
	    MSK_RX_RING_SZ, msk_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to load DMA'able memory for Rx ring\n");
		goto fail;
	}
	sc_if->msk_rdata.msk_rx_ring_paddr = ctx.msk_busaddr;

	/* Create DMA maps for Tx buffers. */
	for (i = 0; i < MSK_TX_RING_CNT; i++) {
		txd = &sc_if->msk_cdata.msk_txdesc[i];
		txd->tx_m = NULL;
		txd->tx_dmamap = NULL;
		error = bus_dmamap_create(sc_if->msk_cdata.msk_tx_tag, 0,
		    &txd->tx_dmamap);
		if (error != 0) {
			device_printf(sc_if->msk_if_dev,
			    "failed to create Tx dmamap\n");
			goto fail;
		}
	}
	/* Create DMA maps for Rx buffers. */
	if ((error = bus_dmamap_create(sc_if->msk_cdata.msk_rx_tag, 0,
	    &sc_if->msk_cdata.msk_rx_sparemap)) != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create spare Rx dmamap\n");
		goto fail;
	}
	for (i = 0; i < MSK_RX_RING_CNT; i++) {
		rxd = &sc_if->msk_cdata.msk_rxdesc[i];
		rxd->rx_m = NULL;
		rxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc_if->msk_cdata.msk_rx_tag, 0,
		    &rxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc_if->msk_if_dev,
			    "failed to create Rx dmamap\n");
			goto fail;
		}
	}

fail:
	return (error);
}

static int
msk_rx_dma_jalloc(struct msk_if_softc *sc_if)
{
	struct msk_dmamap_arg ctx;
	struct msk_rxdesc *jrxd;
	bus_size_t rxalign;
	int error, i;

	if (jumbo_disable != 0 || (sc_if->msk_flags & MSK_FLAG_JUMBO) == 0) {
		sc_if->msk_flags &= ~MSK_FLAG_JUMBO;
		device_printf(sc_if->msk_if_dev,
		    "disabling jumbo frame support\n");
		return (0);
	}
	/* Create tag for jumbo Rx ring. */
	error = bus_dma_tag_create(sc_if->msk_cdata.msk_parent_tag,/* parent */
		    MSK_RING_ALIGN, 0,		/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MSK_JUMBO_RX_RING_SZ,	/* maxsize */
		    1,				/* nsegments */
		    MSK_JUMBO_RX_RING_SZ,	/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->msk_cdata.msk_jumbo_rx_ring_tag);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create jumbo Rx ring DMA tag\n");
		goto jumbo_fail;
	}

	rxalign = 1;
	/*
	 * Workaround hardware hang which seems to happen when Rx buffer
	 * is not aligned on multiple of FIFO word(8 bytes).
	 */
	if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) != 0)
		rxalign = MSK_RX_BUF_ALIGN;
	/* Create tag for jumbo Rx buffers. */
	error = bus_dma_tag_create(sc_if->msk_cdata.msk_parent_tag,/* parent */
		    rxalign, 0,			/* alignment, boundary */
		    BUS_SPACE_MAXADDR,		/* lowaddr */
		    BUS_SPACE_MAXADDR,		/* highaddr */
		    NULL, NULL,			/* filter, filterarg */
		    MJUM9BYTES,			/* maxsize */
		    1,				/* nsegments */
		    MJUM9BYTES,			/* maxsegsize */
		    0,				/* flags */
		    NULL, NULL,			/* lockfunc, lockarg */
		    &sc_if->msk_cdata.msk_jumbo_rx_tag);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create jumbo Rx DMA tag\n");
		goto jumbo_fail;
	}

	/* Allocate DMA'able memory and load the DMA map for jumbo Rx ring. */
	error = bus_dmamem_alloc(sc_if->msk_cdata.msk_jumbo_rx_ring_tag,
	    (void **)&sc_if->msk_rdata.msk_jumbo_rx_ring,
	    BUS_DMA_WAITOK | BUS_DMA_COHERENT | BUS_DMA_ZERO,
	    &sc_if->msk_cdata.msk_jumbo_rx_ring_map);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to allocate DMA'able memory for jumbo Rx ring\n");
		goto jumbo_fail;
	}

	ctx.msk_busaddr = 0;
	error = bus_dmamap_load(sc_if->msk_cdata.msk_jumbo_rx_ring_tag,
	    sc_if->msk_cdata.msk_jumbo_rx_ring_map,
	    sc_if->msk_rdata.msk_jumbo_rx_ring, MSK_JUMBO_RX_RING_SZ,
	    msk_dmamap_cb, &ctx, BUS_DMA_NOWAIT);
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to load DMA'able memory for jumbo Rx ring\n");
		goto jumbo_fail;
	}
	sc_if->msk_rdata.msk_jumbo_rx_ring_paddr = ctx.msk_busaddr;

	/* Create DMA maps for jumbo Rx buffers. */
	if ((error = bus_dmamap_create(sc_if->msk_cdata.msk_jumbo_rx_tag, 0,
	    &sc_if->msk_cdata.msk_jumbo_rx_sparemap)) != 0) {
		device_printf(sc_if->msk_if_dev,
		    "failed to create spare jumbo Rx dmamap\n");
		goto jumbo_fail;
	}
	for (i = 0; i < MSK_JUMBO_RX_RING_CNT; i++) {
		jrxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[i];
		jrxd->rx_m = NULL;
		jrxd->rx_dmamap = NULL;
		error = bus_dmamap_create(sc_if->msk_cdata.msk_jumbo_rx_tag, 0,
		    &jrxd->rx_dmamap);
		if (error != 0) {
			device_printf(sc_if->msk_if_dev,
			    "failed to create jumbo Rx dmamap\n");
			goto jumbo_fail;
		}
	}

	return (0);

jumbo_fail:
	msk_rx_dma_jfree(sc_if);
	device_printf(sc_if->msk_if_dev, "disabling jumbo frame support "
	    "due to resource shortage\n");
	sc_if->msk_flags &= ~MSK_FLAG_JUMBO;
	return (error);
}

static void
msk_txrx_dma_free(struct msk_if_softc *sc_if)
{
	struct msk_txdesc *txd;
	struct msk_rxdesc *rxd;
	int i;

	/* Tx ring. */
	if (sc_if->msk_cdata.msk_tx_ring_tag) {
		if (sc_if->msk_rdata.msk_tx_ring_paddr)
			bus_dmamap_unload(sc_if->msk_cdata.msk_tx_ring_tag,
			    sc_if->msk_cdata.msk_tx_ring_map);
		if (sc_if->msk_rdata.msk_tx_ring)
			bus_dmamem_free(sc_if->msk_cdata.msk_tx_ring_tag,
			    sc_if->msk_rdata.msk_tx_ring,
			    sc_if->msk_cdata.msk_tx_ring_map);
		sc_if->msk_rdata.msk_tx_ring = NULL;
		sc_if->msk_rdata.msk_tx_ring_paddr = 0;
		bus_dma_tag_destroy(sc_if->msk_cdata.msk_tx_ring_tag);
		sc_if->msk_cdata.msk_tx_ring_tag = NULL;
	}
	/* Rx ring. */
	if (sc_if->msk_cdata.msk_rx_ring_tag) {
		if (sc_if->msk_rdata.msk_rx_ring_paddr)
			bus_dmamap_unload(sc_if->msk_cdata.msk_rx_ring_tag,
			    sc_if->msk_cdata.msk_rx_ring_map);
		if (sc_if->msk_rdata.msk_rx_ring)
			bus_dmamem_free(sc_if->msk_cdata.msk_rx_ring_tag,
			    sc_if->msk_rdata.msk_rx_ring,
			    sc_if->msk_cdata.msk_rx_ring_map);
		sc_if->msk_rdata.msk_rx_ring = NULL;
		sc_if->msk_rdata.msk_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc_if->msk_cdata.msk_rx_ring_tag);
		sc_if->msk_cdata.msk_rx_ring_tag = NULL;
	}
	/* Tx buffers. */
	if (sc_if->msk_cdata.msk_tx_tag) {
		for (i = 0; i < MSK_TX_RING_CNT; i++) {
			txd = &sc_if->msk_cdata.msk_txdesc[i];
			if (txd->tx_dmamap) {
				bus_dmamap_destroy(sc_if->msk_cdata.msk_tx_tag,
				    txd->tx_dmamap);
				txd->tx_dmamap = NULL;
			}
		}
		bus_dma_tag_destroy(sc_if->msk_cdata.msk_tx_tag);
		sc_if->msk_cdata.msk_tx_tag = NULL;
	}
	/* Rx buffers. */
	if (sc_if->msk_cdata.msk_rx_tag) {
		for (i = 0; i < MSK_RX_RING_CNT; i++) {
			rxd = &sc_if->msk_cdata.msk_rxdesc[i];
			if (rxd->rx_dmamap) {
				bus_dmamap_destroy(sc_if->msk_cdata.msk_rx_tag,
				    rxd->rx_dmamap);
				rxd->rx_dmamap = NULL;
			}
		}
		if (sc_if->msk_cdata.msk_rx_sparemap) {
			bus_dmamap_destroy(sc_if->msk_cdata.msk_rx_tag,
			    sc_if->msk_cdata.msk_rx_sparemap);
			sc_if->msk_cdata.msk_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc_if->msk_cdata.msk_rx_tag);
		sc_if->msk_cdata.msk_rx_tag = NULL;
	}
	if (sc_if->msk_cdata.msk_parent_tag) {
		bus_dma_tag_destroy(sc_if->msk_cdata.msk_parent_tag);
		sc_if->msk_cdata.msk_parent_tag = NULL;
	}
}

static void
msk_rx_dma_jfree(struct msk_if_softc *sc_if)
{
	struct msk_rxdesc *jrxd;
	int i;

	/* Jumbo Rx ring. */
	if (sc_if->msk_cdata.msk_jumbo_rx_ring_tag) {
		if (sc_if->msk_rdata.msk_jumbo_rx_ring_paddr)
			bus_dmamap_unload(sc_if->msk_cdata.msk_jumbo_rx_ring_tag,
			    sc_if->msk_cdata.msk_jumbo_rx_ring_map);
		if (sc_if->msk_rdata.msk_jumbo_rx_ring)
			bus_dmamem_free(sc_if->msk_cdata.msk_jumbo_rx_ring_tag,
			    sc_if->msk_rdata.msk_jumbo_rx_ring,
			    sc_if->msk_cdata.msk_jumbo_rx_ring_map);
		sc_if->msk_rdata.msk_jumbo_rx_ring = NULL;
		sc_if->msk_rdata.msk_jumbo_rx_ring_paddr = 0;
		bus_dma_tag_destroy(sc_if->msk_cdata.msk_jumbo_rx_ring_tag);
		sc_if->msk_cdata.msk_jumbo_rx_ring_tag = NULL;
	}
	/* Jumbo Rx buffers. */
	if (sc_if->msk_cdata.msk_jumbo_rx_tag) {
		for (i = 0; i < MSK_JUMBO_RX_RING_CNT; i++) {
			jrxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[i];
			if (jrxd->rx_dmamap) {
				bus_dmamap_destroy(
				    sc_if->msk_cdata.msk_jumbo_rx_tag,
				    jrxd->rx_dmamap);
				jrxd->rx_dmamap = NULL;
			}
		}
		if (sc_if->msk_cdata.msk_jumbo_rx_sparemap) {
			bus_dmamap_destroy(sc_if->msk_cdata.msk_jumbo_rx_tag,
			    sc_if->msk_cdata.msk_jumbo_rx_sparemap);
			sc_if->msk_cdata.msk_jumbo_rx_sparemap = 0;
		}
		bus_dma_tag_destroy(sc_if->msk_cdata.msk_jumbo_rx_tag);
		sc_if->msk_cdata.msk_jumbo_rx_tag = NULL;
	}
}

static int
msk_encap(struct msk_if_softc *sc_if, struct mbuf **m_head)
{
	struct msk_txdesc *txd, *txd_last;
	struct msk_tx_desc *tx_le;
	struct mbuf *m;
	bus_dmamap_t map;
	bus_dma_segment_t txsegs[MSK_MAXTXSEGS];
	uint32_t control, csum, prod, si;
	uint16_t offset, tcp_offset, tso_mtu;
	int error, i, nseg, tso;

	MSK_IF_LOCK_ASSERT(sc_if);

	tcp_offset = offset = 0;
	m = *m_head;
	if (((sc_if->msk_flags & MSK_FLAG_AUTOTX_CSUM) == 0 &&
	    (m->m_pkthdr.csum_flags & MSK_CSUM_FEATURES) != 0) ||
	    ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0 &&
	    (m->m_pkthdr.csum_flags & CSUM_TSO) != 0)) {
		/*
		 * Since mbuf has no protocol specific structure information
		 * in it we have to inspect protocol information here to
		 * setup TSO and checksum offload. I don't know why Marvell
		 * made a such decision in chip design because other GigE
		 * hardwares normally takes care of all these chores in
		 * hardware. However, TSO performance of Yukon II is very
		 * good such that it's worth to implement it.
		 */
		struct ether_header *eh;
		struct ip *ip;
		struct tcphdr *tcp;

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

		offset = sizeof(struct ether_header);
		m = m_pullup(m, offset);
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		eh = mtod(m, struct ether_header *);
		/* Check if hardware VLAN insertion is off. */
		if (eh->ether_type == htons(ETHERTYPE_VLAN)) {
			offset = sizeof(struct ether_vlan_header);
			m = m_pullup(m, offset);
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
		}
		m = m_pullup(m, offset + sizeof(struct ip));
		if (m == NULL) {
			*m_head = NULL;
			return (ENOBUFS);
		}
		ip = (struct ip *)(mtod(m, char *) + offset);
		offset += (ip->ip_hl << 2);
		tcp_offset = offset;
		if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
			m = m_pullup(m, offset + sizeof(struct tcphdr));
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			tcp = (struct tcphdr *)(mtod(m, char *) + offset);
			offset += (tcp->th_off << 2);
		} else if ((sc_if->msk_flags & MSK_FLAG_AUTOTX_CSUM) == 0 &&
		    (m->m_pkthdr.len < MSK_MIN_FRAMELEN) &&
		    (m->m_pkthdr.csum_flags & CSUM_TCP) != 0) {
			/*
			 * It seems that Yukon II has Tx checksum offload bug
			 * for small TCP packets that's less than 60 bytes in
			 * size (e.g. TCP window probe packet, pure ACK packet).
			 * Common work around like padding with zeros to make
			 * the frame minimum ethernet frame size didn't work at
			 * all.
			 * Instead of disabling checksum offload completely we
			 * resort to S/W checksum routine when we encounter
			 * short TCP frames.
			 * Short UDP packets appear to be handled correctly by
			 * Yukon II. Also I assume this bug does not happen on
			 * controllers that use newer descriptor format or
			 * automatic Tx checksum calculation.
			 */
			m = m_pullup(m, offset + sizeof(struct tcphdr));
			if (m == NULL) {
				*m_head = NULL;
				return (ENOBUFS);
			}
			*(uint16_t *)(m->m_data + offset +
			    m->m_pkthdr.csum_data) = in_cksum_skip(m,
			    m->m_pkthdr.len, offset);
			m->m_pkthdr.csum_flags &= ~CSUM_TCP;
		}
		*m_head = m;
	}

	prod = sc_if->msk_cdata.msk_tx_prod;
	txd = &sc_if->msk_cdata.msk_txdesc[prod];
	txd_last = txd;
	map = txd->tx_dmamap;
	error = bus_dmamap_load_mbuf_sg(sc_if->msk_cdata.msk_tx_tag, map,
	    *m_head, txsegs, &nseg, BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		m = m_collapse(*m_head, M_NOWAIT, MSK_MAXTXSEGS);
		if (m == NULL) {
			m_freem(*m_head);
			*m_head = NULL;
			return (ENOBUFS);
		}
		*m_head = m;
		error = bus_dmamap_load_mbuf_sg(sc_if->msk_cdata.msk_tx_tag,
		    map, *m_head, txsegs, &nseg, BUS_DMA_NOWAIT);
		if (error != 0) {
			m_freem(*m_head);
			*m_head = NULL;
			return (error);
		}
	} else if (error != 0)
		return (error);
	if (nseg == 0) {
		m_freem(*m_head);
		*m_head = NULL;
		return (EIO);
	}

	/* Check number of available descriptors. */
	if (sc_if->msk_cdata.msk_tx_cnt + nseg >=
	    (MSK_TX_RING_CNT - MSK_RESERVED_TX_DESC_CNT)) {
		bus_dmamap_unload(sc_if->msk_cdata.msk_tx_tag, map);
		return (ENOBUFS);
	}

	control = 0;
	tso = 0;
	tx_le = NULL;

	/* Check TSO support. */
	if ((m->m_pkthdr.csum_flags & CSUM_TSO) != 0) {
		if ((sc_if->msk_flags & MSK_FLAG_DESCV2) != 0)
			tso_mtu = m->m_pkthdr.tso_segsz;
		else
			tso_mtu = offset + m->m_pkthdr.tso_segsz;
		if (tso_mtu != sc_if->msk_cdata.msk_tso_mtu) {
			tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
			tx_le->msk_addr = htole32(tso_mtu);
			if ((sc_if->msk_flags & MSK_FLAG_DESCV2) != 0)
				tx_le->msk_control = htole32(OP_MSS | HW_OWNER);
			else
				tx_le->msk_control =
				    htole32(OP_LRGLEN | HW_OWNER);
			sc_if->msk_cdata.msk_tx_cnt++;
			MSK_INC(prod, MSK_TX_RING_CNT);
			sc_if->msk_cdata.msk_tso_mtu = tso_mtu;
		}
		tso++;
	}
	/* Check if we have a VLAN tag to insert. */
	if ((m->m_flags & M_VLANTAG) != 0) {
		if (tx_le == NULL) {
			tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
			tx_le->msk_addr = htole32(0);
			tx_le->msk_control = htole32(OP_VLAN | HW_OWNER |
			    htons(m->m_pkthdr.ether_vtag));
			sc_if->msk_cdata.msk_tx_cnt++;
			MSK_INC(prod, MSK_TX_RING_CNT);
		} else {
			tx_le->msk_control |= htole32(OP_VLAN |
			    htons(m->m_pkthdr.ether_vtag));
		}
		control |= INS_VLAN;
	}
	/* Check if we have to handle checksum offload. */
	if (tso == 0 && (m->m_pkthdr.csum_flags & MSK_CSUM_FEATURES) != 0) {
		if ((sc_if->msk_flags & MSK_FLAG_AUTOTX_CSUM) != 0)
			control |= CALSUM;
		else {
			control |= CALSUM | WR_SUM | INIT_SUM | LOCK_SUM;
			if ((m->m_pkthdr.csum_flags & CSUM_UDP) != 0)
				control |= UDPTCP;
			/* Checksum write position. */
			csum = (tcp_offset + m->m_pkthdr.csum_data) & 0xffff;
			/* Checksum start position. */
			csum |= (uint32_t)tcp_offset << 16;
			if (csum != sc_if->msk_cdata.msk_last_csum) {
				tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
				tx_le->msk_addr = htole32(csum);
				tx_le->msk_control = htole32(1 << 16 |
				    (OP_TCPLISW | HW_OWNER));
				sc_if->msk_cdata.msk_tx_cnt++;
				MSK_INC(prod, MSK_TX_RING_CNT);
				sc_if->msk_cdata.msk_last_csum = csum;
			}
		}
	}

#ifdef MSK_64BIT_DMA
	if (MSK_ADDR_HI(txsegs[0].ds_addr) !=
	    sc_if->msk_cdata.msk_tx_high_addr) {
		sc_if->msk_cdata.msk_tx_high_addr =
		    MSK_ADDR_HI(txsegs[0].ds_addr);
		tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
		tx_le->msk_addr = htole32(MSK_ADDR_HI(txsegs[0].ds_addr));
		tx_le->msk_control = htole32(OP_ADDR64 | HW_OWNER);
		sc_if->msk_cdata.msk_tx_cnt++;
		MSK_INC(prod, MSK_TX_RING_CNT);
	}
#endif
	si = prod;
	tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
	tx_le->msk_addr = htole32(MSK_ADDR_LO(txsegs[0].ds_addr));
	if (tso == 0)
		tx_le->msk_control = htole32(txsegs[0].ds_len | control |
		    OP_PACKET);
	else
		tx_le->msk_control = htole32(txsegs[0].ds_len | control |
		    OP_LARGESEND);
	sc_if->msk_cdata.msk_tx_cnt++;
	MSK_INC(prod, MSK_TX_RING_CNT);

	for (i = 1; i < nseg; i++) {
		tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
#ifdef MSK_64BIT_DMA
		if (MSK_ADDR_HI(txsegs[i].ds_addr) !=
		    sc_if->msk_cdata.msk_tx_high_addr) {
			sc_if->msk_cdata.msk_tx_high_addr =
			    MSK_ADDR_HI(txsegs[i].ds_addr);
			tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
			tx_le->msk_addr =
			    htole32(MSK_ADDR_HI(txsegs[i].ds_addr));
			tx_le->msk_control = htole32(OP_ADDR64 | HW_OWNER);
			sc_if->msk_cdata.msk_tx_cnt++;
			MSK_INC(prod, MSK_TX_RING_CNT);
			tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
		}
#endif
		tx_le->msk_addr = htole32(MSK_ADDR_LO(txsegs[i].ds_addr));
		tx_le->msk_control = htole32(txsegs[i].ds_len | control |
		    OP_BUFFER | HW_OWNER);
		sc_if->msk_cdata.msk_tx_cnt++;
		MSK_INC(prod, MSK_TX_RING_CNT);
	}
	/* Update producer index. */
	sc_if->msk_cdata.msk_tx_prod = prod;

	/* Set EOP on the last descriptor. */
	prod = (prod + MSK_TX_RING_CNT - 1) % MSK_TX_RING_CNT;
	tx_le = &sc_if->msk_rdata.msk_tx_ring[prod];
	tx_le->msk_control |= htole32(EOP);

	/* Turn the first descriptor ownership to hardware. */
	tx_le = &sc_if->msk_rdata.msk_tx_ring[si];
	tx_le->msk_control |= htole32(HW_OWNER);

	txd = &sc_if->msk_cdata.msk_txdesc[prod];
	map = txd_last->tx_dmamap;
	txd_last->tx_dmamap = txd->tx_dmamap;
	txd->tx_dmamap = map;
	txd->tx_m = m;

	/* Sync descriptors. */
	bus_dmamap_sync(sc_if->msk_cdata.msk_tx_tag, map, BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(sc_if->msk_cdata.msk_tx_ring_tag,
	    sc_if->msk_cdata.msk_tx_ring_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

static void
msk_start(struct ifnet *ifp)
{
	struct msk_if_softc *sc_if;

	sc_if = ifp->if_softc;
	MSK_IF_LOCK(sc_if);
	msk_start_locked(ifp);
	MSK_IF_UNLOCK(sc_if);
}

static void
msk_start_locked(struct ifnet *ifp)
{
	struct msk_if_softc *sc_if;
	struct mbuf *m_head;
	int enq;

	sc_if = ifp->if_softc;
	MSK_IF_LOCK_ASSERT(sc_if);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc_if->msk_flags & MSK_FLAG_LINK) == 0)
		return;

	for (enq = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    sc_if->msk_cdata.msk_tx_cnt <
	    (MSK_TX_RING_CNT - MSK_RESERVED_TX_DESC_CNT); ) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;
		/*
		 * Pack the data into the transmit ring. If we
		 * don't have room, set the OACTIVE flag and wait
		 * for the NIC to drain the ring.
		 */
		if (msk_encap(sc_if, &m_head) != 0) {
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
		CSR_WRITE_2(sc_if->msk_softc,
		    Y2_PREF_Q_ADDR(sc_if->msk_txq, PREF_UNIT_PUT_IDX_REG),
		    sc_if->msk_cdata.msk_tx_prod);

		/* Set a timeout in case the chip goes out to lunch. */
		sc_if->msk_watchdog_timer = MSK_TX_TIMEOUT;
	}
}

static void
msk_watchdog(struct msk_if_softc *sc_if)
{
	struct ifnet *ifp;

	MSK_IF_LOCK_ASSERT(sc_if);

	if (sc_if->msk_watchdog_timer == 0 || --sc_if->msk_watchdog_timer)
		return;
	ifp = sc_if->msk_ifp;
	if ((sc_if->msk_flags & MSK_FLAG_LINK) == 0) {
		if (bootverbose)
			if_printf(sc_if->msk_ifp, "watchdog timeout "
			   "(missed link)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		msk_init_locked(sc_if);
		return;
	}

	if_printf(ifp, "watchdog timeout\n");
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	msk_init_locked(sc_if);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		msk_start_locked(ifp);
}

static int
mskc_shutdown(device_t dev)
{
	struct msk_softc *sc;
	int i;

	sc = device_get_softc(dev);
	MSK_LOCK(sc);
	for (i = 0; i < sc->msk_num_port; i++) {
		if (sc->msk_if[i] != NULL && sc->msk_if[i]->msk_ifp != NULL &&
		    ((sc->msk_if[i]->msk_ifp->if_drv_flags &
		    IFF_DRV_RUNNING) != 0))
			msk_stop(sc->msk_if[i]);
	}
	MSK_UNLOCK(sc);

	/* Put hardware reset. */
	CSR_WRITE_2(sc, B0_CTST, CS_RST_SET);
	return (0);
}

static int
mskc_suspend(device_t dev)
{
	struct msk_softc *sc;
	int i;

	sc = device_get_softc(dev);

	MSK_LOCK(sc);

	for (i = 0; i < sc->msk_num_port; i++) {
		if (sc->msk_if[i] != NULL && sc->msk_if[i]->msk_ifp != NULL &&
		    ((sc->msk_if[i]->msk_ifp->if_drv_flags &
		    IFF_DRV_RUNNING) != 0))
			msk_stop(sc->msk_if[i]);
	}

	/* Disable all interrupts. */
	CSR_WRITE_4(sc, B0_IMSK, 0);
	CSR_READ_4(sc, B0_IMSK);
	CSR_WRITE_4(sc, B0_HWE_IMSK, 0);
	CSR_READ_4(sc, B0_HWE_IMSK);

	msk_phy_power(sc, MSK_PHY_POWERDOWN);

	/* Put hardware reset. */
	CSR_WRITE_2(sc, B0_CTST, CS_RST_SET);
	sc->msk_pflags |= MSK_FLAG_SUSPEND;

	MSK_UNLOCK(sc);

	return (0);
}

static int
mskc_resume(device_t dev)
{
	struct msk_softc *sc;
	int i;

	sc = device_get_softc(dev);

	MSK_LOCK(sc);

	CSR_PCI_WRITE_4(sc, PCI_OUR_REG_3, 0);
	mskc_reset(sc);
	for (i = 0; i < sc->msk_num_port; i++) {
		if (sc->msk_if[i] != NULL && sc->msk_if[i]->msk_ifp != NULL &&
		    ((sc->msk_if[i]->msk_ifp->if_flags & IFF_UP) != 0)) {
			sc->msk_if[i]->msk_ifp->if_drv_flags &=
			    ~IFF_DRV_RUNNING;
			msk_init_locked(sc->msk_if[i]);
		}
	}
	sc->msk_pflags &= ~MSK_FLAG_SUSPEND;

	MSK_UNLOCK(sc);

	return (0);
}

#ifndef __NO_STRICT_ALIGNMENT
static __inline void
msk_fixup_rx(struct mbuf *m)
{
        int i;
        uint16_t *src, *dst;

	src = mtod(m, uint16_t *);
	dst = src - 3;

	for (i = 0; i < (m->m_len / sizeof(uint16_t) + 1); i++)
		*dst++ = *src++;

	m->m_data -= (MSK_RX_BUF_ALIGN - ETHER_ALIGN);
}
#endif

static __inline void
msk_rxcsum(struct msk_if_softc *sc_if, uint32_t control, struct mbuf *m)
{
	struct ether_header *eh;
	struct ip *ip;
	struct udphdr *uh;
	int32_t hlen, len, pktlen, temp32;
	uint16_t csum, *opts;

	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) != 0) {
		if ((control & (CSS_IPV4 | CSS_IPFRAG)) == CSS_IPV4) {
			m->m_pkthdr.csum_flags |= CSUM_IP_CHECKED;
			if ((control & CSS_IPV4_CSUM_OK) != 0)
				m->m_pkthdr.csum_flags |= CSUM_IP_VALID;
			if ((control & (CSS_TCP | CSS_UDP)) != 0 &&
			    (control & (CSS_TCPUDP_CSUM_OK)) != 0) {
				m->m_pkthdr.csum_flags |= CSUM_DATA_VALID |
				    CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}
		return;
	}
	/*
	 * Marvell Yukon controllers that support OP_RXCHKS has known
	 * to have various Rx checksum offloading bugs. These
	 * controllers can be configured to compute simple checksum
	 * at two different positions. So we can compute IP and TCP/UDP
	 * checksum at the same time. We intentionally have controller
	 * compute TCP/UDP checksum twice by specifying the same
	 * checksum start position and compare the result. If the value
	 * is different it would indicate the hardware logic was wrong.
	 */
	if ((sc_if->msk_csum & 0xFFFF) != (sc_if->msk_csum >> 16)) {
		if (bootverbose)
			device_printf(sc_if->msk_if_dev,
			    "Rx checksum value mismatch!\n");
		return;
	}
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
		return;	/* can't handle fragmented packet. */

	switch (ip->ip_p) {
	case IPPROTO_TCP:
		if (pktlen < (hlen + sizeof(struct tcphdr)))
			return;
		break;
	case IPPROTO_UDP:
		if (pktlen < (hlen + sizeof(struct udphdr)))
			return;
		uh = (struct udphdr *)((caddr_t)ip + hlen);
		if (uh->uh_sum == 0)
			return; /* no checksum */
		break;
	default:
		return;
	}
	csum = bswap16(sc_if->msk_csum & 0xFFFF);
	/* Checksum fixup for IP options. */
	len = hlen - sizeof(struct ip);
	if (len > 0) {
		opts = (uint16_t *)(ip + 1);
		for (; len > 0; len -= sizeof(uint16_t), opts++) {
			temp32 = csum - *opts;
			temp32 = (temp32 >> 16) + (temp32 & 65535);
			csum = temp32 & 65535;
		}
	}
	m->m_pkthdr.csum_flags |= CSUM_DATA_VALID;
	m->m_pkthdr.csum_data = csum;
}

static void
msk_rxeof(struct msk_if_softc *sc_if, uint32_t status, uint32_t control,
    int len)
{
	struct mbuf *m;
	struct ifnet *ifp;
	struct msk_rxdesc *rxd;
	int cons, rxlen;

	ifp = sc_if->msk_ifp;

	MSK_IF_LOCK_ASSERT(sc_if);

	cons = sc_if->msk_cdata.msk_rx_cons;
	do {
		rxlen = status >> 16;
		if ((status & GMR_FS_VLAN) != 0 &&
		    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
			rxlen -= ETHER_VLAN_ENCAP_LEN;
		if ((sc_if->msk_flags & MSK_FLAG_NORXCHK) != 0) {
			/*
			 * For controllers that returns bogus status code
			 * just do minimal check and let upper stack
			 * handle this frame.
			 */
			if (len > MSK_MAX_FRAMELEN || len < ETHER_HDR_LEN) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				msk_discard_rxbuf(sc_if, cons);
				break;
			}
		} else if (len > sc_if->msk_framesize ||
		    ((status & GMR_FS_ANY_ERR) != 0) ||
		    ((status & GMR_FS_RX_OK) == 0) || (rxlen != len)) {
			/* Don't count flow-control packet as errors. */
			if ((status & GMR_FS_GOOD_FC) == 0)
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			msk_discard_rxbuf(sc_if, cons);
			break;
		}
#ifdef MSK_64BIT_DMA
		rxd = &sc_if->msk_cdata.msk_rxdesc[(cons + 1) %
		    MSK_RX_RING_CNT];
#else
		rxd = &sc_if->msk_cdata.msk_rxdesc[cons];
#endif
		m = rxd->rx_m;
		if (msk_newbuf(sc_if, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/* Reuse old buffer. */
			msk_discard_rxbuf(sc_if, cons);
			break;
		}
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = len;
#ifndef __NO_STRICT_ALIGNMENT
		if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) != 0)
			msk_fixup_rx(m);
#endif
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
			msk_rxcsum(sc_if, control, m);
		/* Check for VLAN tagged packets. */
		if ((status & GMR_FS_VLAN) != 0 &&
		    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0) {
			m->m_pkthdr.ether_vtag = sc_if->msk_vtag;
			m->m_flags |= M_VLANTAG;
		}
		MSK_IF_UNLOCK(sc_if);
		(*ifp->if_input)(ifp, m);
		MSK_IF_LOCK(sc_if);
	} while (0);

	MSK_RX_INC(sc_if->msk_cdata.msk_rx_cons, MSK_RX_RING_CNT);
	MSK_RX_INC(sc_if->msk_cdata.msk_rx_prod, MSK_RX_RING_CNT);
}

static void
msk_jumbo_rxeof(struct msk_if_softc *sc_if, uint32_t status, uint32_t control,
    int len)
{
	struct mbuf *m;
	struct ifnet *ifp;
	struct msk_rxdesc *jrxd;
	int cons, rxlen;

	ifp = sc_if->msk_ifp;

	MSK_IF_LOCK_ASSERT(sc_if);

	cons = sc_if->msk_cdata.msk_rx_cons;
	do {
		rxlen = status >> 16;
		if ((status & GMR_FS_VLAN) != 0 &&
		    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0)
			rxlen -= ETHER_VLAN_ENCAP_LEN;
		if (len > sc_if->msk_framesize ||
		    ((status & GMR_FS_ANY_ERR) != 0) ||
		    ((status & GMR_FS_RX_OK) == 0) || (rxlen != len)) {
			/* Don't count flow-control packet as errors. */
			if ((status & GMR_FS_GOOD_FC) == 0)
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			msk_discard_jumbo_rxbuf(sc_if, cons);
			break;
		}
#ifdef MSK_64BIT_DMA
		jrxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[(cons + 1) %
		    MSK_JUMBO_RX_RING_CNT];
#else
		jrxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[cons];
#endif
		m = jrxd->rx_m;
		if (msk_jumbo_newbuf(sc_if, cons) != 0) {
			if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
			/* Reuse old buffer. */
			msk_discard_jumbo_rxbuf(sc_if, cons);
			break;
		}
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = len;
#ifndef __NO_STRICT_ALIGNMENT
		if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) != 0)
			msk_fixup_rx(m);
#endif
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if ((ifp->if_capenable & IFCAP_RXCSUM) != 0)
			msk_rxcsum(sc_if, control, m);
		/* Check for VLAN tagged packets. */
		if ((status & GMR_FS_VLAN) != 0 &&
		    (ifp->if_capenable & IFCAP_VLAN_HWTAGGING) != 0) {
			m->m_pkthdr.ether_vtag = sc_if->msk_vtag;
			m->m_flags |= M_VLANTAG;
		}
		MSK_IF_UNLOCK(sc_if);
		(*ifp->if_input)(ifp, m);
		MSK_IF_LOCK(sc_if);
	} while (0);

	MSK_RX_INC(sc_if->msk_cdata.msk_rx_cons, MSK_JUMBO_RX_RING_CNT);
	MSK_RX_INC(sc_if->msk_cdata.msk_rx_prod, MSK_JUMBO_RX_RING_CNT);
}

static void
msk_txeof(struct msk_if_softc *sc_if, int idx)
{
	struct msk_txdesc *txd;
	struct msk_tx_desc *cur_tx;
	struct ifnet *ifp;
	uint32_t control;
	int cons, prog;

	MSK_IF_LOCK_ASSERT(sc_if);

	ifp = sc_if->msk_ifp;

	bus_dmamap_sync(sc_if->msk_cdata.msk_tx_ring_tag,
	    sc_if->msk_cdata.msk_tx_ring_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	/*
	 * Go through our tx ring and free mbufs for those
	 * frames that have been sent.
	 */
	cons = sc_if->msk_cdata.msk_tx_cons;
	prog = 0;
	for (; cons != idx; MSK_INC(cons, MSK_TX_RING_CNT)) {
		if (sc_if->msk_cdata.msk_tx_cnt <= 0)
			break;
		prog++;
		cur_tx = &sc_if->msk_rdata.msk_tx_ring[cons];
		control = le32toh(cur_tx->msk_control);
		sc_if->msk_cdata.msk_tx_cnt--;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		if ((control & EOP) == 0)
			continue;
		txd = &sc_if->msk_cdata.msk_txdesc[cons];
		bus_dmamap_sync(sc_if->msk_cdata.msk_tx_tag, txd->tx_dmamap,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc_if->msk_cdata.msk_tx_tag, txd->tx_dmamap);

		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		KASSERT(txd->tx_m != NULL, ("%s: freeing NULL mbuf!",
		    __func__));
		m_freem(txd->tx_m);
		txd->tx_m = NULL;
	}

	if (prog > 0) {
		sc_if->msk_cdata.msk_tx_cons = cons;
		if (sc_if->msk_cdata.msk_tx_cnt == 0)
			sc_if->msk_watchdog_timer = 0;
		/* No need to sync LEs as we didn't update LEs. */
	}
}

static void
msk_tick(void *xsc_if)
{
	struct msk_if_softc *sc_if;
	struct mii_data *mii;

	sc_if = xsc_if;

	MSK_IF_LOCK_ASSERT(sc_if);

	mii = device_get_softc(sc_if->msk_miibus);

	mii_tick(mii);
	if ((sc_if->msk_flags & MSK_FLAG_LINK) == 0)
		msk_miibus_statchg(sc_if->msk_if_dev);
	msk_handle_events(sc_if->msk_softc);
	msk_watchdog(sc_if);
	callout_reset(&sc_if->msk_tick_ch, hz, msk_tick, sc_if);
}

static void
msk_intr_phy(struct msk_if_softc *sc_if)
{
	uint16_t status;

	msk_phy_readreg(sc_if, PHY_ADDR_MARV, PHY_MARV_INT_STAT);
	status = msk_phy_readreg(sc_if, PHY_ADDR_MARV, PHY_MARV_INT_STAT);
	/* Handle FIFO Underrun/Overflow? */
	if ((status & PHY_M_IS_FIFO_ERROR))
		device_printf(sc_if->msk_if_dev,
		    "PHY FIFO underrun/overflow.\n");
}

static void
msk_intr_gmac(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	uint8_t status;

	sc = sc_if->msk_softc;
	status = CSR_READ_1(sc, MR_ADDR(sc_if->msk_port, GMAC_IRQ_SRC));

	/* GMAC Rx FIFO overrun. */
	if ((status & GM_IS_RX_FF_OR) != 0)
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T),
		    GMF_CLI_RX_FO);
	/* GMAC Tx FIFO underrun. */
	if ((status & GM_IS_TX_FF_UR) != 0) {
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T),
		    GMF_CLI_TX_FU);
		device_printf(sc_if->msk_if_dev, "Tx FIFO underrun!\n");
		/*
		 * XXX
		 * In case of Tx underrun, we may need to flush/reset
		 * Tx MAC but that would also require resynchronization
		 * with status LEs. Reinitializing status LEs would
		 * affect other port in dual MAC configuration so it
		 * should be avoided as possible as we can.
		 * Due to lack of documentation it's all vague guess but
		 * it needs more investigation.
		 */
	}
}

static void
msk_handle_hwerr(struct msk_if_softc *sc_if, uint32_t status)
{
	struct msk_softc *sc;

	sc = sc_if->msk_softc;
	if ((status & Y2_IS_PAR_RD1) != 0) {
		device_printf(sc_if->msk_if_dev,
		    "RAM buffer read parity error\n");
		/* Clear IRQ. */
		CSR_WRITE_2(sc, SELECT_RAM_BUFFER(sc_if->msk_port, B3_RI_CTRL),
		    RI_CLR_RD_PERR);
	}
	if ((status & Y2_IS_PAR_WR1) != 0) {
		device_printf(sc_if->msk_if_dev,
		    "RAM buffer write parity error\n");
		/* Clear IRQ. */
		CSR_WRITE_2(sc, SELECT_RAM_BUFFER(sc_if->msk_port, B3_RI_CTRL),
		    RI_CLR_WR_PERR);
	}
	if ((status & Y2_IS_PAR_MAC1) != 0) {
		device_printf(sc_if->msk_if_dev, "Tx MAC parity error\n");
		/* Clear IRQ. */
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T),
		    GMF_CLI_TX_PE);
	}
	if ((status & Y2_IS_PAR_RX1) != 0) {
		device_printf(sc_if->msk_if_dev, "Rx parity error\n");
		/* Clear IRQ. */
		CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_rxq, Q_CSR), BMU_CLR_IRQ_PAR);
	}
	if ((status & (Y2_IS_TCP_TXS1 | Y2_IS_TCP_TXA1)) != 0) {
		device_printf(sc_if->msk_if_dev, "TCP segmentation error\n");
		/* Clear IRQ. */
		CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR), BMU_CLR_IRQ_TCP);
	}
}

static void
msk_intr_hwerr(struct msk_softc *sc)
{
	uint32_t status;
	uint32_t tlphead[4];

	status = CSR_READ_4(sc, B0_HWE_ISRC);
	/* Time Stamp timer overflow. */
	if ((status & Y2_IS_TIST_OV) != 0)
		CSR_WRITE_1(sc, GMAC_TI_ST_CTRL, GMT_ST_CLR_IRQ);
	if ((status & Y2_IS_PCI_NEXP) != 0) {
		/*
		 * PCI Express Error occurred which is not described in PEX
		 * spec.
		 * This error is also mapped either to Master Abort(
		 * Y2_IS_MST_ERR) or Target Abort (Y2_IS_IRQ_STAT) bit and
		 * can only be cleared there.
                 */
		device_printf(sc->msk_dev,
		    "PCI Express protocol violation error\n");
	}

	if ((status & (Y2_IS_MST_ERR | Y2_IS_IRQ_STAT)) != 0) {
		uint16_t v16;

		if ((status & Y2_IS_MST_ERR) != 0)
			device_printf(sc->msk_dev,
			    "unexpected IRQ Status error\n");
		else
			device_printf(sc->msk_dev,
			    "unexpected IRQ Master error\n");
		/* Reset all bits in the PCI status register. */
		v16 = pci_read_config(sc->msk_dev, PCIR_STATUS, 2);
		CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		pci_write_config(sc->msk_dev, PCIR_STATUS, v16 |
		    PCIM_STATUS_PERR | PCIM_STATUS_SERR | PCIM_STATUS_RMABORT |
		    PCIM_STATUS_RTABORT | PCIM_STATUS_MDPERR, 2);
		CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	}

	/* Check for PCI Express Uncorrectable Error. */
	if ((status & Y2_IS_PCI_EXP) != 0) {
		uint32_t v32;

		/*
		 * On PCI Express bus bridges are called root complexes (RC).
		 * PCI Express errors are recognized by the root complex too,
		 * which requests the system to handle the problem. After
		 * error occurrence it may be that no access to the adapter
		 * may be performed any longer.
		 */

		v32 = CSR_PCI_READ_4(sc, PEX_UNC_ERR_STAT);
		if ((v32 & PEX_UNSUP_REQ) != 0) {
			/* Ignore unsupported request error. */
			device_printf(sc->msk_dev,
			    "Uncorrectable PCI Express error\n");
		}
		if ((v32 & (PEX_FATAL_ERRORS | PEX_POIS_TLP)) != 0) {
			int i;

			/* Get TLP header form Log Registers. */
			for (i = 0; i < 4; i++)
				tlphead[i] = CSR_PCI_READ_4(sc,
				    PEX_HEADER_LOG + i * 4);
			/* Check for vendor defined broadcast message. */
			if (!(tlphead[0] == 0x73004001 && tlphead[1] == 0x7f)) {
				sc->msk_intrhwemask &= ~Y2_IS_PCI_EXP;
				CSR_WRITE_4(sc, B0_HWE_IMSK,
				    sc->msk_intrhwemask);
				CSR_READ_4(sc, B0_HWE_IMSK);
			}
		}
		/* Clear the interrupt. */
		CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_ON);
		CSR_PCI_WRITE_4(sc, PEX_UNC_ERR_STAT, 0xffffffff);
		CSR_WRITE_1(sc, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
	}

	if ((status & Y2_HWE_L1_MASK) != 0 && sc->msk_if[MSK_PORT_A] != NULL)
		msk_handle_hwerr(sc->msk_if[MSK_PORT_A], status);
	if ((status & Y2_HWE_L2_MASK) != 0 && sc->msk_if[MSK_PORT_B] != NULL)
		msk_handle_hwerr(sc->msk_if[MSK_PORT_B], status >> 8);
}

static __inline void
msk_rxput(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;

	sc = sc_if->msk_softc;
	if (sc_if->msk_framesize > (MCLBYTES - MSK_RX_BUF_ALIGN))
		bus_dmamap_sync(
		    sc_if->msk_cdata.msk_jumbo_rx_ring_tag,
		    sc_if->msk_cdata.msk_jumbo_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	else
		bus_dmamap_sync(
		    sc_if->msk_cdata.msk_rx_ring_tag,
		    sc_if->msk_cdata.msk_rx_ring_map,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	CSR_WRITE_2(sc, Y2_PREF_Q_ADDR(sc_if->msk_rxq,
	    PREF_UNIT_PUT_IDX_REG), sc_if->msk_cdata.msk_rx_prod);
}

static int
msk_handle_events(struct msk_softc *sc)
{
	struct msk_if_softc *sc_if;
	int rxput[2];
	struct msk_stat_desc *sd;
	uint32_t control, status;
	int cons, len, port, rxprog;

	if (sc->msk_stat_cons == CSR_READ_2(sc, STAT_PUT_IDX))
		return (0);

	/* Sync status LEs. */
	bus_dmamap_sync(sc->msk_stat_tag, sc->msk_stat_map,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	rxput[MSK_PORT_A] = rxput[MSK_PORT_B] = 0;
	rxprog = 0;
	cons = sc->msk_stat_cons;
	for (;;) {
		sd = &sc->msk_stat_ring[cons];
		control = le32toh(sd->msk_control);
		if ((control & HW_OWNER) == 0)
			break;
		control &= ~HW_OWNER;
		sd->msk_control = htole32(control);
		status = le32toh(sd->msk_status);
		len = control & STLE_LEN_MASK;
		port = (control >> 16) & 0x01;
		sc_if = sc->msk_if[port];
		if (sc_if == NULL) {
			device_printf(sc->msk_dev, "invalid port opcode "
			    "0x%08x\n", control & STLE_OP_MASK);
			continue;
		}

		switch (control & STLE_OP_MASK) {
		case OP_RXVLAN:
			sc_if->msk_vtag = ntohs(len);
			break;
		case OP_RXCHKSVLAN:
			sc_if->msk_vtag = ntohs(len);
			/* FALLTHROUGH */
		case OP_RXCHKS:
			sc_if->msk_csum = status;
			break;
		case OP_RXSTAT:
			if (!(sc_if->msk_ifp->if_drv_flags & IFF_DRV_RUNNING))
				break;
			if (sc_if->msk_framesize >
			    (MCLBYTES - MSK_RX_BUF_ALIGN))
				msk_jumbo_rxeof(sc_if, status, control, len);
			else
				msk_rxeof(sc_if, status, control, len);
			rxprog++;
			/*
			 * Because there is no way to sync single Rx LE
			 * put the DMA sync operation off until the end of
			 * event processing.
			 */
			rxput[port]++;
			/* Update prefetch unit if we've passed water mark. */
			if (rxput[port] >= sc_if->msk_cdata.msk_rx_putwm) {
				msk_rxput(sc_if);
				rxput[port] = 0;
			}
			break;
		case OP_TXINDEXLE:
			if (sc->msk_if[MSK_PORT_A] != NULL)
				msk_txeof(sc->msk_if[MSK_PORT_A],
				    status & STLE_TXA1_MSKL);
			if (sc->msk_if[MSK_PORT_B] != NULL)
				msk_txeof(sc->msk_if[MSK_PORT_B],
				    ((status & STLE_TXA2_MSKL) >>
				    STLE_TXA2_SHIFTL) |
				    ((len & STLE_TXA2_MSKH) <<
				    STLE_TXA2_SHIFTH));
			break;
		default:
			device_printf(sc->msk_dev, "unhandled opcode 0x%08x\n",
			    control & STLE_OP_MASK);
			break;
		}
		MSK_INC(cons, sc->msk_stat_count);
		if (rxprog > sc->msk_process_limit)
			break;
	}

	sc->msk_stat_cons = cons;
	bus_dmamap_sync(sc->msk_stat_tag, sc->msk_stat_map,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (rxput[MSK_PORT_A] > 0)
		msk_rxput(sc->msk_if[MSK_PORT_A]);
	if (rxput[MSK_PORT_B] > 0)
		msk_rxput(sc->msk_if[MSK_PORT_B]);

	return (sc->msk_stat_cons != CSR_READ_2(sc, STAT_PUT_IDX));
}

static void
msk_intr(void *xsc)
{
	struct msk_softc *sc;
	struct msk_if_softc *sc_if0, *sc_if1;
	struct ifnet *ifp0, *ifp1;
	uint32_t status;
	int domore;

	sc = xsc;
	MSK_LOCK(sc);

	/* Reading B0_Y2_SP_ISRC2 masks further interrupts. */
	status = CSR_READ_4(sc, B0_Y2_SP_ISRC2);
	if (status == 0 || status == 0xffffffff ||
	    (sc->msk_pflags & MSK_FLAG_SUSPEND) != 0 ||
	    (status & sc->msk_intrmask) == 0) {
		CSR_WRITE_4(sc, B0_Y2_SP_ICR, 2);
		MSK_UNLOCK(sc);
		return;
	}

	sc_if0 = sc->msk_if[MSK_PORT_A];
	sc_if1 = sc->msk_if[MSK_PORT_B];
	ifp0 = ifp1 = NULL;
	if (sc_if0 != NULL)
		ifp0 = sc_if0->msk_ifp;
	if (sc_if1 != NULL)
		ifp1 = sc_if1->msk_ifp;

	if ((status & Y2_IS_IRQ_PHY1) != 0 && sc_if0 != NULL)
		msk_intr_phy(sc_if0);
	if ((status & Y2_IS_IRQ_PHY2) != 0 && sc_if1 != NULL)
		msk_intr_phy(sc_if1);
	if ((status & Y2_IS_IRQ_MAC1) != 0 && sc_if0 != NULL)
		msk_intr_gmac(sc_if0);
	if ((status & Y2_IS_IRQ_MAC2) != 0 && sc_if1 != NULL)
		msk_intr_gmac(sc_if1);
	if ((status & (Y2_IS_CHK_RX1 | Y2_IS_CHK_RX2)) != 0) {
		device_printf(sc->msk_dev, "Rx descriptor error\n");
		sc->msk_intrmask &= ~(Y2_IS_CHK_RX1 | Y2_IS_CHK_RX2);
		CSR_WRITE_4(sc, B0_IMSK, sc->msk_intrmask);
		CSR_READ_4(sc, B0_IMSK);
	}
        if ((status & (Y2_IS_CHK_TXA1 | Y2_IS_CHK_TXA2)) != 0) {
		device_printf(sc->msk_dev, "Tx descriptor error\n");
		sc->msk_intrmask &= ~(Y2_IS_CHK_TXA1 | Y2_IS_CHK_TXA2);
		CSR_WRITE_4(sc, B0_IMSK, sc->msk_intrmask);
		CSR_READ_4(sc, B0_IMSK);
	}
	if ((status & Y2_IS_HW_ERR) != 0)
		msk_intr_hwerr(sc);

	domore = msk_handle_events(sc);
	if ((status & Y2_IS_STAT_BMU) != 0 && domore == 0)
		CSR_WRITE_4(sc, STAT_CTRL, SC_STAT_CLR_IRQ);

	/* Reenable interrupts. */
	CSR_WRITE_4(sc, B0_Y2_SP_ICR, 2);

	if (ifp0 != NULL && (ifp0->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
	    !IFQ_DRV_IS_EMPTY(&ifp0->if_snd))
		msk_start_locked(ifp0);
	if (ifp1 != NULL && (ifp1->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
	    !IFQ_DRV_IS_EMPTY(&ifp1->if_snd))
		msk_start_locked(ifp1);

	MSK_UNLOCK(sc);
}

static void
msk_set_tx_stfwd(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	struct ifnet *ifp;

	ifp = sc_if->msk_ifp;
	sc = sc_if->msk_softc;
	if ((sc->msk_hw_id == CHIP_ID_YUKON_EX &&
	    sc->msk_hw_rev != CHIP_REV_YU_EX_A0) ||
	    sc->msk_hw_id >= CHIP_ID_YUKON_SUPR) {
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T),
		    TX_STFW_ENA);
	} else {
		if (ifp->if_mtu > ETHERMTU) {
			/* Set Tx GMAC FIFO Almost Empty Threshold. */
			CSR_WRITE_4(sc,
			    MR_ADDR(sc_if->msk_port, TX_GMF_AE_THR),
			    MSK_ECU_JUMBO_WM << 16 | MSK_ECU_AE_THR);
			/* Disable Store & Forward mode for Tx. */
			CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T),
			    TX_STFW_DIS);
		} else {
			CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T),
			    TX_STFW_ENA);
		}
	}
}

static void
msk_init(void *xsc)
{
	struct msk_if_softc *sc_if = xsc;

	MSK_IF_LOCK(sc_if);
	msk_init_locked(sc_if);
	MSK_IF_UNLOCK(sc_if);
}

static void
msk_init_locked(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	struct ifnet *ifp;
	struct mii_data	 *mii;
	uint8_t *eaddr;
	uint16_t gmac;
	uint32_t reg;
	int error;

	MSK_IF_LOCK_ASSERT(sc_if);

	ifp = sc_if->msk_ifp;
	sc = sc_if->msk_softc;
	mii = device_get_softc(sc_if->msk_miibus);

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	error = 0;
	/* Cancel pending I/O and free all Rx/Tx buffers. */
	msk_stop(sc_if);

	if (ifp->if_mtu < ETHERMTU)
		sc_if->msk_framesize = ETHERMTU;
	else
		sc_if->msk_framesize = ifp->if_mtu;
	sc_if->msk_framesize += ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN;
	if (ifp->if_mtu > ETHERMTU &&
	    (sc_if->msk_flags & MSK_FLAG_JUMBO_NOCSUM) != 0) {
		ifp->if_hwassist &= ~(MSK_CSUM_FEATURES | CSUM_TSO);
		ifp->if_capenable &= ~(IFCAP_TSO4 | IFCAP_TXCSUM);
	}

	/* GMAC Control reset. */
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, GMAC_CTRL), GMC_RST_SET);
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, GMAC_CTRL), GMC_RST_CLR);
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, GMAC_CTRL), GMC_F_LOOPB_OFF);
	if (sc->msk_hw_id == CHIP_ID_YUKON_EX ||
	    sc->msk_hw_id == CHIP_ID_YUKON_SUPR)
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, GMAC_CTRL),
		    GMC_BYP_MACSECRX_ON | GMC_BYP_MACSECTX_ON |
		    GMC_BYP_RETR_ON);

	/*
	 * Initialize GMAC first such that speed/duplex/flow-control
	 * parameters are renegotiated when interface is brought up.
	 */
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_GP_CTRL, 0);

	/* Dummy read the Interrupt Source Register. */
	CSR_READ_1(sc, MR_ADDR(sc_if->msk_port, GMAC_IRQ_SRC));

	/* Clear MIB stats. */
	msk_stats_clear(sc_if);

	/* Disable FCS. */
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_RX_CTRL, GM_RXCR_CRC_DIS);

	/* Setup Transmit Control Register. */
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_TX_CTRL, TX_COL_THR(TX_COL_DEF));

	/* Setup Transmit Flow Control Register. */
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_TX_FLOW_CTRL, 0xffff);

	/* Setup Transmit Parameter Register. */
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_TX_PARAM,
	    TX_JAM_LEN_VAL(TX_JAM_LEN_DEF) | TX_JAM_IPG_VAL(TX_JAM_IPG_DEF) |
	    TX_IPG_JAM_DATA(TX_IPG_JAM_DEF) | TX_BACK_OFF_LIM(TX_BOF_LIM_DEF));

	gmac = DATA_BLIND_VAL(DATA_BLIND_DEF) |
	    GM_SMOD_VLAN_ENA | IPG_DATA_VAL(IPG_DATA_DEF);

	if (ifp->if_mtu > ETHERMTU)
		gmac |= GM_SMOD_JUMBO_ENA;
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SERIAL_MODE, gmac);

	/* Set station address. */
	eaddr = IF_LLADDR(ifp);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SRC_ADDR_1L,
	    eaddr[0] | (eaddr[1] << 8));
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SRC_ADDR_1M,
	    eaddr[2] | (eaddr[3] << 8));
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SRC_ADDR_1H,
	    eaddr[4] | (eaddr[5] << 8));
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SRC_ADDR_2L,
	    eaddr[0] | (eaddr[1] << 8));
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SRC_ADDR_2M,
	    eaddr[2] | (eaddr[3] << 8));
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_SRC_ADDR_2H,
	    eaddr[4] | (eaddr[5] << 8));

	/* Disable interrupts for counter overflows. */
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_TX_IRQ_MSK, 0);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_RX_IRQ_MSK, 0);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_TR_IRQ_MSK, 0);

	/* Configure Rx MAC FIFO. */
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T), GMF_RST_SET);
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T), GMF_RST_CLR);
	reg = GMF_OPER_ON | GMF_RX_F_FL_ON;
	if (sc->msk_hw_id == CHIP_ID_YUKON_FE_P ||
	    sc->msk_hw_id == CHIP_ID_YUKON_EX)
		reg |= GMF_RX_OVER_ON;
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T), reg);

	/* Set receive filter. */
	msk_rxfilter(sc_if);

	if (sc->msk_hw_id == CHIP_ID_YUKON_XL) {
		/* Clear flush mask - HW bug. */
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_FL_MSK), 0);
	} else {
		/* Flush Rx MAC FIFO on any flow control or error. */
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_FL_MSK),
		    GMR_FS_ANY_ERR);
	}

	/*
	 * Set Rx FIFO flush threshold to 64 bytes + 1 FIFO word
	 * due to hardware hang on receipt of pause frames.
	 */
	reg = RX_GMF_FL_THR_DEF + 1;
	/* Another magic for Yukon FE+ - From Linux. */
	if (sc->msk_hw_id == CHIP_ID_YUKON_FE_P &&
	    sc->msk_hw_rev == CHIP_REV_YU_FE_P_A0)
		reg = 0x178;
	CSR_WRITE_2(sc, MR_ADDR(sc_if->msk_port, RX_GMF_FL_THR), reg);

	/* Configure Tx MAC FIFO. */
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T), GMF_RST_SET);
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T), GMF_RST_CLR);
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T), GMF_OPER_ON);

	/* Configure hardware VLAN tag insertion/stripping. */
	msk_setvlan(sc_if, ifp);

	if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) == 0) {
		/* Set Rx Pause threshold. */
		CSR_WRITE_2(sc, MR_ADDR(sc_if->msk_port, RX_GMF_LP_THR),
		    MSK_ECU_LLPP);
		CSR_WRITE_2(sc, MR_ADDR(sc_if->msk_port, RX_GMF_UP_THR),
		    MSK_ECU_ULPP);
		/* Configure store-and-forward for Tx. */
		msk_set_tx_stfwd(sc_if);
	}

	if (sc->msk_hw_id == CHIP_ID_YUKON_FE_P &&
	    sc->msk_hw_rev == CHIP_REV_YU_FE_P_A0) {
		/* Disable dynamic watermark - from Linux. */
		reg = CSR_READ_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_EA));
		reg &= ~0x03;
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_EA), reg);
	}

	/*
	 * Disable Force Sync bit and Alloc bit in Tx RAM interface
	 * arbiter as we don't use Sync Tx queue.
	 */
	CSR_WRITE_1(sc, MR_ADDR(sc_if->msk_port, TXA_CTRL),
	    TXA_DIS_FSYNC | TXA_DIS_ALLOC | TXA_STOP_RC);
	/* Enable the RAM Interface Arbiter. */
	CSR_WRITE_1(sc, MR_ADDR(sc_if->msk_port, TXA_CTRL), TXA_ENA_ARB);

	/* Setup RAM buffer. */
	msk_set_rambuffer(sc_if);

	/* Disable Tx sync Queue. */
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_txsq, RB_CTRL), RB_RST_SET);

	/* Setup Tx Queue Bus Memory Interface. */
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR), BMU_CLR_RESET);
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR), BMU_OPER_INIT);
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR), BMU_FIFO_OP_ON);
	CSR_WRITE_2(sc, Q_ADDR(sc_if->msk_txq, Q_WM), MSK_BMU_TX_WM);
	switch (sc->msk_hw_id) {
	case CHIP_ID_YUKON_EC_U:
		if (sc->msk_hw_rev == CHIP_REV_YU_EC_U_A0) {
			/* Fix for Yukon-EC Ultra: set BMU FIFO level */
			CSR_WRITE_2(sc, Q_ADDR(sc_if->msk_txq, Q_AL),
			    MSK_ECU_TXFF_LEV);
		}
		break;
	case CHIP_ID_YUKON_EX:
		/*
		 * Yukon Extreme seems to have silicon bug for
		 * automatic Tx checksum calculation capability.
		 */
		if (sc->msk_hw_rev == CHIP_REV_YU_EX_B0)
			CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_F),
			    F_TX_CHK_AUTO_OFF);
		break;
	}

	/* Setup Rx Queue Bus Memory Interface. */
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_rxq, Q_CSR), BMU_CLR_RESET);
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_rxq, Q_CSR), BMU_OPER_INIT);
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_rxq, Q_CSR), BMU_FIFO_OP_ON);
	CSR_WRITE_2(sc, Q_ADDR(sc_if->msk_rxq, Q_WM), MSK_BMU_RX_WM);
        if (sc->msk_hw_id == CHIP_ID_YUKON_EC_U &&
	    sc->msk_hw_rev >= CHIP_REV_YU_EC_U_A1) {
		/* MAC Rx RAM Read is controlled by hardware. */
                CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_rxq, Q_F), F_M_RX_RAM_DIS);
	}

	msk_set_prefetch(sc, sc_if->msk_txq,
	    sc_if->msk_rdata.msk_tx_ring_paddr, MSK_TX_RING_CNT - 1);
	msk_init_tx_ring(sc_if);

	/* Disable Rx checksum offload and RSS hash. */
	reg = BMU_DIS_RX_RSS_HASH;
	if ((sc_if->msk_flags & MSK_FLAG_DESCV2) == 0 &&
	    (ifp->if_capenable & IFCAP_RXCSUM) != 0)
		reg |= BMU_ENA_RX_CHKSUM;
	else
		reg |= BMU_DIS_RX_CHKSUM;
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_rxq, Q_CSR), reg);
	if (sc_if->msk_framesize > (MCLBYTES - MSK_RX_BUF_ALIGN)) {
		msk_set_prefetch(sc, sc_if->msk_rxq,
		    sc_if->msk_rdata.msk_jumbo_rx_ring_paddr,
		    MSK_JUMBO_RX_RING_CNT - 1);
		error = msk_init_jumbo_rx_ring(sc_if);
	 } else {
		msk_set_prefetch(sc, sc_if->msk_rxq,
		    sc_if->msk_rdata.msk_rx_ring_paddr,
		    MSK_RX_RING_CNT - 1);
		error = msk_init_rx_ring(sc_if);
	}
	if (error != 0) {
		device_printf(sc_if->msk_if_dev,
		    "initialization failed: no memory for Rx buffers\n");
		msk_stop(sc_if);
		return;
	}
	if (sc->msk_hw_id == CHIP_ID_YUKON_EX ||
	    sc->msk_hw_id == CHIP_ID_YUKON_SUPR) {
		/* Disable flushing of non-ASF packets. */
		CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T),
		    GMF_RX_MACSEC_FLUSH_OFF);
	}

	/* Configure interrupt handling. */
	if (sc_if->msk_port == MSK_PORT_A) {
		sc->msk_intrmask |= Y2_IS_PORT_A;
		sc->msk_intrhwemask |= Y2_HWE_L1_MASK;
	} else {
		sc->msk_intrmask |= Y2_IS_PORT_B;
		sc->msk_intrhwemask |= Y2_HWE_L2_MASK;
	}
	/* Configure IRQ moderation mask. */
	CSR_WRITE_4(sc, B2_IRQM_MSK, sc->msk_intrmask);
	if (sc->msk_int_holdoff > 0) {
		/* Configure initial IRQ moderation timer value. */
		CSR_WRITE_4(sc, B2_IRQM_INI,
		    MSK_USECS(sc, sc->msk_int_holdoff));
		CSR_WRITE_4(sc, B2_IRQM_VAL,
		    MSK_USECS(sc, sc->msk_int_holdoff));
		/* Start IRQ moderation. */
		CSR_WRITE_1(sc, B2_IRQM_CTRL, TIM_START);
	}
	CSR_WRITE_4(sc, B0_HWE_IMSK, sc->msk_intrhwemask);
	CSR_READ_4(sc, B0_HWE_IMSK);
	CSR_WRITE_4(sc, B0_IMSK, sc->msk_intrmask);
	CSR_READ_4(sc, B0_IMSK);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc_if->msk_flags &= ~MSK_FLAG_LINK;
	mii_mediachg(mii);

	callout_reset(&sc_if->msk_tick_ch, hz, msk_tick, sc_if);
}

static void
msk_set_rambuffer(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	int ltpp, utpp;

	sc = sc_if->msk_softc;
	if ((sc_if->msk_flags & MSK_FLAG_RAMBUF) == 0)
		return;

	/* Setup Rx Queue. */
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_rxq, RB_CTRL), RB_RST_CLR);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_rxq, RB_START),
	    sc->msk_rxqstart[sc_if->msk_port] / 8);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_rxq, RB_END),
	    sc->msk_rxqend[sc_if->msk_port] / 8);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_rxq, RB_WP),
	    sc->msk_rxqstart[sc_if->msk_port] / 8);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_rxq, RB_RP),
	    sc->msk_rxqstart[sc_if->msk_port] / 8);

	utpp = (sc->msk_rxqend[sc_if->msk_port] + 1 -
	    sc->msk_rxqstart[sc_if->msk_port] - MSK_RB_ULPP) / 8;
	ltpp = (sc->msk_rxqend[sc_if->msk_port] + 1 -
	    sc->msk_rxqstart[sc_if->msk_port] - MSK_RB_LLPP_B) / 8;
	if (sc->msk_rxqsize < MSK_MIN_RXQ_SIZE)
		ltpp += (MSK_RB_LLPP_B - MSK_RB_LLPP_S) / 8;
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_rxq, RB_RX_UTPP), utpp);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_rxq, RB_RX_LTPP), ltpp);
	/* Set Rx priority(RB_RX_UTHP/RB_RX_LTHP) thresholds? */

	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_rxq, RB_CTRL), RB_ENA_OP_MD);
	CSR_READ_1(sc, RB_ADDR(sc_if->msk_rxq, RB_CTRL));

	/* Setup Tx Queue. */
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_txq, RB_CTRL), RB_RST_CLR);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_txq, RB_START),
	    sc->msk_txqstart[sc_if->msk_port] / 8);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_txq, RB_END),
	    sc->msk_txqend[sc_if->msk_port] / 8);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_txq, RB_WP),
	    sc->msk_txqstart[sc_if->msk_port] / 8);
	CSR_WRITE_4(sc, RB_ADDR(sc_if->msk_txq, RB_RP),
	    sc->msk_txqstart[sc_if->msk_port] / 8);
	/* Enable Store & Forward for Tx side. */
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_txq, RB_CTRL), RB_ENA_STFWD);
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_txq, RB_CTRL), RB_ENA_OP_MD);
	CSR_READ_1(sc, RB_ADDR(sc_if->msk_txq, RB_CTRL));
}

static void
msk_set_prefetch(struct msk_softc *sc, int qaddr, bus_addr_t addr,
    uint32_t count)
{

	/* Reset the prefetch unit. */
	CSR_WRITE_4(sc, Y2_PREF_Q_ADDR(qaddr, PREF_UNIT_CTRL_REG),
	    PREF_UNIT_RST_SET);
	CSR_WRITE_4(sc, Y2_PREF_Q_ADDR(qaddr, PREF_UNIT_CTRL_REG),
	    PREF_UNIT_RST_CLR);
	/* Set LE base address. */
	CSR_WRITE_4(sc, Y2_PREF_Q_ADDR(qaddr, PREF_UNIT_ADDR_LOW_REG),
	    MSK_ADDR_LO(addr));
	CSR_WRITE_4(sc, Y2_PREF_Q_ADDR(qaddr, PREF_UNIT_ADDR_HI_REG),
	    MSK_ADDR_HI(addr));
	/* Set the list last index. */
	CSR_WRITE_2(sc, Y2_PREF_Q_ADDR(qaddr, PREF_UNIT_LAST_IDX_REG),
	    count);
	/* Turn on prefetch unit. */
	CSR_WRITE_4(sc, Y2_PREF_Q_ADDR(qaddr, PREF_UNIT_CTRL_REG),
	    PREF_UNIT_OP_ON);
	/* Dummy read to ensure write. */
	CSR_READ_4(sc, Y2_PREF_Q_ADDR(qaddr, PREF_UNIT_CTRL_REG));
}

static void
msk_stop(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	struct msk_txdesc *txd;
	struct msk_rxdesc *rxd;
	struct msk_rxdesc *jrxd;
	struct ifnet *ifp;
	uint32_t val;
	int i;

	MSK_IF_LOCK_ASSERT(sc_if);
	sc = sc_if->msk_softc;
	ifp = sc_if->msk_ifp;

	callout_stop(&sc_if->msk_tick_ch);
	sc_if->msk_watchdog_timer = 0;

	/* Disable interrupts. */
	if (sc_if->msk_port == MSK_PORT_A) {
		sc->msk_intrmask &= ~Y2_IS_PORT_A;
		sc->msk_intrhwemask &= ~Y2_HWE_L1_MASK;
	} else {
		sc->msk_intrmask &= ~Y2_IS_PORT_B;
		sc->msk_intrhwemask &= ~Y2_HWE_L2_MASK;
	}
	CSR_WRITE_4(sc, B0_HWE_IMSK, sc->msk_intrhwemask);
	CSR_READ_4(sc, B0_HWE_IMSK);
	CSR_WRITE_4(sc, B0_IMSK, sc->msk_intrmask);
	CSR_READ_4(sc, B0_IMSK);

	/* Disable Tx/Rx MAC. */
	val = GMAC_READ_2(sc, sc_if->msk_port, GM_GP_CTRL);
	val &= ~(GM_GPCR_RX_ENA | GM_GPCR_TX_ENA);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_GP_CTRL, val);
	/* Read again to ensure writing. */
	GMAC_READ_2(sc, sc_if->msk_port, GM_GP_CTRL);
	/* Update stats and clear counters. */
	msk_stats_update(sc_if);

	/* Stop Tx BMU. */
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR), BMU_STOP);
	val = CSR_READ_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR));
	for (i = 0; i < MSK_TIMEOUT; i++) {
		if ((val & (BMU_STOP | BMU_IDLE)) == 0) {
			CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR),
			    BMU_STOP);
			val = CSR_READ_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR));
		} else
			break;
		DELAY(1);
	}
	if (i == MSK_TIMEOUT)
		device_printf(sc_if->msk_if_dev, "Tx BMU stop failed\n");
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_txq, RB_CTRL),
	    RB_RST_SET | RB_DIS_OP_MD);

	/* Disable all GMAC interrupt. */
	CSR_WRITE_1(sc, MR_ADDR(sc_if->msk_port, GMAC_IRQ_MSK), 0);
	/* Disable PHY interrupt. */
	msk_phy_writereg(sc_if, PHY_ADDR_MARV, PHY_MARV_INT_MASK, 0);

	/* Disable the RAM Interface Arbiter. */
	CSR_WRITE_1(sc, MR_ADDR(sc_if->msk_port, TXA_CTRL), TXA_DIS_ARB);

	/* Reset the PCI FIFO of the async Tx queue */
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_txq, Q_CSR),
	    BMU_RST_SET | BMU_FIFO_RST);

	/* Reset the Tx prefetch units. */
	CSR_WRITE_4(sc, Y2_PREF_Q_ADDR(sc_if->msk_txq, PREF_UNIT_CTRL_REG),
	    PREF_UNIT_RST_SET);

	/* Reset the RAM Buffer async Tx queue. */
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_txq, RB_CTRL), RB_RST_SET);

	/* Reset Tx MAC FIFO. */
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, TX_GMF_CTRL_T), GMF_RST_SET);
	/* Set Pause Off. */
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, GMAC_CTRL), GMC_PAUSE_OFF);

	/*
	 * The Rx Stop command will not work for Yukon-2 if the BMU does not
	 * reach the end of packet and since we can't make sure that we have
	 * incoming data, we must reset the BMU while it is not during a DMA
	 * transfer. Since it is possible that the Rx path is still active,
	 * the Rx RAM buffer will be stopped first, so any possible incoming
	 * data will not trigger a DMA. After the RAM buffer is stopped, the
	 * BMU is polled until any DMA in progress is ended and only then it
	 * will be reset.
	 */

	/* Disable the RAM Buffer receive queue. */
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_rxq, RB_CTRL), RB_DIS_OP_MD);
	for (i = 0; i < MSK_TIMEOUT; i++) {
		if (CSR_READ_1(sc, RB_ADDR(sc_if->msk_rxq, Q_RSL)) ==
		    CSR_READ_1(sc, RB_ADDR(sc_if->msk_rxq, Q_RL)))
			break;
		DELAY(1);
	}
	if (i == MSK_TIMEOUT)
		device_printf(sc_if->msk_if_dev, "Rx BMU stop failed\n");
	CSR_WRITE_4(sc, Q_ADDR(sc_if->msk_rxq, Q_CSR),
	    BMU_RST_SET | BMU_FIFO_RST);
	/* Reset the Rx prefetch unit. */
	CSR_WRITE_4(sc, Y2_PREF_Q_ADDR(sc_if->msk_rxq, PREF_UNIT_CTRL_REG),
	    PREF_UNIT_RST_SET);
	/* Reset the RAM Buffer receive queue. */
	CSR_WRITE_1(sc, RB_ADDR(sc_if->msk_rxq, RB_CTRL), RB_RST_SET);
	/* Reset Rx MAC FIFO. */
	CSR_WRITE_4(sc, MR_ADDR(sc_if->msk_port, RX_GMF_CTRL_T), GMF_RST_SET);

	/* Free Rx and Tx mbufs still in the queues. */
	for (i = 0; i < MSK_RX_RING_CNT; i++) {
		rxd = &sc_if->msk_cdata.msk_rxdesc[i];
		if (rxd->rx_m != NULL) {
			bus_dmamap_sync(sc_if->msk_cdata.msk_rx_tag,
			    rxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc_if->msk_cdata.msk_rx_tag,
			    rxd->rx_dmamap);
			m_freem(rxd->rx_m);
			rxd->rx_m = NULL;
		}
	}
	for (i = 0; i < MSK_JUMBO_RX_RING_CNT; i++) {
		jrxd = &sc_if->msk_cdata.msk_jumbo_rxdesc[i];
		if (jrxd->rx_m != NULL) {
			bus_dmamap_sync(sc_if->msk_cdata.msk_jumbo_rx_tag,
			    jrxd->rx_dmamap, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc_if->msk_cdata.msk_jumbo_rx_tag,
			    jrxd->rx_dmamap);
			m_freem(jrxd->rx_m);
			jrxd->rx_m = NULL;
		}
	}
	for (i = 0; i < MSK_TX_RING_CNT; i++) {
		txd = &sc_if->msk_cdata.msk_txdesc[i];
		if (txd->tx_m != NULL) {
			bus_dmamap_sync(sc_if->msk_cdata.msk_tx_tag,
			    txd->tx_dmamap, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc_if->msk_cdata.msk_tx_tag,
			    txd->tx_dmamap);
			m_freem(txd->tx_m);
			txd->tx_m = NULL;
		}
	}

	/*
	 * Mark the interface down.
	 */
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc_if->msk_flags &= ~MSK_FLAG_LINK;
}

/*
 * When GM_PAR_MIB_CLR bit of GM_PHY_ADDR is set, reading lower
 * counter clears high 16 bits of the counter such that accessing
 * lower 16 bits should be the last operation.
 */
#define	MSK_READ_MIB32(x, y)					\
	(((uint32_t)GMAC_READ_2(sc, x, (y) + 4)) << 16) +	\
	(uint32_t)GMAC_READ_2(sc, x, y)
#define	MSK_READ_MIB64(x, y)					\
	(((uint64_t)MSK_READ_MIB32(x, (y) + 8)) << 32) +	\
	(uint64_t)MSK_READ_MIB32(x, y)

static void
msk_stats_clear(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	uint32_t reg;
	uint16_t gmac;
	int i;

	MSK_IF_LOCK_ASSERT(sc_if);

	sc = sc_if->msk_softc;
	/* Set MIB Clear Counter Mode. */
	gmac = GMAC_READ_2(sc, sc_if->msk_port, GM_PHY_ADDR);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_PHY_ADDR, gmac | GM_PAR_MIB_CLR);
	/* Read all MIB Counters with Clear Mode set. */
	for (i = GM_RXF_UC_OK; i <= GM_TXE_FIFO_UR; i += sizeof(uint32_t))
		reg = MSK_READ_MIB32(sc_if->msk_port, i);
	/* Clear MIB Clear Counter Mode. */
	gmac &= ~GM_PAR_MIB_CLR;
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_PHY_ADDR, gmac);
}

static void
msk_stats_update(struct msk_if_softc *sc_if)
{
	struct msk_softc *sc;
	struct ifnet *ifp;
	struct msk_hw_stats *stats;
	uint16_t gmac;
	uint32_t reg;

	MSK_IF_LOCK_ASSERT(sc_if);

	ifp = sc_if->msk_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;
	sc = sc_if->msk_softc;
	stats = &sc_if->msk_stats;
	/* Set MIB Clear Counter Mode. */
	gmac = GMAC_READ_2(sc, sc_if->msk_port, GM_PHY_ADDR);
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_PHY_ADDR, gmac | GM_PAR_MIB_CLR);

	/* Rx stats. */
	stats->rx_ucast_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_UC_OK);
	stats->rx_bcast_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_BC_OK);
	stats->rx_pause_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_MPAUSE);
	stats->rx_mcast_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_MC_OK);
	stats->rx_crc_errs +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_FCS_ERR);
	reg = MSK_READ_MIB32(sc_if->msk_port, GM_RXF_SPARE1);
	stats->rx_good_octets +=
	    MSK_READ_MIB64(sc_if->msk_port, GM_RXO_OK_LO);
	stats->rx_bad_octets +=
	    MSK_READ_MIB64(sc_if->msk_port, GM_RXO_ERR_LO);
	stats->rx_runts +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_SHT);
	stats->rx_runt_errs +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXE_FRAG);
	stats->rx_pkts_64 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_64B);
	stats->rx_pkts_65_127 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_127B);
	stats->rx_pkts_128_255 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_255B);
	stats->rx_pkts_256_511 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_511B);
	stats->rx_pkts_512_1023 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_1023B);
	stats->rx_pkts_1024_1518 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_1518B);
	stats->rx_pkts_1519_max +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_MAX_SZ);
	stats->rx_pkts_too_long +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_LNG_ERR);
	stats->rx_pkts_jabbers +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXF_JAB_PKT);
	reg = MSK_READ_MIB32(sc_if->msk_port, GM_RXF_SPARE2);
	stats->rx_fifo_oflows +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_RXE_FIFO_OV);
	reg = MSK_READ_MIB32(sc_if->msk_port, GM_RXF_SPARE3);

	/* Tx stats. */
	stats->tx_ucast_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_UC_OK);
	stats->tx_bcast_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_BC_OK);
	stats->tx_pause_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_MPAUSE);
	stats->tx_mcast_frames +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_MC_OK);
	stats->tx_octets +=
	    MSK_READ_MIB64(sc_if->msk_port, GM_TXO_OK_LO);
	stats->tx_pkts_64 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_64B);
	stats->tx_pkts_65_127 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_127B);
	stats->tx_pkts_128_255 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_255B);
	stats->tx_pkts_256_511 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_511B);
	stats->tx_pkts_512_1023 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_1023B);
	stats->tx_pkts_1024_1518 +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_1518B);
	stats->tx_pkts_1519_max +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_MAX_SZ);
	reg = MSK_READ_MIB32(sc_if->msk_port, GM_TXF_SPARE1);
	stats->tx_colls +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_COL);
	stats->tx_late_colls +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_LAT_COL);
	stats->tx_excess_colls +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_ABO_COL);
	stats->tx_multi_colls +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_MUL_COL);
	stats->tx_single_colls +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXF_SNG_COL);
	stats->tx_underflows +=
	    MSK_READ_MIB32(sc_if->msk_port, GM_TXE_FIFO_UR);
	/* Clear MIB Clear Counter Mode. */
	gmac &= ~GM_PAR_MIB_CLR;
	GMAC_WRITE_2(sc, sc_if->msk_port, GM_PHY_ADDR, gmac);
}

static int
msk_sysctl_stat32(SYSCTL_HANDLER_ARGS)
{
	struct msk_softc *sc;
	struct msk_if_softc *sc_if;
	uint32_t result, *stat;
	int off;

	sc_if = (struct msk_if_softc *)arg1;
	sc = sc_if->msk_softc;
	off = arg2;
	stat = (uint32_t *)((uint8_t *)&sc_if->msk_stats + off);

	MSK_IF_LOCK(sc_if);
	result = MSK_READ_MIB32(sc_if->msk_port, GM_MIB_CNT_BASE + off * 2);
	result += *stat;
	MSK_IF_UNLOCK(sc_if);

	return (sysctl_handle_int(oidp, &result, 0, req));
}

static int
msk_sysctl_stat64(SYSCTL_HANDLER_ARGS)
{
	struct msk_softc *sc;
	struct msk_if_softc *sc_if;
	uint64_t result, *stat;
	int off;

	sc_if = (struct msk_if_softc *)arg1;
	sc = sc_if->msk_softc;
	off = arg2;
	stat = (uint64_t *)((uint8_t *)&sc_if->msk_stats + off);

	MSK_IF_LOCK(sc_if);
	result = MSK_READ_MIB64(sc_if->msk_port, GM_MIB_CNT_BASE + off * 2);
	result += *stat;
	MSK_IF_UNLOCK(sc_if);

	return (sysctl_handle_64(oidp, &result, 0, req));
}

#undef MSK_READ_MIB32
#undef MSK_READ_MIB64

#define MSK_SYSCTL_STAT32(sc, c, o, p, n, d) 				\
	SYSCTL_ADD_PROC(c, p, OID_AUTO, o, CTLTYPE_UINT | CTLFLAG_RD, 	\
	    sc, offsetof(struct msk_hw_stats, n), msk_sysctl_stat32,	\
	    "IU", d)
#define MSK_SYSCTL_STAT64(sc, c, o, p, n, d) 				\
	SYSCTL_ADD_PROC(c, p, OID_AUTO, o, CTLTYPE_U64 | CTLFLAG_RD, 	\
	    sc, offsetof(struct msk_hw_stats, n), msk_sysctl_stat64,	\
	    "QU", d)

static void
msk_sysctl_node(struct msk_if_softc *sc_if)
{
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child, *schild;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(sc_if->msk_if_dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(sc_if->msk_if_dev));

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
	    NULL, "MSK Statistics");
	schild = SYSCTL_CHILDREN(tree);
	tree = SYSCTL_ADD_NODE(ctx, schild, OID_AUTO, "rx", CTLFLAG_RD,
	    NULL, "MSK RX Statistics");
	child = SYSCTL_CHILDREN(tree);
	MSK_SYSCTL_STAT32(sc_if, ctx, "ucast_frames",
	    child, rx_ucast_frames, "Good unicast frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "bcast_frames",
	    child, rx_bcast_frames, "Good broadcast frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "pause_frames",
	    child, rx_pause_frames, "Pause frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "mcast_frames",
	    child, rx_mcast_frames, "Multicast frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "crc_errs",
	    child, rx_crc_errs, "CRC errors");
	MSK_SYSCTL_STAT64(sc_if, ctx, "good_octets",
	    child, rx_good_octets, "Good octets");
	MSK_SYSCTL_STAT64(sc_if, ctx, "bad_octets",
	    child, rx_bad_octets, "Bad octets");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_64",
	    child, rx_pkts_64, "64 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_65_127",
	    child, rx_pkts_65_127, "65 to 127 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_128_255",
	    child, rx_pkts_128_255, "128 to 255 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_256_511",
	    child, rx_pkts_256_511, "256 to 511 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_512_1023",
	    child, rx_pkts_512_1023, "512 to 1023 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_1024_1518",
	    child, rx_pkts_1024_1518, "1024 to 1518 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_1519_max",
	    child, rx_pkts_1519_max, "1519 to max frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_too_long",
	    child, rx_pkts_too_long, "frames too long");
	MSK_SYSCTL_STAT32(sc_if, ctx, "jabbers",
	    child, rx_pkts_jabbers, "Jabber errors");
	MSK_SYSCTL_STAT32(sc_if, ctx, "overflows",
	    child, rx_fifo_oflows, "FIFO overflows");

	tree = SYSCTL_ADD_NODE(ctx, schild, OID_AUTO, "tx", CTLFLAG_RD,
	    NULL, "MSK TX Statistics");
	child = SYSCTL_CHILDREN(tree);
	MSK_SYSCTL_STAT32(sc_if, ctx, "ucast_frames",
	    child, tx_ucast_frames, "Unicast frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "bcast_frames",
	    child, tx_bcast_frames, "Broadcast frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "pause_frames",
	    child, tx_pause_frames, "Pause frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "mcast_frames",
	    child, tx_mcast_frames, "Multicast frames");
	MSK_SYSCTL_STAT64(sc_if, ctx, "octets",
	    child, tx_octets, "Octets");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_64",
	    child, tx_pkts_64, "64 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_65_127",
	    child, tx_pkts_65_127, "65 to 127 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_128_255",
	    child, tx_pkts_128_255, "128 to 255 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_256_511",
	    child, tx_pkts_256_511, "256 to 511 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_512_1023",
	    child, tx_pkts_512_1023, "512 to 1023 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_1024_1518",
	    child, tx_pkts_1024_1518, "1024 to 1518 bytes frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "frames_1519_max",
	    child, tx_pkts_1519_max, "1519 to max frames");
	MSK_SYSCTL_STAT32(sc_if, ctx, "colls",
	    child, tx_colls, "Collisions");
	MSK_SYSCTL_STAT32(sc_if, ctx, "late_colls",
	    child, tx_late_colls, "Late collisions");
	MSK_SYSCTL_STAT32(sc_if, ctx, "excess_colls",
	    child, tx_excess_colls, "Excessive collisions");
	MSK_SYSCTL_STAT32(sc_if, ctx, "multi_colls",
	    child, tx_multi_colls, "Multiple collisions");
	MSK_SYSCTL_STAT32(sc_if, ctx, "single_colls",
	    child, tx_single_colls, "Single collisions");
	MSK_SYSCTL_STAT32(sc_if, ctx, "underflows",
	    child, tx_underflows, "FIFO underflows");
}

#undef MSK_SYSCTL_STAT32
#undef MSK_SYSCTL_STAT64

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
sysctl_hw_msk_proc_limit(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req, MSK_PROC_MIN,
	    MSK_PROC_MAX));
}
